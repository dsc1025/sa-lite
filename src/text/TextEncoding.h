#pragma once

#include <string>

namespace TextEncoding
{
    std::wstring LegacyText(const std::string& text);
    std::wstring ErrorText(const char* message);
}
