#pragma once

#include <array>
#include <cstdint>
#include <string>

#define BATTLE_DELAY_MS 200
#define BATTLE_PLAYER_MENU_OFF 0x02
#define BATTLE_PET_MENU_OFF 0x08
#define BATTLE_MEMBER_DEAD 0x02
#define BATTLE_MEMBER_HIDDEN 0x200

namespace Game
{
    struct BattleMember
    {
        bool present = false;
        std::string name;
        std::uint32_t image = 0;
        int level = 0, hp = 0, maxHp = 0;
        std::string rideName;
        int rideLevel = 0, rideHp = 0, rideMaxHp = 0;
        std::uint32_t flags = 0;
    };

    struct BattleState
    {
        int playerSlot = -1;
        int round = -1;
        std::uint32_t fieldAttribute = 0;
        bool received = false;
        std::uint32_t menuFlags = 0;
        std::uint32_t readyMask = 0;
        bool commandsPending = false;
        bool roundReceived = false;
        bool endPending = false;
        int sentRound = -1;
        std::uint64_t nextAction = 0;
        std::array<BattleMember, 20> members;
    };

    struct BattleResult
    {
        bool received = false;
        std::uint32_t experience = 0;
        std::uint64_t totalExperience = 0;
        std::wstring message;
    };

    struct EncounterState
    {
        bool enabled = false;
        bool stopPending = false;
        int map = -1, x = -1, y = -1;
        std::uint64_t nextRequest = 0;
    };
}
