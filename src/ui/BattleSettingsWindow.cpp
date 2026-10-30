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
    END_MESSAGE_MAP()

    void BattleSettingsWindow::LoadSettings(const CString& path, GameSession& session)
    {
        configPath_ = path;
        session_ = &session;
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
                L"战斗设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                CRect(0, 0, 484, 172), owner, 0))
            {
                updating_ = false;
                return;
            }
            CenterWindow(owner);
        }
        session_ = &session;
        currentMagic_ = magic;
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
        CWnd* controls[] = { &items_, &magic_, &skills_, &itemHp_, &magicHp_ };
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
            int x = margin, y = margin + row * (height + gap);
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
        dc.SelectObject(previousFont);
        CRect area, window;
        GetClientRect(&area);
        GetWindowRect(&window);
        SetWindowPos(nullptr, 0, 0, window.Width() + contentRight + margin - area.Width(),
            window.Height() + margin * 2 + 2 * height + gap - area.Height(), SWP_NOMOVE | SWP_NOZORDER);
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
        const wchar_t* keys[] = { L"UseItems", L"UseMagic",
            L"ItemPercent", L"MagicPercent", L"MagicSlot", L"MagicName" };
        CString values[] = { Number(useItems_ ? 1 : 0),
            Number(useMagic_ ? 1 : 0), Number(itemPercent_), Number(magicPercent_),
            Number(magicSlot_), LegacyText(magicName_) };
        for (int i = 0; i < 6; ++i)
        {
            if (!WritePrivateProfileStringW(L"Battle", keys[i], values[i], configPath_))
            {
                MessageBox(L"无法保存 exe 同目录的 config.ini，请检查目录写入权限。",
                    L"战斗设置", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    void BattleSettingsWindow::ApplySettings()
    {
        Game::HealingSettings settings;
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

    BEGIN_MESSAGE_MAP(CaptureSettingsWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_CLOSE()
        ON_BN_CLICKED(2, &CaptureSettingsWindow::OnSave)
        ON_EN_CHANGE(3, &CaptureSettingsWindow::OnSave)
        ON_EN_CHANGE(4, &CaptureSettingsWindow::OnSave)
        ON_CBN_SELCHANGE(5, &CaptureSettingsWindow::OnSave)
    END_MESSAGE_MAP()

    void CaptureSettingsWindow::LoadSettings(const CString& path, GameSession& session)
    {
        configPath_ = path;
        flee_ = GetPrivateProfileIntW(L"Capture", L"Flee", 1, configPath_) != 0;
        capturePercent_ = static_cast<int>(GetPrivateProfileIntW(L"Capture", L"CapturePercent", 40, configPath_));
        magicPercent_ = static_cast<int>(GetPrivateProfileIntW(L"Capture", L"MagicPercent", 60, configPath_));
        if (capturePercent_ < 0 || capturePercent_ > 100) capturePercent_ = 40;
        if (magicPercent_ < 0 || magicPercent_ > 100) magicPercent_ = 60;
        magicSlot_ = static_cast<int>(GetPrivateProfileIntW(L"Capture", L"MagicSlot", -1, configPath_));
        wchar_t name[256];
        GetPrivateProfileStringW(L"Capture", L"MagicName", L"", name, 256, configPath_);
        magicName_ = name;
        session_ = &session;
        ApplySettings();
    }

    void CaptureSettingsWindow::Show(CWnd* owner, const std::vector<Game::MagicStatus>& magic, GameSession& session)
    {
        updating_ = true;
        if (!GetSafeHwnd())
        {
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)),
                L"抓宠设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                CRect(0, 0, 380, 160), owner, 0))
            {
                updating_ = false;
                return;
            }
            CenterWindow(owner);
        }
        session_ = &session;
        fleeOption_.SetCheck(flee_ ? BST_CHECKED : BST_UNCHECKED);
        captureHp_.SetWindowText(Number(capturePercent_));
        magicHp_.SetWindowText(Number(magicPercent_));
        skills_.ResetContent();
        skills_.AddString(L"不使用技能");
        skills_.SetItemData(0, static_cast<DWORD_PTR>(-1));
        int selected = 0;
        for (std::size_t slot = 0; slot < magic.size(); ++slot)
        {
            const auto& skill = magic[slot];
            if (!skill.received || !skill.present || skill.name.empty())
                continue;
            CString name = LegacyText(skill.name);
            int row = skills_.AddString(name);
            skills_.SetItemData(row, slot);
            if (name == magicName_ && (selected == 0 || static_cast<int>(slot) == magicSlot_))
                selected = row;
        }
        skills_.SetCurSel(selected);
        skills_.EnableWindow(skills_.GetCount() > 1);
        magicHp_.EnableWindow(selected > 0);
        if (selected == 0)
        {
            magicSlot_ = -1;
            magicName_.Empty();
            ApplySettings();
        }
        updating_ = false;
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
    }

    int CaptureSettingsWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1)
            return -1;
        font_.CreatePointFont(90, L"Microsoft YaHei UI");
        DWORD labelStyle = WS_CHILD | WS_VISIBLE;
        DWORD editStyle = labelStyle | WS_TABSTOP | WS_BORDER | ES_NUMBER;
        fleeOption_.Create(L"遇敌逃跑", labelStyle | WS_TABSTOP | BS_AUTOCHECKBOX,
            CRect(10, 10, 150, 32), this, 2);
        labels_[0].Create(L"抓取", labelStyle, CRect(10, 43, 46, 62), this);
        labels_[1].Create(L"敌方HP<=", labelStyle, CRect(50, 43, 112, 62), this);
        captureHp_.Create(editStyle, CRect(116, 40, 158, 62), this, 3);
        labels_[2].Create(L"%", labelStyle, CRect(162, 43, 178, 62), this);
        labels_[3].Create(L"使用", labelStyle, CRect(10, 73, 46, 92), this);
        skills_.Create(labelStyle | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            CRect(50, 70, 204, 234), this, 5);
        labels_[4].Create(L"敌方HP>", labelStyle, CRect(212, 73, 274, 92), this);
        magicHp_.Create(editStyle, CRect(278, 70, 320, 92), this, 4);
        labels_[5].Create(L"%", labelStyle, CRect(324, 73, 340, 92), this);
        captureHp_.SetLimitText(3);
        magicHp_.SetLimitText(3);
        CWnd* controls[] = { &fleeOption_, &captureHp_, &magicHp_, &skills_ };
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
        fleeOption_.MoveWindow(margin, margin,
            captionWidth(fleeOption_) + GetSystemMetrics(SM_CXMENUCHECK) + gap, height);
        int contentRight = 0;
        for (int row = 0; row < 2; ++row)
        {
            int x = margin, y = margin + (row + 1) * (height + gap);
            CStatic& action = labels_[row * 3];
            int width = captionWidth(action);
            action.MoveWindow(x, y + 3, width, height - 3);
            x += width + gap;
            if (row == 1)
            {
                skills_.MoveWindow(x, y, 138, 164);
                x += 138 + gap;
            }
            CStatic& hp = labels_[row * 3 + 1];
            width = captionWidth(hp);
            hp.MoveWindow(x, y + 3, width, height - 3);
            x += width + gap;
            CEdit& input = row == 0 ? captureHp_ : magicHp_;
            input.MoveWindow(x, y, 42, height);
            x += 42 + gap;
            CStatic& percent = labels_[row * 3 + 2];
            width = captionWidth(percent);
            percent.MoveWindow(x, y + 3, width, height - 3);
            contentRight = std::max(contentRight, x + width);
        }
        dc.SelectObject(previousFont);
        CRect area, window;
        GetClientRect(&area);
        GetWindowRect(&window);
        SetWindowPos(nullptr, 0, 0, window.Width() + contentRight + margin - area.Width(),
            window.Height() + margin * 2 + 3 * height + 2 * gap - area.Height(), SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }

    void CaptureSettingsWindow::OnSave()
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
        readPercent(captureHp_, capturePercent_);
        readPercent(magicHp_, magicPercent_);
        flee_ = fleeOption_.GetCheck() == BST_CHECKED;
        int row = skills_.GetCurSel();
        if (row > 0)
        {
            magicSlot_ = static_cast<int>(skills_.GetItemData(row));
            skills_.GetLBText(row, magicName_);
        }
        else
        {
            magicSlot_ = -1;
            magicName_.Empty();
        }
        magicHp_.EnableWindow(row > 0);
        ApplySettings();
        const wchar_t* keys[] = { L"Flee", L"CapturePercent", L"MagicPercent", L"MagicSlot", L"MagicName" };
        CString values[] = { Number(flee_ ? 1 : 0), Number(capturePercent_), Number(magicPercent_),
            Number(magicSlot_), magicName_ };
        for (int i = 0; i < 5; ++i)
        {
            if (!WritePrivateProfileStringW(L"Capture", keys[i], values[i], configPath_))
            {
                MessageBox(L"无法保存 exe 同目录的 config.ini，请检查目录写入权限。",
                    L"抓宠设置", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    void CaptureSettingsWindow::ApplySettings()
    {
        Game::CaptureSettings settings;
        settings.flee = flee_;
        settings.capturePercent = capturePercent_;
        settings.magicPercent = magicPercent_;
        settings.magicSlot = magicSlot_;
        int length = WideCharToMultiByte(936, 0, magicName_, -1, nullptr, 0, nullptr, nullptr);
        if (length > 0)
        {
            settings.magicName.resize(length);
            WideCharToMultiByte(936, 0, magicName_, -1, settings.magicName.data(), length, nullptr, nullptr);
            settings.magicName.resize(length - 1);
        }
        if (session_)
            session_->SetCaptureSettings(settings);
    }

    void CaptureSettingsWindow::OnClose()
    {
        updating_ = true;
        captureHp_.SetWindowText(Number(capturePercent_));
        magicHp_.SetWindowText(Number(magicPercent_));
        updating_ = false;
        ShowWindow(SW_HIDE);
    }
}
