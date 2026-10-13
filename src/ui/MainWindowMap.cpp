#include "MainWindow.h"

namespace Client
{
    namespace
    {
        CString MenuText(const std::wstring& text)
        {
            CString label(text.c_str());
            label.Replace(L"&", L"&&");
            return label;
        }

        bool AppendMapMenu(CMenu& menu, const MapNode& node, std::vector<const MapNode*>& commands)
        {
            CString label = MenuText(node.displayName);
            if (!node.children.empty())
            {
                CMenu submenu;
                if (!submenu.CreatePopupMenu())
                    return false;
                if (node.mapId >= 0)
                {
                    if (node.positions.empty())
                    {
                        if (!submenu.AppendMenu(MF_STRING | MF_GRAYED, 0, label))
                            return false;
                    }
                    else
                    {
                        commands.push_back(&node);
                        if (!submenu.AppendMenu(MF_STRING, static_cast<UINT_PTR>(commands.size()), label))
                            return false;
                    }
                    if (!submenu.AppendMenu(MF_SEPARATOR))
                        return false;
                }
                for (const auto& child : node.children)
                    if (!AppendMapMenu(submenu, child, commands))
                        return false;
                if (!menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(submenu.GetSafeHmenu()), label))
                    return false;
                submenu.Detach();
                return true;
            }
            if (node.mapId < 0 || node.positions.empty())
                return menu.AppendMenu(MF_STRING | MF_GRAYED, 0, label) != FALSE;
            commands.push_back(&node);
            return menu.AppendMenu(MF_STRING, static_cast<UINT_PTR>(commands.size()), label) != FALSE;
        }
    }

    void MainWindow::RefreshMapPositions()
    {
        bool hasPositions = selectedMap_ && !selectedMap_->positions.empty();
        mapPosition_.SetWindowText(hasPositions ? L"选择地点 ▾" :
            selectedMap_ ? L"暂无地点" : L"选择地点 ▾");
        mapPosition_.EnableWindow(hasPositions);
    }

    void MainWindow::OnMapRegion()
    {
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 1 || mapNodes_.empty())
            return;
        CMenu menu;
        if (!menu.CreatePopupMenu())
            return;
        std::vector<const MapNode*> commands;
        for (const auto& node : mapNodes_)
            if (!AppendMapMenu(menu, node, commands))
                return;
        CRect rect;
        mapRegion_.GetWindowRect(&rect);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            rect.left, rect.bottom, this);
        if (command == 0 || command > commands.size() ||
            view_.screen != Screen::Game || mainTabs_.GetCurSel() != 1)
            return;
        selectedMap_ = commands[command - 1];
        CString label;
        label.Format(L"%s ▾", selectedMap_->displayName.c_str());
        mapRegion_.SetWindowText(label);
        RefreshMapPositions();
    }

    void MainWindow::OnMapPosition()
    {
        const auto* map = selectedMap_;
        if (view_.screen != Screen::Game || mainTabs_.GetCurSel() != 1 ||
            !map || map->positions.empty())
            return;
        CMenu menu;
        if (!menu.CreatePopupMenu())
            return;
        for (std::size_t i = 0; i < map->positions.size(); ++i)
            if (!menu.AppendMenu(MF_STRING, static_cast<UINT_PTR>(i + 1), MenuText(map->positions[i].name)))
                return;
        CRect rect;
        mapPosition_.GetWindowRect(&rect);
        UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            rect.left, rect.bottom, this);
        if (command == 0 || command > map->positions.size() || selectedMap_ != map ||
            view_.screen != Screen::Game || mainTabs_.GetCurSel() != 1)
            return;
        const auto& target = map->positions[command - 1];
        CString label;
        label.Format(L"%s ▾", target.name.c_str());
        mapPosition_.SetWindowText(label);
        session_.RequestMove(map->mapId, target.x, target.y);
    }
}
