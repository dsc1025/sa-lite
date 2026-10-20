#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <commctrl.h>
#include <winrt/Windows.Data.Json.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>

namespace
{
    constexpr UINT FinishedMessage = WM_APP + 1;
    constexpr UINT ProgressMessage = WM_APP + 2;
    constexpr wchar_t DownloadUrl[] = L"https://cso.tomsworld.com.cn/upgrade/sa/SAClient.exe";
    constexpr unsigned long long MaximumSize = 256ULL * 1024 * 1024;

    struct Error { std::wstring message; };
    struct Cancelled {};

    void Require(bool success, const wchar_t* message)
    {
        if (!success)
            throw Error{ message };
    }

    void CheckWindows(bool success, const wchar_t* message)
    {
        if (!success)
            throw Error{ std::wstring(message) + L"（系统错误 " + std::to_wstring(GetLastError()) + L"）" };
    }

    struct Handle
    {
        HANDLE value = nullptr;
        explicit Handle(HANDLE handle = nullptr) : value(handle) {}
        ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        void Close()
        {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = nullptr;
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
    };

    struct Internet
    {
        HINTERNET value;
        explicit Internet(HINTERNET handle) : value(handle)
        {
            CheckWindows(value != nullptr, L"无法初始化更新网络连接。");
        }
        ~Internet() { WinHttpCloseHandle(value); }
        Internet(const Internet&) = delete;
        Internet& operator=(const Internet&) = delete;
    };

    struct Manifest
    {
        std::wstring version, hash;
        unsigned long long size = 0;
    };

    struct State
    {
        HWND window = nullptr, label = nullptr, progress = nullptr;
        HANDLE parent = nullptr;
        DWORD parentId = 0;
        std::filesystem::path target;
        std::wstring currentVersion, error;
        Manifest manifest;
        std::thread worker;
        std::atomic<bool> cancel{ false }, committing{ false };
        std::mutex cancellationMutex;
        bool downloading = false, cancelled = false, installed = false;
    };

    void CheckCancelled(const State& state)
    {
        if (state.cancel.load())
            throw Cancelled{};
    }

    std::vector<unsigned> VersionParts(const std::wstring& text)
    {
        std::vector<unsigned> parts;
        unsigned part = 0;
        bool digit = false;
        for (wchar_t ch : text + L".")
        {
            if (ch == L'.')
            {
                Require(digit && parts.size() < 4, L"版本号格式不正确。");
                parts.push_back(part);
                part = 0;
                digit = false;
            }
            else
            {
                Require(ch >= L'0' && ch <= L'9', L"版本号格式不正确。");
                part = part * 10 + (ch - L'0');
                Require(part <= 65535, L"版本号数值超出范围。");
                digit = true;
            }
        }
        parts.resize(4);
        return parts;
    }

