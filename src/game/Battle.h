#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#define BATTLE_DELAY_MS 200
#define SIMPLE_BATTLE_DELAY_MS 200
#define BATTLE_PLAYER_MENU_OFF 0x02
#define BATTLE_PET_MENU_OFF 0x08
#define BATTLE_MEMBER_DEAD 0x02
#define BATTLE_MEMBER_HIDDEN 0x200
#define BATTLE_HIT_DEATH 0x01
#define BATTLE_HIT_GUARD 0x08
#define BATTLE_HIT_DODGE 0x20
#define BATTLE_HIT_ABSORB 0x800
#define BATTLE_HIT_VANISH 0x1000

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
        std::uint64_t membersRevision = 0;
        std::array<BattleMember, 20> members;
    };

    struct BattleEvent
    {
        enum class Kind { Attack, Damage, Heal, SetHp, Guard, Escape, Cast };
        Kind kind = Kind::Attack;
        int actor = -1, target = -1, guardian = -1;
        std::uint32_t flags = 0, amount = 0, petAmount = 0;
        std::uint32_t effect = 0, targetEffect = 0;
        std::vector<int> targets;
    };

    struct BattlePlayback
    {
        std::uint64_t serial = 0;
        BattleState initial;
        std::vector<BattleEvent> events;
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
