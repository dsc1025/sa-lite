#pragma once

#include <cstdint>
#include <string>

namespace Game
{
    struct ShopItem
    {
        int id = -1, cost = -1, pearl = -1, stock = -1, image = 0, level = 0, type = -1;
        std::string name, description;
    };

    inline bool IsEquipmentType(int type)
    {
        return (type >= 0 && type <= 15 && type != 5) || (type >= 17 && type <= 19);
    }

    inline int EquipmentSlot(int type)
    {
        if (!IsEquipmentType(type)) return -1;
        if (type == 6) return 0;
        if (type == 7) return 1;
        if (type >= 8 && type <= 15) return 3;
        return 2;
    }

    enum class ItemStorageAction { List, Put, Get };
    struct ItemStatus
    {
        bool present = false;
        std::string name, description, uniqueCode;
        int level = 0, type = -1, useField = 0, target = 0;
        bool levelKnown = false;
        std::uint32_t revision = 0;
    };
}
