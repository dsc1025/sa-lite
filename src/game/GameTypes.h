#pragma once

#include <cstdint>
#include <string>

namespace Game
{
    enum class LoginResult { Accepted, Rejected };
    enum class PetState { Standby, Battle, Ride };
    struct Character
    {
        std::string name;
        int level = -1;
    };
    struct PlayerStatus
    {
        bool received = false;
        int hp = 0;
        int maxHp = 0;
        int mp = 0;
        int maxMp = 0;
        int level = 0;
        int gold = 0;
        int mapId = -1;
        int mapX = -1, mapY = -1;
        bool inBattle = false;
        int vital = 0, strength = 0, toughness = 0, dexterity = 0;
        int experience = 0, nextExperience = 0;
        int attack = 0, defence = 0, quick = 0, charm = 0, luck = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int title = 0, duelPoints = 0, rebirth = 0, perception = 0;
        int ridePet = -1, battlePet = -1;
        std::uint32_t standbyPets = 0;
        bool standbyReceived = false;
        bool battleReceived = false;
    };
    struct PetStatus
    {
        bool received = false, present = false;
        std::string name, nickname;
        int image = 0, hp = 0, maxHp = 0, mp = 0, maxMp = 0;
        int experience = 0, nextExperience = 0, level = 0;
        int attack = 0, defence = 0, quick = 0, loyalty = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int skillSlots = 0, canRename = 0, perception = 0, rebirth = 0;
    };
    struct ItemStatus
    {
        bool present = false;
        std::string name, description;
        int level = 0;
    };
}
