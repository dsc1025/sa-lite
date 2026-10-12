#include "ui/MainWindow.h"
#include <uxtheme.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "UxTheme.lib")

namespace Client
{
    class ClientApp : public CWinApp
    {
        BOOL InitInstance() override
        {
            INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
            InitCommonControlsEx(&controls);
            CWinApp::InitInstance();
            SetThemeAppProperties(0);
            auto window = new MainWindow;
            if (!window->Create(nullptr, L"登录", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CRect(0, 0, 460, 330)))
                return FALSE;
            m_pMainWnd = window;
            window->CenterWindow();
            window->ShowWindow(m_nCmdShow);
            window->UpdateWindow();
            return TRUE;
        }
    };

    ClientApp application;
}
