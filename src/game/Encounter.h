#pragma once

#include <cstdint>

namespace Game
{
    struct EncounterState
    {
        bool enabled = false;
        bool stopPending = false;
        bool requestPending = false;
        bool battleStarted = false;
        int requestResult = -1;
        int map = -1;
        std::uint64_t requestDeadline = 0;
        std::uint64_t nextRequest = 0;
    };
}
