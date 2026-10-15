#pragma once

#include "GameSession.h"
#include "SessionView.h"
#include "../game/BattleSettings.h"
#include "../game/Character.h"
#include "../game/ItemTypes.h"
#include "../game/PetTypes.h"
#include <condition_variable>
#include <cstdint>
#include <string>
#include <mutex>
#include <thread>

namespace Client
{
    struct GameSession::State
    {
        HWND window_ = nullptr;
        UINT message_ = 0;
        SessionView shared_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
        bool stop_ = false, done_ = true;
        bool encounterRequested_ = false;
        Game::HealingSettings healing_;
        Game::CaptureSettings capture_;
        bool captureEnabled_ = false;
        int requestedCharacter_ = -1;
        bool createRequested_ = false;
        Game::NewCharacter requestedCreation_;
        int requestedPet_ = -1, requestedPetState_ = 0;
        int requestedAttribute_ = -1;
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
