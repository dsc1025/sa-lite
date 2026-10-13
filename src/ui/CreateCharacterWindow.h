#pragma once

#include <afxwin.h>
#include "../session/GameSession.h"
#include <random>

namespace Client
{
    class CreateCharacterWindow : public CWnd
    {
    public:
        void Show(CWnd* owner, GameSession* session);
        void Update(const SessionView& view);

    protected:
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnRandom();
        afx_msg void OnSubmit();
        afx_msg void OnClose();
        BOOL PreTranslateMessage(MSG* message) override;
        DECLARE_MESSAGE_MAP()

    private:
        CFont font_;
        CStatic label_, result_, status_;
        CEdit name_;
        CButton random_, submit_, cancel_;
        GameSession* session_ = nullptr;
        LoginClient::NewCharacter character_;
        std::mt19937 randomEngine_{ std::random_device{}() };
        bool pending_ = false;
        unsigned int revision_ = 0;
    };
}
