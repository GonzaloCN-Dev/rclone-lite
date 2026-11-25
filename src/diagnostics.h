#pragma once

#include <cstdint>
#include <string>

#include "rclone.h"

namespace rl {

struct ErrorHint {
    std::string kind;   // quota | permission | auth | not_found | disk_full | network | busy | ""
    std::string title;  // what happened, in plain words
    std::string action; // what the user can do about it
    bool known() const { return !kind.empty(); }
};

// Recognises the typical rclone failures. Unknown messages return an empty hint.
ErrorHint explain_error(const std::string &raw);

// "Cause. Action." for known errors, otherwise the raw text.
std::string friendly_error(const std::string &raw);

struct BackgroundRun {
    int64_t time = 0; // unix seconds when the last batch of due jobs finished
    int jobs = 0;
    int failures = 0;
    std::string last_error;
};

struct BackgroundTask {
    bool queried = false;
    bool exists = false;
    int64_t last_run = 0; // unix seconds, 0 = never
    int64_t next_run = 0;
    long long last_result = 0;
    bool last_result_known = false;
};

// Parses the "key=value" lines printed by the PowerShell query. Numbers only, so it works in any Windows language.
BackgroundTask parse_task_info(const std::string &text);

struct BackgroundSummary {
    std::string text;
    bool problem = false;
};

BackgroundSummary summarize_background(bool enabled, bool has_schedules, const BackgroundTask &task,
                                       const BackgroundRun &run);

} // namespace rl
