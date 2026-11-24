#include "rclone.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
#include "jsonutil.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rl {

namespace {

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string error_text(const ProcessResult &r)
{
    if (!r.started)
        return "No se pudo ejecutar rclone.";
    std::string t = trim(r.err);
    if (t.empty())
        t = trim(r.out);
    if (t.empty())
        return "rclone terminó con código " + std::to_string(r.exit_code);
    // Keep the last few lines: rclone prints the useful part at the end.
    std::vector<std::string> lines;
    std::istringstream is(t);
    for (std::string l; std::getline(is, l);)
        if (!trim(l).empty())
            lines.push_back(trim(l));
    std::string out;
    size_t start = lines.size() > 3 ? lines.size() - 3 : 0;
    for (size_t i = start; i < lines.size(); ++i)
        out += (out.empty() ? "" : "\n") + lines[i];
    if (out.size() > 500)
        out.resize(500);
    return out;
}

std::vector<std::string> split_words(const std::string &s)
{
    std::vector<std::string> v;
    std::istringstream is(s);
    for (std::string w; is >> w;)
        v.push_back(w);
    return v;
}

Stats parse_stats(const json &j)
{
    Stats s;
    auto num = [&](const char *k, double def = 0) {
        auto it = j.find(k);
        return (it != j.end() && it->is_number()) ? it->get<double>() : def;
    };
    s.bytes = static_cast<int64_t>(num("bytes"));
    s.total_bytes = static_cast<int64_t>(num("totalBytes"));
    s.speed = num("speed");
    s.eta = num("eta", -1);
    s.transfers = static_cast<int64_t>(num("transfers"));
    s.total_transfers = static_cast<int64_t>(num("totalTransfers"));
    s.checks = static_cast<int64_t>(num("checks"));
    s.errors = static_cast<int64_t>(num("errors"));
    s.elapsed = num("elapsedTime");
    if (j.contains("lastError") && j["lastError"].is_string())
        s.last_error = j["lastError"].get<std::string>();
    if (j.contains("transferring") && j["transferring"].is_array()) {
        for (const auto &t : j["transferring"]) {
            TransferFile f;
            f.name = jv(t, "name", "");
            f.size = jv(t, "size", static_cast<int64_t>(0));
            f.bytes = jv(t, "bytes", static_cast<int64_t>(0));
            f.percent = jv(t, "percentage", 0.0);
            f.speed = jv(t, "speed", 0.0);
            s.active.push_back(std::move(f));
        }
    }
    return s;
}

std::string write_filter_file(const std::string &rules)
{
    std::string trimmed = trim(rules);
    if (trimmed.empty())
        return {};
    return write_temp_lines({ trimmed });
}

} // namespace

std::string write_temp_lines(const std::vector<std::string> &lines)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(tmp, L"rlf", 0, name))
        return {};
    std::ofstream f(fs::path(name), std::ios::binary | std::ios::trunc);
    if (!f)
        return {};
    for (const auto &l : lines)
        f << l << "\n";
    return wide_to_utf8(name);
}

void remove_temp_file(const std::string &path)
{
    if (path.empty())
        return;
    std::error_code ec;
    fs::remove(fs::path(utf8_to_wide(path)), ec);
}

OpResult Rclone::delete_listed(const std::string &root,
                               const std::vector<std::string> &rel_paths) const
{
    std::string list = write_temp_lines(rel_paths);
    if (list.empty())
        return { false, "No se pudo crear la lista temporal." };
    auto r = run_process(exe_, { "delete", root, "--files-from-raw=" + list });
    remove_temp_file(list);
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

std::string join_path(const std::string &base, const std::string &name)
{
    if (base.empty())
        return name;
    char last = base.back();
    if (last == ':' || last == '/' || last == '\\')
        return base + name;
    return base + "/" + name;
}

std::string parent_path(const std::string &path)
{
    std::string s = path;
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\') && s[s.size() - 2] != ':')
        s.pop_back();
    size_t colon = s.find(':');
    size_t pos = s.find_last_of("/\\");
    if (pos == std::string::npos) {
        if (colon != std::string::npos)
            return colon == 1 ? s.substr(0, 2) + "/" : s.substr(0, colon + 1);
        return s;
    }
    if (colon != std::string::npos && pos <= colon + 1)
        return s.substr(0, pos + 1);
    return s.substr(0, pos);
}

Rclone::Rclone() : exe_(auto_detect()) { }

std::string Rclone::auto_detect()
{
    std::vector<fs::path> candidates;

    wchar_t self[MAX_PATH];
    if (GetModuleFileNameW(nullptr, self, MAX_PATH))
        candidates.push_back(fs::path(self).parent_path() / L"rclone.exe");

    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"rclone.exe", nullptr, MAX_PATH, found, nullptr))
        candidates.push_back(found);

    wchar_t local[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
        fs::path pk = fs::path(local) / L"Microsoft" / L"WinGet";
        candidates.push_back(pk / L"Links" / L"rclone.exe");
        std::error_code ec;
        for (fs::directory_iterator it(pk / L"Packages", ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->path().filename().wstring().rfind(L"Rclone.Rclone", 0) != 0)
                continue;
            for (fs::recursive_directory_iterator r(it->path(), ec), e2; !ec && r != e2;
                 r.increment(ec)) {
                if (r->path().filename() == L"rclone.exe")
                    candidates.push_back(r->path());
            }
        }
    }
    candidates.push_back(L"C:\\Program Files\\rclone\\rclone.exe");
    candidates.push_back(L"C:\\rclone\\rclone.exe");

    for (const auto &c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
            return wide_to_utf8(c.wstring());
    }
    return {};
}

