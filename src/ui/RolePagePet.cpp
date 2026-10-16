#include "RolePage.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "UiText.h"
#include "PetUi.h"
#include <charconv>
#include <string_view>

namespace Client
{
    namespace
    {
        bool ParsePetInitialAttributes(const std::string& nickname, int (&attributes)[5])
        {
            std::string_view values(nickname);
            if (values.size() >= 2 && values.front() == '[' && values.back() == ']')
                values = values.substr(1, values.size() - 2);
            for (int i = 0; i < 5; ++i)
            {
                auto end = values.find('/');
                if ((i < 4 && end == std::string_view::npos) ||
                    (i == 4 && end != std::string_view::npos))
                    return false;
                auto value = values.substr(0, end);
                if (value.empty() || value.front() < '0' || value.front() > '9')
                    return false;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), attributes[i]);
                if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
                    return false;
                if (i < 4)
                    values.remove_prefix(end + 1);
            }
            return true;
        }

        CString PetPropertiesText(const Game::PetStatus& pet)
        {
            CString text;
            text.Format(L"名称：%s\r\n昵称：%s\r\n转生：%d\r\n等级：%d\r\n"
                L"生命：%d / %d\r\n"
                // L"魔力：%d / %d\r\n"
                L"攻击：%d\r\n防御：%d\r\n敏捷：%d\r\n"
                // L"感知：%d\r\n"
                L"忠诚：%d\r\n技能栏：%d\r\n元素：地 %d  水 %d  火 %d  风 %d\r\n"
                L"经验 / 下级经验：%d / %d",
                LegacyText(pet.name).GetString(), LegacyText(pet.nickname).GetString(), pet.rebirth, pet.level,
                pet.hp, pet.maxHp, /* pet.mp, pet.maxMp, */ pet.attack, pet.defence, pet.quick,
                /* pet.perception, */ pet.loyalty, pet.skillSlots, pet.earth, pet.water, pet.fire, pet.wind,
                pet.experience, pet.nextExperience);
            int initialAttributes[5];
            if (pet.level >= 1 && ParsePetInitialAttributes(pet.nickname, initialAttributes))
            {
                const int attributes[] = { pet.maxHp, pet.attack, pet.defence, pet.quick, pet.perception };
                double growth[5] = {};
                if (pet.level > 1)
                {
                    for (int i = 0; i < 5; ++i)
                        growth[i] = (static_cast<double>(attributes[i]) - initialAttributes[i]) / (pet.level - 1);
                }
                CString line;
                line.Format(L"\r\n\r\n成长计算:\r\n生命:%.3f 攻击:%.3f 防御:%.3f 敏捷:%.3f" /* L" 感知:%.3f" */ L" 合计:%.3f",
                    growth[0], growth[1], growth[2], growth[3] /* , growth[4] */, growth[1] + growth[2] + growth[3]);
                text += line;
            }
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
            return text;
        }
    }
    void RolePage::OnPetRenameBegin(NMHDR* header, LRESULT* result)
    {
        auto edit = reinterpret_cast<NMLVDISPINFO*>(header);
        *result = TRUE;
        if (view_.screen != Screen::Game || tabs_.GetCurSel() != 1 || renamingPet_ < 0 ||
            edit->item.iItem < 0 || static_cast<int>(list_.GetItemData(edit->item.iItem)) != renamingPet_)
            return;
        *result = FALSE;
        if (auto control = list_.GetEditControl())
            control->SetLimitText(16);
    }

    void RolePage::OnPetRenameEnd(NMHDR* header, LRESULT* result)
    {
        *result = FALSE;
        auto edit = reinterpret_cast<NMLVDISPINFO*>(header);
        int slot = renamingPet_;
        std::string code = renamingPetCode_;
        renamingPet_ = -1;
        renamingPetCode_.clear();
        if (!edit->item.pszText || slot < 0 || view_.screen != Screen::Game || tabs_.GetCurSel() != 1)
            return;
        CString text(edit->item.pszText);
        BOOL substituted = FALSE;
        int length = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, text.GetString(), text.GetLength(),
            nullptr, 0, nullptr, &substituted);
        if (length <= 0 || length > 16 || substituted)
        {
            MessageBox(L"昵称须为 1 至 16 字节（通常最多 8 个汉字），且字符必须受服务端编码支持。",
                L"无法重命名", MB_OK | MB_ICONWARNING);
            return;
        }
        std::string nickname(length, '\0');
        if (!WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, text.GetString(), text.GetLength(),
            nickname.data(), length, nullptr, &substituted) || substituted)
            return;
        if (nickname.find_first_of("\r\n\t|") != std::string::npos)
        {
            MessageBox(L"昵称不能包含控制字符或竖线。", L"无法重命名", MB_OK | MB_ICONWARNING);
            return;
        }
        footer_.SetWindowText(session_.RequestPetRename(slot, code, nickname) ?
            L"正在重命名宠物…" : L"当前状态或宠物数据已变化，请重新选择后重命名");
    }

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
        int image = 0;
        if (&list == &petStorage_)
        {
            if (!view_.hasPetStorage || slot >= view_.petStorage.size() || !view_.petStorage[slot].present)
                return;
            propertiesPetSlot_ = -1;
            const auto& pet = view_.petStorage[slot];
            text.Format(L"名称：%s\r\n昵称：%s\r\n等级：%d",
                LegacyText(pet.name).GetString(), LegacyText(pet.nickname).GetString(), pet.level);
        }
        else
        {
            if (slot >= view_.pets.size() || !view_.pets[slot].received || !view_.pets[slot].present)
                return;
            const auto& pet = view_.pets[slot];
            image = pet.image;
            text = PetPropertiesText(pet);
            propertiesPetSlot_ = static_cast<int>(slot);
            propertiesPetLevel_ = pet.level;
            propertiesPetCode_ = pet.uniqueCode;
        }
        petProperties_.Show(GetTopLevelParent(), text, L"宠物属性", image);
    }

    void RolePage::RefreshPetProperties()
    {
        if (propertiesPetSlot_ < 0)
            return;
        if (view_.screen != Screen::Game || !petProperties_.GetSafeHwnd() ||
            !petProperties_.IsWindowVisible() || propertiesPetSlot_ >= static_cast<int>(view_.pets.size()))
        {
            propertiesPetSlot_ = -1;
            return;
        }
        const auto& pet = view_.pets[propertiesPetSlot_];
        if (!pet.received || !pet.present || pet.uniqueCode != propertiesPetCode_)
        {
            propertiesPetSlot_ = -1;
            return;
        }
        if (pet.level > propertiesPetLevel_)
        {
            petProperties_.UpdateText(PetPropertiesText(pet));
            propertiesPetLevel_ = pet.level;
        }
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
        if (list_.GetEditControl()) return;
        ListUpdate rows(list_);
        for (std::size_t i = 0; i < view_.pets.size(); ++i)
        {
            const auto& pet = view_.pets[i];
            CString name = pet.present ? LegacyText(pet.nickname.empty() ? pet.name : pet.nickname) : CString(L"空栏位");
            if (!pet.received) name = L"等待数据…";
            int row = rows.Add(i, name);
            SetCellIfChanged(list_, row, 1, pet.present ? Number(pet.level) : CString());
            CString state;
            if (pet.received && pet.present)
            {
                int index = PetStateIndex(view_.player, static_cast<int>(i));
                state = PetStateNames[index];
            }
            SetCellIfChanged(list_, row, 2, state);
        }
    }

    void RolePage::RefreshPetStorage(bool pets)
    {
        if (!pets) return;
        ListUpdate rows(petStorage_);
        if (pets)
        {
            if (!view_.hasPetStorage)
            {
                int row = rows.Add(static_cast<DWORD_PTR>(-1), view_.petStoragePending ? L"正在加载…" : L"未加载，右键刷新");
                SetCellIfChanged(petStorage_, row, 1, CString());
            }
            else
            {
                for (std::size_t i = 0; i < view_.petStorage.size(); ++i)
                {
                    const auto& pet = view_.petStorage[i];
                    CString name;
                    name.Format(L"%d · ", static_cast<int>(i + 1));
                    name += pet.present ? LegacyText(pet.nickname.empty() ? pet.name : pet.nickname) : CString(L"空栏位");
                    int row = rows.Add(i, name);
                    SetCellIfChanged(petStorage_, row, 1, pet.present ? Number(pet.level) : CString());
                }
            }
        }
    }
}
