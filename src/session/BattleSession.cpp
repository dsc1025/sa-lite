#include "GameSession.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    void GameSession::SetEncounterEnabled(bool enabled)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || done_ || shared_.screen != Screen::Game || (enabled && shared_.petStoragePending))
                return;
            encounterRequested_ = enabled;
            if (enabled)
                requestedMap_ = -1;
            shared_.encounterPending = true;
            shared_.encounterMessage = enabled ? L"正在开始快速遇敌…" : L"正在停止遇敌…";
        }
        ::PostMessage(window_, message_, 0, 0);
    }

    void GameSession::UpdateEncounter(LoginClient& client)
    {
        bool requested;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return;
            requested = encounterRequested_;
            if (!requested && !client.IsEncounterEnabled() && !client.IsEncounterStopping() &&
                !shared_.encounterPending)
                return;
        }
        std::wstring message;
        bool failed = false;
        try
        {
            if (requested != client.IsEncounterEnabled())
                client.SetEncounterEnabled(requested);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stop_ || encounterRequested_ != requested)
                    return;
            }
            client.PollEncounter();
            message = !client.IsEncounterEnabled() ? L"遇敌已停止" :
                client.GetPlayerStatus().inBattle ? L"战斗中，结束后继续遇敌" : L"快速遇敌中";
        }
        catch (const std::exception& error)
        {
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
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || encounterRequested_ != requested)
                return;
            if (failed)
                encounterRequested_ = false;
            bool enabled = client.IsEncounterEnabled();
            changed = shared_.encounterEnabled != enabled || shared_.encounterPending ||
                shared_.encounterMessage != message;
            shared_.encounterEnabled = enabled;
            shared_.encounterPending = false;
            shared_.encounterMessage = message;
        }
        if (changed)
            Publish(Screen::Game, message, &client);
    }
}
