#pragma once

#include "network/SocketConnection.h"
#include "game/GameTypes.h"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

class LoginClient
{
public:
    using LoginResult = Game::LoginResult;
    using PetState = Game::PetState;
    using Character = Game::Character;
    using PlayerStatus = Game::PlayerStatus;
    using PetStatus = Game::PetStatus;
    using ItemStatus = Game::ItemStatus;

    LoginClient();
    ~LoginClient();
    LoginClient(const LoginClient&) = delete;
    LoginClient& operator=(const LoginClient&) = delete;

    void Connect(const std::string& address, std::uint16_t port, int timeoutMs = 10000);
    LoginResult Login(const std::string& account, const std::string& password,
        int timeoutMs = 10000);
    std::vector<Character> GetCharacters(int timeoutMs = 10000);
    void EnterGame(const Character& character, int timeoutMs = 10000);
    bool PollGame(int timeoutMs = 100);
    bool MoveTo(int map, int x, int y, int timeoutMs = 2000);
    void SetPetState(int slot, PetState state);
    void LeaveGame(int timeoutMs = 10000);
    const PlayerStatus& GetPlayerStatus() const noexcept;
    const std::vector<PetStatus>& GetPets() const noexcept;
    const std::vector<ItemStatus>& GetItems() const noexcept;
    bool HasItems() const noexcept;
    void Disconnect() noexcept;
    bool IsConnected() const noexcept;

private:
    std::vector<std::string> ReceiveStrings(const std::vector<std::string>& fields) const;
    std::vector<std::string> WaitReply(const std::string& function, ULONGLONG deadline);
    bool HandleGamePacket(const std::vector<std::string>& fields);
    bool HandlePositionPacket(const std::vector<std::string>& fields);
    bool HandleServerMessage(const std::vector<std::string>& fields);
    bool HandlePetStateReply(const std::vector<std::string>& fields);
    bool ApplyItemStatus(const std::string& data, bool partial);
    bool ApplyMapStatus(const std::vector<std::string>& values);
    bool ApplyPlayerStatus(const std::vector<std::string>& values);
    bool ApplyPetStatus(const std::vector<std::string>& values);
    void SendNumbers(const char* function, std::initializer_list<int> numbers, ULONGLONG deadline);
    void RefreshPosition(ULONGLONG deadline);

    SocketConnection connection_;
    bool loggedIn_ = false;
    bool inGame_ = false;
    std::string sessionKey_;
    std::vector<Character> characters_;
    PlayerStatus playerStatus_;
    std::vector<PetStatus> pets_ = std::vector<PetStatus>(5);
    std::vector<ItemStatus> items_ = std::vector<ItemStatus>(20);
    bool itemsReceived_ = false;
    std::uint32_t battleReplySerial_ = 0;
    bool battleReplyAccepted_ = false;
    bool waitingRide_ = false;
    std::string rideError_;
    ULONGLONG nextHeartbeat_ = 0;
    std::uint32_t positionSerial_ = 0;
    int eventSequence_ = 0;
};
