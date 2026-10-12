#include <afxwin.h>
#include <afxcmn.h>
#include <uxtheme.h>
#include "LoginClient.h"
#include <algorithm>
#include <climits>
#include <condition_variable>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "UxTheme.lib")

namespace
{
    constexpr UINT SessionMessage = WM_APP + 1;
    enum ControlId { LoginButton = 100, EnterButton, ExitButton, Tabs, List, Detail, PetStateCombo, MainTabs = 120, ShopTabs };
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

    std::unordered_map<int, CString> LoadMapNames(std::filesystem::path& mapDirectory)
    {
        std::vector<wchar_t> executable(32768);
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
            throw std::runtime_error("无法确定地图索引路径");
        auto directory = std::filesystem::path(std::wstring(executable.data(), length)).parent_path();
        const auto relative = std::filesystem::path(L"data") / L"map" / L"map_index.csv";
        auto path = directory / relative;
        while (!std::filesystem::exists(path) && directory != directory.root_path())
        {
            directory = directory.parent_path();
            path = directory / relative;
        }
        if (!std::filesystem::exists(path))
            path = std::filesystem::current_path() / relative;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("无法打开 data\\map\\map_index.csv");
        mapDirectory = path.parent_path();
        if (input.peek() == 0xef)
        {
            char bom[3];
            input.read(bom, 3);
            if (input.gcount() != 3 || static_cast<unsigned char>(bom[1]) != 0xbb ||
                static_cast<unsigned char>(bom[2]) != 0xbf)
                throw std::runtime_error("地图索引的 UTF-8 BOM 无效");
        }
        auto readRow = [&](std::vector<std::string>& fields)
        {
            fields.clear();
            std::string field;
            bool quoted = false, closed = false, started = false;
            char c;
            while (input.get(c))
            {
                started = true;
                if (quoted)
                {
                    if (c == '"')
                    {
                        if (input.peek() == '"')
                        {
                            input.get(c);
                            field += c;
                        }
                        else
                        {
                            quoted = false;
                            closed = true;
                        }
                    }
                    else
                        field += c;
                }
                else if (c == ',' || c == '\r' || c == '\n')
                {
                    fields.push_back(field);
                    field.clear();
                    closed = false;
                    if (c != ',')
                    {
                        if (c == '\r' && input.peek() == '\n')
                            input.get(c);
                        return true;
                    }
                }
                else if (c == '"' && field.empty() && !closed)
                    quoted = true;
                else if (closed || c == '"')
                    throw std::runtime_error("地图索引的 CSV 引号格式无效");
                else
                    field += c;
            }
            if (input.bad() || quoted)
                throw std::runtime_error("地图索引读取失败或 CSV 字段未闭合");
            if (started)
                fields.push_back(field);
            return started;
        };
        std::vector<std::string> header, row;
        if (!readRow(header))
            throw std::runtime_error("地图索引为空");
        std::size_t idColumn = header.size(), nameColumn = header.size();
        for (std::size_t i = 0; i < header.size(); ++i)
        {
            if (header[i] == "map_id") idColumn = i;
            if (header[i] == "map_name") nameColumn = i;
        }
        if (idColumn == header.size() || nameColumn == header.size())
            throw std::runtime_error("地图索引缺少 map_id 或 map_name 列");
        std::unordered_map<int, CString> names;
        while (readRow(row))
        {
            if (row.size() == 1 && row[0].empty())
                continue;
            if (row.size() != header.size())
                throw std::runtime_error("地图索引的列数不一致");
            const auto& idText = row[idColumn];
            if (idText.empty() || idText.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("地图索引的 map_id 无效");
            int id = std::stoi(idText);
            const auto& name = row[nameColumn];
            if (name.empty())
                continue;
            int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                static_cast<int>(name.size()), nullptr, 0);
            if (count == 0)
                throw std::runtime_error("地图名称不是有效的 UTF-8 文本");
            CString text;
            wchar_t* buffer = text.GetBuffer(count);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                static_cast<int>(name.size()), buffer, count);
            text.ReleaseBuffer(count);
            names.emplace(id, text);
        }
        return names;
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

    class MiniMap : public CStatic
    {
    public:
        void Update(const std::filesystem::path& directory, int id, int x, int y, const CString& name)
        {
            x_ = x;
            y_ = y;
            title_.Format(L"地图 %s (%d)   坐标 %d, %d", name.GetString(), id, x, y);
            if (id != id_ || directory != directory_)
            {
                id_ = id;
                directory_ = directory;
                pixels_.clear();
                error_ = L"等待服务器发送地图位置";
                if (id >= 0 && !directory.empty())
                {
                    try { Load(directory, id); }
                    catch (const std::exception& error) { error_ = ErrorText(error.what()); }
                }
            }
            Invalidate(FALSE);
        }