std::string Rclone::version() const
{
    if (exe_.empty())
        return {};
    auto r = run_process(exe_, { "version" });
    std::istringstream is(r.out);
    std::string line;
    std::getline(is, line);
    return trim(line);
}

std::vector<RemoteInfo> Rclone::list_remotes() const
{
    std::vector<RemoteInfo> out;
    if (exe_.empty())
        return out;
    auto r = run_process(exe_, { "config", "dump" });
    if (r.exit_code != 0)
        return out;
    auto j = json::parse(r.out, nullptr, false);
    if (!j.is_object())
        return out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        RemoteInfo ri;
        ri.name = it.key();
        if (it->is_object())
            ri.type = jv(*it, "type", "");
        out.push_back(std::move(ri));
    }
    return out;
}

std::map<std::string, std::string> Rclone::remote_options(const std::string &name) const
{
    std::map<std::string, std::string> out;
    auto r = run_process(exe_, { "config", "dump" });
    auto j = json::parse(r.out, nullptr, false);
    if (!j.is_object() || !j.contains(name) || !j[name].is_object())
        return out;
    for (auto it = j[name].begin(); it != j[name].end(); ++it)
        out[it.key()] = it->is_string() ? it->get<std::string>() : it->dump();
    return out;
}

OpResult Rclone::create_remote(const std::string &name, const std::string &type,
                               const std::vector<std::pair<std::string, std::string>> &params,
                               ProcessHandle *handle) const
{
    std::vector<std::string> args { "config", "create", name, type };
    for (const auto &[k, v] : params)
        if (!v.empty())
            args.push_back(k + "=" + v);
    auto r = run_process(exe_, args, {}, {}, handle);
    if (r.exit_code == 0)
        return { true, {} };
    return { false, error_text(r) };
}

