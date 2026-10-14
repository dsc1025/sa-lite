#include "RolePage.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "UiText.h"
#include "PetUi.h"

namespace Client
{
    void RolePage::OnPetClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (view_.screen != Screen::Game || !IsWindowVisible() || tabs_.GetCurSel() != 1 || click->iItem < 0)
            return;
        RefreshDetail();
        if (click->iSubItem != 2 || !view_.player.received || !view_.player.battleReceived || view_.petStoragePending || view_.itemPending)
            return;
        int slot = static_cast<int>(list_.GetItemData(click->iItem));
        if (slot < 0 || slot >= static_cast<int>(view_.pets.size()) ||
            !view_.pets[slot].received || !view_.pets[slot].present)
            return;
        CRect cell;
        if (!list_.GetSubItemRect(click->iItem, 2, LVIR_BOUNDS, cell))
            return;
        list_.ClientToScreen(&cell);
        ScreenToClient(&cell);
        editingPet_ = slot;
        int state = PetStateIndex(view_.player, slot);
        petState_.SetCurSel(state);
        petState_.SetWindowPos(&CWnd::wndTop, cell.left, cell.top, cell.Width(), 150, SWP_SHOWWINDOW);
        petState_.SetFocus();
        petState_.ShowDropDown(TRUE);
    }

    void RolePage::ShowPetProperties(CListCtrl& list, int row)
    {
        if (view_.screen != Screen::Game || !IsWindowVisible() || tabs_.GetCurSel() != 1 ||
            row < 0 || row >= list.GetItemCount())
            return;
        auto slot = static_cast<std::size_t>(list.GetItemData(row));
        CString text;
        if (&list == &petStorage_)
        {
            if (!view_.hasPetStorage || slot >= view_.petStorage.size() || !view_.petStorage[slot].present)
                return;
            const auto& pet = view_.petStorage[slot];
            text.Format(L"名称：%s\r\n昵称：%s\r\n等级：%d",
                LegacyText(pet.name).GetString(), LegacyText(pet.nickname).GetString(), pet.level);
        }
        else
        {
            if (slot >= view_.pets.size() || !view_.pets[slot].received || !view_.pets[slot].present)
                return;
            const auto& pet = view_.pets[slot];
            text.Format(L"名称：%s\r\n昵称：%s\r\n等级 / 转生：%d / %d\r\n"
                L"生命：%d / %d\r\n魔力：%d / %d\r\n攻击：%d\r\n防御：%d\r\n敏捷：%d\r\n"
                L"感知：%d\r\n忠诚：%d\r\n技能栏：%d\r\n元素：地 %d  水 %d  火 %d  风 %d\r\n"
                L"经验 / 下级经验：%d / %d",
                LegacyText(pet.name).GetString(), LegacyText(pet.nickname).GetString(), pet.level, pet.rebirth,
                pet.hp, pet.maxHp, pet.mp, pet.maxMp, pet.attack, pet.defence, pet.quick,
                pet.perception, pet.loyalty, pet.skillSlots, pet.earth, pet.water, pet.fire, pet.wind,
                pet.experience, pet.nextExperience);
            text += L"\r\n\r\n宠物技能：";
            if (!pet.skillsReceived)
                text += L"\r\n等待技能数据…";
            else
            {
                bool hasSkills = false;
                for (std::size_t i = 0; i < pet.skills.size(); ++i)
                {
                    const auto& skill = pet.skills[i];
                    if (!skill.present)
                        continue;
                    CString line;
                    line.Format(L"\r\n%d. %s", static_cast<int>(i + 1), LegacyText(skill.name).GetString());
                    text += line;
                    if (!skill.description.empty())
                    {
                        text += L"  ";
                        text += LegacyText(skill.description);
                    }
                    hasSkills = true;
                }
                if (!hasSkills)
                    text += L"\r\n无";
            }
        }
        petProperties_.Show(GetTopLevelParent(), text, L"宠物属性");
    }

    void RolePage::OnPetStorageRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        if (view_.screen != Screen::Game || !IsWindowVisible() || tabs_.GetCurSel() != 1)
            return;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        int slot = click->iItem < 0 ? -1 : static_cast<int>(petStorage_.GetItemData(click->iItem));
        bool available = view_.hasPetStorage && slot >= 0 && slot < static_cast<int>(view_.petStorage.size()) &&
            view_.petStorage[slot].present;
        std::string uniqueCode = available ? view_.petStorage[slot].uniqueCode : std::string();
        if (click->iItem >= 0)
        {
            petStorage_.SetItemState(click->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            RefreshDetail();
        }
        petStorage_.SetFocus();
        bool idle = !view_.petStoragePending && !view_.itemPending && !view_.player.inBattle &&
            !view_.encounterEnabled && !view_.encounterPending;
        CMenu menu;
        if (!menu.CreatePopupMenu()) return;
        if (available)
            menu.AppendMenu(MF_STRING | (idle ? MF_ENABLED : MF_GRAYED), 1, L"取出");
        menu.AppendMenu(MF_STRING | (idle ? MF_ENABLED : MF_GRAYED), 2, L"刷新");
        if (available)
            menu.AppendMenu(MF_STRING, 3, L"属性");
        CPoint point = click->ptAction;
        petStorage_.ClientToScreen(&point);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, this);
        if (command == 3 && available)
        {
            ShowPetProperties(petStorage_, click->iItem);
            return;
        }
        if (!idle) return;
        if (command == 1 && available)
        {
            if (session_.RequestPetStorage(Game::PetStorageAction::Get, slot, uniqueCode))
                footer_.SetWindowText(L"正在取出宠物…");
        }
        else if (command == 2)
            session_.RequestPetStorage(Game::PetStorageAction::List);
    }

    void RolePage::OnPetState()
    {
        int slot = editingPet_, state = petState_.GetCurSel();
        OnPetStateClose();
        if (slot < 0 || state < 0 || view_.screen != Screen::Game)
            return;
        if (state == PetStateIndex(view_.player, slot))
            return;
        if (!session_.RequestPetState(slot, state))
            return;
        footer_.SetWindowText(L"正在设置宠物状态…");
    }

    void RolePage::OnPetStateClose()
    {
        editingPet_ = -1;
        if (petState_.GetSafeHwnd())
            petState_.ShowWindow(SW_HIDE);
    }

    void RolePage::RefreshPets()
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

    void RolePage::RefreshPetStorage(bool pets)
    {
        int storageSelection = petStorage_.GetNextItem(-1, LVNI_SELECTED);
        DWORD_PTR storageSlot = storageSelection >= 0 ? petStorage_.GetItemData(storageSelection) : 0;
        petStorage_.DeleteAllItems();
        if (pets)
        {
            if (!view_.hasPetStorage)
            {
                int row = petStorage_.InsertItem(0, view_.petStoragePending ? L"正在加载…" : L"未加载，右键刷新");
                petStorage_.SetItemData(row, static_cast<DWORD_PTR>(-1));
            }
            else
            {
                for (std::size_t i = 0; i < view_.petStorage.size(); ++i)
                {
                    const auto& pet = view_.petStorage[i];
                    CString name;
                    name.Format(L"%d · ", static_cast<int>(i + 1));
                    name += pet.present ? LegacyText(pet.nickname.empty() ? pet.name : pet.nickname) : CString(L"空栏位");
                    int row = petStorage_.InsertItem(static_cast<int>(i), name);
                    petStorage_.SetItemText(row, 1, pet.present ? Number(pet.level) : CString());
                    petStorage_.SetItemData(row, i);
                    if (i == storageSlot)
                        petStorage_.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
        }
    }
}
