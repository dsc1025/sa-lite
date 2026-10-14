#pragma once

#include <afxwin.h>
#include "../game/Battle.h"
#include <array>

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
