#include "MapData.h"
#include <windows.h>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace Client
{
    std::unordered_map<int, std::wstring> LoadMapNames(std::filesystem::path& mapDirectory,
        std::unordered_map<int, std::vector<MapPosition>>& positions)
    {
        std::vector<wchar_t> executable(32768);
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
            throw std::runtime_error("无法确定地图索引路径");
        auto directory = std::filesystem::path(std::wstring(executable.data(), length)).parent_path();
        const auto relative = std::filesystem::path(L"data") / L"map" / L"map_index.csv";
        auto path = directory / relative;
        while (!std::filesystem::exists(path) && directory != directory.root_path())
        {
            directory = directory.parent_path();
            path = directory / relative;
        }
        if (!std::filesystem::exists(path))
            path = std::filesystem::current_path() / relative;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("无法打开 data\\map\\map_index.csv");
        mapDirectory = path.parent_path();
        if (input.peek() == 0xef)
        {
            char bom[3];
            input.read(bom, 3);
            if (input.gcount() != 3 || static_cast<unsigned char>(bom[1]) != 0xbb ||
                static_cast<unsigned char>(bom[2]) != 0xbf)
                throw std::runtime_error("地图索引的 UTF-8 BOM 无效");
        }
        auto readRow = [&](std::vector<std::string>& fields)
        {
            fields.clear();
            std::string field;
            bool quoted = false, closed = false, started = false;
            char c;
            while (input.get(c))
            {
                started = true;
                if (quoted)
                {
                    if (c == '"')
                    {
                        if (input.peek() == '"')
                        {
                            input.get(c);
                            field += c;
                        }
                        else
                        {
                            quoted = false;
                            closed = true;
                        }
                    }
                    else
                        field += c;
                }
                else if (c == ',' || c == '\r' || c == '\n')
                {
                    fields.push_back(field);
                    field.clear();
                    closed = false;
                    if (c != ',')
                    {
                        if (c == '\r' && input.peek() == '\n')
                            input.get(c);
                        return true;
                    }
                }
                else if (c == '"' && field.empty() && !closed)
                    quoted = true;
                else if (closed || c == '"')
                    throw std::runtime_error("地图索引的 CSV 引号格式无效");
                else
                    field += c;
            }
            if (input.bad() || quoted)
                throw std::runtime_error("地图索引读取失败或 CSV 字段未闭合");
            if (started)
                fields.push_back(field);
            return started;
        };
        std::vector<std::string> header, row;
        if (!readRow(header))
            throw std::runtime_error("地图索引为空");
        std::size_t idColumn = header.size(), nameColumn = header.size(), posColumn = header.size();
        for (std::size_t i = 0; i < header.size(); ++i)
        {
            if (header[i] == "map_id") idColumn = i;
            if (header[i] == "map_name") nameColumn = i;
            if (header[i] == "pos") posColumn = i;
        }
        if (idColumn == header.size() || nameColumn == header.size())
            throw std::runtime_error("地图索引缺少 map_id 或 map_name 列");
        std::unordered_map<int, std::wstring> names;
        std::unordered_map<int, std::vector<MapPosition>> loadedPositions;
        while (readRow(row))
        {
            if (row.size() == 1 && row[0].empty())
                continue;
            if (row.size() != header.size())
                throw std::runtime_error("地图索引的列数不一致");
            const auto& idText = row[idColumn];
            if (idText.empty() || idText.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("地图索引的 map_id 无效");
            int id = std::stoi(idText);
            const auto& name = row[nameColumn];
            if (name.empty())
                continue;
            int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                static_cast<int>(name.size()), nullptr, 0);
            if (count == 0)
                throw std::runtime_error("地图名称不是有效的 UTF-8 文本");
            std::wstring text(count, L'\0');
            wchar_t* buffer = text.data();
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                static_cast<int>(name.size()), buffer, count);
            names.emplace(id, text);
            if (posColumn != header.size() && !row[posColumn].empty())
            {
                const auto& value = row[posColumn];
                std::size_t start = 0;
                while (start < value.size())
                {
                    auto end = value.find('|', start);
                    auto entry = value.substr(start, end == std::string::npos ? end : end - start);
                    auto first = entry.find(','), second = entry.find(',', first == std::string::npos ? first : first + 1);
                    if (first == std::string::npos || second == std::string::npos)
                        throw std::runtime_error("地图索引的 pos 格式无效");
                    auto xText = entry.substr(0, first), yText = entry.substr(first + 1, second - first - 1);
                    if (xText.empty() || yText.empty() || xText.find_first_not_of("0123456789") != std::string::npos ||
                        yText.find_first_not_of("0123456789") != std::string::npos)
                        throw std::runtime_error("地图索引的 pos 坐标无效");
                    auto label = entry.substr(second + 1);
                    int labelCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(),
                        static_cast<int>(label.size()), nullptr, 0);
                    if (labelCount == 0)
                        throw std::runtime_error("地图索引的 pos 名称无效");
                    std::wstring labelText(labelCount, L'\0');
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(),
                        static_cast<int>(label.size()), labelText.data(), labelCount);
                    loadedPositions[id].push_back({ std::stoi(xText), std::stoi(yText), std::move(labelText) });
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
        }
        positions = std::move(loadedPositions);
        return names;
    }

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
        auto original = directory.parent_path().parent_path().parent_path() / L"SA2.5";
        auto datPath = directory / (std::to_wstring(id) + L".DAT");
        if (!std::filesystem::exists(datPath))
            datPath = original / L"map" / (std::to_wstring(id) + L".DAT");
        if (std::filesystem::exists(datPath) && std::filesystem::file_size(datPath) >= 8)
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
            auto resource = [&](const wchar_t* filename)
            {
                auto path = directory.parent_path() / filename;
                return std::filesystem::exists(path) ? path : original / L"data" / filename;
            };
            auto indexPath = resource(L"adrn_15.bin");
            auto colourPath = resource(L"auto.dat");
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
