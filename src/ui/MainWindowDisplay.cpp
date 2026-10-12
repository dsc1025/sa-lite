#include "MainWindow.h"
#include "UiText.h"

namespace Client
{
    void MainWindow::AddRow(const wchar_t* name, const CString& value)
    {
        int row = detail_.InsertItem(detail_.GetItemCount(), name);
        detail_.SetItemText(row, 1, value);
    }

    void MainWindow::RefreshDetail()
    {
        detail_.DeleteAllItems();
        if (mainTabs_.GetCurSel() != 0) return;
        int tab = tabs_.GetCurSel();
        if (tab == 0)
        {
            const auto& p = view_.player;
            if (!p.received)
            {
                AddRow(L"状态", L"正在等待人物数据…");
                return;
            }
            AddRow(L"等级 / 转生", Ratio(p.level, p.rebirth));
            AddRow(L"生命", Ratio(p.hp, p.maxHp));
            AddRow(L"魔力", Ratio(p.mp, p.maxMp));
            AddRow(L"体力", Number(p.vital)); AddRow(L"腕力", Number(p.strength));
            AddRow(L"耐力", Number(p.toughness)); AddRow(L"速度", Number(p.dexterity));
            AddRow(L"攻击", Number(p.attack)); AddRow(L"防御", Number(p.defence));
            AddRow(L"敏捷", Number(p.quick)); AddRow(L"魅力", Number(p.charm));
            AddRow(L"运气", Number(p.luck)); AddRow(L"感知", Number(p.perception));
            CString elements;
            elements.Format(L"地 %d   水 %d   火 %d   风 %d", p.earth, p.water, p.fire, p.wind);
            AddRow(L"元素", elements); AddRow(L"经验 / 下级经验", Ratio(p.experience, p.nextExperience));
            AddRow(L"金币", Number(p.gold)); AddRow(L"决斗积分", Number(p.duelPoints));
        }
        else
        {
            if (tab == 2 && !view_.hasItems)
            {
                AddRow(L"状态", L"正在等待道具数据…");
                return;
            }
            int selected = list_.GetNextItem(-1, LVNI_SELECTED);
            if (selected < 0)
            {
                AddRow(L"状态", tab == 2 ? L"背包与装备为空" : L"选择一个宠物查看属性");
                return;
            }
            auto slot = static_cast<std::size_t>(list_.GetItemData(selected));
            if (tab == 1 && slot < view_.pets.size())
            {
                const auto& p = view_.pets[slot];
                if (!p.received || !p.present)
                {
                    AddRow(L"状态", p.received ? L"这个宠物栏位为空" : L"正在等待宠物数据…");
                    return;
                }
                AddRow(L"名称", LegacyText(p.name)); AddRow(L"昵称", LegacyText(p.nickname));
                AddRow(L"等级 / 转生", Ratio(p.level, p.rebirth));
                AddRow(L"生命", Ratio(p.hp, p.maxHp)); AddRow(L"魔力", Ratio(p.mp, p.maxMp));
                AddRow(L"攻击", Number(p.attack)); AddRow(L"防御", Number(p.defence));
                AddRow(L"敏捷", Number(p.quick)); AddRow(L"忠诚", Number(p.loyalty));
                AddRow(L"感知", Number(p.perception)); AddRow(L"技能栏", Number(p.skillSlots));
                CString elements;
                elements.Format(L"地 %d   水 %d   火 %d   风 %d", p.earth, p.water, p.fire, p.wind);
                AddRow(L"元素", elements); AddRow(L"经验 / 下级经验", Ratio(p.experience, p.nextExperience));
            }
            else if (tab == 2 && slot < view_.items.size())
            {
                const auto& item = view_.items[slot];
                AddRow(L"名称", LegacyText(item.name)); AddRow(L"使用等级", Number(item.level));
                AddRow(L"说明", LegacyText(item.description));
            }
        }
    }

    void MainWindow::Refresh()
    {
        refreshing_ = true;
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
        bool shop = game && mainTabs_.GetCurSel() == 2;
        developing_.ShowWindow(game && !role && !mapPage ? SW_SHOW : SW_HIDE);
        miniMap_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        if (mapPage)
        {
            auto name = mapNames_.find(view_.player.mapId);
            miniMap_.Update(mapDirectory_, view_.player.mapId, view_.player.mapX, view_.player.mapY,
                name != mapNames_.end() ? CString(name->second.c_str()) : Number(view_.player.mapId));
        }
        tabs_.ShowWindow(role ? SW_SHOW : SW_HIDE);
        shopTabs_.ShowWindow(shop ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(characters || (role && tabs_.GetCurSel() != 0) ? SW_SHOW : SW_HIDE);
        detail_.ShowWindow(role ? SW_SHOW : SW_HIDE);
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
        bool petColumns = role && tabs_.GetCurSel() == 1;
        int columns = list_.GetHeaderCtrl()->GetItemCount();
        if (petColumns && columns == 2)
            list_.InsertColumn(2, L"状态", LVCFMT_LEFT, 60);
        else if (!petColumns && columns == 3)
            list_.DeleteColumn(2);
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
        else if (role && tabs_.GetCurSel() == 1)
        {
            for (std::size_t i = 0; i < view_.pets.size(); ++i)
            {
                const auto& pet = view_.pets[i];
                CString name = pet.present ? LegacyText(pet.nickname.empty() ? pet.name : pet.nickname) : CString(L"空栏位");
                if (!pet.received) name = L"等待数据…";
                int row = list_.InsertItem(static_cast<int>(i), name);
                list_.SetItemText(row, 1, pet.present ? Number(pet.level) : CString());
                CString state;
                if (pet.received && pet.present)
                {
                    int index = PetStateIndex(view_.player, static_cast<int>(i));
                    state = PetStateNames[index];
                }
                list_.SetItemText(row, 2, state);
                list_.SetItemData(row, i);
            }
        }
        else if (role && tabs_.GetCurSel() == 2)
        {
            const wchar_t* equipment[] = { L"头部", L"身体", L"武器", L"饰品1", L"饰品2" };
            for (std::size_t i = 0; i < view_.items.size(); ++i)
            {
                if (!view_.items[i].present) continue;
                CString name;
                if (i < 5) name = equipment[i];
                else name.Format(L"背包 %d", static_cast<int>(i - 4));
                name += L" · "; name += LegacyText(view_.items[i].name);
                int row = list_.InsertItem(list_.GetItemCount(), name);
                list_.SetItemText(row, 1, Number(view_.items[i].level));
                list_.SetItemData(row, i);
            }
        }
        int selection = 0;
        for (int i = 0; i < list_.GetItemCount(); ++i)
            if (list_.GetItemData(i) == previousSlot) selection = i;
        if (list_.GetItemCount())
            list_.SetItemState(selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (game) RefreshDetail();
        refreshing_ = false;
        Layout();
    }
}
