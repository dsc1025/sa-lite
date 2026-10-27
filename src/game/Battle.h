#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Game
{
    struct BattleMember
    {
        bool present = false;
        std::string name;
        std::uint32_t image = 0;
        int level = 0, hp = 0, maxHp = 0;
        int sourceInstanceId = -1;
        std::string rideName;
        int rideLevel = 0, rideHp = 0, rideMaxHp = 0;
        std::uint32_t flags = 0;
    };

    struct BattleState
    {
        int playerSlot = -1;
        int round = -1;
        bool received = false;
        bool captureCounted = false, captureLevelOneCounted = false;
        std::uint32_t fieldAttribute = 0;
        std::uint32_t menuFlags = 0;
        std::uint32_t readyMask = 0;
        bool commandsPending = false;
        bool roundReceived = false;
        bool endPending = false;
        int sentRound = -1;
        int playerAction = 3, petAction = 3;
        std::uint64_t nextAction = 0;
        std::array<BattleMember, 20> members;
    };

    struct BattleResult
    {
        bool received = false;
        std::uint32_t experience = 0;
        std::uint64_t petExperience = 0;
    };
}
