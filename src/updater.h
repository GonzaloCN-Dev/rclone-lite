#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace rl::updater {

struct Release {
    bool ok = false;
    std::string error;
    std::string tag;
    std::string version; // tag without the leading "v"
    std::string notes;
    std::string installer_url;
    std::string installer_name;
    std::string sha256; // lowercase hex, empty if the release does not publish one
    int64_t size = 0;
};

// Queries the latest GitHub release of Rclone Lite. Blocking; call from a worker thread.
Release fetch_latest();

// True when `remote` is a higher dotted version than `local` ("1.10.0" > "1.9.2").
bool is_newer(const std::string &remote, const std::string &local);

// Downloads `url` (https only) into `path` through a ".part" file. Returns an empty string on
// success, otherwise an error message. `progress(done, total)` may report total <= 0.
std::string download(const std::string &url, const std::string &path,
                     const std::function<void(int64_t, int64_t)> &progress);

// Lowercase hex SHA-256 of a file, or an empty string if it cannot be read.
std::string sha256_file(const std::string &path);

// Starts the installer silently and returns immediately. The caller must exit afterwards so the
// installer can replace the running files. Requests elevation when the app lives in Program Files.
bool run_installer(const std::string &path);

} // namespace rl::updater
