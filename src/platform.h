#pragma once

#include <functional>
#include <string>
#include <vector>

namespace rl::plat {

std::string pick_folder(const std::string &title);
std::string open_file_dialog(const std::string &title, const wchar_t *filter);
std::string save_file_dialog(const std::string &title, const std::string &default_name,
                             const wchar_t *filter, const wchar_t *default_ext);

void open_url(const std::string &url);
void open_path(const std::string &path);
bool set_clipboard(const std::string &utf8);

bool winfsp_installed();
bool path_exists(const std::string &path);
std::vector<char> free_drive_letters();
std::vector<std::string> local_drives(); // "C:" ...
std::string user_profile();

// Children (rclone processes) die together with this process.
void kill_children_on_exit();

// Returns true if another instance is already running (and asks it to show itself).
bool signal_existing_instance();

// Notification-area icon with a context menu. Callbacks run on the UI thread.
bool tray_create(const std::string &tooltip, std::function<void()> on_open, std::function<void()> on_quit);
void tray_destroy();
void tray_notify(const std::string &title, const std::string &text, bool error);

bool app_is_foreground();
void focus_main_window();

bool read_text_file(const std::string &path, std::string &out);
bool write_text_file(const std::string &path, const std::string &data);

} // namespace rl::plat
