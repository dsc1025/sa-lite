#include "MainWindow.h"
#include "ChatWindow.h"
#include "MiniMap.h"
#include "CreateCharacterWindow.h"
#include "BattleGroup.h"
#include "BattleSettingsWindow.h"
#include "BattleSceneWindow.h"
#include "RolePage.h"
#include "AuctionPage.h"

namespace Client
{
    MainWindow::MainWindow()
        : miniMap_(std::make_unique<MiniMap>()),
          chatWindow_(std::make_unique<ChatWindow>()),
          createCharacterWindow_(std::make_unique<CreateCharacterWindow>()),
          battleSides_{ std::make_unique<BattleGroup>(), std::make_unique<BattleGroup>() },
          battleSettingsWindow_(std::make_unique<BattleSettingsWindow>()),
          captureSettingsWindow_(std::make_unique<CaptureSettingsWindow>()),
          battleSceneWindow_(std::make_unique<BattleSceneWindow>()),
          rolePage_(std::make_unique<RolePage>(view_, session_, footer_)),
          auctionPage_(std::make_unique<AuctionPage>(view_))
    {
        auctionPage_->request = [this](const Game::AuctionRequest& request) { return session_.RequestAuction(request); };
        background_.CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        white_.CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    }

    MainWindow::~MainWindow()
    {
        session_.Stop();
        session_.Join();
        if (updateProcess_) CloseHandle(updateProcess_);
    }
}
