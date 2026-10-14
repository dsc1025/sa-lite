#include "MainWindow.h"
#include "MiniMap.h"
#include "CreateCharacterWindow.h"
#include "BattleGroup.h"
#include "BattleSettingsWindow.h"
#include "BattleSceneWindow.h"
#include "RolePage.h"

namespace Client
{
    MainWindow::MainWindow()
        : miniMap_(std::make_unique<MiniMap>()),
          createCharacterWindow_(std::make_unique<CreateCharacterWindow>()),
          battleSides_{ std::make_unique<BattleGroup>(), std::make_unique<BattleGroup>() },
          battleSettingsWindow_(std::make_unique<BattleSettingsWindow>()),
          battleSceneWindow_(std::make_unique<BattleSceneWindow>()),
          rolePage_(std::make_unique<RolePage>(view_, session_, footer_))
    {
        background_.CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        white_.CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    }

    MainWindow::~MainWindow()
    {
        session_.Stop();
        session_.Join();
    }
}
