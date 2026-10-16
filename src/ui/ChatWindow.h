#pragma once

#include <afxwin.h>
#include "../session/GameSession.h"
#include "../session/SessionView.h"

namespace Client
{
    class ChatHistoryEdit : public CEdit
    {
    public:
        CEdit* input = nullptr;

    protected:
        afx_msg void OnSetFocus(CWnd* previous);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        DECLARE_MESSAGE_MAP()
    };

    class ChatWindow : public CWnd
    {
    public:
        void Show(CWnd* owner, GameSession* session, const SessionView& view);
        void Update(const SessionView& view);

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnClose();
        afx_msg void OnSetFocus(CWnd* previous);
        afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();

        ChatHistoryEdit history_;
        CEdit input_;
        CStatic prompt_;
        CFont font_;
        CBrush background_;
        GameSession* session_ = nullptr;
        CString displayed_;
    };
}
