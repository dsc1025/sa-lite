#pragma once

#include <array>
#include <string>
#include <cstdint>

namespace Game
{
    struct HealingSettings
    {
        bool useItems = false, useMagic = false;
        int itemPercent = 80, magicPercent = 60, magicSlot = -1;
        std::string magicName;
    };

    struct CaptureStatistics
    {
        std::uint64_t encounters = 0, levelOneEncounters = 0, capturedLevelOne = 0;
    };

    struct CaptureSettings
    {
        bool flee = true;
        int sourceInstanceId = -1;
        std::array<int, 5> minimums{ -1, -1, -1, -1, -1 };
        int capturePercent = 40, magicPercent = 60, magicSlot = -1;
        std::string magicName;
    };
}
