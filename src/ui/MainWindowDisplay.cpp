#include "MainWindow.h"
#include "UiText.h"

namespace Client
{
    void MainWindow::RefreshMapPositions()
    {
        mapPosition_.ShowDropDown(FALSE);
        mapPosition_.ResetContent();
        mapPosition_.AddString(L"请选择地点");
        int selected = mapRegion_.GetCurSel();
        positionMap_ = selected <= 0 ? -1 : static_cast<int>(mapRegion_.GetItemData(selected));
        auto positions = mapPositions_.find(positionMap_);
        bool hasPositions = positions != mapPositions_.end() && !positions->second.empty();
        if (hasPositions)
            for (const auto& position : positions->second)
                mapPosition_.AddString(position.name.c_str());
        mapPosition_.SetCurSel(0);
        mapPosition_.EnableWindow(hasPositions);
    }

    void MainWindow::Refresh()
    {
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        bool game = view_.screen == Screen::Game;
        if (game && !expanded_)
        {
            expanded_ = true;
            SetWindowPos(nullptr, 0, 0, 760, 480, SWP_NOMOVE | SWP_NOZORDER);
            CenterWindow();
        }
        else if ((login || characters) && expanded_)
        {
            expanded_ = false;
            SetWindowPos(nullptr, 0, 0, 460, 330, SWP_NOMOVE | SWP_NOZORDER);
            CenterWindow();
        }
        for (auto& label : labels_)
            label.ShowWindow(login ? SW_SHOW : SW_HIDE);
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (auto edit : edits)
        {
            edit->ShowWindow(login ? SW_SHOW : SW_HIDE);
            edit->EnableWindow(view_.screen == Screen::Login);
        }
        login_.ShowWindow(login ? SW_SHOW : SW_HIDE);
        login_.EnableWindow(view_.screen == Screen::Login);
        enter_.ShowWindow(characters ? SW_SHOW : SW_HIDE);
        enter_.EnableWindow(view_.screen == Screen::Characters);
        bool role = game && mainTabs_.GetCurSel() == 0;
        mainTabs_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        bool mapPage = game && mainTabs_.GetCurSel() == 1;
        bool battle = game && mainTabs_.GetCurSel() == 2;
        bool shop = game && mainTabs_.GetCurSel() == 3;
        developing_.ShowWindow(game && !role && !mapPage && !battle ? SW_SHOW : SW_HIDE);
        RefreshBattle(battle);
        if (!game && battleSettingsWindow_.GetSafeHwnd())
            battleSettingsWindow_.ShowWindow(SW_HIDE);
        miniMap_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        if (!mapPage)
        {
            mapPosition_.ShowDropDown(FALSE);
            mapRegion_.ShowDropDown(FALSE);
        }
        mapPosition_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        mapRegion_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        if (mapPage)
        {
            miniMap_.Update(mapDirectory_, view_.player.mapId, view_.player.mapX, view_.player.mapY);
            if (positionMap_ == -2)
                RefreshMapPositions();
        }
        rolePage_.ShowWindow(role ? SW_SHOW : SW_HIDE);
        rolePage_.Refresh();
        shopTabs_.ShowWindow(shop ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(characters ? SW_SHOW : SW_HIDE);
        exit_.EnableWindow(view_.screen != Screen::Closing);
        exit_.SetWindowText(game ? L"退出登录" : L"退出");
        SetWindowText(login ? L"登录" : characters ? L"选择角色" : L"SA25_#1");
        summary_.ShowWindow(login ? SW_HIDE : SW_SHOW);
        footer_.SetWindowText(view_.message.c_str());
        CString summary = game ? CString(view_.characterName.c_str()) : CString();
        if (game && view_.player.received)
        {
            CString stats;
            stats.Format(L"   等级 %d   HP %d/%d   MP %d/%d   金币 %d", view_.player.level,
                view_.player.hp, view_.player.maxHp, view_.player.mp, view_.player.maxMp, view_.player.gold);
            summary += stats;
            if (view_.player.mapId >= 0)
            {
                CString map;
                auto name = mapNames_.find(view_.player.mapId);
                if (name != mapNames_.end())
                    map.Format(L"   地图 %s", name->second.c_str());
                else
                    map.Format(L"   地图 %d", view_.player.mapId);
                summary += map;
            }
        }
        summary_.SetWindowText(characters ? CString(L"选择角色后进入游戏") : summary);
        int previous = list_.GetNextItem(-1, LVNI_SELECTED);
        DWORD_PTR previousSlot = previous >= 0 ? list_.GetItemData(previous) : 0;
        list_.DeleteAllItems();
        if (characters)
        {
            for (std::size_t i = 0; i < view_.characters.size(); ++i)
            {
                int row = list_.InsertItem(static_cast<int>(i), LegacyText(view_.characters[i].name));
                list_.SetItemText(row, 1, Number(view_.characters[i].level));
                list_.SetItemData(row, i);
            }
        }
        int selection = 0;
        for (int i = 0; i < list_.GetItemCount(); ++i)
            if (list_.GetItemData(i) == previousSlot) selection = i;
        if (list_.GetItemCount())
            list_.SetItemState(selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        Layout();
    }
}
