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
    std::string status;   // as printed by schtasks (localized)
    std::string last_run; // as printed by schtasks (localized)
    std::string next_run;
    long long last_result = 0;
    bool last_result_known = false;
};

// Parses one data line of `schtasks /Query /FO CSV /V /NH`. Column positions do not depend on the language.
BackgroundTask parse_task_query_csv(const std::string &text);

struct BackgroundSummary {
    std::string text;
    bool problem = false;
};

BackgroundSummary summarize_background(bool enabled, bool has_schedules, const BackgroundTask &task,
                                       const BackgroundRun &run);

} // namespace rl
