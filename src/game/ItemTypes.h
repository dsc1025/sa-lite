#pragma once

#include <cstdint>
#include <string>

namespace Game
{
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
