#include "MainWindow.h"
#include "UiText.h"

namespace Client
{
    void MainWindow::Refresh()
    {
        createCharacterWindow_.Update(view_);
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
        enter_.EnableWindow(view_.screen == Screen::Characters && !view_.characters.empty() && !view_.createPending);
        createCharacter_.ShowWindow(characters ? SW_SHOW : SW_HIDE);
        createCharacter_.EnableWindow(view_.screen == Screen::Characters && view_.characters.size() < 2 && !view_.createPending);
        bool role = game && mainTabs_.GetCurSel() == 0;
        mainTabs_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        bool mapPage = game && mainTabs_.GetCurSel() == 1;
        bool battle = game && mainTabs_.GetCurSel() == 2;
        bool shop = game && mainTabs_.GetCurSel() == 3;
        developing_.ShowWindow(game && !role && !mapPage && !battle ? SW_SHOW : SW_HIDE);
        RefreshBattle(battle);
        battleSceneWindow_.Update(this, view_, session_);
        if (!game && battleSettingsWindow_.GetSafeHwnd())
            battleSettingsWindow_.ShowWindow(SW_HIDE);
        miniMap_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        mapPosition_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        mapRegion_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        if (mapPage)
        {
            miniMap_.Update(mapDirectory_, view_.player.mapId, view_.player.mapX, view_.player.mapY);
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
                CString position;
                position.Format(L"   坐标 %d, %d", view_.player.mapX, view_.player.mapY);
                summary += position;
            }
        }
        summary_.SetWindowText(characters ? CString(view_.characters.empty() ? L"暂无角色，请创建角色" : L"选择角色后进入游戏") : summary);
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
        if (!view_.createdCharacter.empty() && selectedCreationRevision_ != view_.createRevision)
            for (std::size_t i = 0; i < view_.characters.size(); ++i)
                if (view_.characters[i].name == view_.createdCharacter) selection = static_cast<int>(i);
        selectedCreationRevision_ = view_.createRevision;
        if (list_.GetItemCount())
            list_.SetItemState(selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        Layout();
    }
}
