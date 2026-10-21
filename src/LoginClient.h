#pragma once

#include "network/SocketConnection.h"
#include "game/Character.h"
#include "game/PlayerStatus.h"
#include "game/PetTypes.h"
#include "game/ItemTypes.h"
#include "game/AuctionTypes.h"
#include "game/MagicStatus.h"
#include "game/BattleSettings.h"
#include "game/Battle.h"
#include "game/Encounter.h"
#include <array>
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
    using NewCharacter = Game::NewCharacter;
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
    static void ValidateNewCharacter(const NewCharacter& character);
    bool CreateCharacter(const NewCharacter& character, std::string& error, int timeoutMs = 10000);
    void EnterGame(const Character& character, int timeoutMs = 10000);
    bool PollGame(int timeoutMs = 100);
    void SendChat(const std::string& text);
    const std::vector<std::string>& GetChatMessages() const noexcept { return chatMessages_; }
    bool MoveTo(int map, int x, int y, int timeoutMs = 2000);
    void SetPetState(int slot, PetState state);
    void IncreaseAttribute(int attribute);
    std::string RenamePet(int slot, const std::string& uniqueCode, const std::string& nickname);
    std::string DropPet(int slot, const std::string& uniqueCode);
    std::string DropItem(int slot, std::uint32_t revision);
    std::string EquipItem(int slot, std::uint32_t revision, int equipmentSlot = -1);
    std::string UseItem(int slot, std::uint32_t revision, int petSlot);
    void RefreshShop(int category);
    std::string BuyShopItem(int itemId, int quantity, int price, int currency);
    const std::vector<Game::ShopItem>& GetShopItems() const noexcept { return shopItems_; }
    int GetShopCategory() const noexcept { return shopCategory_; }
    int GetPearlBalance() const noexcept { return pearlBalance_; }
    void RequestAuction(const Game::AuctionRequest& request);
    const Game::AuctionView& GetAuction() const noexcept { return auction_; }
    void RefreshItemStorage();
    void TransferItemStorage(bool put, int slot, std::uint32_t revision, const std::string& uniqueCode);
    const std::vector<ItemStatus>& GetItemStorage() const noexcept { return itemStorage_; }
    bool HasItemStorage() const noexcept { return itemStorageReceived_; }
    void RefreshPetStorage();
    void TransferPetStorage(bool put, int slot, const std::string& uniqueCode);
    const std::vector<Game::StoredPet>& GetPetStorage() const noexcept { return petStorage_; }
    bool HasPetStorage() const noexcept { return petStorageReceived_; }
    void SetEncounterEnabled(bool enabled);
    void PollEncounter();
    void SetHealingSettings(const Game::HealingSettings& settings) { healing_ = settings; }
    void SetCaptureSettings(const Game::CaptureSettings& settings, bool enabled) { capture_ = settings; captureEnabled_ = enabled; }
    bool IsCaptureFull() const noexcept;
    bool HasPendingCaptureNames() const noexcept;
    bool HasRejectedCapture() const noexcept;
    const Game::CaptureStatistics& GetCaptureStatistics() const noexcept { return captureStatistics_; }
    bool IsEncounterEnabled() const noexcept { return encounter_.enabled; }
    bool IsEncounterStopping() const noexcept { return encounter_.stopPending; }
    bool IsEncounterWaiting() const noexcept { return encounter_.requestPending; }
    void LeaveGame(int timeoutMs = 10000);
    const PlayerStatus& GetPlayerStatus() const noexcept;
    const std::vector<PetStatus>& GetPets() const noexcept;
    const std::vector<ItemStatus>& GetItems() const noexcept;
    bool HasItems() const noexcept;
    const std::vector<Game::MagicStatus>& GetMagic() const noexcept { return magic_; }
    const Game::BattleState& GetBattle() const noexcept { return battle_; }
    const Game::BattleResult& GetBattleResult() const noexcept { return battleResult_; }
    void Disconnect() noexcept;
    bool IsConnected() const noexcept;

private:
    std::string AuctionExchange(const std::string& command, const std::string& requestId,
        const Game::AuctionRequest& request);
    Game::AuctionView auction_;
    std::vector<std::string> ReceiveStrings(const std::vector<std::string>& fields) const;
    std::vector<std::string> WaitReply(const std::string& function, ULONGLONG deadline);
    bool HandleGamePacket(const std::vector<std::string>& fields);
    bool HandleBattlePacket(const std::vector<std::string>& fields);
    bool HandleBattleResult(const std::vector<std::string>& fields);
    bool PollBattle();
    void FinishBattle();
    bool HandlePositionPacket(const std::vector<std::string>& fields);
    bool HandleServerMessage(const std::vector<std::string>& fields);
    bool HandlePetStateReply(const std::vector<std::string>& fields);
    std::string PetStorageRequest(const std::string& action, int slot, const std::string& uniqueCode);
    std::string ItemStorageRequest(const std::string& action, int slot, const std::string& uniqueCode,
        std::uint32_t revision = 0);
    bool ApplyItemStatus(const std::string& data, bool partial);
    bool ApplyMagicStatus(const std::vector<std::string>& values);
    bool ApplyMapStatus(const std::vector<std::string>& values);
    bool ApplyPlayerStatus(const std::vector<std::string>& values);
    bool ApplyPetStatus(const std::vector<std::string>& values);
    bool ApplyPetSkillStatus(const std::vector<std::string>& values);
    void SendNumbers(const char* function, std::initializer_list<int> numbers, ULONGLONG deadline);
    void RefreshPosition(ULONGLONG deadline);
    bool HandleEncounterReply(const std::vector<std::string>& fields);
    bool PollCaptureNames();

    std::vector<Game::ShopItem> shopItems_;
    int shopCategory_ = -1, pearlBalance_ = 0;
    bool shopUncertain_ = false;
    SocketConnection connection_;
    std::vector<std::string> chatMessages_;
    bool loggedIn_ = false;
    bool inGame_ = false;
    std::string sessionKey_;
    std::vector<Character> characters_;
    PlayerStatus playerStatus_;
    Game::EncounterState encounter_;
    Game::HealingSettings healing_;
    Game::CaptureSettings capture_;
    bool captureEnabled_ = false;
    Game::CaptureStatistics captureStatistics_;
    struct CaptureName
    {
        bool expected = false, discard = false;
        std::string uniqueCode, nickname;
        std::uint64_t deadline = 0;
    };
    std::array<CaptureName, 5> captureNames_;
    Game::BattleState battle_;
    Game::BattleResult battleResult_;
    std::vector<PetStatus> pets_ = std::vector<PetStatus>(5);
    std::vector<ItemStatus> items_ = std::vector<ItemStatus>(20);
    std::vector<Game::MagicStatus> magic_ = std::vector<Game::MagicStatus>(5);
    bool itemsReceived_ = false;
    std::uint32_t itemRevision_ = 0;
    std::uint32_t itemMoveSerial_ = 0;
    int itemMoveFrom_ = -1, itemMoveTo_ = -1;
    std::vector<ItemStatus> itemStorage_;
    bool itemStorageReceived_ = false;
    bool waitingFood_ = false;
    std::string foodMessage_;
    std::vector<Game::StoredPet> petStorage_;
    bool petStorageReceived_ = false;
    std::uint32_t battleReplySerial_ = 0;
    bool battleReplyAccepted_ = false;
    bool waitingRide_ = false;
    std::string rideError_;
    ULONGLONG nextHeartbeat_ = 0;
    std::uint32_t positionSerial_ = 0;
    int eventSequence_ = 0;
};
