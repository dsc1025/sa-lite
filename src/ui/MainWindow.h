#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include "../map/MapIndex.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Client
{
    class MiniMap;
    class ChatWindow;
    class CreateCharacterWindow;
    class BattleSettingsWindow;
    class CaptureSettingsWindow;
    class BattleSceneWindow;
    class RolePage;
    class BattleGroup;

    class MainWindow : public CFrameWnd
    {
    public:
        MainWindow();
        ~MainWindow() override;

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg int OnCreate(LPCREATESTRUCT create);
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
        afx_msg void OnPaint();
        afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);
        afx_msg void OnLogin();
        afx_msg void OnEnter();
        afx_msg void OnCreateCharacter();
        afx_msg void OnExit();
        afx_msg void OnClose();
        afx_msg LRESULT OnSession(WPARAM, LPARAM);
        afx_msg void OnMainTab(NMHDR*, LRESULT* result);
        afx_msg void OnMapPosition();
        afx_msg void OnMapRegion();
        afx_msg void OnBattleStart();
        afx_msg void OnBattleSettings();
        afx_msg void OnBattleDisplay();
        afx_msg LRESULT OnBattleSceneClosed(WPARAM, LPARAM);
        afx_msg void OnCapturePet();
        DECLARE_MESSAGE_MAP()

    private:
        void SetBattleDisplay(bool show);
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
        std::vector<MapNode> mapNodes_;
        CButton mapPosition_, mapRegion_;
        const MapNode* selectedMap_ = nullptr;
        std::filesystem::path mapDirectory_;
        std::unique_ptr<MiniMap> miniMap_;
        std::unique_ptr<ChatWindow> chatWindow_;
        CEdit address_, port_, account_, password_;
        CButton login_, enter_, exit_;
        CButton createCharacter_;
        std::unique_ptr<CreateCharacterWindow> createCharacterWindow_;
        bool creationOffered_ = false;
        unsigned int selectedCreationRevision_ = 0;
        CTabCtrl mainTabs_, shopTabs_, battleTabs_;
        std::unique_ptr<BattleGroup> battleSides_[2];
        CButton battleStart_, battleSettings_, battleDisplay_;
        bool showBattle_ = false;
        std::unique_ptr<BattleSettingsWindow> battleSettingsWindow_;
        std::unique_ptr<CaptureSettingsWindow> captureSettingsWindow_;
        std::unique_ptr<BattleSceneWindow> battleSceneWindow_;
        CStatic battleRound_, battleExperience_;
        CButton capturePet_;
        CStatic captureDestination_, captureStatistics_, captureFilterLabels_[5];
        CEdit captureMinimums_[5];
        std::vector<PetLv1Entry> capturePets_;
        int captureSourceInstanceId_ = -1;
        CListCtrl list_;
        SessionView view_;
        GameSession session_;
        std::unique_ptr<RolePage> rolePage_;
        bool closeWindow_ = false;
        bool expanded_ = false;
        int displayedScreen_ = -1;
        int displayedTab_ = -1;
    };
}
