#include "scheduler.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "platform.h"
#include "proc.h"
#include "jsonutil.h"

namespace fs = std::filesystem;

namespace rl {
namespace {

fs::path background_status_path()
{
    return fs::path(utf8_to_wide(Store::data_dir())) / L"background_status.json";
}

std::wstring quote_task_argument(const std::wstring &arg)
{
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
        } else if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(c);
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(c);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::string task_action(const std::string &exe)
{
    return wide_to_utf8(quote_task_argument(utf8_to_wide(exe))) + " --run-due-jobs";
}

int run_due_jobs_unlocked(Store &store, Rclone &rclone, int64_t now)
{
    int failures = 0;
    BackgroundRun run;
    for (auto &job : store.jobs) {
        if (!schedule_is_due(job.schedule, now))
            continue;
        ++run.jobs;

        // Claim this occurrence before starting work, so a second minute tick cannot duplicate it.
        job.schedule.last_run = now;
        job.last_run = now;
        job.last_status.clear();
        if (!store.save()) {
            ++failures;
            plat::log_error("No se pudo guardar antes del trabajo programado «" + job.name + "»: " +
                            store.save_error);
            continue;
        }

        const auto started = std::chrono::steady_clock::now();
        int64_t bytes = 0;
        int64_t files = 0;
        std::string errors;
        std::string destinations;
        for (const auto &destination : job.destinations) {
            if (!destinations.empty())
                destinations += ", ";
            destinations += destination;
            TransferOptions options = job.options;
            options.transfers = options.transfers > 0 ? options.transfers : store.settings.transfers;
            options.checkers = options.checkers > 0 ? options.checkers : store.settings.checkers;
            if (options.bwlimit.empty())
                options.bwlimit = store.settings.bwlimit;
            if (!store.settings.extra_flags.empty())
                options.extra_flags = store.settings.extra_flags + " " + options.extra_flags;
            options.update_existing = options.update_existing || store.settings.update_existing;
            options.lock_wait_seconds = 600;

            ProcessHandle handle;
            Stats final_stats;
            OpResult result = rclone.transfer(job.source, destination, options,
                                              [&](const Stats &stats) { final_stats = stats; },
                                              {}, &handle);
            bytes += final_stats.bytes;
            files += final_stats.transfers;
            if (!result.ok) {
                ++failures;
                ++run.failures;
                run.last_error = result.message;
                if (!errors.empty())
                    errors += "\n";
                errors += destination + ": " + result.message;
            }
        }

        job.last_status = errors.empty() ? "ok" : "error";
        HistoryEntry history;
        history.time = now_unix();
        history.job = job.name;
        history.mode = job.options.mode;
        history.source = job.source;
        history.destination = destinations;
        history.status = errors.empty() ? "ok" : "error";
        history.trigger = "scheduled";
        history.bytes = bytes;
        history.files = files;
        history.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        history.error = errors.size() > 500 ? errors.substr(0, 500) : errors;
        store.add_history(std::move(history));
        if (!store.save()) {
            ++failures;
            plat::log_error("No se pudo guardar el resultado del trabajo programado «" + job.name + "»: " +
                            store.save_error);
        }
        if (!errors.empty())
            plat::log_error("Falló el trabajo programado «" + job.name + "»: " + errors);
    }
    if (run.jobs > 0) {
        run.time = now_unix();
        save_background_run(run);
    }
    return failures;
}

} // namespace

void set_update_marker(bool on)
{
    const fs::path path = fs::path(utf8_to_wide(Store::data_dir())) / L"updating.flag";
    std::error_code ec;
    if (!on) {
        fs::remove(path, ec);
        return;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << now_unix();
}

bool update_marker_active(int64_t now, int64_t max_age_seconds)
{
    std::ifstream f(fs::path(utf8_to_wide(Store::data_dir())) / L"updating.flag", std::ios::binary);
    if (!f)
        return false;
    long long stamp = 0;
    f >> stamp;
    return stamp > 0 && now - stamp >= 0 && now - stamp < max_age_seconds;
}

void save_background_run(const BackgroundRun &run)
{
    nlohmann::json j = { { "time", run.time }, { "jobs", run.jobs }, { "failures", run.failures },
                         { "last_error", run.last_error.substr(0, 400) } };
    const fs::path path = background_status_path();
    fs::path tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump();
        if (!f)
            return;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
        fs::remove(tmp, ec);
}

BackgroundRun load_background_run()
{
    BackgroundRun run;
    std::ifstream f(background_status_path(), std::ios::binary);
    if (!f)
        return run;
    std::stringstream ss;
    ss << f.rdbuf();
    auto j = nlohmann::json::parse(ss.str(), nullptr, false);
    if (!j.is_object())
        return run;
    run.time = jv(j, "time", static_cast<int64_t>(0));
    run.jobs = jv(j, "jobs", 0);
    run.failures = jv(j, "failures", 0);
    run.last_error = jv(j, "last_error", "");
    return run;
}

BackgroundTask query_background_task(const std::string &task_name)
{
    // PowerShell returns numbers (unix seconds, result code), so nothing depends on Windows' language.
    std::string quoted;
    for (char c : task_name)
        quoted += c == '\'' ? std::string("''") : std::string(1, c);
    const std::string script =
            "$ErrorActionPreference='Stop';try{$t=Get-ScheduledTask -TaskName '" + quoted +
            "';$i=$t|Get-ScheduledTaskInfo;"
            "function U($d){if($d -and $d.Year -gt 2000){([DateTimeOffset]$d).ToUnixTimeSeconds()}else{0}};"
            "Write-Output 'exists=1';Write-Output ('last='+(U $i.LastRunTime));Write-Output ('next='+(U $i.NextRunTime));"
            "Write-Output ('result='+$i.LastTaskResult)}catch{Write-Output 'exists=0'}";
    ProcessResult r = run_process("powershell.exe", { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script });
    BackgroundTask task;
    if (!r.started)
        return task;
    return parse_task_info(r.out);
}
std::string current_executable_path()
{
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size())
        return {};
    path.resize(size);
    return wide_to_utf8(path);
}

bool schedule_is_due(const Schedule &schedule, int64_t now)
{
    if (!schedule.enabled || now <= 0)
        return false;

    if (schedule.kind != "daily") {
        const int interval = std::max(1, schedule.interval_minutes);
        return now >= schedule.last_run + static_cast<int64_t>(interval) * 60;
    }

    int hour = 0, minute = 0;
    char trailing = '\0';
    if (std::sscanf(schedule.daily_time.c_str(), "%d:%d%c", &hour, &minute, &trailing) != 2 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;

    const std::time_t current = static_cast<std::time_t>(now);
    std::tm today {};
    if (localtime_s(&today, &current) != 0)
        return false;
    today.tm_hour = hour;
    today.tm_min = minute;
    today.tm_sec = 0;
    today.tm_isdst = -1;
    const int64_t due_at = static_cast<int64_t>(std::mktime(&today));
    return now >= due_at && schedule.last_run < due_at;
}

int64_t schedule_next_due(const Schedule &schedule, int64_t now)
{
    if (!schedule.enabled || now <= 0)
        return 0;
    if (schedule.kind != "daily")
        return schedule.last_run + static_cast<int64_t>(std::max(1, schedule.interval_minutes)) * 60;

    int hour = 0, minute = 0;
    char trailing = '\0';
    if (std::sscanf(schedule.daily_time.c_str(), "%d:%d%c", &hour, &minute, &trailing) != 2 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return 0;
    const std::time_t current = static_cast<std::time_t>(now);
    std::tm local {};
    if (localtime_s(&local, &current) != 0)
        return 0;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    int64_t due = static_cast<int64_t>(std::mktime(&local));
    if (schedule.last_run >= due) {
        ++local.tm_mday;
        local.tm_isdst = -1;
        due = static_cast<int64_t>(std::mktime(&local));
    }
    return due > 0 ? due : 0;
}

std::vector<std::string> background_task_arguments(const std::string &exe, const std::string &task_name)
{
    return { "/Create", "/TN", task_name, "/SC", "MINUTE", "/MO", "1",
             "/TR", task_action(exe), "/IT", "/F" };
}

OpResult configure_background_task(const std::string &exe, bool enabled, const std::string &task_name)
{
    if (enabled && exe.empty())
        return { false, "No se pudo determinar la ruta de Rclone Lite.exe." };

    if (enabled) {
        ProcessResult result = run_process("schtasks.exe", background_task_arguments(exe, task_name));
        if (!result.started || result.exit_code != 0) {
            std::string message = result.err.empty() ? result.out : result.err;
            if (message.empty())
                message = "Windows no pudo registrar la tarea programada.";
            return { false, message };
        }
        return { true, "" };
    }

    ProcessResult query = run_process("schtasks.exe", { "/Query", "/TN", task_name });
    if (!query.started || query.exit_code != 0)
        return { true, "" };
    ProcessResult result = run_process("schtasks.exe", { "/Delete", "/TN", task_name, "/F" });
    if (!result.started || result.exit_code != 0) {
        std::string message = result.err.empty() ? result.out : result.err;
        return { false, message.empty() ? "Windows no pudo quitar la tarea programada." : message };
    }
    return { true, "" };
}

int run_due_jobs(Store &store, Rclone &rclone, int64_t now)
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RcloneLiteBackgroundScheduledJobs");
    if (!mutex)
        return 1;
    const DWORD wait = WaitForSingleObject(mutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(mutex);
        return wait == WAIT_TIMEOUT ? 0 : 1;
    }
    const int failures = run_due_jobs_unlocked(store, rclone, now);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return failures;
}

int run_scheduled_jobs_headless()
{
    Store store;
    store.load();
    for (const auto &warning : store.warnings)
        plat::log_error("Almacén local: " + warning);
    if (!store.settings.background_schedules)
        return 0;
    if (update_marker_active(now_unix())) {
        plat::log_error("Se omite la ejecución en segundo plano: hay una actualización de Rclone Lite en curso.");
        return 0;
    }

    Rclone rclone;
    if (!store.settings.rclone_path.empty())
        rclone.set_executable(store.settings.rclone_path);
    else
        rclone.set_executable(Rclone::auto_detect());
    if (!rclone.available()) {
        plat::log_error("No se encontró rclone para ejecutar los trabajos programados.");
        return 2;
    }
    rclone.set_auto_tune(store.settings.auto_tune);
    rclone.set_remote_types(rclone.list_remotes());
    return run_due_jobs(store, rclone, now_unix()) == 0 ? 0 : 1;
}

} // namespace rl
