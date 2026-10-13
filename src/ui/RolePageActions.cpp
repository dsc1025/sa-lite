#include "RolePage.h"
#include "UiText.h"

namespace Client
{
    void RolePage::OnListRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        int tab = tabs_.GetCurSel();
        if (view_.screen != Screen::Game || !IsWindowVisible() ||
            (tab != 1 && tab != 2) || click->iItem < 0)
            return;
        int slot = static_cast<int>(list_.GetItemData(click->iItem));
        bool pet = tab == 1;
        bool enabled = true;
        std::string uniqueCode;
        bool food = false;
        std::uint32_t itemRevision = 0;
        CString name;
        if (pet)
        {
            if (slot < 0 || slot >= static_cast<int>(view_.pets.size()) ||
                !view_.pets[slot].received || !view_.pets[slot].present)
                return;
            const auto& status = view_.pets[slot];
            name = LegacyText(status.nickname.empty() ? status.name : status.nickname);
            uniqueCode = status.uniqueCode;
            enabled = view_.player.received && view_.player.battleReceived &&
                PetStateIndex(view_.player, slot) == static_cast<int>(Game::PetState::Standby);
        }
        else
        {
            if (!view_.hasItems || slot < 0 || slot >= static_cast<int>(view_.items.size()) ||
                !view_.items[slot].present)
                return;
            name = LegacyText(view_.items[slot].name);
            food = view_.items[slot].type == 20 && slot >= 5;
            itemRevision = view_.items[slot].revision;
        }
        list_.SetItemState(click->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        RefreshDetail();
        list_.SetFocus();
        CMenu menu;
        if (!menu.CreatePopupMenu())
            return;
        UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED);
        bool idle = !view_.petStoragePending && !view_.itemPending && !view_.player.inBattle &&
            !view_.encounterEnabled && !view_.encounterPending;
        bool canStore = enabled && idle && (pet ? view_.hasPetStorage : view_.hasItemStorage && slot >= 5);
        menu.AppendMenu(MF_STRING | (canStore ? MF_ENABLED : MF_GRAYED), 1, L"存入");
        menu.AppendMenu(flags, 2, L"丢弃");
        CMenu useMenu;
        if (food)
        {
            const auto& item = view_.items[slot];
            bool canUse = !view_.itemPending && !view_.petStoragePending && !view_.player.inBattle &&
                !view_.encounterEnabled && !view_.encounterPending && item.useField != 1 && item.target <= 2;
            if (!useMenu.CreatePopupMenu())
                return;
            useMenu.AppendMenu(MF_STRING | (canUse ? MF_ENABLED : MF_GRAYED), 4, L"角色");
            for (int petSlot = 0; petSlot < static_cast<int>(view_.pets.size()); ++petSlot)
            {
                const auto& status = view_.pets[petSlot];
                if (!status.received || !status.present)
                    continue;
                CString label = LegacyText(status.nickname.empty() ? status.name : status.nickname);
                useMenu.AppendMenu(MF_STRING | (canUse ? MF_ENABLED : MF_GRAYED), 5 + petSlot, label);
            }
            menu.AppendMenu(MF_POPUP | (canUse ? MF_ENABLED : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(useMenu.Detach()), L"使用");
        }
        menu.AppendMenu(MF_STRING, 3, L"属性");
        CPoint point = click->ptAction;
        list_.ClientToScreen(&point);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, this);
        if (command == 3)
        {
            if (pet) ShowPetProperties(list_, click->iItem);
            else ShowItemProperties(list_, click->iItem);
            return;
        }
        if (food && command >= 4 && command < 5 + view_.pets.size())
        {
            int petSlot = command == 4 ? -1 : static_cast<int>(command) - 5;
            if (session_.RequestFood(slot, itemRevision, petSlot))
                footer_.SetWindowText(petSlot < 0 ? L"正在对当前角色使用食物…" : L"正在对随身宠物使用食物…");
            return;
        }
        if (!enabled || (command != 1 && command != 2))
            return;
        if (!pet && command == 1)
        {
            if (canStore)
                session_.RequestItemStorage(Game::ItemStorageAction::Put, slot, itemRevision);
            return;
        }
        if (pet && command == 1)
        {
            if (!canStore) return;
            if (uniqueCode.empty())
            {
                MessageBox(L"缺少宠物唯一编码，请更新服务器并重新登录。", L"无法存入", MB_OK | MB_ICONWARNING);
                return;
            }
            if (session_.RequestPetStorage(Game::PetStorageAction::Put, slot, uniqueCode))
                footer_.SetWindowText(L"正在存入宠物…");
            return;
        }
        if (pet && command == 2)
        {
            CString prompt;
            prompt.Format(L"确定要丢弃宠物“%s”吗？", name.GetString());
            if (MessageBox(prompt, L"确认丢弃", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
        }
        CString message;
        message.Format(L"%s%s功能尚未接入，当前仅提供 UI", pet ? L"宠物" : L"道具",
            command == 1 ? L"存入" : L"丢弃");
        footer_.SetWindowText(message);
    }

    void RolePage::OnListDoubleClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (tabs_.GetCurSel() == 1)
        {
            OnPetStateClose();
            ShowPetProperties(header->idFrom == PetStorageList ? petStorage_ : list_, click->iItem);
        }
        else
            ShowItemProperties(header->idFrom == ItemStorageList ? itemStorage_ : list_, click->iItem);
    }
}
