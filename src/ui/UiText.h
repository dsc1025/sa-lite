#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include <string>

namespace Client
{
    void SetTextIfChanged(CWnd& window, const CString& text);
    void ShowIfChanged(CWnd& window, bool visible);
    void EnableIfChanged(CWnd& window, bool enabled);
    void SetCellIfChanged(CListCtrl& list, int row, int column, const CString& text);

    class ListUpdate
    {
    public:
        explicit ListUpdate(CListCtrl& list);
        ~ListUpdate();
        int Add(DWORD_PTR key, const CString& text);

    private:
        CListCtrl& list_;
        int row_ = 0;
        int top_ = -1;
        int topOffset_ = 0;
        DWORD_PTR topKey_ = 0;
        bool changed_ = false;
    };

    CString LegacyText(const std::string& text);
    CString ErrorText(const char* message);
    std::string AsciiText(CEdit& edit);
    CString Number(int value);
    CString Ratio(int value, int maximum);
}
