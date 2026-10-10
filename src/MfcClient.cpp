#include <afxwin.h>
#include <afxcmn.h>
#include "LoginClient.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, Tabs, List, Detail };
    enum class Screen { Login, Connecting, Characters, Entering, Game, Closing };

    CString LegacyText(const std::string& text)
    {
        if (text.empty())
            return CString();
        int length = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        CString result;
        wchar_t* buffer = result.GetBuffer(length);
        MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), buffer, length);
        result.ReleaseBuffer(length);
        return result;
    }

    CString ErrorText(const char* message)
    {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, nullptr, 0);
        if (length == 0)
            return LegacyText(message);
        CString result;
        wchar_t* buffer = result.GetBuffer(length);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, buffer, length);
        result.ReleaseBuffer(length - 1);
        return result;
    }

    std::string AsciiText(CEdit& edit)
    {
        CString text;
        edit.GetWindowText(text);
        std::string result;
        for (int i = 0; i < text.GetLength(); ++i)
        {
            if (text[i] < 33 || text[i] > 126)
                throw std::invalid_argument("Please use ASCII characters without spaces");
            result += static_cast<char>(text[i]);
        }
        return result;
    }

    CString Number(int value)
    {
        CString text;
        text.Format(L"%d", value);
        return text;
    }

    CString Ratio(int value, int maximum)
    {
        CString text;
        text.Format(L"%d / %d", value, maximum);
        return text;
    }

    struct SessionView
    {
        Screen screen = Screen::Login;
        CString message = L"请输入服务器和账号信息";
        CString characterName;
        std::vector<LoginClient::Character> characters;
        LoginClient::PlayerStatus player;
        std::vector<LoginClient::PetStatus> pets;
        std::vector<LoginClient::ItemStatus> items;
        bool hasItems = false;
        bool failed = false;
    };

    class MainWindow : public CFrameWnd
    {
    public:
        MainWindow()
        {
            background_.CreateSolidBrush(RGB(246, 247, 249));
            white_.CreateSolidBrush(RGB(255, 255, 255));
        }

        ~MainWindow() override
        {
            StopWorker();
            if (worker_.joinable())
                worker_.join();
        }

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
        afx_msg void OnPaint();
        afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);
        afx_msg void OnLogin();
        afx_msg void OnEnter();
        afx_msg void OnExit();
        afx_msg void OnClose();
        afx_msg LRESULT OnSession(WPARAM, LPARAM);
        afx_msg void OnTab(NMHDR*, LRESULT* result);
        afx_msg void OnSelect(NMHDR*, LRESULT* result);
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();
        void Refresh();
        void RefreshDetail();
        void AddRow(const wchar_t* name, const CString& value);
        void StopWorker();
        void Publish(Screen screen, const CString& message, const LoginClient* client = nullptr);
        void RunSession(std::string address, std::uint16_t port, std::string account, std::string password);

        CFont font_, headingFont_;
        CBrush background_, white_;
        CStatic heading_, summary_, footer_, description_, labels_[4];
        CEdit address_, port_, account_, password_;
        CButton login_, enter_, exit_;
        CTabCtrl tabs_;
        CListCtrl list_, detail_;
        SessionView view_, shared_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
        bool stop_ = false, done_ = true, closeWindow_ = false;
        int requestedCharacter_ = -1;
        bool refreshing_ = false;
        bool expanded_ = false;
    };

    BEGIN_MESSAGE_MAP(MainWindow, CFrameWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
        ON_WM_GETMINMAXINFO()
        ON_WM_PAINT()
        ON_WM_CTLCOLOR()
        ON_WM_CLOSE()
        ON_BN_CLICKED(LoginButton, &MainWindow::OnLogin)
        ON_BN_CLICKED(EnterButton, &MainWindow::OnEnter)
        ON_BN_CLICKED(ExitButton, &MainWindow::OnExit)
        ON_MESSAGE(SessionMessage, &MainWindow::OnSession)
        ON_NOTIFY(TCN_SELCHANGE, Tabs, &MainWindow::OnTab)
        ON_NOTIFY(LVN_ITEMCHANGED, List, &MainWindow::OnSelect)
    END_MESSAGE_MAP()

    int MainWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CFrameWnd::OnCreate(create) == -1)
            return -1;
        font_.CreatePointFont(100, L"Microsoft YaHei UI");
        headingFont_.CreatePointFont(180, L"Microsoft YaHei UI");
        CRect empty(0, 0, 0, 0);
        heading_.Create(L"石器", WS_CHILD | WS_VISIBLE, empty, this);
        summary_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        footer_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        description_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        const wchar_t* captions[] = { L"服务器 IPv4", L"端口", L"账号", L"密码" };
        for (int i = 0; i < 4; ++i)
            labels_[i].Create(captions[i], WS_CHILD, empty, this);
        DWORD editStyle = WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        address_.Create(editStyle, empty, this, 110);
        port_.Create(editStyle | ES_NUMBER, empty, this, 111);
        account_.Create(editStyle, empty, this, 112);
        password_.Create(editStyle | ES_PASSWORD, empty, this, 113);
        address_.SetLimitText(15);
        port_.SetLimitText(5);
        port_.SetWindowText(L"9065");
        account_.SetLimitText(15);
        password_.SetLimitText(15);
        login_.Create(L"登录", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, empty, this, LoginButton);
        enter_.Create(L"进入游戏", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, empty, this, EnterButton);
        exit_.Create(L"退出", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, empty, this, ExitButton);
        tabs_.Create(WS_CHILD | WS_TABSTOP | TCS_FLATBUTTONS | TCS_BUTTONS, empty, this, Tabs);
        tabs_.InsertItem(0, L"人物");
        tabs_.InsertItem(1, L"宠物");
        tabs_.InsertItem(2, L"道具");
        DWORD listStyle = WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        list_.Create(listStyle, empty, this, List);
        detail_.Create(listStyle, empty, this, Detail);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        detail_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        list_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        list_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 70);
        detail_.InsertColumn(0, L"属性", LVCFMT_LEFT, 150);
        detail_.InsertColumn(1, L"数值", LVCFMT_LEFT, 230);
        list_.SetBkColor(RGB(255, 255, 255));
        detail_.SetBkColor(RGB(255, 255, 255));
        list_.SetTextColor(RGB(40, 45, 55));
        detail_.SetTextColor(RGB(40, 45, 55));
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
            child->SetFont(&font_);
        heading_.SetFont(&headingFont_);
        Refresh();
        return 0;
    }

    void MainWindow::OnSize(UINT type, int width, int height)
    {
        CFrameWnd::OnSize(type, width, height);
        if (heading_.GetSafeHwnd())
            Layout();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* info)
    {
        CFrameWnd::OnGetMinMaxInfo(info);
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        info->ptMinTrackSize = CPoint(login ? 520 : 720, 580);
    }

    void MainWindow::Layout()
    {
        CRect area;
        GetClientRect(&area);
        int width = area.Width(), height = area.Height();
        heading_.MoveWindow(32, 24, width - 180, 44);
        summary_.MoveWindow(54, 80, width - 90, 28);
        exit_.MoveWindow(width - 112, 30, 80, 32);
        footer_.MoveWindow(32, height - 38, width - 64, 28);
        int left = (width - 340) / 2;
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (int i = 0; i < 4; ++i)
        {
            int top = 148 + i * 68;
            labels_[i].MoveWindow(left, top, 340, 24);
            edits[i]->MoveWindow(left, top + 25, 340, 32);
        }
        login_.MoveWindow(left, 434, 340, 38);
        enter_.MoveWindow(width - 182, height - 92, 150, 38);
        tabs_.MoveWindow(32, 120, width - 64, 38);
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        bool player = tabs_.GetCurSel() == 0;
        list_.MoveWindow(32, characters ? 134 : 172, characters ? width - 64 : 280,
            characters ? height - 246 : height - 242);
        detail_.MoveWindow(player ? 32 : 332, 172, player ? width - 64 : width - 364, height - 326);
        description_.MoveWindow(player ? 32 : 332, height - 138, player ? width - 64 : width - 364, 82);
        detail_.SetColumnWidth(1, (player ? width - 64 : width - 364) - 174);
        list_.SetColumnWidth(0, characters ? width - 164 : 190);
        Invalidate(FALSE);
    }

    void MainWindow::OnPaint()
    {
        CPaintDC dc(this);
        CRect area;
        GetClientRect(&area);
        dc.FillSolidRect(area, RGB(246, 247, 249));
        COLORREF color = view_.screen == Screen::Game || view_.screen == Screen::Characters
            ? RGB(45, 160, 105) : RGB(160, 166, 176);
        CBrush dot(color);
        auto oldBrush = dc.SelectObject(&dot);
        auto oldPen = dc.SelectStockObject(NULL_PEN);
        dc.Ellipse(34, 87, 43, 96);
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);
    }

    HBRUSH MainWindow::OnCtlColor(CDC* dc, CWnd* window, UINT type)
    {
        CFrameWnd::OnCtlColor(dc, window, type);
        dc->SetTextColor(window == &heading_ ? RGB(40, 100, 210)
            : window == &footer_ ? RGB(100, 108, 122) : RGB(40, 45, 55));
        dc->SetBkColor(type == CTLCOLOR_EDIT ? RGB(255, 255, 255) : RGB(246, 247, 249));
        return static_cast<HBRUSH>((type == CTLCOLOR_EDIT ? white_ : background_).GetSafeHandle());
    }

    void MainWindow::AddRow(const wchar_t* name, const CString& value)
    {
        int row = detail_.InsertItem(detail_.GetItemCount(), name);
        detail_.SetItemText(row, 1, value);
    }

    void MainWindow::RefreshDetail()
    {
        detail_.DeleteAllItems();
        description_.SetWindowText(L"");
        int tab = tabs_.GetCurSel();
        if (tab == 0)
        {
            const auto& p = view_.player;
            if (!p.received)
            {
                description_.SetWindowText(L"正在等待人物数据…");
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
                description_.SetWindowText(L"正在等待道具数据…");
                return;
            }
            int selected = list_.GetNextItem(-1, LVNI_SELECTED);
            if (selected < 0)
            {
                description_.SetWindowText(tab == 2 ? L"背包与装备为空" : L"选择一个宠物查看属性");
                return;
            }
            auto slot = static_cast<std::size_t>(list_.GetItemData(selected));
            if (tab == 1 && slot < view_.pets.size())
            {
                const auto& p = view_.pets[slot];
                if (!p.received || !p.present)
                {
                    description_.SetWindowText(p.received ? L"这个宠物栏位为空" : L"正在等待宠物数据…");
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
                description_.SetWindowText(LegacyText(item.description));
            }
        }
    }

    void MainWindow::Refresh()
    {
        refreshing_ = true;
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        bool game = view_.screen == Screen::Game;
        if ((characters || game) && !expanded_)
        {
            expanded_ = true;
            SetWindowPos(nullptr, 0, 0, 1024, 768, SWP_NOMOVE | SWP_NOZORDER);
            CenterWindow();
        }
        else if (login && expanded_)
        {
            expanded_ = false;
            SetWindowPos(nullptr, 0, 0, 520, 610, SWP_NOMOVE | SWP_NOZORDER);
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
        tabs_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(characters || (game && tabs_.GetCurSel() != 0) ? SW_SHOW : SW_HIDE);
        detail_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        description_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        exit_.EnableWindow(view_.screen != Screen::Closing);
        heading_.SetWindowText(login ? L"石器 · 登录" : characters ? L"选择角色" : L"石器");
        footer_.SetWindowText(view_.message);
        CString summary = game ? view_.characterName : CString();
        if (game && view_.player.received)
        {
            CString stats;
            stats.Format(L"   等级 %d   HP %d/%d   MP %d/%d   金币 %d", view_.player.level,
                view_.player.hp, view_.player.maxHp, view_.player.mp, view_.player.maxMp, view_.player.gold);
            summary += stats;
        }
        summary_.SetWindowText(summary.IsEmpty() ? L"石器放置" : summary);
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
        else if (game && tabs_.GetCurSel() == 1)
        {
            for (std::size_t i = 0; i < view_.pets.size(); ++i)
            {
                const auto& pet = view_.pets[i];
                CString name = pet.present ? LegacyText(pet.nickname.empty() ? pet.name : pet.nickname) : L"空栏位";
                if (!pet.received) name = L"等待数据…";
                int row = list_.InsertItem(static_cast<int>(i), name);
                list_.SetItemText(row, 1, pet.present ? Number(pet.level) : CString());
                list_.SetItemData(row, i);
            }
        }
        else if (game && tabs_.GetCurSel() == 2)
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
            if (!view_.hasItems) description_.SetWindowText(L"正在等待道具数据…");
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

    void MainWindow::StopWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
    }

    void MainWindow::Publish(Screen screen, const CString& message, const LoginClient* client)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shared_.screen = screen;
            shared_.message = message;
            if (client)
            {
                shared_.player = client->GetPlayerStatus();
                shared_.pets = client->GetPets();
                shared_.items = client->GetItems();
                shared_.hasItems = client->HasItems();
            }
        }
        ::PostMessage(m_hWnd, SessionMessage, 0, 0);
    }

    void MainWindow::RunSession(std::string address, std::uint16_t port, std::string account, std::string password)
    {
        bool entered = false;
        try
        {
            LoginClient client;
            client.Connect(address, port);
            auto login = client.Login(account, password);
            SecureZeroMemory(password.data(), password.size());
            password.clear();
            if (login != LoginClient::LoginResult::Accepted)
                throw std::runtime_error("账号或密码不正确，或账号服务拒绝登录");
            auto characters = client.GetCharacters();
            if (characters.empty())
                throw std::runtime_error("账号下没有角色，请先使用旧客户端创建角色");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_.characters = characters;
            }
            Publish(Screen::Characters, L"选择一个角色，进入游戏");
            int selection;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || requestedCharacter_ >= 0; });
                selection = stop_ ? -1 : requestedCharacter_;
            }
            if (selection >= 0)
            {
                Publish(Screen::Entering, L"正在进入游戏…");
                client.EnterGame(characters.at(selection));
                entered = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    shared_.characterName = LegacyText(characters[selection].name);
                }
                Publish(Screen::Game, L"已连接", &client);
                for (;;)
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stop_) break;
                    }
                    if (client.PollGame(100))
                        Publish(Screen::Game, L"已连接", &client);
                }
            }
            if (entered)
            {
                Publish(Screen::Closing, L"正在退出并等待服务器保存…");
                client.LeaveGame();
            }
            Publish(Screen::Login, L"连接已关闭");
        }
        catch (const std::exception& error)
        {
            SecureZeroMemory(password.data(), password.size());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_.failed = true;
            }
            Publish(Screen::Login, ErrorText(error.what()));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        ::PostMessage(m_hWnd, SessionMessage, 0, 0);
    }

    void MainWindow::OnLogin()
    {
        if (view_.screen != Screen::Login)
            return;
        try
        {
            auto address = AsciiText(address_);
            auto account = AsciiText(account_);
            auto password = AsciiText(password_);
            auto portText = AsciiText(port_);
            if (portText.empty() || portText.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("端口必须为 1 至 65535 的数字");
            auto port = std::stoul(portText);
            if (address.empty() || account.empty() || password.empty() || port == 0 || port > 65535)
                throw std::invalid_argument("请填写服务器、端口、账号和密码");
            if (worker_.joinable()) worker_.join();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = false; done_ = false; requestedCharacter_ = -1;
                shared_ = {};
                shared_.screen = Screen::Connecting;
                shared_.message = L"正在连接并登录…";
            }
            password_.SetWindowText(L"");
            view_ = {};
            view_.screen = Screen::Connecting;
            view_.message = L"正在连接并登录…";
            Refresh();
            worker_ = std::thread(&MainWindow::RunSession, this, address,
                static_cast<std::uint16_t>(port), account, password);
            SecureZeroMemory(password.data(), password.size());
        }
        catch (const std::exception& error)
        {
            view_.screen = Screen::Login;
            view_.message = ErrorText(error.what());
            if (!worker_.joinable())
            {
                std::lock_guard<std::mutex> lock(mutex_);
                done_ = true;
            }
            Refresh();
        }
    }

    void MainWindow::OnEnter()
    {
        int selected = list_.GetNextItem(-1, LVNI_SELECTED);
        if (view_.screen != Screen::Characters || selected < 0) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requestedCharacter_ = static_cast<int>(list_.GetItemData(selected));
        }
        view_.screen = Screen::Entering;
        view_.message = L"正在进入游戏…";
        Refresh();
        wake_.notify_all();
    }

    void MainWindow::OnExit()
    {
        OnClose();
    }

    BOOL MainWindow::PreTranslateMessage(MSG* message)
    {
        if (message->message == WM_KEYDOWN)
        {
            if (message->wParam == VK_RETURN)
            {
                if (view_.screen == Screen::Login) OnLogin();
                else if (view_.screen == Screen::Characters) OnEnter();
                return TRUE;
            }
            if (message->wParam == VK_ESCAPE)
            {
                OnClose();
                return TRUE;
            }
            if (message->wParam == VK_TAB && IsDialogMessage(message))
                return TRUE;
        }
        return CFrameWnd::PreTranslateMessage(message);
    }

    void MainWindow::OnClose()
    {
        closeWindow_ = true;
        StopWorker();
        bool done;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done = done_;
        }
        if (done)
        {
            if (worker_.joinable()) worker_.join();
            DestroyWindow();
        }
        else
        {
            view_.screen = Screen::Closing;
            view_.message = L"正在关闭连接，请稍候…";
            Refresh();
        }
    }

    LRESULT MainWindow::OnSession(WPARAM, LPARAM)
    {
        bool done;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            view_ = shared_;
            done = done_;
        }
        if (done && worker_.joinable()) worker_.join();
        if (closeWindow_)
        {
            if (done)
            {
                if (view_.failed)
                    AfxMessageBox(view_.message, MB_OK | MB_ICONWARNING);
                DestroyWindow();
            }
            return 0;
        }
        // A failed session can publish its final state just before the thread ends.
        if (view_.screen == Screen::Login && !done)
            view_.screen = Screen::Connecting;
        Refresh();
        return 0;
    }

    void MainWindow::OnTab(NMHDR*, LRESULT* result)
    {
        list_.DeleteAllItems();
        Refresh();
        *result = 0;
    }

    void MainWindow::OnSelect(NMHDR*, LRESULT* result)
    {
        if (!refreshing_ && view_.screen == Screen::Game)
            RefreshDetail();
        *result = 0;
    }

    class ClientApp : public CWinApp
    {
        BOOL InitInstance() override
        {
            INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
            InitCommonControlsEx(&controls);
            CWinApp::InitInstance();
            auto window = new MainWindow;
            if (!window->Create(nullptr, L"石器", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CRect(0, 0, 520, 610)))
                return FALSE;
            m_pMainWnd = window;
            window->CenterWindow();
            window->ShowWindow(m_nCmdShow);
            window->UpdateWindow();
            return TRUE;
        }
    };

    ClientApp application;
}
