#include "MapData.h"
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace Client
{
    MapData LoadMap(const std::filesystem::path& directory, int id)
    {
        std::ifstream input(directory / (std::to_wstring(id) + L".MAP"), std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("找不到当前地图的 .MAP 文件");
        auto size = input.tellg();
        input.seekg(0);
        std::uint32_t dimensions[2]{};
        input.read(reinterpret_cast<char*>(dimensions), sizeof(dimensions));
        std::uint64_t cells = static_cast<std::uint64_t>(dimensions[0]) * dimensions[1];
        if (!input || cells == 0 || cells > 16 * 1024 * 1024 ||
            dimensions[0] > INT_MAX || dimensions[1] > INT_MAX ||
            size != static_cast<std::streamoff>(8 + cells * 2))
            throw std::runtime_error("小地图文件尺寸或长度无效");
        std::vector<unsigned char> colours(static_cast<std::size_t>(cells));
        input.read(reinterpret_cast<char*>(colours.data()), static_cast<std::streamsize>(colours.size()));
        if (!input)
            throw std::runtime_error("小地图颜色数据读取失败");
        auto datPath = directory / (std::to_wstring(id) + L".DAT");
        if (!std::filesystem::exists(datPath))
            throw std::runtime_error("找不到当前地图的 data\\map\\<地图编号>.DAT 文件");
        if (std::filesystem::file_size(datPath) < 8)
            throw std::runtime_error("物件地图 .DAT 长度无效");
        {
            std::ifstream dat(datPath, std::ios::binary | std::ios::ate);
            if (!dat || dat.tellg() != static_cast<std::streamoff>(8 + cells * 6))
                throw std::runtime_error("物件地图 .DAT 长度无效");
            dat.seekg(0);
            std::uint32_t datDimensions[2]{};
            dat.read(reinterpret_cast<char*>(datDimensions), sizeof(datDimensions));
            if (!dat || datDimensions[0] != dimensions[0] || datDimensions[1] != dimensions[1])
                throw std::runtime_error("物件地图 .DAT 尺寸与 .MAP 不一致");
            dat.seekg(static_cast<std::streamoff>(8 + cells * 2));
            std::vector<std::uint16_t> objects(static_cast<std::size_t>(cells));
            dat.read(reinterpret_cast<char*>(objects.data()), static_cast<std::streamsize>(cells * 2));
            if (!dat)
                throw std::runtime_error("物件地图 .DAT 读取失败");
            auto indexPath = directory.parent_path() / L"adrn_15.bin";
            auto colourPath = directory.parent_path() / L"auto.dat";
            struct Shape { unsigned char east = 0, south = 0; bool footprint = false; };
            static std::filesystem::path cachedIndexPath, cachedColourPath;
            static std::vector<Shape> shapes;
            static std::vector<unsigned char> objectColours;
            if (shapes.empty() || cachedIndexPath != indexPath || cachedColourPath != colourPath)
            {
                std::ifstream index(indexPath, std::ios::binary | std::ios::ate);
                std::ifstream colourFile(colourPath, std::ios::binary | std::ios::ate);
                if (!index || !colourFile)
                    throw std::runtime_error("建筑轮廓需要 adrn_15.bin 和 auto.dat");
                auto indexSize = static_cast<std::streamoff>(index.tellg());
                auto colourSize = static_cast<std::streamoff>(colourFile.tellg());
                if (indexSize <= 0 || indexSize % 80 != 0 || indexSize > 80000000 || colourSize < 10 + 65536)
                    throw std::runtime_error("建筑轮廓资源长度无效");
                std::vector<unsigned char> records(static_cast<std::size_t>(indexSize));
                index.seekg(0);
                index.read(reinterpret_cast<char*>(records.data()), static_cast<std::streamsize>(records.size()));
                std::vector<unsigned char> newColours(65536);
                colourFile.seekg(10);
                colourFile.read(reinterpret_cast<char*>(newColours.data()), static_cast<std::streamsize>(newColours.size()));
                if (!index || !colourFile)
                    throw std::runtime_error("建筑轮廓资源读取失败");
                std::vector<Shape> newShapes(65536);
                for (std::size_t offset = 0; offset < records.size(); offset += 80)
                {
                    std::uint32_t mapImage = 0, imageSize = 0;
                    std::uint16_t flags = 0;
                    std::memcpy(&mapImage, &records[offset + 76], sizeof(mapImage));
                    std::memcpy(&imageSize, &records[offset + 8], sizeof(imageSize));
                    std::memcpy(&flags, &records[offset + 30], sizeof(flags));
                    if (mapImage > 0 && mapImage < newShapes.size() && imageSize > 0)
                        newShapes[mapImage] = { records[offset + 28], records[offset + 29], flags % 100 == 0 };
                }
                shapes = std::move(newShapes);
                objectColours = std::move(newColours);
                cachedIndexPath = indexPath;
                cachedColourPath = colourPath;
            }
            int width = static_cast<int>(dimensions[0]), height = static_cast<int>(dimensions[1]);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    auto object = objects[static_cast<std::size_t>(y) * width + x];
                    if (object == 0 || objectColours[object] == 0)
                        continue;
                    const auto& shape = shapes[object];
                    int east = shape.footprint ? shape.east : 1;
                    int south = shape.footprint ? shape.south : 1;
                    for (int dy = 0; dy < south && y - dy >= 0; ++dy)
                        for (int dx = 0; dx < east && x + dx < width; ++dx)
                            colours[static_cast<std::size_t>(y - dy) * width + x + dx] = objectColours[object];
                }
        }
        return { static_cast<int>(dimensions[0]), static_cast<int>(dimensions[1]), std::move(colours) };
    }
}
