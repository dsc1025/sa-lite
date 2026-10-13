#pragma once

#include <afxwin.h>

namespace Client
{
    class ItemPropertiesWindow : public CWnd
    {
    public:
        void Show(CWnd* owner, const CString& text, const wchar_t* title = L"道具属性");

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSize(UINT type, int width, int height);
        DECLARE_MESSAGE_MAP()

    private:
        CFont font_;
        CEdit text_;
    };
}
