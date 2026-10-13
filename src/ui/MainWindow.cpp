#include "MainWindow.h"
#include "BattleUi.h"
#include "UiText.h"
#include "../map/MapData.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    BEGIN_MESSAGE_MAP(ItemPropertiesWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
    END_MESSAGE_MAP()

    void ItemPropertiesWindow::Show(CWnd* owner, const CString& text, const wchar_t* title)
    {
        if (!GetSafeHwnd())
        {
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)),
                title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
                CRect(0, 0, 460, 320), owner, 0))
                return;
            CenterWindow(owner);
        }
        SetWindowText(title);
        text_.SetWindowText(text);
        text_.SetSel(0, 0);
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
        text_.SetFocus();
    }

    int ItemPropertiesWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1)
            return -1;
        if (!font_.GetSafeHandle())
            font_.CreatePointFont(90, L"Microsoft YaHei UI");
        CRect area;
        GetClientRect(&area);
        area.DeflateRect(8, 8);
        if (!text_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, area, this, 1))
            return -1;
        text_.SetFont(&font_);
        return 0;
    }

    void ItemPropertiesWindow::OnSize(UINT type, int width, int height)
    {
        CWnd::OnSize(type, width, height);
        if (text_.GetSafeHwnd())
            text_.MoveWindow(8, 8, width > 16 ? width - 16 : 0, height > 16 ? height - 16 : 0);
    }

    BOOL ItemPropertiesWindow::PreTranslateMessage(MSG* message)
    {
        if (message->hwnd == text_.GetSafeHwnd() && message->message == WM_KEYDOWN &&
            message->wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            text_.SetSel(0, -1);
            return TRUE;
        }
        if (message->message == WM_KEYDOWN &&
            (message->wParam == VK_RETURN || message->wParam == VK_ESCAPE))
            return TRUE;
        return CWnd::PreTranslateMessage(message);
    }

    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, Tabs, List, Detail, PetStateCombo, PetStorageList, ItemStorageList, MainTabs = 120, ShopTabs, MapPositionCombo, MapRegionCombo };

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
        ON_NOTIFY(TCN_SELCHANGE, MainTabs, &MainWindow::OnMainTab)
        ON_NOTIFY(TCN_SELCHANGE, Tabs, &MainWindow::OnTab)
        ON_NOTIFY(TCN_SELCHANGE, IDC_BATTLE_TABS, &MainWindow::OnMainTab)
        ON_BN_CLICKED(IDC_BATTLE_START, &MainWindow::OnBattleStart)
        ON_BN_CLICKED(IDC_BATTLE_STOP, &MainWindow::OnBattleStop)
        ON_NOTIFY(LVN_ITEMCHANGED, List, &MainWindow::OnSelect)
        ON_NOTIFY(NM_CLICK, List, &MainWindow::OnPetClick)
        ON_NOTIFY(NM_RCLICK, List, &MainWindow::OnListRightClick)
        ON_NOTIFY(NM_RCLICK, PetStorageList, &MainWindow::OnPetStorageRightClick)
        ON_NOTIFY(NM_DBLCLK, PetStorageList, &MainWindow::OnListDoubleClick)
        ON_NOTIFY(NM_DBLCLK, List, &MainWindow::OnListDoubleClick)
        ON_NOTIFY(NM_DBLCLK, ItemStorageList, &MainWindow::OnListDoubleClick)
        ON_NOTIFY(NM_RCLICK, ItemStorageList, &MainWindow::OnItemStorageRightClick)
        ON_CBN_SELENDOK(PetStateCombo, &MainWindow::OnPetState)
        ON_CBN_SELENDCANCEL(PetStateCombo, &MainWindow::OnPetStateClose)
        ON_CBN_KILLFOCUS(PetStateCombo, &MainWindow::OnPetStateClose)
        ON_CBN_SELENDOK(MapPositionCombo, &MainWindow::OnMapPosition)
        ON_CBN_SELENDOK(MapRegionCombo, &MainWindow::OnMapRegion)
    END_MESSAGE_MAP()

    int MainWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CFrameWnd::OnCreate(create) == -1)
            return -1;
        try
        {
            mapNames_ = LoadMapNames(mapDirectory_, mapPositions_);
        }
        catch (const std::exception& error)
        {
            MessageBox(ErrorText(error.what()), L"地图索引加载失败", MB_OK | MB_ICONWARNING);
        }
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
        mainTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_VERTICAL | TCS_MULTILINE,
            empty, this, MainTabs);
        const wchar_t* mainCaptions[] = { L"角色", L"地图", L"战斗", L"商店", L"任务" };
        for (int i = 0; i < 5; ++i)
            mainTabs_.InsertItem(i, mainCaptions[i]);
        mainTabs_.SetPadding(CSize(12, 10));
        developing_.Create(L"开发中", WS_CHILD | SS_CENTER | SS_CENTERIMAGE, empty, this);
        miniMap_.Create(L"", WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY, empty, this);
        mapPosition_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            empty, this, MapPositionCombo);
        mapRegion_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            empty, this, MapRegionCombo);
        mapRegion_.AddString(L"请选择地区");
        const int regions[] = { 100, 200, 300, 400, 500, 1000, 1100,
            1200, 1300, 1400, 2000, 3000, 3100, 3200, 3300, 3400, 4000, 5000, 5100, 5509, 5540, 10920, 130 };
        for (int id : regions)
        {
            auto name = mapNames_.find(id);
            if (name != mapNames_.end())
            {
                int item = mapRegion_.AddString(name->second.c_str());
                if (item != CB_ERR && item != CB_ERRSPACE)
                    mapRegion_.SetItemData(item, id);
            }
        }
        mapRegion_.SetCurSel(0);
        tabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, Tabs);
        tabs_.InsertItem(0, L"属性");
        tabs_.InsertItem(1, L"宠物");
        tabs_.InsertItem(2, L"道具");
        CreateBattleControls();
        shopTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, ShopTabs);
        shopTabs_.InsertItem(0, L"道具");
        shopTabs_.InsertItem(1, L"装备");
        shopTabs_.InsertItem(2, L"技能");
        DWORD listStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        list_.Create(listStyle, empty, this, List);
        detail_.Create(listStyle, empty, this, Detail);
        petStorage_.Create(listStyle | WS_VSCROLL, empty, this, PetStorageList);
        itemStorage_.Create(listStyle, empty, this, ItemStorageList);
        itemStorage_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        itemStorage_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        itemStorage_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        itemStorage_.InsertColumn(1, L"分类", LVCFMT_LEFT, 80);
        itemStorage_.SetBkColor(RGB(255, 255, 255));
        itemStorage_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        petState_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, PetStateCombo);
        for (const wchar_t* state : PetStateNames)
            petState_.AddString(state);
        tabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        battleTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        shopTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        mainTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        list_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        detail_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        petStorage_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        detail_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        petStorage_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        petStorage_.InsertColumn(0, L"名称", LVCFMT_LEFT, 150);
        petStorage_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 45);
        list_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        list_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 70);
        detail_.InsertColumn(0, L"属性", LVCFMT_LEFT, 150);
        detail_.InsertColumn(1, L"数值", LVCFMT_LEFT, 230);
        list_.SetBkColor(RGB(255, 255, 255));
        detail_.SetBkColor(RGB(255, 255, 255));
        petStorage_.SetBkColor(RGB(255, 255, 255));
        list_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        detail_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        petStorage_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
            child->SetFont(&font_);
        try { LoadLogin(); }
        catch (const std::exception& error) { view_.message = TextEncoding::ErrorText(error.what()); }
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
        info->ptMinTrackSize = CPoint(login || characters ? 460 : 760, login || characters ? 330 : 480);
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
        mainTabs_.MoveWindow(16, 54, width - 32, height - 92);
        CRect mainPage(16, 54, width - 16, height - 38);
        CRect tabItem;
        if (mainTabs_.GetItemRect(0, &tabItem))
            mainPage.left += tabItem.right;
        mainPage.DeflateRect(6, 6);
        developing_.MoveWindow(mainPage);
        miniMap_.MoveWindow(mainPage);
        int regionLeft = mainPage.left + 8;
        mapRegion_.SetWindowPos(&CWnd::wndTop, regionLeft, mainPage.top + 4, 180, 300, SWP_NOACTIVATE);
        mapPosition_.SetWindowPos(&CWnd::wndTop, regionLeft + 188,
            mainPage.top + 4, 200, 220, SWP_NOACTIVATE);
        tabs_.MoveWindow(mainPage);
        shopTabs_.MoveWindow(mainPage);
        CRect page = mainPage;
        tabs_.AdjustRect(FALSE, &page);
        page.DeflateRect(6, 6);
        if (mainTabs_.GetCurSel() == 3)
        {
            CRect shopPage = mainPage;
            shopTabs_.AdjustRect(FALSE, &shopPage);
            shopPage.DeflateRect(6, 6);
            developing_.MoveWindow(shopPage);
        }
        LayoutBattle(mainPage);
        bool characters = view_.screen == Screen::Characters || view_.screen == Screen::Entering;
        bool player = tabs_.GetCurSel() == 0;
        bool pets = view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 1;
        bool items = view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 2;
        int itemWidth = (page.Width() - 16) / 2;
        list_.MoveWindow(characters ? 16 : page.left, characters ? 54 : page.top,
            characters ? width - 32 : pets || items ? itemWidth : 220, characters ? height - 146 : page.Height());
        if (pets)
            petStorage_.MoveWindow(page.left + itemWidth + 16, page.top,
                page.Width() - itemWidth - 16, page.Height());
        if (items)
            itemStorage_.MoveWindow(page.left + itemWidth + 16, page.top,
                page.Width() - itemWidth - 16, page.Height());
        detail_.MoveWindow(player ? page.left : page.left + 236, page.top,
            player ? page.Width() : page.Width() - 236, page.Height());
        if (!login)
        {
            CRect detailArea, listArea;
            detail_.GetClientRect(&detailArea);
            list_.GetClientRect(&listArea);
            int detailWidth = detailArea.Width() - 150 - GetSystemMetrics(SM_CXVSCROLL);
            bool petColumns = tabs_.GetCurSel() == 1 && list_.GetHeaderCtrl()->GetItemCount() == 3;
            int listWidth = listArea.Width() - (petColumns ? 105 : items ? 80 : 70) - GetSystemMetrics(SM_CXVSCROLL);
            list_.SetColumnWidth(1, petColumns ? 45 : items ? 80 : 70);
            if (petColumns)
                list_.SetColumnWidth(2, 60);
            detail_.SetColumnWidth(1, detailWidth > 0 ? detailWidth : 0);
            list_.SetColumnWidth(0, listWidth > 0 ? listWidth : 0);
            detail_.ShowScrollBar(SB_HORZ, FALSE);
            list_.ShowScrollBar(SB_HORZ, FALSE);
            CRect storageArea;
            petStorage_.GetClientRect(&storageArea);
            int storageWidth = storageArea.Width() - 45 - GetSystemMetrics(SM_CXVSCROLL);
            petStorage_.SetColumnWidth(0, storageWidth > 0 ? storageWidth : 0);
            petStorage_.SetColumnWidth(1, 45);
            petStorage_.ShowScrollBar(SB_HORZ, FALSE);
            CRect itemStorageArea;
            itemStorage_.GetClientRect(&itemStorageArea);
            int itemStorageWidth = itemStorageArea.Width() - 80 - GetSystemMetrics(SM_CXVSCROLL);
            itemStorage_.SetColumnWidth(0, itemStorageWidth > 0 ? itemStorageWidth : 0);
            itemStorage_.SetColumnWidth(1, 80);
            itemStorage_.ShowScrollBar(SB_HORZ, FALSE);
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
            view_ = {};
            view_.screen = Screen::Connecting;
            view_.message = L"正在连接并登录…";
            Refresh();
            session_.Start(m_hWnd, SessionMessage, address,
                static_cast<std::uint16_t>(port), account, password);
            SecureZeroMemory(password.data(), password.size());
        }
        catch (const std::exception& error)
        {
            view_.screen = Screen::Login;
            view_.message = TextEncoding::ErrorText(error.what());
            Refresh();
        }
    }

    void MainWindow::OnEnter()
    {
        int selected = list_.GetNextItem(-1, LVNI_SELECTED);
        if (view_.screen != Screen::Characters || selected < 0) return;
        session_.SelectCharacter(static_cast<int>(list_.GetItemData(selected)));
        view_.screen = Screen::Entering;
        view_.message = L"正在进入游戏…";
        Refresh();
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
        session_.Stop();
        view_.screen = Screen::Closing;
        view_.message = L"正在退出并返回账号登录…";
        Refresh();
    }

    BOOL MainWindow::PreTranslateMessage(MSG* message)
    {
        if ((message->hwnd == mapPosition_.GetSafeHwnd() || message->hwnd == mapRegion_.GetSafeHwnd()) &&
            message->message == WM_KEYDOWN &&
            (message->wParam == VK_RETURN || message->wParam == VK_ESCAPE))
            return CFrameWnd::PreTranslateMessage(message);
        if (message->message == WM_LBUTTONDOWN && message->hwnd == miniMap_.GetSafeHwnd() &&
            view_.screen == Screen::Game && mainTabs_.GetCurSel() == 1)
        {
            CPoint point(static_cast<short>(LOWORD(message->lParam)), static_cast<short>(HIWORD(message->lParam)));
            int x, y;
            if (miniMap_.GetTarget(point, x, y))
                session_.RequestMove(view_.player.mapId, x, y);
            return TRUE;
        }
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
        session_.Stop();
        bool done = session_.IsDone();
        if (done)
        {
            session_.Join();
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
        view_ = session_.Snapshot(done, stopped);
        if (done) session_.Join();
        if (closeWindow_)
        {
            if (done)
            {
                if (view_.failed)
                    AfxMessageBox(view_.message.c_str(), MB_OK | MB_ICONWARNING);
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

    void MainWindow::OnMainTab(NMHDR*, LRESULT* result)
    {
        OnPetStateClose();
        Refresh();
        if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 1)
            session_.RequestPetStorage(Game::PetStorageAction::List);
        else if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 2)
            session_.RequestItemStorage(Game::ItemStorageAction::List);
        *result = 0;
    }

    void MainWindow::OnTab(NMHDR*, LRESULT* result)
    {
        list_.DeleteAllItems();
        Refresh();
        if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 1)
            session_.RequestPetStorage(Game::PetStorageAction::List);
        else if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0 && tabs_.GetCurSel() == 2)
            session_.RequestItemStorage(Game::ItemStorageAction::List);
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
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 1 || click->iItem < 0)
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

    void MainWindow::OnListRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        int tab = tabs_.GetCurSel();
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 ||
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
        if (food)
        {
            const auto& item = view_.items[slot];
            bool canUse = !view_.itemPending && !view_.petStoragePending && !view_.player.inBattle &&
                !view_.encounterEnabled && !view_.encounterPending && item.useField != 1 && item.target <= 2;
            menu.AppendMenu(MF_STRING | (canUse ? MF_ENABLED : MF_GRAYED), 4, L"使用");
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
        if (command == 4 && food)
        {
            if (session_.RequestFood(slot, itemRevision))
                footer_.SetWindowText(L"正在对当前角色使用食物…");
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

    void MainWindow::ShowItemProperties(CListCtrl& list, int row)
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 2 ||
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
            item.type == 20 ? L"食物" : L"未分类");
        if (item.levelKnown)
        {
            CString level;
            level.Format(L"使用等级：%d\r\n", item.level);
            text += level;
        }
        text += L"\r\n说明：\r\n";
        text += LegacyText(item.description);
        itemProperties_.Show(this, text);
    }

    void MainWindow::ShowPetProperties(CListCtrl& list, int row)
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 1 ||
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
        }
        petProperties_.Show(this, text, L"宠物属性");
    }

    void MainWindow::OnListDoubleClick(NMHDR* header, LRESULT* result)
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

    void MainWindow::OnItemStorageRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 2)
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
            menu.AppendMenu(MF_STRING, 2, L"丢弃");
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
        else if (command == 2 && available)
            footer_.SetWindowText(L"道具丢弃功能尚未接入，当前仅提供 UI");
    }

    void MainWindow::OnPetStorageRightClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        OnPetStateClose();
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 1)
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

    void MainWindow::OnPetState()
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

    void MainWindow::OnMapPosition()
    {
        int selected = mapPosition_.GetCurSel();
        auto positions = mapPositions_.find(positionMap_);
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 1 ||
            positions == mapPositions_.end() || selected <= 0 ||
            selected > static_cast<int>(positions->second.size()))
            return;
        const auto& target = positions->second[selected - 1];
        session_.RequestMove(positionMap_, target.x, target.y);
    }

    void MainWindow::OnMapRegion()
    {
        RefreshMapPositions();
        if (mapPosition_.IsWindowEnabled())
            mapPosition_.SetCurSel(1);
        OnMapPosition();
    }

    void MainWindow::OnPetStateClose()
    {
        editingPet_ = -1;
        if (petState_.GetSafeHwnd())
            petState_.ShowWindow(SW_HIDE);
    }
}
