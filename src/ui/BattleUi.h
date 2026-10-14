#pragma once

#include <afxwin.h>

#include "../game/Battle.h"
#include "../game/GameTypes.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#define IDC_BATTLE_TABS 124
#define IDC_BATTLE_UPPER 126
#define IDC_BATTLE_LOWER 127
#define IDC_BATTLE_INFO 130
#define IDC_BATTLE_START 131
#define IDC_BATTLE_STOP 132
#define IDC_BATTLE_SETTINGS 133

namespace Client
{
    class GameSession;
    struct SessionView;

    class BattleSceneWindow : public CWnd
    {
    public:
        void LoadResources(const CString& configPath);
        void Update(CWnd* owner, const SessionView& view, GameSession& session);

    protected:
        afx_msg void OnPaint();
        afx_msg void OnClose();
        DECLARE_MESSAGE_MAP()

    private:
        void InspectImage(std::uint32_t image);
        CFont font_;
        CEdit debug_;
        Game::BattleState battle_;
        GameSession* session_ = nullptr;
        bool active_ = false, dismissed_ = false;
        std::filesystem::path resourceDirectory_;
        std::unordered_map<std::uint32_t, std::uint32_t> animations_;
        std::unordered_map<std::uint32_t, std::array<std::uint32_t, 2>> pictures_;
        std::unordered_map<std::uint32_t, bool> inspected_;
        std::ifstream sprites_, images_;
        std::uint64_t spriteSize_ = 0, imageSize_ = 0;
        std::vector<std::wstring> resourceDebug_;
        CString debugText_;
    };

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
        CButton items_, magic_, quickBattle_;
        CComboBox skills_;
        CEdit itemHp_, magicHp_;
        CStatic labels_[4];
        GameSession* session_ = nullptr;
        bool useItems_ = false, useMagic_ = false, quickBattleEnabled_ = true;
        int itemPercent_ = 80, magicPercent_ = 60, magicSlot_ = -1;
        std::string magicName_;
        std::vector<Game::MagicStatus> currentMagic_;
    };

    class BattleGroup : public CButton
    {
    public:
        void Update(const Game::BattleState& battle, int offset, bool received);

    protected:
        void DrawItem(LPDRAWITEMSTRUCT item) override;

    private:
        std::array<Game::BattleMember, 10> members_;
        int offset_ = 0;
        bool received_ = false;
    };
}
