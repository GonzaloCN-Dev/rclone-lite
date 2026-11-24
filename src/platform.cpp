#include "platform.h"

#include <windows.h>

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "proc.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")

namespace rl::plat {

namespace {
struct ComInit {
    ComInit() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComInit()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    HRESULT hr;
};
} // namespace

std::string pick_folder(const std::string &title)
{
    ComInit com;
    IFileOpenDialog *dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(utf8_to_wide(title).c_str());
    std::string result;
    if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = wide_to_utf8(p);
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::string open_file_dialog(const std::string &title, const wchar_t *filter)
{
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    std::wstring t = utf8_to_wide(title);
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return {};
    return wide_to_utf8(buf);
}

std::string save_file_dialog(const std::string &title, const std::string &default_name,
                             const wchar_t *filter, const wchar_t *default_ext)
{
    wchar_t buf[MAX_PATH * 2] = {};
    std::wstring dn = utf8_to_wide(default_name);
    wcsncpy_s(buf, dn.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.lpstrDefExt = default_ext;
    std::wstring t = utf8_to_wide(title);
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn))
        return {};
    return wide_to_utf8(buf);
}

void open_url(const std::string &url)
{
    ShellExecuteW(nullptr, L"open", utf8_to_wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_path(const std::string &path)
{
    std::wstring p = utf8_to_wide(path);
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool set_clipboard(const std::string &utf8)
{
    std::wstring w = utf8_to_wide(utf8);
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        CloseClipboard();
        return false;
    }
    memcpy(GlobalLock(h), w.c_str(), bytes);
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return true;
}

std::string temp_dir()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH)
        return ".";
    std::wstring s(buf, n);
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/'))
        s.pop_back();
    return wide_to_utf8(s);
}

bool winfsp_installed()
{
    wchar_t pf[MAX_PATH];
    const wchar_t *vars[] = { L"ProgramFiles(x86)", L"ProgramFiles" };
    for (auto v : vars) {
        if (GetEnvironmentVariableW(v, pf, MAX_PATH)) {
            std::filesystem::path p = std::filesystem::path(pf) / L"WinFsp" / L"bin" / L"winfsp-x64.dll";
            std::error_code ec;
            if (std::filesystem::exists(p, ec))
                return true;
        }
    }
    return false;
}

bool path_exists(const std::string &path)
{
    std::wstring p = utf8_to_wide(path);
    if (p.size() == 2 && p[1] == L':')
        p += L"\\";
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<char> free_drive_letters()
{
    DWORD mask = GetLogicalDrives();
    std::vector<char> out;
    for (char c = 'Z'; c >= 'D'; --c)
        if (!(mask & (1u << (c - 'A'))))
            out.push_back(c);
    return out;
}

std::vector<std::string> local_drives()
{
    std::vector<std::string> out;
    DWORD mask = GetLogicalDrives();
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (!(mask & (1u << (c - 'A'))))
            continue;
        std::wstring root = { static_cast<wchar_t>(c), L':', L'\\' };
        UINT t = GetDriveTypeW(root.c_str());
        if (t == DRIVE_FIXED || t == DRIVE_REMOVABLE || t == DRIVE_REMOTE)
            out.push_back(std::string(1, c) + ":");
    }
    return out;
}

std::string user_profile()
{
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH))
        return wide_to_utf8(buf);
    return {};
}

namespace {

std::mutex g_log_mutex;

std::filesystem::path log_path()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::filesystem::path dir = n && n < MAX_PATH ? std::filesystem::path(buf) / L"RcloneLite"
                                                  : std::filesystem::temp_directory_path() / L"RcloneLite";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / L"errors.log";
}

