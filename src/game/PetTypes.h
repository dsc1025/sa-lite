#pragma once

#include <string>
#include <vector>

namespace Game
{
    enum class PetState { Standby, Battle, Ride };
    enum class PetStorageAction { List, Put, Get };
    struct StoredPet
    {
        bool present = false;
        std::string name, nickname, uniqueCode;
        int level = 0;
    };
    struct PetSkillStatus
    {
        bool present = false;
        std::string name, description;
        int id = 0, useField = 0, target = 0;
    };
    struct PetStatus
    {
        bool received = false, present = false;
        std::string name, nickname, uniqueCode;
        int image = 0, hp = 0, maxHp = 0, mp = 0, maxMp = 0;
        int experience = 0, nextExperience = 0, level = 0;
        int attack = 0, defence = 0, quick = 0, loyalty = 0;
        int earth = 0, water = 0, fire = 0, wind = 0;
        int skillSlots = 0, canRename = 0, perception = 0, rebirth = 0;
        bool skillsReceived = false;
        std::vector<PetSkillStatus> skills;
    };
}
