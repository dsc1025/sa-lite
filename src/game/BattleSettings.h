#pragma once

#include <string>

namespace Game
{
    struct HealingSettings
    {
        bool useItems = false, useMagic = false;
        int itemPercent = 80, magicPercent = 60, magicSlot = -1;
        std::string magicName;
    };

    struct CaptureSettings
    {
        bool flee = true;
        int capturePercent = 40, magicPercent = 60, magicSlot = -1;
        std::string magicName;
    };
}
