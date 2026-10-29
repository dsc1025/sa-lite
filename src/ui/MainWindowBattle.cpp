#include "MainWindow.h"
#include "BattleControlIds.h"
#include "BattleSettingsWindow.h"
#include "BattleSceneWindow.h"
#include "BattleGroup.h"
#include "UiText.h"
#include <algorithm>
#include <stdexcept>

namespace Client
{
    void MainWindow::CreateBattleControls()
    {
        CRect empty(0, 0, 0, 0);
        battleTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, IDC_BATTLE_TABS);
        battleTabs_.InsertItem(0, L"练级");
        battleTabs_.InsertItem(1, L"抓宠");
        battleStart_.Create(L"开始", WS_CHILD | WS_TABSTOP | BS_CHECKBOX | BS_PUSHLIKE, empty, this, IDC_BATTLE_START);
        battleSettings_.Create(L"设置", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_SETTINGS);
        battleDisplay_.Create(L"显示", WS_CHILD | WS_TABSTOP | BS_CHECKBOX | BS_PUSHLIKE, empty, this, IDC_BATTLE_DISPLAY);
        battleRound_.Create(L"回合：—", WS_CHILD | SS_RIGHT, empty, this);
        const wchar_t* sides[] = { L"上方队员", L"下方队员" };
        for (int i = 0; i < 2; ++i)
            battleSides_[i]->Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                empty, this, IDC_BATTLE_UPPER + i);
        battleExperience_.Create(L"经验获得：人物 —    宠物 —", WS_CHILD, empty, this);
        capturePet_.Create(L"选择宠物 ▾", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_CAPTURE_PET);
        captureDestination_.Create(L"选择宠物和地点后，直接移动到捕捉起点", WS_CHILD, empty, this);
        captureStatistics_.Create(L"遇敌次数：0    遇到 LV1：0    捕获 LV1：0", WS_CHILD, empty, this);
        const wchar_t* attributes[] = { L"生命≥", L"攻击≥", L"防御≥", L"敏捷≥", L"感知≥" };
        for (int i = 0; i < 5; ++i)
        {
            captureFilterLabels_[i].Create(attributes[i], WS_CHILD, empty, this);
            captureMinimums_[i].Create(WS_CHILD | WS_TABSTOP | WS_BORDER | ES_NUMBER, empty, this, 136 + i);
            captureMinimums_[i].SetLimitText(10);
            if (i == 4)
            {
                captureMinimums_[i].SetWindowText(L"0");
                captureMinimums_[i].EnableWindow(FALSE);
            }
        }
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
        capturePet_.MoveWindow(battlePage.left, battlePage.top, 110, 26);
        captureDestination_.MoveWindow(battlePage.left + 118, battlePage.top + 4,
            std::max(0, battleWidth - 118), 22);
        int buttonTop = battlePage.top + (battleTabs_.GetCurSel() == 1 ? 34 : 0);
        battleStart_.MoveWindow(battlePage.left, buttonTop, 70, 26);
        battleSettings_.MoveWindow(battlePage.left + 156, buttonTop, 70, 26);
        battleDisplay_.MoveWindow(battlePage.left + 78, buttonTop, 70, 26);
        captureStatistics_.MoveWindow(battlePage.left, buttonTop + 34, battleWidth, 22);
        CClientDC dc(this);
        CFont* previousFont = dc.SelectObject(&font_);
        int filterX = battlePage.left;
        for (int i = 0; i < 5; ++i)
        {
            CString caption;
            captureFilterLabels_[i].GetWindowText(caption);
            int width = dc.GetTextExtent(caption).cx;
            captureFilterLabels_[i].MoveWindow(filterX, buttonTop + 63, width, 19);
            filterX += width + 4;
            captureMinimums_[i].MoveWindow(filterX, buttonTop + 60, 48, 22);
            filterX += 48 + 8;
        }
        dc.SelectObject(previousFont);
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
        battleExperience_.MoveWindow(battlePage.left, sideBottom + 8, sideWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            ShowIfChanged(developing_, false);
        bool capturing = visible && battleTabs_.GetCurSel() == 1;
        ShowIfChanged(capturePet_, capturing);
        ShowIfChanged(captureDestination_, capturing);
        ShowIfChanged(captureStatistics_, capturing);
        for (int i = 0; i < 5; ++i)
        {
            ShowIfChanged(captureFilterLabels_[i], capturing);
            ShowIfChanged(captureMinimums_[i], capturing);
            EnableIfChanged(captureMinimums_[i], i != 4 && capturing && !view_.player.inBattle &&
                !view_.encounterEnabled && !view_.encounterPending);
        }
        CString statistics;
        statistics.Format(L"遇敌次数：%llu    遇到 LV1：%llu    捕获 LV1：%llu",
            static_cast<unsigned long long>(view_.captureStatistics.encounters),
            static_cast<unsigned long long>(view_.captureStatistics.levelOneEncounters),
            static_cast<unsigned long long>(view_.captureStatistics.capturedLevelOne));
        SetTextIfChanged(captureStatistics_, statistics);
        EnableIfChanged(capturePet_, capturing && view_.screen == Screen::Game &&
            !view_.player.inBattle && !view_.encounterEnabled && !view_.encounterPending &&
            !view_.petStoragePending && !view_.itemPending);
        CWnd* actions[] = { &battleStart_, &battleDisplay_, &battleSettings_ };
        for (auto control : actions)
            ShowIfChanged(*control, leveling || capturing);
        CWnd* battleControls[] = { &battleRound_,
            battleSides_[0].get(), battleSides_[1].get(), &battleExperience_ };
        for (auto control : battleControls)
            ShowIfChanged(*control, leveling);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            SetTextIfChanged(*battleSides_[side], playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            battleSides_[side]->Update(view_.battle, offset, received);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"第%d回合", view_.battle.round);
        SetTextIfChanged(battleRound_, round);
        ShowIfChanged(battleTabs_, visible);
        EnableIfChanged(battleSettings_, (leveling || capturing) && !view_.encounterEnabled && !view_.encounterPending && !view_.player.inBattle);
        if ((view_.encounterEnabled || view_.encounterPending || view_.player.inBattle) &&
            battleSettingsWindow_->GetSafeHwnd())
            battleSettingsWindow_->ShowWindow(SW_HIDE);
        if ((view_.encounterEnabled || view_.encounterPending || view_.player.inBattle) &&
            captureSettingsWindow_->GetSafeHwnd())
            captureSettingsWindow_->ShowWindow(SW_HIDE);
        EnableIfChanged(battleStart_, (leveling || capturing) && !view_.encounterPending);
        bool started = view_.encounterEnabled || view_.encounterPending;
        if ((battleStart_.GetCheck() == BST_CHECKED) != started)
            battleStart_.SetCheck(started ? BST_CHECKED : BST_UNCHECKED);
        CString experience;
        if (view_.battleResult.received)
            experience.Format(L"经验获得：人物 %s    宠物 %s",
                ExperienceNumber(view_.battleResult.experience).GetString(), ExperienceNumber(view_.battleResult.petExperience).GetString());
        else
            experience = L"经验获得：人物 —    宠物 —";
        SetTextIfChanged(battleExperience_, experience);
    }

    void MainWindow::OnBattleDisplay()
    {
        SetBattleDisplay(!showBattle_);
    }

    LRESULT MainWindow::OnBattleSceneClosed(WPARAM, LPARAM)
    {
        SetBattleDisplay(false);
        return 0;
    }

    void MainWindow::SetBattleDisplay(bool show)
    {
        showBattle_ = show;
        battleDisplay_.SetCheck(showBattle_ ? BST_CHECKED : BST_UNCHECKED);
        battleSceneWindow_->Update(this, view_, showBattle_);
        if (!WritePrivateProfileStringW(L"Battle", L"ShowBattle", show ? L"1" : L"0", configPath_))
        {
            MessageBox(L"无法保存 exe 同目录的 config.ini，请检查目录写入权限。",
                L"战斗画面", MB_OK | MB_ICONWARNING);
        }
    }

    void MainWindow::OnBattleSettings()
    {
        if (view_.screen != Screen::Game || view_.encounterEnabled || view_.encounterPending || view_.player.inBattle)
            return;
        if (battleTabs_.GetCurSel() == 1)
        {
            if (battleSettingsWindow_->GetSafeHwnd())
                battleSettingsWindow_->ShowWindow(SW_HIDE);
            captureSettingsWindow_->Show(this, view_.magic, session_);
        }
        else
        {
            if (captureSettingsWindow_->GetSafeHwnd())
                captureSettingsWindow_->ShowWindow(SW_HIDE);
            battleSettingsWindow_->Show(this, view_.magic, session_);
        }
    }

    void MainWindow::OnBattleStart()
    {
        if (view_.screen != Screen::Game || view_.encounterPending)
            return;
        if (view_.encounterEnabled)
            session_.SetEncounterEnabled(false);
        else
        {
            if (battleTabs_.GetCurSel() == 1)
            {
                std::array<int, 5> minimums{ -1, -1, -1, -1, 0 };
                for (int i = 0; i < 4; ++i)
                {
                    CString text;
                    captureMinimums_[i].GetWindowText(text);
                    if (text.IsEmpty()) continue;
                    if (text.SpanIncluding(L"0123456789") != text || _wtoi64(text) > INT32_MAX)
                    {
                        MessageBox(L"筛选下限请输入有效的非负整数，留空表示不限制。",
                            L"抓宠筛选", MB_OK | MB_ICONWARNING);
                        captureMinimums_[i].SetFocus();
                        return;
                    }
                    minimums[i] = static_cast<int>(_wtoi64(text));
                }
                session_.SetCaptureMinimums(minimums);
            }
            if (battleSettingsWindow_->GetSafeHwnd())
                battleSettingsWindow_->ShowWindow(SW_HIDE);
            if (captureSettingsWindow_->GetSafeHwnd())
                captureSettingsWindow_->ShowWindow(SW_HIDE);
            battleSceneWindow_->PrepareForEncounter();
            session_.SetEncounterEnabled(true, battleTabs_.GetCurSel() == 1, captureSourceInstanceId_);
        }
    }

    void MainWindow::OnCapturePet()
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 2 || battleTabs_.GetCurSel() != 1 ||
            view_.player.inBattle || view_.encounterEnabled || view_.encounterPending ||
            view_.petStoragePending || view_.itemPending)
            return;
        if (capturePets_.empty())
        {
            try { capturePets_ = LoadPetLv1Index(mapDirectory_); }
            catch (const std::exception& error)
            {
                MessageBox(ErrorText(error.what()), L"一级宠物索引加载失败", MB_OK | MB_ICONWARNING);
                return;
            }
        }
        CMenu menu;
        if (!menu.CreatePopupMenu())
            return;
        struct Selection
        {
            const PetLv1Entry* pet;
            const PetLv1Location* location;
        };
        std::vector<Selection> commands;
        for (const auto& pet : capturePets_)
        {
            CMenu locations;
            if (!locations.CreatePopupMenu())
                return;
            for (const auto& location : pet.locations)
            {
                CString label;
                label.Format(L"%s [%d] (%d,%d ~ %d,%d)", location.mapName.c_str(),
                    pet.sourceInstanceId, location.x, location.y, location.x2, location.y2);
                label.Replace(L"&", L"&&");
                commands.push_back({ &pet, &location });
                if (!locations.AppendMenu(MF_STRING, static_cast<UINT_PTR>(commands.size()), label))
                    return;
            }
            CString name(pet.name.c_str());
            name.Replace(L"&", L"&&");
            if (!menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(locations.GetSafeHmenu()), name))
                return;
            locations.Detach();
        }
        CRect rect;
        capturePet_.GetWindowRect(&rect);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            rect.left, rect.bottom, this);
        if (command == 0 || command > commands.size() || view_.screen != Screen::Game ||
            mainTabs_.GetCurSel() != 2 || battleTabs_.GetCurSel() != 1 ||
            view_.player.inBattle || view_.encounterEnabled || view_.encounterPending ||
            view_.petStoragePending || view_.itemPending)
            return;
        const auto& selection = commands[command - 1];
        const auto& location = *selection.location;
        captureSourceInstanceId_ = selection.pet->sourceInstanceId;
        CString label;
        label.Format(L"%s ▾", selection.pet->name.c_str());
        capturePet_.SetWindowText(label);
        label.Format(L"%s：%s [%d] (%d,%d ~ %d,%d)", selection.pet->name.c_str(),
            location.mapName.c_str(), selection.pet->sourceInstanceId, location.x, location.y, location.x2, location.y2);
        captureDestination_.SetWindowText(label);
        session_.RequestMove(location.mapId, location.x, location.y);
    }
}
