#pragma once

#include <string>

namespace Game
{
    struct HealingSettings
    {
        bool quickBattle = true;
        bool useItems = false, useMagic = false;
        int itemPercent = 80, magicPercent = 60, magicSlot = -1;
        std::string magicName;
    };
}
