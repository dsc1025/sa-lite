#include "GameSession.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    GameSession::~GameSession()
    {
        Stop();
        Join();
    }

    void GameSession::Start(HWND window, UINT message, const std::string& address, std::uint16_t port,
        const std::string& account, const std::string& password)
    {
        Join();
        window_ = window;
        message_ = message;
        try
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = false; done_ = false; requestedCharacter_ = -1;
                requestedPet_ = -1;
                requestedMap_ = -1;
                shared_ = {};
                shared_.screen = Screen::Connecting;
                shared_.message = L"正在连接并登录…";
            }
            worker_ = std::thread(&GameSession::RunSession, this, address, port, account, password);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
            throw;
        }
    }

    void GameSession::Join()
    {
        if (worker_.joinable())
            worker_.join();
    }

    bool GameSession::IsDone()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return done_;
    }

    SessionView GameSession::Snapshot(bool& done, bool& stopped)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done = done_;
        stopped = stop_;
        return shared_;
    }

    void GameSession::SelectCharacter(int selection)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requestedCharacter_ = selection;
        }
        wake_.notify_all();
    }

    void GameSession::RequestMove(int map, int x, int y)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stop_ && !done_)
        {
            requestedMap_ = map;
            requestedX_ = x;
            requestedY_ = y;
        }
    }

    bool GameSession::RequestPetState(int slot, int state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || done_ || requestedPet_ >= 0)
            return false;
        requestedPet_ = slot;
        requestedPetState_ = state;
        return true;
    }

    void GameSession::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
    }

    void GameSession::Publish(Screen screen, const std::wstring& message, const LoginClient* client)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shared_.screen = screen;
            shared_.message = message;
            if (client)
            {
                shared_.player = client->GetPlayerStatus();
                shared_.pets = client->GetPets();
                shared_.items = client->GetItems();
                shared_.hasItems = client->HasItems();
            }
        }
        ::PostMessage(window_, message_, 0, 0);
    }

    void GameSession::RunSession(std::string address, std::uint16_t port, std::string account, std::string password)
    {
        bool entered = false;
        try
        {
            LoginClient client;
            client.Connect(address, port);
            auto login = client.Login(account, password);
            SecureZeroMemory(password.data(), password.size());
            password.clear();
            if (login != LoginClient::LoginResult::Accepted)
                throw std::runtime_error("账号或密码不正确，或账号服务拒绝登录");
            auto characters = client.GetCharacters();
            if (characters.empty())
                throw std::runtime_error("账号下没有角色，请先使用旧客户端创建角色");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_.characters = characters;
            }
            Publish(Screen::Characters, L"选择一个角色，进入游戏");
            int selection;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || requestedCharacter_ >= 0; });
                selection = stop_ ? -1 : requestedCharacter_;
            }
            if (selection >= 0)
            {
                Publish(Screen::Entering, L"正在进入游戏…");
                client.EnterGame(characters.at(selection));
                entered = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    shared_.characterName = TextEncoding::LegacyText(characters[selection].name);
                }
                Publish(Screen::Game, L"已连接", &client);
                int targetMap = -1, targetX = -1, targetY = -1;
                std::wstring movementMessage = L"已连接";
                for (;;)
                {
                    int pet, state;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stop_) break;
                        pet = requestedPet_;
                        state = requestedPetState_;
                        requestedPet_ = -1;
                        if (requestedMap_ >= 0)
                        {
                            targetMap = requestedMap_;
                            targetX = requestedX_;
                            targetY = requestedY_;
                            requestedMap_ = -1;
                            movementMessage = L"已连接";
                        }
                    }
                    if (client.PollGame(100))
                        Publish(Screen::Game, movementMessage, &client);
                    const auto& position = client.GetPlayerStatus();
                    if (targetMap >= 0 && (position.mapId != targetMap || position.inBattle ||
                        (position.mapX == targetX && position.mapY == targetY)))
                        targetMap = -1;
                    if (targetMap >= 0)
                    {
                        try
                        {
                            bool moved = client.MoveTo(targetMap, targetX, targetY);
                            const auto& actual = client.GetPlayerStatus();
                            movementMessage = moved ? L"已连接" : L"服务器未接受移动：目标 " +
                                std::to_wstring(targetX) + L"," + std::to_wstring(targetY) + L"，实际 " +
                                std::to_wstring(actual.mapX) + L"," + std::to_wstring(actual.mapY);
                            Publish(Screen::Game, movementMessage, &client);
                        }
                        catch (const std::exception& error)
                        {
                            movementMessage = TextEncoding::ErrorText(error.what());
                            Publish(Screen::Game, movementMessage, &client);
                        }
                        targetMap = -1;
                    }
                    if (pet >= 0)
                    {
                        try
                        {
                            client.SetPetState(pet, static_cast<LoginClient::PetState>(state));
                            Publish(Screen::Game, L"宠物状态已更新", &client);
                        }
                        catch (const std::exception& error)
                        {
                            Publish(Screen::Game, TextEncoding::ErrorText(error.what()), &client);
                        }
                    }
                }
            }
            if (entered)
            {
                Publish(Screen::Closing, L"正在退出并等待服务器保存…");
                client.LeaveGame();
            }
            Publish(Screen::Login, L"连接已关闭");
        }
        catch (const std::exception& error)
        {
            SecureZeroMemory(password.data(), password.size());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_.failed = true;
            }
            Publish(Screen::Login, TextEncoding::ErrorText(error.what()));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        ::PostMessage(window_, message_, 0, 0);
    }
}
