#include "MainWindow.h"
#include "../AppInfo.h"
#include "ChatWindow.h"
#include "MiniMap.h"
#include "CreateCharacterWindow.h"
#include "BattleSettingsWindow.h"
#include "BattleSceneWindow.h"
#include "RolePage.h"
#include "AuctionPage.h"
#include "UiText.h"

namespace Client
{
    void MainWindow::Refresh()
    {
        chatWindow_->Update(view_);
        bool layoutChanged = displayedScreen_ != static_cast<int>(view_.screen) ||
            displayedTab_ != mainTabs_.GetCurSel();
        displayedScreen_ = static_cast<int>(view_.screen);
        displayedTab_ = mainTabs_.GetCurSel();
        createCharacterWindow_->Update(view_);
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
            ShowIfChanged(label, login);
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (auto edit : edits)
        {
            ShowIfChanged(*edit, login);
            EnableIfChanged(*edit, view_.screen == Screen::Login);
        }
        ShowIfChanged(update_, login);
        EnableIfChanged(update_, view_.screen == Screen::Login);
        ShowIfChanged(login_, login);
        EnableIfChanged(login_, view_.screen == Screen::Login);
        ShowIfChanged(enter_, characters);
        EnableIfChanged(enter_, view_.screen == Screen::Characters && !view_.characters.empty() && !view_.createPending);
        ShowIfChanged(createCharacter_, characters);
        EnableIfChanged(createCharacter_, view_.screen == Screen::Characters && view_.characters.size() < 2 && !view_.createPending);
        bool role = game && mainTabs_.GetCurSel() == 0;
        ShowIfChanged(mainTabs_, game);
        bool mapPage = game && mainTabs_.GetCurSel() == 1;
        bool battle = game && mainTabs_.GetCurSel() == 2;
        bool shop = game && mainTabs_.GetCurSel() == 3;
        ShowIfChanged(developing_, game && !role && !mapPage && !shop && (!battle || battleTabs_.GetCurSel() != 0));
        RefreshBattle(battle);
        battleSceneWindow_->Update(this, view_, showBattle_);
        if (!game && battleSettingsWindow_->GetSafeHwnd())
            battleSettingsWindow_->ShowWindow(SW_HIDE);
        if (!game && captureSettingsWindow_->GetSafeHwnd())
            captureSettingsWindow_->ShowWindow(SW_HIDE);
        ShowIfChanged(*miniMap_, mapPage);
        ShowIfChanged(mapPosition_, mapPage);
        ShowIfChanged(mapRegion_, mapPage);
        if (mapPage)
        {
            miniMap_->Update(mapDirectory_, view_.player.mapId, view_.player.mapX, view_.player.mapY);
        }
        ShowIfChanged(*rolePage_, role);
        rolePage_->Refresh();
        ShowIfChanged(shopTabs_, shop);
        ShowIfChanged(*auctionPage_, shop && shopTabs_.GetCurSel() == 3);
        auctionPage_->Refresh();
        shop = shop && shopTabs_.GetCurSel() < 3;
        ShowIfChanged(shopList_, shop);
        ShowIfChanged(shopRefresh_, shop);
        ShowIfChanged(shopBuy_, shop);
        ShowIfChanged(shopQuantity_, shop);
        ShowIfChanged(shopQuantityLabel_, shop);
        ShowIfChanged(shopCurrency_, shop);
        bool shopReady = shop && !view_.shopPending && !view_.player.inBattle &&
            !view_.encounterEnabled && !view_.encounterPending && !view_.itemPending &&
            !view_.petStoragePending;
        EnableIfChanged(shopRefresh_, shopReady);
        EnableIfChanged(shopBuy_, shopReady && view_.shopCategory == shopTabs_.GetCurSel());
        EnableIfChanged(shopQuantity_, shopReady);
        EnableIfChanged(shopCurrency_, shopReady);
        if (shop)
        {
            ListUpdate rows(shopList_);
            if (view_.shopCategory == shopTabs_.GetCurSel())
                for (const auto& item : view_.shopItems)
                {
                    int row = rows.Add(item.id, LegacyText(item.name));
                    SetCellIfChanged(shopList_, row, 1, item.cost < 0 ? CString(L"不可用") : Number(item.cost));
                    SetCellIfChanged(shopList_, row, 2, item.pearl < 0 ? CString(L"不可用") : Number(item.pearl));
                    SetCellIfChanged(shopList_, row, 3, item.stock < 0 ? CString(L"不限量") : Number(item.stock));
                    SetCellIfChanged(shopList_, row, 4, Number(item.level));
                    SetCellIfChanged(shopList_, row, 5, LegacyText(item.description));
                }
        }
        ShowIfChanged(list_, characters);
        EnableIfChanged(exit_, view_.screen != Screen::Closing);
        SetTextIfChanged(exit_, game ? L"退出登录" : L"退出");
        CString title;
        title.Format(L"%s v%s", AppInfo::Name, AppInfo::Version);
        SetTextIfChanged(*this, login ? L"登录" : characters ? L"选择角色" : title.GetString());
        ShowIfChanged(summary_, !login);
        SetTextIfChanged(footer_, view_.shopPending ? L"黑市正在处理…" :
            view_.auction.pending ? L"竞拍正在处理，请等待服务器确认…" : view_.message.c_str());
        CString summary = game ? CString(view_.characterName.c_str()) : CString();
        if (game && view_.player.received)
        {
            CString stats;
            stats.Format(L"   等级 %d   COST %d   P %d", view_.player.level,
                view_.player.gold, view_.pearlBalance);
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
        SetTextIfChanged(summary_, characters ? CString(view_.characters.empty() ? L"暂无角色，请创建角色" : L"选择角色后进入游戏") : summary);
        if (characters)
        {
            ListUpdate rows(list_);
            for (std::size_t i = 0; i < view_.characters.size(); ++i)
            {
                int row = rows.Add(i, LegacyText(view_.characters[i].name));
                SetCellIfChanged(list_, row, 1, Number(view_.characters[i].level));
            }
        }
        int selection = list_.GetNextItem(-1, LVNI_SELECTED);
        if (!view_.createdCharacter.empty() && selectedCreationRevision_ != view_.createRevision)
            for (std::size_t i = 0; i < view_.characters.size(); ++i)
                if (view_.characters[i].name == view_.createdCharacter) selection = static_cast<int>(i);
        selectedCreationRevision_ = view_.createRevision;
        if (characters && list_.GetItemCount() && (selection < 0 || selection != list_.GetNextItem(-1, LVNI_SELECTED)))
            list_.SetItemState(selection < 0 ? 0 : selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (layoutChanged) Layout();
    }
}
