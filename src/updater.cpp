#include "updater.h"

#include <windows.h>

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "jsonutil.h"
#include "proc.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace rl::updater {

namespace {

constexpr char kLatestApi[] = "https://api.github.com/repos/GonzaloCN-Dev/rclone-lite/releases/latest";
constexpr char kAssetPrefix[] = "https://github.com/GonzaloCN-Dev/rclone-lite/releases/download/";
constexpr int64_t kMaxBody = 4 * 1024 * 1024;

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v = nullptr) : h(v) { }
    ~Handle()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

std::string win_error(const char *what)
{
    return std::string(what) + " (error " + std::to_string(GetLastError()) + ")";
}

// Streams a GET response to `sink`. `sink` returns false to abort.
std::string http_get(const std::string &url, bool json,
                     const std::function<bool(const char *, size_t)> &sink,
                     const std::function<void(int64_t)> &on_length = nullptr)
{
    std::wstring wurl = utf8_to_wide(url);
    URL_COMPONENTSW uc {};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        return "URL no válida";
    if (uc.nScheme != INTERNET_SCHEME_HTTPS)
        return "Solo se permiten conexiones https";

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    std::wstring agent = L"RcloneLite/" + utf8_to_wide(APP_VERSION);
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h)
        return win_error("No se pudo iniciar WinHTTP");
    WinHttpSetTimeouts(session.h, 10000, 10000, 20000, 30000);

    Handle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!conn.h)
        return win_error("No se pudo conectar");
    Handle req(WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req.h)
        return win_error("No se pudo crear la petición");

    if (json)
        WinHttpAddRequestHeaders(req.h, L"Accept: application/vnd.github+json", static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr))
        return win_error("Sin conexión con el servidor");

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
        return "El servidor respondió " + std::to_string(status);

    if (on_length) {
        wchar_t len[32] = {};
        DWORD ls = sizeof(len);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &ls,
                                WINHTTP_NO_HEADER_INDEX))
            on_length(_wtoi64(len));
    }

    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), static_cast<DWORD>(buf.size()), &got))
            return win_error("Se interrumpió la descarga");
        if (got == 0)
            break;
        if (!sink(buf.data(), got))
            return "Descarga cancelada";
    }
    return "";
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_hex64(const std::string &s)
{
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::vector<int> parse_version(const std::string &v)
{
    std::vector<int> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    int cur = 0;
    bool any = false;
    for (; i < v.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(v[i]))) {
            cur = cur * 10 + (v[i] - '0');
            any = true;
        } else if (v[i] == '.') {
            out.push_back(cur);
            cur = 0;
            any = false;
        } else {
            break; // pre-release suffix such as "-beta"
        }
    }
    if (any)
        out.push_back(cur);
    return out;
}

bool is_admin_install()
{
    wchar_t self[MAX_PATH], pf[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH))
        return false;
    for (const wchar_t *var : { L"ProgramFiles", L"ProgramFiles(x86)" }) {
        DWORD n = GetEnvironmentVariableW(var, pf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            continue;
        std::wstring root = pf;
        std::wstring me = self;
        if (me.size() > root.size() && _wcsnicmp(me.c_str(), root.c_str(), root.size()) == 0)
            return true;
    }
    return false;
}

} // namespace

bool is_newer(const std::string &remote, const std::string &local)
{
    auto a = parse_version(remote), b = parse_version(local);
    if (a.empty())
        return false;
    size_t n = std::max(a.size(), b.size());
    a.resize(n, 0);
    b.resize(n, 0);
    return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
}

