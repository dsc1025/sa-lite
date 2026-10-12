#include <afxwin.h>
#include <afxcmn.h>
#include "LoginClient.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, Tabs, List, Detail, PetStateCombo };
    enum class Screen { Login, Connecting, Characters, Entering, Game, Closing };
    const wchar_t* PetStateNames[] = { L"等待", L"战斗", L"骑乘" };

    int PetStateIndex(const LoginClient::PlayerStatus& player, int slot)
    {
        if (player.ridePet == slot) return static_cast<int>(LoginClient::PetState::Ride);
        if (player.battlePet == slot) return static_cast<int>(LoginClient::PetState::Battle);
        return static_cast<int>(LoginClient::PetState::Standby);
    }

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
            background_.CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            white_.CreateSolidBrush(GetSysColor(COLOR_WINDOW));
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
        afx_msg void OnPetClick(NMHDR* header, LRESULT* result);
        afx_msg void OnPetState();
        afx_msg void OnPetStateClose();
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();
        void Refresh();
        void RefreshDetail();
        void AddRow(const wchar_t* name, const CString& value);
        void LoadLogin();
        void SaveLogin();
        void StopWorker();
        void Publish(Screen screen, const CString& message, const LoginClient* client = nullptr);
        void RunSession(std::string address, std::uint16_t port, std::string account, std::string password);

        CFont font_;
        CBrush background_, white_;
        CStatic summary_, footer_, labels_[4];
        CString configPath_;
        CEdit address_, port_, account_, password_;
        CButton login_, enter_, exit_;
        CTabCtrl tabs_;
        CListCtrl list_, detail_;
        CComboBox petState_;
        int editingPet_ = -1, requestedPet_ = -1, requestedPetState_ = 0;
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
        ON_NOTIFY(NM_CLICK, List, &MainWindow::OnPetClick)
        ON_CBN_SELENDOK(PetStateCombo, &MainWindow::OnPetState)
        ON_CBN_SELENDCANCEL(PetStateCombo, &MainWindow::OnPetStateClose)
        ON_CBN_KILLFOCUS(PetStateCombo, &MainWindow::OnPetStateClose)
    END_MESSAGE_MAP()

    int MainWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CFrameWnd::OnCreate(create) == -1)
            return -1;
        font_.CreatePointFont(90, L"Microsoft YaHei UI");
        CRect empty(0, 0, 0, 0);
        summary_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        footer_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        const wchar_t* captions[] = { L"服务器", L"端口", L"账号", L"密码" };
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
        tabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, Tabs);
        tabs_.InsertItem(0, L"人物");
        tabs_.InsertItem(1, L"宠物");
        tabs_.InsertItem(2, L"道具");
        DWORD listStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        list_.Create(listStyle, empty, this, List);
        detail_.Create(listStyle, empty, this, Detail);
        petState_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, PetStateCombo);
        for (const wchar_t* state : PetStateNames)
            petState_.AddString(state);
        tabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        list_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        detail_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        detail_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        list_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        list_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 70);
        detail_.InsertColumn(0, L"属性", LVCFMT_LEFT, 150);
        detail_.InsertColumn(1, L"数值", LVCFMT_LEFT, 230);
        list_.SetBkColor(RGB(255, 255, 255));
        detail_.SetBkColor(RGB(255, 255, 255));
        list_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        detail_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
            child->SetFont(&font_);
        try { LoadLogin(); }
        catch (const std::exception& error) { view_.message = ErrorText(error.what()); }
        Refresh();
        return 0;
    }

    void MainWindow::OnSize(UINT type, int width, int height)
    {
        CFrameWnd::OnSize(type, width, height);
        if (detail_.GetSafeHwnd())
            Layout();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* info)
    {
        CFrameWnd::OnGetMinMaxInfo(info);
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        info->ptMinTrackSize = CPoint(login || characters ? 460 : 640, login || characters ? 330 : 480);
    }

    void MainWindow::Layout()
    {
        OnPetStateClose();
        CRect area;
        GetClientRect(&area);
        int width = area.Width(), height = area.Height();
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        summary_.MoveWindow(16, 16, width - 128, 24);
        footer_.MoveWindow(16, height - 30, width - 32, 24);
        int left = (width - 360) / 2;
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (int i = 0; i < 4; ++i)
        {
            int top = 30 + i * 42;
            labels_[i].MoveWindow(left, top + 4, 80, 24);
            edits[i]->MoveWindow(left + 88, top, 272, 26);
        }
        login_.MoveWindow(left + 88, 208, 100, 30);
        exit_.MoveWindow(login ? left + 200 : width - 116, login ? 208 : 12, 100, 30);
        enter_.MoveWindow(width - 146, height - 76, 130, 30);
        tabs_.MoveWindow(16, 54, width - 32, height - 92);
        CRect page(16, 54, width - 16, height - 38);
        tabs_.AdjustRect(FALSE, &page);
        page.DeflateRect(6, 6);
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        bool player = tabs_.GetCurSel() == 0;
        list_.MoveWindow(characters ? 16 : page.left, characters ? 54 : page.top,
            characters ? width - 32 : 220, characters ? height - 146 : page.Height());
        detail_.MoveWindow(player ? page.left : page.left + 236, page.top,
            player ? page.Width() : page.Width() - 236, page.Height());
        if (!login)
        {
            CRect detailArea, listArea;
            detail_.GetClientRect(&detailArea);
            list_.GetClientRect(&listArea);
            int detailWidth = detailArea.Width() - 150 - GetSystemMetrics(SM_CXVSCROLL);
            bool petColumns = tabs_.GetCurSel() == 1 && list_.GetHeaderCtrl()->GetItemCount() == 3;
            int listWidth = listArea.Width() - (petColumns ? 105 : 70) - GetSystemMetrics(SM_CXVSCROLL);
            list_.SetColumnWidth(1, petColumns ? 45 : 70);
            if (petColumns)
                list_.SetColumnWidth(2, 60);
            detail_.SetColumnWidth(1, detailWidth > 0 ? detailWidth : 0);
            list_.SetColumnWidth(0, listWidth > 0 ? listWidth : 0);
            detail_.ShowScrollBar(SB_HORZ, FALSE);
            list_.ShowScrollBar(SB_HORZ, FALSE);
        }
        Invalidate(FALSE);
    }

    void MainWindow::OnPaint()
    {
        CPaintDC dc(this);
        CRect area;
        GetClientRect(&area);
        dc.FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
    }

    HBRUSH MainWindow::OnCtlColor(CDC* dc, CWnd* window, UINT type)
    {
        CFrameWnd::OnCtlColor(dc, window, type);
        dc->SetTextColor(GetSysColor(type == CTLCOLOR_EDIT ? COLOR_WINDOWTEXT : COLOR_BTNTEXT));
        dc->SetBkColor(GetSysColor(type == CTLCOLOR_EDIT ? COLOR_WINDOW : COLOR_BTNFACE));
        return static_cast<HBRUSH>((type == CTLCOLOR_EDIT ? white_ : background_).GetSafeHandle());
    }

    void MainWindow::AddRow(const wchar_t* name, const CString& value)
    {
        int row = detail_.InsertItem(detail_.GetItemCount(), name);
        detail_.SetItemText(row, 1, value);
    }

    void MainWindow::LoadLogin()
    {
        std::vector<wchar_t> path(32768);
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
            throw std::runtime_error("无法获取程序目录，不能读取登录配置");
        CString executable(path.data(), static_cast<int>(length));
        int separator = executable.ReverseFind(L'\\');
        if (separator < 0)
            throw std::runtime_error("无法获取程序目录，不能读取登录配置");
        configPath_ = executable.Left(separator + 1) + L"login.ini";
        wchar_t value[256];
        GetPrivateProfileStringW(L"Login", L"Server", L"", value, 256, configPath_);
        address_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Port", L"9065", value, 256, configPath_);
        port_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Account", L"", value, 256, configPath_);
        account_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Password", L"", value, 256, configPath_);
        password_.SetWindowText(value);
    }

    void MainWindow::SaveLogin()
    {
        if (configPath_.IsEmpty())
            throw std::runtime_error("无法获取程序目录，不能保存登录配置");
        const wchar_t* keys[] = { L"Server", L"Port", L"Account", L"Password" };
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (int i = 0; i < 4; ++i)
        {
            CString value;
            edits[i]->GetWindowText(value);
            if (!WritePrivateProfileStringW(L"Login", keys[i], value, configPath_))
                throw std::runtime_error("无法保存 exe 同目录的 login.ini，请检查目录写入权限");
        }
    }

    void MainWindow::RefreshDetail()
    {
        detail_.DeleteAllItems();
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
            SetWindowPos(nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
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
        tabs_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(characters || (game && tabs_.GetCurSel() != 0) ? SW_SHOW : SW_HIDE);
        detail_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        exit_.EnableWindow(view_.screen != Screen::Closing);
        exit_.SetWindowText(game ? L"退出登录" : L"退出");
        SetWindowText(login ? L"登录" : characters ? L"选择角色" : L"石器");
        summary_.ShowWindow(login ? SW_HIDE : SW_SHOW);
        footer_.SetWindowText(view_.message);
        CString summary = game ? view_.characterName : CString();
        if (game && view_.player.received)
        {
            CString stats;
            stats.Format(L"   等级 %d   HP %d/%d   MP %d/%d   金币 %d", view_.player.level,
                view_.player.hp, view_.player.maxHp, view_.player.mp, view_.player.maxMp, view_.player.gold);
            summary += stats;
        }
        summary_.SetWindowText(characters ? CString(L"选择角色后进入游戏") : summary);
        bool petColumns = game && tabs_.GetCurSel() == 1;
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
        else if (game && tabs_.GetCurSel() == 1)
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
                    int pet, state;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stop_) break;
                        pet = requestedPet_;
                        state = requestedPetState_;
                        requestedPet_ = -1;
                    }
                    if (client.PollGame(100))
                        Publish(Screen::Game, L"已连接", &client);
                    if (pet >= 0)
                    {
                        try
                        {
                            client.SetPetState(pet, static_cast<LoginClient::PetState>(state));
                            Publish(Screen::Game, L"宠物状态已更新", &client);
                        }
                        catch (const std::exception& error)
                        {
                            Publish(Screen::Game, ErrorText(error.what()), &client);
                        }
                    }
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
            SaveLogin();
            if (worker_.joinable()) worker_.join();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = false; done_ = false; requestedCharacter_ = -1;
                requestedPet_ = -1;
                shared_ = {};
                shared_.screen = Screen::Connecting;
                shared_.message = L"正在连接并登录…";
            }
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
        if (view_.screen == Screen::Login)
        {
            OnClose();
            return;
        }
        if (view_.screen == Screen::Closing)
            return;
        StopWorker();
        view_.screen = Screen::Closing;
        view_.message = L"正在退出并返回账号登录…";
        Refresh();
    }

    BOOL MainWindow::PreTranslateMessage(MSG* message)
    {
        if (petState_.GetSafeHwnd() && petState_.IsWindowVisible() && message->message == WM_KEYDOWN)
        {
            if (message->wParam == VK_ESCAPE)
            {
                petState_.ShowDropDown(FALSE);
                OnPetStateClose();
                return TRUE;
            }
            if (message->wParam == VK_RETURN)
            {
                OnPetState();
                return TRUE;
            }
        }
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
                OnExit();
                return TRUE;
            }
            if (message->wParam == VK_TAB && IsDialogMessage(message))
                return TRUE;
        }
        return CFrameWnd::PreTranslateMessage(message);
    }

    void MainWindow::OnClose()
    {
        if (view_.screen == Screen::Login && !closeWindow_)
        {
            try { SaveLogin(); }
            catch (const std::exception& error) { AfxMessageBox(ErrorText(error.what()), MB_OK | MB_ICONWARNING); }
        }
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
        bool done, stopped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            view_ = shared_;
            done = done_;
            stopped = stop_;
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
        if (stopped && !done)
        {
            view_.screen = Screen::Closing;
            view_.message = L"正在退出并返回账号登录…";
        }
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

    void MainWindow::OnPetClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (view_.screen != Screen::Game || tabs_.GetCurSel() != 1 ||
            click->iItem < 0 || click->iSubItem != 2 || !view_.player.received || !view_.player.battleReceived)
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

    void MainWindow::OnPetState()
    {
        int slot = editingPet_, state = petState_.GetCurSel();
        OnPetStateClose();
        if (slot < 0 || state < 0 || view_.screen != Screen::Game)
            return;
        if (state == PetStateIndex(view_.player, slot))
            return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || requestedPet_ >= 0)
                return;
            requestedPet_ = slot;
            requestedPetState_ = state;
        }
        footer_.SetWindowText(L"正在设置宠物状态…");
    }

    void MainWindow::OnPetStateClose()
    {
        editingPet_ = -1;
        if (petState_.GetSafeHwnd())
            petState_.ShowWindow(SW_HIDE);
    }

    class ClientApp : public CWinApp
    {
        BOOL InitInstance() override
        {
            INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
            InitCommonControlsEx(&controls);
            CWinApp::InitInstance();
            auto window = new MainWindow;
            if (!window->Create(nullptr, L"登录", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CRect(0, 0, 460, 330)))
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
