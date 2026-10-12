#pragma once

#include <afxwin.h>
#include <filesystem>
#include <vector>

namespace Client
{
    class MiniMap : public CStatic
    {
    public:
        bool GetTarget(CPoint point, int& x, int& y) const;

        void Update(const std::filesystem::path& directory, int id, int x, int y, const CString& name);

    protected:
        void DrawItem(LPDRAWITEMSTRUCT item) override;

    private:
        CRect CanvasRect() const;

        static constexpr double CellScale = 8.0;
        void Load(const std::filesystem::path& directory, int id);

        int id_ = -2, x_ = -1, y_ = -1, width_ = 0, height_ = 0;
        std::filesystem::path directory_;
        std::vector<RGBQUAD> pixels_;
        CString title_, error_;
    };
}
