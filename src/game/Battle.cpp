#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include "../text/TextEncoding.h"
#include "BattleCommands.h"
#include "BattleTiming.h"
#include <cstdio>
#include <stdexcept>

using namespace Protocol;

namespace
{
    std::uint32_t BattleNumber(const std::string& value)
    {
        if (value.empty() || value.size() > 8 ||
            value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error("Invalid battle number");
        return static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
    }
}

bool LoginClient::HandleBattlePacket(const std::vector<std::string>& fields)
{
    auto strings = ReceiveStrings(fields);
    if (strings.size() != 1)
        throw std::runtime_error("Invalid battle reply");
    auto values = SplitLegacy(strings[0]);
    if (values[0] == "BU")
    {
        FinishBattle();
        return true;
    }
    if (values[0] == "BP")
    {
        if (values.size() != 4)
            throw std::runtime_error("Invalid battle position");
        auto slot = BattleNumber(values[1]);
        if (slot > 20)
            throw std::runtime_error("Invalid battle player slot");
        auto flags = BattleNumber(values[2]);
        battle_.playerSlot = static_cast<int>(slot);
        battle_.menuFlags = flags;
        battle_.commandsPending = false;
        battle_.roundReceived = false;
        return true;
    }
    if (values[0] == "BA")
    {
        if (values.size() != 4 || !values.back().empty())
            throw std::runtime_error("Invalid battle round");
        auto round = BattleNumber(values[2]);
        if (round >= INT32_MAX)
            throw std::runtime_error("Invalid battle round number");
        auto readyMask = BattleNumber(values[1]);
        battle_.round = static_cast<int>(round) + 1;
        battle_.readyMask = readyMask;
        battle_.roundReceived = true;
        return true;
    }
    if (values[0] != "BC")
        return !healing_.quickBattle;
    // BC carries a field attribute, then thirteen fields per occupied battle slot.
    if (values.size() < 3 || !values.back().empty() || (values.size() - 3) % 13 != 0)
        throw std::runtime_error("Incomplete battle members");
    auto updated = battle_;
    BattleNumber(values[1]);
    updated.members = {};
    for (std::size_t i = 2; i + 13 < values.size(); i += 13)
    {
        auto slot = BattleNumber(values[i]);
        if (slot >= updated.members.size() || updated.members[slot].present)
            throw std::runtime_error("Invalid battle member slot");
        auto& member = updated.members[slot];
        member.name = UnescapeLegacy(values[i + 1]);
        member.image = BattleNumber(values[i + 3]);
        member.level = static_cast<int>(BattleNumber(values[i + 4]));
        member.hp = static_cast<int>(BattleNumber(values[i + 5]));
        member.maxHp = static_cast<int>(BattleNumber(values[i + 6]));
        member.flags = BattleNumber(values[i + 7]);
        if (BattleNumber(values[i + 8]) == 1)
        {
            member.rideName = UnescapeLegacy(values[i + 9]);
            member.rideLevel = static_cast<int>(BattleNumber(values[i + 10]));
            member.rideHp = static_cast<int>(BattleNumber(values[i + 11]));
            member.rideMaxHp = static_cast<int>(BattleNumber(values[i + 12]));
            if (member.rideLevel < 0 || member.rideHp < 0 || member.rideMaxHp < 0)
                throw std::runtime_error("Invalid battle ride status");
        }
        if (member.level < 0 || member.hp < 0 || member.maxHp < 0)
            throw std::runtime_error("Invalid battle member status");
        member.present = true;
    }
    updated.received = true;
    updated.commandsPending = true;
    updated.roundReceived = false;
    updated.nextAction = GetTickCount64() + (healing_.quickBattle ? Game::BATTLE_DELAY_MS : Game::SIMPLE_BATTLE_DELAY_MS);
    battle_ = updated;
    battleResult_.message = L"等待本回合指令";
    return true;
}

bool LoginClient::HandleBattleResult(const std::vector<std::string>& fields)
{
    auto strings = ReceiveStrings(fields);
    if (strings.size() != 1)
        throw std::runtime_error("Invalid battle result");
    // RS starts with the character's slot (-2), level-up flag and base-62 experience.
    auto end = strings[0].find(',');
    if (end == std::string::npos)
        throw std::runtime_error("Incomplete battle result");
    auto values = SplitLegacy(strings[0].substr(0, end));
    if (values.size() != 3 || values[0] != "-2" ||
        (values[1] != "0" && values[1] != "1"))
        throw std::runtime_error("Invalid character battle result");
    auto experience = ParseMask(values[2]);
    battleResult_.received = true;
    battleResult_.experience = experience;
    battleResult_.totalExperience += experience;
    battleResult_.message = L"战斗结束";
    return true;
}

void LoginClient::FinishBattle()
{
    if (!playerStatus_.inBattle || battle_.endPending)
        return;
    battle_.endPending = true;
    battle_.commandsPending = false;
    battle_.nextAction = GetTickCount64() + (healing_.quickBattle ? Game::BATTLE_DELAY_MS : 0);
}

bool LoginClient::PollBattle()
{
    if (!playerStatus_.inBattle || GetTickCount64() < battle_.nextAction)
        return false;
    if (battle_.endPending)
    {
        // STW acknowledges the post-battle XYD before resuming encounters.
        SendNumbers("8", { 0 }, GetTickCount64() + Game::BATTLE_DELAY_MS);
        playerStatus_.inBattle = false;
        battle_ = {};
        encounter_.nextRequest = GetTickCount64() + Game::BATTLE_DELAY_MS;
        return true;
    }
    int player = battle_.playerSlot;
    if (!battle_.commandsPending || !battle_.roundReceived || !battle_.received ||
        battle_.round <= battle_.sentRound || player < 0 || player >= 20 || player % 10 >= 5)
        return false;
    int enemyOffset = player < 10 ? 10 : 0;
    int target = -1;
    for (int slot = enemyOffset; slot < enemyOffset + 10; ++slot)
    {
        const auto& member = battle_.members[slot];
        if (member.present && member.hp > 0 &&
            !(member.flags & (Game::BATTLE_MEMBER_DEAD | Game::BATTLE_MEMBER_HIDDEN)))
        {
            target = slot;
            break;
        }
    }
    std::string packets;
    auto append = [&](const std::string& command)
    {
        packets += EncodePacket("&;14;" + Encode64(command, sessionKey_, -1) + ";" +
            EncodeInteger(static_cast<std::uint32_t>(command.size()), sessionKey_) + ";#;");
    };
    char command[32];
    auto healingTarget = [&](int percent, int targetType)
    {
        for (int slot : { player, player + 5 })
        {
            const auto& member = battle_.members[slot];
            if (!member.present || member.hp <= 0 || member.maxHp <= 0 ||
                (member.flags & (Game::BATTLE_MEMBER_DEAD | Game::BATTLE_MEMBER_HIDDEN)))
                continue;
            bool needsHealing = static_cast<std::int64_t>(member.hp) * 100 <
                static_cast<std::int64_t>(member.maxHp) * percent;
            bool rideNeedsHealing = slot == player && member.rideHp > 0 && member.rideMaxHp > 0 &&
                static_cast<std::int64_t>(member.rideHp) * 100 <
                    static_cast<std::int64_t>(member.rideMaxHp) * percent;
            if (!needsHealing && !rideNeedsHealing)
                continue;
            if (targetType == 0 && slot != player)
                continue;
            if (targetType == 6 && slot == player)
                continue;
            if (targetType == 0 || targetType == 1 || targetType == 2 || targetType == 6)
                return slot;
            if (targetType == 8)
                return player < 10 ? 20 : 21;
        }
        return -1;
    };
    auto heal = [&]()
    {
        if (healing_.useItems && itemsReceived_)
        {
            for (std::size_t slot = 5; slot < items_.size(); ++slot)
            {
                const auto& item = items_[slot];
                if (!item.present || item.useField == 2 || item.target < 0 || item.target > 2)
                    continue;
                auto description = TextEncoding::LegacyText(item.description);
                auto hp = description.find(L"耐久力");
                if (hp == std::wstring::npos || description.find(L'-', hp) != std::wstring::npos)
                    continue;
                int healTarget = healingTarget(healing_.itemPercent, item.target);
                if (healTarget < 0)
                    continue;
                std::snprintf(command, sizeof(command), "I|%X|%X", static_cast<unsigned>(slot), healTarget);
                append(command);
                return true;
            }
        }
        if (healing_.useMagic)
        {
            int slot = healing_.magicSlot;
            if (slot < 0 || slot >= static_cast<int>(magic_.size()) ||
                !magic_[slot].present || magic_[slot].name != healing_.magicName)
            {
                slot = -1;
                for (std::size_t i = 0; i < magic_.size(); ++i)
                    if (magic_[i].present && magic_[i].name == healing_.magicName)
                    {
                        slot = static_cast<int>(i);
                        break;
                    }
            }
            if (slot >= 0)
            {
                const auto& magic = magic_[slot];
                int healTarget = healingTarget(healing_.magicPercent, magic.target);
                if (magic.received && magic.present && magic.useField != 2 &&
                    magic.mp >= 0 && playerStatus_.mp >= magic.mp && healTarget >= 0)
                {
                    std::snprintf(command, sizeof(command), "J|%X|%X", slot, healTarget);
                    append(command);
                    return true;
                }
            }
        }
        return false;
    };
    const auto& character = battle_.members[player];
    if (!(battle_.readyMask & (std::uint32_t{1} << player)))
    {
        if (target >= 0 && character.present && character.hp > 0 &&
            !(battle_.menuFlags & Game::BATTLE_PLAYER_MENU_OFF))
        {
            if (!heal())
            {
                std::snprintf(command, sizeof(command), "H|%X", target);
                append(command);
            }
        }
        else
            append("G");
    }
    int petSlot = player + 5;
    const auto& pet = battle_.members[petSlot];
    if (!(battle_.readyMask & (std::uint32_t{1} << petSlot)))
    {
        if (!pet.present)
            append("W|FF|FF");
        else if (target < 0 || pet.hp <= 0 || (battle_.menuFlags & Game::BATTLE_PET_MENU_OFF))
            append("W|1|FF");
        else
        {
            std::snprintf(command, sizeof(command), "W|0|%X", target);
            append(command);
        }
    }
    if (!packets.empty())
        connection_.Send(packets, GetTickCount64() + Game::BATTLE_DELAY_MS);
    battleResult_.message = packets.empty() ? L"等待回合结束" : L"已提交本回合指令";
    battle_.sentRound = battle_.round;
    battle_.commandsPending = false;
    return true;
}
