#include "UiText.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    void SetTextIfChanged(CWnd& window, const CString& text)
    {
        CString previous;
        window.GetWindowText(previous);
        if (previous != text) window.SetWindowText(text);
    }

    void ShowIfChanged(CWnd& window, bool visible)
    {
        if (((window.GetStyle() & WS_VISIBLE) != 0) != visible)
            window.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    }

    void EnableIfChanged(CWnd& window, bool enabled)
    {
        if ((window.IsWindowEnabled() != FALSE) != enabled)
            window.EnableWindow(enabled);
    }

    void SetCellIfChanged(CListCtrl& list, int row, int column, const CString& text)
    {
        if (list.GetItemText(row, column) != text)
            list.SetItemText(row, column, text);
    }

    ListUpdate::ListUpdate(CListCtrl& list) : list_(list)
    {
        if (list_.GetItemCount())
        {
            top_ = list_.GetTopIndex();
            topKey_ = list_.GetItemData(top_);
            CRect rect;
            if (list_.GetItemRect(top_, &rect, LVIR_BOUNDS)) topOffset_ = rect.top;
        }
    }

    int ListUpdate::Add(DWORD_PTR key, const CString& text)
    {
        int found = row_;
        while (found < list_.GetItemCount() && list_.GetItemData(found) != key) ++found;
        if (found == list_.GetItemCount())
        {
            list_.InsertItem(row_, text);
            list_.SetItemData(row_, key);
            changed_ = true;
        }
        else
        {
            while (found-- > row_)
            {
                list_.DeleteItem(row_);
                changed_ = true;
            }
            SetCellIfChanged(list_, row_, 0, text);
        }
        return row_++;
    }

    ListUpdate::~ListUpdate()
    {
        while (list_.GetItemCount() > row_)
        {
            list_.DeleteItem(row_);
            changed_ = true;
        }
        if (!changed_ || top_ < 0 || !list_.GetItemCount()) return;
        int target = top_ < list_.GetItemCount() ? top_ : list_.GetItemCount() - 1;
        for (int i = 0; i < list_.GetItemCount(); ++i)
            if (list_.GetItemData(i) == topKey_) { target = i; break; }
        CRect rect;
        if (list_.GetItemRect(target, &rect, LVIR_BOUNDS))
            list_.Scroll(CSize(0, rect.top - topOffset_));
    }

    CString LegacyText(const std::string& text)
    {
        auto wide = TextEncoding::LegacyText(text);
        return CString(wide.data(), static_cast<int>(wide.size()));
    }

    CString ErrorText(const char* message)
    {
        auto wide = TextEncoding::ErrorText(message);
        return CString(wide.data(), static_cast<int>(wide.size()));
    }

    std::string AsciiText(CEdit& edit)
    {
        CString text;
        edit.GetWindowText(text);
        std::string result;
        for (int i = 0; i < text.GetLength(); ++i)
        {
            if (text[i] < 33 || text[i] > 126)
                throw std::invalid_argument("Please use ASCII characters without spaces");
            result += static_cast<char>(text[i]);
        }
        return result;
    }

    CString Number(int value)
    {
        CString text;
        text.Format(L"%d", value);
        return text;
    }

    CString Ratio(int value, int maximum)
    {
        CString text;
        text.Format(L"%d / %d", value, maximum);
        return text;
    }
}
