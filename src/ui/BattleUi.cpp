#include "MainWindow.h"
#include "BattleUi.h"
#include "UiText.h"
#include <algorithm>

namespace Client
{
    void BattleGroup::Update(const Game::BattleState& battle, int offset, bool received)
    {
        for (int i = 0; i < 10; ++i)
            members_[i] = battle.members[offset + i];
        offset_ = offset;
        received_ = received;
        Invalidate(FALSE);
    }

    void BattleGroup::DrawItem(LPDRAWITEMSTRUCT item)
    {
        CDC* dc = CDC::FromHandle(item->hDC);
        int saved = dc->SaveDC();
        if (CFont* font = GetFont())
            dc->SelectObject(font);
        CRect area(item->rcItem);
        dc->FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
        CString caption;
        GetWindowText(caption);
        CSize size = dc->GetTextExtent(caption);
        CRect border = area;
        border.top += size.cy / 2;
        dc->DrawEdge(&border, EDGE_ETCHED, BF_RECT);
        CRect title(area.left + 8, area.top, area.left + 16 + size.cx, area.top + size.cy);
        dc->FillSolidRect(title, GetSysColor(COLOR_BTNFACE));
        dc->SetTextColor(GetSysColor(COLOR_BTNTEXT));
        dc->SetBkMode(TRANSPARENT);
        dc->DrawText(caption, title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        CRect content(area.left + 12, area.top + size.cy + 8, area.right - 10, area.bottom - 10);
        if (!received_)
            dc->DrawText(L"等待战斗队员数据", content, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        else
        {
            int lineHeight = size.cy + 4;
            int spacing = std::max(1, std::min(lineHeight, content.Height() / 10));
            int top = content.top + (content.Height() - spacing * 10) / 2;
            // Each of the five fixed positions keeps its pet above its character.
            const int order[] = { 9, 4, 7, 2, 5, 0, 6, 1, 8, 3 };
            for (int rowIndex = 0; rowIndex < 10; ++rowIndex)
            {
                int slot = order[rowIndex];
                const auto& member = members_[slot];
                if (!member.present)
                    continue;
                CString text;
                text.Format(L"* [%X] %s  Lv:%d (%d/%d)", offset_ + slot,
                    LegacyText(member.name).GetString(), member.level, member.hp, member.maxHp);
                if (!member.rideName.empty())
                {
                    CString ride;
                    ride.Format(L" [骑] %s  Lv:%d (%d/%d)", LegacyText(member.rideName).GetString(),
                        member.rideLevel, member.rideHp, member.rideMaxHp);
                    text += ride;
                }
                int rowTop = top + rowIndex * spacing;
                CRect row(content.left, rowTop, content.right,
                    std::min<LONG>(rowTop + spacing, content.bottom));
                if (row.bottom > row.top)
                    dc->DrawText(text, row, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        dc->RestoreDC(saved);
    }

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
            battleSides_[i].Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                empty, this, IDC_BATTLE_UPPER + i);
        battleInfo_.Create(L"战斗提示：等待战斗", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            empty, this, IDC_BATTLE_INFO);
        battleExperience_.Create(L"经验获得：本场 —    累计 0", WS_CHILD, empty, this);
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
        battleSides_[0].MoveWindow(battlePage.left, sideTop, sideWidth,
            std::max(0, sideBottom - sideTop - 24));
        battleSides_[1].MoveWindow(rightLeft, sideTop + 24,
            std::max<LONG>(0, battlePage.right - rightLeft), std::max(0, sideBottom - sideTop - 24));
        battleRound_.MoveWindow(rightLeft, sideTop,
            std::max<LONG>(0, battlePage.right - rightLeft - 8), 22);
        battleInfo_.MoveWindow(battlePage.left + 8, sideBottom - 22,
            std::max(0, sideWidth - 16), 22);
        battleExperience_.MoveWindow(battlePage.left, sideBottom + 8, battleWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            developing_.ShowWindow(leveling ? SW_HIDE : SW_SHOW);
        CWnd* battleControls[] = { &battleStart_, &battleStop_, &battleSettings_, &battleRound_,
            &battleSides_[0], &battleSides_[1], &battleInfo_, &battleExperience_ };
        for (auto control : battleControls)
            control->ShowWindow(leveling ? SW_SHOW : SW_HIDE);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            battleSides_[side].SetWindowText(playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            battleSides_[side].Update(view_.battle, offset, received);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"第%d回合", view_.battle.round);
        battleRound_.SetWindowText(round);
        battleTabs_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        battleStart_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending);
        battleStop_.EnableWindow(leveling && (view_.encounterEnabled || view_.encounterPending));
        CString message = L"战斗提示：";
        message += view_.battleResult.message.empty() ? L"等待战斗" : view_.battleResult.message.c_str();
        battleInfo_.SetWindowText(message);
        CString experience;
        if (view_.battleResult.received)
            experience.Format(L"经验获得：本场 %u    累计 %llu",
                view_.battleResult.experience, static_cast<unsigned long long>(view_.battleResult.totalExperience));
        else
            experience.Format(L"经验获得：本场 —    累计 %llu",
                static_cast<unsigned long long>(view_.battleResult.totalExperience));
        battleExperience_.SetWindowText(experience);
    }

    void MainWindow::OnBattleStart()
    {
        if (view_.screen == Screen::Game)
            session_.SetEncounterEnabled(true);
    }

    void MainWindow::OnBattleStop()
    {
        if (view_.screen == Screen::Game)
            session_.SetEncounterEnabled(false);
    }
}
