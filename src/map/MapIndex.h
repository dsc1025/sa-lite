#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Client
{
    struct MapPosition
    {
        int x = 0, y = 0;
        std::wstring name;
    };

    struct MapNode
    {
        int mapId = -1;
        std::wstring displayName;
        std::vector<MapPosition> positions;
        std::vector<MapNode> children;
    };

    std::vector<MapNode> LoadMapIndex(std::filesystem::path& mapDirectory,
        std::unordered_map<int, std::wstring>& names);
}
