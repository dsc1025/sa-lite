#include "PropertiesWindow.h"
#include "UiText.h"

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
}
