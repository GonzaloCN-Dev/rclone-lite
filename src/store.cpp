#include "store.h"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>
#include "jsonutil.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rl {

namespace {

constexpr size_t kMaxHistory = 500;

json options_to_json(const TransferOptions &o)
{
    return { { "mode", o.mode },
             { "transfers", o.transfers },
             { "checkers", o.checkers },
             { "dry_run", o.dry_run },
             { "checksum", o.checksum },
             { "size_only", o.size_only },
             { "ignore_existing", o.ignore_existing },
             { "bwlimit", o.bwlimit },
             { "filters", o.filters },
             { "extra_flags", o.extra_flags } };
}

TransferOptions options_from_json(const json &j)
{
    TransferOptions o;
    if (!j.is_object())
        return o;
    o.mode = jv(j, "mode", o.mode);
    o.transfers = jv(j, "transfers", o.transfers);
    o.checkers = jv(j, "checkers", o.checkers);
    o.dry_run = jv(j, "dry_run", o.dry_run);
    o.checksum = jv(j, "checksum", o.checksum);
    o.size_only = jv(j, "size_only", o.size_only);
    o.ignore_existing = jv(j, "ignore_existing", o.ignore_existing);
    o.bwlimit = jv(j, "bwlimit", o.bwlimit);
    o.filters = jv(j, "filters", o.filters);
    o.extra_flags = jv(j, "extra_flags", o.extra_flags);
    return o;
}

json job_to_json(const Job &j)
{
    return { { "id", j.id },
             { "name", j.name },
             { "source", j.source },
             { "destinations", j.destinations },
             { "options", options_to_json(j.options) },
             { "schedule",
               { { "enabled", j.schedule.enabled },
                 { "kind", j.schedule.kind },
                 { "interval_minutes", j.schedule.interval_minutes },
                 { "daily_time", j.schedule.daily_time },
                 { "last_run", j.schedule.last_run } } },
             { "created", j.created },
             { "last_run", j.last_run },
             { "last_status", j.last_status } };
}

Job job_from_json(const json &j)
{
    Job job;
    job.id = jv(j, "id", "");
    if (job.id.empty())
        job.id = new_id();
    job.name = jv(j, "name", "Trabajo");
    job.source = jv(j, "source", "");
    if (j.contains("destinations") && j["destinations"].is_array())
        for (const auto &d : j["destinations"])
            if (d.is_string())
                job.destinations.push_back(d.get<std::string>());
    job.options = options_from_json(jv(j, "options", json::object()));
    if (j.contains("schedule") && j["schedule"].is_object()) {
        const auto &s = j["schedule"];
        job.schedule.enabled = jv(s, "enabled", false);
        job.schedule.kind = jv(s, "kind", "interval");
        job.schedule.interval_minutes = jv(s, "interval_minutes", 60);
        job.schedule.daily_time = jv(s, "daily_time", "02:00");
        job.schedule.last_run = jv(s, "last_run", static_cast<int64_t>(0));
    }
    job.created = jv(j, "created", static_cast<int64_t>(0));
    job.last_run = jv(j, "last_run", static_cast<int64_t>(0));
    job.last_status = jv(j, "last_status", "");
    return job;
}

bool read_file(const fs::path &p, std::string &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void write_file_atomic(const fs::path &p, const std::string &data)
{
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << data;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
}

} // namespace

std::string new_id()
{
    static std::mt19937_64 rng { std::random_device {}() };
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(rng()));
    return buf;
}

int64_t now_unix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
}

std::string format_time(int64_t t)
{
    if (t <= 0)
        return "-";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm {};
    localtime_s(&tm, &tt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string format_duration(double seconds)
{
    auto s = static_cast<int64_t>(seconds + 0.5);
    char buf[32];
    if (s >= 3600)
        std::snprintf(buf, sizeof(buf), "%lldh %02lldm %02llds", static_cast<long long>(s / 3600),
                      static_cast<long long>((s % 3600) / 60), static_cast<long long>(s % 60));
    else if (s >= 60)
        std::snprintf(buf, sizeof(buf), "%lldm %02llds", static_cast<long long>(s / 60),
                      static_cast<long long>(s % 60));
    else
        std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(s));
    return buf;
}

std::string Store::data_dir()
{
    wchar_t buf[MAX_PATH];
    fs::path base;
    if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH))
        base = buf;
    else
        base = fs::temp_directory_path();
    fs::path dir = base / L"RcloneLite";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return wide_to_utf8(dir.wstring());
}

