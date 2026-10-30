#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include "BattleTiming.h"
#include <stdexcept>
#include <utility>

using namespace Protocol;

namespace
{
    void ApplyStatus(const std::vector<std::string>& values, std::size_t start, std::uint32_t mask,
        const std::vector<int*>& numbers, std::string* name = nullptr, std::string* nickname = nullptr,
        std::int64_t* experience = nullptr, std::int64_t* nextExperience = nullptr, std::size_t experienceBit = 0)
    {
        for (std::size_t bit = 1; bit <= 31; ++bit)
        {
            if (!(mask & (std::uint32_t{1} << bit)))
                continue;
            if (start >= values.size())
                throw std::runtime_error("Incomplete status update");
            if (experience && bit == experienceBit)
            {
                auto value = ParseNumber64(values[start]);
                if (value < 0) throw std::runtime_error("Invalid current experience");
                *experience = value;
            }
            else if (nextExperience && bit == experienceBit + 1)
            {
                auto value = ParseNumber64(values[start]);
                if (value < -1) throw std::runtime_error("Invalid next experience");
                *nextExperience = value;
            }
            else if (bit <= numbers.size() && numbers[bit - 1])
                *numbers[bit - 1] = ParseNumber(values[start]);
            else if (name && bit == numbers.size() + 1)
                *name = UnescapeLegacy(values[start]);
            else if (nickname && bit == numbers.size() + 2)
                *nickname = UnescapeLegacy(values[start]);
            ++start;
        }
    }

    void ApplyExtra(const std::vector<std::string>& values, int& perception, int& rebirth)
    {
        // STW1 is appended after the legacy fields, including names.
        for (std::size_t end = values.size(); end >= 3; --end)
            if (values[end - 3] == "STW1")
            {
                perception = ParseNumber(values[end - 2]);
                rebirth = ParseNumber(values[end - 1]);
                break;
            }
    }

    LoginClient::ItemStatus ParseItem(const std::vector<std::string>& values, std::size_t start)
    {
        LoginClient::ItemStatus item;
        if (values[start].empty())
            return item;
        item.present = true;
        item.name = UnescapeLegacy(values[start]);
        item.description = UnescapeLegacy(values[start + 3]);
        item.level = ParseNumber(values[start + 7]);
        item.levelKnown = true;
        item.useField = ParseNumber(values[start + 5]);
        item.target = ParseNumber(values[start + 6]);
        int flags = ParseNumber(values[start + 8]);
        // Bit 6 marks an exact type in bits 8-30; older servers only send legacy flags.
        if (flags >= 0 && (flags & 64)) item.type = flags >> 8;
        else if (flags & 4) item.type = 20;
        else if (flags & 8) item.type = 21;
        else if (flags & 16) item.type = 22;
        return item;
    }
}

void LoginClient::IncreaseAttribute(int attribute)
{
    if (!inGame_ || !playerStatus_.received || playerStatus_.inBattle ||
        IsEncounterEnabled() || IsEncounterStopping() || !playerStatus_.remainingPointsReceived ||
        playerStatus_.remainingPoints <= 0 || attribute < 0 || attribute >= 4)
        throw std::runtime_error("当前状态无法加点");
    int* attributes[] = { &playerStatus_.vital, &playerStatus_.strength,
        &playerStatus_.toughness, &playerStatus_.dexterity };
    int before = *attributes[attribute];
    int points = playerStatus_.remainingPoints;
    auto deadline = GetTickCount64() + 2000;
    SendNumbers("64", { attribute }, deadline);
    while (*attributes[attribute] == before && GetTickCount64() < deadline)
        PollGame(100);
    if (*attributes[attribute] <= before)
    {
        playerStatus_.remainingPointsReceived = false;
        throw std::runtime_error("服务器未确认加点，请重新登录确认剩余点数");
    }
    if (playerStatus_.remainingPoints == points)
        --playerStatus_.remainingPoints;
}

