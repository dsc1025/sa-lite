#include "RolePage.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "UiText.h"

namespace Client
{
    namespace
    {
        CString ItemTypeName(int type)
        {
            const wchar_t* names[] = {
                L"拳", L"斧头", L"棍棒", L"枪／矛", L"弓", L"盾", L"头盔", L"铠甲",
                L"手镯", L"乐器", L"项链", L"戒指", L"腰带", L"耳环", L"鼻环", L"护身符",
                L"道具", L"回旋镖", L"投掷斧头", L"投掷石", L"料理", L"金属", L"宝石", L"货物"
            };
            return type >= 0 && type < static_cast<int>(sizeof(names) / sizeof(names[0])) ?
                CString(names[type]) : Number(type);
        }
    }

    void RolePage::ShowItemProperties(CListCtrl& list, int row)
    {
        if (view_.screen != Screen::Game || !IsWindowVisible() || tabs_.GetCurSel() != 2 ||
            row < 0 || row >= list.GetItemCount())
            return;
        bool storage = &list == &itemStorage_;
        if (storage && !view_.hasItemStorage) return;
        const auto& items = storage ? view_.itemStorage : view_.items;
        auto slot = static_cast<std::size_t>(list.GetItemData(row));
        if (slot >= items.size() || !items[slot].present)
            return;
        const auto item = items[slot];
        CString text;
        text.Format(L"名称：%s\r\n分类：%s\r\n", LegacyText(item.name).GetString(),
            ItemTypeName(item.type).GetString());
        if (item.levelKnown)
        {
            CString level;
            level.Format(L"使用等级：%d\r\n", item.level);
            text += level;
        }
        text += L"\r\n说明：\r\n";
        text += LegacyText(item.description);
        itemProperties_.Show(GetTopLevelParent(), text);
    }

    void RolePage::OnItemStorageRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (view_.screen != Screen::Game || !IsWindowVisible() || tabs_.GetCurSel() != 2)
            return;
        auto slot = click->iItem < 0 ? view_.itemStorage.size() :
            static_cast<std::size_t>(itemStorage_.GetItemData(click->iItem));
        bool available = view_.hasItemStorage && slot < view_.itemStorage.size() && view_.itemStorage[slot].present;
        std::string code = available ? view_.itemStorage[slot].uniqueCode : std::string();
        if (click->iItem >= 0)
            itemStorage_.SetItemState(click->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        itemStorage_.SetFocus();
        bool idle = !view_.itemPending && !view_.petStoragePending && !view_.player.inBattle &&
            !view_.encounterEnabled && !view_.encounterPending;
        CMenu menu;
        if (!menu.CreatePopupMenu()) return;
        if (available)
        {
            menu.AppendMenu(MF_STRING | (idle ? MF_ENABLED : MF_GRAYED), 1, L"取出");
            menu.AppendMenu(MF_STRING, 3, L"属性");
        }
        menu.AppendMenu(MF_STRING | (idle ? MF_ENABLED : MF_GRAYED), 4, L"刷新");
        CPoint point = click->ptAction;
        itemStorage_.ClientToScreen(&point);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, this);
        if (command == 3 && available)
            ShowItemProperties(itemStorage_, click->iItem);
        else if (command == 1 && available && idle)
            session_.RequestItemStorage(Game::ItemStorageAction::Get, static_cast<int>(slot), 0, code);
        else if (command == 4 && idle)
            session_.RequestItemStorage(Game::ItemStorageAction::List);
    }

    void RolePage::RefreshItems()
    {
        ListUpdate rows(list_);
        const wchar_t* equipment[] = { L"头部", L"身体", L"武器", L"饰品1", L"饰品2" };
        for (std::size_t i = 0; i < view_.items.size(); ++i)
        {
            if (!view_.items[i].present) continue;
            CString name;
            if (i < 5) name = equipment[i];
            else name.Format(L"背包 %d", static_cast<int>(i - 4));
            name += L" · "; name += LegacyText(view_.items[i].name);
            int row = rows.Add(i, name);
            SetCellIfChanged(list_, row, 1, ItemTypeName(view_.items[i].type));
        }
    }

    void RolePage::RefreshItemStorage(bool items)
    {
        if (!items) return;
        ListUpdate rows(itemStorage_);
        bool any = false;
        if (items && view_.hasItemStorage)
        {
            for (std::size_t i = 0; i < view_.itemStorage.size(); ++i)
            {
                const auto& item = view_.itemStorage[i];
                if (!item.present) continue;
                int row = rows.Add(i, LegacyText(item.name));
                SetCellIfChanged(itemStorage_, row, 1, ItemTypeName(item.type));
                any = true;
            }
        }
        if (!any)
        {
            int row = rows.Add(static_cast<DWORD_PTR>(-1), view_.hasItemStorage ? L"仓库为空" :
                view_.itemPending ? L"正在加载…" : L"未加载，右键刷新");
            SetCellIfChanged(itemStorage_, row, 1, CString());
        }
    }
}
