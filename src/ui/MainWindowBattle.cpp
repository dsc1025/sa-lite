#include "MainWindow.h"
#include "BattleControlIds.h"
#include "BattleSettingsWindow.h"
#include "BattleSceneWindow.h"
#include "BattleGroup.h"
#include "UiText.h"
#include <algorithm>

namespace Client
{
    void MainWindow::CreateBattleControls()
    {
        CRect empty(0, 0, 0, 0);
        battleTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, IDC_BATTLE_TABS);
        battleTabs_.InsertItem(0, L"练级");
        battleTabs_.InsertItem(1, L"抓宠");
        battleStart_.Create(L"开始", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_START);
        battleStop_.Create(L"停止", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_STOP);
        battleSettings_.Create(L"设置", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_SETTINGS);
        battleRound_.Create(L"回合：—", WS_CHILD | SS_RIGHT, empty, this);
        const wchar_t* sides[] = { L"上方队员", L"下方队员" };
        for (int i = 0; i < 2; ++i)
            battleSides_[i]->Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                empty, this, IDC_BATTLE_UPPER + i);
        battleExperience_.Create(L"经验获得：人物 —    宠物 —", WS_CHILD, empty, this);
    }

    void MainWindow::LayoutBattle(const CRect& mainPage)
    {
        battleTabs_.MoveWindow(mainPage);
        CRect battlePage = mainPage;
        battleTabs_.AdjustRect(FALSE, &battlePage);
        battlePage.DeflateRect(6, 6);
        if (mainTabs_.GetCurSel() == 2)
            developing_.MoveWindow(battlePage);
        int battleWidth = std::max(0, battlePage.Width());
        battleStart_.MoveWindow(battlePage.left, battlePage.top, 70, 26);
        battleStop_.MoveWindow(battlePage.left + 78, battlePage.top, 70, 26);
        battleSettings_.MoveWindow(battlePage.left + 156, battlePage.top, 70, 26);
        int sideTop = battlePage.top + 34;
        int sideBottom = std::max<LONG>(sideTop + 24, battlePage.bottom - 30);
        int sideWidth = std::max(0, (battleWidth - 12) / 2);
        int rightLeft = battlePage.left + sideWidth + 12;
        battleSides_[0]->MoveWindow(battlePage.left, sideTop, sideWidth,
            std::max(0, sideBottom - sideTop));
        battleSides_[1]->MoveWindow(rightLeft, sideTop + 24,
            std::max<LONG>(0, battlePage.right - rightLeft), std::max(0, sideBottom - sideTop));
        battleRound_.MoveWindow(rightLeft, sideTop,
            std::max<LONG>(0, battlePage.right - rightLeft - 8), 22);
        battleExperience_.MoveWindow(battlePage.left, sideBottom + 8, battleWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            developing_.ShowWindow(leveling ? SW_HIDE : SW_SHOW);
        CWnd* battleControls[] = { &battleStart_, &battleStop_, &battleSettings_, &battleRound_,
            battleSides_[0].get(), battleSides_[1].get(), &battleExperience_ };
        for (auto control : battleControls)
            control->ShowWindow(leveling ? SW_SHOW : SW_HIDE);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            battleSides_[side]->SetWindowText(playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            battleSides_[side]->Update(view_.battle, offset, received);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"第%d回合", view_.battle.round);
        battleRound_.SetWindowText(round);
        battleTabs_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        battleSettings_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending && !view_.player.inBattle);
        if ((view_.encounterEnabled || view_.encounterPending || view_.player.inBattle) &&
            battleSettingsWindow_->GetSafeHwnd())
            battleSettingsWindow_->ShowWindow(SW_HIDE);
        battleStart_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending);
        battleStop_.EnableWindow(leveling && (view_.encounterEnabled || view_.encounterPending));
        CString experience;
        if (view_.battleResult.received)
            experience.Format(L"经验获得：人物 %u    宠物 %llu",
                view_.battleResult.experience, static_cast<unsigned long long>(view_.battleResult.petExperience));
        else
            experience = L"经验获得：人物 —    宠物 —";
        battleExperience_.SetWindowText(experience);
    }

    void MainWindow::OnBattleSettings()
    {
        if (view_.screen != Screen::Game || view_.encounterEnabled || view_.encounterPending || view_.player.inBattle)
            return;
        battleSettingsWindow_->Show(this, view_.magic, session_);
    }

    void MainWindow::OnBattleStart()
    {
        if (view_.screen == Screen::Game && !view_.encounterEnabled && !view_.encounterPending)
        {
            if (battleSettingsWindow_->GetSafeHwnd())
                battleSettingsWindow_->ShowWindow(SW_HIDE);
            battleSceneWindow_->PrepareForEncounter();
            session_.SetEncounterEnabled(true);
        }
    }

    void MainWindow::OnBattleStop()
    {
        if (view_.screen == Screen::Game)
            session_.SetEncounterEnabled(false);
    }
}
