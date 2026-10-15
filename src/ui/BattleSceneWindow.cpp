#include "BattleSceneWindow.h"
#include "UiText.h"
#include "../map/ResourceDirectory.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace Client
{
    BEGIN_MESSAGE_MAP(BattleSceneWindow, CWnd)
        ON_WM_PAINT()
        ON_WM_GETMINMAXINFO()
        ON_WM_CLOSE()
        ON_WM_TIMER()
        ON_WM_ERASEBKGND()
        ON_WM_DESTROY()
    END_MESSAGE_MAP()

    void BattleSceneWindow::LoadResources(const CString& configPath)
    {
        resourceDirectory_ = LoadResourceDirectory(std::filesystem::path(configPath.GetString()));
        animations_.clear();
        pictures_.clear();
        inspected_.clear();
        staticSprites_.clear();
        cachedBytes_ = 0;
        cacheSerial_ = 0;
        paletteReady_ = false;
        sprites_.close();
        images_.close();
        sprites_.clear();
        images_.clear();
        auto loadIndex = [&](const wchar_t* name, std::size_t recordSize, auto readRecord)
        {
            std::ifstream file(resourceDirectory_ / name, std::ios::binary | std::ios::ate);
            if (!file)
                return;
            auto size = static_cast<std::streamoff>(file.tellg());
            if (size <= 0 || size % static_cast<std::streamoff>(recordSize) != 0 || size > 80000000)
                return;
            file.seekg(0);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!file)
                return;
            for (std::size_t offset = 0; offset < bytes.size(); offset += recordSize)
                readRecord(bytes.data() + offset);
        };
        loadIndex(L"spradrn_lite.bin", 12, [&](const unsigned char* record)
        {
            std::uint32_t id, offset, metadata;
            std::memcpy(&id, record, 4);
            std::memcpy(&offset, record + 4, 4);
            std::memcpy(&metadata, record + 8, 4);
            animations_[id] = { offset, metadata & 0xFFFF };
        });
        loadIndex(L"adrn_lite.bin", 80, [&](const unsigned char* record)
        {
            std::uint32_t id;
            PictureIndex picture;
            std::memcpy(&id, record, 4);
            std::memcpy(&picture.offset, record + 4, 4);
            std::memcpy(&picture.size, record + 8, 4);
            std::memcpy(&picture.x, record + 12, 4);
            std::memcpy(&picture.y, record + 16, 4);
            std::memcpy(&picture.width, record + 20, 4);
            std::memcpy(&picture.height, record + 24, 4);
            pictures_[id] = picture;
        });
        sprites_.open(resourceDirectory_ / L"spr_lite.bin", std::ios::binary | std::ios::ate);
        images_.open(resourceDirectory_ / L"real_lite.bin", std::ios::binary | std::ios::ate);
        spriteSize_ = sprites_ ? static_cast<std::uint64_t>(sprites_.tellg()) : 0;
        imageSize_ = images_ ? static_cast<std::uint64_t>(images_.tellg()) : 0;
        std::ifstream paletteFile(resourceDirectory_ / L"Palet_1.sap", std::ios::binary | std::ios::ate);
        std::array<unsigned char, 708> raw{};
        if (paletteFile && paletteFile.tellg() == 708)
        {
            paletteFile.seekg(0);
            paletteFile.read(reinterpret_cast<char*>(raw.data()), raw.size());
            const COLORREF system[] = { RGB(0,0,0), RGB(128,0,0), RGB(0,128,0), RGB(128,128,0),
                RGB(0,0,128), RGB(128,0,128), RGB(0,128,128), RGB(192,192,192), RGB(192,220,192),
                RGB(166,202,240), RGB(222,0,0), RGB(255,95,0), RGB(255,255,160), RGB(0,95,210),
                RGB(80,210,255), RGB(40,225,40) };
            const COLORREF upper[] = { RGB(255,251,240), RGB(160,160,164), RGB(128,128,128),
                RGB(255,0,0), RGB(0,255,0), RGB(255,255,0), RGB(0,0,255), RGB(255,0,255),
                RGB(0,255,255), RGB(255,255,255), RGB(245,195,150), RGB(225,160,95), RGB(195,125,70),
                RGB(155,85,30), RGB(70,65,55), RGB(40,35,30) };
            for (int i = 0; i < 256; ++i)
            {
                COLORREF colour = i < 16 ? system[i] : i >= 240 ? upper[i - 240] :
                    RGB(raw[(i - 16) * 3 + 2], raw[(i - 16) * 3 + 1], raw[(i - 16) * 3]);
                palette_[i] = { GetBValue(colour), GetGValue(colour), GetRValue(colour),
                    static_cast<BYTE>(i == 253 ? 0 : 255) };
            }
            paletteReady_ = static_cast<bool>(paletteFile);
        }
    }

    std::uint64_t BattleSceneWindow::SpriteKey(std::uint32_t image, int direction, int action, std::uint32_t frame)
    {
        return (static_cast<std::uint64_t>(image) << 32) | (static_cast<std::uint64_t>(direction) << 24) |
            (static_cast<std::uint64_t>(action) << 16) | frame;
    }

    void BattleSceneWindow::InspectImage(std::uint32_t image, int direction, int requestedAction, std::uint32_t frameIndex)
    {
        const std::uint64_t key = SpriteKey(image, direction, requestedAction, frameIndex);
        auto cached = staticSprites_.find(key);
        if (cached != staticSprites_.end())
        {
            cached->second.lastUsed = ++cacheSerial_;
            return;
        }
        if (!inspected_.emplace(key, true).second)
            return;
        try
        {
            auto require = [](bool valid, const char* error)
            {
                if (!valid) throw std::runtime_error(error);
            };
            require(paletteReady_, "调色板未载入");
            auto animation = animations_.find(image);
            require(animation != animations_.end(), "动画索引未找到");
            std::uint64_t cursor = animation->second[0];
            std::uint32_t bitmap = 0;
            int frameX = 0, frameY = 0, score = -1;
            std::uint32_t selectedCount = 1, selectedDuration = 500;
            for (std::uint32_t i = 0; i < animation->second[1]; ++i)
            {
                require(cursor + 12 <= spriteSize_, "动作头越界");
                unsigned char header[12];
                sprites_.clear();
                sprites_.seekg(static_cast<std::streamoff>(cursor));
                sprites_.read(reinterpret_cast<char*>(header), sizeof(header));
                require(static_cast<bool>(sprites_), "动作头读取失败");
                std::uint16_t facing, action;
                std::uint32_t count, duration;
                std::memcpy(&facing, header, 2);
                std::memcpy(&action, header + 2, 2);
                std::memcpy(&count, header + 8, 4);
                std::memcpy(&duration, header + 4, 4);
                cursor += 12;
                require(cursor + static_cast<std::uint64_t>(count) * 10 <= spriteSize_, "动作帧越界");
                require(count <= 4096, "动作帧数量无效");
                int candidate = (action == requestedAction ? 4 : action == 3 ? 2 : 0) + (facing == direction ? 1 : 0);
                if (count > 0 && candidate > score)
                {
                    unsigned char frame[10];
                    sprites_.seekg(static_cast<std::streamoff>(cursor + (frameIndex % count) * 10));
                    sprites_.read(reinterpret_cast<char*>(frame), sizeof(frame));
                    require(static_cast<bool>(sprites_), "首帧读取失败");
                    std::int16_t x, y;
                    std::memcpy(&bitmap, frame, 4);
                    std::memcpy(&x, frame + 4, 2);
                    std::memcpy(&y, frame + 6, 2);
                    frameX = x;
                    frameY = y;
                    score = candidate;
                    selectedCount = count;
                    selectedDuration = std::max<std::uint32_t>(100, std::min<std::uint32_t>(duration, 10000));
                }
                cursor += static_cast<std::uint64_t>(count) * 10;
            }
            require(score >= 0, "没有可显示的帧");
            auto found = pictures_.find(bitmap);
            require(found != pictures_.end(), "图片索引缺失");
            const auto& info = found->second;
            require(info.size >= 16 && info.size <= 16 * 1024 * 1024 &&
                static_cast<std::uint64_t>(info.offset) + info.size <= imageSize_, "图片数据越界");
            require(info.width > 0 && info.height > 0 && info.width <= 4096 && info.height <= 4096 &&
                static_cast<std::uint64_t>(info.width) * info.height <= 4 * 1024 * 1024, "图片尺寸无效");
            std::vector<unsigned char> block(info.size);
            images_.clear();
            images_.seekg(info.offset);
            images_.read(reinterpret_cast<char*>(block.data()), block.size());
            require(static_cast<bool>(images_), "图片读取失败");
            std::uint32_t width, height, size;
            std::memcpy(&width, block.data() + 4, 4);
            std::memcpy(&height, block.data() + 8, 4);
            std::memcpy(&size, block.data() + 12, 4);
            require(block[0] == 'R' && block[1] == 'D' && width == info.width &&
                height == info.height && size == info.size, "RD 图片头与索引不一致");
            const std::size_t expected = static_cast<std::size_t>(width) * height;
            std::vector<unsigned char> pixels;
            pixels.reserve(expected);
            std::size_t input = 16;
            auto take = [&]()
            {
                require(input < block.size(), "RLE 数据不完整");
                return block[input++];
            };
            if (block[2] == 0)
            {
                require(block.size() - 16 == expected, "未压缩图片长度无效");
                pixels.assign(block.begin() + 16, block.end());
            }
            else
            {
                while (pixels.size() < expected)
                {
                    unsigned char code = take(), family = code >> 4;
                    std::size_t length = code & 15;
                    bool literal = family <= 2;
                    unsigned char colour = 253;
                    if (family == 8 || family == 9 || family == 10)
                        colour = take();
                    if (family == 1 || family == 9 || family == 13)
                        length = (length << 8) | take();
                    else if (family == 2 || family == 10 || family == 14)
                    {
                        unsigned char high = take(), low = take();
                        length = (length << 16) | (static_cast<std::size_t>(high) << 8) | low;
                    }
                    else if (family != 0 && family != 8 && family != 12)
                    {
                        length = 1;
                        colour = code;
                    }
                    require(length <= expected - pixels.size(), "RLE 像素数量越界");
                    if (literal)
                    {
                        require(length <= block.size() - input, "RLE 原色数据不完整");
                        pixels.insert(pixels.end(), block.begin() + input, block.begin() + input + length);
                        input += length;
                    }
                    else
                        pixels.insert(pixels.end(), length, colour);
                }
                require(input == block.size(), "RLE 存在未解析的尾部数据");
            }
            StaticSprite sprite;
            sprite.width = static_cast<int>(width);
            sprite.height = static_cast<int>(height);
            sprite.x = info.x + frameX;
            sprite.y = info.y + frameY;
            sprite.frames = selectedCount;
            sprite.duration = selectedDuration;
            sprite.lastUsed = ++cacheSerial_;
            sprite.pixels.reserve(expected);
            for (unsigned char pixel : pixels)
                sprite.pixels.push_back(palette_[pixel]);
            const std::size_t bytes = expected * sizeof(RGBQUAD);
            while (cachedBytes_ + bytes > 32 * 1024 * 1024 && !staticSprites_.empty())
            {
                auto oldest = std::min_element(staticSprites_.begin(), staticSprites_.end(),
                    [](const auto& left, const auto& right) { return left.second.lastUsed < right.second.lastUsed; });
                cachedBytes_ -= oldest->second.pixels.size() * sizeof(RGBQUAD);
                inspected_.erase(oldest->first);
                staticSprites_.erase(oldest);
            }
            staticSprites_.emplace(key, std::move(sprite));
            cachedBytes_ += bytes;
        }
        catch (const std::exception&)
        {
        }
    }

    void BattleSceneWindow::PrepareAnimationFrames()
    {
        const auto now = GetTickCount64();
        const int playerSide = battle_.playerSlot >= 10 ? 1 : 0;
        for (int slot = 0; slot < 20; ++slot)
        {
            const auto& member = battle_.members[slot];
            if (!member.present) continue;
            auto& unit = unitAnimations_[slot];
            if (unit.started == 0) unit.started = now;
            const int direction = slot / 10 == playerSide ? 3 : 7;
            const int action = slot / 10 == playerSide ? 0 : 3;
            InspectImage(member.image, direction, action, 0);
            auto first = staticSprites_.find(SpriteKey(member.image, direction, action, 0));
            if (first == staticSprites_.end()) { unit.frame = 0; continue; }
            const auto elapsed = now >= unit.started ? now - unit.started : 0;
            unit.frame = static_cast<std::uint32_t>((elapsed % first->second.duration) *
                first->second.frames / first->second.duration);
            InspectImage(member.image, direction, action, unit.frame);
        }
    }

    void BattleSceneWindow::ApplyBattleState(const Game::BattleState& state)
    {
        auto previous = battle_.members;
        battle_ = state;
        for (int slot = 0; slot < 20; ++slot)
        {
            if (previous[slot].image != battle_.members[slot].image ||
                previous[slot].present != battle_.members[slot].present)
            {
                unitAnimations_[slot] = {};
                unitAnimations_[slot].started = GetTickCount64();
            }
        }
    }

    void BattleSceneWindow::OnTimer(UINT_PTR timer)
    {
        if (timer != 1) { CWnd::OnTimer(timer); return; }
        if (!active_ || dismissed_) return;
        PrepareAnimationFrames();
        Invalidate(FALSE);
    }

    void BattleSceneWindow::PrepareForEncounter()
    {
        dismissed_ = false;
        active_ = false;
        unitAnimations_ = {};
    }

    void BattleSceneWindow::Update(CWnd* owner, const SessionView& view, GameSession& session)
    {
        session_ = &session;
        if (view.screen != Screen::Game)
            dismissed_ = false;
        if (dismissed_)
        {
            if (GetSafeHwnd())
                ShowWindow(SW_HIDE);
            return;
        }
        bool active = view.screen == Screen::Game && view.player.inBattle && !view.quickBattle;
        CString roundText = L"回合：—";
        const int round = active ? view.battle.round : battle_.round;
        if (round >= 0)
            roundText.Format(L"第%d回合", round);
        CString title;
        title.Format(L"等级 %d HP %d/%d MP %d/%d - [%s]", view.player.level,
            view.player.hp, view.player.maxHp, view.player.mp, view.player.maxMp, roundText.GetString());
        if (!active)
        {
            active_ = false;
            if (GetSafeHwnd()) KillTimer(1);
            if (GetSafeHwnd() && (view.quickBattle || view.screen != Screen::Game))
                ShowWindow(SW_HIDE);
            else if (GetSafeHwnd())
            {
                SetWindowText(title);
                Invalidate(FALSE);
            }
            return;
        }
        if (!view.battle.received)
            return;
        if (!GetSafeHwnd())
        {
            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
            CRect area(0, 0, 640, 480);
            ::AdjustWindowRectEx(&area, style, FALSE, WS_EX_DLGMODALFRAME);
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)),
                title, style, area, owner, 0))
                return;
            font_.CreatePointFont(85, L"Microsoft YaHei UI");
            CenterWindow(owner);
        }
        if (!active_)
        {
            inspected_.clear();
            unitAnimations_ = {};
            for (auto& unit : unitAnimations_) unit.started = GetTickCount64();
            SetTimer(1, 33, nullptr);
        }
        active_ = true;
        ApplyBattleState(view.battle);
        PrepareAnimationFrames();
        if (!IsWindowVisible())
            ShowWindow(SW_SHOWNOACTIVATE);
        Invalidate(FALSE);
        SetWindowText(title);
    }

    void BattleSceneWindow::OnPaint()
    {
        CPaintDC paint(this);
        CRect area;
        GetClientRect(&area);
        if (area.IsRectEmpty())
            return;
        const int width = area.Width(), height = area.Height();
        if (!bufferDC_.GetSafeHdc() || bufferSize_ != area.Size())
        {
            if (previousBitmap_)
                ::SelectObject(bufferDC_.GetSafeHdc(), previousBitmap_);
            previousBitmap_ = nullptr;
            bufferBitmap_.DeleteObject();
            bufferDC_.DeleteDC();
            if (!bufferDC_.CreateCompatibleDC(&paint) || !bufferBitmap_.CreateCompatibleBitmap(&paint, width, height))
            {
                paint.FillSolidRect(area, RGB(160, 160, 160));
                return;
            }
            previousBitmap_ = ::SelectObject(bufferDC_.GetSafeHdc(), bufferBitmap_.GetSafeHandle());
            bufferSize_ = area.Size();
        }
        CDC& dc = bufferDC_;
        auto& canvas = canvas_;
        canvas.resize(static_cast<std::size_t>(width) * height);
        std::fill(canvas.begin(), canvas.end(), RGBQUAD{ 160, 160, 160, 0 });
        const int playerSide = battle_.playerSlot >= 10 ? 1 : 0;
        const int columns[] = { 0, 1, -1, 2, -2 };
        auto position = [&](int slot)
        {
            bool ally = slot / 10 == playerSide;
            int column = columns[slot % 5];
            bool pet = slot % 10 >= 5;
            return CPoint((ally ? 455 : 185) + column * 52 + (pet ? (ally ? -64 : 64) : 0),
                (ally ? 366 : 168) + column * (ally ? 34 : -34) + (pet ? (ally ? -48 : 48) : 0));
        };
        std::array<int, 20> order;
        for (int slot = 0; slot < 20; ++slot)
            order[slot] = slot;
        std::stable_sort(order.begin(), order.end(), [&](int left, int right)
        {
            return position(left).y < position(right).y;
        });
        for (int slot : order)
        {
            const auto& member = battle_.members[slot];
            if (!member.present)
                continue;
            bool ally = slot / 10 == playerSide;
            const std::uint64_t key = SpriteKey(member.image, ally ? 3 : 7, ally ? 0 : 3, unitAnimations_[slot].frame);
            auto found = staticSprites_.find(key);
            if (found == staticSprites_.end())
                continue;
            const auto& sprite = found->second;
            CPoint anchor = position(slot);
            const int left = anchor.x + sprite.x;
            const int top = anchor.y + sprite.y;
            for (int row = std::max(0, -top); row < sprite.height && top + row < height; ++row)
            {
                for (int column = std::max(0, -left); column < sprite.width && left + column < width; ++column)
                {
                    const auto& pixel = sprite.pixels[(sprite.height - 1 - row) * sprite.width + column];
                    if (pixel.rgbReserved != 0)
                        canvas[(top + row) * width + left + column] = pixel;
                }
            }
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        ::SetDIBitsToDevice(dc.GetSafeHdc(), 0, 0, width, height, 0, 0, 0, height,
            canvas.data(), &info, DIB_RGB_COLORS);
        int saved = dc.SaveDC();
        dc.SelectObject(&font_);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(235, 235, 235));
        for (int slot : order)
        {
            const auto& member = battle_.members[slot];
            if (!member.present)
                continue;
            bool ally = slot / 10 == playerSide;
            CPoint anchor = position(slot);
            int x = anchor.x - 56;
            int y = anchor.y;
            const std::uint64_t key = SpriteKey(member.image, ally ? 3 : 7, ally ? 0 : 3, unitAnimations_[slot].frame);
            if (staticSprites_.find(key) == staticSprites_.end())
            {
                CRect box(x, y - 58, x + 112, y);
                dc.FillSolidRect(box, ally ? RGB(36, 78, 74) : RGB(88, 51, 51));
                CString missing;
                missing.Format(L"素材 %u\n未载入", member.image);
                dc.DrawText(missing, box, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            CString label;
            label.Format(L"[%d] %s %d/%d", slot, LegacyText(member.name).GetString(), member.hp, member.maxHp);
            CRect caption(x, y + 2, x + 112, y + 18);
            dc.DrawText(label, caption, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            dc.FillSolidRect(x + 4, y + 20, 104, 4, RGB(50, 50, 50));
            int hp = member.maxHp > 0 ? static_cast<int>(std::min<std::int64_t>(104,
                static_cast<std::int64_t>(std::max(0, member.hp)) * 104 / member.maxHp)) : 0;
            if (hp > 0)
                dc.FillSolidRect(x + 4, y + 20, hp, 4, RGB(80, 200, 100));
        }
        dc.RestoreDC(saved);
        paint.BitBlt(0, 0, width, height, &dc, 0, 0, SRCCOPY);
    }

    BOOL BattleSceneWindow::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void BattleSceneWindow::OnDestroy()
    {
        KillTimer(1);
        if (previousBitmap_)
            ::SelectObject(bufferDC_.GetSafeHdc(), previousBitmap_);
        previousBitmap_ = nullptr;
        bufferBitmap_.DeleteObject();
        bufferDC_.DeleteDC();
        canvas_.clear();
        CWnd::OnDestroy();
    }

    void BattleSceneWindow::OnGetMinMaxInfo(MINMAXINFO* info)
    {
        CWnd::OnGetMinMaxInfo(info);
        CRect area(0, 0, 640, 480);
        ::AdjustWindowRectEx(&area, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            FALSE, WS_EX_DLGMODALFRAME);
        info->ptMinTrackSize = CPoint(area.Width(), area.Height());
        info->ptMaxTrackSize = info->ptMinTrackSize;
        info->ptMaxSize = info->ptMinTrackSize;
    }

    void BattleSceneWindow::OnClose()
    {
        dismissed_ = true;
        active_ = false;
        KillTimer(1);
        if (session_)
            session_->SetEncounterEnabled(false);
        ShowWindow(SW_HIDE);
    }
}
