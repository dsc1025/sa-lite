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

    struct PetLv1Location
    {
        int mapId = -1, x = -1, y = -1, x2 = -1, y2 = -1;
        std::wstring mapName;
    };

    struct PetLv1Entry
    {
        std::wstring name;
        std::vector<PetLv1Location> locations;
    };

    std::vector<PetLv1Entry> LoadPetLv1Index(const std::filesystem::path& mapDirectory);

    std::vector<MapNode> LoadMapIndex(std::filesystem::path& mapDirectory,
        std::unordered_map<int, std::wstring>& names);
}