Release fetch_latest()
{
    Release r;
    std::string body;
    std::string err = http_get(kLatestApi, true, [&](const char *p, size_t n) {
        if (static_cast<int64_t>(body.size() + n) > kMaxBody)
            return false;
        body.append(p, n);
        return true;
    });
    if (!err.empty()) {
        r.error = err;
        return r;
    }

    auto j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object()) {
        r.error = "Respuesta de GitHub no válida";
        return r;
    }
    if (jv(j, "draft", false) || jv(j, "prerelease", false)) {
        r.error = "No hay una versión estable publicada";
        return r;
    }
    r.tag = jv(j, "tag_name", "");
    r.version = (!r.tag.empty() && (r.tag[0] == 'v' || r.tag[0] == 'V')) ? r.tag.substr(1) : r.tag;
    r.notes = jv(j, "body", "");
    if (parse_version(r.version).empty()) {
        r.error = "La versión publicada no es válida";
        return r;
    }

    std::string sha_url;
    auto assets = j.find("assets");
    if (assets != j.end() && assets->is_array()) {
        for (const auto &a : *assets) {
            std::string name = jv(a, "name", "");
            std::string url = jv(a, "browser_download_url", "");
            if (url.rfind(kAssetPrefix, 0) != 0)
                continue;
            std::string lname = to_lower(name);
            if (lname.rfind("rclonelite-setup-", 0) == 0 && lname.size() > 4 && lname.substr(lname.size() - 4) == ".exe") {
                r.installer_name = name;
                r.installer_url = url;
                r.size = jv(a, "size", static_cast<int64_t>(0));
                std::string digest = to_lower(jv(a, "digest", ""));
                if (digest.rfind("sha256:", 0) == 0 && is_hex64(digest.substr(7)))
                    r.sha256 = digest.substr(7);
            } else if (lname.size() > 7 && lname.substr(lname.size() - 7) == ".sha256") {
                sha_url = url;
            }
        }
    }
    if (r.installer_url.empty()) {
        r.error = "La versión " + r.tag + " no incluye instalador";
        return r;
    }

    if (r.sha256.empty() && !sha_url.empty()) {
        std::string text;
        if (http_get(sha_url, false, [&](const char *p, size_t n) {
                if (text.size() + n > 4096)
                    return false;
                text.append(p, n);
                return true;
            }).empty()) {
            std::string first = to_lower(text.substr(0, 64));
            if (is_hex64(first))
                r.sha256 = first;
        }
    }
    if (r.sha256.empty()) {
        r.error = "La versión " + r.tag + " no publica una suma SHA-256 para verificar la descarga";
        return r;
    }
    r.ok = true;
    return r;
}

std::string download(const std::string &url, const std::string &path,
                     const std::function<void(int64_t, int64_t)> &progress)
{
    if (url.rfind(kAssetPrefix, 0) != 0)
        return "Origen de descarga no permitido";
    fs::path target = fs::path(utf8_to_wide(path));
    fs::path part = target;
    part += L".part";
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::remove(part, ec);

    std::string err;
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out)
            return "No se pudo crear el archivo temporal";
        int64_t total = 0, done = 0;
        err = http_get(
            url, false,
            [&](const char *p, size_t n) {
                out.write(p, static_cast<std::streamsize>(n));
                if (!out)
                    return false;
                done += static_cast<int64_t>(n);
                if (progress)
                    progress(done, total);
                return true;
            },
            [&](int64_t len) { total = len; });
        out.close();
        if (err.empty() && !out)
            err = "No se pudo escribir en el disco";
    }
    if (!err.empty()) {
        fs::remove(part, ec);
        return err;
    }
    fs::remove(target, ec);
    fs::rename(part, target, ec);
    if (ec)
        return "No se pudo guardar el instalador: " + ec.message();
    return "";
}

std::string sha256_file(const std::string &path)
{
    std::ifstream in(fs::path(utf8_to_wide(path)), std::ios::binary);
    if (!in)
        return "";
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return "";
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::vector<char> buf(256 * 1024);
        bool ok = true;
        while (in) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            auto got = static_cast<ULONG>(in.gcount());
            if (got && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), got, 0) < 0) {
                ok = false;
                break;
            }
        }
        UCHAR digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
            static const char *hex = "0123456789abcdef";
            for (UCHAR b : digest) {
                result += hex[b >> 4];
                result += hex[b & 15];
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

bool run_installer(const std::string &path)
{
    std::wstring cmd = L"\"" + utf8_to_wide(path) + L"\" /SILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CLOSEAPPLICATIONS /RELAUNCH=1 ";
    cmd += is_admin_install() ? L"/ALLUSERS" : L"/CURRENTUSER";

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    // Our own job object kills children on exit; the installer must outlive us, so break away.
    // Setup elevates itself when the install lives in Program Files.
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP,
                             nullptr, nullptr, &si, &pi);
    if (!ok)
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
} // namespace rl::updater
