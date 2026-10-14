#pragma once

#include <afxwin.h>

#include "../game/Battle.h"
#include "../game/GameTypes.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <deque>

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
        void PrepareForEncounter();
        void Update(CWnd* owner, const SessionView& view, GameSession& session);

    protected:
        afx_msg void OnPaint();
        afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
        afx_msg void OnClose();
        afx_msg void OnTimer(UINT_PTR timer);
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnDestroy();
        DECLARE_MESSAGE_MAP()

    private:
        void InspectImage(std::uint32_t image, int direction, int action = 3, std::uint32_t frame = 0);
        static std::uint64_t SpriteKey(std::uint32_t image, int direction, int action, std::uint32_t frame);
        void PrepareAnimationFrames();
        void ApplyBattleState(const Game::BattleState& state);
        void StartEvent(const Game::BattleEvent& event);
        std::uint32_t EffectFrame(std::uint32_t image, int direction);
        struct UnitAnimation
        {
            int action = 3;
            std::uint32_t frame = 0;
            std::uint64_t started = 0;
            std::wstring label;
        };
        void UpdateDebug(const SessionView& view);
        struct PictureIndex
        {
            std::uint32_t offset = 0, size = 0, width = 0, height = 0;
            int x = 0, y = 0;
        };
        struct StaticSprite
        {
            int width = 0, height = 0, x = 0, y = 0;
            std::uint32_t frames = 1, duration = 500;
            std::uint64_t lastUsed = 0;
            std::vector<RGBQUAD> pixels;
        };
        CFont font_;
        CDC bufferDC_;
        CBitmap bufferBitmap_;
        HGDIOBJ previousBitmap_ = nullptr;
        CSize bufferSize_{ 0, 0 };
        std::vector<RGBQUAD> canvas_;
        Game::BattleState battle_;
        Game::BattleState latestBattle_;
        GameSession* session_ = nullptr;
        std::array<UnitAnimation, 20> unitAnimations_;
        std::deque<Game::BattlePlayback> playback_;
        std::uint64_t seenPlayback_ = 0, eventEnd_ = 0;
        std::size_t eventIndex_ = 0;
        int playbackRound_ = -1;
        std::uint64_t playbackRevision_ = 0;
        Game::BattleEvent currentEvent_;
        bool active_ = false, dismissed_ = false;
        std::filesystem::path resourceDirectory_;
        std::unordered_map<std::uint32_t, std::array<std::uint32_t, 2>> animations_;
        std::unordered_map<std::uint32_t, PictureIndex> pictures_;
        std::unordered_map<std::uint64_t, bool> inspected_;
        std::unordered_map<std::uint64_t, StaticSprite> staticSprites_;
        std::array<RGBQUAD, 256> palette_{};
        bool paletteReady_ = false;
        std::size_t cachedBytes_ = 0;
        std::uint64_t cacheSerial_ = 0;
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
