#include "CreateCharacterWindow.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    BEGIN_MESSAGE_MAP(CreateCharacterWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_CLOSE()
        ON_BN_CLICKED(2, &CreateCharacterWindow::OnRandom)
        ON_BN_CLICKED(3, &CreateCharacterWindow::OnSubmit)
        ON_BN_CLICKED(4, &CreateCharacterWindow::OnClose)
    END_MESSAGE_MAP()

    void CreateCharacterWindow::Show(CWnd* owner, GameSession* session)
    {
        session_ = session;
        if (!GetSafeHwnd())
        {
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(0,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)),
                L"创建角色", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                CRect(0, 0, 460, 340), owner, 0))
                return;
            CenterWindow(owner);
        }
        if (!IsWindowVisible())
        {
            name_.SetWindowText(L"");
            status_.SetWindowText(L"填写名字，其余参数已随机生成");
            OnRandom();
        }
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
        name_.SetFocus();
    }

    int CreateCharacterWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1) return -1;
        font_.CreatePointFont(90, L"Microsoft YaHei UI");
        label_.Create(L"角色名字", WS_CHILD | WS_VISIBLE, CRect(20, 22, 100, 46), this);
        name_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            CRect(106, 18, 420, 46), this, 1);
        name_.SetLimitText(31);
        result_.Create(L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, CRect(20, 64, 420, 210), this);
        status_.Create(L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, CRect(20, 212, 420, 248), this);
        random_.Create(L"重新随机", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            CRect(20, 260, 130, 290), this, 2);
        submit_.Create(L"创建角色", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            CRect(190, 260, 300, 290), this, 3);
        cancel_.Create(L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            CRect(310, 260, 420, 290), this, 4);
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
            child->SetFont(&font_);
        return 0;
    }

    void CreateCharacterWindow::OnRandom()
    {
        if (pending_) return;
        auto pick = [&](int maximum) { return std::uniform_int_distribution<int>(0, maximum)(randomEngine_); };
        character_.model = pick(11);
        character_.color = pick(3);
        character_.hometown = pick(3);
        for (int& point : character_.points) point = 0;
        for (int i = 0; i < 20; ++i) ++character_.points[pick(3)];
        for (int& element : character_.elements) element = 0;
        int first = pick(3);
        int share = pick(10);
        character_.elements[first] = share;
        character_.elements[(first + 1) % 4] = 10 - share;
        const wchar_t* villages[] = { L"萨姆吉尔", L"玛丽娜丝", L"加加", L"卡鲁它那" };
        CString text;
        text.Format(L"形象：%d    颜色：%d\r\n\r\n体力：%d    腕力：%d    耐力：%d    速度：%d\r\n\r\n地：%d    水：%d    火：%d    风：%d\r\n\r\n出生村庄：%s",
            character_.model + 1, character_.color + 1, character_.points[0], character_.points[1],
            character_.points[2], character_.points[3], character_.elements[0], character_.elements[1],
            character_.elements[2], character_.elements[3], villages[character_.hometown]);
        result_.SetWindowText(text);
    }

    void CreateCharacterWindow::OnSubmit()
    {
        if (pending_ || !session_) return;
        try
        {
            CString name;
            name_.GetWindowText(name);
            BOOL substituted = FALSE;
            int length = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, name.GetString(), name.GetLength(),
                nullptr, 0, nullptr, &substituted);
            if (length <= 0 || substituted) throw std::invalid_argument("名字无法转换为服务端编码");
            character_.name.resize(length);
            if (!WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, name.GetString(), name.GetLength(),
                character_.name.data(), length, nullptr, &substituted) || substituted)
                throw std::invalid_argument("名字包含服务端编码不支持的字符");
            Game::ValidateNewCharacter(character_);
            if (!session_->RequestCreateCharacter(character_))
                throw std::runtime_error("当前不能创建角色，请检查连接及角色槽位");
            pending_ = true;
            name_.EnableWindow(FALSE);
            random_.EnableWindow(FALSE);
            submit_.EnableWindow(FALSE);
            cancel_.EnableWindow(FALSE);
            status_.SetWindowText(L"正在创建角色…");
        }
        catch (const std::exception& error)
        {
            status_.SetWindowText(TextEncoding::ErrorText(error.what()).c_str());
        }
    }

    void CreateCharacterWindow::Update(const SessionView& view)
    {
        if (!GetSafeHwnd() || (!IsWindowVisible() && !pending_)) return;
        bool completed = pending_ && view.createRevision != revision_;
        revision_ = view.createRevision;
        if (completed || view.screen != Screen::Characters)
        {
            pending_ = false;
            name_.EnableWindow(TRUE);
            random_.EnableWindow(TRUE);
            submit_.EnableWindow(TRUE);
            cancel_.EnableWindow(TRUE);
            status_.SetWindowText(view.message.c_str());
            if (view.screen != Screen::Characters || !view.createdCharacter.empty()) ShowWindow(SW_HIDE);
        }
    }

    void CreateCharacterWindow::OnClose()
    {
        if (!pending_) ShowWindow(SW_HIDE);
    }

    BOOL CreateCharacterWindow::PreTranslateMessage(MSG* message)
    {
        if (message->message == WM_KEYDOWN)
        {
            if (message->wParam == VK_RETURN) { OnSubmit(); return TRUE; }
            if (message->wParam == VK_ESCAPE) { OnClose(); return TRUE; }
            if (message->wParam == VK_TAB && IsDialogMessage(message)) return TRUE;
        }
        return CWnd::PreTranslateMessage(message);
    }
}
