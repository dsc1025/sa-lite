#include "ResourceDirectory.h"
#include <windows.h>
#include <vector>

namespace Client
{
    std::filesystem::path LoadResourceDirectory(const std::filesystem::path& configPath)
    {
        std::vector<wchar_t> value(32768);
        GetPrivateProfileStringW(L"Resources", L"Directory", L"", value.data(),
            static_cast<DWORD>(value.size()), configPath.c_str());
        std::filesystem::path directory(value.data());
        if (directory.empty())
            return configPath.parent_path() / L"data";
        if (directory.is_relative())
            directory = configPath.parent_path() / directory;
        return directory.lexically_normal();
    }
}