OpResult Rclone::delete_remote(const std::string &name) const
{
    auto r = run_process(exe_, { "config", "delete", name });
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

OpResult Rclone::reconnect_remote(const std::string &name, ProcessHandle *handle) const
{
    auto r = run_process(exe_, { "config", "reconnect", name + ":", "--auto-confirm" }, {}, {},
                         handle);
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

std::optional<std::vector<Entry>> Rclone::list(const std::string &spec, std::string *error) const
{
    auto r = run_process(exe_, { "lsjson", "--no-mimetype", spec });
    auto j = json::parse(r.out, nullptr, false);
    // rclone may exit non-zero on a few unreadable entries yet still list the rest.
    if (!j.is_array() || (r.exit_code != 0 && j.empty())) {
        if (error)
            *error = r.exit_code != 0 ? error_text(r) : "Respuesta de rclone no válida.";
        return std::nullopt;
    }
    std::vector<Entry> out;
    out.reserve(j.size());
    for (const auto &e : j) {
        Entry en;
        en.name = jv(e, "Name", "");
        en.is_dir = jv(e, "IsDir", false);
        en.size = en.is_dir ? 0 : jv(e, "Size", static_cast<int64_t>(0));
        if (en.size < 0)
            en.size = 0;
        en.modtime = jv(e, "ModTime", "");
        out.push_back(std::move(en));
    }
    return out;
}

OpResult Rclone::mkdir(const std::string &spec) const
{
    auto r = run_process(exe_, { "mkdir", spec });
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

OpResult Rclone::delete_file(const std::string &spec) const
{
    auto r = run_process(exe_, { "deletefile", spec });
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

OpResult Rclone::purge(const std::string &spec) const
{
    auto r = run_process(exe_, { "purge", spec });
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

OpResult Rclone::rename(const std::string &from, const std::string &to, bool is_dir) const
{
    (void)is_dir;
    auto r = run_process(exe_, { "moveto", from, to });
    return { r.exit_code == 0, r.exit_code == 0 ? "" : error_text(r) };
}

std::string Rclone::link(const std::string &spec) const
{
    auto r = run_process(exe_, { "link", spec });
    return r.exit_code == 0 ? trim(r.out) : std::string();
}

std::optional<Rclone::About> Rclone::about(const std::string &spec) const
{
    auto r = run_process(exe_, { "about", spec, "--json" });
    if (r.exit_code != 0)
        return std::nullopt;
    auto j = json::parse(r.out, nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    About a;
    a.total = jv(j, "total", static_cast<int64_t>(-1));
    a.used = jv(j, "used", static_cast<int64_t>(-1));
    a.free = jv(j, "free", static_cast<int64_t>(-1));
    return a;
}

std::vector<std::string> Rclone::common_flags(const TransferOptions &opt,
                                              std::string *filter_file) const
{
    std::vector<std::string> a;
    a.push_back("--transfers=" + std::to_string(std::max(1, opt.transfers)));
    a.push_back("--checkers=" + std::to_string(std::max(1, opt.checkers)));
    if (opt.dry_run)
        a.push_back("--dry-run");
    if (opt.checksum)
        a.push_back("--checksum");
    if (opt.size_only)
        a.push_back("--size-only");
    if (opt.ignore_existing)
        a.push_back("--ignore-existing");
    if (!opt.files_from.empty())
        a.push_back("--files-from-raw=" + opt.files_from);
    if (!trim(opt.bwlimit).empty())
        a.push_back("--bwlimit=" + trim(opt.bwlimit));
    if (filter_file) {
        *filter_file = write_filter_file(opt.filters);
        if (!filter_file->empty())
            a.push_back("--filter-from=" + *filter_file);
    }
    for (auto &w : split_words(opt.extra_flags))
        a.push_back(w);
    return a;
}

namespace {

OpResult run_transfer(const Rclone &rc, std::vector<std::string> args, const TransferOptions &opt,
                      const std::function<void(const Stats &)> &on_stats,
                      const std::function<void(const std::string &, const std::string &)> &on_log,
                      ProcessHandle *handle)
{
    std::string filter_file;
    for (auto &f : rc.common_flags(opt, &filter_file))
        args.push_back(std::move(f));
    args.insert(args.end(), { "--use-json-log", "-v", "--stats=500ms", "--stats-log-level=NOTICE" });

    std::string last_error;
    auto on_line = [&](const std::string &line) {
        if (line.empty())
            return;
        auto j = json::parse(line, nullptr, false);
        if (!j.is_object()) {
            if (on_log)
                on_log("info", line);
            return;
        }
        if (j.contains("stats") && j["stats"].is_object()) {
            if (on_stats)
                on_stats(parse_stats(j["stats"]));
            return;
        }
        std::string level = jv(j, "level", "info");
        std::string msg = jv(j, "msg", "");
        std::string object = jv(j, "object", "");
        if (!object.empty() && msg.find(object) == std::string::npos)
            msg = object + ": " + msg;
        if (level == "error")
            last_error = msg;
        if (on_log)
            on_log(level, msg);
    };

    auto r = run_process(rc.executable(), args, {}, on_line, handle);
    if (!filter_file.empty()) {
        std::error_code ec;
        fs::remove(fs::path(utf8_to_wide(filter_file)), ec);
    }
    if (!r.started)
        return { false, "No se pudo ejecutar rclone." };
    if (handle && handle->killed())
        return { false, "Cancelado por el usuario." };
    if (r.exit_code == 0)
        return { true, {} };
    return { false, last_error.empty() ? "rclone terminó con código " + std::to_string(r.exit_code)
                                       : last_error };
}

} // namespace

OpResult Rclone::transfer(const std::string &src, const std::string &dst,
                          const TransferOptions &opt,
                          const std::function<void(const Stats &)> &on_stats,
                          const std::function<void(const std::string &, const std::string &)> &on_log,
                          ProcessHandle *handle) const
{
    std::string mode = opt.mode == "sync" || opt.mode == "move" ? opt.mode : "copy";
    return run_transfer(*this, { mode, src, dst }, opt, on_stats, on_log, handle);
}

OpResult Rclone::transfer_file(const std::string &src, const std::string &dst, bool move,
                               const TransferOptions &opt,
                               const std::function<void(const Stats &)> &on_stats,
                               const std::function<void(const std::string &, const std::string &)> &on_log,
                               ProcessHandle *handle) const
{
    return run_transfer(*this, { move ? "moveto" : "copyto", src, dst }, opt, on_stats, on_log,
                        handle);
}

OpResult Rclone::compare(const std::string &left, const std::string &right,
                         const TransferOptions &opt, std::vector<CompareItem> &out,
                         ProcessHandle *handle) const
{
    TransferOptions o;
    o.checkers = opt.checkers;
    o.filters = opt.filters;
    o.size_only = !opt.checksum;
    std::string filter_file;
    std::vector<std::string> args { "check", left, right, "--combined", "-" };
    args.push_back("--checkers=" + std::to_string(std::max(1, o.checkers)));
    if (o.size_only)
        args.push_back("--size-only");
    filter_file = write_filter_file(o.filters);
    if (!filter_file.empty())
        args.push_back("--filter-from=" + filter_file);

    auto on_line = [&](const std::string &line) {
        if (line.size() < 3 || line[1] != ' ')
            return;
        char c = line[0];
        if (c != '=' && c != '+' && c != '-' && c != '*' && c != '!')
            return;
        // rclone: '+' = only in source, '-' = only in destination. We use the
        // opposite convention (see CompareItem), so swap them here.
        if (c == '+')
            c = '-';
        else if (c == '-')
            c = '+';
        out.push_back({ c, line.substr(2) });
    };
    auto r = run_process(exe_, args, on_line, {}, handle);
    if (!filter_file.empty()) {
        std::error_code ec;
        fs::remove(fs::path(utf8_to_wide(filter_file)), ec);
    }
    if (!r.started)
        return { false, "No se pudo ejecutar rclone." };
    if (handle && handle->killed())
        return { false, "Cancelado por el usuario." };
    // `check` exits non-zero when differences exist; that is not a failure here.
    if (r.exit_code != 0 && out.empty()) {
        std::string t = error_text(r);
        // Exit code 1 with no output and no error text means "nothing to compare".
        if (!trim(r.err).empty())
            return { false, t };
    }
    return { true, {} };
}

ProcessResult Rclone::mount(const std::string &spec, const std::string &mountpoint,
                            const std::string &cache_mode, bool read_only,
                            const std::string &volname,
                            const std::function<void(const std::string &)> &on_log,
                            ProcessHandle *handle) const
{
    std::vector<std::string> args { "mount", spec, mountpoint, "--vfs-cache-mode=" + cache_mode,
                                    "-v" };
    if (read_only)
        args.push_back("--read-only");
    if (!volname.empty())
        args.push_back("--volname=" + volname);
    return run_process(exe_, args, {}, on_log, handle);
}

std::string format_size(int64_t bytes)
{
    if (bytes < 0)
        return "-";
    static const char *units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0)
        std::snprintf(buf, sizeof(buf), "%lld %s", static_cast<long long>(bytes), units[u]);
    else
        std::snprintf(buf, sizeof(buf), v >= 100 ? "%.0f %s" : "%.1f %s", v, units[u]);
    return buf;
}

std::string format_speed(double bps)
{
    return format_size(static_cast<int64_t>(bps)) + "/s";
}

std::string format_eta(double seconds)
{
    if (seconds < 0 || !std::isfinite(seconds))
        return "-";
    auto s = static_cast<int64_t>(seconds);
    char buf[32];
    if (s >= 3600)
        std::snprintf(buf, sizeof(buf), "%lldh %02lldm", static_cast<long long>(s / 3600),
                      static_cast<long long>((s % 3600) / 60));
    else if (s >= 60)
        std::snprintf(buf, sizeof(buf), "%lldm %02llds", static_cast<long long>(s / 60),
                      static_cast<long long>(s % 60));
    else
        std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(s));
    return buf;
}

std::string format_date(const std::string &iso)
{
    if (iso.size() < 16)
        return iso;
    std::string s = iso.substr(0, 16);
    if (s[10] == 'T')
        s[10] = ' ';
    return s;
}

} // namespace rl
