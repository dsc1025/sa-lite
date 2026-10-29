#pragma once

#include <cstdint>

namespace Game
{
    struct PlayerStatus
    {
        bool received = false;
        int hp = 0;
        int maxHp = 0;
        int mp = 0;
        int maxMp = 0;
        int level = 0;
        int remainingPoints = 0;
        bool remainingPointsReceived = false;
        int gold = 0;
        int mapId = -1;
        int mapX = -1, mapY = -1;
        bool inBattle = false;
        int vital = 0, strength = 0, toughness = 0, dexterity = 0;
        std::int64_t experience = 0, nextExperience = 0;
        int attack = 0, defence = 0, quick = 0, charm = 0, luck = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int title = 0, duelPoints = 0, rebirth = 0, perception = 0;
        int ridePet = -1, battlePet = -1;
        std::uint32_t standbyPets = 0;
        bool standbyReceived = false;
        bool battleReceived = false;
    };
}
