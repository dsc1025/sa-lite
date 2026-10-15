#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include <stdexcept>
#include <algorithm>

using namespace Protocol;

bool LoginClient::IsCaptureFull() const noexcept
{
    return std::all_of(pets_.begin(), pets_.end(),
        [](const PetStatus& pet) { return pet.received && pet.present; });
}

bool LoginClient::HasPendingCaptureNames() const noexcept
{
    return std::any_of(captureNames_.begin(), captureNames_.end(),
        [](const CaptureName& name) { return !name.nickname.empty(); });
}

bool LoginClient::HasRejectedCapture() const noexcept
{
    return std::any_of(captureNames_.begin(), captureNames_.end(),
        [](const CaptureName& name) { return name.discard && !name.nickname.empty(); });
}

bool LoginClient::PollCaptureNames()
{
    if (playerStatus_.inBattle)
        return false;
    bool pending = false;
    for (std::size_t slot = 0; slot < captureNames_.size(); ++slot)
    {
        auto& name = captureNames_[slot];
        name.expected = false;
        if (name.nickname.empty())
            continue;
        const auto& pet = pets_[slot];
        if (!pet.present || pet.uniqueCode != name.uniqueCode ||
            (!name.discard && pet.nickname == name.nickname))
        {
            name = {};
            continue;
        }
        if (name.discard)
        {
            if (name.uniqueCode.empty() || !pet.received || !playerStatus_.received ||
                !playerStatus_.battleReceived || playerStatus_.battlePet == static_cast<int>(slot) ||
                playerStatus_.ridePet == static_cast<int>(slot))
            {
                name = {};
                throw std::runtime_error("捕获宠物自动丢弃条件不满足，抓宠已停止，请检查宠物状态");
            }
            const auto now = GetTickCount64();
            if (name.deadline == 0)
            {
                SendNumbers("21", { playerStatus_.mapX, playerStatus_.mapY, static_cast<int>(slot) }, now + 2000);
                name.deadline = now + 2000;
            }
            else if (now >= name.deadline)
            {
                name = {};
                throw std::runtime_error("服务器未确认不合格宠物自动丢弃，抓宠已停止，请检查宠物栏");
            }
            pending = true;
            continue;
        }
        if (name.uniqueCode.empty() || name.nickname.size() > 16)
        {
            name = {};
            throw std::runtime_error("捕获宠物无法自动改名：缺少唯一编码或五项数值名称超过 16 字节，抓宠已停止");
        }
        const auto now = GetTickCount64();
        if (name.deadline == 0)
        {
            auto number = static_cast<std::uint32_t>(slot);
            auto length = static_cast<std::uint32_t>(name.nickname.size());
            connection_.Send(EncodePacket("&;65;" + EncodeInteger(number, sessionKey_) + ";" +
                Encode64(name.nickname, sessionKey_, -1) + ";" +
                EncodeInteger(number + length, sessionKey_) + ";#;"), now + 2000);
            name.deadline = now + 2000;
        }
        else if (now >= name.deadline)
        {
            name = {};
            throw std::runtime_error("服务器未确认捕获宠物自动改名，抓宠已停止，请检查宠物昵称");
        }
        pending = true;
    }
    return pending;
}

