#include "MainWindow.h"
#include "ChatWindow.h"
#include "BattleControlIds.h"
#include "MiniMap.h"
#include "CreateCharacterWindow.h"
#include "RolePage.h"
#include "AuctionPage.h"
#include "UiText.h"
#include "../map/MapIndex.h"
#include "../text/TextEncoding.h"
#include <stdexcept>
#include <algorithm>

namespace Client
{
    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, UpdateButton, CharacterList = 104, MainTabs = 120, ShopTabs, MapPositionButton, MapRegionButton, ShopRefreshButton = 150, ShopBuyButton, ShopList, ShopQuantity, ShopCurrency };

    BEGIN_MESSAGE_MAP(MainWindow, CFrameWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
        ON_WM_GETMINMAXINFO()
        ON_WM_PAINT()
        ON_WM_CTLCOLOR()
        ON_WM_CLOSE()
        ON_BN_CLICKED(LoginButton, &MainWindow::OnLogin)
        ON_BN_CLICKED(UpdateButton, &MainWindow::OnUpdate)
        ON_BN_CLICKED(EnterButton, &MainWindow::OnEnter)
        ON_BN_CLICKED(105, &MainWindow::OnCreateCharacter)
        ON_BN_CLICKED(ExitButton, &MainWindow::OnExit)
        ON_MESSAGE(SessionMessage, &MainWindow::OnSession)
        ON_MESSAGE(BattleSceneClosedMessage, &MainWindow::OnBattleSceneClosed)
        ON_NOTIFY(TCN_SELCHANGE, ShopTabs, &MainWindow::OnMainTab)
        ON_BN_CLICKED(ShopRefreshButton, &MainWindow::OnShopRefresh)
        ON_BN_CLICKED(ShopBuyButton, &MainWindow::OnShopBuy)
        ON_NOTIFY(NM_DBLCLK, ShopList, &MainWindow::OnShopDetail)
        ON_NOTIFY(TCN_SELCHANGE, MainTabs, &MainWindow::OnMainTab)
        ON_NOTIFY(TCN_SELCHANGE, IDC_BATTLE_TABS, &MainWindow::OnMainTab)
        ON_BN_CLICKED(IDC_BATTLE_START, &MainWindow::OnBattleStart)
        ON_BN_CLICKED(IDC_BATTLE_SETTINGS, &MainWindow::OnBattleSettings)
        ON_BN_CLICKED(IDC_BATTLE_DISPLAY, &MainWindow::OnBattleDisplay)
        ON_BN_CLICKED(IDC_CAPTURE_PET, &MainWindow::OnCapturePet)
        ON_BN_CLICKED(MapPositionButton, &MainWindow::OnMapPosition)
        ON_BN_CLICKED(MapRegionButton, &MainWindow::OnMapRegion)
    END_MESSAGE_MAP()

    int MainWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CFrameWnd::OnCreate(create) == -1)
            return -1;
        try
        {
            mapNodes_ = LoadMapIndex(mapDirectory_, mapNames_);
        }
        catch (const std::exception& error)
        {
            MessageBox(ErrorText(error.what()), L"地图索引加载失败", MB_OK | MB_ICONWARNING);
        }
        font_.CreatePointFont(90, L"Microsoft YaHei UI");
        CRect empty(0, 0, 0, 0);
        summary_.Create(L"", WS_CHILD | WS_VISIBLE, empty, this);
        footer_.Create(L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, empty, this);
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
        createCharacter_.Create(L"创建角色", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, 105);
        exit_.Create(L"退出", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, empty, this, ExitButton);
        update_.Create(L"更新", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, UpdateButton);
        mainTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_VERTICAL | TCS_MULTILINE,
            empty, this, MainTabs);
        const wchar_t* mainCaptions[] = { L"角色", L"地图", L"战斗", L"黑市", L"任务" };
        for (int i = 0; i < 5; ++i)
            mainTabs_.InsertItem(i, mainCaptions[i]);
        mainTabs_.SetPadding(CSize(12, 10));
        developing_.Create(L"开发中", WS_CHILD | SS_CENTER | SS_CENTERIMAGE, empty, this);
        miniMap_->Create(L"", WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY, empty, this);
        mapPosition_.Create(L"选择地点 ▾", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            empty, this, MapPositionButton);
        mapRegion_.Create(L"选择地图 ▾", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            empty, this, MapRegionButton);
        mapRegion_.EnableWindow(!mapNodes_.empty());
        RefreshMapPositions();
        if (!rolePage_->Create(this, &font_))
            return -1;
        CreateBattleControls();
        shopTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, ShopTabs);
        shopTabs_.InsertItem(0, L"道具");
        shopTabs_.InsertItem(1, L"装备");
        shopTabs_.InsertItem(2, L"技能");
        shopTabs_.InsertItem(3, L"竞拍");
        if (!auctionPage_->Create(this, &font_))
            return -1;
        DWORD listStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        shopList_.Create(listStyle, empty, this, ShopList);
        shopList_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        shopList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* shopColumns[] = { L"商品", L"COST", L"P", L"库存", L"等级", L"说明" };
        const int shopWidths[] = { 140, 65, 65, 65, 45, 200 };
        for (int i = 0; i < 6; ++i) shopList_.InsertColumn(i, shopColumns[i], LVCFMT_LEFT, shopWidths[i]);
        shopRefresh_.Create(L"刷新", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, ShopRefreshButton);
        shopBuy_.Create(L"购买", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, ShopBuyButton);
        shopQuantityLabel_.Create(L"数量", WS_CHILD, empty, this);
        shopQuantity_.Create(WS_CHILD | WS_TABSTOP | WS_BORDER | ES_NUMBER, empty, this, ShopQuantity);
        shopQuantity_.SetLimitText(2);
        shopQuantity_.SetWindowText(L"1");
        shopCurrency_.Create(WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, empty, this, ShopCurrency);
        shopCurrency_.AddString(L"COST");
        shopCurrency_.AddString(L"P");
        shopCurrency_.SetCurSel(0);
        list_.Create(listStyle, empty, this, CharacterList);
        list_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        list_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        list_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 70);
        list_.SetBkColor(RGB(255, 255, 255));
        list_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        battleTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        shopTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        mainTabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        if (rolePage_->GetSafeHwnd())
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
        rolePage_->CloseEditor();
        CRect area;
        GetClientRect(&area);
        int width = area.Width(), height = area.Height();
        bool login = view_.screen == Screen::Login || view_.screen == Screen::Connecting;
        summary_.MoveWindow(16, 16, width - 128, 24);
        footer_.MoveWindow(16, height - 30, width - 32, 24);
        int left = (width - 360) / 2;
        constexpr int inputWidth = 272;
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (int i = 0; i < 4; ++i)
        {
            int top = 30 + i * 42;
            labels_[i].MoveWindow(left, top + 4, 80, 24);
            edits[i]->MoveWindow(left + 88, top, inputWidth, 26);
        }
        constexpr int buttonWidth = 80;
        int buttonGap = (inputWidth - buttonWidth * 3) / 2;
        login_.MoveWindow(left + 88, 208, buttonWidth, 30);
        exit_.MoveWindow(login ? left + 88 + buttonWidth + buttonGap : width - 116,
            login ? 208 : 12, login ? buttonWidth : 100, 30);
        update_.MoveWindow(left + 88 + inputWidth - buttonWidth, 208, buttonWidth, 30);
        enter_.MoveWindow(width - 146, height - 76, 130, 30);
        createCharacter_.MoveWindow(16, height - 76, 130, 30);
        mainTabs_.MoveWindow(16, 54, width - 32, height - 92);
        CRect mainPage(16, 54, width - 16, height - 38);
        CRect tabItem;
        if (mainTabs_.GetItemRect(0, &tabItem))
            mainPage.left += tabItem.right;
        mainPage.DeflateRect(6, 6);
        developing_.MoveWindow(mainPage);
        miniMap_->MoveWindow(mainPage);
        int regionLeft = mainPage.left + 8;
        mapRegion_.SetWindowPos(&CWnd::wndTop, regionLeft, mainPage.top + 4, 160, 26, SWP_NOACTIVATE);
        mapPosition_.SetWindowPos(&CWnd::wndTop, regionLeft + 168,
            mainPage.top + 4, 160, 26, SWP_NOACTIVATE);
        rolePage_->MoveWindow(mainPage);
        shopTabs_.MoveWindow(mainPage);
        if (mainTabs_.GetCurSel() == 3)
        {
            CRect shopPage = mainPage;
            shopTabs_.AdjustRect(FALSE, &shopPage);
            shopPage.DeflateRect(6, 6);
            auctionPage_->MoveWindow(shopPage);
            shopList_.MoveWindow(shopPage.left, shopPage.top, shopPage.Width(), shopPage.Height() - 38);
            int bottom = shopPage.bottom - 28;
            shopQuantityLabel_.MoveWindow(shopPage.left, bottom + 3, 36, 24);
            shopQuantity_.MoveWindow(shopPage.left + 40, bottom, 50, 26);
            shopCurrency_.MoveWindow(shopPage.left + 100, bottom, 90, 160);
            shopBuy_.MoveWindow(shopPage.left + 200, bottom, 80, 26);
            shopRefresh_.MoveWindow(shopPage.left + 290, bottom, 70, 26);
        }
        LayoutBattle(mainPage);
        list_.MoveWindow(16, 54, width - 32, height - 146);
        if (!login)
        {
            CRect listArea;
            list_.GetClientRect(&listArea);
            int listWidth = listArea.Width() - 70 - GetSystemMetrics(SM_CXVSCROLL);
            list_.SetColumnWidth(0, listWidth > 0 ? listWidth : 0);
            list_.SetColumnWidth(1, 70);
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
        if (UpdateInProgress())
        {
            MessageBox(L"请先完成或关闭更新窗口，再登录游戏。", L"客户端更新", MB_OK | MB_ICONINFORMATION);
            return;
        }
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
            creationOffered_ = false;
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
        if (view_.screen != Screen::Characters || view_.createPending || selected < 0 ||
            (createCharacterWindow_->GetSafeHwnd() && createCharacterWindow_->IsWindowVisible())) return;
        session_.SelectCharacter(static_cast<int>(list_.GetItemData(selected)));
        view_.screen = Screen::Entering;
        view_.message = L"正在进入游戏…";
        Refresh();
    }

    void MainWindow::OnCreateCharacter()
    {
        if (view_.screen != Screen::Characters || view_.createPending || view_.characters.size() >= 2) return;
        createCharacterWindow_->Show(this, &session_);
        createCharacterWindow_->Update(view_);
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
        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN) &&
            message->wParam == VK_F10 && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (GetKeyState(VK_SHIFT) & 0x8000))
        {
            chatWindow_->Show(this, &session_, view_);
            return TRUE;
        }
        if (chatWindow_->GetSafeHwnd() &&
            (message->hwnd == chatWindow_->GetSafeHwnd() || chatWindow_->IsChild(CWnd::FromHandle(message->hwnd))))
            return CFrameWnd::PreTranslateMessage(message);
        if ((message->hwnd == mapPosition_.GetSafeHwnd() || message->hwnd == mapRegion_.GetSafeHwnd()) &&
            message->message == WM_KEYDOWN &&
            (message->wParam == VK_RETURN || message->wParam == VK_ESCAPE))
        {
            if (message->wParam == VK_RETURN)
            {
                if (message->hwnd == mapRegion_.GetSafeHwnd()) OnMapRegion();
                else OnMapPosition();
                return TRUE;
            }
            return CFrameWnd::PreTranslateMessage(message);
        }
        if (message->message == WM_LBUTTONDOWN && message->hwnd == miniMap_->GetSafeHwnd() &&
            view_.screen == Screen::Game && mainTabs_.GetCurSel() == 1)
        {
            CPoint point(static_cast<short>(LOWORD(message->lParam)), static_cast<short>(HIWORD(message->lParam)));
            int x, y;
            if (miniMap_->GetTarget(point, x, y))
                session_.RequestMove(view_.player.mapId, x, y);
            return TRUE;
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
                return TRUE;
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
        bool firstCharacters = !creationOffered_;
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
        if (view_.screen == Screen::Login) creationOffered_ = false;
        if (view_.screen == Screen::Characters && firstCharacters)
        {
            creationOffered_ = true;
            if (view_.characters.empty()) OnCreateCharacter();
        }
        return 0;
    }

    void MainWindow::OnMainTab(NMHDR*, LRESULT* result)
    {
        rolePage_->CloseEditor();
        Refresh();
        Layout();
        if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 0)
            rolePage_->Activate();
        if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 3 &&
            shopTabs_.GetCurSel() < 3 && !view_.shopPending && view_.shopCategory != shopTabs_.GetCurSel())
            OnShopRefresh();
        if (view_.screen == Screen::Game && mainTabs_.GetCurSel() == 3 && shopTabs_.GetCurSel() == 3)
            auctionPage_->Activate();
        *result = 0;
    }

    void MainWindow::OnShopRefresh()
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 3 || shopTabs_.GetCurSel() >= 3) return;
        if (!session_.RequestShop(shopTabs_.GetCurSel()))
            SetTextIfChanged(footer_, L"请停止遇敌并等待当前操作结束后刷新黑市");
    }

    void MainWindow::OnShopDetail(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 3 ||
            view_.shopPending || view_.shopCategory != shopTabs_.GetCurSel() || click->iItem < 0)
            return;
        int id = static_cast<int>(shopList_.GetItemData(click->iItem));
        auto found = std::find_if(view_.shopItems.begin(), view_.shopItems.end(),
            [id](const Game::ShopItem& item) { return item.id == id; });
        if (found == view_.shopItems.end()) return;
        CString text;
        text.Format(L"商品：%s\r\n类型：%d\r\n等级：%d\r\nCOST：%s\r\nP：%s\r\n库存：%s\r\n\r\n说明：\r\n%s",
            LegacyText(found->name).GetString(), found->type, found->level,
            found->cost < 0 ? L"不可用" : Number(found->cost).GetString(),
            found->pearl < 0 ? L"不可用" : Number(found->pearl).GetString(),
            found->stock < 0 ? L"不限量" : Number(found->stock).GetString(),
            LegacyText(found->description).GetString());
        shopProperties_.Show(this, text, L"商品详情");
    }

    void MainWindow::OnShopBuy()
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 3) return;
        int row = shopList_.GetNextItem(-1, LVNI_SELECTED);
        if (row < 0 || view_.shopPending || view_.shopCategory != shopTabs_.GetCurSel()) return;
        int id = static_cast<int>(shopList_.GetItemData(row));
        auto found = std::find_if(view_.shopItems.begin(), view_.shopItems.end(),
            [id](const Game::ShopItem& item) { return item.id == id; });
        if (found == view_.shopItems.end()) return;
        CString quantityText;
        shopQuantity_.GetWindowText(quantityText);
        int quantity = _wtoi(quantityText);
        int currency = shopCurrency_.GetCurSel();
        int price = currency == 1 ? found->pearl : found->cost;
        if (quantity < 1 || quantity > 15 || price < 0 || (found->stock >= 0 && quantity > found->stock))
        {
            MessageBox(L"请检查支付方式、购买数量及库存。", L"无法购买", MB_OK | MB_ICONWARNING);
            return;
        }
        CString question;
        question.Format(L"购买 %s × %d，支付 %lld %s？", LegacyText(found->name).GetString(),
            quantity, static_cast<long long>(price) * quantity, currency == 1 ? L"P" : L"COST");
        if (MessageBox(question, L"购买商品", MB_YESNO | MB_ICONQUESTION) == IDYES)
            if (!session_.RequestShop(shopTabs_.GetCurSel(), id, quantity, price, currency))
                SetTextIfChanged(footer_, L"请停止遇敌并等待当前操作结束后购买");
    }
}