void Store::load()
{
    fs::path dir = fs::path(utf8_to_wide(data_dir()));
    std::string text;

    if (read_file(dir / L"settings.json", text)) {
        auto j = json::parse(text, nullptr, false);
        if (j.is_object()) {
            settings.rclone_path = jv(j, "rclone_path", "");
            settings.dark_theme = jv(j, "dark_theme", true);
            settings.transfers = jv(j, "transfers", 4);
            settings.checkers = jv(j, "checkers", 8);
            settings.confirm_delete = jv(j, "confirm_delete", true);
            settings.notify_done = jv(j, "notify_done", true);
            settings.minimize_to_tray = jv(j, "minimize_to_tray", false);
            settings.auto_update = jv(j, "auto_update", true);
            settings.bwlimit = jv(j, "bwlimit", "");
            settings.extra_flags = jv(j, "extra_flags", "");
        }
    }

    jobs.clear();
    if (read_file(dir / L"jobs.json", text)) {
        auto j = json::parse(text, nullptr, false);
        if (j.is_array())
            for (const auto &e : j)
                if (e.is_object())
                    jobs.push_back(job_from_json(e));
    }

    history.clear();
    if (read_file(dir / L"history.json", text)) {
        auto j = json::parse(text, nullptr, false);
        if (j.is_array()) {
            for (const auto &e : j) {
                HistoryEntry h;
                h.time = jv(e, "time", static_cast<int64_t>(0));
                h.job = jv(e, "job", "");
                h.mode = jv(e, "mode", "");
                h.source = jv(e, "source", "");
                h.destination = jv(e, "destination", "");
                h.status = jv(e, "status", "");
                h.trigger = jv(e, "trigger", "manual");
                h.bytes = jv(e, "bytes", static_cast<int64_t>(0));
                h.files = jv(e, "files", static_cast<int64_t>(0));
                h.seconds = jv(e, "seconds", 0.0);
                h.error = jv(e, "error", "");
                history.push_back(std::move(h));
            }
        }
    }

    mounts.clear();
    if (read_file(dir / L"mounts.json", text)) {
        auto j = json::parse(text, nullptr, false);
        if (j.is_array()) {
            for (const auto &e : j) {
                MountDef m;
                m.id = jv(e, "id", new_id());
                m.name = jv(e, "name", "");
                m.spec = jv(e, "spec", "");
                m.mountpoint = jv(e, "mountpoint", "");
                m.cache_mode = jv(e, "cache_mode", "full");
                m.read_only = jv(e, "read_only", false);
                m.auto_mount = jv(e, "auto_mount", false);
                mounts.push_back(std::move(m));
            }
        }
    }
}

void Store::save() const
{
    fs::path dir = fs::path(utf8_to_wide(data_dir()));

    json s = { { "rclone_path", settings.rclone_path },
               { "dark_theme", settings.dark_theme },
               { "transfers", settings.transfers },
               { "checkers", settings.checkers },
               { "confirm_delete", settings.confirm_delete },
               { "notify_done", settings.notify_done },
               { "minimize_to_tray", settings.minimize_to_tray },
               { "auto_update", settings.auto_update },
               { "bwlimit", settings.bwlimit },
               { "extra_flags", settings.extra_flags } };
    write_file_atomic(dir / L"settings.json", s.dump(2, ' ', false, json::error_handler_t::replace));

    json jobs_j = json::array();
    for (const auto &j : jobs)
        jobs_j.push_back(job_to_json(j));
    write_file_atomic(dir / L"jobs.json", jobs_j.dump(2, ' ', false, json::error_handler_t::replace));

    json hist = json::array();
    for (const auto &h : history)
        hist.push_back({ { "time", h.time },
                         { "job", h.job },
                         { "mode", h.mode },
                         { "source", h.source },
                         { "destination", h.destination },
                         { "status", h.status },
                         { "trigger", h.trigger },
                         { "bytes", h.bytes },
                         { "files", h.files },
                         { "seconds", h.seconds },
                         { "error", h.error } });
    write_file_atomic(dir / L"history.json", hist.dump(1, ' ', false, json::error_handler_t::replace));

    json mj = json::array();
    for (const auto &m : mounts)
        mj.push_back({ { "id", m.id },
                       { "name", m.name },
                       { "spec", m.spec },
                       { "mountpoint", m.mountpoint },
                       { "cache_mode", m.cache_mode },
                       { "read_only", m.read_only },
                       { "auto_mount", m.auto_mount } });
    write_file_atomic(dir / L"mounts.json", mj.dump(2, ' ', false, json::error_handler_t::replace));
}

Job *Store::find_job(const std::string &id)
{
    for (auto &j : jobs)
        if (j.id == id)
            return &j;
    return nullptr;
}

MountDef *Store::find_mount(const std::string &id)
{
    for (auto &m : mounts)
        if (m.id == id)
            return &m;
    return nullptr;
}

void Store::add_history(HistoryEntry h)
{
    history.insert(history.begin(), std::move(h));
    if (history.size() > kMaxHistory)
        history.resize(kMaxHistory);
}

std::string Store::export_jobs_json() const
{
    json arr = json::array();
    for (const auto &j : jobs)
        arr.push_back(job_to_json(j));
    return arr.dump(2, ' ', false, json::error_handler_t::replace);
}

int Store::import_jobs_json(const std::string &text)
{
    auto j = json::parse(text, nullptr, false);
    if (!j.is_array())
        return -1;
    int n = 0;
    for (const auto &e : j) {
        if (!e.is_object())
            continue;
        Job job = job_from_json(e);
        job.id = new_id();
        job.last_run = 0;
        job.last_status.clear();
        job.schedule.last_run = 0;
        jobs.push_back(std::move(job));
        ++n;
    }
    return n;
}

} // namespace rl