std::string LoginClient::DropPet(int slot, const std::string& uniqueCode)
{
    if (!inGame_ || !playerStatus_.received || playerStatus_.inBattle ||
        IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再丢弃宠物");
    if (slot < 0 || slot >= static_cast<int>(pets_.size()) || !pets_[slot].received ||
        !pets_[slot].present || uniqueCode.empty() || pets_[slot].uniqueCode != uniqueCode)
        throw std::runtime_error("宠物数据已变化或缺少唯一编码，请重新选择");
    if (!playerStatus_.battleReceived || playerStatus_.battlePet == slot || playerStatus_.ridePet == slot)
        throw std::runtime_error("请先将宠物设为等待，再丢弃");
    auto deadline = GetTickCount64() + 2000;
    SendNumbers("21", { playerStatus_.mapX, playerStatus_.mapY, slot }, deadline);
    while (pets_[slot].present && pets_[slot].uniqueCode == uniqueCode && GetTickCount64() < deadline)
        PollGame(100);
    if (!pets_[slot].present)
        return "宠物已丢弃";
    return "服务器未确认宠物丢弃，请检查宠物及当前场景是否允许丢弃，勿连续重复操作";
}

void LoginClient::SetPetState(int slot, PetState state)
{
    if (!inGame_ || slot < 0 || slot >= static_cast<int>(pets_.size()) || !pets_[slot].present ||
        !pets_[slot].received || state < PetState::Standby || state > PetState::Ride)
        throw std::invalid_argument("请选择有效的宠物");
    if (state == PetState::Ride && playerStatus_.ridePet >= 0 && playerStatus_.ridePet != slot)
        throw std::invalid_argument("当前已有骑宠，请先将它改为等待，再选择新的骑宠");
    auto deadline = GetTickCount64() + 10000;
    auto sendNumbers = [&](const char* function, std::initializer_list<int> numbers)
    {
        std::string raw = std::string("&;") + function + ";";
        std::uint32_t checksum = 0;
        for (int number : numbers)
        {
            auto value = static_cast<std::uint32_t>(number);
            raw += EncodeInteger(value, sessionKey_) + ";";
            checksum += value;
        }
        connection_.Send(EncodePacket(raw + EncodeInteger(checksum, sessionKey_) + ";#;"), deadline);
    };
    auto ride = [&](int pet)
    {
        if (playerStatus_.ridePet == pet)
            return;
        waitingRide_ = true;
        rideError_.clear();
        try
        {
            std::string message = "R|P|" + std::to_string(pet);
            connection_.Send(EncodePacket("&;94;" + Encode64(message, sessionKey_, -1) + ";" +
                EncodeInteger(static_cast<std::uint32_t>(message.size()), sessionKey_) + ";#;"), deadline);
            while (playerStatus_.ridePet != pet)
            {
                if (!rideError_.empty())
                    throw std::runtime_error(rideError_);
                if (GetTickCount64() >= deadline)
                    throw std::runtime_error("服务器未确认骑乘，请检查宠物等级、忠诚及骑乘资格");
                PollGame(100);
            }
        }
        catch (...)
        {
            waitingRide_ = false;
            throw;
        }
        waitingRide_ = false;
    };
    auto battle = [&](int pet)
    {
        if (playerStatus_.battlePet == pet)
            return;
        auto serial = battleReplySerial_;
        sendNumbers("54", { pet });
        while (battleReplySerial_ == serial)
        {
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("服务器未确认出战，请检查宠物状态及是否正在战斗");
            PollGame(100);
        }
        if (!battleReplyAccepted_ || playerStatus_.battlePet != pet)
            throw std::runtime_error("服务器拒绝出战设置，请检查宠物状态及是否正在战斗");
    };
    if (playerStatus_.ridePet == slot && state != PetState::Ride)
        ride(-1);
    if (state != PetState::Battle && playerStatus_.battlePet == slot)
        battle(-1);
    if (state == PetState::Standby)
        sendNumbers("96", { slot, 1 });
    if (state == PetState::Battle)
        battle(slot);
    if (state == PetState::Ride)
        ride(slot);
}

namespace
{
    bool ValidPetCode(const std::string& code)
    {
        return !code.empty() && code.size() <= 128 && std::none_of(code.begin(), code.end(),
            [](unsigned char value) { return value <= 32 || value >= 127 || value == '|'; });
    }

    std::string PetStorageError(const std::string& reason, bool put)
    {
        if (reason == "BUSY") return "战斗或交易中无法操作宠物仓库";
        if (reason == "FULL") return put ? "宠物仓库已满" : "最多只能携带 5 只宠物，请先空出宠物栏";
        if (reason == "STALE") return "宠物数据已变化，请重新打开宠物页刷新后再操作";
        if (reason == "RIDING") return "请先将骑宠改为等待状态";
        if (reason == "SLOT") return "宠物栏位无效或超过当前仓库容量";
        if (reason == "DATA") return "服务器无法读取宠物仓库数据";
        if (reason == "FORBIDDEN") return "该宠物不能存入仓库";
        return "服务器拒绝宠物仓库请求：" + reason;
    }

    Game::StoredPet ParseStoredPet(const std::vector<std::string>& values)
    {
        Game::StoredPet pet;
        for (std::size_t i = 4; i < values.size(); ++i)
        {
            auto colon = values[i].find(':');
            if (colon == std::string::npos) continue;
            auto key = values[i].substr(0, colon);
            auto value = values[i].substr(colon + 1);
            if (key == "name") pet.name = UnescapeLegacy(value);
            else if (key == "ownt") pet.nickname = UnescapeLegacy(value);
            else if (key == "ucode") pet.uniqueCode = UnescapeLegacy(value);
            else if (key == "lv") pet.level = ParseNumber(value);
        }
        if (pet.name.empty() || !ValidPetCode(pet.uniqueCode))
            throw std::runtime_error("宠物仓库数据不完整");
        pet.present = true;
        return pet;
    }
}

std::string LoginClient::PetStorageRequest(const std::string& action, int slot, const std::string& uniqueCode)
{
    auto deadline = GetTickCount64() + 10000;
    std::string request = "WH|" + action + "|P";
    if (action != "LIST")
        request += "|" + std::to_string(slot) + "|" + uniqueCode;
    try
    {
        connection_.Send(EncodePacket("&;45;" + Encode64(request, sessionKey_, -1) + ";" +
            EncodeInteger(static_cast<std::uint32_t>(request.size()), sessionKey_) + ";#;"), deadline);
        std::vector<Game::StoredPet> updated;
        bool began = false;
        for (;;)
        {
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("宠物仓库请求超时，连接已关闭，请重新登录确认存取结果");
            auto fields = DecodePacket(connection_.Receive(deadline));
            if (fields[1] != "46")
            {
                HandleGamePacket(fields);
                continue;
            }
            auto strings = ReceiveStrings(fields);
            if (strings.size() != 1)
                throw std::runtime_error("Invalid warehouse reply");
            if (strings[0].compare(0, 3, "WH|") != 0)
            {
                HandleGamePacket(fields);
                continue;
            }
            auto values = SplitLegacy(strings[0]);
            if (values.size() == 4 && values[1] == "RESULT" && values[2] == "0")
                return values[3];
            if (action == "LIST")
            {
                if (values.size() == 4 && values[1] == "BEGIN" && values[2] == "P" && !began)
                {
                    int capacity = ParseNumber(values[3]);
                    if (capacity < 5 || capacity > 15)
                        throw std::runtime_error("Invalid pet warehouse capacity");
                    updated.resize(capacity);
                    began = true;
                    continue;
                }
                if (values.size() >= 5 && values[1] == "ENTRY" && values[2] == "P" && began)
                {
                    int entry = ParseNumber(values[3]);
                    if (entry < 0 || entry >= static_cast<int>(updated.size()) || updated[entry].present)
                        throw std::runtime_error("Invalid pet warehouse slot");
                    updated[entry] = ParseStoredPet(values);
                    continue;
                }
                if (values.size() == 3 && values[1] == "END" && values[2] == "P" && began)
                {
                    petStorage_ = updated;
                    petStorageReceived_ = true;
                    return {};
                }
            }
            else if (values.size() == 8 && values[1] == "RESULT" && values[2] == "1" &&
                values[3] == action && values[4] == "P" && ParseNumber(values[5]) == slot &&
                values[7] == uniqueCode)
            {
                int target = ParseNumber(values[6]);
                int capacity = action == "PUT" ? static_cast<int>(petStorage_.size()) : static_cast<int>(pets_.size());
                if (target < 0 || target >= capacity)
                    throw std::runtime_error("Invalid pet warehouse destination");
                return {};
            }
            throw std::runtime_error("Unexpected pet warehouse reply");
        }
    }
    catch (...)
    {
        bool timedOut = GetTickCount64() >= deadline;
        Disconnect();
        if (timedOut)
            throw std::runtime_error("宠物仓库请求超时，连接已关闭，请重新登录确认存取结果");
        throw;
    }
}

void LoginClient::RefreshPetStorage()
{
    if (!inGame_ || playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再刷新宠物仓库");
    petStorageReceived_ = false;
    auto error = PetStorageRequest("LIST", -1, {});
    if (!error.empty())
        throw std::runtime_error(PetStorageError(error, false));
}

void LoginClient::TransferPetStorage(bool put, int slot, const std::string& uniqueCode)
{
    if (!inGame_ || !petStorageReceived_)
        throw std::runtime_error("请先打开宠物页加载仓库");
    if (playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再存取宠物");
    if (!ValidPetCode(uniqueCode))
        throw std::runtime_error("缺少宠物唯一编码，请更新服务器并重新登录");
    if (put)
    {
        if (slot < 0 || slot >= static_cast<int>(pets_.size()) || !pets_[slot].received ||
            !pets_[slot].present || pets_[slot].uniqueCode != uniqueCode)
            throw std::runtime_error("宠物数据已变化，请重新选择宠物");
        if (!playerStatus_.received || !playerStatus_.battleReceived ||
            playerStatus_.ridePet == slot || playerStatus_.battlePet == slot)
            throw std::runtime_error("只有等待状态的宠物可以存入");
        if (std::none_of(petStorage_.begin(), petStorage_.end(), [](const Game::StoredPet& pet) { return !pet.present; }))
            throw std::runtime_error("宠物仓库已满");
    }
    else
    {
        if (slot < 0 || slot >= static_cast<int>(petStorage_.size()) || !petStorage_[slot].present ||
            petStorage_[slot].uniqueCode != uniqueCode)
            throw std::runtime_error("仓库宠物数据已变化，请刷新后再操作");
        if (std::any_of(pets_.begin(), pets_.end(), [](const PetStatus& pet) { return !pet.received; }))
            throw std::runtime_error("请等待宠物栏数据加载完成");
        if (std::none_of(pets_.begin(), pets_.end(), [](const PetStatus& pet) { return !pet.present; }))
            throw std::runtime_error("最多只能携带 5 只宠物，请先空出宠物栏");
    }
    auto error = PetStorageRequest(put ? "PUT" : "GET", slot, uniqueCode);
    if (!error.empty())
    {
        if (error == "STALE" || error == "SLOT") petStorageReceived_ = false;
        throw std::runtime_error(PetStorageError(error, put));
    }
    try { RefreshPetStorage(); }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string(put ? "宠物已存入，但仓库刷新失败：" : "宠物已取出，但仓库刷新失败：") + error.what());
    }
}
