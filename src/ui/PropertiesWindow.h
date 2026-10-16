#pragma once

#include <afxwin.h>
#include "BattleSceneWindow.h"

namespace Client
{
    class PetAppearanceWindow : public CWnd
    {
    public:
        void Show(CWnd* owner, int image);

    protected:
        afx_msg void OnPaint();
        afx_msg void OnTimer(UINT_PTR timer);
        afx_msg void OnDestroy();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        DECLARE_MESSAGE_MAP()

    private:
        BattleSceneWindow renderer_;
        bool loaded_ = false;
        int image_ = 0;
        ULONGLONG started_ = 0;
    };

    class ItemPropertiesWindow : public CWnd
    {
    public:
        void Show(CWnd* owner, const CString& text, const wchar_t* title = L"道具属性", int petImage = -1);

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnShowWindow(BOOL show, UINT status);
        afx_msg void OnTimer(UINT_PTR timer);
        afx_msg void OnDestroy();
        DECLARE_MESSAGE_MAP()

    private:
        PetAppearanceWindow appearance_;
        int pendingPetImage_ = -1;
        CFont font_;
        CEdit text_;
    };
}
