#pragma once

#include <winsock2.h>
#include <cstdint>
#include <string>
#include <vector>

class LoginClient
{
public:
    enum class LoginResult { Accepted, Rejected };
    enum class PetState { Standby, Battle, Ride };
    struct Character
    {
        std::string name;
        int level = -1;
    };
    struct PlayerStatus
    {
        bool received = false;
        int hp = 0;
        int maxHp = 0;
        int mp = 0;
        int maxMp = 0;
        int level = 0;
        int gold = 0;
        int mapId = -1;
        int mapX = -1, mapY = -1;
        int vital = 0, strength = 0, toughness = 0, dexterity = 0;
        int experience = 0, nextExperience = 0;
        int attack = 0, defence = 0, quick = 0, charm = 0, luck = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int title = 0, duelPoints = 0, rebirth = 0, perception = 0;
        int ridePet = -1, battlePet = -1;
        std::uint32_t standbyPets = 0;
        bool standbyReceived = false;
        bool battleReceived = false;
    };
    struct PetStatus
    {
        bool received = false, present = false;
        std::string name, nickname;
        int image = 0, hp = 0, maxHp = 0, mp = 0, maxMp = 0;
        int experience = 0, nextExperience = 0, level = 0;
        int attack = 0, defence = 0, quick = 0, loyalty = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int skillSlots = 0, canRename = 0, perception = 0, rebirth = 0;
    };
    struct ItemStatus
    {
        bool present = false;
        std::string name, description;
        int level = 0;
    };

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
    void SetPetState(int slot, PetState state);
    void LeaveGame(int timeoutMs = 10000);
    const PlayerStatus& GetPlayerStatus() const noexcept;
    const std::vector<PetStatus>& GetPets() const noexcept;
    const std::vector<ItemStatus>& GetItems() const noexcept;
    bool HasItems() const noexcept;
    void Disconnect() noexcept;
    bool IsConnected() const noexcept;

private:
    void Wait(bool writing, ULONGLONG deadline);
    void Send(const std::string& packet, ULONGLONG deadline);
    std::string Receive(ULONGLONG deadline, char terminator = '\n', std::size_t maxSize = 65500);
    std::vector<std::string> ReceiveStrings(const std::vector<std::string>& fields) const;
    std::vector<std::string> WaitReply(const std::string& function, ULONGLONG deadline);
    bool HandleGamePacket(const std::vector<std::string>& fields);

    SOCKET socket_ = INVALID_SOCKET;
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
    std::string pending_;
};
