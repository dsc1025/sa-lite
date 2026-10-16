#include "PropertiesWindow.h"
#include "UiText.h"

namespace Client
{
    BEGIN_MESSAGE_MAP(ItemPropertiesWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
        ON_WM_SHOWWINDOW()
        ON_WM_TIMER()
        ON_WM_DESTROY()
    END_MESSAGE_MAP()

    void ItemPropertiesWindow::Show(CWnd* owner, const CString& text, const wchar_t* title, int petImage)
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
        KillTimer(1);
        pendingPetImage_ = -1;
        if (appearance_.GetSafeHwnd())
            appearance_.DestroyWindow();
        SetWindowText(title);
        text_.SetWindowText(text);
        text_.SetSel(0, 0);
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
        text_.SetFocus();
        if (petImage >= 0)
        {
            pendingPetImage_ = petImage;
            SetTimer(1, 200, nullptr);
        }
    }

    BEGIN_MESSAGE_MAP(PetAppearanceWindow, CWnd)
        ON_WM_PAINT()
        ON_WM_TIMER()
        ON_WM_DESTROY()
        ON_WM_ERASEBKGND()
    END_MESSAGE_MAP()

    void PetAppearanceWindow::Show(CWnd* owner, int image)
    {
        if (!loaded_)
        {
            std::vector<wchar_t> path(32768);
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length > 0 && length < path.size())
            {
                auto config = std::filesystem::path(path.data()).parent_path() / L"config.ini";
                renderer_.LoadResources(CString(config.c_str()));
                loaded_ = true;
            }
        }
        if (!GetSafeHwnd() && !CreateEx(WS_EX_DLGMODALFRAME,
            AfxRegisterWndClass(0, LoadCursor(nullptr, IDC_ARROW)), L"宠物形象",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CRect(0, 0, 280, 280), owner, 0))
            return;
        image_ = image;
        started_ = GetTickCount64();
        CRect rect;
        owner->GetWindowRect(&rect);
        SetWindowPos(&CWnd::wndTop, rect.left + 300, rect.top + 50, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(1, 33, nullptr);
        Invalidate(FALSE);
    }

    void PetAppearanceWindow::OnPaint()
    {
        CPaintDC dc(this);
        CRect area;
        GetClientRect(&area);
        if (area.IsRectEmpty())
            return;
        CDC buffer;
        CBitmap bitmap;
        if (!buffer.CreateCompatibleDC(&dc) ||
            !bitmap.CreateCompatibleBitmap(&dc, area.Width(), area.Height()))
        {
            renderer_.DrawPet(dc, area, image_, GetTickCount64() - started_);
            return;
        }
        CBitmap* previous = buffer.SelectObject(&bitmap);
        renderer_.DrawPet(buffer, area, image_, GetTickCount64() - started_);
        dc.BitBlt(0, 0, area.Width(), area.Height(), &buffer, 0, 0, SRCCOPY);
        buffer.SelectObject(previous);
    }

    void PetAppearanceWindow::OnTimer(UINT_PTR timer)
    {
        if (timer == 1) Invalidate(FALSE);
        else CWnd::OnTimer(timer);
    }

    void PetAppearanceWindow::OnDestroy()
    {
        KillTimer(1);
        CWnd::OnDestroy();
    }

    BOOL PetAppearanceWindow::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void ItemPropertiesWindow::OnShowWindow(BOOL show, UINT status)
    {
        CWnd::OnShowWindow(show, status);
        if (!show)
        {
            KillTimer(1);
            pendingPetImage_ = -1;
            if (appearance_.GetSafeHwnd())
                appearance_.DestroyWindow();
        }
    }

    void ItemPropertiesWindow::OnTimer(UINT_PTR timer)
    {
        if (timer != 1)
        {
            CWnd::OnTimer(timer);
            return;
        }
        KillTimer(1);
        const int image = pendingPetImage_;
        pendingPetImage_ = -1;
        if (image >= 0 && IsWindowVisible())
            appearance_.Show(this, image);
    }

    void ItemPropertiesWindow::OnDestroy()
    {
        KillTimer(1);
        pendingPetImage_ = -1;
        if (appearance_.GetSafeHwnd())
            appearance_.DestroyWindow();
        CWnd::OnDestroy();
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
        if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE)
        {
            SendMessage(WM_CLOSE);
            return TRUE;
        }
        if (message->message == WM_KEYDOWN && message->wParam == VK_RETURN)
            return TRUE;
        return CWnd::PreTranslateMessage(message);
    }
}
