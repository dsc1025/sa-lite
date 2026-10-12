#include "MainWindow.h"
#include "UiText.h"
#include "../map/MapData.h"
#include "../text/TextEncoding.h"
#include <algorithm>
#include <stdexcept>

namespace Client
{
    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, Tabs, List, Detail, PetStateCombo, MainTabs = 120, ShopTabs };

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
        try
        {
            mapNames_ = LoadMapNames(mapDirectory_);
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
        const wchar_t* mainCaptions[] = { L"角色", L"地图", L"商店", L"任务" };
        for (int i = 0; i < 4; ++i)
            mainTabs_.InsertItem(i, mainCaptions[i]);
        mainTabs_.SetPadding(CSize(12, 10));
        developing_.Create(L"开发中", WS_CHILD | SS_CENTER | SS_CENTERIMAGE, empty, this);
        miniMap_.Create(L"", WS_CHILD | SS_OWNERDRAW | SS_NOTIFY, empty, this);
        tabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, Tabs);
        tabs_.InsertItem(0, L"属性");
        tabs_.InsertItem(1, L"宠物");
        tabs_.InsertItem(2, L"道具");
        shopTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, ShopTabs);
        shopTabs_.InsertItem(0, L"道具");
        shopTabs_.InsertItem(1, L"装备");
        shopTabs_.InsertItem(2, L"技能");
        DWORD listStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        list_.Create(listStyle, empty, this, List);
        detail_.Create(listStyle, empty, this, Detail);
        petState_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, PetStateCombo);
        for (const wchar_t* state : PetStateNames)
            petState_.AddString(state);
        tabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        shopTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        mainTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        tabs_.MoveWindow(mainPage);
        shopTabs_.MoveWindow(mainPage);
        CRect page = mainPage;
        tabs_.AdjustRect(FALSE, &page);
        page.DeflateRect(6, 6);
        if (mainTabs_.GetCurSel() == 2)
        {
            CRect shopPage = mainPage;
            shopTabs_.AdjustRect(FALSE, &shopPage);
            shopPage.DeflateRect(6, 6);
            developing_.MoveWindow(shopPage);
        }
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
        *result = 0;
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
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 0 || tabs_.GetCurSel() != 1 ||
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
        if (!session_.RequestPetState(slot, state))
            return;
        footer_.SetWindowText(L"正在设置宠物状态…");
    }

    void MainWindow::OnPetStateClose()
    {
        editingPet_ = -1;
        if (petState_.GetSafeHwnd())
            petState_.ShowWindow(SW_HIDE);
    }
}
