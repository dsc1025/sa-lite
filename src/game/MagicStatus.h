#pragma once

#include <string>

namespace Game
{
    struct MagicStatus
    {
        bool received = false, present = false;
        std::string name, description;
        int mp = 0, useField = 0, target = 0;
    };
}
