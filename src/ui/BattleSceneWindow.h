#pragma once

#include <afxwin.h>
#include "../game/Battle.h"
#include <array>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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
        void PrepareBackground(const SessionView& view);
        void PrepareAnimationFrames();
        void ApplyBattleState(const Game::BattleState& state);
        struct UnitAnimation
        {
            std::uint32_t frame = 0;
            std::uint64_t started = 0;
        };
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
        std::vector<RGBQUAD> background_;
        Game::BattleState battle_;
        GameSession* session_ = nullptr;
        std::array<UnitAnimation, 20> unitAnimations_;
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
    };

}
