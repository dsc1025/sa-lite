#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include <stdexcept>

using namespace Protocol;

namespace
{
    void ApplyStatus(const std::vector<std::string>& values, std::size_t start, std::uint32_t mask,
        const std::vector<int*>& numbers, std::string* name = nullptr, std::string* nickname = nullptr)
    {
        for (std::size_t bit = 1; bit <= 31; ++bit)
        {
            if (!(mask & (std::uint32_t{1} << bit)))
                continue;
            if (start >= values.size())
                throw std::runtime_error("Incomplete status update");
            if (bit <= numbers.size() && numbers[bit - 1])
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
        return item;
    }
}

bool LoginClient::HandleGamePacket(const std::vector<std::string>& fields)
{
    if (fields[1] == "2" || fields[1] == "7")
        return HandlePositionPacket(fields);
    if (fields[1] == "36" && waitingRide_)
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
    auto values = SplitLegacy(data);
    if (data[0] == 'C')
        return ApplyMapStatus(values);
    if (data[0] == 'P')
        return ApplyPlayerStatus(values);
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
    }
    else
        playerStatus_.inBattle = numbers[0] != 0;
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
    if (sender == static_cast<std::uint32_t>(-1) && message.compare(0, 2, "P|") == 0)
    {
        auto text = UnescapeLegacy(message.substr(2));
        int length = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        if (length > 0)
            MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
        if (wide.find(L"骑") != std::wstring::npos || wide.find(L"忠诚") != std::wstring::npos ||
            wide.find(L"模型不匹配") != std::wstring::npos)
            rideError_ = text;
    }
    return false;
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
    }
    items_ = updated;
    if (!partial)
        itemsReceived_ = true;
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
        &status.experience, &status.nextExperience, &status.level, &status.attack,
        &status.defence, &status.quick, &status.charm, &status.luck,
        &status.earth, &status.water, &status.fire, &status.wind, &status.gold,
        &status.title, &status.duelPoints, &status.rebirth, nullptr, nullptr, &status.ridePet };
    bool full = values[0] == "P1";
    ApplyStatus(values, 1, full ? 0x01fffffe : ParseMask(values[0].substr(1)), numbers);
    if (full && values.size() > 25)
        status.ridePet = ParseNumber(values[25]);
    ApplyExtra(values, status.perception, status.rebirth);
    if (full)
        status.received = true;
    playerStatus_ = status;
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
        return true;
    }
    auto pet = pets_[slot];
    std::vector<int*> numbers = { &pet.image, &pet.hp, &pet.maxHp, &pet.mp, &pet.maxMp,
        &pet.experience, &pet.nextExperience, &pet.level, &pet.attack, &pet.defence,
        &pet.quick, &pet.loyalty, &pet.earth, &pet.water, &pet.fire, &pet.wind,
        &pet.skillSlots, &pet.canRename };
    bool full = values[1] == "1";
    auto mask = full ? 0x001ffffe : ParseMask(values[1]);
    // The server's partial-pet table omits MAXMP, so this bit carries no value.
    if (!full)
        mask &= ~(std::uint32_t{1} << 5);
    ApplyStatus(values, 2, mask, numbers, &pet.name, &pet.nickname);
    ApplyExtra(values, pet.perception, pet.rebirth);
    pet.present = true;
    if (full)
        pet.received = true;
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
