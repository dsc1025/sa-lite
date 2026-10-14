#pragma once

#include "../LoginClient.h"
#include "SessionView.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Client
{
    class GameSession
    {
    public:
        GameSession() = default;
        ~GameSession();
        GameSession(const GameSession&) = delete;
        GameSession& operator=(const GameSession&) = delete;

        void Start(HWND window, UINT message, const std::string& address, std::uint16_t port,
            const std::string& account, const std::string& password);
        void Stop();
        void Join();
        bool IsDone();
        SessionView Snapshot(bool& done, bool& stopped);
        void SelectCharacter(int selection);
        bool RequestCreateCharacter(const LoginClient::NewCharacter& character);
        void RequestMove(int map, int x, int y);
        bool RequestPetState(int slot, int state);
        bool RequestPetStorage(Game::PetStorageAction action, int slot = -1, const std::string& uniqueCode = {});
        bool RequestDiscard(bool pet, int slot, std::uint32_t revision, const std::string& uniqueCode);
        bool RequestFood(int slot, std::uint32_t revision, int petSlot);
        bool RequestItemStorage(Game::ItemStorageAction action, int slot = -1,
            std::uint32_t revision = 0, const std::string& uniqueCode = {});
        void SetEncounterEnabled(bool enabled);
        void SetHealingSettings(const Game::HealingSettings& settings);

    private:
        void UpdateEncounter(LoginClient& client);
        void Publish(Screen screen, const std::wstring& message, const LoginClient* client = nullptr);
        void RunSession(std::string address, std::uint16_t port, std::string account, std::string password);

        HWND window_ = nullptr;
        UINT message_ = 0;
        SessionView shared_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
        bool stop_ = false, done_ = true;
        bool encounterRequested_ = false;
        Game::HealingSettings healing_;
        int requestedCharacter_ = -1;
        bool createRequested_ = false;
        LoginClient::NewCharacter requestedCreation_;
        int requestedPet_ = -1, requestedPetState_ = 0;
        bool storageRequested_ = false;
        Game::PetStorageAction requestedStorageAction_ = Game::PetStorageAction::List;
        int requestedStorageSlot_ = -1;
        std::string requestedStorageCode_;
        bool itemStorageRequested_ = false;
        Game::ItemStorageAction requestedItemStorageAction_ = Game::ItemStorageAction::List;
        int requestedItemStorageSlot_ = -1;
        std::uint32_t requestedItemStorageRevision_ = 0;
        std::string requestedItemStorageCode_;
        int requestedDiscard_ = -1;
        bool requestedDiscardPet_ = false;
        std::uint32_t requestedDiscardRevision_ = 0;
        std::string requestedDiscardCode_;
        int requestedFood_ = -1;
        int requestedFoodPet_ = -1;
        std::uint32_t requestedFoodRevision_ = 0;
        int requestedMap_ = -1, requestedX_ = -1, requestedY_ = -1;
    };
}
