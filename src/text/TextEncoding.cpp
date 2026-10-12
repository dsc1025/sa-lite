#include "TextEncoding.h"
#include <windows.h>

namespace TextEncoding
{
    std::wstring LegacyText(const std::string& text)
    {
        if (text.empty())
            return {};
        int length = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(length, L'\0');
        wchar_t* buffer = result.data();
        MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), buffer, length);
        return result;
    }

    std::wstring ErrorText(const char* message)
    {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, nullptr, 0);
        if (length == 0)
            return LegacyText(message);
        std::wstring result(length, L'\0');
        wchar_t* buffer = result.data();
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, buffer, length);
        result.resize(length - 1);
        return result;
    }
}
