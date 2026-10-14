#include "MainWindow.h"
#include "BattleUi.h"
#include "UiText.h"
#include <algorithm>
#include <cstring>

namespace Client
{
    BEGIN_MESSAGE_MAP(BattleSceneWindow, CWnd)
        ON_WM_PAINT()
        ON_WM_CLOSE()
    END_MESSAGE_MAP()

    void BattleSceneWindow::LoadResources(const CString& configPath)
    {
        wchar_t path[32768];
        GetPrivateProfileStringW(L"Battle", L"ResourceDirectory",
            L"C:\\Work\\SA\\SA2.5\\SA2.5\\data", path, 32768, configPath);
        resourceDirectory_ = path;
        resourceDebug_.clear();
        animations_.clear();
        pictures_.clear();
        inspected_.clear();
        sprites_.close();
        images_.close();
        sprites_.clear();
        images_.clear();
        resourceDebug_.push_back(L"资源目录：" + resourceDirectory_.wstring());
        auto loadIndex = [&](const wchar_t* name, std::size_t recordSize, auto readRecord)
        {
            std::ifstream file(resourceDirectory_ / name, std::ios::binary | std::ios::ate);
            if (!file)
            {
                resourceDebug_.push_back(std::wstring(name) + L"：打开失败");
                return;
            }
            auto size = static_cast<std::streamoff>(file.tellg());
            if (size <= 0 || size % static_cast<std::streamoff>(recordSize) != 0 || size > 80000000)
            {
                resourceDebug_.push_back(std::wstring(name) + L"：索引长度无效");
                return;
            }
            file.seekg(0);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!file)
            {
                resourceDebug_.push_back(std::wstring(name) + L"：读取失败");
                return;
            }
            for (std::size_t offset = 0; offset < bytes.size(); offset += recordSize)
                readRecord(bytes.data() + offset);
            resourceDebug_.push_back(std::wstring(name) + L"：读取 " +
                std::to_wstring(bytes.size() / recordSize) + L" 条索引");
        };
        loadIndex(L"spradrn_5.bin", 12, [&](const unsigned char* record)
        {
            std::uint32_t id, offset;
            std::memcpy(&id, record, 4);
            std::memcpy(&offset, record + 4, 4);
            animations_[id] = offset;
        });
        loadIndex(L"adrn_15.bin", 80, [&](const unsigned char* record)
        {
            std::uint32_t id, offset, size;
            std::memcpy(&id, record, 4);
            std::memcpy(&offset, record + 4, 4);
            std::memcpy(&size, record + 8, 4);
            pictures_[id] = { offset, size };
        });
        sprites_.open(resourceDirectory_ / L"spr_4.bin", std::ios::binary | std::ios::ate);
        images_.open(resourceDirectory_ / L"real_15.bin", std::ios::binary | std::ios::ate);
        spriteSize_ = sprites_ ? static_cast<std::uint64_t>(sprites_.tellg()) : 0;
        imageSize_ = images_ ? static_cast<std::uint64_t>(images_.tellg()) : 0;
        resourceDebug_.push_back(L"spr_4.bin：" + std::to_wstring(spriteSize_) +
            L" 字节；real_15.bin：" + std::to_wstring(imageSize_) + L" 字节（按需读取）");
        resourceDebug_.push_back(L"当前阶段：单位占位显示；图片解码、动作播放尚未接入。");
    }

    void BattleSceneWindow::InspectImage(std::uint32_t image)
    {
        if (!inspected_.emplace(image, true).second)
            return;
        if (resourceDebug_.size() >= 40)
            resourceDebug_.erase(resourceDebug_.begin() + 5);
        std::wstring message = L"素材调用 ID=" + std::to_wstring(image);
        auto animation = animations_.find(image);
        if (animation == animations_.end())
        {
            resourceDebug_.push_back(message + L"：动画索引未找到");
            return;
        }
        auto offset = static_cast<std::uint64_t>(animation->second);
        if (offset + 24 > spriteSize_)
        {
            resourceDebug_.push_back(message + L"：动画文件缺失或偏移越界");
            return;
        }
        std::uint32_t header[6]{};
        sprites_.clear();
        sprites_.seekg(static_cast<std::streamoff>(offset));
        sprites_.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!sprites_ || header[2] == 0)
        {
            resourceDebug_.push_back(message + L"：首动作头读取失败或无帧");
            return;
        }
        message += L" → 首帧图片=" + std::to_wstring(header[3]);
        auto picture = pictures_.find(header[3]);
        if (picture == pictures_.end() || picture->second[1] < 16 ||
            static_cast<std::uint64_t>(picture->second[0]) + picture->second[1] > imageSize_)
        {
            resourceDebug_.push_back(message + L"：图片索引缺失或数据越界");
            return;
        }
        unsigned char imageHeader[16]{};
        images_.clear();
        images_.seekg(picture->second[0]);
        images_.read(reinterpret_cast<char*>(imageHeader), sizeof(imageHeader));
        std::uint32_t width, height;
        std::memcpy(&width, imageHeader + 4, 4);
        std::memcpy(&height, imageHeader + 8, 4);
        if (!images_ || imageHeader[0] != 'R' || imageHeader[1] != 'D')
            message += L"：图片头无效";
        else
            message += L" → RD " + std::to_wstring(width) + L"×" + std::to_wstring(height) +
                L"（仅探测首帧，格式待完整验证）";
        resourceDebug_.push_back(message);
    }

    void BattleSceneWindow::Update(CWnd* owner, const SessionView& view, GameSession& session)
    {
        session_ = &session;
        bool active = view.screen == Screen::Game && view.player.inBattle && !view.quickBattle;
        if (!active)
        {
            active_ = false;
            dismissed_ = false;
            if (GetSafeHwnd() && (view.quickBattle || view.screen != Screen::Game))
                ShowWindow(SW_HIDE);
            else if (GetSafeHwnd())
            {
                SetWindowText(L"简易战斗画面 — 战斗结束");
                CString text = debugText_;
                for (const auto& line : view.battleDebug)
                    text += line.c_str() + CString(L"\r\n");
                CString previous;
                debug_.GetWindowText(previous);
                if (previous != text)
                {
                    debug_.SetWindowText(text);
                    debug_.LineScroll(debug_.GetLineCount());
                }
            }
            return;
        }
        if (!GetSafeHwnd())
        {
            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
            CRect area(0, 0, 640, 480);
            ::AdjustWindowRectEx(&area, style, FALSE, WS_EX_DLGMODALFRAME);
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)),
                L"简易战斗画面", style, area, owner, 0))
                return;
            font_.CreatePointFont(85, L"Microsoft YaHei UI");
            debug_.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_BORDER |
                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                CRect(8, 8, 632, 166), this, 1);
            debug_.SetFont(&font_);
            debug_.SetLimitText(256 * 1024);
            debug_.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            CenterWindow(owner);
        }
        if (!active_)
        {
            inspected_.clear();
            if (!dismissed_)
                ShowWindow(SW_SHOWNOACTIVATE);
        }
        active_ = true;
        battle_ = view.battle;
        for (const auto& member : battle_.members)
            if (member.present)
                InspectImage(member.image);
        CString text;
        text.Format(L"DEBUG 回合=%d  场地属性=%X  我方位置=%d\r\n服务端：%s\r\n",
            battle_.round, battle_.fieldAttribute, battle_.playerSlot, view.battleResult.message.c_str());
        for (const auto& line : resourceDebug_)
            text += line.c_str() + CString(L"\r\n");
        debugText_ = text;
        for (const auto& line : view.battleDebug)
            text += line.c_str() + CString(L"\r\n");
        CString previous;
        debug_.GetWindowText(previous);
        if (previous != text)
        {
            debug_.SetWindowText(text);
            debug_.LineScroll(debug_.GetLineCount());
        }
        SetWindowText(L"简易战斗画面 — 自动战斗 / Debug");
        Invalidate(FALSE);
    }

    void BattleSceneWindow::OnPaint()
    {
        CPaintDC dc(this);
        CRect area;
        GetClientRect(&area);
        dc.FillSolidRect(area, RGB(28, 40, 48));
        int saved = dc.SaveDC();
        dc.SelectObject(&font_);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(235, 235, 235));
        const int playerSide = battle_.playerSlot >= 10 ? 1 : 0;
        const int columns[] = { 0, 1, -1, 2, -2 };
        for (int slot = 0; slot < 20; ++slot)
        {
            const auto& member = battle_.members[slot];
            if (!member.present)
                continue;
            bool ally = slot / 10 == playerSide;
            int x = (area.Width() - 112) / 2 + columns[slot % 5] * 124;
            int y = (ally ? 338 : 180) + (slot % 10 < 5 ? 64 : 0);
            CRect box(x, y, x + 112, y + 58);
            dc.FillSolidRect(box, member.hp > 0 ? (ally ? RGB(36, 78, 74) : RGB(88, 51, 51)) : RGB(55, 55, 55));
            CString label;
            label.Format(L"[%d] %s\n素材 %u  HP %d/%d", slot,
                LegacyText(member.name).GetString(), member.image, member.hp, member.maxHp);
            CRect caption = box;
            caption.DeflateRect(3, 2);
            dc.DrawText(label, caption, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            dc.FillSolidRect(x + 4, y + 51, 104, 4, RGB(50, 50, 50));
            int hp = member.maxHp > 0 ? static_cast<int>(std::min<std::int64_t>(104,
                static_cast<std::int64_t>(std::max(0, member.hp)) * 104 / member.maxHp)) : 0;
            if (hp > 0)
                dc.FillSolidRect(x + 4, y + 51, hp, 4, RGB(80, 200, 100));
        }
        dc.RestoreDC(saved);
    }

    void BattleSceneWindow::OnClose()
    {
        if (session_)
            session_->SetEncounterEnabled(false);
        dismissed_ = active_;
        ShowWindow(SW_HIDE);
    }

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

    void BattleGroup::Update(const Game::BattleState& battle, int offset, bool received)
    {
        for (int i = 0; i < 10; ++i)
            members_[i] = battle.members[offset + i];
        offset_ = offset;
        received_ = received;
        Invalidate(FALSE);
    }

    void BattleGroup::DrawItem(LPDRAWITEMSTRUCT item)
    {
        CDC* dc = CDC::FromHandle(item->hDC);
        int saved = dc->SaveDC();
        if (CFont* font = GetFont())
            dc->SelectObject(font);
        CRect area(item->rcItem);
        dc->FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
        CString caption;
        GetWindowText(caption);
        CSize size = dc->GetTextExtent(caption);
        CRect border = area;
        border.top += size.cy / 2;
        dc->DrawEdge(&border, EDGE_ETCHED, BF_RECT);
        CRect title(area.left + 8, area.top, area.left + 16 + size.cx, area.top + size.cy);
        dc->FillSolidRect(title, GetSysColor(COLOR_BTNFACE));
        dc->SetTextColor(GetSysColor(COLOR_BTNTEXT));
        dc->SetBkMode(TRANSPARENT);
        dc->DrawText(caption, title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        CRect content(area.left + 12, area.top + size.cy + 8, area.right - 10, area.bottom - 10);
        if (!received_)
            dc->DrawText(L"等待战斗队员数据", content, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        else
        {
            int lineHeight = size.cy + 4;
            int spacing = std::max(1, std::min(lineHeight, content.Height() / 10));
            int top = content.top + (content.Height() - spacing * 10) / 2;
            // Each of the five fixed positions keeps its pet above its character.
            const int order[] = { 9, 4, 7, 2, 5, 0, 6, 1, 8, 3 };
            for (int rowIndex = 0; rowIndex < 10; ++rowIndex)
            {
                int slot = order[rowIndex];
                const auto& member = members_[slot];
                if (!member.present)
                    continue;
                CString text;
                text.Format(L"* [%X] %s  Lv:%d (%d/%d)", offset_ + slot,
                    LegacyText(member.name).GetString(), member.level, member.hp, member.maxHp);
                if (!member.rideName.empty())
                {
                    CString ride;
                    ride.Format(L" [骑] %s  Lv:%d (%d/%d)", LegacyText(member.rideName).GetString(),
                        member.rideLevel, member.rideHp, member.rideMaxHp);
                    text += ride;
                }
                int rowTop = top + rowIndex * spacing;
                CRect row(content.left, rowTop, content.right,
                    std::min<LONG>(rowTop + spacing, content.bottom));
                if (row.bottom > row.top)
                    dc->DrawText(text, row, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        dc->RestoreDC(saved);
    }

    void MainWindow::CreateBattleControls()
    {
        CRect empty(0, 0, 0, 0);
        battleTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, IDC_BATTLE_TABS);
        battleTabs_.InsertItem(0, L"练级");
        battleTabs_.InsertItem(1, L"抓宠");
        battleStart_.Create(L"开始", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_START);
        battleStop_.Create(L"停止", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_STOP);
        battleSettings_.Create(L"设置", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_SETTINGS);
        battleRound_.Create(L"回合：—", WS_CHILD | SS_RIGHT, empty, this);
        const wchar_t* sides[] = { L"上方队员", L"下方队员" };
        for (int i = 0; i < 2; ++i)
            battleSides_[i].Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                empty, this, IDC_BATTLE_UPPER + i);
        battleInfo_.Create(L"战斗提示：等待战斗", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            empty, this, IDC_BATTLE_INFO);
        battleExperience_.Create(L"经验获得：本场 —    累计 0", WS_CHILD, empty, this);
    }

    void MainWindow::LayoutBattle(const CRect& mainPage)
    {
        battleTabs_.MoveWindow(mainPage);
        CRect battlePage = mainPage;
        battleTabs_.AdjustRect(FALSE, &battlePage);
        battlePage.DeflateRect(6, 6);
        if (mainTabs_.GetCurSel() == 2)
            developing_.MoveWindow(battlePage);
        int battleWidth = std::max(0, battlePage.Width());
        battleStart_.MoveWindow(battlePage.left, battlePage.top, 70, 26);
        battleStop_.MoveWindow(battlePage.left + 78, battlePage.top, 70, 26);
        battleSettings_.MoveWindow(battlePage.left + 156, battlePage.top, 70, 26);
        int sideTop = battlePage.top + 34;
        int sideBottom = std::max<LONG>(sideTop + 24, battlePage.bottom - 30);
        int sideWidth = std::max(0, (battleWidth - 12) / 2);
        int rightLeft = battlePage.left + sideWidth + 12;
        battleSides_[0].MoveWindow(battlePage.left, sideTop, sideWidth,
            std::max(0, sideBottom - sideTop - 24));
        battleSides_[1].MoveWindow(rightLeft, sideTop + 24,
            std::max<LONG>(0, battlePage.right - rightLeft), std::max(0, sideBottom - sideTop - 24));
        battleRound_.MoveWindow(rightLeft, sideTop,
            std::max<LONG>(0, battlePage.right - rightLeft - 8), 22);
        battleInfo_.MoveWindow(battlePage.left + 8, sideBottom - 22,
            std::max(0, sideWidth - 16), 22);
        battleExperience_.MoveWindow(battlePage.left, sideBottom + 8, battleWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            developing_.ShowWindow(leveling ? SW_HIDE : SW_SHOW);
        CWnd* battleControls[] = { &battleStart_, &battleStop_, &battleSettings_, &battleRound_,
            &battleSides_[0], &battleSides_[1], &battleInfo_, &battleExperience_ };
        for (auto control : battleControls)
            control->ShowWindow(leveling ? SW_SHOW : SW_HIDE);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            battleSides_[side].SetWindowText(playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            battleSides_[side].Update(view_.battle, offset, received);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"第%d回合", view_.battle.round);
        battleRound_.SetWindowText(round);
        battleTabs_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        battleSettings_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending && !view_.player.inBattle);
        if ((view_.encounterEnabled || view_.encounterPending || view_.player.inBattle) &&
            battleSettingsWindow_.GetSafeHwnd())
            battleSettingsWindow_.ShowWindow(SW_HIDE);
        battleStart_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending);
        battleStop_.EnableWindow(leveling && (view_.encounterEnabled || view_.encounterPending));
        CString message = L"战斗提示：";
        if (view_.encounterPending || (!view_.player.inBattle && view_.encounterMessage == L"自动战斗已停止"))
            message += view_.encounterMessage.c_str();
        else
            message += view_.battleResult.message.empty() ? L"等待战斗" : view_.battleResult.message.c_str();
        battleInfo_.SetWindowText(message);
        CString experience;
        if (view_.battleResult.received)
            experience.Format(L"经验获得：本场 %u    累计 %llu",
                view_.battleResult.experience, static_cast<unsigned long long>(view_.battleResult.totalExperience));
        else
            experience.Format(L"经验获得：本场 —    累计 %llu",
                static_cast<unsigned long long>(view_.battleResult.totalExperience));
        battleExperience_.SetWindowText(experience);
    }

    void MainWindow::OnBattleSettings()
    {
        if (view_.screen != Screen::Game || view_.encounterEnabled || view_.encounterPending || view_.player.inBattle)
            return;
        battleSettingsWindow_.Show(this, view_.magic, session_);
    }

    void MainWindow::OnBattleStart()
    {
        if (view_.screen == Screen::Game)
        {
            if (battleSettingsWindow_.GetSafeHwnd())
                battleSettingsWindow_.ShowWindow(SW_HIDE);
            session_.SetEncounterEnabled(true);
        }
    }

    void MainWindow::OnBattleStop()
    {
        if (view_.screen == Screen::Game)
            session_.SetEncounterEnabled(false);
    }
}
