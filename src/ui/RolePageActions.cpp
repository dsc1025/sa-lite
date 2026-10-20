#include "RolePage.h"
#include "RoleControlIds.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "UiText.h"
#include "PetUi.h"

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
        bool targetedUse = false;
        bool directUse = false;
        bool equipment = false;
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
            targetedUse = slot >= 5 && (view_.items[slot].type == 20 ||
                (view_.items[slot].type == 16 && view_.items[slot].target >= 1 && view_.items[slot].target <= 2));
            directUse = view_.items[slot].type == 16 && view_.items[slot].target == 0 && slot >= 5;
            itemRevision = view_.items[slot].revision;
            equipment = Game::IsEquipmentType(view_.items[slot].type);
        }
        list_.SetItemState(click->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        RefreshDetail();
        list_.SetFocus();
        CMenu menu;
        if (!menu.CreatePopupMenu())
            return;
        bool idle = !view_.petStoragePending && !view_.itemPending && !view_.player.inBattle &&
            !view_.encounterEnabled && !view_.encounterPending;
        bool canStore = enabled && idle && (pet ? view_.hasPetStorage : view_.hasItemStorage && slot >= 5);
        menu.AppendMenu(MF_STRING | (canStore ? MF_ENABLED : MF_GRAYED), 1, L"存入");
        bool canDiscard = enabled && idle;
        menu.AppendMenu(MF_STRING | (canDiscard ? MF_ENABLED : MF_GRAYED), 2, L"丢弃");
        CMenu equipmentMenu;
        if (equipment)
        {
            bool canEquip = idle && !view_.shopPending;
            if (slot >= 5 && Game::EquipmentSlot(view_.items[slot].type) == 3)
            {
                if (!equipmentMenu.CreatePopupMenu()) return;
                equipmentMenu.AppendMenu(MF_STRING, 11, L"饰品1");
                equipmentMenu.AppendMenu(MF_STRING, 12, L"饰品2");
                menu.AppendMenu(MF_POPUP | (canEquip ? MF_ENABLED : MF_GRAYED),
                    reinterpret_cast<UINT_PTR>(equipmentMenu.Detach()), L"装备");
            }
            else
                menu.AppendMenu(MF_STRING | (canEquip ? MF_ENABLED : MF_GRAYED), 11, slot < 5 ? L"卸下" : L"装备");
        }
        CMenu useMenu;
        if (directUse)
        {
            bool canUse = idle && view_.items[slot].useField != 1;
            menu.AppendMenu(MF_STRING | (canUse ? MF_ENABLED : MF_GRAYED), 4, L"使用");
        }
        else if (targetedUse)
        {
            const auto& item = view_.items[slot];
            bool canUse = !view_.itemPending && !view_.petStoragePending && !view_.player.inBattle &&
                !view_.encounterEnabled && !view_.encounterPending && item.useField != 1 && item.target <= 2;
            if (!useMenu.CreatePopupMenu())
                return;
            useMenu.AppendMenu(MF_STRING | (canUse ? MF_ENABLED : MF_GRAYED), 4, view_.characterName.c_str());
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
        if (pet)
            menu.AppendMenu(MF_STRING | (idle && view_.pets[slot].canRename && !uniqueCode.empty() ? MF_ENABLED : MF_GRAYED), 10, L"重命名");
        menu.AppendMenu(MF_STRING, 3, L"属性");
        CPoint point = click->ptAction;
        list_.ClientToScreen(&point);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, this);
        if (equipment && (command == 11 || command == 12))
        {
            int equipmentSlot = slot >= 5 && Game::EquipmentSlot(view_.items[slot].type) == 3 ?
                (command == 11 ? 3 : 4) : -1;
            if (session_.RequestItemEquip(slot, itemRevision, equipmentSlot))
                footer_.SetWindowText(slot < 5 ? L"正在卸下装备…" : L"正在穿上装备…");
            else
                footer_.SetWindowText(L"当前状态或装备数据已变化，请重新选择后操作");
            return;
        }
        if (command == 10 && pet && idle && view_.pets[slot].canRename && !uniqueCode.empty())
        {
            renamingPet_ = slot;
            renamingPetCode_ = uniqueCode;
            if (!list_.EditLabel(click->iItem))
            {
                renamingPet_ = -1;
                renamingPetCode_.clear();
            }
            return;
        }
        if (command == 3)
        {
            if (pet) ShowPetProperties(list_, click->iItem);
            else ShowItemProperties(list_, click->iItem);
            return;
        }
        if ((directUse && command == 4) || (targetedUse && command >= 4 && command < 5 + view_.pets.size()))
        {
            int petSlot = command == 4 ? -1 : static_cast<int>(command) - 5;
            if (session_.RequestItemUse(slot, itemRevision, petSlot))
                footer_.SetWindowText(directUse ? L"正在使用道具…" :
                    petSlot < 0 ? L"正在对当前角色使用道具…" : L"正在对随身宠物使用道具…");
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
        if (command == 2 && canDiscard)
        {
            CString prompt;
            prompt.Format(L"确定要丢弃%s“%s”吗？", pet ? L"宠物" : L"道具", name.GetString());
            if (MessageBox(prompt, L"确认丢弃", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
            if (session_.RequestDiscard(pet, slot, itemRevision, uniqueCode))
                footer_.SetWindowText(pet ? L"正在丢弃宠物…" : L"正在丢弃道具…");
            else
                footer_.SetWindowText(L"当前状态或数据已变化，请重新选择后丢弃");
        }
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
