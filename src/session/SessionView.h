#pragma once

#include "../game/GameTypes.h"
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
        std::vector<Game::PetStatus> pets;
        std::vector<Game::ItemStatus> items;
        bool hasItems = false;
        bool failed = false;
    };
}
