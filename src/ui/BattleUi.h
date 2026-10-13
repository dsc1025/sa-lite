#pragma once

#include <afxwin.h>

#include "../game/Battle.h"

#define IDC_BATTLE_TABS 124
#define IDC_BATTLE_UPPER 126
#define IDC_BATTLE_LOWER 127
#define IDC_BATTLE_INFO 130
#define IDC_BATTLE_START 131
#define IDC_BATTLE_STOP 132
#define IDC_BATTLE_SETTINGS 133

namespace Client
{
    class BattleGroup : public CButton
    {
    public:
        void Update(const Game::BattleState& battle, int offset, bool received);

    protected:
        void DrawItem(LPDRAWITEMSTRUCT item) override;

    private:
        std::array<Game::BattleMember, 10> members_;
        int offset_ = 0;
        bool received_ = false;
    };
}
