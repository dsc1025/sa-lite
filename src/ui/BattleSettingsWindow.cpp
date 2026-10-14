#include "BattleSettingsWindow.h"
#include "UiText.h"
#include "../game/BattleSettings.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include <algorithm>

namespace Client
{
    BEGIN_MESSAGE_MAP(BattleSettingsWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_CLOSE()
        ON_BN_CLICKED(2, &BattleSettingsWindow::OnSave)
        ON_BN_CLICKED(3, &BattleSettingsWindow::OnSave)
        ON_CBN_SELCHANGE(4, &BattleSettingsWindow::OnSave)
        ON_EN_CHANGE(5, &BattleSettingsWindow::OnSave)
        ON_EN_CHANGE(6, &BattleSettingsWindow::OnSave)
        ON_BN_CLICKED(7, &BattleSettingsWindow::OnSave)
    END_MESSAGE_MAP()

    void BattleSettingsWindow::LoadSettings(const CString& path, GameSession& session)
    {
        configPath_ = path;
        session_ = &session;
        quickBattleEnabled_ = GetPrivateProfileIntW(L"Battle", L"QuickBattle", 1, configPath_) != 0;
        useItems_ = GetPrivateProfileIntW(L"Battle", L"UseItems", 0, configPath_) != 0;
        useMagic_ = GetPrivateProfileIntW(L"Battle", L"UseMagic", 0, configPath_) != 0;
        itemPercent_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"ItemPercent", 80, configPath_));
        magicPercent_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"MagicPercent", 60, configPath_));
        if (itemPercent_ < 0 || itemPercent_ > 100)
            itemPercent_ = 80;
        if (magicPercent_ < 0 || magicPercent_ > 100)
            magicPercent_ = 60;
        magicSlot_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"MagicSlot", -1, configPath_));
        wchar_t name[256];
        GetPrivateProfileStringW(L"Battle", L"MagicName", L"", name, 256, configPath_);
        int length = WideCharToMultiByte(936, 0, name, -1, nullptr, 0, nullptr, nullptr);
        magicName_.resize(length > 0 ? length : 0);
        if (length > 0)
        {
            WideCharToMultiByte(936, 0, name, -1, magicName_.data(), length, nullptr, nullptr);
            magicName_.resize(length - 1);
        }
        ApplySettings();
    }

    void BattleSettingsWindow::Show(CWnd* owner, const std::vector<Game::MagicStatus>& magic, GameSession& session)
    {
        updating_ = true;
        if (!GetSafeHwnd())
        {
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)),
                L"练级设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                CRect(0, 0, 484, 172), owner, 0))
            {
                updating_ = false;
                return;
            }
            CenterWindow(owner);
        }
        session_ = &session;
        currentMagic_ = magic;
        quickBattle_.SetCheck(quickBattleEnabled_ ? BST_CHECKED : BST_UNCHECKED);
        items_.SetCheck(useItems_ ? BST_CHECKED : BST_UNCHECKED);
        magic_.SetCheck(useMagic_ ? BST_CHECKED : BST_UNCHECKED);
        itemHp_.SetWindowText(Number(itemPercent_));
        magicHp_.SetWindowText(Number(magicPercent_));
        skills_.ResetContent();
        skills_.AddString(L"请选择技能");
        skills_.SetItemData(0, static_cast<DWORD_PTR>(-1));
        int selected = 0;
        for (std::size_t slot = 0; slot < magic.size(); ++slot)
        {
            const auto& skill = magic[slot];
            if (!skill.received || !skill.present || skill.name.empty())
                continue;
            int row = skills_.AddString(LegacyText(skill.name));
            skills_.SetItemData(row, slot);
            if (skill.name == magicName_ && (selected == 0 || static_cast<int>(slot) == magicSlot_))
                selected = row;
        }
        skills_.SetCurSel(selected);
        skills_.EnableWindow(skills_.GetCount() > 1);
        updating_ = false;
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
    }

    int BattleSettingsWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1)
            return -1;
        font_.CreatePointFont(90, L"Microsoft YaHei UI");

        DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        quickBattle_.Create(L"快速战斗", buttonStyle, CRect(10, 10, 164, 32), this, 7);
        items_.Create(L"食物补血", buttonStyle, CRect(10, 10, 164, 32), this, 2);
        magic_.Create(L"补血", buttonStyle, CRect(10, 36, 102, 58), this, 3);
        skills_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            CRect(108, 36, 280, 200), this, 4);
        DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_NUMBER;
        itemHp_.Create(editStyle, CRect(202, 10, 244, 32), this, 5);
        magicHp_.Create(editStyle, CRect(322, 36, 364, 58), this, 6);
        itemHp_.SetLimitText(3);
        magicHp_.SetLimitText(3);
        labels_[0].Create(L"HP<", WS_CHILD | WS_VISIBLE, CRect(170, 13, 202, 32), this);
        labels_[1].Create(L"%", WS_CHILD | WS_VISIBLE, CRect(250, 13, 266, 32), this);
        labels_[2].Create(L"HP<", WS_CHILD | WS_VISIBLE, CRect(286, 39, 318, 58), this);
        labels_[3].Create(L"%", WS_CHILD | WS_VISIBLE, CRect(370, 39, 382, 58), this);
        CWnd* controls[] = { &items_, &magic_, &quickBattle_, &skills_, &itemHp_, &magicHp_ };
        for (auto control : controls)
            control->SetFont(&font_);
        for (auto& label : labels_)
            label.SetFont(&font_);
        CClientDC dc(this);
        CFont* previousFont = dc.SelectObject(&font_);
        const int margin = 10, gap = 4, height = 22;
        auto captionWidth = [&](CWnd& control)
        {
            CString caption;
            control.GetWindowText(caption);
            return dc.GetTextExtent(caption).cx;
        };
        int contentRight = 0;
        for (int row = 0; row < 2; ++row)
        {
            int x = margin, y = margin + (row + 1) * (height + gap);
            CButton& option = row == 0 ? items_ : magic_;
            int width = captionWidth(option) + GetSystemMetrics(SM_CXMENUCHECK) + gap;
            option.MoveWindow(x, y, width, height);
            x += width + gap;
            if (row == 1)
            {
                skills_.MoveWindow(x, y, 138, 164);
                x += 138 + gap;
            }
            CStatic& hp = labels_[row * 2];
            width = captionWidth(hp);
            hp.MoveWindow(x, y + 3, width, height - 3);
            x += width + gap;
            CEdit& input = row == 0 ? itemHp_ : magicHp_;
            input.MoveWindow(x, y, 42, height);
            x += 42 + gap;
            CStatic& percent = labels_[row * 2 + 1];
            width = captionWidth(percent);
            percent.MoveWindow(x, y + 3, width, height - 3);
            contentRight = std::max(contentRight, x + width);
        }
        quickBattle_.MoveWindow(margin, margin,
            captionWidth(quickBattle_) + GetSystemMetrics(SM_CXMENUCHECK) + gap, height);
        dc.SelectObject(previousFont);
        CRect area, window;
        GetClientRect(&area);
        GetWindowRect(&window);
        SetWindowPos(nullptr, 0, 0, window.Width() + contentRight + margin - area.Width(),
            window.Height() + margin * 2 + 3 * height + 2 * gap - area.Height(), SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }

    void BattleSettingsWindow::OnSave()
    {
        if (updating_ || configPath_.IsEmpty())
            return;
        if (session_)
        {
            bool done, stopped;
            auto view = session_->Snapshot(done, stopped);
            if (done || stopped || view.screen != Screen::Game || view.encounterEnabled ||
                view.encounterPending || view.player.inBattle)
            {
                ShowWindow(SW_HIDE);
                return;
            }
        }
        auto readPercent = [](CEdit& edit, int& value)
        {
            CString text;
            edit.GetWindowText(text);
            if (text.IsEmpty() || text.SpanIncluding(L"0123456789") != text)
                return;
            int percent = _wtoi(text);
            if (percent >= 0 && percent <= 100)
                value = percent;
        };
        readPercent(itemHp_, itemPercent_);
        readPercent(magicHp_, magicPercent_);
        int row = skills_.GetCurSel();
        int slot = row > 0 ? static_cast<int>(skills_.GetItemData(row)) : -1;
        quickBattleEnabled_ = quickBattle_.GetCheck() == BST_CHECKED;
        useItems_ = items_.GetCheck() == BST_CHECKED;
        useMagic_ = magic_.GetCheck() == BST_CHECKED;
        if (slot >= 0 && slot < static_cast<int>(currentMagic_.size()))
        {
            magicSlot_ = slot;
            magicName_ = currentMagic_[slot].name;
        }
        else if (row == 0 && skills_.GetCount() > 1)
        {
            magicSlot_ = -1;
            magicName_.clear();
        }
        ApplySettings();
        const wchar_t* keys[] = { L"QuickBattle", L"UseItems", L"UseMagic",
            L"ItemPercent", L"MagicPercent", L"MagicSlot", L"MagicName" };
        CString values[] = { Number(quickBattleEnabled_ ? 1 : 0), Number(useItems_ ? 1 : 0),
            Number(useMagic_ ? 1 : 0), Number(itemPercent_), Number(magicPercent_),
            Number(magicSlot_), LegacyText(magicName_) };
        for (int i = 0; i < 7; ++i)
        {
            if (!WritePrivateProfileStringW(L"Battle", keys[i], values[i], configPath_))
            {
                MessageBox(L"无法保存 exe 同目录的 config.ini，请检查目录写入权限。",
                    L"练级设置", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    void BattleSettingsWindow::ApplySettings()
    {
        Game::HealingSettings settings;
        settings.quickBattle = quickBattleEnabled_;
        settings.useItems = useItems_;
        settings.useMagic = useMagic_ && !magicName_.empty();
        settings.itemPercent = itemPercent_;
        settings.magicPercent = magicPercent_;
        settings.magicSlot = magicSlot_;
        settings.magicName = magicName_;
        if (session_)
            session_->SetHealingSettings(settings);
    }

    void BattleSettingsWindow::OnClose()
    {
        ShowWindow(SW_HIDE);
    }
}
