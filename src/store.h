#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "rclone.h"

namespace rl {

struct Settings {
    std::string rclone_path;
    bool dark_theme = true;
    int transfers = 4;
    int checkers = 8;
    bool confirm_delete = true;
    bool notify_done = true;
    bool notify_failures = true; // Windows notification when a background job fails
    bool minimize_to_tray = false;
    bool auto_update = true;
    bool auto_tune = true;
    bool update_existing = false;
    bool background_schedules = false;
    std::string bwlimit;
    std::string extra_flags;
};

struct Schedule {
    bool enabled = false;
    std::string kind = "interval"; // interval | daily
    int interval_minutes = 60;
    std::string daily_time = "02:00"; // HH:MM, local time
    int64_t last_run = 0;             // unix seconds
};

struct Job {
    std::string id;
    std::string name;
    std::string source;
    std::vector<std::string> destinations;
    TransferOptions options;
    Schedule schedule;
    int64_t created = 0;
    int64_t last_run = 0;
    std::string last_status; // ok | error | ""
};

struct HistoryEntry {
    int64_t time = 0;
    std::string job;
    std::string mode;
    std::string source;
    std::string destination;
    std::string status; // ok | error | cancelled
    std::string trigger; // manual | scheduled
    int64_t bytes = 0;
    int64_t files = 0;
    double seconds = 0;
    std::string error;
};

struct MountDef {
    std::string id;
    std::string name;
    std::string spec;
    std::string mountpoint; // e.g. "X:" or a folder
    std::string cache_mode = "full";
    bool read_only = false;
    bool auto_mount = false;
};

// What happened while reading a jobs file that came from outside (see Store::parse_jobs_import).
struct ImportReport {
    int total = 0;
    int skipped = 0;            // not an object, or source/destination missing or not a valid path
    int schedules_disabled = 0; // imported jobs never keep their schedule switched on
    int sync_jobs = 0;          // mirror jobs delete files at the destination
    std::vector<std::string> removed_flags;
};

struct Store {
    Settings settings;
    std::vector<Job> jobs;
    std::vector<HistoryEntry> history;
    std::vector<MountDef> mounts;

    // Problems found by load() (unreadable file, recovered from backup...), to show the user.
    std::vector<std::string> warnings;
    // Names of the files the last save() could not write.
    mutable std::string save_error;

    void load();
    bool save() const;
    static std::string data_dir();

    Job *find_job(const std::string &id);
    MountDef *find_mount(const std::string &id);
    void add_history(HistoryEntry h);

    std::string export_jobs_json() const;
    // Validates and sanitizes jobs from a file; nothing is added to `jobs` here.
    bool parse_jobs_import(const std::string &text, std::vector<Job> &out, ImportReport &rep) const;
};

nlohmann::json options_to_json(const TransferOptions &o);
TransferOptions options_from_json(const nlohmann::json &j);

std::string new_id();
int64_t now_unix();
std::string format_time(int64_t t);
std::string format_duration(double seconds);

} // namespace rl
