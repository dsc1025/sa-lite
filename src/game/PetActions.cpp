#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include <stdexcept>

using namespace Protocol;

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

