#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diagnostics.h"
#include "rclone.h"
#include "store.h"

namespace rl {

std::string current_executable_path();
bool schedule_is_due(const Schedule &schedule, int64_t now);
int64_t schedule_next_due(const Schedule &schedule, int64_t now);
std::vector<std::string> background_task_arguments(const std::string &exe,
                                                   const std::string &task_name = "Rclone Lite Background Scheduled Jobs");
OpResult configure_background_task(const std::string &exe, bool enabled,
                                   const std::string &task_name = "Rclone Lite Background Scheduled Jobs");

// Last batch of background jobs, kept in the data folder so the app can show it.
void save_background_run(const BackgroundRun &run);
BackgroundRun load_background_run();

// Written just before the installer starts so background runs do not start while files are replaced.
void set_update_marker(bool on);
bool update_marker_active(int64_t now, int64_t max_age_seconds = 900);
BackgroundTask query_background_task(const std::string &task_name = "Rclone Lite Background Scheduled Jobs");

// Runs jobs due at `now`, persists their status and history, and returns the number of failures.
int run_due_jobs(Store &store, Rclone &rclone, int64_t now);

// Entry point used by the Task Scheduler's --run-due-jobs command line.
int run_scheduled_jobs_headless();

} // namespace rl
