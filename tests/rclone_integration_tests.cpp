#include "rclone.h"
#include "scheduler.h"
#include "store.h"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void write_text(const fs::path &path, const std::string &text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    require(static_cast<bool>(file), "write test file");
}

} // namespace

int main()
{
    const char *exe = std::getenv("RCLONE_EXE");
    if (!exe || !*exe) {
        std::cerr << "SKIP: RCLONE_EXE is not set.\n";
        return 77;
    }

    const std::wstring suffix = std::to_wstring(GetCurrentProcessId());
    const fs::path root = fs::temp_directory_path() / (L"RcloneLiteRcloneIntegration-" + suffix);
    const fs::path data = fs::temp_directory_path() / (L"RcloneLiteRcloneIntegrationData-" + suffix);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(data, ec);
    fs::create_directories(root / L"source", ec);
    fs::create_directories(root / L"copied", ec);
    fs::create_directories(root / L"moved", ec);
    fs::create_directories(data, ec);
    require(!ec, "create temporary integration directories");

    require(_wputenv_s(L"APPDATA", data.wstring().c_str()) == 0, "isolate app data");
    write_text(root / L"source" / L"copy me.txt", "copy");
    write_text(root / L"source" / L"move [one].txt", "move");
    write_text(root / L"source" / L"delete me.txt", "delete");
    write_text(root / L"source" / L"keep.txt", "keep");

    rl::Rclone rclone;
    rclone.set_executable(exe);
    require(rclone.available(), "configured rclone binary");
    const std::string source = (root / L"source").string();
    const std::string copied = (root / L"copied").string();
    const std::string moved = (root / L"moved").string();

    HANDLE held_transfer = CreateMutexW(nullptr, TRUE, L"Local\\RcloneLiteTransferInProgress");
    require(held_transfer != nullptr, "test can hold the global transfer lock");
    rl::OpResult blocked;
    std::thread blocked_thread([&] {
        rl::TransferOptions blocked_options;
        rl::ProcessHandle blocked_handle;
        blocked = rclone.transfer(source, copied, blocked_options, {}, {}, &blocked_handle);
    });
    blocked_thread.join();
    require(!blocked.ok && blocked.message.find("transferencia de Rclone Lite") != std::string::npos,
            "second transfer is rejected while another Rclone Lite transfer owns the lock");
    ReleaseMutex(held_transfer);
    CloseHandle(held_transfer);

    rl::TransferOptions copy_options;
    rl::ProcessHandle copy_handle;
    auto copy = rclone.transfer(source, copied, copy_options, {}, {}, &copy_handle);
    require(copy.ok, "rclone wrapper copies a directory to local destination");
    require(fs::exists(root / L"copied" / L"copy me.txt"), "copied file exists");

    const fs::path files_from = root / L"move-files.txt";
    write_text(files_from, "move [one].txt\n");
    rl::TransferOptions move_options;
    move_options.mode = "move";
    move_options.files_from = files_from.string();
    rl::ProcessHandle move_handle;
    auto move = rclone.transfer(source, moved, move_options, {}, {}, &move_handle);
    require(move.ok, "rclone wrapper moves --files-from-raw selection");
    require(fs::exists(root / L"moved" / L"move [one].txt") &&
                    !fs::exists(root / L"source" / L"move [one].txt"),
            "selected file moved and removed from source");

    auto deleted = rclone.delete_listed(copied, { "delete me.txt" });
    require(deleted.ok && !fs::exists(root / L"copied" / L"delete me.txt"),
            "rclone wrapper deletes only listed path");
    require(fs::exists(root / L"copied" / L"keep.txt"), "unlisted file remains");

    rl::Store store;
    rl::Job job;
    job.id = "integration-job";
    job.name = "Integration scheduled copy";
    job.source = source;
    job.destinations = { (root / L"scheduled-copy").string() };
    job.schedule.enabled = true;
    job.schedule.kind = "interval";
    job.schedule.interval_minutes = 1;
    job.schedule.last_run = rl::now_unix() - 120;
    store.jobs.push_back(job);
    const int result = rl::run_due_jobs(store, rclone, rl::now_unix());
    require(result == 0 && fs::exists(root / L"scheduled-copy" / L"keep.txt"),
            "headless due-job runner executes a real scheduled transfer");
    require(store.jobs[0].last_status == "ok" && !store.history.empty() &&
                    store.history.back().trigger == "scheduled",
            "headless runner stores job status and scheduled history");

    write_text(root / L"blocked-destination", "not a directory");
    rl::Job multi;
    multi.id = "integration-multi-destination";
    multi.name = "Integration 1:N continuation";
    multi.source = source;
    multi.destinations = { (root / L"blocked-destination" / L"child").string(),
                           (root / L"continued-copy").string() };
    multi.schedule.enabled = true;
    multi.schedule.kind = "interval";
    multi.schedule.interval_minutes = 1;
    multi.schedule.last_run = rl::now_unix() - 120;
    store.jobs.push_back(multi);
    const int multi_result = rl::run_due_jobs(store, rclone, rl::now_unix());
    require(multi_result == 1 && fs::exists(root / L"continued-copy" / L"keep.txt"),
            "scheduled 1:N run continues to later destinations after an earlier destination fails");
    require(store.jobs.back().last_status == "error" && store.history.front().status == "error",
            "1:N failure is reflected in scheduled job status and history");

    fs::remove_all(root, ec);
    fs::remove_all(data, ec);
    std::cout << "Real-rclone integration tests passed.\n";
    return 0;
}
