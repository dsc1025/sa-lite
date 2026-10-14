#pragma once

#include <filesystem>

namespace Client
{
    std::filesystem::path LoadResourceDirectory(const std::filesystem::path& configPath);
}
