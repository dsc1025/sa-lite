#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Client
{
    struct MapData
    {
        int width = 0, height = 0;
        std::vector<unsigned char> colours;
    };

    std::unordered_map<int, std::wstring> LoadMapNames(std::filesystem::path& mapDirectory);
    MapData LoadMap(const std::filesystem::path& directory, int id);
}
