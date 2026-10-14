#pragma once

#include <filesystem>
#include <vector>

namespace Client
{
    struct MapData
    {
        int width = 0, height = 0;
        std::vector<unsigned char> colours;
    };

    MapData LoadMap(const std::filesystem::path& directory, int id);
}
