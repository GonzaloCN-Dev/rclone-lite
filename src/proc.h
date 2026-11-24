#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rl {

struct ProcessResult {
    int exit_code = -1;
    std::string out;
    std::string err;
    bool started = false;
};

using LineCallback = std::function<void(const std::string &)>;

// Handle to a running child process. Safe to kill() from any thread.
class ProcessHandle {
public:
    ProcessHandle();
    ~ProcessHandle();
    ProcessHandle(const ProcessHandle &) = delete;
    ProcessHandle &operator=(const ProcessHandle &) = delete;

    void kill();
    bool killed() const { return killed_.load(); }

private:
    friend ProcessResult run_process(const std::string &, const std::vector<std::string> &,
                                     const LineCallback &, const LineCallback &,
                                     ProcessHandle *);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> killed_{false};
};

// Runs `exe args...` hidden (no console window), blocks until it exits.
// Optional callbacks receive complete lines as they arrive. Output is also
// collected in the result unless a callback for that stream is supplied.
ProcessResult run_process(const std::string &exe, const std::vector<std::string> &args,
                          const LineCallback &on_stdout = {}, const LineCallback &on_stderr = {},
                          ProcessHandle *handle = nullptr);

std::wstring utf8_to_wide(const std::string &s);
std::string wide_to_utf8(const std::wstring &s);

} // namespace rl
