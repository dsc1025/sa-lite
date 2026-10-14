#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include "PropertiesWindow.h"

namespace Client
{
    struct SessionView;
    class GameSession;

    class RolePage : public CWnd
    {
    public:
        RolePage(const SessionView& view, GameSession& session, CStatic& footer)
            : view_(view), session_(session), footer_(footer) {}

        bool Create(CWnd* owner, CFont* font);
        void Refresh();
        void Activate();
        void CloseEditor();

    protected:
        BOOL PreTranslateMessage(MSG* message) override;
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnPaint();
        afx_msg void OnTab(NMHDR*, LRESULT* result);
        afx_msg void OnSelect(NMHDR*, LRESULT* result);
        afx_msg void OnPetClick(NMHDR* header, LRESULT* result);
        afx_msg void OnListRightClick(NMHDR* header, LRESULT* result);
        afx_msg void OnPetStorageRightClick(NMHDR* header, LRESULT* result);
        afx_msg void OnListDoubleClick(NMHDR* header, LRESULT* result);
        afx_msg void OnItemStorageRightClick(NMHDR* header, LRESULT* result);
        afx_msg void OnDetailDraw(NMHDR* header, LRESULT* result);
        afx_msg void OnDetailClick(NMHDR* header, LRESULT* result);
        afx_msg void OnPetState();
        afx_msg void OnPetStateClose();
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();
        void RefreshDetail();
        bool CanIncreaseAttribute() const;
        bool AttributeButtonRect(int row, CRect& rect) const;
        void RefreshPets();
        void RefreshItems();
        void RefreshPetStorage(bool visible);
        void RefreshItemStorage(bool visible);
        void AddRow(const wchar_t* name, const CString& value);
        void ShowItemProperties(CListCtrl& list, int row);
        void ShowPetProperties(CListCtrl& list, int row);

        const SessionView& view_;
        GameSession& session_;
        CStatic& footer_;
        CTabCtrl tabs_;
        CListCtrl list_, detail_, petStorage_, itemStorage_;
        ItemPropertiesWindow itemProperties_, petProperties_;
        CComboBox petState_;
        int editingPet_ = -1;
        bool refreshing_ = false;
    };
}
