#include "MiniMap.h"
#include "UiText.h"
#include "../map/MapData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
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

    void MiniMap::Update(const std::filesystem::path& directory, int id, int x, int y)
    {
        if (id == id_ && directory == directory_ && x == x_ && y == y_) return;
        x_ = x;
        y_ = y;
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
        auto oldFont = dc.SelectObject(GetFont());
        CRect area(item->rcItem);
        dc.FillSolidRect(area, RGB(255, 255, 255));
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(30, 30, 30));
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
        dc.SelectObject(oldFont);
        dc.Detach();
    }

    CRect MiniMap::CanvasRect() const
    {
        CRect area;
        GetClientRect(&area);
        area.DeflateRect(4, 4);
        area.top += 30;
        area.right = (std::max)(area.left, area.right);
        area.bottom = (std::max)(area.top, area.bottom);
        return area;
    }

    void MiniMap::Load(const std::filesystem::path& directory, int id)
    {
        auto map = LoadMap(directory, id);
        const auto& colours = map.colours;
        auto palettePath = directory.parent_path() / L"pal" / L"Palet_1.sap";
        std::ifstream paletteFile(palettePath, std::ios::binary | std::ios::ate);
        if (!paletteFile || paletteFile.tellg() != 708)
            throw std::runtime_error("找不到有效的小地图配色表 data\\pal\\Palet_1.sap");
        paletteFile.seekg(0);
        std::array<unsigned char, 708> raw{};
        paletteFile.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (!paletteFile)
            throw std::runtime_error("小地图配色表读取失败");
        std::array<COLORREF, 256> palette{};
        const COLORREF systemColours[] = { RGB(0,0,0), RGB(128,0,0), RGB(0,128,0), RGB(128,128,0),
            RGB(0,0,128), RGB(128,0,128), RGB(0,128,128), RGB(192,192,192), RGB(192,220,192),
            RGB(166,202,240), RGB(222,0,0), RGB(255,95,0), RGB(255,255,160), RGB(0,95,210),
            RGB(80,210,255), RGB(40,225,40) };
        for (std::size_t i = 0; i < 16; ++i)
            palette[i] = systemColours[i];
        for (std::size_t i = 0; i < 224; ++i)
            palette[16 + i] = RGB(raw[i * 3 + 2], raw[i * 3 + 1], raw[i * 3]);
        const COLORREF upperColours[] = { RGB(255,251,240), RGB(160,160,164), RGB(128,128,128),
            RGB(255,0,0), RGB(0,255,0), RGB(255,255,0), RGB(0,0,255), RGB(255,0,255),
            RGB(0,255,255), RGB(255,255,255), RGB(245,195,150), RGB(225,160,95), RGB(195,125,70),
            RGB(155,85,30), RGB(70,65,55), RGB(40,35,30) };
        for (std::size_t i = 0; i < 16; ++i)
            palette[240 + i] = upperColours[i];
        std::vector<RGBQUAD> pixels(colours.size());
        for (std::size_t i = 0; i < colours.size(); ++i)
        {
            COLORREF colour = palette[colours[i]];
            double luminance = GetRValue(colour) * 0.299 + GetGValue(colour) * 0.587 + GetBValue(colour) * 0.114;
            auto channel = [luminance](int value)
            {
                double saturated = luminance + (value - luminance) * 1.1;
                return static_cast<BYTE>((std::clamp)(static_cast<int>((saturated - 128) * 1.08 + 128), 0, 255));
            };
            pixels[i] = { channel(GetBValue(colour)), channel(GetGValue(colour)), channel(GetRValue(colour)), 0 };
        }
        width_ = map.width;
        height_ = map.height;
        pixels_ = std::move(pixels);
        error_.Empty();
    }
}