    protected:
        void DrawItem(LPDRAWITEMSTRUCT item) override
        {
            CDC dc;
            dc.Attach(item->hDC);
            CRect area(item->rcItem);
            dc.FillSolidRect(area, RGB(255, 255, 255));
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(30, 30, 30));
            CRect heading = area;
            heading.DeflateRect(8, 6);
            heading.bottom = heading.top + 24;
            dc.DrawText(title_, heading, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            area.DeflateRect(12, 12);
            area.top += 30;
            if (pixels_.empty())
                dc.DrawText(error_, area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            else if (area.Width() > 0 && area.Height() > 0)
            {
                int span = width_ + height_;
                double scale = (std::min)(static_cast<double>(area.Width()) / span,
                    static_cast<double>(area.Height()) * 2 / span);
                int width = (std::max)(1, static_cast<int>(span * scale));
                int height = (std::max)(1, static_cast<int>(span * scale / 2));
                int left = area.left + (area.Width() - width) / 2;
                int top = area.top + (area.Height() - height) / 2;
                std::vector<RGBQUAD> projected(static_cast<std::size_t>(width) * height,
                    RGBQUAD{ 255, 255, 255, 0 });
                for (int py = 0; py < height; ++py)
                {
                    double difference = (py + 0.5) * span / height - width_;
                    for (int px = 0; px < width; ++px)
                    {
                        double sum = (px + 0.5) * span / width;
                        double x = (sum - difference) / 2;
                        double y = (sum + difference) / 2;
                        if (x >= 0 && x < width_ && y >= 0 && y < height_)
                            projected[static_cast<std::size_t>(py) * width + px] =
                                pixels_[static_cast<std::size_t>(static_cast<int>(y)) * width_ + static_cast<int>(x)];
                    }
                }
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = width;
                info.bmiHeader.biHeight = -height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                SetDIBitsToDevice(dc.GetSafeHdc(), left, top, width, height, 0, 0, 0, height,
                    projected.data(), &info, DIB_RGB_COLORS);
                if (x_ >= 0 && x_ < width_ && y_ >= 0 && y_ < height_)
                {
                    int px = left + static_cast<int>((x_ + y_ + 1) * static_cast<double>(width) / span);
                    int py = top + static_cast<int>((y_ - x_ + width_) * static_cast<double>(height) / span);
                    CBrush marker(RGB(255, 40, 40));
                    CPen outline(PS_SOLID, 1, RGB(255, 255, 255));
                    auto oldBrush = dc.SelectObject(&marker);
                    auto oldPen = dc.SelectObject(&outline);
                    dc.Ellipse(px - 4, py - 4, px + 5, py + 5);
                    dc.SelectObject(oldPen);
                    dc.SelectObject(oldBrush);
                }
            }
            dc.Detach();
        }

    private:
        void Load(const std::filesystem::path& directory, int id)
        {
            std::ifstream input(directory / (std::to_wstring(id) + L".MAP"), std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("找不到当前地图的 .MAP 文件");
            auto size = input.tellg();
            input.seekg(0);
            std::uint32_t dimensions[2]{};
            input.read(reinterpret_cast<char*>(dimensions), sizeof(dimensions));
            std::uint64_t cells = static_cast<std::uint64_t>(dimensions[0]) * dimensions[1];
            if (!input || cells == 0 || cells > 16 * 1024 * 1024 ||
                dimensions[0] > INT_MAX || dimensions[1] > INT_MAX ||
                size != static_cast<std::streamoff>(8 + cells * 2))
                throw std::runtime_error("小地图文件尺寸或长度无效");
            std::vector<unsigned char> colours(static_cast<std::size_t>(cells));
            input.read(reinterpret_cast<char*>(colours.data()), static_cast<std::streamsize>(colours.size()));
            if (!input)
                throw std::runtime_error("小地图颜色数据读取失败");
            const std::array<COLORREF, 16> palette = { RGB(73,86,99), RGB(105,125,141),
                RGB(98,143,132), RGB(103,166,150), RGB(72,139,104), RGB(119,168,107),
                RGB(165,190,122), RGB(192,194,139), RGB(80,149,190), RGB(109,176,201),
                RGB(211,195,149), RGB(193,174,139), RGB(164,153,139), RGB(137,143,150),
                RGB(166,175,184), RGB(208,215,217) };
            std::vector<RGBQUAD> pixels(colours.size());
            for (std::size_t i = 0; i < colours.size(); ++i)
            {
                COLORREF colour = palette[colours[i] / 16];
                int shade = (colours[i] % 16 - 8) * 2;
                auto channel = [shade](int value) { return static_cast<BYTE>((std::clamp)(value + shade, 0, 255)); };
                pixels[i] = { channel(GetBValue(colour)), channel(GetGValue(colour)), channel(GetRValue(colour)), 0 };
            }
            width_ = static_cast<int>(dimensions[0]);
            height_ = static_cast<int>(dimensions[1]);
            pixels_ = std::move(pixels);
            error_.Empty();
        }

        int id_ = -2, x_ = -1, y_ = -1, width_ = 0, height_ = 0;
        std::filesystem::path directory_;
        std::vector<RGBQUAD> pixels_;
        CString title_, error_;
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
        afx_msg void OnMainTab(NMHDR*, LRESULT* result);
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
        CStatic summary_, footer_, labels_[4], developing_;
        CString configPath_;
        std::unordered_map<int, CString> mapNames_;
        std::filesystem::path mapDirectory_;
        MiniMap miniMap_;
        CEdit address_, port_, account_, password_;
        CButton login_, enter_, exit_;
        CTabCtrl mainTabs_, tabs_, shopTabs_;
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
        const wchar_t* mainCaptions[] = { L"角色", L"地图", L"任务", L"商店" };
        for (int i = 0; i < 4; ++i)
            mainTabs_.InsertItem(i, mainCaptions[i]);
        mainTabs_.SetPadding(CSize(12, 10));
        developing_.Create(L"开发中", WS_CHILD | SS_CENTER | SS_CENTERIMAGE, empty, this);
        miniMap_.Create(L"", WS_CHILD | SS_OWNERDRAW, empty, this);
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
        if (mainTabs_.GetCurSel() == 3)
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
        if (mainTabs_.GetCurSel() != 0) return;
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
            SetWindowPos(nullptr, 0, 0, 760, 480, SWP_NOMOVE | SWP_NOZORDER);
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
        bool role = game && mainTabs_.GetCurSel() == 0;
        mainTabs_.ShowWindow(game ? SW_SHOW : SW_HIDE);
        bool mapPage = game && mainTabs_.GetCurSel() == 1;
        bool shop = game && mainTabs_.GetCurSel() == 3;
        developing_.ShowWindow(game && !role && !mapPage ? SW_SHOW : SW_HIDE);
        miniMap_.ShowWindow(mapPage ? SW_SHOW : SW_HIDE);
        if (mapPage)
        {
            auto name = mapNames_.find(view_.player.mapId);
            miniMap_.Update(mapDirectory_, view_.player.mapId, view_.player.mapX, view_.player.mapY,
                name != mapNames_.end() ? name->second : Number(view_.player.mapId));
        }
        tabs_.ShowWindow(role ? SW_SHOW : SW_HIDE);
        shopTabs_.ShowWindow(shop ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(characters || (role && tabs_.GetCurSel() != 0) ? SW_SHOW : SW_HIDE);
        detail_.ShowWindow(role ? SW_SHOW : SW_HIDE);
        exit_.EnableWindow(view_.screen != Screen::Closing);
        exit_.SetWindowText(game ? L"退出登录" : L"退出");
        SetWindowText(login ? L"登录" : characters ? L"选择角色" : L"SA25_#1");
        summary_.ShowWindow(login ? SW_HIDE : SW_SHOW);
        footer_.SetWindowText(view_.message);
        CString summary = game ? view_.characterName : CString();
        if (game && view_.player.received)
        {
            CString stats;
            stats.Format(L"   等级 %d   HP %d/%d   MP %d/%d   金币 %d", view_.player.level,
                view_.player.hp, view_.player.maxHp, view_.player.mp, view_.player.maxMp, view_.player.gold);
            summary += stats;
            if (view_.player.mapId >= 0)
            {
                CString map;
                auto name = mapNames_.find(view_.player.mapId);
                if (name != mapNames_.end())
                    map.Format(L"   地图 %s", name->second.GetString());
                else
                    map.Format(L"   地图 %d", view_.player.mapId);
                summary += map;
            }
        }
        summary_.SetWindowText(characters ? CString(L"选择角色后进入游戏") : summary);
        bool petColumns = role && tabs_.GetCurSel() == 1;
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
        else if (role && tabs_.GetCurSel() == 1)
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
        else if (role && tabs_.GetCurSel() == 2)
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
            SetThemeAppProperties(0);
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
