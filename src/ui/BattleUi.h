#pragma once

#include <afxwin.h>

#include "../game/Battle.h"

#define IDC_BATTLE_TABS 124
#define IDC_BATTLE_UPPER 126
#define IDC_BATTLE_LOWER 127
#define IDC_BATTLE_INFO 130
#define IDC_BATTLE_START 131
#define IDC_BATTLE_STOP 132
#define IDC_BATTLE_UPPER_MEMBERS 133
#define IDC_BATTLE_LOWER_MEMBERS 134

namespace Client
{
    class BattleGroup : public CButton
    {
    protected:
        void DrawItem(LPDRAWITEMSTRUCT item) override;
    };
}