bool LoginClient::HandleGamePacket(const std::vector<std::string>& fields)
{
    if (fields[1] == "24")
    {
        if (fields.size() != 6)
            throw std::runtime_error("Invalid item move reply");
        auto from = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
        auto to = ReadInteger(Decode64(fields[3], sessionKey_, -1), { 2, 4, 1, 3 });
        auto checksum = ReadInteger(Decode64(fields[4], sessionKey_, -1), { 2, 4, 1, 3 });
        if (checksum != from + to)
            throw std::runtime_error("Invalid item move checksum");
        if (from == static_cast<std::uint32_t>(-1) && to == static_cast<std::uint32_t>(-1))
        {
            itemMoveFrom_ = itemMoveTo_ = -1;
            ++itemMoveSerial_;
            return true;
        }
        if (from >= items_.size() || to >= items_.size() || from == to)
            throw std::runtime_error("Invalid item move slots");
        std::swap(items_[from], items_[to]);
        items_[from].revision = ++itemRevision_;
        items_[to].revision = ++itemRevision_;
        itemMoveFrom_ = static_cast<int>(from);
        itemMoveTo_ = static_cast<int>(to);
        ++itemMoveSerial_;
        return true;
    }
    if (fields[1] == "63")
    {
        if (fields.size() != 5)
            throw std::runtime_error("Invalid skill points reply");
        auto points = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
        auto checksum = ReadInteger(Decode64(fields[3], sessionKey_, -1), { 2, 4, 1, 3 });
        if (checksum != points)
            throw std::runtime_error("Invalid skill points checksum");
        playerStatus_.remainingPoints = static_cast<int>(points);
        playerStatus_.remainingPointsReceived = true;
        return true;
    }
    if (fields[1] == "119")
        return HandleEncounterReply(fields);
    if (fields[1] == "127")
        return HandleOfflineResult(fields);
    if (fields[1] == "12")
        return HandleBattleResult(fields);
    if (fields[1] == "121")
        return HandleBattlePacket(fields);
    if (fields[1] == "2" || fields[1] == "7")
        return HandlePositionPacket(fields);
    if (fields[1] == "36")
        return HandleServerMessage(fields);
    if (fields[1] == "55" || fields[1] == "115")
        return HandlePetStateReply(fields);
    if (fields[1] != "46" && fields[1] != "88" && fields[1] != "22")
        return false;
    auto strings = ReceiveStrings(fields);
    if (strings.size() != 1)
        throw std::runtime_error("Invalid status or heartbeat reply");
    if (fields[1] == "88" || strings[0].empty())
        return false;
    const auto& data = strings[0];
    if (fields[1] == "22" || data[0] == 'I')
        return ApplyItemStatus(data, fields[1] == "22");
    if (data.compare(0, 3, "WH|") == 0)
        return false;
    auto values = SplitLegacy(data);
    if (data[0] == 'C')
        return ApplyMapStatus(values);
    if (data[0] == 'P')
        return ApplyPlayerStatus(values);
    if (data[0] == 'J')
        return ApplyMagicStatus(values);
    if (data[0] == 'W')
        return ApplyPetSkillStatus(values);
    if (data[0] == 'K')
        return ApplyPetStatus(values);
    return false;
}

bool LoginClient::HandlePositionPacket(const std::vector<std::string>& fields)
{
    bool position = fields[1] == "2";
    if (fields.size() != (position ? 7 : 6))
        throw std::runtime_error("Invalid position or battle reply");
    std::uint32_t numbers[3]{}, checksum = 0;
    for (int i = 0; i < (position ? 3 : 2); ++i)
    {
        numbers[i] = ReadInteger(Decode64(fields[2 + i], sessionKey_, -1), { 2, 4, 1, 3 });
        checksum += numbers[i];
    }
    if (checksum != ReadInteger(Decode64(fields[fields.size() - 2], sessionKey_, -1), { 2, 4, 1, 3 }))
        throw std::runtime_error("Invalid position or battle checksum");
    if (position)
    {
        playerStatus_.mapX = static_cast<int>(numbers[0]);
        playerStatus_.mapY = static_cast<int>(numbers[1]);
        if (playerStatus_.inBattle)
            FinishBattle();
    }
    else
    {
        bool inBattle = numbers[0] != 0;
        if (!inBattle && playerStatus_.inBattle)
            encounter_.nextRequest = GetTickCount64() + Game::BATTLE_DELAY_MS;
        if (!inBattle || !playerStatus_.inBattle)
            battle_ = {};
        if (inBattle && encounter_.requestPending)
            encounter_.battleStarted = true;
        playerStatus_.inBattle = inBattle;
    }
    return true;
}

