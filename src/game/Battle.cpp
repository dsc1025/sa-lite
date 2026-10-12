#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
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
        return false;
    // BC carries a field attribute, then thirteen fields per occupied battle slot.
    if (values.size() < 3 || !values.back().empty() || (values.size() - 3) % 13 != 0)
        throw std::runtime_error("Incomplete battle members");
    BattleNumber(values[1]);
    auto updated = battle_;
    updated.members = {};
    for (std::size_t i = 2; i + 13 < values.size(); i += 13)
    {
        auto slot = BattleNumber(values[i]);
        if (slot >= updated.members.size() || updated.members[slot].present)
            throw std::runtime_error("Invalid battle member slot");
        auto& member = updated.members[slot];
        member.name = UnescapeLegacy(values[i + 1]);
        member.level = static_cast<int>(BattleNumber(values[i + 4]));
        member.hp = static_cast<int>(BattleNumber(values[i + 5]));
        member.maxHp = static_cast<int>(BattleNumber(values[i + 6]));
        member.flags = BattleNumber(values[i + 7]);
        if (member.level < 0 || member.hp < 0 || member.maxHp < 0)
            throw std::runtime_error("Invalid battle member status");
        member.present = true;
    }
    updated.received = true;
    updated.commandsPending = true;
    updated.roundReceived = false;
    updated.nextAction = GetTickCount64() + BATTLE_DELAY_MS;
    battle_ = updated;
    return true;
}

void LoginClient::FinishBattle()
{
    if (!playerStatus_.inBattle || battle_.endPending)
        return;
    battle_.endPending = true;
    battle_.commandsPending = false;
    battle_.nextAction = GetTickCount64() + BATTLE_DELAY_MS;
}

bool LoginClient::PollBattle()
{
    if (!playerStatus_.inBattle || GetTickCount64() < battle_.nextAction)
        return false;
    if (battle_.endPending)
    {
        // STW acknowledges the post-battle XYD before resuming encounters.
        SendNumbers("8", { 0 }, GetTickCount64() + BATTLE_DELAY_MS);
        playerStatus_.inBattle = false;
        battle_ = {};
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
            !(member.flags & (BATTLE_MEMBER_DEAD | BATTLE_MEMBER_HIDDEN)))
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
    const auto& character = battle_.members[player];
    if (!(battle_.readyMask & (std::uint32_t{1} << player)))
    {
        if (target >= 0 && character.present && character.hp > 0 &&
            !(battle_.menuFlags & BATTLE_PLAYER_MENU_OFF))
        {
            std::snprintf(command, sizeof(command), "H|%X", target);
            append(command);
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
        else if (target < 0 || pet.hp <= 0 || (battle_.menuFlags & BATTLE_PET_MENU_OFF))
            append("W|1|FF");
        else
        {
            std::snprintf(command, sizeof(command), "W|0|%X", target);
            append(command);
        }
    }
    if (!packets.empty())
        connection_.Send(packets, GetTickCount64() + BATTLE_DELAY_MS);
    battle_.sentRound = battle_.round;
    battle_.commandsPending = false;
    return false;
}

void LoginClient::SendEncounterWalk(const std::string& directions, int x, int y)
{
    std::uint32_t checksum = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) +
        static_cast<std::uint32_t>(directions.size());
    connection_.Send(EncodePacket("&;1;" + EncodeInteger(x, sessionKey_) + ";" +
        EncodeInteger(y, sessionKey_) + ";" + Encode64(directions, sessionKey_, -1) + ";" +
        EncodeInteger(checksum, sessionKey_) + ";#;"), GetTickCount64() + BATTLE_DELAY_MS);
}

void LoginClient::SetEncounterEnabled(bool enabled)
{
    if (encounter_.enabled == enabled && !encounter_.stopPending)
        return;
    if (!enabled)
    {
        encounter_.enabled = false;
        encounter_.stopPending = true;
    }
    if (!inGame_)
        throw std::logic_error("尚未进入游戏，无法设置遇敌");
    RefreshPosition(GetTickCount64() + 2000);
    if (playerStatus_.mapId < 0 || playerStatus_.mapX < 0 || playerStatus_.mapY < 0)
        throw std::runtime_error("尚未收到有效地图位置，无法设置遇敌");
    if (enabled)
    {
        encounter_.map = playerStatus_.mapId;
        encounter_.x = playerStatus_.mapX;
        encounter_.y = playerStatus_.mapY;
        encounter_.nextRequest = 0;
        encounter_.enabled = true;
        encounter_.stopPending = false;
    }
    else if (!playerStatus_.inBattle)
    {
        SendEncounterWalk("", playerStatus_.mapX, playerStatus_.mapY);
        encounter_.stopPending = false;
    }
}

void LoginClient::PollEncounter()
{
    if (encounter_.stopPending)
    {
        if (!playerStatus_.inBattle)
            SetEncounterEnabled(false);
        return;
    }
    if (!encounter_.enabled || playerStatus_.inBattle || GetTickCount64() < encounter_.nextRequest)
        return;
    if (playerStatus_.mapId != encounter_.map)
    {
        SetEncounterEnabled(false);
        throw std::runtime_error("地图已改变，快速遇敌已停止");
    }
    // STW repeats the same W2 origin with ten pairs of opposite directions.
    SendEncounterWalk("gcgcgcgcgcgcgcgcgcgc", encounter_.x, encounter_.y);
    encounter_.nextRequest = GetTickCount64() + BATTLE_DELAY_MS;
}
