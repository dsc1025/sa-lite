#include "MainWindow.h"
#include <stdexcept>

namespace Client
{
    void MainWindow::LoadLogin()
    {
        std::vector<wchar_t> path(32768);
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
            throw std::runtime_error("无法获取程序目录，不能读取登录配置");
        CString executable(path.data(), static_cast<int>(length));
        int separator = executable.ReverseFind(L'\\');
        if (separator < 0)
            throw std::runtime_error("无法获取程序目录，不能读取登录配置");
        configPath_ = executable.Left(separator + 1) + L"config.ini";
        battleSettingsWindow_.LoadSettings(configPath_, session_);
        wchar_t value[256];
        GetPrivateProfileStringW(L"Login", L"Server", L"", value, 256, configPath_);
        address_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Port", L"9065", value, 256, configPath_);
        port_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Account", L"", value, 256, configPath_);
        account_.SetWindowText(value);
        GetPrivateProfileStringW(L"Login", L"Password", L"", value, 256, configPath_);
        password_.SetWindowText(value);
    }

    void MainWindow::SaveLogin()
    {
        if (configPath_.IsEmpty())
            throw std::runtime_error("无法获取程序目录，不能保存登录配置");
        const wchar_t* keys[] = { L"Server", L"Port", L"Account", L"Password" };
        CEdit* edits[] = { &address_, &port_, &account_, &password_ };
        for (int i = 0; i < 4; ++i)
        {
            CString value;
            edits[i]->GetWindowText(value);
            if (!WritePrivateProfileStringW(L"Login", keys[i], value, configPath_))
                throw std::runtime_error("无法保存 exe 同目录的 config.ini，请检查目录写入权限");
        }
    }
}