bool LoginClient::HandleServerMessage(const std::vector<std::string>& fields)
{
    if (fields.size() != 7)
        throw std::runtime_error("Invalid server message");
    auto sender = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
    auto message = Decode64(fields[3], sessionKey_, 1);
    auto color = ReadInteger(Decode64(fields[4], sessionKey_, -1), { 2, 4, 1, 3 });
    auto checksum = ReadInteger(Decode64(fields[5], sessionKey_, -1), { 2, 4, 1, 3 });
    if (checksum != sender + static_cast<std::uint32_t>(message.size()) + color)
        throw std::runtime_error("Invalid server message checksum");
    auto body = message.size() >= 2 && message[1] == '|' ? message.substr(2) : message;
    chatMessages_.push_back(UnescapeLegacy(body));
    if (chatMessages_.size() > 500)
        chatMessages_.erase(chatMessages_.begin());
    if (sender == static_cast<std::uint32_t>(-1) && message.compare(0, 2, "P|") == 0)
    {
        auto text = UnescapeLegacy(message.substr(2));
        if (waitingFood_) foodMessage_ = text;
        int length = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        if (length > 0)
            MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
        if (wide.find(L"骑") != std::wstring::npos || wide.find(L"忠诚") != std::wstring::npos ||
            wide.find(L"模型不匹配") != std::wstring::npos)
            rideError_ = text;
    }
    return true;
}

bool LoginClient::HandlePetStateReply(const std::vector<std::string>& fields)
{
    if (fields.size() != 6)
        throw std::runtime_error("Invalid pet state reply");
    auto slot = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
    auto result = ReadInteger(Decode64(fields[3], sessionKey_, -1), { 2, 4, 1, 3 });
    auto checksum = ReadInteger(Decode64(fields[4], sessionKey_, -1), { 2, 4, 1, 3 });
    if (checksum != slot + result)
        throw std::runtime_error("Invalid pet state checksum");
    if (fields[1] == "55")
    {
        ++battleReplySerial_;
        battleReplyAccepted_ = result != 0;
    }
    if (!result)
        return false;
    if (fields[1] == "115")
    {
        if (slot & ~((std::uint32_t{1} << pets_.size()) - 1))
            throw std::runtime_error("Invalid standby pet mask");
        playerStatus_.standbyPets = slot;
        playerStatus_.standbyReceived = true;
        return true;
    }
    if (slot != static_cast<std::uint32_t>(-1) && slot >= pets_.size())
        throw std::runtime_error("Invalid battle pet slot");
    playerStatus_.battlePet = slot == static_cast<std::uint32_t>(-1) ? -1 : static_cast<int>(slot);
    playerStatus_.battleReceived = true;
    return true;
}

bool LoginClient::ApplyItemStatus(const std::string& data, bool partial)
{
    auto values = SplitLegacy(partial ? data : data.substr(1));
    auto updated = partial ? items_ : std::vector<ItemStatus>(20);
    std::size_t width = partial ? 10 : 9;
    if (values.size() % width != 0 || (!partial && values.size() != updated.size() * width))
        throw std::runtime_error("Incomplete item status");
    for (std::size_t i = 0; i < values.size(); i += width)
    {
        int slot = partial ? ParseNumber(values[i]) : static_cast<int>(i / width);
        if (slot < 0 || static_cast<std::size_t>(slot) >= updated.size())
            throw std::runtime_error("Invalid item slot");
        updated[slot] = ParseItem(values, i + (partial ? 1 : 0));
        updated[slot].revision = ++itemRevision_;
    }
    items_ = updated;
    if (!partial)
        itemsReceived_ = true;
    return true;
}

