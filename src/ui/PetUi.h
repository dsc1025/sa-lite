#pragma once

namespace Game
{
    struct PlayerStatus;
}

namespace Client
{
    inline const wchar_t* const PetStateNames[] = { L"等待", L"战斗", L"骑乘" };

    int PetStateIndex(const Game::PlayerStatus& player, int slot);
}
