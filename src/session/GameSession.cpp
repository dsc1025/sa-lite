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
                createRequested_ = false;
                requestedPet_ = -1;
                storageRequested_ = false;
                requestedFood_ = -1;
                requestedDiscard_ = -1;
                requestedDiscardCode_.clear();
                requestedStorageCode_.clear();
                itemStorageRequested_ = false;
                requestedItemStorageCode_.clear();
                requestedMap_ = -1;
                encounterRequested_ = false;
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
            if (shared_.screen != Screen::Characters || shared_.createPending || stop_ || done_ ||
                selection < 0 || selection >= static_cast<int>(shared_.characters.size())) return;
            requestedCharacter_ = selection;
        }
        wake_.notify_all();
    }

    bool GameSession::RequestCreateCharacter(const LoginClient::NewCharacter& character)
    {
        LoginClient::ValidateNewCharacter(character);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Characters || shared_.createPending ||
                requestedCharacter_ >= 0 || shared_.characters.size() >= 2)
                return false;
            requestedCreation_ = character;
            createRequested_ = true;
            shared_.createPending = true;
            shared_.message = L"正在创建角色…";
        }
        wake_.notify_all();
        ::PostMessage(window_, message_, 0, 0);
        return true;
    }

    void GameSession::RequestMove(int map, int x, int y)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stop_ && !done_)
        {
            encounterRequested_ = false;
            requestedMap_ = map;
            requestedX_ = x;
            requestedY_ = y;
        }
    }

    bool GameSession::RequestPetState(int slot, int state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || done_ || requestedPet_ >= 0 || shared_.petStoragePending || shared_.itemPending)
            return false;
        requestedPet_ = slot;
        requestedPetState_ = state;
        return true;
    }

    bool GameSession::RequestPetStorage(Game::PetStorageAction action, int slot, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Game || shared_.petStoragePending || shared_.itemPending ||
                requestedPet_ >= 0 || encounterRequested_ || shared_.encounterPending || shared_.player.inBattle)
                return false;
            if (action != Game::PetStorageAction::List &&
                (!shared_.hasPetStorage || slot < 0 || uniqueCode.empty()))
                return false;
            storageRequested_ = true;
            requestedStorageAction_ = action;
            requestedStorageSlot_ = slot;
            requestedStorageCode_ = uniqueCode;
            shared_.petStoragePending = true;
            shared_.message = action == Game::PetStorageAction::List ? L"正在加载宠物仓库…" :
                action == Game::PetStorageAction::Put ? L"正在存入宠物…" : L"正在取出宠物…";
        }
        ::PostMessage(window_, message_, 0, 0);
        return true;
    }

    bool GameSession::RequestDiscard(bool pet, int slot, std::uint32_t revision, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Game || shared_.itemPending ||
                shared_.petStoragePending || requestedPet_ >= 0 || encounterRequested_ ||
                shared_.encounterEnabled || shared_.encounterPending || shared_.player.inBattle ||
                !shared_.player.received || slot < 0)
                return false;
            if (pet)
            {
                if (slot >= static_cast<int>(shared_.pets.size()) || !shared_.pets[slot].received ||
                    !shared_.pets[slot].present || uniqueCode.empty() ||
                    shared_.pets[slot].uniqueCode != uniqueCode || !shared_.player.battleReceived ||
                    shared_.player.battlePet == slot || shared_.player.ridePet == slot)
                    return false;
            }
            else if (!shared_.hasItems || slot >= static_cast<int>(shared_.items.size()) ||
                !shared_.items[slot].present || shared_.items[slot].revision != revision)
                return false;
            requestedDiscard_ = slot;
            requestedDiscardPet_ = pet;
            requestedDiscardRevision_ = revision;
            requestedDiscardCode_ = uniqueCode;
            shared_.itemPending = true;
            shared_.message = pet ? L"正在丢弃宠物…" : L"正在丢弃道具…";
        }
        ::PostMessage(window_, message_, 0, 0);
        return true;
    }

    bool GameSession::RequestFood(int slot, std::uint32_t revision, int petSlot)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Game || shared_.itemPending ||
                shared_.petStoragePending || requestedPet_ >= 0 || encounterRequested_ ||
                shared_.encounterPending || shared_.player.inBattle || slot < 5 ||
                slot >= static_cast<int>(shared_.items.size()) || !shared_.items[slot].present ||
                shared_.items[slot].type != 20 || shared_.items[slot].revision != revision)
                return false;
            if (petSlot < -1 || petSlot >= static_cast<int>(shared_.pets.size()) ||
                (petSlot >= 0 && (!shared_.pets[petSlot].received || !shared_.pets[petSlot].present)))
                return false;
            requestedFood_ = slot;
            requestedFoodPet_ = petSlot;
            requestedFoodRevision_ = revision;
            shared_.itemPending = true;
            shared_.message = petSlot < 0 ? L"正在对当前角色使用食物…" : L"正在对随身宠物使用食物…";
        }
        ::PostMessage(window_, message_, 0, 0);
        return true;
    }

    bool GameSession::RequestItemStorage(Game::ItemStorageAction action, int slot,
        std::uint32_t revision, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Game || shared_.itemPending ||
                shared_.petStoragePending || requestedPet_ >= 0 || encounterRequested_ ||
                shared_.encounterPending || shared_.player.inBattle)
                return false;
            if (action != Game::ItemStorageAction::List && (!shared_.hasItemStorage || !shared_.hasItems))
                return false;
            if (action == Game::ItemStorageAction::Put && (slot < 5 ||
                slot >= static_cast<int>(shared_.items.size()) || !shared_.items[slot].present ||
                shared_.items[slot].revision != revision))
                return false;
            if (action == Game::ItemStorageAction::Get && (slot < 0 ||
                slot >= static_cast<int>(shared_.itemStorage.size()) || !shared_.itemStorage[slot].present ||
                shared_.itemStorage[slot].uniqueCode != uniqueCode || uniqueCode.empty()))
                return false;
            itemStorageRequested_ = true;
            requestedItemStorageAction_ = action;
            requestedItemStorageSlot_ = slot;
            requestedItemStorageRevision_ = revision;
            requestedItemStorageCode_ = uniqueCode;
            shared_.itemPending = true;
            shared_.message = action == Game::ItemStorageAction::List ? L"正在加载道具仓库…" :
                action == Game::ItemStorageAction::Put ? L"正在存入道具…" : L"正在取出道具…";
        }
        ::PostMessage(window_, message_, 0, 0);
        return true;
    }

    void GameSession::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            encounterRequested_ = false;
        }
        wake_.notify_all();
    }

    void GameSession::Publish(Screen screen, const std::wstring& message, const LoginClient* client)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shared_.screen = screen;
            if (screen != Screen::Game)
            {
                encounterRequested_ = false;
                shared_.encounterEnabled = false;
                shared_.encounterPending = false;
                shared_.encounterMessage = L"遇敌未开始";
                shared_.petStoragePending = false;
                shared_.itemPending = false;
            }
            shared_.message = message;
            if (client)
            {
                shared_.player = client->GetPlayerStatus();
                shared_.battle = client->GetBattle();
                shared_.battleResult = client->GetBattleResult();
                shared_.pets = client->GetPets();
                shared_.items = client->GetItems();
                shared_.magic = client->GetMagic();
                shared_.hasItems = client->HasItems();
                shared_.itemStorage = client->GetItemStorage();
                shared_.hasItemStorage = client->HasItemStorage();
                shared_.petStorage = client->GetPetStorage();
                shared_.hasPetStorage = client->HasPetStorage();
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
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_.characters = characters;
            }
            Publish(Screen::Characters, characters.empty() ? L"暂无角色，请创建角色" : L"选择一个角色，进入游戏");
            int selection = -1;
            for (;;)
            {
                LoginClient::NewCharacter creation;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait(lock, [&] { return stop_ || requestedCharacter_ >= 0 || createRequested_; });
                    if (stop_) break;
                    if (requestedCharacter_ >= 0)
                    {
                        selection = requestedCharacter_;
                        break;
                    }
                    creation = requestedCreation_;
                    createRequested_ = false;
                }
                bool created = false;
                std::string error;
                try { created = client.CreateCharacter(creation, error); }
                catch (const std::exception& exception)
                {
                    if (!client.IsConnected()) throw;
                    error = exception.what();
                }
                if (created) characters = client.GetCharacters();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    shared_.characters = characters;
                    shared_.createPending = false;
                    shared_.createdCharacter = created ? creation.name : std::string();
                    ++shared_.createRevision;
                }
                Publish(Screen::Characters, created ? L"角色创建成功，请选择角色进入游戏" :
                    TextEncoding::ErrorText(error.c_str()));
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
                std::wstring storageMessage = L"已连接";
                try { client.RefreshPetStorage(); }
                catch (const std::exception& error) { storageMessage = TextEncoding::ErrorText(error.what()); }
                if (!client.IsConnected())
                    throw std::runtime_error("宠物仓库加载失败，连接已关闭，请检查服务器版本后重新登录");
                Publish(Screen::Game, storageMessage, &client);
                int targetMap = -1, targetX = -1, targetY = -1;
                std::wstring movementMessage = storageMessage;
                for (;;)
                {
                    int pet, state, pollTimeout, storageSlot, food, foodPet, discard;
                    bool discardPet;
                    std::uint32_t discardRevision;
                    std::string discardCode;
                    std::uint32_t foodRevision, itemStorageRevision;
                    bool itemStorage;
                    int itemStorageSlot;
                    Game::ItemStorageAction itemStorageAction;
                    std::string itemStorageCode;
                    bool storage;
                    Game::PetStorageAction storageAction;
                    std::string storageCode;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stop_) break;
                        pollTimeout = encounterRequested_ || client.GetPlayerStatus().inBattle ? BATTLE_DELAY_MS : 100;
                        pet = requestedPet_;
                        state = requestedPetState_;
                        discard = requestedDiscard_;
                        discardPet = requestedDiscardPet_;
                        discardRevision = requestedDiscardRevision_;
                        discardCode = requestedDiscardCode_;
                        requestedDiscard_ = -1;
                        food = requestedFood_;
                        foodPet = requestedFoodPet_;
                        foodRevision = requestedFoodRevision_;
                        requestedFood_ = -1;
                        itemStorage = itemStorageRequested_;
                        itemStorageAction = requestedItemStorageAction_;
                        itemStorageSlot = requestedItemStorageSlot_;
                        itemStorageRevision = requestedItemStorageRevision_;
                        itemStorageCode = requestedItemStorageCode_;
                        itemStorageRequested_ = false;
                        requestedPet_ = -1;
                        storage = storageRequested_;
                        storageAction = requestedStorageAction_;
                        storageSlot = requestedStorageSlot_;
                        storageCode = requestedStorageCode_;
                        storageRequested_ = false;
                        if (requestedMap_ >= 0)
                        {
                            targetMap = requestedMap_;
                            targetX = requestedX_;
                            targetY = requestedY_;
                            requestedMap_ = -1;
                            movementMessage = L"已连接";
                        }
                    }
                    if (storage)
                    {
                        std::wstring message;
                        std::string failure;
                        try
                        {
                            if (storageAction == Game::PetStorageAction::List)
                                client.RefreshPetStorage();
                            else
                                client.TransferPetStorage(storageAction == Game::PetStorageAction::Put, storageSlot, storageCode);
                            message = storageAction == Game::PetStorageAction::List ? L"宠物仓库已刷新" :
                                storageAction == Game::PetStorageAction::Put ? L"宠物已存入" : L"宠物已取出";
                        }
                        catch (const std::exception& error)
                        {
                            failure = error.what();
                            message = TextEncoding::ErrorText(error.what());
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            shared_.petStoragePending = false;
                        }
                        if (!client.IsConnected())
                            throw std::runtime_error(failure);
                        movementMessage = message;
                        Publish(Screen::Game, message, &client);
                    }
                    if (itemStorage)
                    {
                        std::wstring message;
                        std::string failure;
                        try
                        {
                            if (itemStorageAction == Game::ItemStorageAction::List)
                                client.RefreshItemStorage();
                            else
                                client.TransferItemStorage(itemStorageAction == Game::ItemStorageAction::Put,
                                    itemStorageSlot, itemStorageRevision, itemStorageCode);
                            message = itemStorageAction == Game::ItemStorageAction::List ? L"道具仓库已刷新" :
                                itemStorageAction == Game::ItemStorageAction::Put ? L"道具已存入" : L"道具已取出到背包";
                        }
                        catch (const std::exception& error)
                        {
                            failure = error.what();
                            message = TextEncoding::ErrorText(error.what());
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            shared_.itemPending = false;
                        }
                        if (!client.IsConnected())
                            throw std::runtime_error(failure);
                        movementMessage = message;
                        Publish(Screen::Game, message, &client);
                    }
                    if (discard >= 0)
                    {
                        std::wstring message;
                        std::string failure;
                        try
                        {
                            auto reply = discardPet ? client.DropPet(discard, discardCode) :
                                client.DropItem(discard, discardRevision);
                            message = TextEncoding::ErrorText(reply.c_str());
                        }
                        catch (const std::exception& error)
                        {
                            failure = error.what();
                            message = TextEncoding::ErrorText(error.what());
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            shared_.itemPending = false;
                        }
                        if (!client.IsConnected())
                            throw std::runtime_error(failure);
                        movementMessage = message;
                        Publish(Screen::Game, message, &client);
                    }
                    if (food >= 0)
                    {
                        std::wstring message;
                        std::string failure;
                        try { message = TextEncoding::ErrorText(client.UseFood(food, foodRevision, foodPet).c_str()); }
                        catch (const std::exception& error)
                        {
                            failure = error.what();
                            message = TextEncoding::ErrorText(error.what());
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            shared_.itemPending = false;
                        }
                        if (!client.IsConnected())
                            throw std::runtime_error(failure);
                        movementMessage = message;
                        Publish(Screen::Game, message, &client);
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        client.SetHealingSettings(healing_);
                    }
                    if (client.PollGame(pollTimeout))
                        Publish(Screen::Game, movementMessage, &client);
                    UpdateEncounter(client);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (encounterRequested_)
                            targetMap = -1;
                    }
                    const auto& position = client.GetPlayerStatus();
                    if (targetMap >= 0 && (position.inBattle ||
                        (position.mapId == targetMap && position.mapX == targetX && position.mapY == targetY)))
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
                Publish(Screen::Closing, L"正在原地登出…");
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