bool LoginClient::ApplyMagicStatus(const std::vector<std::string>& values)
{
    if (values.size() < 2)
        throw std::runtime_error("Incomplete magic status");
    int slot = ParseNumber(values[0].substr(1));
    if (slot < 0 || static_cast<std::size_t>(slot) >= magic_.size())
        throw std::runtime_error("Invalid magic slot");
    Game::MagicStatus magic;
    magic.received = true;
    if (values[1] == "1")
    {
        if (values.size() != 8 || !values.back().empty())
            throw std::runtime_error("Incomplete magic status");
        magic.present = true;
        magic.mp = ParseNumber(values[2]);
        magic.useField = ParseNumber(values[3]);
        magic.target = ParseNumber(values[4]);
        magic.name = UnescapeLegacy(values[5]);
        magic.description = UnescapeLegacy(values[6]);
    }
    else if (values[1] != "0" || values.size() != 3 || !values.back().empty())
        throw std::runtime_error("Invalid magic status");
    magic_[slot] = magic;
    return true;
}

bool LoginClient::ApplyMapStatus(const std::vector<std::string>& values)
{
    if (values.size() != 5)
        throw std::runtime_error("Invalid map status");
    auto status = playerStatus_;
    status.mapId = ParseNumber(values[0].substr(1));
    status.mapX = ParseNumber(values[3]);
    status.mapY = ParseNumber(values[4]);
    playerStatus_ = status;
    ++positionSerial_;
    return true;
}

