#include "rclone.h"
#include "store.h"

#include <cstdlib>
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

    require(valid_path_spec("drive:photos"), "remote paths are valid");
    require(valid_path_spec("C:/Users/name"), "Windows paths are valid");
    require(!valid_path_spec("--config=C:/outside.conf"), "leading flags are not valid paths");
    require(!valid_path_spec(":sftp,ssh=cmd:host:/data"), "backend-option paths are not valid imports");
    require(sanitize_extra_flags("-config C:/outside.conf -transfers=4") == "-transfers=4",
            "single-dash long forms of blocked flags are stripped");

    std::vector<std::string> removed;
    std::string safe = sanitize_extra_flags(
            "--transfers=16 --password-command=whoami --log-file C:/private.log --files-from C:/private.txt --bwlimit=2M --rc",
            &removed);
    require(safe == "--transfers=16 --bwlimit=2M", "dangerous imported flags are stripped");
    require(removed.size() == 4, "blocked flags and their values are reported");

    require(is_auth_error("failed to refresh token: invalid_grant"), "invalid_grant is recognized");
    require(is_auth_error("AADSTS700082: refresh token has expired"), "expired Microsoft tokens are recognized");
    require(!is_auth_error("quota exceeded"), "quota errors are not auth errors");
    require(is_oauth_type("drive") && is_oauth_type("onedrive") && !is_oauth_type("s3"),
            "only browser OAuth backends are eligible for reauthentication");

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
    persisted.jobs.push_back(first);
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
    require(!recovered.warnings.empty() &&
                    std::filesystem::exists(data_root / L"RcloneLite" / L"jobs.json.corrupt"),
            "store recovery reports and preserves the damaged file");
    std::filesystem::remove_all(data_root, ec);

    std::cout << "All logic tests passed.\n";
    return 0;
}
