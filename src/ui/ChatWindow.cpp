#include "ChatWindow.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    BEGIN_MESSAGE_MAP(ChatHistoryEdit, CEdit)
        ON_WM_SETFOCUS()
        ON_WM_LBUTTONUP()
    END_MESSAGE_MAP()

    void ChatHistoryEdit::OnSetFocus(CWnd* previous)
    {
        CEdit::OnSetFocus(previous);
        HideCaret();
    }

    void ChatHistoryEdit::OnLButtonUp(UINT flags, CPoint point)
    {
        CEdit::OnLButtonUp(flags, point);
        if (input && input->IsWindowEnabled()) input->SetFocus();
    }

    BEGIN_MESSAGE_MAP(ChatWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
        ON_WM_CLOSE()
        ON_WM_SETFOCUS()
        ON_WM_CTLCOLOR()
    END_MESSAGE_MAP()

    void ChatWindow::Show(CWnd* owner, GameSession* session, const SessionView& view)
    {
        session_ = session;
        if (!GetSafeHwnd())
        {
            if (!background_.GetSafeHandle()) background_.CreateSolidBrush(RGB(12, 12, 12));
            if (!CreateEx(0, AfxRegisterWndClass(CS_DBLCLKS, LoadCursor(nullptr, IDC_ARROW),
                static_cast<HBRUSH>(background_.GetSafeHandle())), L"聊天终端 - Enter 发送 / Esc 隐藏",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CRect(0, 0, 760, 460), owner, 0))
                return;
            CenterWindow(owner);
        }
        Update(view);
        ShowWindow(IsIconic() ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow();
        if (input_.IsWindowEnabled()) input_.SetFocus();
        else history_.SetFocus();
    }

    int ChatWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1) return -1;
        if (!font_.GetSafeHandle()) font_.CreatePointFont(100, L"Consolas");
        CRect empty(0, 0, 0, 0);
        if (!history_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL, empty, this, 1) ||
            !input_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                empty, this, 2) ||
            !prompt_.Create(L"> ", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, empty, this, 3))
            return -1;
        history_.SetLimitText(1024 * 1024);
        history_.input = &input_;
        input_.SetLimitText(200);
        history_.SetFont(&font_);
        input_.SetFont(&font_);
        input_.SetMargins(0, 0);
        prompt_.SetFont(&font_);
        displayed_.Empty();
        return 0;
    }

    void ChatWindow::Update(const SessionView& view)
    {
        if (!GetSafeHwnd()) return;
        CString text;
        for (const auto& message : view.chatMessages)
        {
            CString line(TextEncoding::LegacyText(message).c_str());
            line.Replace(L"\r\n", L"\n");
            line.Replace(L"\n", L"\r\n");
            text += line + L"\r\n";
        }
        if (text != displayed_)
        {
            displayed_ = text;
            history_.SetWindowText(text);
            history_.SetSel(-1, -1);
            history_.SendMessage(EM_SCROLLCARET);
        }
        input_.EnableWindow(view.screen == Screen::Game);
        CString prompt = view.characterName.empty() ? CString(L"> ") : CString(view.characterName.c_str()) + L" > ";
        CString previous;
        prompt_.GetWindowText(previous);
        if (prompt != previous)
        {
            prompt_.SetWindowText(prompt);
            Layout();
        }
    }

    void ChatWindow::OnSize(UINT type, int width, int height)
    {
        CWnd::OnSize(type, width, height);
        Layout();
    }

    void ChatWindow::Layout()
    {
        if (!prompt_.GetSafeHwnd()) return;
        CRect area;
        GetClientRect(&area);
        int innerWidth = area.Width() > 16 ? area.Width() - 16 : 0;
        CString prompt;
        prompt_.GetWindowText(prompt);
        CClientDC dc(this);
        CFont* previous = dc.SelectObject(&font_);
        CSize size = dc.GetTextExtent(prompt);
        dc.SelectObject(previous);
        int promptWidth = size.cx < innerWidth ? size.cx : innerWidth;
        int top = area.Height() > size.cy + 16 ? area.Height() - size.cy - 8 : 8;
        history_.MoveWindow(8, 8, innerWidth, top > 16 ? top - 16 : 0);
        prompt_.MoveWindow(8, top, promptWidth, size.cy);
        input_.MoveWindow(8 + promptWidth, top, innerWidth - promptWidth, size.cy);
    }

    HBRUSH ChatWindow::OnCtlColor(CDC* dc, CWnd* window, UINT type)
    {
        CWnd::OnCtlColor(dc, window, type);
        dc->SetTextColor(RGB(220, 220, 220));
        dc->SetBkColor(RGB(12, 12, 12));
        return static_cast<HBRUSH>(background_.GetSafeHandle());
    }

    void ChatWindow::OnClose()
    {
        ShowWindow(SW_HIDE);
    }

    void ChatWindow::OnSetFocus(CWnd* previous)
    {
        CWnd::OnSetFocus(previous);
        if (input_.GetSafeHwnd() && input_.IsWindowEnabled()) input_.SetFocus();
    }

    BOOL ChatWindow::PreTranslateMessage(MSG* message)
    {
        if (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
        {
            if (message->message == WM_KEYDOWN && message->wParam == 'C' &&
                (GetKeyState(VK_CONTROL) & 0x8000) && message->hwnd == input_.GetSafeHwnd())
            {
                int inputStart, inputEnd, historyStart, historyEnd;
                input_.GetSel(inputStart, inputEnd);
                history_.GetSel(historyStart, historyEnd);
                if (inputStart == inputEnd && historyStart != historyEnd)
                {
                    history_.Copy();
                    return TRUE;
                }
            }
            if (message->wParam == VK_ESCAPE || (message->wParam == VK_F10 &&
                (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)))
            {
                OnClose();
                return TRUE;
            }
            if (message->wParam == VK_RETURN)
            {
                if (message->hwnd == input_.GetSafeHwnd() && input_.IsWindowEnabled())
                {
                    CString text;
                    input_.GetWindowText(text);
                    if (text.IsEmpty()) return TRUE;
                    try
                    {
                        BOOL substituted = FALSE;
                        int length = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, text.GetString(),
                            text.GetLength(), nullptr, 0, nullptr, &substituted);
                        if (length <= 0 || length > 200 || substituted)
                            throw std::invalid_argument("内容超出 200 字节或包含服务端不支持的字符");
                        std::string encoded(length, '\0');
                        if (!WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, text.GetString(),
                            text.GetLength(), encoded.data(), length, nullptr, &substituted) || substituted)
                            throw std::invalid_argument("聊天内容无法转换为服务端编码");
                        if (!session_ || !session_->RequestChat(encoded))
                            throw std::runtime_error("发送失败，请确认已进入游戏，稍后重试");
                        input_.SetWindowText(L"");
                    }
                    catch (const std::exception& error)
                    {
                        MessageBox(TextEncoding::ErrorText(error.what()).c_str(), L"聊天", MB_OK | MB_ICONWARNING);
                    }
                }
                return TRUE;
            }
            if (message->wParam == VK_TAB && IsDialogMessage(message)) return TRUE;
        }
        return CWnd::PreTranslateMessage(message);
    }
}