bool LoginClient::ApplyPlayerStatus(const std::vector<std::string>& values)
{
    auto status = playerStatus_;
    std::vector<int*> numbers = { &status.hp, &status.maxHp, &status.mp, &status.maxMp,
        &status.vital, &status.strength, &status.toughness, &status.dexterity,
        nullptr, nullptr, &status.level, &status.attack,
        &status.defence, &status.quick, &status.charm, &status.luck,
        &status.earth, &status.water, &status.fire, &status.wind, &status.gold,
        &status.title, &status.duelPoints, &status.rebirth, nullptr, nullptr, &status.ridePet };
    bool full = values[0] == "P1";
    ApplyStatus(values, 1, full ? 0x01fffffe : ParseMask(values[0].substr(1)), numbers,
        nullptr, nullptr, &status.experience, &status.nextExperience, 9);
    if (full && values.size() > 25)
        status.ridePet = ParseNumber(values[25]);
    ApplyExtra(values, status.perception, status.rebirth);
    for (std::size_t i = 1; i + 2 < values.size(); ++i)
        if (values[i] == "TW1" && values[i + 2] == "STW1")
        {
            const auto& progress = values[i + 1];
            if (progress.size() < 5 || progress.size() > 12 ||
                progress.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("Invalid tower progress");
            status.towerFloor = ParseNumber(progress.substr(0, progress.size() - 4));
            status.towerStage = ParseNumber(progress.substr(progress.size() - 4, 2));
            status.towerReceived = true;
            break;
        }
    if (full)
        status.received = true;
    int balance = pearlBalance_;
    for (std::size_t end = values.size(); end >= 2; --end)
        if (values[end - 2] == "VP1")
        {
            balance = ParseNumber(values[end - 1]);
            break;
        }
    playerStatus_ = status;
    pearlBalance_ = balance;
    return true;
}

bool LoginClient::ApplyPetSkillStatus(const std::vector<std::string>& values)
{
    if (values.size() < 2 || !values.back().empty() || (values.size() - 2) % 5 != 0)
        throw std::runtime_error("Invalid pet skill status");
    int slot = ParseNumber(values[0].substr(1));
    if (slot < 0 || static_cast<std::size_t>(slot) >= pets_.size())
        throw std::runtime_error("Invalid pet slot");
    std::vector<Game::PetSkillStatus> skills((values.size() - 2) / 5);
    for (std::size_t i = 0; i < skills.size(); ++i)
    {
        std::size_t start = 1 + i * 5;
        if (values[start].empty())
            continue;
        auto& skill = skills[i];
        skill.present = true;
        skill.id = ParseNumber(values[start]);
        skill.useField = ParseNumber(values[start + 1]);
        skill.target = ParseNumber(values[start + 2]);
        skill.name = UnescapeLegacy(values[start + 3]);
        skill.description = UnescapeLegacy(values[start + 4]);
    }
    pets_[slot].skills = std::move(skills);
    pets_[slot].skillsReceived = true;
    return true;
}

bool LoginClient::ApplyPetStatus(const std::vector<std::string>& values)
{
    if (values.size() < 2)
        throw std::runtime_error("Incomplete pet status");
    int slot = ParseNumber(values[0].substr(1));
    if (slot < 0 || static_cast<std::size_t>(slot) >= pets_.size())
        throw std::runtime_error("Invalid pet slot");
    if (values[1] == "0")
    {
        pets_[slot] = {};
        pets_[slot].received = true;
        captureNames_[slot] = {};
        return true;
    }
    auto pet = values[1] == "1" ? PetStatus{} : pets_[slot];
    std::vector<int*> numbers = { &pet.image, &pet.hp, &pet.maxHp, &pet.mp, &pet.maxMp,
        nullptr, nullptr, &pet.level, &pet.attack, &pet.defence,
        &pet.quick, &pet.loyalty, &pet.earth, &pet.water, &pet.fire, &pet.wind,
        &pet.skillSlots, &pet.canRename };
    bool full = values[1] == "1";
    auto mask = full ? 0x001ffffe : ParseMask(values[1]);
    // The server's partial-pet table omits MAXMP, so this bit carries no value.
    if (!full)
        mask &= ~(std::uint32_t{1} << 5);
    ApplyStatus(values, 2, mask, numbers, &pet.name, &pet.nickname, &pet.experience, &pet.nextExperience, 6);
    ApplyExtra(values, pet.perception, pet.rebirth);
    if (values.size() >= 3 && values[values.size() - 3] == "UC1" && values.back().empty())
        pet.uniqueCode = UnescapeLegacy(values[values.size() - 2]);
    pet.present = true;
    if (full)
        pet.received = true;
    auto& captureName = captureNames_[slot];
    if (full && captureName.expected && !pets_[slot].present && pet.level == 1)
    {
        ++captureStatistics_.capturedLevelOne;
        captureName.uniqueCode = pet.uniqueCode;
        captureName.nickname = std::to_string(pet.maxHp) + "/" + std::to_string(pet.attack) + "/" +
            std::to_string(pet.defence) + "/" + std::to_string(pet.quick) + "/" + std::to_string(pet.perception);
        captureName.expected = false;
        const int attributes[] = { pet.maxHp, pet.attack, pet.defence, pet.quick, pet.perception };
        for (std::size_t i = 0; i < capture_.minimums.size(); ++i)
            if (capture_.minimums[i] >= 0 && attributes[i] < capture_.minimums[i])
                captureName.discard = true;
    }
    pets_[slot] = pet;
    return true;
}

const LoginClient::PlayerStatus& LoginClient::GetPlayerStatus() const noexcept
{
    return playerStatus_;
}

const std::vector<LoginClient::PetStatus>& LoginClient::GetPets() const noexcept
{
    return pets_;
}

const std::vector<LoginClient::ItemStatus>& LoginClient::GetItems() const noexcept
{
    return items_;
}

bool LoginClient::HasItems() const noexcept
{
    return itemsReceived_;
}
