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
        void RequestMove(int map, int x, int y);
        bool RequestPetState(int slot, int state);

    private:
        void Publish(Screen screen, const std::wstring& message, const LoginClient* client = nullptr);
        void RunSession(std::string address, std::uint16_t port, std::string account, std::string password);

        HWND window_ = nullptr;
        UINT message_ = 0;
        SessionView shared_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
        bool stop_ = false, done_ = true;
        int requestedCharacter_ = -1;
        int requestedPet_ = -1, requestedPetState_ = 0;
        int requestedMap_ = -1, requestedX_ = -1, requestedY_ = -1;
    };
}
