#include "MiniMap.h"
#include "UiText.h"
#include "../map/MapData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Client
{
    bool MiniMap::GetTarget(CPoint point, int& x, int& y) const
    {
        CRect area = CanvasRect();
        if (pixels_.empty() || x_ < 0 || y_ < 0 || !area.PtInRect(point))
            return false;
        double sum = x_ + y_ + 1 + (point.x - area.CenterPoint().x) / CellScale;
        double difference = y_ - x_ + (point.y - area.CenterPoint().y) * 2 / CellScale;
        x = static_cast<int>(std::floor((sum - difference) / 2));
        y = static_cast<int>(std::floor((sum + difference) / 2));
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    void MiniMap::Update(const std::filesystem::path& directory, int id, int x, int y, const CString& name)
    {
        x_ = x;
        y_ = y;
        title_.Format(L"地图 %s (%d)   坐标 %d, %d", name.GetString(), id, x, y);
        if (id != id_ || directory != directory_)
        {
            id_ = id;
            directory_ = directory;
            pixels_.clear();
            error_ = L"等待服务器发送地图位置";
            if (id >= 0 && !directory.empty())
            {
                try { Load(directory, id); }
                catch (const std::exception& error) { error_ = ErrorText(error.what()); }
            }
        }
        Invalidate(FALSE);
    }

    void MiniMap::DrawItem(LPDRAWITEMSTRUCT item)
    {
        CDC dc;
        dc.Attach(item->hDC);
        CRect area(item->rcItem);
        dc.FillSolidRect(area, RGB(255, 255, 255));
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(30, 30, 30));
        CRect heading = area;
        heading.DeflateRect(8, 6);
        heading.bottom = heading.top + 24;
        dc.DrawText(title_, heading, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        area = CanvasRect();
        if (pixels_.empty())
            dc.DrawText(error_, area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        else if (area.Width() > 0 && area.Height() > 0)
        {
            int width = area.Width(), height = area.Height();
            int left = area.left, top = area.top;
            CPoint center = area.CenterPoint();
            std::vector<RGBQUAD> projected(static_cast<std::size_t>(width) * height,
                RGBQUAD{ 244, 241, 237, 0 });
            for (int py = 0; py < height; ++py)
            {
                double difference = y_ - x_ + (top + py + 0.5 - center.y) * 2 / CellScale;
                for (int px = 0; px < width; ++px)
                {
                    double sum = x_ + y_ + 1 + (left + px + 0.5 - center.x) / CellScale;
                    double x = (sum - difference) / 2;
                    double y = (sum + difference) / 2;
                    if (x >= 0 && x < width_ && y >= 0 && y < height_)
                        projected[static_cast<std::size_t>(py) * width + px] =
                            pixels_[static_cast<std::size_t>(static_cast<int>(y)) * width_ + static_cast<int>(x)];
                }
            }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc.GetSafeHdc(), left, top, width, height, 0, 0, 0, height,
                projected.data(), &info, DIB_RGB_COLORS);
            if (x_ >= 0 && x_ < width_ && y_ >= 0 && y_ < height_)
            {
                int px = center.x, py = center.y;
                CBrush marker(RGB(255, 40, 40));
                CPen outline(PS_SOLID, 1, RGB(255, 255, 255));
                auto oldBrush = dc.SelectObject(&marker);
                auto oldPen = dc.SelectObject(&outline);
                dc.Ellipse(px - 4, py - 4, px + 5, py + 5);
                dc.SelectObject(oldPen);
                dc.SelectObject(oldBrush);
            }
        }
        dc.Detach();
    }

    CRect MiniMap::CanvasRect() const
    {
        CRect area;
        GetClientRect(&area);
        area.DeflateRect(12, 12);
        area.top += 30;
        int width = (std::max)(0, (std::min)(512, area.Width()));
        int height = (std::max)(0, (std::min)(256, area.Height()));
        CPoint center = area.CenterPoint();
        return CRect(center.x - width / 2, center.y - height / 2,
            center.x - width / 2 + width, center.y - height / 2 + height);
    }

    void MiniMap::Load(const std::filesystem::path& directory, int id)
    {
        auto map = LoadMap(directory, id);
        const auto& colours = map.colours;
        const std::array<COLORREF, 16> palette = { RGB(73,86,99), RGB(105,125,141),
            RGB(98,143,132), RGB(103,166,150), RGB(72,139,104), RGB(119,168,107),
            RGB(165,190,122), RGB(192,194,139), RGB(80,149,190), RGB(109,176,201),
            RGB(211,195,149), RGB(193,174,139), RGB(164,153,139), RGB(137,143,150),
            RGB(166,175,184), RGB(208,215,217) };
        std::vector<RGBQUAD> pixels(colours.size());
        for (std::size_t i = 0; i < colours.size(); ++i)
        {
            COLORREF colour = palette[colours[i] / 16];
            int shade = (colours[i] % 16 - 8) * 2;
            auto channel = [shade](int value) { return static_cast<BYTE>((std::clamp)(value + shade, 0, 255)); };
            pixels[i] = { channel(GetBValue(colour)), channel(GetGValue(colour)), channel(GetRValue(colour)), 0 };
        }
        width_ = map.width;
        height_ = map.height;
        pixels_ = std::move(pixels);
        error_.Empty();
    }
}
