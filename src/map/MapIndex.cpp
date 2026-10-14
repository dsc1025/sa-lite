#include "MapIndex.h"
#include "ResourceDirectory.h"
#include <windows.h>
#include <climits>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace Client
{
    namespace
    {
        class MapIndexReader
        {
        public:
            explicit MapIndexReader(const std::wstring& text) : text_(text) {}

            template<class Read>
            void Object(Read read)
            {
                Require(L'{');
                if (Take(L'}')) return;
                do
                {
                    auto key = String();
                    Require(L':');
                    read(key);
                } while (Take(L','));
                Require(L'}');
            }

            template<class Read>
            void Array(Read read)
            {
                Require(L'[');
                if (Take(L']')) return;
                do { read(); } while (Take(L','));
                Require(L']');
            }

            std::wstring String()
            {
                Require(L'"');
                std::wstring result;
                while (offset_ < text_.size())
                {
                    wchar_t c = text_[offset_++];
                    if (c == L'"') return result;
                    if (c < 0x20) Fail();
                    if (c != L'\\')
                    {
                        result += c;
                        continue;
                    }
                    if (offset_ == text_.size()) Fail();
                    switch (text_[offset_++])
                    {
                    case L'"': result += L'"'; break;
                    case L'\\': result += L'\\'; break;
                    case L'/': result += L'/'; break;
                    case L'b': result += L'\b'; break;
                    case L'f': result += L'\f'; break;
                    case L'n': result += L'\n'; break;
                    case L'r': result += L'\r'; break;
                    case L't': result += L'\t'; break;
                    case L'u':
                    {
                        wchar_t value = Unicode();
                        if (value >= 0xd800 && value <= 0xdbff)
                        {
                            if (offset_ + 2 > text_.size() || text_[offset_] != L'\\' ||
                                text_[offset_ + 1] != L'u') Fail();
                            offset_ += 2;
                            wchar_t low = Unicode();
                            if (low < 0xdc00 || low > 0xdfff) Fail();
                            result += value;
                            result += low;
                        }
                        else
                        {
                            if (value >= 0xdc00 && value <= 0xdfff) Fail();
                            result += value;
                        }
                        break;
                    }
                    default: Fail();
                    }
                }
                Fail();
            }

            std::wstring Number()
            {
                Space();
                std::size_t start = offset_;
                if (offset_ < text_.size() && text_[offset_] == L'-') ++offset_;
                if (offset_ < text_.size() && text_[offset_] == L'0') ++offset_;
                else Digits();
                if (offset_ < text_.size() && text_[offset_] == L'.')
                {
                    ++offset_;
                    Digits();
                }
                if (offset_ < text_.size() && (text_[offset_] == L'e' || text_[offset_] == L'E'))
                {
                    ++offset_;
                    if (offset_ < text_.size() && (text_[offset_] == L'+' || text_[offset_] == L'-'))
                        ++offset_;
                    Digits();
                }
                return text_.substr(start, offset_ - start);
            }

            void Skip()
            {
                Space();
                if (offset_ == text_.size()) Fail();
                switch (text_[offset_])
                {
                case L'{': Object([&](const std::wstring&) { Skip(); }); break;
                case L'[': Array([&] { Skip(); }); break;
                case L'"': String(); break;
                case L't': Literal(L"true"); break;
                case L'f': Literal(L"false"); break;
                case L'n': Literal(L"null"); break;
                default: Number(); break;
                }
            }

            void Finish()
            {
                Space();
                if (offset_ != text_.size()) Fail();
            }

        private:
            [[noreturn]] void Fail() const
            {
                throw std::runtime_error("地图索引 JSON 格式无效");
            }

            void Space()
            {
                while (offset_ < text_.size() && (text_[offset_] == L' ' || text_[offset_] == L'\t' ||
                    text_[offset_] == L'\r' || text_[offset_] == L'\n')) ++offset_;
            }

            bool Take(wchar_t value)
            {
                Space();
                if (offset_ == text_.size() || text_[offset_] != value) return false;
                ++offset_;
                return true;
            }

            void Require(wchar_t value)
            {
                if (!Take(value)) Fail();
            }

            void Digits()
            {
                std::size_t start = offset_;
                while (offset_ < text_.size() && text_[offset_] >= L'0' && text_[offset_] <= L'9')
                    ++offset_;
                if (start == offset_) Fail();
            }

            wchar_t Unicode()
            {
                unsigned int value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (offset_ == text_.size()) Fail();
                    wchar_t c = text_[offset_++];
                    value *= 16;
                    if (c >= L'0' && c <= L'9') value += c - L'0';
                    else if (c >= L'a' && c <= L'f') value += c - L'a' + 10;
                    else if (c >= L'A' && c <= L'F') value += c - L'A' + 10;
                    else Fail();
                }
                return static_cast<wchar_t>(value);
            }

            void Literal(const wchar_t* value)
            {
                while (*value)
                {
                    if (offset_ == text_.size() || text_[offset_++] != *value++) Fail();
                }
            }

            const std::wstring& text_;
            std::size_t offset_ = 0;
        };

        int MapNumber(const std::wstring& value)
        {
            if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                throw std::runtime_error("地图编号或坐标必须是非负整数");
            return std::stoi(value);
        }

        std::vector<MapPosition> ReadPositions(const std::wstring& value)
        {
            std::vector<MapPosition> positions;
            std::size_t start = 0;
            while (start < value.size())
            {
                auto end = value.find(L'|', start);
                auto entry = value.substr(start, end == std::wstring::npos ? end : end - start);
                auto first = entry.find(L','), second = entry.find(L',', first == std::wstring::npos ? first : first + 1);
                if (first == std::wstring::npos || second == std::wstring::npos || second + 1 == entry.size())
                    throw std::runtime_error("地图索引的 pos 格式无效");
                int x = MapNumber(entry.substr(0, first));
                int y = MapNumber(entry.substr(first + 1, second - first - 1));
                positions.push_back({ x, y, entry.substr(second + 1) });
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
            return positions;
        }

        MapNode ReadNode(MapIndexReader& reader)
        {
            MapNode node;
            std::wstring name, positions;
            reader.Object([&](const std::wstring& key)
            {
                if (key == L"display_name") node.displayName = reader.String();
                else if (key == L"name") name = reader.String();
                else if (key == L"map_id") node.mapId = MapNumber(reader.Number());
                else if (key == L"pos") positions = reader.String();
                else if (key == L"maps" || key == L"children")
                    reader.Array([&] { node.children.push_back(ReadNode(reader)); });
                else reader.Skip();
            });
            if (node.displayName.empty() && node.mapId < 0)
                node.displayName = std::move(name);
            if (node.displayName.empty())
                throw std::runtime_error("地图缺少 display_name 或区域缺少 name");
            if (!positions.empty() && node.mapId < 0)
                throw std::runtime_error("含 pos 的地图缺少 map_id");
            node.positions = ReadPositions(positions);
            return node;
        }

        void CollectNames(const std::vector<MapNode>& nodes, std::unordered_map<int, std::wstring>& names)
        {
            for (const auto& node : nodes)
            {
                if (node.mapId >= 0) names.emplace(node.mapId, node.displayName);
                CollectNames(node.children, names);
            }
        }
    }

    std::vector<MapNode> LoadMapIndex(std::filesystem::path& mapDirectory,
        std::unordered_map<int, std::wstring>& names)
    {
        std::vector<wchar_t> executable(32768);
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
            throw std::runtime_error("无法确定地图索引路径");
        auto directory = std::filesystem::path(std::wstring(executable.data(), length)).parent_path();
        auto path = LoadResourceDirectory(directory / L"config.ini") / L"map" / L"map_index.json";
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("无法打开地图索引：" + path.u8string());
        mapDirectory = path.parent_path();
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (input.bad() || bytes.empty() || bytes.size() > INT_MAX)
            throw std::runtime_error("地图索引读取失败或文件长度无效");
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0);
        if (count == 0)
            throw std::runtime_error("地图索引不是有效的 UTF-8 文本");
        std::wstring text(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), text.data(), count);
        if (!text.empty() && text.front() == 0xfeff) text.erase(0, 1);
        MapIndexReader reader(text);
        std::vector<MapNode> nodes;
        bool hasRegions = false;
        reader.Object([&](const std::wstring& key)
        {
            if (key == L"regions")
            {
                hasRegions = true;
                reader.Array([&] { nodes.push_back(ReadNode(reader)); });
            }
            else reader.Skip();
        });
        reader.Finish();
        if (!hasRegions)
            throw std::runtime_error("地图索引缺少 regions");
        std::unordered_map<int, std::wstring> loadedNames;
        CollectNames(nodes, loadedNames);
        names = std::move(loadedNames);
        return nodes;
    }
}
