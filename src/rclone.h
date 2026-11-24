#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "proc.h"

namespace rl {

struct Entry {
    std::string name;
    int64_t size = 0;
    std::string modtime; // ISO-8601 as given by rclone
    bool is_dir = false;
};

struct RemoteInfo {
    std::string name;
    std::string type;
    bool own_client = false; // drive: has its own client_id (not rclone's shared one)
};

struct TransferFile {
    std::string name;
    int64_t size = 0;
    int64_t bytes = 0;
    double percent = 0;
    double speed = 0;
};

struct Stats {
    int64_t bytes = 0;
    int64_t total_bytes = 0;
    double speed = 0;
    double eta = -1;
    int64_t transfers = 0;
    int64_t total_transfers = 0;
    int64_t checks = 0;
    int64_t errors = 0;
    double elapsed = 0;
    std::string last_error;
    std::vector<TransferFile> active;
};

struct CompareItem {
    char status = '='; // '-' only in left, '+' only in right, '=' equal, '*' differ, '!' error
    std::string path;
};

struct TransferOptions {
    std::string mode = "copy"; // copy | sync | move
    int transfers = 4;
    int checkers = 8;
    bool dry_run = false;
    bool checksum = false;
    bool size_only = false;
    bool ignore_existing = false;
    std::string bwlimit;       // e.g. "2M"
    std::string filters;       // newline separated rclone filter rules (e.g. "- *.tmp")
    std::string extra_flags;   // free form flags separated by spaces
    std::string files_from;    // optional path to a --files-from list
};

struct OpResult {
    bool ok = false;
    std::string message;
};

std::string join_path(const std::string &base, const std::string &name);
std::string parent_path(const std::string &path);

class Rclone {
public:
    Rclone();

    void set_executable(std::string path) { exe_ = std::move(path); }
    const std::string &executable() const { return exe_; }
    static std::string auto_detect();

    bool available() const { return !exe_.empty(); }

    // Adds provider-specific speed flags (e.g. Google Drive pacing) to transfers.
    void set_auto_tune(bool on) { auto_tune_ = on; }
    bool auto_tune() const { return auto_tune_; }
    void set_remote_types(const std::vector<RemoteInfo> &list);
    std::string version() const;

    std::vector<RemoteInfo> list_remotes() const;
    std::map<std::string, std::string> remote_options(const std::string &name) const;
    OpResult create_remote(const std::string &name, const std::string &type,
                           const std::vector<std::pair<std::string, std::string>> &params,
                           ProcessHandle *handle = nullptr) const;
    OpResult delete_remote(const std::string &name) const;
    // Sets the given options on an existing remote (empty values are written too).
    OpResult update_remote(const std::string &name,
                           const std::vector<std::pair<std::string, std::string>> &params) const;
    OpResult reconnect_remote(const std::string &name, ProcessHandle *handle = nullptr) const;

    // `spec` is a full rclone path such as "gdrive:Photos" or "C:/Users".
    std::optional<std::vector<Entry>> list(const std::string &spec, std::string *error = nullptr) const;
    OpResult mkdir(const std::string &spec) const;
    OpResult delete_file(const std::string &spec) const;
    OpResult purge(const std::string &spec) const;
    OpResult delete_listed(const std::string &root, const std::vector<std::string> &rel_paths) const;
    OpResult rename(const std::string &from, const std::string &to, bool is_dir) const;
    std::string link(const std::string &spec) const;
    struct About { int64_t total = -1, used = -1, free = -1; };
    std::optional<About> about(const std::string &spec) const;

    // Long running transfer. Blocks; call from a worker thread.
    OpResult transfer(const std::string &src, const std::string &dst, const TransferOptions &opt,
                      const std::function<void(const Stats &)> &on_stats,
                      const std::function<void(const std::string &level, const std::string &msg)> &on_log,
                      ProcessHandle *handle) const;

    // Same as transfer() but for a single file: copyto/moveto.
    OpResult transfer_file(const std::string &src, const std::string &dst, bool move,
                           const TransferOptions &opt,
                           const std::function<void(const Stats &)> &on_stats,
                           const std::function<void(const std::string &, const std::string &)> &on_log,
                           ProcessHandle *handle) const;

    OpResult compare(const std::string &left, const std::string &right, const TransferOptions &opt,
                     std::vector<CompareItem> &out, ProcessHandle *handle) const;

    // Starts `rclone mount` and blocks until it exits. Hidden console.
    ProcessResult mount(const std::string &spec, const std::string &mountpoint,
                        const std::string &cache_mode, bool read_only, const std::string &volname,
                        const std::function<void(const std::string &)> &on_log,
                        ProcessHandle *handle) const;

    std::vector<std::string> common_flags(const TransferOptions &opt, std::string *filter_file) const;

    bool is_drive_spec(const std::string &spec) const;

private:
    std::string exe_;
    std::atomic<bool> auto_tune_ { true };
    mutable std::mutex types_mu_;
    std::map<std::string, std::string> remote_types_;
};

// Writes lines to a temp file and returns its path ("" on failure). Caller removes it.
std::string write_temp_lines(const std::vector<std::string> &lines);
void remove_temp_file(const std::string &path);

std::string format_size(int64_t bytes);
std::string format_speed(double bytes_per_sec);
std::string format_eta(double seconds);
std::string format_date(const std::string &iso);

} // namespace rl
