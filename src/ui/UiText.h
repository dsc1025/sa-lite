#pragma once

#include <afxwin.h>
#include "../game/GameTypes.h"

namespace Client
{
    inline const wchar_t* const PetStateNames[] = { L"等待", L"战斗", L"骑乘" };

    int PetStateIndex(const Game::PlayerStatus& player, int slot);
    CString LegacyText(const std::string& text);
    CString ErrorText(const char* message);
    std::string AsciiText(CEdit& edit);
    CString Number(int value);
    CString Ratio(int value, int maximum);
}
