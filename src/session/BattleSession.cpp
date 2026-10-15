#include "../LoginClient.h"
#include "GameSessionState.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    void GameSession::SetHealingSettings(const Game::HealingSettings& settings)
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        state_->healing_ = settings;
    }

    void GameSession::SetCaptureSettings(const Game::CaptureSettings& settings)
    {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        state_->capture_ = settings;
    }

    void GameSession::SetEncounterEnabled(bool enabled, bool capture)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->done_ || state_->shared_.screen != Screen::Game || (enabled && (state_->shared_.petStoragePending || state_->shared_.itemPending)))
                return;
            if (enabled && !state_->encounterRequested_)
                state_->captureEnabled_ = capture;
            state_->encounterRequested_ = enabled;
            if (enabled)
                state_->requestedMap_ = -1;
            state_->shared_.encounterPending = true;
            state_->shared_.encounterMessage = enabled ? L"正在开始原地遇敌…" :
                state_->shared_.player.inBattle ? L"等待战斗结束，结束后停止自动战斗" : L"正在停止自动战斗…";
            state_->shared_.message = state_->shared_.encounterMessage;
        }
        ::PostMessage(state_->window_, state_->message_, 0, 0);
    }

    void GameSession::UpdateEncounter(LoginClient& client)
    {
        bool requested;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_) return;
            requested = state_->encounterRequested_;
            if (!requested && !client.IsEncounterEnabled() && !client.IsEncounterStopping() &&
                !state_->shared_.encounterPending)
                return;
        }
        std::wstring message;
        bool failed = false;
        try
        {
            if (requested != client.IsEncounterEnabled())
                client.SetEncounterEnabled(requested);
            {
                std::lock_guard<std::mutex> lock(state_->mutex_);
                if (state_->stop_ || state_->encounterRequested_ != requested)
                    return;
            }
            client.PollEncounter();
            message = !requested && client.GetPlayerStatus().inBattle ? L"等待战斗结束，结束后停止自动战斗" :
                client.IsEncounterStopping() ? L"等待开战请求完成，完成后停止自动战斗" :
                !client.IsEncounterEnabled() ? L"自动战斗已停止" :
                client.GetPlayerStatus().inBattle ? L"战斗中，结束后继续遇敌" :
                client.IsEncounterWaiting() ? L"等待服务器原地开战" : L"原地遇敌中";
        }
        catch (const std::exception& error)
        {
            if (!client.IsConnected()) throw;
            failed = true;
            message = TextEncoding::ErrorText(error.what());
            try { client.SetEncounterEnabled(false); }
            catch (const std::exception& stopError)
            {
                message += L"；停止确认失败：" + TextEncoding::ErrorText(stopError.what());
                client.Disconnect();
            }
        }
        bool changed;
        {
            std::lock_guard<std::mutex> lock(state_->mutex_);
            if (state_->stop_ || state_->encounterRequested_ != requested)
                return;
            if (failed)
                state_->encounterRequested_ = false;
            bool enabled = client.IsEncounterEnabled();
            bool pending = !requested && (client.GetPlayerStatus().inBattle || client.IsEncounterStopping());
            changed = state_->shared_.encounterEnabled != enabled || state_->shared_.encounterPending != pending ||
                state_->shared_.encounterMessage != message;
            state_->shared_.encounterEnabled = enabled;
            state_->shared_.encounterPending = pending;
            state_->shared_.encounterMessage = message;
        }
        if (changed)
            Publish(Screen::Game, message, &client);
    }
}
