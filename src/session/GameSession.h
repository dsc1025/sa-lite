#pragma once

#include <array>

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

class LoginClient;

namespace Game
{
    struct NewCharacter;
    struct HealingSettings;
    struct CaptureSettings;
    enum class PetStorageAction;
    enum class ItemStorageAction;
}

namespace Client
{
    enum class Screen;
    struct SessionView;

    class GameSession
    {
    public:
        GameSession();
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
        bool RequestCreateCharacter(const Game::NewCharacter& character);
        void RequestMove(int map, int x, int y);
        bool RequestChat(const std::string& text);
        bool RequestPetRename(int slot, const std::string& uniqueCode, const std::string& nickname);
        bool RequestPetState(int slot, int state);
        bool RequestIncreaseAttribute(int attribute);
        bool RequestPetStorage(Game::PetStorageAction action, int slot = -1, const std::string& uniqueCode = {});
        bool RequestDiscard(bool pet, int slot, std::uint32_t revision, const std::string& uniqueCode);
        bool RequestItemUse(int slot, std::uint32_t revision, int petSlot);
        bool RequestItemStorage(Game::ItemStorageAction action, int slot = -1,
            std::uint32_t revision = 0, const std::string& uniqueCode = {});
        void SetEncounterEnabled(bool enabled, bool capture = false, int sourceInstanceId = -1);
        void SetCaptureSettings(const Game::CaptureSettings& settings);
        void SetCaptureMinimums(const std::array<int, 5>& minimums);
        void SetHealingSettings(const Game::HealingSettings& settings);

    private:
        void UpdateEncounter(LoginClient& client);
        void Publish(Screen screen, const std::wstring& message, const LoginClient* client = nullptr);
        void RunSession(std::string address, std::uint16_t port, std::string account, std::string password);

        struct State;
        std::unique_ptr<State> state_;
    };
}