    void Receive(State& state, const wchar_t* path, unsigned long long limit,
        const std::function<void(const char*, DWORD)>& consume)
    {
        Internet session(WinHttpOpen(L"SAClient Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        CheckWindows(WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000) != FALSE, L"无法设置网络超时。");
        Internet connection(WinHttpConnect(session.value, L"cso.tomsworld.com.cn", INTERNET_DEFAULT_HTTPS_PORT, 0));
        Internet request(WinHttpOpenRequest(connection.value, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        CheckWindows(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) != FALSE,
            L"无法设置安全下载选项。");
        CheckCancelled(state);
        CheckWindows(WinHttpSendRequest(request.value, L"Cache-Control: no-cache\r\n", static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE, L"连接更新服务器失败，请检查网络。");
        CheckWindows(WinHttpReceiveResponse(request.value, nullptr) != FALSE, L"读取更新服务器响应失败。");
        DWORD status = 0, statusSize = sizeof(status);
        CheckWindows(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE, L"无法读取下载状态。");
        if (status != 200)
            throw Error{ L"更新服务器返回 HTTP " + std::to_wstring(status) + L"，请确认更新文件已发布。" };
        char buffer[65536];
        unsigned long long received = 0;
        for (;;)
        {
            CheckCancelled(state);
            DWORD count = 0;
            CheckWindows(WinHttpReadData(request.value, buffer, sizeof(buffer), &count) != FALSE, L"下载中断，请重新更新。");
            if (!count)
                break;
            received += count;
            Require(received <= limit, L"下载内容超出清单声明的大小。");
            consume(buffer, count);
        }
        CheckCancelled(state);
    }

    void ReadManifest(State& state)
    {
        std::string bytes;
        Receive(state, L"/upgrade/sa/version.json", 65536,
            [&](const char* data, DWORD count) { bytes.append(data, count); });
        if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0)
            bytes.erase(0, 3);
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        Require(count > 0, L"更新清单不是有效的 UTF-8 文件。");
        std::wstring text(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), count);
        auto json = winrt::Windows::Data::Json::JsonObject::Parse(text);
        state.manifest.version = json.GetNamedString(L"version").c_str();
        Require(std::wstring(json.GetNamedString(L"url").c_str()) == DownloadUrl, L"更新清单中的下载地址不正确。");
        double size = json.GetNamedNumber(L"size");
        Require(std::isfinite(size) && size > 0 && size <= MaximumSize && std::floor(size) == size,
            L"更新清单中的文件大小不正确。");
        state.manifest.size = static_cast<unsigned long long>(size);
        state.manifest.hash = json.GetNamedString(L"sha256").c_str();
        Require(state.manifest.hash.size() == 64, L"更新清单中的 SHA-256 格式不正确。");
        for (auto& ch : state.manifest.hash)
        {
            if (ch >= L'A' && ch <= L'F') ch += L'a' - L'A';
            Require((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'), L"更新清单中的 SHA-256 格式不正确。");
        }
        VersionParts(state.manifest.version);
    }

    class Sha256
    {
    public:
        Sha256()
        {
            if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                throw Error{ L"无法初始化 SHA-256。" };
        }
        ~Sha256()
        {
            if (hash_) BCryptDestroyHash(hash_);
            BCryptCloseAlgorithmProvider(algorithm_, 0);
        }
        std::wstring File(State& state, HANDLE file)
        {
            DWORD objectSize = 0, returned = 0;
            Require(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                sizeof(objectSize), &returned, 0) >= 0, L"无法初始化文件校验。");
            object_.resize(objectSize);
            Require(BCryptCreateHash(algorithm_, &hash_, object_.data(), objectSize, nullptr, 0, 0) >= 0, L"无法创建文件校验。");
            LARGE_INTEGER zero{};
            CheckWindows(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE, L"无法读取待更新文件。");
            unsigned char buffer[65536];
            for (;;)
            {
                CheckCancelled(state);
                DWORD count = 0;
                CheckWindows(ReadFile(file, buffer, sizeof(buffer), &count, nullptr) != FALSE, L"文件校验读取失败。");
                if (!count) break;
                Require(BCryptHashData(hash_, buffer, count, 0) >= 0, L"文件校验失败。");
            }
            unsigned char digest[32];
            Require(BCryptFinishHash(hash_, digest, sizeof(digest), 0) >= 0, L"无法完成文件校验。");
            constexpr wchar_t hex[] = L"0123456789abcdef";
            std::wstring result;
            for (auto byte : digest)
            {
                result += hex[byte >> 4];
                result += hex[byte & 15];
            }
            return result;
        }
    private:
        BCRYPT_ALG_HANDLE algorithm_ = nullptr;
        BCRYPT_HASH_HANDLE hash_ = nullptr;
        std::vector<unsigned char> object_;
    };

    BOOL CALLBACK CloseClient(HWND window, LPARAM parameter)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == static_cast<DWORD>(parameter) && GetWindow(window, GW_OWNER) == nullptr)
            PostMessageW(window, WM_CLOSE, 0, 0);
        return TRUE;
    }

    void DownloadAndApply(State& state)
    {
        Require(WaitForSingleObject(state.parent, 0) == WAIT_TIMEOUT, L"客户端已关闭，请重新打开客户端后更新。");
        wchar_t temporary[32768];
        CheckWindows(GetTempFileNameW(state.target.parent_path().c_str(), L"sau", 0, temporary) != 0,
            L"客户端目录不可写。请将客户端完整移到当前用户可写的目录后重试。");
        struct TemporaryFile
        {
            const wchar_t* path;
            ~TemporaryFile() { DeleteFileW(path); }
        } cleanup{ temporary };
        Handle file(CreateFileW(temporary, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        CheckWindows(file.value != INVALID_HANDLE_VALUE, L"无法创建下载文件，请检查目录权限。");
        unsigned long long received = 0;
        Receive(state, L"/upgrade/sa/SAClient.exe", state.manifest.size, [&](const char* data, DWORD count)
        {
            DWORD written = 0;
            CheckWindows(WriteFile(file.value, data, count, &written, nullptr) != FALSE && written == count,
                L"写入更新文件失败，请检查磁盘空间和目录权限。");
            received += count;
            PostMessageW(state.window, ProgressMessage, static_cast<WPARAM>(received * 100 / state.manifest.size), 0);
        });
        Require(received == state.manifest.size, L"下载文件不完整，请重新更新。");
        CheckWindows(FlushFileBuffers(file.value) != FALSE, L"保存更新文件失败。");
        PostMessageW(state.window, ProgressMessage, 100, 1);
        Sha256 hash;
        Require(hash.File(state, file.value) == state.manifest.hash, L"SHA-256 校验不一致，已停止更新，请稍后重试。");
        Require(WaitForSingleObject(state.parent, 0) == WAIT_TIMEOUT, L"客户端已关闭，更新已取消。");
        {
            std::lock_guard<std::mutex> lock(state.cancellationMutex);
            CheckCancelled(state);
            state.committing = true;
        }
        file.Close();
        PostMessageW(state.window, ProgressMessage, 100, 2);
        CheckWindows(EnumWindows(CloseClient, static_cast<LPARAM>(state.parentId)) != FALSE, L"无法通知客户端退出。");
        Require(WaitForSingleObject(state.parent, 30000) == WAIT_OBJECT_0,
            L"客户端未能在 30 秒内退出，未替换文件。请关闭客户端后重新更新。");
        auto backup = state.target;
        backup += L".bak";
        if (!ReplaceFileW(state.target.c_str(), temporary, backup.c_str(), 0, nullptr, nullptr))
        {
            DWORD error = GetLastError();
            if (GetFileAttributesW(state.target.c_str()) == INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
                MoveFileExW(backup.c_str(), state.target.c_str(), MOVEFILE_WRITE_THROUGH);
            throw Error{ L"替换客户端失败（系统错误 " + std::to_wstring(error) +
                L"）。请检查目录权限、安全软件及是否有其他客户端正在运行。若原文件缺失，可将 SAClient.exe.bak 改回 SAClient.exe。" };
        }
        state.installed = true;
        std::wstring command = L"\"" + state.target.wstring() + L"\"";
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        CheckWindows(CreateProcessW(state.target.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, state.target.parent_path().c_str(), &startup, &process) != FALSE,
            L"更新已安装，但自动启动失败，请手动启动客户端。旧版本保存在 SAClient.exe.bak。");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    void StartWorker(State& state)
    {
        state.worker = std::thread([&state]
        {
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
                if (state.downloading) DownloadAndApply(state);
                else ReadManifest(state);
            }
            catch (const Cancelled&) { state.cancelled = true; }
            catch (const Error& error) { state.error = error.message; }
            catch (const winrt::hresult_error& error) { state.error = L"读取更新清单失败：" + std::wstring(error.message().c_str()); }
            catch (...) { state.error = L"更新失败，请检查网络、磁盘空间和更新文件。"; }
            PostMessageW(state.window, FinishedMessage, 0, 0);
        });
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!state) return DefWindowProcW(window, message, wParam, lParam);
        if (message == WM_CLOSE)
        {
            std::lock_guard<std::mutex> lock(state->cancellationMutex);
            if (!state->committing.load())
            {
                state->cancel = true;
                SetWindowTextW(state->label, L"正在取消，请稍候…");
            }
            return 0;
        }
        if (message == ProgressMessage)
        {
            SendMessageW(state->progress, PBM_SETPOS, wParam, 0);
            if (state->cancel.load()) return 0;
            std::wstring text = lParam == 2 ? L"正在退出客户端、替换文件并重新启动…" :
                lParam == 1 ? L"下载完成，正在校验 SHA-256…" : L"正在下载更新：" + std::to_wstring(wParam) + L"%";
            SetWindowTextW(state->label, text.c_str());
            return 0;
        }
        if (message == FinishedMessage)
        {
            state->worker.join();
            if (state->cancel.load() || state->cancelled)
            {
                DestroyWindow(window);
                return 0;
            }
            if (!state->error.empty())
                MessageBoxW(window, state->error.c_str(), L"客户端更新", MB_OK | MB_ICONERROR);
            else if (!state->downloading)
            {
                if (VersionParts(state->manifest.version) <= VersionParts(state->currentVersion))
                    MessageBoxW(window, L"当前已是最新版本。", L"客户端更新", MB_OK | MB_ICONINFORMATION);
                else
                {
                    auto question = L"当前版本：" + state->currentVersion + L"\n最新版本：" + state->manifest.version +
                        L"\n\n是否下载更新？校验成功后将自动关闭并重新启动客户端。";
                    if (MessageBoxW(window, question.c_str(), L"客户端更新", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        state->downloading = true;
                        SetWindowTextW(state->label, L"正在检查目录权限并下载更新…");
                        try { StartWorker(*state); }
                        catch (...) { MessageBoxW(window, L"无法启动下载线程。", L"客户端更新", MB_OK | MB_ICONERROR); DestroyWindow(window); }
                        return 0;
                    }
                }
            }
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try
    {
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        Require(arguments != nullptr, L"无法读取更新参数。");
        struct Arguments { LPWSTR* value; ~Arguments() { LocalFree(value); } } cleanup{ arguments };
        Require(count == 4, L"请从客户端登录界面点击“更新”按钮。");
        std::wstring pidText = arguments[1];
        Require(!pidText.empty() && pidText.find_first_not_of(L"0123456789") == std::wstring::npos, L"更新参数不正确。");
        unsigned long parentId = std::stoul(pidText);
        State state;
        state.parentId = parentId;
        state.target = std::filesystem::path(arguments[2]);
        state.currentVersion = arguments[3];
        VersionParts(state.currentVersion);
        Require(state.target.is_absolute(), L"客户端路径必须是绝对路径。");
        Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, state.parentId));
        CheckWindows(parent.value != nullptr, L"无法取得客户端进程，请重新启动客户端。");
        state.parent = parent.value;
        wchar_t image[32768];
        DWORD length = _countof(image);
        CheckWindows(QueryFullProcessImageNameW(parent.value, 0, image, &length) != FALSE, L"无法核对客户端路径。");
        Require(std::filesystem::equivalent(state.target, image), L"更新目标与客户端进程不一致。");
        auto lockPath = state.target.parent_path() / L".sa-update.lock";
        Handle updateLock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
        if (updateLock.value == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            throw Error{ error == ERROR_SHARING_VIOLATION ? L"此目录已有更新程序正在运行。" :
                L"客户端目录不可写，无法自动更新。请将客户端完整移到当前用户可写的目录后重试。" };
        }
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&controls);
        WNDCLASSW windowClass{};
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.lpszClassName = L"SAClientUpdateWindow";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        CheckWindows(RegisterClassW(&windowClass) != 0, L"无法创建更新窗口。");
        state.window = CreateWindowExW(0, windowClass.lpszClassName, L"客户端更新",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN) - 480) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - 150) / 2, 480, 150, nullptr, nullptr, instance, &state);
        CheckWindows(state.window != nullptr, L"无法创建更新窗口。");
        state.label = CreateWindowExW(0, L"STATIC", L"正在检查更新…", WS_CHILD | WS_VISIBLE,
            16, 20, 440, 36, state.window, nullptr, instance, nullptr);
        state.progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
            16, 58, 440, 22, state.window, nullptr, instance, nullptr);
        CheckWindows(state.label != nullptr && state.progress != nullptr, L"无法创建更新进度控件。");
        SendMessageW(state.label, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        ShowWindow(state.window, SW_SHOW);
        StartWorker(state);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        state.cancel = true;
        if (state.worker.joinable()) state.worker.join();
        return state.installed ? 0 : 1;
    }
    catch (const Error& error) { MessageBoxW(nullptr, error.message.c_str(), L"客户端更新", MB_OK | MB_ICONERROR); }
    catch (...) { MessageBoxW(nullptr, L"无法启动客户端更新，请检查更新参数和系统环境。", L"客户端更新", MB_OK | MB_ICONERROR); }
    return 1;
}
