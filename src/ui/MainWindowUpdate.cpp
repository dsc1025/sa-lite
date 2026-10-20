#include "MainWindow.h"
#include "../AppInfo.h"

namespace Client
{
    bool MainWindow::UpdateInProgress()
    {
        if (!updateProcess_)
            return false;
        if (WaitForSingleObject(updateProcess_, 0) == WAIT_TIMEOUT)
            return true;
        CloseHandle(updateProcess_);
        updateProcess_ = nullptr;
        return false;
    }

    void MainWindow::OnUpdate()
    {
        if (view_.screen != Screen::Login)
            return;
        if (UpdateInProgress())
        {
            MessageBox(L"更新程序正在运行，请先完成或关闭更新窗口。", L"客户端更新", MB_OK | MB_ICONINFORMATION);
            return;
        }
        wchar_t executable[32768];
        DWORD length = GetModuleFileNameW(nullptr, executable, _countof(executable));
        if (!length || length >= _countof(executable))
        {
            MessageBox(L"无法取得客户端路径。", L"客户端更新", MB_OK | MB_ICONERROR);
            return;
        }
        auto directory = std::filesystem::path(executable).parent_path();
        auto updater = directory / L"SAUpdater.exe";
        std::wstring command = L"\"" + updater.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId()) +
            L" \"" + executable + L"\" \"" + AppInfo::Version + L"\"";
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(updater.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, directory.c_str(), &startup, &process))
        {
            CString message;
            message.Format(L"无法启动更新程序（错误 %lu）。请确认客户端目录中有 SAUpdater.exe，且未被安全软件阻止。", GetLastError());
            MessageBox(message, L"客户端更新", MB_OK | MB_ICONERROR);
            return;
        }
        CloseHandle(process.hThread);
        updateProcess_ = process.hProcess;
    }
}
