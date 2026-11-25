#include "rclone.h"
#include "close_policy.h"
#include "diagnostics.h"
#include "scheduler.h"
#include "store.h"

#include <windows.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main()
{
    using namespace rl;

    require(should_warn_before_exit(1), "active transfers trigger an exit warning");
    require(!should_warn_before_exit(0), "no exit warning is shown without active transfers");
    require(rl::should_defer_update(1), "updates wait for active transfers");
    require(!rl::should_defer_update(0), "updates may proceed without active transfers");

    require(valid_bwlimit(""), "an empty bandwidth limit is valid");
    require(valid_bwlimit("2M") && valid_bwlimit("512k") && valid_bwlimit("1.5M:off"), "plain bandwidth limits are valid");
    require(valid_bwlimit("09:00,2M 18:00,off"), "a timetable is valid");
    require(valid_bwlimit("Mon-09:00,1M Sat-00:00,off"), "a weekday timetable is valid");
    require(!valid_bwlimit("fast") && !valid_bwlimit("25:00,2M") && !valid_bwlimit("09:00,x"),
            "malformed bandwidth limits are rejected");
    require(!valid_bwlimit("2M --config x") && !valid_bwlimit("09:00,2M;calc"), "bandwidth limits cannot carry other flags");
    require(valid_path_spec("drive:photos"), "remote paths are valid");
    require(valid_path_spec("C:/Users/name"), "Windows paths are valid");
    require(!valid_path_spec("--config=C:/outside.conf"), "leading flags are not valid paths");
    require(!valid_path_spec(":sftp,ssh=cmd:host:/data"), "backend-option paths are not valid imports");
    require(!valid_files_from_raw_name("name\nother.txt") &&
                    !valid_files_from_raw_name("name\r.txt") &&
                    valid_files_from_raw_name("two words.txt"),
            "newline-delimited batches reject names with line breaks");
    require(sanitize_extra_flags("-config C:/outside.conf -transfers=4") == "-transfers=4",
            "single-dash long forms of blocked flags are stripped");

    std::vector<std::string> removed;
    std::string safe = sanitize_extra_flags(
            "--transfers=16 --password-command=whoami --log-file C:/private.log --files-from C:/private.txt --config-dir C:/fake --bwlimit=2M --rc",
            &removed);
    require(safe == "--transfers=16 --bwlimit=2M", "dangerous imported flags are stripped");
    require(removed.size() == 5, "blocked flags and their values are reported");

    require(is_auth_error("failed to refresh token: invalid_grant"), "invalid_grant is recognized");
    require(is_auth_error("AADSTS700082: refresh token has expired"), "expired Microsoft tokens are recognized");
    require(!is_auth_error("quota exceeded"), "quota errors are not auth errors");
    require(is_oauth_type("drive") && is_oauth_type("onedrive") && !is_oauth_type("s3"),
            "only browser OAuth backends are eligible for reauthentication");
    Rclone rclone;
    TransferOptions update_options;
    update_options.update_existing = true;
    const auto update_flags = rclone.common_flags(update_options, nullptr);
    require(std::find(update_flags.begin(), update_flags.end(), "--update") != update_flags.end(),
            "update conflict policy passes --update to rclone");
    update_options.ignore_existing = true;
    const auto ignore_flags = rclone.common_flags(update_options, nullptr);
    require(std::find(ignore_flags.begin(), ignore_flags.end(), "--ignore-existing") != ignore_flags.end() &&
                    std::find(ignore_flags.begin(), ignore_flags.end(), "--update") == ignore_flags.end(),
            "ignore-existing takes precedence over update mode");

    Store store;
    std::vector<Job> jobs;
    ImportReport report;
    const std::string import = R"json([
      {
        "name":"nightly","source":"drive:photos","destinations":["D:/backup"],
        "options":{"mode":"sync","extra_flags":"--password-command=cmd --transfers=12"},
        "schedule":{"enabled":true,"kind":"interval","interval_minutes":0}
      },
      {"name":"invalid","source":"--config=x","destinations":["D:/backup"]},
      {"name":"incomplete","source":"drive:photos","destinations":[]}
    ])json";
    require(store.parse_jobs_import(import, jobs, report), "valid JSON arrays parse");
    require(jobs.size() == 1 && report.skipped == 2, "invalid and incomplete jobs are skipped");
    require(!jobs[0].schedule.enabled && jobs[0].schedule.interval_minutes == 1,
            "imported schedules are disabled and invalid intervals are clamped");
    require(jobs[0].options.mode == "sync" && jobs[0].options.extra_flags == "--transfers=12",
            "sync mode is retained but command-execution flags are removed");
    require(report.sync_jobs == 1 && report.schedules_disabled == 1 && report.removed_flags.size() == 1,
            "import report describes destructive and sanitized content");
    require(!store.parse_jobs_import("{}", jobs, report), "non-array import is rejected");

    Schedule interval;
    interval.enabled = true;
    interval.kind = "interval";
    interval.interval_minutes = 15;
    interval.last_run = 1000;
    require(!schedule_is_due(interval, 1899) && schedule_is_due(interval, 1900),
            "interval schedules become due at their exact minute boundary");
    require(schedule_next_due(interval, 1800) == 1900, "next interval uses the last scheduled run");
    require(schedule_next_due(interval, 2000) == 1900, "overdue interval retains its missed due time");
    interval.enabled = false;
    require(schedule_next_due(interval, 1800) == 0, "disabled schedules have no next due time");
    Schedule daily;
    daily.enabled = true;
    daily.kind = "daily";
    daily.daily_time = "02:30";
    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_s(&local, &now);
    local.tm_hour = 2;
    local.tm_min = 30;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    const int64_t today_at_230 = static_cast<int64_t>(std::mktime(&local));
    daily.last_run = today_at_230 - 86400;
    require(!schedule_is_due(daily, today_at_230 - 60) && schedule_is_due(daily, today_at_230),
            "daily schedule fires once when the local-time target is reached");
    daily.last_run = today_at_230;
    require(!schedule_is_due(daily, today_at_230 + 60),
            "daily schedule does not fire a second time on the same day");
    const auto task_args = background_task_arguments("C:/Program Files/Rclone Lite/RcloneLite.exe");
    auto tr = std::find(task_args.begin(), task_args.end(), "/TR");
    require(tr != task_args.end() && tr + 1 != task_args.end() &&
                    (tr + 1)->find("\"C:/Program Files/Rclone Lite/RcloneLite.exe\"") == 0 &&
                    (tr + 1)->find("--run-due-jobs") != std::string::npos,
            "background task action quotes the executable path and selects headless mode");
    if (std::getenv("RCLONE_LITE_TEST_TASK_SCHEDULER")) {
        const std::string task_name = "RcloneLite CI test " + std::to_string(GetCurrentProcessId());
        const OpResult created = configure_background_task(current_executable_path(), true, task_name);
        const OpResult removed = configure_background_task(current_executable_path(), false, task_name);
        require(created.ok, "Windows Task Scheduler accepts a per-user background task");
        require(removed.ok, "Windows Task Scheduler removes the temporary background task");
    }

    auto data_root = std::filesystem::temp_directory_path() / L"RcloneLiteLogicTests";
    std::error_code ec;
    std::filesystem::remove_all(data_root, ec);
    std::filesystem::create_directories(data_root, ec);
    require(_wputenv_s(L"APPDATA", data_root.wstring().c_str()) == 0,
            "test data directory can be isolated");
    Store persisted;
    Job first;
    first.id = "first";
    first.name = "Known good";
    first.source = "C:/source";
    first.destinations = { "D:/backup" };
    first.options.update_existing = true;
    persisted.jobs.push_back(first);
    persisted.settings.update_existing = true;
    persisted.settings.background_schedules = true;
    require(persisted.save(), "initial store save succeeds");
    persisted.jobs[0].name = "Second version";
    require(persisted.save(), "updated store save succeeds and creates backup");
    {
        std::ofstream damaged(data_root / L"RcloneLite" / L"jobs.json", std::ios::binary | std::ios::trunc);
        damaged << "{broken";
    }
    Store recovered;
    recovered.load();
    require(recovered.jobs.size() == 1 && recovered.jobs[0].name == "Known good",
            "corrupt jobs file recovers the last valid backup");
    require(recovered.jobs[0].options.update_existing && recovered.settings.update_existing &&
                    recovered.settings.background_schedules,
            "transfer conflict and background scheduling settings persist");
    require(!recovered.warnings.empty() &&
                    std::filesystem::exists(data_root / L"RcloneLite" / L"jobs.json.corrupt"),
            "store recovery reports and preserves the damaged file");
    std::filesystem::remove_all(data_root, ec);

    require(explain_error("googleapi: Error 403: The user's Drive storage quota has been exceeded, storageQuotaExceeded").kind == "quota",
            "quota errors are explained");
    require(explain_error("Failed to copy: open C:/x: Access is denied.").kind == "permission", "permission errors are explained");
    require(explain_error("couldn't fetch token: invalid_grant").kind == "auth", "expired tokens are explained");
    require(explain_error("error listing: directory not found").kind == "not_found", "missing paths are explained");
    require(explain_error("write x: There is not enough space on the disk.").kind == "disk_full", "full disks are explained");
    require(explain_error("dial tcp: lookup host: no such host").kind == "network", "network errors are explained");
    require(explain_error(transfer_busy_message()).kind == "busy", "busy lock is explained");
    require(!explain_error("something odd happened").known() && friendly_error("something odd happened") == "something odd happened",
            "unknown errors keep their raw text");
    require(friendly_error("permission denied").find("permisos") != std::string::npos, "friendly text names the cause");

    PlanCollector plan;
    plan.add({ "copy", "a.txt", 10, "" });
    plan.add({ "copy", "b.txt", 20, "" });
    plan.add({ "delete", "gone.txt", 5, "" });
    plan.mark_updates({ "b.txt" });
    require(plan.copies == 1 && plan.updates == 1 && plan.deletes == 1 && plan.total() == 3, "plan counts each action once");
    require(plan.bytes_transferred == 30 && plan.bytes_deleted == 5, "plan sums sizes");
    PlanCollector other;
    other.add({ "delete", "x", 1, "" });
    plan.merge(std::move(other), "dest2");
    require(plan.deletes == 2 && plan.items.back().dest == "dest2", "plans from several destinations merge");

    BackgroundTask task = parse_task_info("exists=1\nlast=1795600000\nnext=1795600060\nresult=0\n");
    require(task.exists && task.last_result_known && task.last_result == 0 && task.last_run == 1795600000 && task.next_run == 1795600060,
            "scheduled task info is parsed");
    require(!parse_task_info("exists=0\n").exists && parse_task_info("exists=0\n").queried, "a missing task is reported as missing");
    require(parse_task_info("garbage\nresult=abc\n").last_result_known == false, "malformed task info is ignored");
    BackgroundRun clean { 1000000, 2, 0, "" };
    require(!summarize_background(true, true, task, clean).problem, "a clean background run is not a problem");
    BackgroundRun failed { 1000000, 2, 1, "permission denied" };
    BackgroundSummary bad = summarize_background(true, true, task, failed);
    require(bad.problem && bad.text.find("permisos") != std::string::npos, "failed background jobs are reported with a cause");
    BackgroundTask missing;
    missing.queried = true;
    require(summarize_background(true, true, missing, clean).problem, "a missing Windows task is reported");
    require(!summarize_background(false, true, task, clean).problem, "disabled background runs are not a problem");

    std::cout << "All logic tests passed.\n";
    return 0;
}
