#include "MainWindow.h"
#include "BattleUi.h"
#include "UiText.h"
#include <algorithm>

namespace Client
{
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
        dc->RestoreDC(saved);
    }

    void MainWindow::CreateBattleControls()
    {
        CRect empty(0, 0, 0, 0);
        battleTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, IDC_BATTLE_TABS);
        battleTabs_.InsertItem(0, L"升级");
        battleTabs_.InsertItem(1, L"抓宠");
        battleStart_.Create(L"开始", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_START);
        battleStop_.Create(L"停止", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_STOP);
        battleRound_.Create(L"回合：—", WS_CHILD | SS_RIGHT, empty, this);
        const wchar_t* sides[] = { L"上方队员", L"下方队员" };
        for (int i = 0; i < 2; ++i)
        {
            battleSides_[i].Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW, empty, this, IDC_BATTLE_UPPER + i);
            battleWaiting_[i].Create(L"等待战斗队员数据", WS_CHILD | SS_CENTER | SS_CENTERIMAGE, empty, this);
            battleMembers_[i].Create(WS_CHILD | WS_TABSTOP | WS_BORDER | WS_CLIPSIBLINGS |
                LVS_REPORT | LVS_SINGLESEL, empty, this, IDC_BATTLE_UPPER_MEMBERS + i);
            battleMembers_[i].SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            battleMembers_[i].InsertColumn(0, L"位置", LVCFMT_LEFT, 40);
            battleMembers_[i].InsertColumn(1, L"名称", LVCFMT_LEFT, 110);
            battleMembers_[i].InsertColumn(2, L"等级", LVCFMT_RIGHT, 40);
            battleMembers_[i].InsertColumn(3, L"HP", LVCFMT_RIGHT, 95);
            battleSides_[i].SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        battleInfoLabel_.Create(L"信息提示", WS_CHILD, empty, this);
        battleInfo_.Create(WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, empty, this, IDC_BATTLE_INFO);
        battleInfo_.SetWindowText(L"等待战斗信息。");
        battleExperience_.Create(L"经验获得：本场 —    累计 —", WS_CHILD, empty, this);
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
        battleStart_.MoveWindow(battlePage.right - 280, battlePage.top, 70, 26);
        battleStop_.MoveWindow(battlePage.right - 202, battlePage.top, 70, 26);
        battleRound_.MoveWindow(battlePage.right - 120, battlePage.top + 3, 120, 22);
        int sideTop = battlePage.top + 34;
        int sideHeight = std::max(0, battlePage.Height() - 140);
        int sideWidth = std::max(0, (battleWidth - 12) / 2);
        for (int i = 0; i < 2; ++i)
        {
            int sideLeft = battlePage.left + i * (sideWidth + 12);
            int memberWidth = i == 0 ? sideWidth : std::max(0, battleWidth - sideWidth - 12);
            battleSides_[i].MoveWindow(sideLeft, sideTop, memberWidth, sideHeight);
            battleWaiting_[i].MoveWindow(sideLeft + 8, sideTop + 20,
                std::max(0, memberWidth - 16), std::max(0, sideHeight - 28));
            battleMembers_[i].MoveWindow(sideLeft + 8, sideTop + 20,
                std::max(0, memberWidth - 16), std::max(0, sideHeight - 28));
        }
        int infoTop = sideTop + sideHeight + 8;
        battleInfoLabel_.MoveWindow(battlePage.left, infoTop, battleWidth, 20);
        battleInfo_.MoveWindow(battlePage.left, infoTop + 22, battleWidth, 46);
        battleExperience_.MoveWindow(battlePage.left, infoTop + 74, battleWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            developing_.ShowWindow(leveling ? SW_HIDE : SW_SHOW);
        CWnd* battleControls[] = { &battleStart_, &battleStop_, &battleRound_,
            &battleSides_[0], &battleSides_[1],
            &battleInfoLabel_, &battleInfo_, &battleExperience_ };
        for (auto control : battleControls)
            control->ShowWindow(leveling ? SW_SHOW : SW_HIDE);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            battleWaiting_[side].ShowWindow(leveling && !received ? SW_SHOW : SW_HIDE);
            battleMembers_[side].ShowWindow(leveling && received ? SW_SHOW : SW_HIDE);
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            battleSides_[side].SetWindowText(playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            auto& list = battleMembers_[side];
            list.SetRedraw(FALSE);
            int row = 0;
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            if (received)
                for (int slot = offset; slot < offset + 10; ++slot)
                {
                    const auto& member = view_.battle.members[slot];
                    if (!member.present)
                        continue;
                    CString position;
                    position.Format(L"%X", slot);
                    if (row >= list.GetItemCount())
                        list.InsertItem(row, position);
                    CString cells[] = { position, LegacyText(member.name), Number(member.level),
                        Ratio(member.hp, member.maxHp) };
                    for (int column = 0; column < 4; ++column)
                        if (list.GetItemText(row, column) != cells[column])
                            list.SetItemText(row, column, cells[column]);
                    ++row;
                }
            while (list.GetItemCount() > row)
                list.DeleteItem(row);
            list.SetRedraw(TRUE);
            list.Invalidate(FALSE);
            list.ShowWindow(leveling && received ? SW_SHOW : SW_HIDE);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"回合：%d", view_.battle.round);
        battleRound_.SetWindowText(round);
        battleTabs_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        battleStart_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending);
        battleStop_.EnableWindow(leveling && (view_.encounterEnabled || view_.encounterPending));
        battleInfo_.SetWindowText(view_.encounterMessage.c_str());
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
