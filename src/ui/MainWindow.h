#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include "MiniMap.h"
#include "BattleUi.h"
#include "RolePage.h"
#include "../map/MapData.h"
#include "../session/GameSession.h"
#include <unordered_map>

namespace Client
{
    class MainWindow : public CFrameWnd
    {
    public:
        MainWindow() : rolePage_(view_, session_, footer_)
        {
            background_.CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            white_.CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        }

        ~MainWindow() override
        {
            session_.Stop();
            session_.Join();
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
        afx_msg void OnMapPosition();
        afx_msg void OnMapRegion();
        afx_msg void OnBattleStart();
        afx_msg void OnBattleStop();
        afx_msg void OnBattleSettings();
        DECLARE_MESSAGE_MAP()

    private:
        void CreateBattleControls();
        void LayoutBattle(const CRect& mainPage);
        void RefreshBattle(bool visible);
        void Layout();
        void Refresh();
        void RefreshMapPositions();
        void LoadLogin();
        void SaveLogin();

        CFont font_;
        CBrush background_, white_;
        CStatic summary_, footer_, labels_[4], developing_;
        CString configPath_;
        std::unordered_map<int, std::wstring> mapNames_;
        std::unordered_map<int, std::vector<MapPosition>> mapPositions_;
        CComboBox mapPosition_, mapRegion_;
        int positionMap_ = -2;
        std::filesystem::path mapDirectory_;
        MiniMap miniMap_;
        CEdit address_, port_, account_, password_;
        CButton login_, enter_, exit_;
        CTabCtrl mainTabs_, shopTabs_, battleTabs_;
        BattleGroup battleSides_[2];
        CButton battleStart_, battleStop_, battleSettings_;
        BattleSettingsWindow battleSettingsWindow_;
        CStatic battleRound_, battleInfo_, battleExperience_;
        CListCtrl list_;
        SessionView view_;
        GameSession session_;
        RolePage rolePage_;
        bool closeWindow_ = false;
        bool expanded_ = false;
    };
}
