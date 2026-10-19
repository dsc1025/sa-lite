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
