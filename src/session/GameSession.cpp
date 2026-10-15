#include "../LoginClient.h"
#include "GameSessionState.h"
#include "../text/TextEncoding.h"
#include "../game/BattleTiming.h"
#include <stdexcept>

namespace Client
{
    GameSession::GameSession() : state_(std::make_unique<State>())
    {
    }

    GameSession::~GameSession()
    {
        Stop();
        Join();
    }

    void GameSession::Start(HWND window, UINT message, const std::string& address, std::uint16_t port,
        const std::string& account, const std::string& password)
    {
        Join();
        state_->window_ = window;
        state_->message_ = message;
        try
        {
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                state_->stop_ = false; state_->done_ = false; state_->requestedCharacter_ = -1;
                state_->createRequested_ = false;
                state_->requestedPet_ = -1;
                state_->requestedAttribute_ = -1;
                state_->storageRequested_ = false;
                state_->requestedFood_ = -1;
                state_->requestedDiscard_ = -1;
                state_->requestedDiscardCode_.clear();
                state_->requestedStorageCode_.clear();
                state_->itemStorageRequested_ = false;
                state_->requestedItemStorageCode_.clear();
                state_->requestedMap_ = -1;
                state_->encounterRequested_ = false;
                state_->captureEnabled_ = false;
                state_->shared_ = {};
                state_->shared_.screen = Screen::Connecting;
                state_->shared_.message = L"正在连接并登录…";
            }
            state_->worker_ = std::thread(&GameSession::RunSession, this, address, port, account, password);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->done_ = true;
            throw;
        }
    }

    void GameSession::Join()
    {
        if (state_->worker_.joinable())
            state_->worker_.join();
    }

    bool GameSession::IsDone()
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        return state_->done_;
    }

    SessionView GameSession::Snapshot(bool& done, bool& stopped)
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        done = state_->done_;
        stopped = state_->stop_;
        return state_->shared_;
    }

    void GameSession::SelectCharacter(int selection)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->shared_.screen != Screen::Characters || state_->shared_.createPending || state_->stop_ || state_->done_ ||
                selection < 0 || selection >= static_cast<int>(state_->shared_.characters.size())) return;
            state_->requestedCharacter_ = selection;
        }
        state_->wake_.notify_all();
    }

    bool GameSession::RequestCreateCharacter(const Game::NewCharacter& character)
    {
        LoginClient::ValidateNewCharacter(character);
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Characters || state_->shared_.createPending ||
                state_->requestedCharacter_ >= 0 || state_->shared_.characters.size() >= 2)
                return false;
            state_->requestedCreation_ = character;
            state_->createRequested_ = true;
            state_->shared_.createPending = true;
            state_->shared_.message = L"正在创建角色…";
        }
        state_->wake_.notify_all();
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    void GameSession::RequestMove(int map, int x, int y)
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (!state_->stop_ && !state_->done_)
        {
            state_->encounterRequested_ = false;
            state_->requestedMap_ = map;
            state_->requestedX_ = x;
            state_->requestedY_ = y;
        }
    }

    bool GameSession::RequestIncreaseAttribute(int attribute)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            const auto& player = state_->shared_.player;
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game ||
                state_->shared_.attributePending || state_->shared_.itemPending || state_->shared_.petStoragePending ||
                state_->encounterRequested_ || state_->shared_.encounterPending || player.inBattle ||
                !player.received || !player.remainingPointsReceived || player.remainingPoints <= 0 ||
                attribute < 0 || attribute >= 4)
                return false;
            state_->requestedAttribute_ = attribute;
            state_->shared_.attributePending = true;
            state_->shared_.message = L"正在加点…";
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    bool GameSession::RequestPetState(int slot, int state)
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        if (state_->stop_ || state_->done_ || state_->requestedPet_ >= 0 || state_->shared_.petStoragePending || state_->shared_.itemPending)
            return false;
        state_->requestedPet_ = slot;
        state_->requestedPetState_ = state;
        return true;
    }

    bool GameSession::RequestPetStorage(Game::PetStorageAction action, int slot, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game || state_->shared_.petStoragePending || state_->shared_.itemPending ||
                state_->requestedPet_ >= 0 || state_->encounterRequested_ || state_->shared_.encounterPending || state_->shared_.player.inBattle)
                return false;
            if (action != Game::PetStorageAction::List &&
                (!state_->shared_.hasPetStorage || slot < 0 || uniqueCode.empty()))
                return false;
            state_->storageRequested_ = true;
            state_->requestedStorageAction_ = action;
            state_->requestedStorageSlot_ = slot;
            state_->requestedStorageCode_ = uniqueCode;
            state_->shared_.petStoragePending = true;
            state_->shared_.message = action == Game::PetStorageAction::List ? L"正在加载宠物仓库…" :
                action == Game::PetStorageAction::Put ? L"正在存入宠物…" : L"正在取出宠物…";
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    bool GameSession::RequestDiscard(bool pet, int slot, std::uint32_t revision, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game || state_->shared_.itemPending ||
                state_->shared_.petStoragePending || state_->requestedPet_ >= 0 || state_->encounterRequested_ ||
                state_->shared_.encounterEnabled || state_->shared_.encounterPending || state_->shared_.player.inBattle ||
                !state_->shared_.player.received || slot < 0)
                return false;
            if (pet)
            {
                if (slot >= static_cast<int>(state_->shared_.pets.size()) || !state_->shared_.pets[slot].received ||
                    !state_->shared_.pets[slot].present || uniqueCode.empty() ||
                    state_->shared_.pets[slot].uniqueCode != uniqueCode || !state_->shared_.player.battleReceived ||
                    state_->shared_.player.battlePet == slot || state_->shared_.player.ridePet == slot)
                    return false;
            }
            else if (!state_->shared_.hasItems || slot >= static_cast<int>(state_->shared_.items.size()) ||
                !state_->shared_.items[slot].present || state_->shared_.items[slot].revision != revision)
                return false;
            state_->requestedDiscard_ = slot;
            state_->requestedDiscardPet_ = pet;
            state_->requestedDiscardRevision_ = revision;
            state_->requestedDiscardCode_ = uniqueCode;
            state_->shared_.itemPending = true;
            state_->shared_.message = pet ? L"正在丢弃宠物…" : L"正在丢弃道具…";
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    bool GameSession::RequestFood(int slot, std::uint32_t revision, int petSlot)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game || state_->shared_.itemPending ||
                state_->shared_.petStoragePending || state_->requestedPet_ >= 0 || state_->encounterRequested_ ||
                state_->shared_.encounterPending || state_->shared_.player.inBattle || slot < 5 ||
                slot >= static_cast<int>(state_->shared_.items.size()) || !state_->shared_.items[slot].present ||
                state_->shared_.items[slot].type != 20 || state_->shared_.items[slot].revision != revision)
                return false;
            if (petSlot < -1 || petSlot >= static_cast<int>(state_->shared_.pets.size()) ||
                (petSlot >= 0 && (!state_->shared_.pets[petSlot].received || !state_->shared_.pets[petSlot].present)))
                return false;
            state_->requestedFood_ = slot;
            state_->requestedFoodPet_ = petSlot;
            state_->requestedFoodRevision_ = revision;
            state_->shared_.itemPending = true;
            state_->shared_.message = petSlot < 0 ? L"正在对当前角色使用食物…" : L"正在对随身宠物使用食物…";
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    bool GameSession::RequestItemStorage(Game::ItemStorageAction action, int slot,
        std::uint32_t revision, const std::string& uniqueCode)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game || state_->shared_.itemPending ||
                state_->shared_.petStoragePending || state_->requestedPet_ >= 0 || state_->encounterRequested_ ||
                state_->shared_.encounterPending || state_->shared_.player.inBattle)
                return false;
            if (action != Game::ItemStorageAction::List && (!state_->shared_.hasItemStorage || !state_->shared_.hasItems))
                return false;
            if (action == Game::ItemStorageAction::Put && (slot < 5 ||
                slot >= static_cast<int>(state_->shared_.items.size()) || !state_->shared_.items[slot].present ||
                state_->shared_.items[slot].revision != revision))
                return false;
            if (action == Game::ItemStorageAction::Get && (slot < 0 ||
                slot >= static_cast<int>(state_->shared_.itemStorage.size()) || !state_->shared_.itemStorage[slot].present ||
                state_->shared_.itemStorage[slot].uniqueCode != uniqueCode || uniqueCode.empty()))
                return false;
            state_->itemStorageRequested_ = true;
            state_->requestedItemStorageAction_ = action;
            state_->requestedItemStorageSlot_ = slot;
            state_->requestedItemStorageRevision_ = revision;
            state_->requestedItemStorageCode_ = uniqueCode;
            state_->shared_.itemPending = true;
            state_->shared_.message = action == Game::ItemStorageAction::List ? L"正在加载道具仓库…" :
                action == Game::ItemStorageAction::Put ? L"正在存入道具…" : L"正在取出道具…";
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
        return true;
    }

    void GameSession::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->stop_ = true;
            state_->encounterRequested_ = false;
        }
        state_->wake_.notify_all();
    }

    void GameSession::Publish(Screen screen, const std::wstring& message, const LoginClient* client)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->shared_.screen = screen;
            if (screen != Screen::Game)
            {
                state_->encounterRequested_ = false;
                state_->captureEnabled_ = false;
                state_->shared_.encounterEnabled = false;
                state_->shared_.encounterPending = false;
                state_->shared_.encounterMessage = L"遇敌未开始";
                state_->shared_.petStoragePending = false;
                state_->shared_.itemPending = false;
                state_->shared_.attributePending = false;
            }
            state_->shared_.message = message;
            if (client)
            {
                state_->shared_.player = client->GetPlayerStatus();
                state_->shared_.battle = client->GetBattle();
                state_->shared_.battleResult = client->GetBattleResult();
                state_->shared_.captureStatistics = client->GetCaptureStatistics();
                state_->shared_.pets = client->GetPets();
                state_->shared_.items = client->GetItems();
                state_->shared_.magic = client->GetMagic();
                state_->shared_.hasItems = client->HasItems();
                state_->shared_.itemStorage = client->GetItemStorage();
                state_->shared_.hasItemStorage = client->HasItemStorage();
                state_->shared_.petStorage = client->GetPetStorage();
                state_->shared_.hasPetStorage = client->HasPetStorage();
            }
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
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
                std::lock_guard<std::mutex> lock(state_->mutex_);
                state_->shared_.characters = characters;
            }
            Publish(Screen::Characters, characters.empty() ? L"暂无角色，请创建角色" : L"选择一个角色，进入游戏");
            int selection = -1;
            for (;;)
            {
                Game::NewCharacter creation;
                {
                    std::unique_lock<std::mutex> lock(state_->mutex_);
                    state_->wake_.wait(lock, [&] { return state_->stop_ || state_->requestedCharacter_ >= 0 || state_->createRequested_; });
                    if (state_->stop_) break;
                    if (state_->requestedCharacter_ >= 0)
                    {
                        selection = state_->requestedCharacter_;
                        break;
                    }
                    creation = state_->requestedCreation_;
                    state_->createRequested_ = false;
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
                    std::lock_guard<std::mutex> lock(state_->mutex_);
                    state_->shared_.characters = characters;
                    state_->shared_.createPending = false;
                    state_->shared_.createdCharacter = created ? creation.name : std::string();
                    ++state_->shared_.createRevision;
                }
                Publish(Screen::Characters, created ? L"角色创建成功，请选择角色进入游戏" :
                    TextEncoding::ErrorText(error.c_str()));
            }
            if (selection >= 0)
            {
                Publish(Screen::Entering, L"正在进入游戏…");
                {
                    std::lock_guard<std::mutex> lock(state_->mutex_);
                    client.SetHealingSettings(state_->healing_);
                    client.SetCaptureSettings(state_->capture_, state_->captureEnabled_);
                }
                client.EnterGame(characters.at(selection));
                entered = true;
                {
                    std::lock_guard<std::mutex> lock(state_->mutex_);
                    state_->shared_.characterName = TextEncoding::LegacyText(characters[selection].name);
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
                    int pet, state, pollTimeout, storageSlot, food, foodPet, discard, attribute;
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
                        std::lock_guard<std::mutex> lock(state_->mutex_);
                        if (state_->stop_) break;
                        pollTimeout = state_->encounterRequested_ || client.GetPlayerStatus().inBattle ? Game::BATTLE_DELAY_MS : 100;
                        attribute = state_->requestedAttribute_;
                        state_->requestedAttribute_ = -1;
                        pet = state_->requestedPet_;
                        state = state_->requestedPetState_;
                        discard = state_->requestedDiscard_;
                        discardPet = state_->requestedDiscardPet_;
                        discardRevision = state_->requestedDiscardRevision_;
                        discardCode = state_->requestedDiscardCode_;
                        state_->requestedDiscard_ = -1;
                        food = state_->requestedFood_;
                        foodPet = state_->requestedFoodPet_;
                        foodRevision = state_->requestedFoodRevision_;
                        state_->requestedFood_ = -1;
                        itemStorage = state_->itemStorageRequested_;
                        itemStorageAction = state_->requestedItemStorageAction_;
                        itemStorageSlot = state_->requestedItemStorageSlot_;
                        itemStorageRevision = state_->requestedItemStorageRevision_;
                        itemStorageCode = state_->requestedItemStorageCode_;
                        state_->itemStorageRequested_ = false;
                        state_->requestedPet_ = -1;
                        storage = state_->storageRequested_;
                        storageAction = state_->requestedStorageAction_;
                        storageSlot = state_->requestedStorageSlot_;
                        storageCode = state_->requestedStorageCode_;
                        state_->storageRequested_ = false;
                        if (state_->requestedMap_ >= 0)
                        {
                            targetMap = state_->requestedMap_;
                            targetX = state_->requestedX_;
                            targetY = state_->requestedY_;
                            state_->requestedMap_ = -1;
                            movementMessage = L"已连接";
                        }
                    }
                    if (attribute >= 0)
                    {
                        std::wstring message;
                        try
                        {
                            client.IncreaseAttribute(attribute);
                            message = L"属性已增加 1 点";
                        }
                        catch (const std::exception& error)
                        {
                            if (!client.IsConnected()) throw;
                            message = TextEncoding::ErrorText(error.what());
                        }
                        {
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            state_->shared_.attributePending = false;
                        }
                        movementMessage = message;
                        Publish(Screen::Game, message, &client);
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
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            state_->shared_.petStoragePending = false;
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
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            state_->shared_.itemPending = false;
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
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            state_->shared_.itemPending = false;
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
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            state_->shared_.itemPending = false;
                        }
                        if (!client.IsConnected())
                            throw std::runtime_error(failure);
                        Publish(Screen::Game, message, &client);
                    }
                    {
                        std::lock_guard<std::mutex> lock(state_->mutex_);
                        client.SetHealingSettings(state_->healing_);
                        client.SetCaptureSettings(state_->capture_, state_->captureEnabled_);
                    }
                    if (client.PollGame(pollTimeout))
                    {
                        std::wstring message = movementMessage;
                        {
                            std::lock_guard<std::mutex> lock(state_->mutex_);
                            if (state_->encounterRequested_ || state_->shared_.encounterEnabled || state_->shared_.encounterPending)
                                message = state_->shared_.encounterMessage;
                        }
                        Publish(Screen::Game, message, &client);
                    }
                    UpdateEncounter(client);
                    {
                        std::lock_guard<std::mutex> lock(state_->mutex_);
                        if (state_->encounterRequested_)
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
                std::lock_guard<std::mutex> lock(state_->mutex_);
                state_->shared_.failed = true;
            }
            Publish(Screen::Login, TextEncoding::ErrorText(error.what()));
        }
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            state_->done_ = true;
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
    }
}
