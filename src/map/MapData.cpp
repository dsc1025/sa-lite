#include "MapData.h"
#include <windows.h>
#include <climits>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace Client
{
    std::unordered_map<int, std::wstring> LoadMapNames(std::filesystem::path& mapDirectory)
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
        std::size_t idColumn = header.size(), nameColumn = header.size();
        for (std::size_t i = 0; i < header.size(); ++i)
        {
            if (header[i] == "map_id") idColumn = i;
            if (header[i] == "map_name") nameColumn = i;
        }
        if (idColumn == header.size() || nameColumn == header.size())
            throw std::runtime_error("地图索引缺少 map_id 或 map_name 列");
        std::unordered_map<int, std::wstring> names;
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
        }
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
        return { static_cast<int>(dimensions[0]), static_cast<int>(dimensions[1]), std::move(colours) };
    }
}
