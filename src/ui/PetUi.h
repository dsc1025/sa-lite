#pragma once
#include <afxwin.h>

namespace Game
{
    struct PlayerStatus;
    struct PetStatus;
    struct ItemStatus;
}

namespace Client
{
    inline const wchar_t* const PetStateNames[] = { L"等待", L"战斗", L"骑乘" };

    int PetStateIndex(const Game::PlayerStatus& player, int slot);
    CString PetPropertiesText(const Game::PetStatus& pet);
    CString ItemTypeName(int type);
    CString ItemPropertiesText(const Game::ItemStatus& item);
}
