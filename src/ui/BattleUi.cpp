#include "MainWindow.h"
#include "BattleUi.h"
#include "UiText.h"
#include "../map/MapData.h"
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
        loadIndex(L"spradrn_5.bin", 12, [&](const unsigned char* record)
        {
            std::uint32_t id, offset, metadata;
            std::memcpy(&id, record, 4);
            std::memcpy(&offset, record + 4, 4);
            std::memcpy(&metadata, record + 8, 4);
            animations_[id] = { offset, metadata & 0xFFFF };
        });
        loadIndex(L"adrn_15.bin", 80, [&](const unsigned char* record)
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
        sprites_.open(resourceDirectory_ / L"spr_4.bin", std::ios::binary | std::ios::ate);
        images_.open(resourceDirectory_ / L"real_15.bin", std::ios::binary | std::ios::ate);
        spriteSize_ = sprites_ ? static_cast<std::uint64_t>(sprites_.tellg()) : 0;
        imageSize_ = images_ ? static_cast<std::uint64_t>(images_.tellg()) : 0;
        std::ifstream paletteFile(resourceDirectory_ / L"pal" / L"Palet_1.sap", std::ios::binary | std::ios::ate);
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
            InspectImage(member.image, direction, 3, 0);
            auto first = staticSprites_.find(SpriteKey(member.image, direction, 3, 0));
            if (first == staticSprites_.end()) { unit.frame = 0; continue; }
            const auto elapsed = now >= unit.started ? now - unit.started : 0;
            unit.frame = static_cast<std::uint32_t>((elapsed % first->second.duration) *
                first->second.frames / first->second.duration);
            InspectImage(member.image, direction, 3, unit.frame);
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
        CString title;
        title.Format(L"等级 %d HP %d/%d MP %d/%d - [%s]", view.player.level,
            view.player.hp, view.player.maxHp, view.player.mp, view.player.maxMp,
            active ? (view.battleResult.message.empty() ? L"自动战斗" : view.battleResult.message.c_str()) : L"战斗结束");
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
            if (!dismissed_)
                ShowWindow(SW_SHOWNOACTIVATE);
        }
        active_ = true;
        ApplyBattleState(view.battle);
        PrepareAnimationFrames();
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
                paint.FillSolidRect(area, RGB(220, 220, 220));
                return;
            }
            previousBitmap_ = ::SelectObject(bufferDC_.GetSafeHdc(), bufferBitmap_.GetSafeHandle());
            bufferSize_ = area.Size();
        }
        CDC& dc = bufferDC_;
        auto& canvas = canvas_;
        canvas.resize(static_cast<std::size_t>(width) * height);
        std::fill(canvas.begin(), canvas.end(), RGBQUAD{ 220, 220, 220, 0 });
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
            const std::uint64_t key = SpriteKey(member.image, ally ? 3 : 7, 3, unitAnimations_[slot].frame);
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
            const std::uint64_t key = SpriteKey(member.image, ally ? 3 : 7, 3, unitAnimations_[slot].frame);
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

    BEGIN_MESSAGE_MAP(BattleSettingsWindow, CWnd)
        ON_WM_CREATE()
        ON_WM_CLOSE()
        ON_BN_CLICKED(2, &BattleSettingsWindow::OnSave)
        ON_BN_CLICKED(3, &BattleSettingsWindow::OnSave)
        ON_CBN_SELCHANGE(4, &BattleSettingsWindow::OnSave)
        ON_EN_CHANGE(5, &BattleSettingsWindow::OnSave)
        ON_EN_CHANGE(6, &BattleSettingsWindow::OnSave)
        ON_BN_CLICKED(7, &BattleSettingsWindow::OnSave)
    END_MESSAGE_MAP()

    void BattleSettingsWindow::LoadSettings(const CString& path, GameSession& session)
    {
        configPath_ = path;
        session_ = &session;
        quickBattleEnabled_ = GetPrivateProfileIntW(L"Battle", L"QuickBattle", 1, configPath_) != 0;
        useItems_ = GetPrivateProfileIntW(L"Battle", L"UseItems", 0, configPath_) != 0;
        useMagic_ = GetPrivateProfileIntW(L"Battle", L"UseMagic", 0, configPath_) != 0;
        itemPercent_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"ItemPercent", 80, configPath_));
        magicPercent_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"MagicPercent", 60, configPath_));
        if (itemPercent_ < 0 || itemPercent_ > 100)
            itemPercent_ = 80;
        if (magicPercent_ < 0 || magicPercent_ > 100)
            magicPercent_ = 60;
        magicSlot_ = static_cast<int>(GetPrivateProfileIntW(L"Battle", L"MagicSlot", -1, configPath_));
        wchar_t name[256];
        GetPrivateProfileStringW(L"Battle", L"MagicName", L"", name, 256, configPath_);
        int length = WideCharToMultiByte(936, 0, name, -1, nullptr, 0, nullptr, nullptr);
        magicName_.resize(length > 0 ? length : 0);
        if (length > 0)
        {
            WideCharToMultiByte(936, 0, name, -1, magicName_.data(), length, nullptr, nullptr);
            magicName_.resize(length - 1);
        }
        ApplySettings();
    }

    void BattleSettingsWindow::Show(CWnd* owner, const std::vector<Game::MagicStatus>& magic, GameSession& session)
    {
        updating_ = true;
        if (!GetSafeHwnd())
        {
            if (!CreateEx(WS_EX_DLGMODALFRAME, AfxRegisterWndClass(CS_DBLCLKS,
                LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)),
                L"练级设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                CRect(0, 0, 484, 172), owner, 0))
            {
                updating_ = false;
                return;
            }
            CenterWindow(owner);
        }
        session_ = &session;
        currentMagic_ = magic;
        quickBattle_.SetCheck(quickBattleEnabled_ ? BST_CHECKED : BST_UNCHECKED);
        items_.SetCheck(useItems_ ? BST_CHECKED : BST_UNCHECKED);
        magic_.SetCheck(useMagic_ ? BST_CHECKED : BST_UNCHECKED);
        itemHp_.SetWindowText(Number(itemPercent_));
        magicHp_.SetWindowText(Number(magicPercent_));
        skills_.ResetContent();
        skills_.AddString(L"请选择技能");
        skills_.SetItemData(0, static_cast<DWORD_PTR>(-1));
        int selected = 0;
        for (std::size_t slot = 0; slot < magic.size(); ++slot)
        {
            const auto& skill = magic[slot];
            if (!skill.received || !skill.present || skill.name.empty())
                continue;
            int row = skills_.AddString(LegacyText(skill.name));
            skills_.SetItemData(row, slot);
            if (skill.name == magicName_ && (selected == 0 || static_cast<int>(slot) == magicSlot_))
                selected = row;
        }
        skills_.SetCurSel(selected);
        skills_.EnableWindow(skills_.GetCount() > 1);
        updating_ = false;
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
    }

    int BattleSettingsWindow::OnCreate(LPCREATESTRUCT create)
    {
        if (CWnd::OnCreate(create) == -1)
            return -1;
        font_.CreatePointFont(90, L"Microsoft YaHei UI");

        DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        quickBattle_.Create(L"快速战斗", buttonStyle, CRect(10, 10, 164, 32), this, 7);
        items_.Create(L"食物补血", buttonStyle, CRect(10, 10, 164, 32), this, 2);
        magic_.Create(L"补血", buttonStyle, CRect(10, 36, 102, 58), this, 3);
        skills_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            CRect(108, 36, 280, 200), this, 4);
        DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_NUMBER;
        itemHp_.Create(editStyle, CRect(202, 10, 244, 32), this, 5);
        magicHp_.Create(editStyle, CRect(322, 36, 364, 58), this, 6);
        itemHp_.SetLimitText(3);
        magicHp_.SetLimitText(3);
        labels_[0].Create(L"HP<", WS_CHILD | WS_VISIBLE, CRect(170, 13, 202, 32), this);
        labels_[1].Create(L"%", WS_CHILD | WS_VISIBLE, CRect(250, 13, 266, 32), this);
        labels_[2].Create(L"HP<", WS_CHILD | WS_VISIBLE, CRect(286, 39, 318, 58), this);
        labels_[3].Create(L"%", WS_CHILD | WS_VISIBLE, CRect(370, 39, 382, 58), this);
        CWnd* controls[] = { &items_, &magic_, &quickBattle_, &skills_, &itemHp_, &magicHp_ };
        for (auto control : controls)
            control->SetFont(&font_);
        for (auto& label : labels_)
            label.SetFont(&font_);
        CClientDC dc(this);
        CFont* previousFont = dc.SelectObject(&font_);
        const int margin = 10, gap = 4, height = 22;
        auto captionWidth = [&](CWnd& control)
        {
            CString caption;
            control.GetWindowText(caption);
            return dc.GetTextExtent(caption).cx;
        };
        int contentRight = 0;
        for (int row = 0; row < 2; ++row)
        {
            int x = margin, y = margin + (row + 1) * (height + gap);
            CButton& option = row == 0 ? items_ : magic_;
            int width = captionWidth(option) + GetSystemMetrics(SM_CXMENUCHECK) + gap;
            option.MoveWindow(x, y, width, height);
            x += width + gap;
            if (row == 1)
            {
                skills_.MoveWindow(x, y, 138, 164);
                x += 138 + gap;
            }
            CStatic& hp = labels_[row * 2];
            width = captionWidth(hp);
            hp.MoveWindow(x, y + 3, width, height - 3);
            x += width + gap;
            CEdit& input = row == 0 ? itemHp_ : magicHp_;
            input.MoveWindow(x, y, 42, height);
            x += 42 + gap;
            CStatic& percent = labels_[row * 2 + 1];
            width = captionWidth(percent);
            percent.MoveWindow(x, y + 3, width, height - 3);
            contentRight = std::max(contentRight, x + width);
        }
        quickBattle_.MoveWindow(margin, margin,
            captionWidth(quickBattle_) + GetSystemMetrics(SM_CXMENUCHECK) + gap, height);
        dc.SelectObject(previousFont);
        CRect area, window;
        GetClientRect(&area);
        GetWindowRect(&window);
        SetWindowPos(nullptr, 0, 0, window.Width() + contentRight + margin - area.Width(),
            window.Height() + margin * 2 + 3 * height + 2 * gap - area.Height(), SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }

    void BattleSettingsWindow::OnSave()
    {
        if (updating_ || configPath_.IsEmpty())
            return;
        if (session_)
        {
            bool done, stopped;
            auto view = session_->Snapshot(done, stopped);
            if (done || stopped || view.screen != Screen::Game || view.encounterEnabled ||
                view.encounterPending || view.player.inBattle)
            {
                ShowWindow(SW_HIDE);
                return;
            }
        }
        auto readPercent = [](CEdit& edit, int& value)
        {
            CString text;
            edit.GetWindowText(text);
            if (text.IsEmpty() || text.SpanIncluding(L"0123456789") != text)
                return;
            int percent = _wtoi(text);
            if (percent >= 0 && percent <= 100)
                value = percent;
        };
        readPercent(itemHp_, itemPercent_);
        readPercent(magicHp_, magicPercent_);
        int row = skills_.GetCurSel();
        int slot = row > 0 ? static_cast<int>(skills_.GetItemData(row)) : -1;
        quickBattleEnabled_ = quickBattle_.GetCheck() == BST_CHECKED;
        useItems_ = items_.GetCheck() == BST_CHECKED;
        useMagic_ = magic_.GetCheck() == BST_CHECKED;
        if (slot >= 0 && slot < static_cast<int>(currentMagic_.size()))
        {
            magicSlot_ = slot;
            magicName_ = currentMagic_[slot].name;
        }
        else if (row == 0 && skills_.GetCount() > 1)
        {
            magicSlot_ = -1;
            magicName_.clear();
        }
        ApplySettings();
        const wchar_t* keys[] = { L"QuickBattle", L"UseItems", L"UseMagic",
            L"ItemPercent", L"MagicPercent", L"MagicSlot", L"MagicName" };
        CString values[] = { Number(quickBattleEnabled_ ? 1 : 0), Number(useItems_ ? 1 : 0),
            Number(useMagic_ ? 1 : 0), Number(itemPercent_), Number(magicPercent_),
            Number(magicSlot_), LegacyText(magicName_) };
        for (int i = 0; i < 7; ++i)
        {
            if (!WritePrivateProfileStringW(L"Battle", keys[i], values[i], configPath_))
            {
                MessageBox(L"无法保存 exe 同目录的 config.ini，请检查目录写入权限。",
                    L"练级设置", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    void BattleSettingsWindow::ApplySettings()
    {
        Game::HealingSettings settings;
        settings.quickBattle = quickBattleEnabled_;
        settings.useItems = useItems_;
        settings.useMagic = useMagic_ && !magicName_.empty();
        settings.itemPercent = itemPercent_;
        settings.magicPercent = magicPercent_;
        settings.magicSlot = magicSlot_;
        settings.magicName = magicName_;
        if (session_)
            session_->SetHealingSettings(settings);
    }

    void BattleSettingsWindow::OnClose()
    {
        ShowWindow(SW_HIDE);
    }

    void BattleGroup::Update(const Game::BattleState& battle, int offset, bool received)
    {
        for (int i = 0; i < 10; ++i)
            members_[i] = battle.members[offset + i];
        offset_ = offset;
        received_ = received;
        Invalidate(FALSE);
    }

    void BattleGroup::DrawItem(LPDRAWITEMSTRUCT item)
    {
        CDC* dc = CDC::FromHandle(item->hDC);
        int saved = dc->SaveDC();
        if (CFont* font = GetFont())
            dc->SelectObject(font);
        CRect area(item->rcItem);
        dc->FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
        CString caption;
        GetWindowText(caption);
        CSize size = dc->GetTextExtent(caption);
        CRect border = area;
        border.top += size.cy / 2;
        dc->DrawEdge(&border, EDGE_ETCHED, BF_RECT);
        CRect title(area.left + 8, area.top, area.left + 16 + size.cx, area.top + size.cy);
        dc->FillSolidRect(title, GetSysColor(COLOR_BTNFACE));
        dc->SetTextColor(GetSysColor(COLOR_BTNTEXT));
        dc->SetBkMode(TRANSPARENT);
        dc->DrawText(caption, title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        CRect content(area.left + 12, area.top + size.cy + 8, area.right - 10, area.bottom - 10);
        if (!received_)
            dc->DrawText(L"等待战斗队员数据", content, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        else
        {
            int lineHeight = size.cy + 4;
            int spacing = std::max(1, std::min(lineHeight, content.Height() / 10));
            int top = content.top + (content.Height() - spacing * 10) / 2;
            // Each of the five fixed positions keeps its pet above its character.
            const int order[] = { 9, 4, 7, 2, 5, 0, 6, 1, 8, 3 };
            for (int rowIndex = 0; rowIndex < 10; ++rowIndex)
            {
                int slot = order[rowIndex];
                const auto& member = members_[slot];
                if (!member.present)
                    continue;
                CString text;
                text.Format(L"* [%X] %s  Lv:%d (%d/%d)", offset_ + slot,
                    LegacyText(member.name).GetString(), member.level, member.hp, member.maxHp);
                if (!member.rideName.empty())
                {
                    CString ride;
                    ride.Format(L" [骑] %s  Lv:%d (%d/%d)", LegacyText(member.rideName).GetString(),
                        member.rideLevel, member.rideHp, member.rideMaxHp);
                    text += ride;
                }
                int rowTop = top + rowIndex * spacing;
                CRect row(content.left, rowTop, content.right,
                    std::min<LONG>(rowTop + spacing, content.bottom));
                if (row.bottom > row.top)
                    dc->DrawText(text, row, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        dc->RestoreDC(saved);
    }

    void MainWindow::CreateBattleControls()
    {
        CRect empty(0, 0, 0, 0);
        battleTabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, IDC_BATTLE_TABS);
        battleTabs_.InsertItem(0, L"练级");
        battleTabs_.InsertItem(1, L"抓宠");
        battleStart_.Create(L"开始", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_START);
        battleStop_.Create(L"停止", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_STOP);
        battleSettings_.Create(L"设置", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, empty, this, IDC_BATTLE_SETTINGS);
        battleRound_.Create(L"回合：—", WS_CHILD | SS_RIGHT, empty, this);
        const wchar_t* sides[] = { L"上方队员", L"下方队员" };
        for (int i = 0; i < 2; ++i)
            battleSides_[i].Create(sides[i], WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                empty, this, IDC_BATTLE_UPPER + i);
        battleInfo_.Create(L"战斗提示：等待战斗", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            empty, this, IDC_BATTLE_INFO);
        battleExperience_.Create(L"经验获得：本场 —    累计 0", WS_CHILD, empty, this);
    }

    void MainWindow::LayoutBattle(const CRect& mainPage)
    {
        battleTabs_.MoveWindow(mainPage);
        CRect battlePage = mainPage;
        battleTabs_.AdjustRect(FALSE, &battlePage);
        battlePage.DeflateRect(6, 6);
        if (mainTabs_.GetCurSel() == 2)
            developing_.MoveWindow(battlePage);
        int battleWidth = std::max(0, battlePage.Width());
        battleStart_.MoveWindow(battlePage.left, battlePage.top, 70, 26);
        battleStop_.MoveWindow(battlePage.left + 78, battlePage.top, 70, 26);
        battleSettings_.MoveWindow(battlePage.left + 156, battlePage.top, 70, 26);
        int sideTop = battlePage.top + 34;
        int sideBottom = std::max<LONG>(sideTop + 24, battlePage.bottom - 30);
        int sideWidth = std::max(0, (battleWidth - 12) / 2);
        int rightLeft = battlePage.left + sideWidth + 12;
        battleSides_[0].MoveWindow(battlePage.left, sideTop, sideWidth,
            std::max(0, sideBottom - sideTop - 24));
        battleSides_[1].MoveWindow(rightLeft, sideTop + 24,
            std::max<LONG>(0, battlePage.right - rightLeft), std::max(0, sideBottom - sideTop - 24));
        battleRound_.MoveWindow(rightLeft, sideTop,
            std::max<LONG>(0, battlePage.right - rightLeft - 8), 22);
        battleInfo_.MoveWindow(battlePage.left + 8, sideBottom - 22,
            std::max(0, sideWidth - 16), 22);
        battleExperience_.MoveWindow(battlePage.left, sideBottom + 8, battleWidth, 22);
    }

    void MainWindow::RefreshBattle(bool visible)
    {
        bool leveling = visible && battleTabs_.GetCurSel() == 0;
        if (visible)
            developing_.ShowWindow(leveling ? SW_HIDE : SW_SHOW);
        CWnd* battleControls[] = { &battleStart_, &battleStop_, &battleSettings_, &battleRound_,
            &battleSides_[0], &battleSides_[1], &battleInfo_, &battleExperience_ };
        for (auto control : battleControls)
            control->ShowWindow(leveling ? SW_SHOW : SW_HIDE);
        bool received = view_.player.inBattle && view_.battle.received;
        int playerSide = view_.battle.playerSlot >= 10 && view_.battle.playerSlot < 20 ? 1 : 0;
        for (int side = 0; side < 2; ++side)
        {
            bool playerSideKnown = view_.battle.playerSlot >= 0 && view_.battle.playerSlot < 20;
            battleSides_[side].SetWindowText(playerSideKnown ?
                (side == 0 ? L"上方队员（敌方）" : L"下方队员（我方）") :
                (side == 0 ? L"上方队员" : L"下方队员"));
            int offset = (side == 0 ? 1 - playerSide : playerSide) * 10;
            battleSides_[side].Update(view_.battle, offset, received);
        }
        CString round = L"回合：—";
        if (view_.player.inBattle && view_.battle.round >= 0)
            round.Format(L"第%d回合", view_.battle.round);
        battleRound_.SetWindowText(round);
        battleTabs_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        battleSettings_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending && !view_.player.inBattle);
        if ((view_.encounterEnabled || view_.encounterPending || view_.player.inBattle) &&
            battleSettingsWindow_.GetSafeHwnd())
            battleSettingsWindow_.ShowWindow(SW_HIDE);
        battleStart_.EnableWindow(leveling && !view_.encounterEnabled && !view_.encounterPending);
        battleStop_.EnableWindow(leveling && (view_.encounterEnabled || view_.encounterPending));
        CString message = L"战斗提示：";
        if (view_.encounterPending || (!view_.player.inBattle && view_.encounterMessage == L"自动战斗已停止"))
            message += view_.encounterMessage.c_str();
        else
            message += view_.battleResult.message.empty() ? L"等待战斗" : view_.battleResult.message.c_str();
        battleInfo_.SetWindowText(message);
        CString experience;
        if (view_.battleResult.received)
            experience.Format(L"经验获得：本场 %u    累计 %llu",
                view_.battleResult.experience, static_cast<unsigned long long>(view_.battleResult.totalExperience));
        else
            experience.Format(L"经验获得：本场 —    累计 %llu",
                static_cast<unsigned long long>(view_.battleResult.totalExperience));
        battleExperience_.SetWindowText(experience);
    }

    void MainWindow::OnBattleSettings()
    {
        if (view_.screen != Screen::Game || view_.encounterEnabled || view_.encounterPending || view_.player.inBattle)
            return;
        battleSettingsWindow_.Show(this, view_.magic, session_);
    }

    void MainWindow::OnBattleStart()
    {
        if (view_.screen == Screen::Game && !view_.encounterEnabled && !view_.encounterPending)
        {
            if (battleSettingsWindow_.GetSafeHwnd())
                battleSettingsWindow_.ShowWindow(SW_HIDE);
            battleSceneWindow_.PrepareForEncounter();
            session_.SetEncounterEnabled(true);
        }
    }

    void MainWindow::OnBattleStop()
    {
        if (view_.screen == Screen::Game)
            session_.SetEncounterEnabled(false);
    }
}
