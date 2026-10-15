#pragma once

#include <afxwin.h>
#include "../game/MagicStatus.h"
#include <string>
#include <vector>

namespace Client
{
    class GameSession;

    class BattleSettingsWindow : public CWnd
    {
    public:
        void LoadSettings(const CString& path, GameSession& session);
        void Show(CWnd* owner, const std::vector<Game::MagicStatus>& magic, GameSession& session);

    protected:
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSave();
        afx_msg void OnClose();
        DECLARE_MESSAGE_MAP()

    private:
        void ApplySettings();
        CString configPath_;
        bool updating_ = false;
        CFont font_;
        CButton items_, magic_;
        CComboBox skills_;
        CEdit itemHp_, magicHp_;
        CStatic labels_[4];
        GameSession* session_ = nullptr;
        bool useItems_ = false, useMagic_ = false;
        int itemPercent_ = 80, magicPercent_ = 60, magicSlot_ = -1;
        std::string magicName_;
        std::vector<Game::MagicStatus> currentMagic_;
    };

}
