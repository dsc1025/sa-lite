#pragma once

#include "../game/Character.h"
#include "../game/PlayerStatus.h"
#include "../game/PetTypes.h"
#include "../game/ItemTypes.h"
#include "../game/MagicStatus.h"
#include "../game/Battle.h"
#include <string>
#include <vector>

namespace Client
{
    enum class Screen { Login, Connecting, Characters, Entering, Game, Closing };

    struct SessionView
    {
        Screen screen = Screen::Login;
        std::wstring message = L"请输入服务器和账号信息";
        std::wstring characterName;
        std::vector<Game::Character> characters;
        Game::PlayerStatus player;
        Game::BattleState battle;
        Game::BattleResult battleResult;
        std::vector<Game::PetStatus> pets;
        std::vector<Game::ItemStatus> items;
        std::vector<Game::MagicStatus> magic;
        bool hasItems = false;
        bool itemPending = false;
        bool attributePending = false;
        std::vector<Game::ItemStatus> itemStorage;
        bool hasItemStorage = false;
        std::vector<Game::StoredPet> petStorage;
        bool hasPetStorage = false;
        bool petStoragePending = false;
        bool encounterEnabled = false;
        bool encounterPending = false;
        std::wstring encounterMessage = L"遇敌未开始";
        bool failed = false;
        bool createPending = false;
        unsigned int createRevision = 0;
        std::string createdCharacter;
    };
}
