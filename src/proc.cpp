#include "proc.h"

#include <windows.h>

#include <mutex>
#include <thread>

namespace rl {

std::wstring utf8_to_wide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}

struct ProcessHandle::Impl {
    std::mutex m;
    HANDLE process = nullptr;
};

ProcessHandle::ProcessHandle() : impl_(std::make_unique<Impl>()) { }
ProcessHandle::~ProcessHandle() = default;

void ProcessHandle::kill()
{
    killed_ = true;
    std::lock_guard lock(impl_->m);
    if (impl_->process)
        TerminateProcess(impl_->process, 1);
}

static std::wstring quote_arg(const std::wstring &a)
{
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return a;
    std::wstring r = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < a.size() && a[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == a.size()) {
            r.append(backslashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"') {
            r.append(backslashes * 2 + 1, L'\\');
            r.push_back(L'"');
        } else {
            r.append(backslashes, L'\\');
            r.push_back(a[i]);
        }
    }
    r.push_back(L'"');
    return r;
}

namespace {
void pump(HANDLE pipe, const LineCallback &cb, std::string &collected)
{
    std::string buf;
    char chunk[8192];
    DWORD got = 0;
    auto flush_lines = [&](bool final) {
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            buf.erase(0, pos + 1);
            if (cb)
                cb(line);
        }
        if (final && !buf.empty()) {
            if (cb)
                cb(buf);
            buf.clear();
        }
    };
    while (ReadFile(pipe, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
        if (cb) {
            buf.append(chunk, got);
            flush_lines(false);
        } else {
            collected.append(chunk, got);
        }
    }
    flush_lines(true);
}
} // namespace

ProcessResult run_process(const std::string &exe, const std::vector<std::string> &args,
                          const LineCallback &on_stdout, const LineCallback &on_stderr,
                          ProcessHandle *handle)
{
    ProcessResult result;

    SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0))
        return result;
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                OPEN_EXISTING, 0, nullptr);

    std::wstring cmd = quote_arg(utf8_to_wide(exe));
    for (const auto &a : args)
        cmd += L" " + quote_arg(utf8_to_wide(a));

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul_in;
    si.hStdOutput = out_w;
    si.hStdError = err_w;

    PROCESS_INFORMATION pi {};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si,
                             &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (nul_in && nul_in != INVALID_HANDLE_VALUE)
        CloseHandle(nul_in);
    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        return result;
    }
    result.started = true;

    if (handle) {
        std::lock_guard lock(handle->impl_->m);
        handle->impl_->process = pi.hProcess;
        if (handle->killed_)
            TerminateProcess(pi.hProcess, 1);
    }

    std::thread t_err([&] { pump(err_r, on_stderr, result.err); });
    pump(out_r, on_stdout, result.out);
    t_err.join();

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exit_code = static_cast<int>(code);

    if (handle) {
        std::lock_guard lock(handle->impl_->m);
        handle->impl_->process = nullptr;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(out_r);
    CloseHandle(err_r);
    return result;
}

} // namespace rl
