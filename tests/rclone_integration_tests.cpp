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
    bool probe_busy = false;
    std::thread probe_thread([&] { probe_busy = rl::transfer_lock_busy(); });
    probe_thread.join();
    require(probe_busy, "the probe sees a transfer lock held by another owner");
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
    bool probe_free = false;
    std::thread free_thread([&] { probe_free = !rl::transfer_lock_busy(); });
    free_thread.join();
    require(probe_free, "the probe sees the lock free after release");

    rl::set_update_marker(true);
    require(rl::update_marker_active(rl::now_unix()), "update marker is active right after being set");
    require(!rl::update_marker_active(rl::now_unix() + 2000), "stale update marker is ignored");
    rl::Store marker_store;
    marker_store.settings.background_schedules = true;
    require(rl::run_scheduled_jobs_headless() == 0, "headless run is skipped while an update marker exists");
    rl::set_update_marker(false);
    require(!rl::update_marker_active(rl::now_unix()), "update marker is cleared");

    HANDLE queued_lock = CreateMutexW(nullptr, TRUE, L"Local\\RcloneLiteTransferInProgress");
    require(queued_lock != nullptr, "test can hold the lock for the queue check");
    std::thread releaser([&] {
        Sleep(1200);
        ReleaseMutex(queued_lock);
    });
    rl::TransferOptions queued_options;
    queued_options.lock_wait_seconds = 10;
    rl::ProcessHandle queued_handle;
    auto queued = rclone.transfer(source, (root / L"queued").string(), queued_options, {}, {}, &queued_handle);
    releaser.join();
    CloseHandle(queued_lock);
    require(queued.ok && fs::exists(root / L"queued" / L"keep.txt"), "a queued transfer waits for the lock and then runs");

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

    write_text(root / L"copied" / L"stale.txt", "not in source");
    write_text(root / L"copied" / L"keep.txt", "older and different");
    rl::PlanCollector plan;
    rl::TransferOptions plan_options;
    plan_options.mode = "sync";
    plan_options.dry_run = true;
    plan_options.plan = &plan;
    rl::ProcessHandle plan_handle;
    auto planned = rclone.transfer(source, copied, plan_options, {}, {}, &plan_handle);
    require(planned.ok, "dry-run sync succeeds");
    bool lists_stale = false;
    for (const auto &item : plan.items)
        lists_stale = lists_stale || (item.action == "delete" && item.path == "stale.txt" && item.size == 13);
    require(lists_stale && plan.deletes == 2, "dry-run plan lists the file sync would delete");
    require(plan.updates == 1 && plan.bytes_deleted == 17, "dry-run plan separates updated files and sums deleted bytes");
    require(fs::exists(root / L"copied" / L"stale.txt"), "dry-run does not delete anything");

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

    // Two-way sync: baseline, preview, propagation and conflicts.
    const fs::path bi_a = root / L"bi-a", bi_b = root / L"bi-b";
    fs::create_directories(bi_a, ec);
    fs::create_directories(bi_b, ec);
    write_text(bi_a / L"same.txt", "same");
    write_text(bi_b / L"same.txt", "same");
    write_text(bi_a / L"only-a.txt", "a");
    write_text(bi_b / L"only-b.txt", "b");
    rl::TransferOptions bi;
    bi.mode = "bisync";
    rl::ProcessHandle bi_handle;
    auto no_baseline = rclone.transfer(bi_a.string(), bi_b.string(), bi, {}, {}, &bi_handle);
    require(!no_baseline.ok && no_baseline.message.find("primera sincronización") != std::string::npos,
            "bisync refuses to build its baseline unless it is allowed");
    require(!fs::exists(bi_b / L"only-a.txt"), "refused bisync changed nothing");
    rl::PlanCollector first;
    rl::TransferOptions first_dry = bi;
    first_dry.dry_run = true;
    first_dry.plan = &first;
    require(rclone.transfer(bi_a.string(), bi_b.string(), first_dry, {}, {}, &bi_handle).ok, "bisync first dry run");
    require(first.two_way && first.first_sync && first.copies == 2 && first.deletes == 0,
            "first bisync preview lists copies to both sides and no deletes");
    require(!fs::exists(bi_b / L"only-a.txt"), "bisync dry run changed nothing");
    rl::TransferOptions allowed = bi;
    allowed.allow_resync = true;
    require(rclone.transfer(bi_a.string(), bi_b.string(), allowed, {}, {}, &bi_handle).ok, "bisync baseline run");
    require(fs::exists(bi_b / L"only-a.txt") && fs::exists(bi_a / L"only-b.txt"), "bisync copies missing files both ways");

    fs::remove(bi_a / L"same.txt");
    write_text(bi_a / L"new-a.txt", "new a");
    write_text(bi_a / L"both.txt", "from a");
    write_text(bi_b / L"both.txt", "from b, longer");
    rl::PlanCollector second;
    rl::TransferOptions second_dry = bi;
    second_dry.dry_run = true;
    second_dry.plan = &second;
    require(rclone.transfer(bi_a.string(), bi_b.string(), second_dry, {}, {}, &bi_handle).ok, "bisync second dry run");
    bool deletes_same = false;
    for (const auto &item : second.items)
        deletes_same = deletes_same || (item.action == "delete" && item.path == "same.txt");
    require(!second.first_sync && deletes_same, "bisync preview lists the propagated delete");
    require(fs::exists(bi_b / L"same.txt"), "bisync dry run keeps the file it would delete");
    require(rclone.transfer(bi_a.string(), bi_b.string(), bi, {}, {}, &bi_handle).ok, "bisync incremental run");
    require(!fs::exists(bi_b / L"same.txt") && fs::exists(bi_b / L"new-a.txt"), "bisync propagates deletes and new files");

    const std::string queued_text = "\x1b[33m- Path1    Queue copy to Path2                      - C:\\x\\b\\a.txt\x1b[0m";
    rl::BisyncQueued q;
    require(rl::parse_bisync_queued(queued_text, q) && q.copy && q.side == 2 && q.path == "C:\\x\\b\\a.txt",
            "bisync queue line is parsed without colour codes");
    require(rl::parse_bisync_queued("- Path2    Queue delete   - b/x.txt", q) && !q.copy && q.side == 2,
            "bisync delete line is parsed");

    // Integrity check before deleting a source.
    const fs::path ck_a = root / L"ck-a", ck_b = root / L"ck-b";
    fs::create_directories(ck_a, ec);
    fs::create_directories(ck_b, ec);
    write_text(ck_a / L"ok.txt", "ok");
    write_text(ck_b / L"ok.txt", "ok");
    write_text(ck_a / L"missing.txt", "x");
    write_text(ck_a / L"diff.txt", "short");
    write_text(ck_b / L"diff.txt", "different length");
    write_text(ck_b / L"extra.txt", "extra");
    rl::TransferOptions ck;
    ck.one_way = true;
    std::vector<rl::CompareItem> ck_items;
    require(rclone.compare(ck_a.string(), ck_b.string(), ck, ck_items, nullptr).ok, "one-way check runs");
    int missing = 0, differ = 0, extra = 0, same = 0;
    for (const auto &i : ck_items) {
        missing += i.status == '-' && i.path == "missing.txt";
        differ += i.status == '*' && i.path == "diff.txt";
        extra += i.status == '+';
        same += i.status == '=';
    }
    require(missing == 1 && differ == 1 && extra == 0 && same == 1, "one-way check reports missing and different, ignores extras");

    // Filter preview.
    const fs::path fp = root / L"fp";
    fs::create_directories(fp / L"node_modules" / L"x", ec);
    fs::create_directories(fp / L"docs", ec);
    write_text(fp / L"keep.txt", "k");
    write_text(fp / L"node_modules" / L"x" / L"i.js", "i");
    write_text(fp / L"Thumbs.db", "t");
    write_text(fp / L"docs" / L"~$tmp.docx", "t");
    write_text(fp / L"docs" / L"ok.docx", "o");
    rl::FilterPreview preview;
    require(rclone.filter_preview(fp.string(), "- node_modules/**\n- Thumbs.db\n- ~$*", preview, nullptr).ok,
            "filter preview runs");
    require(preview.kept == 2 && preview.excluded == 3, "filter preview counts kept and excluded files");
    fs::remove_all(root, ec);
    fs::remove_all(data, ec);
    std::cout << "Real-rclone integration tests passed.\n";
    return 0;
}
