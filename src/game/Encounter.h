#pragma once

#include <cstdint>

namespace Game
{
    struct EncounterState
    {
        bool enabled = false;
        bool stopPending = false;
        int map = -1, x = -1, y = -1;
        std::uint64_t nextRequest = 0;
    };
}