void write_log_line(const std::string &message)
{
    std::lock_guard lock(g_log_mutex);
    auto path = log_path();
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > 512 * 1024)
        std::filesystem::remove(path, ec);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[48];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d  ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (f)
        f << stamp << message << "\r\n";
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "FALLO FATAL: excepción 0x%08lX en la dirección %p",
                  ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0UL,
                  ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);
    write_log_line(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void log_error(const std::string &message)
{
    try {
        write_log_line(message);
    } catch (...) {
    }
}

void install_crash_handlers()
{
    SetUnhandledExceptionFilter(unhandled_filter);
    std::set_terminate([] {
        std::string what = "terminate() sin excepción activa";
        if (auto ep = std::current_exception()) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                what = std::string("excepción no capturada: ") + e.what();
            } catch (...) {
                what = "excepción no capturada de tipo desconocido";
            }
        }
        write_log_line("FALLO FATAL: " + what);
        std::abort();
    });
}

void kill_children_on_exit()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
    // BREAKAWAY_OK lets the updater start the installer outside the job so it survives our exit.
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    AssignProcessToJobObject(job, GetCurrentProcess());
}

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_SHOWAPP = WM_APP + 2;
constexpr wchar_t kTrayClass[] = L"RcloneLiteTrayWnd";

HWND g_tray_hwnd = nullptr;
NOTIFYICONDATAW g_nid {};
UINT g_taskbar_created = 0;
std::function<void()> g_on_open;
std::function<void()> g_on_quit;

LRESULT CALLBACK tray_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TRAY) {
        switch (LOWORD(l)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (g_on_open)
                g_on_open();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Abrir Rclone Lite");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, L"Salir");
            SetMenuDefaultItem(menu, 1, FALSE);
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(h);
            UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr));
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == 1 && g_on_open)
                g_on_open();
            else if (cmd == 2 && g_on_quit)
                g_on_quit();
            break;
        }
        }
        return 0;
    }
    if (m == WM_SHOWAPP) {
        if (g_on_open)
            g_on_open();
        return 0;
    }
    if (g_taskbar_created && m == g_taskbar_created) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

bool signal_existing_instance()
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RcloneLite.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(kTrayClass, nullptr))
            PostMessageW(w, WM_SHOWAPP, 0, 0);
        return true;
    }
    return false;
}

bool tray_create(const std::string &tooltip, std::function<void()> on_open, std::function<void()> on_quit)
{
    g_on_open = std::move(on_open);
    g_on_quit = std::move(on_quit);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc {};
    wc.lpfnWndProc = tray_proc;
    wc.hInstance = inst;
    wc.lpszClassName = kTrayClass;
    RegisterClassW(&wc);
    g_tray_hwnd = CreateWindowExW(0, kTrayClass, L"RcloneLiteTray", 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_tray_hwnd)
        return false;
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_tray_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), 0));
    wcsncpy_s(g_nid.szTip, utf8_to_wide(tooltip).c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

void tray_destroy()
{
    if (!g_tray_hwnd)
        return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyWindow(g_tray_hwnd);
    g_tray_hwnd = nullptr;
}

void tray_notify(const std::string &title, const std::string &text, bool error)
{
    if (!g_tray_hwnd)
        return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, utf8_to_wide(title).c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, utf8_to_wide(text).c_str(), _TRUNCATE);
    n.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

bool app_is_foreground()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void focus_main_window()
{
    struct Ctx {
        HWND found = nullptr;
    } ctx;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != GetCurrentProcessId() || h == g_tray_hwnd || GetWindow(h, GW_OWNER))
                return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 64);
            if (wcscmp(cls, kTrayClass) == 0 || !IsWindowVisible(h))
                return TRUE;
            reinterpret_cast<Ctx *>(lp)->found = h;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found) {
        if (IsIconic(ctx.found))
            ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
    }
}

bool read_text_file(const std::string &path, std::string &out)
{
    std::ifstream f(std::filesystem::path(utf8_to_wide(path)), std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text_file(const std::string &path, const std::string &data)
{
    std::ofstream f(std::filesystem::path(utf8_to_wide(path)), std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f << data;
    return static_cast<bool>(f);
}

} // namespace rl::plat
