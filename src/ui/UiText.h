#pragma once

#include <afxwin.h>
#include <string>

namespace Client
{
    CString LegacyText(const std::string& text);
    CString ErrorText(const char* message);
    std::string AsciiText(CEdit& edit);
    CString Number(int value);
    CString Ratio(int value, int maximum);
}
