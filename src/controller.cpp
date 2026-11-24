#include "app.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

#include "platform.h"
#include "proc.h"
#include "providers.h"
#include "rclone.h"
#include "scheduler.h"
#include "store.h"
#include "updater.h"

using namespace rl;

namespace {

std::atomic<bool> g_alive { true };

template<class F>
void post(F f)
{
    slint::invoke_from_event_loop([f = std::move(f)]() mutable {
        if (!g_alive)
            return;
        try {
            f();
        } catch (const std::exception &e) {
            rl::plat::log_error(std::string("Excepción en la interfaz: ") + e.what());
        } catch (...) {
            rl::plat::log_error("Excepción desconocida en la interfaz");
        }
    });
}

// Worker threads must never let an exception escape: that terminates the whole process.
template<class F>
void spawn(F f)
{
    std::thread([f = std::move(f)]() mutable {
        try {
            f();
        } catch (const std::exception &e) {
            rl::plat::log_error(std::string("Excepción en un hilo de trabajo: ") + e.what());
        } catch (...) {
            rl::plat::log_error("Excepción desconocida en un hilo de trabajo");
        }
    }).detach();
}

slint::SharedString S(const std::string &s)
{
    return slint::SharedString(std::string_view(s));
}

std::string T(const slint::SharedString &s)
{
    return std::string(std::string_view(s));
}

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_lines(const std::string &s)
{
    std::vector<std::string> v;
    std::istringstream is(s);
    for (std::string l; std::getline(is, l);) {
        l = trim(l);
        if (!l.empty())
            v.push_back(l);
    }
    return v;
}

int parse_int(const std::string &s, int def, int lo, int hi)
{
    try {
        int v = std::stoi(trim(s));
        return std::clamp(v, lo, hi);
    } catch (...) {
        return def;
    }
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string now_clock()
{
    std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_s(&tm, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

struct Step {
    std::string src, dst;
    bool file = false;
    std::string mode = "copy";
    TransferOptions opt;
    std::string temp_file;
};

struct Task {
    std::string id;
    std::string title;
    std::string route;
    std::string job_id;
    std::string job_name;
    std::string trigger = "manual";
    std::string mode;
    bool dry = false;
    ProcessHandle handle;
    float progress = 0;
    std::string speed;
    std::string detail = "Iniciando…";
    double speed_val = 0;
    bool throttled = false;
    bool auth_noted = false;
    int64_t bytes = 0, total = 0, files = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

struct PaneState {
    std::string remote; // empty => local disk
    std::string path;
    std::vector<Entry> entries;
    std::set<std::string> selected;
    int anchor = -1;
    std::string sort_key = "name";
    bool sort_asc = true;
    std::vector<std::pair<std::string, std::string>> back;
    int gen = 0;
    bool loading = false;
    std::string error;
    std::shared_ptr<slint::VectorModel<FileRow>> model = std::make_shared<slint::VectorModel<FileRow>>();

    bool drives_view() const { return remote.empty() && path.empty(); }
    std::string spec() const { return remote.empty() ? path : remote + ":" + path; }
};

struct MountRt {
    std::string state = "unmounted";
    std::shared_ptr<ProcessHandle> handle;
    std::string detail;
};

bool is_throttle_message(const std::string &msg)
{
    std::string l = lower(msg);
    return l.find("quota exceeded") != std::string::npos || l.find("ratelimitexceeded") != std::string::npos
           || l.find("rate limit exceeded") != std::string::npos || l.find("too many requests") != std::string::npos;
}

bool is_drive_letter(const std::string &s)
{
    return s.size() == 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':';
}

std::string normalize_path(bool local, std::string p)
{
    p = trim(p);
    std::replace(p.begin(), p.end(), '\\', '/');
    if (local) {
        if (p.empty())
            return p;
        if (p.size() == 2 && p[1] == ':')
            return p + "/";
        while (p.size() > 3 && p.back() == '/')
            p.pop_back();
        return p;
    }
    while (!p.empty() && p.front() == '/')
        p.erase(0, 1);
    while (!p.empty() && p.back() == '/')
        p.pop_back();
    return p;
}

std::string up_path(bool local, const std::string &p)
{
    if (local) {
        if (p.size() <= 3)
            return {};
        size_t pos = p.find_last_of('/');
        if (pos == std::string::npos)
            return {};
        std::string r = p.substr(0, pos);
        if (r.size() == 2)
            r += "/";
        return r;
    }
    size_t pos = p.find_last_of('/');
    return pos == std::string::npos ? std::string() : p.substr(0, pos);
}

std::string entry_join(const PaneState &p, const std::string &name)
{
    return join_path(p.spec(), name);
}

class Controller {
public:
    explicit Controller(slint::ComponentHandle<AppWindow> ui) : ui_(std::move(ui)) { }
    void start();
    void shutdown();
    bool minimize_to_tray() const { return store_.settings.minimize_to_tray; }

private:
    const Backend &B() const { return ui_->global<Backend>(); }

    // ---- infrastructure
    void toast(const std::string &msg, bool error = false);
    void confirm(const std::string &title, const std::string &text, const std::string &ok, bool danger,
                 std::function<void()> action);
    void ask(const std::string &title, const std::string &text, const std::string &hint,
             const std::string &initial, const std::string &ok,
             std::function<void(const std::string &)> action);
    void add_log(const std::string &level, const std::string &msg);
    // Saves the store and tells the user (once) when the disk write fails.
    bool persist();
    void record_op(const std::string &mode, const std::string &title, const std::string &src, const std::string &dst,
                   bool ok, const std::string &err, int64_t files, int64_t bytes);
    void bind_callbacks();
    void load_remotes_async();
    void apply_remotes(std::vector<RemoteInfo> list);
    void refresh_rclone_state();
    std::string effective_exe_path() const;

    // ---- explorer
    int remote_index_of(const std::string &remote) const;
    void sync_pane_header(int side);
    void navigate(int side, const std::string &remote, const std::string &path, bool push_history = true);
    void load(int side);
    void sort_entries(PaneState &p);
    void rebuild_model(int side);
    void apply_selection(int side);
    void update_summary(int side);
    void refresh_pane(int side) { load(side); }
    void pane_click(int side, int row, bool ctrl, bool shift);
    void pane_open(int side, int row);
    void pane_remote(int side, int index);
    void pane_sort(int side, const std::string &key);
    std::vector<Entry> selected_entries(int side) const;
    void transfer_selected(bool to_right, bool move);
    void delete_selected();
    void new_folder();
    void rename_selected();
    void copy_link();

    // ---- tasks
    void start_task(std::shared_ptr<Task> t, std::vector<Step> steps,
                    std::function<void(bool, bool, const std::string &)> on_done = {});
    void refresh_active();
    void finish_task(const std::shared_ptr<Task> &t, bool ok, bool cancelled, const std::string &err);
    TransferOptions base_options() const;
    TransferOptions with_defaults(TransferOptions o) const;

    // ---- compare
    void start_compare();
    void run_compare();
    void rebuild_compare_rows();
    void compare_copy(bool to_right);
    void compare_delete(bool left);
    void close_compare();

    // ---- remotes
    void refresh_remote_rows();
    void select_remote(int i);
    void remote_new();
    void remote_edit();
    void save_remote_edit();
    void apply_provider(int i);
    void create_remote();
    void cancel_create_remote();
    void delete_remote();
    void reconnect_remote();
    void remote_reauth();
    void refresh_drive_ui();
    void note_throttle(const std::shared_ptr<Task> &t, const std::string &src, const std::string &dst);
    void note_auth_error(const std::shared_ptr<Task> &t, const std::string &src, const std::string &dst);
    void offer_reauth(const std::string &remote);
    void reauth_remote_named(const std::string &name);
    void select_drive_remote(int i);
    void save_drive_creds();

    // ---- jobs
    void refresh_jobs();
    void open_job_form(const Job *job, bool from_panes);
    bool save_job_from_form(std::string &id_out);
    void run_job(const std::string &id, bool dry, const std::string &trigger);
    void check_schedules();
    void sync_background_scheduler();
    std::string schedule_text(const Schedule &s) const;
    void export_jobs();
    void import_jobs();

    // ---- mounts
    void refresh_mounts();
    void open_mount_form(const std::string &spec, const std::string &name);
    void save_mount();
    void start_mount(const std::string &id);
    void stop_mount(const std::string &id);
    void toggle_mount();
    void poll_mounts();

    // ---- history / settings
    void refresh_history();
    void load_settings_into_ui();
    void apply_settings();

    slint::ComponentHandle<AppWindow> ui_;
    Rclone rc_;
    Store store_;

    std::vector<RemoteInfo> remotes_;
    std::vector<std::string> remote_names_; // index 0 = local disk
    PaneState pane_[2];

    std::vector<std::shared_ptr<Task>> tasks_;
    std::shared_ptr<slint::VectorModel<TransferRow>> active_model_ = std::make_shared<slint::VectorModel<TransferRow>>();
    std::shared_ptr<slint::VectorModel<TransferRow>> done_model_ = std::make_shared<slint::VectorModel<TransferRow>>();
    std::shared_ptr<slint::VectorModel<LogRow>> log_model_ = std::make_shared<slint::VectorModel<LogRow>>();
    std::set<std::string> running_jobs_;
    std::set<std::string> reauth_busy_;
    bool save_warned_ = false;
    bool background_scheduler_active_ = false;

    std::shared_ptr<slint::VectorModel<slint::SharedString>> remote_names_model_ = std::make_shared<slint::VectorModel<slint::SharedString>>();
    std::shared_ptr<slint::VectorModel<RemoteRow>> remote_rows_ = std::make_shared<slint::VectorModel<RemoteRow>>();
    std::shared_ptr<slint::VectorModel<slint::SharedString>> provider_labels_ = std::make_shared<slint::VectorModel<slint::SharedString>>();
    std::shared_ptr<slint::VectorModel<FieldRow>> field_rows_ = std::make_shared<slint::VectorModel<FieldRow>>();
    std::vector<std::string> field_values_;
    std::shared_ptr<ProcessHandle> remote_handle_;
    std::string last_suggestion_;
    std::string edit_name_;
    std::string edit_type_;
    std::vector<std::string> edit_keys_;
    std::vector<std::string> edit_initial_;
    std::vector<bool> edit_secret_;
    int about_gen_ = 0;
    std::vector<std::string> drive_names_;
    std::shared_ptr<slint::VectorModel<slint::SharedString>> drive_model_ = std::make_shared<slint::VectorModel<slint::SharedString>>();
    std::shared_ptr<ProcessHandle> drive_handle_;

    std::shared_ptr<slint::VectorModel<JobRow>> job_rows_ = std::make_shared<slint::VectorModel<JobRow>>();
    std::string editing_job_id_;

    std::shared_ptr<slint::VectorModel<MountRow>> mount_rows_ = std::make_shared<slint::VectorModel<MountRow>>();
    std::map<std::string, MountRt> mount_rt_;

    std::shared_ptr<slint::VectorModel<HistoryRow>> history_rows_ = std::make_shared<slint::VectorModel<HistoryRow>>();

    // compare
    std::vector<CompareItem> cmp_items_;
    std::vector<char> cmp_sel_;
    std::vector<int> cmp_visible_;
    std::string cmp_left_, cmp_right_;
    std::shared_ptr<ProcessHandle> cmp_handle_;
    int cmp_gen_ = 0;
    std::shared_ptr<slint::VectorModel<CompareRow>> cmp_model_ = std::make_shared<slint::VectorModel<CompareRow>>();

    std::function<void()> confirm_action_;
    std::function<void(const std::string &)> input_action_;
    slint::Timer toast_timer_;
    slint::Timer poll_timer_;
    int tick_ = 0;
    static constexpr int kSpeedSamples = 60;
    std::deque<double> speed_hist_;
    std::shared_ptr<slint::VectorModel<float>> speed_model_ = std::make_shared<slint::VectorModel<float>>();
    void sample_speed();
    void check_updates(bool manual);
    void on_update_result(const updater::Release &r, bool manual);
    void start_update(const updater::Release &r);
    bool update_busy_ = false;
};

// ------------------------------------------------------------------ infrastructure

void Controller::toast(const std::string &msg, bool error)
{
    B().set_toast(S(msg));
    B().set_toast_error(error);
    B().set_toast_visible(true);
    toast_timer_.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(error ? 8000 : 4000),
                       [this] { B().set_toast_visible(false); });
}

void Controller::confirm(const std::string &title, const std::string &text, const std::string &ok,
                         bool danger, std::function<void()> action)
{
    confirm_action_ = std::move(action);
    input_action_ = nullptr;
    B().set_dialog_title(S(title));
    B().set_dialog_text(S(text));
    B().set_dialog_ok(S(ok));
    B().set_dialog_danger(danger);
    B().set_dialog(S("confirm"));
}

void Controller::ask(const std::string &title, const std::string &text, const std::string &hint,
                     const std::string &initial, const std::string &ok,
                     std::function<void(const std::string &)> action)
{
    input_action_ = std::move(action);
    confirm_action_ = nullptr;
    B().set_dialog_title(S(title));
    B().set_dialog_text(S(text));
    B().set_dialog_hint(S(hint));
    B().set_dialog_input(S(initial));
    B().set_dialog_ok(S(ok));
    B().set_dialog_danger(false);
    B().set_dialog(S("input"));
}

void Controller::add_log(const std::string &level, const std::string &msg)
{
    std::string lv = level == "error" ? "ERROR" : level == "warning" ? "WARN" : level == "debug" ? "DEBUG" : "INFO";
    if (lv == "DEBUG")
        return;
    LogRow r;
    r.time = S(now_clock());
    r.level = S(lv);
    r.text = S(msg);
    log_model_->insert(0, r);
    while (log_model_->row_count() > 600)
        log_model_->erase(log_model_->row_count() - 1);
}

bool Controller::persist()
{
    if (store_.save()) {
        save_warned_ = false;
        return true;
    }
    plat::log_error("No se pudo guardar: " + store_.save_error);
    if (!save_warned_) {
        save_warned_ = true;
        toast("No se pudieron guardar los datos (" + store_.save_error + "). Comprueba el espacio en disco y los permisos de %APPDATA%\\RcloneLite.", true);
    }
    return false;
}

void Controller::record_op(const std::string &mode, const std::string &title, const std::string &src,
                           const std::string &dst, bool ok, const std::string &err, int64_t files, int64_t bytes)
{
    HistoryEntry h;
    h.time = now_unix();
    h.job = title;
    h.mode = mode;
    h.source = src;
    h.destination = dst;
    h.status = ok ? "ok" : "error";
    h.trigger = "manual";
    h.bytes = bytes;
    h.files = files;
    h.error = err.size() > 400 ? err.substr(0, 400) : err;
    store_.add_history(std::move(h));
    persist();
    refresh_history();
}

void Controller::note_auth_error(const std::shared_ptr<Task> &t, const std::string &src, const std::string &dst)
{
    if (t->auth_noted)
        return;
    t->auth_noted = true;
    for (const auto &spec : { src, dst }) {
        size_t colon = spec.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = spec.substr(0, colon);
        auto it = std::find_if(remotes_.begin(), remotes_.end(),
                               [&](const RemoteInfo &r) { return r.name == name && is_oauth_type(r.type); });
        if (it != remotes_.end()) {
            add_log("warning", "El acceso de «" + name + "» puede haber caducado. Vuelve a autenticarlo.");
            offer_reauth(name);
            return;
        }
    }
}

void Controller::offer_reauth(const std::string &remote)
{
    if (!T(B().get_dialog()).empty() || reauth_busy_.count(remote))
        return;
    confirm("Acceso caducado", "La sesión de «" + remote + "» parece haber caducado. ¿Quieres volver a iniciar sesión en el navegador?",
            "Reautenticar", false, [this, remote] { reauth_remote_named(remote); });
}

void Controller::reauth_remote_named(const std::string &name)
{
    if (!reauth_busy_.insert(name).second)
        return;
    toast("Se abrirá el navegador para renovar el acceso a «" + name + "»…");
    spawn([this, name] {
        OpResult r = rc_.reconnect_remote(name);
        post([this, name, r] {
            reauth_busy_.erase(name);
            if (r.ok) {
                toast("«" + name + "» reconectado.");
                add_log("info", "Remoto reautenticado: " + name);
            } else {
                toast(r.message, true);
            }
        });
    });
}

std::string Controller::effective_exe_path() const
{
    return store_.settings.rclone_path.empty() ? Rclone::auto_detect() : store_.settings.rclone_path;
}

void Controller::refresh_rclone_state()
{
    rc_.set_executable(effective_exe_path());
    B().set_rclone_ok(rc_.available());
    B().set_winfsp_ok(plat::winfsp_installed());
    if (!rc_.available()) {
        B().set_rclone_version(S(""));
        return;
    }
    spawn([this] {
        std::string v = rc_.version();
        post([this, v] { B().set_rclone_version(S(v)); });
    });
}

void Controller::load_remotes_async()
{
    if (!rc_.available()) {
        apply_remotes({});
        return;
    }
    spawn([this] {
        auto list = rc_.list_remotes();
        post([this, list]() mutable { apply_remotes(std::move(list)); });
    });
}

void Controller::apply_remotes(std::vector<RemoteInfo> list)
{
    std::sort(list.begin(), list.end(), [](const RemoteInfo &a, const RemoteInfo &b) {
        return lower(a.name) < lower(b.name);
    });
    rc_.set_remote_types(list);
    remotes_ = std::move(list);
    remote_names_.clear();
    remote_names_.push_back("Disco local");
    for (auto &r : remotes_)
        remote_names_.push_back(r.name + ":");

    std::vector<slint::SharedString> names;
    for (auto &n : remote_names_)
        names.push_back(S(n));
    remote_names_model_->set_vector(names);

    refresh_remote_rows();
    refresh_drive_ui();

    for (int side = 0; side < 2; ++side) {
        auto &p = pane_[side];
        bool exists = p.remote.empty();
        for (auto &r : remotes_)
            if (r.name == p.remote)
                exists = true;
        if (!exists) {
            p.remote.clear();
            p.path.clear();
            p.entries.clear();
        }
        sync_pane_header(side);
    }
}

// ------------------------------------------------------------------ explorer

int Controller::remote_index_of(const std::string &remote) const
{
    if (remote.empty())
        return 0;
    for (size_t i = 0; i < remotes_.size(); ++i)
        if (remotes_[i].name == remote)
            return static_cast<int>(i) + 1;
    return 0;
}

void Controller::sync_pane_header(int side)
{
    auto &p = pane_[side];
    int idx = remote_index_of(p.remote);
    if (side == 0) {
        B().set_left_remote_index(idx);
        B().set_left_path(S(p.path));
    } else {
        B().set_right_remote_index(idx);
        B().set_right_path(S(p.path));
    }
}

void Controller::navigate(int side, const std::string &remote, const std::string &path, bool push_history)
{
    auto &p = pane_[side];
    if (push_history && (p.remote != remote || p.path != path || p.entries.empty()))
        p.back.push_back({ p.remote, p.path });
    if (p.back.size() > 100)
        p.back.erase(p.back.begin());
    p.remote = remote;
    p.path = path;
    p.selected.clear();
    p.anchor = -1;
    sync_pane_header(side);
    load(side);
}

void Controller::sort_entries(PaneState &p)
{
    const std::string key = p.sort_key;
    const bool asc = p.sort_asc;
    std::stable_sort(p.entries.begin(), p.entries.end(), [&](const Entry &a, const Entry &b) {
        if (a.is_dir != b.is_dir)
            return a.is_dir;
        int c = 0;
        if (key == "size")
            c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
        else if (key == "date")
            c = a.modtime < b.modtime ? -1 : a.modtime > b.modtime ? 1 : 0;
        if (c == 0) {
            std::string la = lower(a.name), lb = lower(b.name);
            c = la < lb ? -1 : la > lb ? 1 : 0;
            return (key == "name" && !asc) ? c > 0 : c < 0;
        }
        return asc ? c < 0 : c > 0;
    });
}

void Controller::rebuild_model(int side)
{
    auto &p = pane_[side];
    std::vector<FileRow> rows;
    rows.reserve(p.entries.size());
    for (auto &e : p.entries) {
        FileRow r;
        r.name = S(e.name);
        r.size = S(e.is_dir ? "" : format_size(e.size));
        r.date = S(format_date(e.modtime));
        r.is_dir = e.is_dir;
        r.selected = p.selected.count(e.name) > 0;
        rows.push_back(std::move(r));
    }
    p.model->set_vector(std::move(rows));
    update_summary(side);
}

void Controller::apply_selection(int side)
{
    auto &p = pane_[side];
    for (size_t i = 0; i < p.entries.size(); ++i) {
        bool sel = p.selected.count(p.entries[i].name) > 0;
        auto row = p.model->row_data(i);
        if (row && row->selected != sel) {
            FileRow r = *row;
            r.selected = sel;
            p.model->set_row_data(i, r);
        }
    }
    update_summary(side);
}

void Controller::update_summary(int side)
{
    auto &p = pane_[side];
    int dirs = 0, files = 0;
    int64_t total = 0, sel_size = 0;
    int sel = 0;
    for (auto &e : p.entries) {
        (e.is_dir ? dirs : files)++;
        total += e.size;
        if (p.selected.count(e.name)) {
            ++sel;
            sel_size += e.size;
        }
    }
    std::string s;
    if (p.drives_view())
        s = std::to_string(dirs) + " unidades";
    else
        s = std::to_string(dirs) + " carpetas, " + std::to_string(files) + " archivos · " + format_size(total);
    if (sel > 0)
        s += "   |   " + std::to_string(sel) + " seleccionados" + (sel_size ? " (" + format_size(sel_size) + ")" : "");
    if (side == 0)
        B().set_left_summary(S(s));
    else
        B().set_right_summary(S(s));
}

void Controller::load(int side)
{
    auto &p = pane_[side];
    int gen = ++p.gen;
    p.error.clear();
    auto set_state = [this, side](bool loading, const std::string &err) {
        pane_[side].loading = loading;
        if (side == 0) {
            B().set_left_loading(loading);
            B().set_left_error(S(err));
        } else {
            B().set_right_loading(loading);
            B().set_right_error(S(err));
        }
    };

    if (p.drives_view()) {
        p.entries.clear();
        for (auto &d : plat::local_drives()) {
            Entry e;
            e.name = d;
            e.is_dir = true;
            p.entries.push_back(e);
        }
        set_state(false, "");
        rebuild_model(side);
        return;
    }
    if (!rc_.available()) {
        p.entries.clear();
        set_state(false, "rclone no está disponible.");
        rebuild_model(side);
        return;
    }

    set_state(true, "");
    std::string spec = p.spec();
    spawn([this, side, gen, spec, set_state] {
        std::string err;
        auto r = rc_.list(spec, &err);
        post([this, side, gen, set_state, r, err]() mutable {
            auto &p = pane_[side];
            if (gen != p.gen)
                return;
            if (!r) {
                p.entries.clear();
                p.selected.clear();
                set_state(false, err);
            } else {
                p.entries = std::move(*r);
                sort_entries(p);
                std::set<std::string> keep;
                for (auto &e : p.entries)
                    if (p.selected.count(e.name))
                        keep.insert(e.name);
                p.selected = std::move(keep);
                set_state(false, "");
            }
            rebuild_model(side);
        });
    });
}

void Controller::pane_click(int side, int row, bool ctrl, bool shift)
{
    auto &p = pane_[side];
    if (row < 0 || row >= static_cast<int>(p.entries.size()))
        return;
    const std::string &name = p.entries[row].name;
    if (shift && p.anchor >= 0 && p.anchor < static_cast<int>(p.entries.size())) {
        if (!ctrl)
            p.selected.clear();
        int a = std::min(p.anchor, row), b = std::max(p.anchor, row);
        for (int i = a; i <= b; ++i)
            p.selected.insert(p.entries[i].name);
    } else if (ctrl) {
        if (!p.selected.erase(name))
            p.selected.insert(name);
        p.anchor = row;
    } else {
        p.selected.clear();
        p.selected.insert(name);
        p.anchor = row;
    }
    apply_selection(side);
}

void Controller::pane_open(int side, int row)
{
    auto &p = pane_[side];
    if (row < 0 || row >= static_cast<int>(p.entries.size()))
        return;
    const Entry &e = p.entries[row];
    if (e.is_dir) {
        if (p.drives_view())
            navigate(side, "", e.name + "/");
        else
            navigate(side, p.remote, join_path(p.path, e.name));
    } else if (p.remote.empty()) {
        plat::open_path(join_path(p.path, e.name));
    }
}

void Controller::pane_remote(int side, int index)
{
    std::string remote;
    std::string path;
    if (index > 0 && index <= static_cast<int>(remotes_.size()))
        remote = remotes_[index - 1].name;
    else
        path = side == 0 ? plat::user_profile() : "";
    std::replace(path.begin(), path.end(), '\\', '/');
    navigate(side, remote, path);
}

void Controller::pane_sort(int side, const std::string &key)
{
    auto &p = pane_[side];
    if (p.sort_key == key)
        p.sort_asc = !p.sort_asc;
    else {
        p.sort_key = key;
        p.sort_asc = true;
    }
    sort_entries(p);
    if (side == 0) {
        B().set_left_sort_key(S(p.sort_key));
        B().set_left_sort_asc(p.sort_asc);
    } else {
        B().set_right_sort_key(S(p.sort_key));
        B().set_right_sort_asc(p.sort_asc);
    }
    rebuild_model(side);
}

std::vector<Entry> Controller::selected_entries(int side) const
{
    std::vector<Entry> out;
    for (auto &e : pane_[side].entries)
        if (pane_[side].selected.count(e.name))
            out.push_back(e);
    return out;
}

TransferOptions Controller::base_options() const
{
    TransferOptions o;
    o.transfers = store_.settings.transfers;
    o.checkers = store_.settings.checkers;
    o.update_existing = store_.settings.update_existing;
    o.bwlimit = store_.settings.bwlimit;
    o.extra_flags = store_.settings.extra_flags;
    return o;
}

TransferOptions Controller::with_defaults(TransferOptions o) const
{
    o.update_existing = o.update_existing || store_.settings.update_existing;
    if (trim(o.bwlimit).empty())
        o.bwlimit = store_.settings.bwlimit;
    if (!trim(store_.settings.extra_flags).empty())
        o.extra_flags = store_.settings.extra_flags + " " + o.extra_flags;
    return o;
}

void Controller::transfer_selected(bool to_right, bool move)
{
    int from = to_right ? 0 : 1, to = to_right ? 1 : 0;
    auto &P = pane_[from];
    auto &D = pane_[to];
    auto sel = selected_entries(from);
    if (sel.empty()) {
        toast(std::string("Selecciona archivos o carpetas en el panel ") + (from == 0 ? "izquierdo." : "derecho."), true);
        return;
    }
    if (P.drives_view() || D.drives_view()) {
        toast("Abre una carpeta en ambos paneles (no la lista de unidades).", true);
        return;
    }
    if (P.spec() == D.spec()) {
        toast("El origen y el destino son la misma carpeta.", true);
        return;
    }

    std::vector<Step> steps;
    std::vector<std::string> file_names;
    for (auto &e : sel) {
        if (!e.is_dir) {
            file_names.push_back(e.name);
            continue;
        }
        Step s;
        s.src = entry_join(P, e.name);
        s.dst = entry_join(D, e.name);
        s.mode = move ? "move" : "copy";
        s.opt = base_options();
        s.opt.mode = s.mode; // rclone::transfer() reads the mode from the options
        if (move)
            s.opt.extra_flags += " --delete-empty-src-dirs";
        steps.push_back(std::move(s));
    }
    // Several loose files go in a single rclone run instead of one process per file.
    if (file_names.size() == 1) {
        Step s;
        s.src = entry_join(P, file_names[0]);
        s.dst = entry_join(D, file_names[0]);
        s.file = true;
        s.mode = move ? "move" : "copy";
        s.opt = base_options();
        steps.insert(steps.begin(), std::move(s));
    } else if (file_names.size() > 1) {
        const bool raw_list_safe = std::all_of(file_names.begin(), file_names.end(), valid_files_from_raw_name);
        if (raw_list_safe) {
            Step s;
            s.src = P.spec();
            s.dst = D.spec();
            s.mode = move ? "move" : "copy";
            s.opt = base_options();
            s.opt.mode = s.mode;
            s.temp_file = write_temp_lines(file_names);
            if (s.temp_file.empty()) {
                toast("No se pudo crear la lista temporal de archivos.", true);
                return;
            }
            s.opt.files_from = s.temp_file;
            steps.insert(steps.begin(), std::move(s));
        } else {
            // A newline is an item separator in --files-from-raw. Keep unusual remote names
            // intact by transferring those files individually instead of splitting their names.
            std::vector<Step> individual_steps;
            for (const auto &name : file_names) {
                Step s;
                s.src = entry_join(P, name);
                s.dst = entry_join(D, name);
                s.file = true;
                s.mode = move ? "move" : "copy";
                s.opt = base_options();
                individual_steps.push_back(std::move(s));
            }
            steps.insert(steps.begin(), std::make_move_iterator(individual_steps.begin()),
                         std::make_move_iterator(individual_steps.end()));
        }
    }
    auto t = std::make_shared<Task>();
    t->id = new_id();
    t->mode = move ? "move" : "copy";
    t->title = std::string(move ? "Mover " : "Copiar ") + (sel.size() == 1 ? sel[0].name : std::to_string(sel.size()) + " elementos");
    t->route = P.spec() + "  →  " + D.spec();
    start_task(t, std::move(steps), [this, from, to, move](bool ok, bool, const std::string &) {
        (void)ok;
        refresh_pane(to);
        if (move)
            refresh_pane(from);
    });
}

void Controller::delete_selected()
{
    int side = B().get_active_side();
    auto &P = pane_[side];
    auto sel = selected_entries(side);
    if (sel.empty()) {
        toast("Selecciona qué quieres eliminar.", true);
        return;
    }
    if (P.drives_view()) {
        toast("No se pueden eliminar unidades.", true);
        return;
    }
    auto run = [this, side, sel]() {
        auto &P = pane_[side];
        std::string base = P.spec();
        pane_[side].loading = true;
        if (side == 0) B().set_left_loading(true); else B().set_right_loading(true);
        spawn([this, side, sel, base] {
            std::string errors;
            int ok = 0;
            int64_t files = 0, bytes = 0;
            for (auto &e : sel) {
                std::string spec = join_path(base, e.name);
                OpResult r = e.is_dir ? rc_.purge(spec) : rc_.delete_file(spec);
                if (r.ok) {
                    ++ok;
                    if (!e.is_dir) {
                        ++files;
                        bytes += e.size;
                    }
                } else {
                    if (!errors.empty())
                        errors += "\n";
                    errors += e.name + ": " + r.message;
                }
            }
            post([this, side, ok, errors, base, files, bytes] {
                record_op("delete", "Eliminar " + std::to_string(ok) + " elementos", base, "", errors.empty(),
                          errors, files, bytes);
                add_log(errors.empty() ? "info" : "error",
                        errors.empty() ? "Eliminados " + std::to_string(ok) + " elementos" : errors);
                if (errors.empty())
                    toast("Se eliminaron " + std::to_string(ok) + " elementos.");
                else
                    toast(errors.substr(0, 700), true);
                refresh_pane(side);
            });
        });
    };
    if (!store_.settings.confirm_delete) {
        run();
        return;
    }
    std::string what = sel.size() == 1 ? "«" + sel[0].name + "»" : std::to_string(sel.size()) + " elementos";
    confirm("Eliminar", "Se eliminará " + what + " de " + P.spec() + ".\nEsta acción no se puede deshacer.",
            "Eliminar", true, run);
}

void Controller::new_folder()
{
    int side = B().get_active_side();
    if (pane_[side].drives_view()) {
        toast("Abre una unidad o remoto para crear carpetas.", true);
        return;
    }
    ask("Carpeta nueva", "Se creará en " + pane_[side].spec(), "Nombre de la carpeta", "", "Crear",
        [this, side](const std::string &name) {
            std::string spec = entry_join(pane_[side], name);
            spawn([this, side, spec] {
                OpResult r = rc_.mkdir(spec);
                post([this, side, r] {
                    if (!r.ok)
                        toast(r.message, true);
                    refresh_pane(side);
                });
            });
        });
}

void Controller::rename_selected()
{
    int side = B().get_active_side();
    auto sel = selected_entries(side);
    if (sel.size() != 1) {
        toast("Selecciona un único elemento para renombrar.", true);
        return;
    }
    if (pane_[side].drives_view())
        return;
    std::string old = sel[0].name;
    bool is_dir = sel[0].is_dir;
    ask("Renombrar", "Nuevo nombre para «" + old + "»", "Nombre", old, "Renombrar",
        [this, side, old, is_dir](const std::string &name) {
            if (name == old)
                return;
            std::string from = entry_join(pane_[side], old);
            std::string to = entry_join(pane_[side], name);
            spawn([this, side, from, to, is_dir] {
                OpResult r = rc_.rename(from, to, is_dir);
                post([this, side, r] {
                    if (!r.ok)
                        toast(r.message, true);
                    pane_[side].selected.clear();
                    refresh_pane(side);
                });
            });
        });
}

void Controller::copy_link()
{
    int side = B().get_active_side();
    auto sel = selected_entries(side);
    if (sel.size() != 1) {
        toast("Selecciona un único elemento.", true);
        return;
    }
    std::string spec = entry_join(pane_[side], sel[0].name);
    if (pane_[side].remote.empty()) {
        plat::set_clipboard(spec);
        toast("Ruta copiada al portapapeles.");
        return;
    }
    toast("Obteniendo enlace…");
    spawn([this, spec] {
        std::string link = rc_.link(spec);
        post([this, link] {
            if (link.empty())
                toast("Este remoto no permite crear enlaces públicos.", true);
            else {
                plat::set_clipboard(link);
                toast("Enlace copiado al portapapeles.");
            }
        });
    });
}

// ------------------------------------------------------------------ tasks

void Controller::refresh_active()
{
    std::vector<TransferRow> rows;
    double total_speed = 0;
    for (auto &t : tasks_) {
        TransferRow r;
        r.id = S(t->id);
        r.title = S(t->title);
        r.detail = S(t->detail);
        r.progress = t->progress;
        r.speed = S(t->speed);
        r.state = S("running");
        r.info = S("");
        rows.push_back(std::move(r));
        total_speed += t->speed_val;
    }
    active_model_->set_vector(std::move(rows));
    B().set_total_speed(S(total_speed > 0 ? format_speed(total_speed) : ""));
}

// ------------------------------------------------------------------ updates

void Controller::check_updates(bool manual)
{
    if (update_busy_) {
        if (manual)
            toast("Ya hay una actualización en curso.");
        return;
    }
    update_busy_ = true;
    B().set_update_busy(true);
    B().set_update_status(S("Buscando actualizaciones…"));
    spawn([this, manual] {
        updater::Release r = updater::fetch_latest();
        post([this, r, manual] { on_update_result(r, manual); });
    });
}

void Controller::on_update_result(const updater::Release &r, bool manual)
{
    update_busy_ = false;
    B().set_update_busy(false);

    if (!r.ok) {
        B().set_update_status(S(manual ? "No se pudo comprobar: " + r.error : ""));
        if (manual)
            toast("No se pudo buscar actualizaciones: " + r.error, true);
        else
            plat::log_error("Comprobación de actualizaciones: " + r.error);
        return;
    }
    if (!updater::is_newer(r.version, APP_VERSION)) {
        B().set_update_status(S("Tienes la última versión (" + std::string(APP_VERSION) + ")."));
        if (manual)
            toast("Rclone Lite está actualizado.");
        return;
    }

    B().set_update_status(S("Hay una versión nueva: " + r.version));
    if (!manual && !T(B().get_dialog()).empty())
        return; // do not interrupt another dialog; the user can check from Settings

    std::string text = "Está disponible Rclone Lite " + r.version + " (tienes la " + APP_VERSION + ").";
    if (r.size > 0)
        text += "\nDescarga: " + format_size(r.size) + ".";
    text += "\n\nSe descargará, se comprobará su integridad y se instalará. La aplicación se cerrará y volverá a abrirse sola.";
    if (!tasks_.empty())
        text += "\n\nAtención: hay transferencias en curso y se cancelarán.";
    confirm("Actualización disponible", text, "Actualizar ahora", false, [this, r] { start_update(r); });
}

void Controller::start_update(const updater::Release &r)
{
    if (update_busy_)
        return;
    update_busy_ = true;
    B().set_update_busy(true);
    B().set_update_status(S("Descargando la versión " + r.version + "…"));
    toast("Descargando la actualización…");
    add_log("info", "Descargando Rclone Lite " + r.version);

    std::string dest = plat::temp_dir() + "\\RcloneLite-update\\" + r.installer_name;
    spawn([this, r, dest] {
        auto last = std::chrono::steady_clock::now();
        std::string err = updater::download(r.installer_url, dest, [&](int64_t done, int64_t) {
            auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds(400))
                return;
            last = now;
            int64_t total = r.size;
            std::string msg = "Descargando la versión " + r.version + ": " + format_size(done) +
                              (total > 0 ? " de " + format_size(total) : "");
            post([this, msg] { B().set_update_status(S(msg)); });
        });
        if (err.empty()) {
            std::string got = updater::sha256_file(dest);
            if (got != r.sha256) {
                err = "La descarga está dañada o no coincide con la suma SHA-256 publicada.";
                std::error_code ec;
                std::filesystem::remove(std::filesystem::path(utf8_to_wide(dest)), ec);
            }
        }
        post([this, err, dest, r] {
            update_busy_ = false;
            B().set_update_busy(false);
            if (!err.empty()) {
                B().set_update_status(S("Error al actualizar: " + err));
                toast("No se pudo actualizar: " + err, true);
                add_log("error", "Actualización: " + err);
                return;
            }
            B().set_update_status(S("Instalando la versión " + r.version + "…"));
            if (!updater::run_installer(dest)) {
                B().set_update_status(S("No se pudo iniciar el instalador."));
                toast("No se pudo iniciar el instalador (¿cancelaste el permiso de administrador?).", true);
                return;
            }
            slint::quit_event_loop();
        });
    });
}
// Called once per second: pushes the combined speed of all running tasks into the chart history.
void Controller::sample_speed()
{
    double total = 0;
    for (auto &t : tasks_)
        total += t->speed_val;
    if (tasks_.empty() && speed_hist_.empty())
        return;

    speed_hist_.push_back(total);
    while (static_cast<int>(speed_hist_.size()) > kSpeedSamples)
        speed_hist_.pop_front();

    if (tasks_.empty()) {
        bool all_zero = std::all_of(speed_hist_.begin(), speed_hist_.end(), [](double v) { return v <= 0; });
        if (all_zero) {
            speed_hist_.clear();
            speed_model_->set_vector({});
            B().set_speed_peak(S(""));
            return;
        }
    }

    double peak = *std::max_element(speed_hist_.begin(), speed_hist_.end());
    std::vector<float> norm;
    norm.reserve(kSpeedSamples);
    for (int i = static_cast<int>(speed_hist_.size()); i < kSpeedSamples; ++i)
        norm.push_back(0.f);
    for (double v : speed_hist_)
        norm.push_back(peak > 0 ? static_cast<float>(v / peak) : 0.f);
    speed_model_->set_vector(std::move(norm));
    B().set_speed_peak(S(peak > 0 ? "máx. " + format_speed(peak) : ""));
}

void Controller::start_task(std::shared_ptr<Task> t, std::vector<Step> steps,
                            std::function<void(bool, bool, const std::string &)> on_done)
{
    if (!rc_.available()) {
        toast("rclone no está disponible.", true);
        return;
    }
    t->dry = !steps.empty() && steps[0].opt.dry_run;
    tasks_.push_back(t);
    refresh_active();
    B().set_bottom_tab(0);
    B().set_bottom_open(true);
    add_log("info", t->title + "  (" + t->route + ")");

    spawn([this, t, steps = std::move(steps), on_done = std::move(on_done)] {
        int64_t base_bytes = 0, base_total = 0, base_files = 0;
        bool ok = true;
        std::string err;
        // One failing step (e.g. one unreachable destination) must not stop the other ones.
        std::vector<std::string> failures;
        for (auto &s : steps) {
            if (t->handle.killed()) {
                ok = false;
                err = "Cancelado por el usuario.";
                break;
            }
            Stats last;
            auto on_stats = [&](const Stats &st) {
                last = st;
                int64_t b = base_bytes + st.bytes, tot = base_total + st.total_bytes, f = base_files + st.transfers;
                std::string detail = format_size(b) + " de " + format_size(tot) + "  ·  " + std::to_string(f) + " archivos";
                if (!st.active.empty())
                    detail += "  ·  " + st.active[0].name;
                std::string sp = st.speed > 0 ? format_speed(st.speed) + "  ·  " + format_eta(st.eta) : "";
                float prog = tot > 0 ? static_cast<float>(static_cast<double>(b) / static_cast<double>(tot)) : 0.f;
                double speed = st.speed;
                post([this, t, b, tot, f, detail, sp, prog, speed] {
                    t->bytes = b;
                    t->total = tot;
                    t->files = f;
                    t->detail = t->throttled ? detail + "  |  Google limita las peticiones" : detail;
                    t->speed = sp;
                    t->speed_val = speed;
                    t->progress = prog;
                    refresh_active();
                });
            };
            auto on_log = [&, src = s.src, dst = s.dst](const std::string &level, const std::string &msg) {
                if (level == "error" || level == "warning" || level == "notice" || level == "info")
                    post([this, level, msg] { add_log(level, msg); });
                if (is_throttle_message(msg))
                    post([this, t, src, dst] { note_throttle(t, src, dst); });
                if (is_auth_error(msg))
                    post([this, t, src, dst] { note_auth_error(t, src, dst); });
            };
            OpResult r = s.file ? rc_.transfer_file(s.src, s.dst, s.mode == "move", s.opt, on_stats, on_log, &t->handle)
                                : rc_.transfer(s.src, s.dst, s.opt, on_stats, on_log, &t->handle);
            remove_temp_file(s.temp_file);
            base_bytes += last.bytes;
            base_total += last.total_bytes;
            base_files += last.transfers;
            if (!r.ok) {
                if (t->handle.killed()) {
                    ok = false;
                    err = "Cancelado por el usuario.";
                    break;
                }
                failures.push_back(steps.size() > 1 ? s.dst + ": " + r.message : r.message);
            }
        }
        if (!failures.empty()) {
            ok = false;
            err = failures.front();
            if (failures.size() > 1) {
                err = std::to_string(failures.size()) + " de " + std::to_string(steps.size()) + " pasos fallaron. ";
                for (size_t i = 0; i < failures.size() && i < 3; ++i)
                    err += (i ? " | " : "") + failures[i];
            }
            if (err.size() > 600)
                err.resize(600);
        }
        for (auto &s : steps)
            remove_temp_file(s.temp_file);
        bool cancelled = t->handle.killed();
        post([this, t, ok, cancelled, err, on_done, base_bytes, base_files]() mutable {
            if (base_bytes > t->bytes)
                t->bytes = base_bytes;
            if (base_files > t->files)
                t->files = base_files;
            finish_task(t, ok, cancelled, err);
            if (on_done)
                on_done(ok, cancelled, err);
        });
    });
}

void Controller::finish_task(const std::shared_ptr<Task> &t, bool ok, bool cancelled, const std::string &err)
{
    tasks_.erase(std::remove(tasks_.begin(), tasks_.end(), t), tasks_.end());
    refresh_active();

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t->start).count();
    std::string state = ok ? "ok" : cancelled ? "cancelled" : "error";

    TransferRow r;
    r.id = S(t->id);
    r.title = S((t->dry ? "Simulación · " : "") + t->title);
    r.detail = S(t->route);
    r.progress = ok ? 1.f : t->progress;
    r.speed = S("");
    r.state = S(state);
    std::string info = ok ? (t->dry ? "Simulación · " : "") + format_size(t->bytes) + " · " + std::to_string(t->files) + " arch. · " + format_duration(secs)
                          : (cancelled ? "Cancelado" : err);
    r.info = S(info);
    done_model_->insert(0, r);
    while (done_model_->row_count() > 200)
        done_model_->erase(done_model_->row_count() - 1);

    if (!t->dry || !ok) {
        HistoryEntry h;
        h.time = now_unix();
        h.job = t->job_name.empty() ? t->title : t->job_name;
        h.mode = t->mode;
        const std::string arrow = "  →  ";
        auto pos = t->route.find(arrow);
        h.source = pos == std::string::npos ? t->route : t->route.substr(0, pos);
        h.destination = pos == std::string::npos ? "" : t->route.substr(pos + arrow.size());
        h.status = state;
        h.trigger = t->trigger;
        h.bytes = t->bytes;
        h.files = t->files;
        h.seconds = secs;
        h.error = ok ? "" : err;
        store_.add_history(std::move(h));
        persist();
        refresh_history();
    }

    add_log(ok ? "info" : "error", (ok ? "Terminado: " : cancelled ? "Cancelado: " : "Error: ") + t->title + (ok || cancelled ? "" : " — " + err));
    if (ok) {
        toast(t->dry ? "Simulación completada. Revisa el Registro para ver los cambios previstos."
                     : "Completado: " + t->title + " (" + format_size(t->bytes) + ")");
        if (t->dry) {
            B().set_bottom_tab(2);
            B().set_bottom_open(true);
        }
    } else if (cancelled) {
        toast("Operación cancelada.");
    } else {
        toast("Error: " + err, true);
    }
    if (store_.settings.notify_done && !cancelled && !plat::app_is_foreground())
        plat::tray_notify(ok ? "Operación completada" : "Operación con error", ok ? t->title + " (" + format_size(t->bytes) + ")" : t->title + ": " + err, !ok);
}

// ------------------------------------------------------------------ compare

void Controller::start_compare()
{
    if (pane_[0].drives_view() || pane_[1].drives_view()) {
        toast("Abre una carpeta en cada panel para compararlas.", true);
        return;
    }
    cmp_left_ = pane_[0].spec();
    cmp_right_ = pane_[1].spec();
    B().set_compare_left(S(cmp_left_));
    B().set_compare_right(S(cmp_right_));
    B().set_compare_open(true);
    run_compare();
}

void Controller::run_compare()
{
    cmp_items_.clear();
    cmp_sel_.clear();
    rebuild_compare_rows();
    B().set_compare_busy(true);
    B().set_compare_summary(S(""));
    int gen = ++cmp_gen_;
    cmp_handle_ = std::make_shared<ProcessHandle>();
    auto handle = cmp_handle_;
    std::string l = cmp_left_, r = cmp_right_;
    TransferOptions opt;
    opt.checkers = store_.settings.checkers;
    spawn([this, gen, handle, l, r, opt] {
        std::vector<CompareItem> items;
        OpResult res = rc_.compare(l, r, opt, items, handle.get());
        post([this, gen, res, items]() mutable {
            if (gen != cmp_gen_)
                return;
            B().set_compare_busy(false);
            if (!res.ok) {
                toast(res.message, true);
                return;
            }
            std::sort(items.begin(), items.end(), [](const CompareItem &a, const CompareItem &b) { return a.path < b.path; });
            cmp_items_ = std::move(items);
            cmp_sel_.assign(cmp_items_.size(), 0);
            rebuild_compare_rows();
        });
    });
}

void Controller::rebuild_compare_rows()
{
    int nl = 0, nr = 0, nd = 0, ne = 0, ner = 0;
    for (auto &i : cmp_items_) {
        switch (i.status) {
        case '-': ++nl; break;
        case '+': ++nr; break;
        case '*': ++nd; break;
        case '=': ++ne; break;
        default: ++ner; break;
        }
    }
    bool sl = B().get_cmp_show_left(), sr = B().get_cmp_show_right(), sd = B().get_cmp_show_diff(), se = B().get_cmp_show_equal();
    cmp_visible_.clear();
    std::vector<CompareRow> rows;
    for (size_t i = 0; i < cmp_items_.size(); ++i) {
        const auto &it = cmp_items_[i];
        bool show = (it.status == '-' && sl) || (it.status == '+' && sr) || (it.status == '*' && sd) ||
                (it.status == '=' && se) || (it.status == '!' && sd);
        if (!show)
            continue;
        cmp_visible_.push_back(static_cast<int>(i));
        CompareRow r;
        r.path = S(it.path);
        r.status = S(std::string(1, it.status));
        r.label = S(it.status == '-' ? "Solo izquierda" : it.status == '+' ? "Solo derecha" : it.status == '*' ? "Diferente" : it.status == '=' ? "Igual" : "Error");
        r.selected = cmp_sel_[i] != 0;
        rows.push_back(std::move(r));
    }
    cmp_model_->set_vector(std::move(rows));
    B().set_compare_rows(cmp_model_);
    if (!cmp_items_.empty() || !B().get_compare_busy()) {
        std::string s = std::to_string(nl) + " solo izq. · " + std::to_string(nr) + " solo der. · " + std::to_string(nd) +
                " diferentes · " + std::to_string(ne) + " iguales";
        if (ner)
            s += " · " + std::to_string(ner) + " errores";
        B().set_compare_summary(S(s));
    }
}

void Controller::compare_copy(bool to_right)
{
    std::vector<std::string> list;
    for (size_t i = 0; i < cmp_items_.size(); ++i) {
        if (!cmp_sel_[i])
            continue;
        char s = cmp_items_[i].status;
        if (s == '*' || (to_right ? s == '-' : s == '+'))
            list.push_back(cmp_items_[i].path);
    }
    if (list.empty()) {
        toast("Selecciona archivos que existan solo en el lado de origen o sean diferentes.", true);
        return;
    }
    Step s;
    s.src = to_right ? cmp_left_ : cmp_right_;
    s.dst = to_right ? cmp_right_ : cmp_left_;
    s.mode = "copy";
    s.opt = base_options();
    s.temp_file = write_temp_lines(list);
    s.opt.files_from = s.temp_file;
    auto t = std::make_shared<Task>();
    t->id = new_id();
    t->mode = "copy";
    t->title = "Copiar " + std::to_string(list.size()) + " archivos (comparación)";
    t->route = s.src + "  →  " + s.dst;
    start_task(t, { s }, [this](bool, bool, const std::string &) {
        if (B().get_compare_open())
            run_compare();
        refresh_pane(0);
        refresh_pane(1);
    });
}

void Controller::compare_delete(bool left)
{
    std::vector<std::string> list;
    for (size_t i = 0; i < cmp_items_.size(); ++i) {
        if (!cmp_sel_[i])
            continue;
        char s = cmp_items_[i].status;
        if (s == '=' || s == '*' || (left ? s == '-' : s == '+'))
            list.push_back(cmp_items_[i].path);
    }
    if (list.empty()) {
        toast("No hay archivos seleccionados en ese lado.", true);
        return;
    }
    std::string root = left ? cmp_left_ : cmp_right_;
    confirm("Eliminar archivos",
            "Se eliminarán " + std::to_string(list.size()) + " archivos de " + root + ".\nEsta acción no se puede deshacer.",
            "Eliminar", true, [this, list, root] {
                B().set_compare_busy(true);
                spawn([this, list, root] {
                    OpResult r = rc_.delete_listed(root, list);
                    post([this, r, root, count = list.size()] {
                        record_op("delete", "Eliminar archivos (comparación)", root, "", r.ok,
                                  r.ok ? "" : r.message, r.ok ? static_cast<int64_t>(count) : 0, 0);
                        if (!r.ok)
                            toast(r.message, true);
                        else
                            toast("Archivos eliminados.");
                        run_compare();
                        refresh_pane(0);
                        refresh_pane(1);
                    });
                });
            });
}

void Controller::close_compare()
{
    ++cmp_gen_;
    if (cmp_handle_)
        cmp_handle_->kill();
    B().set_compare_open(false);
    B().set_compare_busy(false);
}

// ------------------------------------------------------------------ remotes

void Controller::refresh_remote_rows()
{
    std::vector<RemoteRow> rows;
    for (auto &r : remotes_) {
        RemoteRow row;
        row.name = S(r.name);
        row.kind = S(r.type);
        row.detail = S("");
        rows.push_back(std::move(row));
    }
    remote_rows_->set_vector(std::move(rows));
    if (B().get_remote_selected() >= static_cast<int>(remotes_.size()))
        B().set_remote_selected(-1);
}

void Controller::select_remote(int i)
{
    B().set_remote_selected(i);
    B().set_remote_about(S(""));
    if (i < 0 || i >= static_cast<int>(remotes_.size()))
        return;
    int gen = ++about_gen_;
    std::string spec = remotes_[i].name + ":";
    B().set_remote_about(S("Consultando espacio…"));
    spawn([this, gen, spec] {
        auto a = rc_.about(spec);
        post([this, gen, a] {
            if (gen != about_gen_)
                return;
            if (!a || a->used < 0) {
                B().set_remote_about(S("Espacio no disponible para este remoto"));
                return;
            }
            std::string s = "Usado " + format_size(a->used);
            if (a->total > 0)
                s += " de " + format_size(a->total);
            if (a->free >= 0)
                s += " · libre " + format_size(a->free);
            B().set_remote_about(S(s));
        });
    });
}

void Controller::remote_new()
{
    if (provider_labels_->row_count() == 0) {
        std::vector<slint::SharedString> labels;
        for (auto &p : providers())
            labels.push_back(S(p.label));
        provider_labels_->set_vector(labels);
    }
    edit_name_.clear();
    B().set_remote_edit_name(S(""));
    B().set_remote_oauth(false);
    B().set_remote_error(S(""));
    B().set_remote_busy(false);
    B().set_remote_name_input(S(""));
    last_suggestion_.clear();
    B().set_remote_type_index(0);
    apply_provider(0);
    B().set_dialog(S("remote"));
}

void Controller::apply_provider(int i)
{
    const auto &list = providers();
    if (i < 0 || i >= static_cast<int>(list.size()))
        return;
    const auto &p = list[i];
    B().set_remote_type_index(i);
    field_values_.assign(p.fields.size(), "");
    std::vector<FieldRow> rows;
    for (auto &f : p.fields) {
        FieldRow r;
        r.label = S(f.label);
        r.hint = S(f.hint);
        r.secret = f.secret;
        rows.push_back(std::move(r));
    }
    field_rows_->set_vector(std::move(rows));
    B().set_remote_help(S(p.help));
    B().set_remote_error(S(""));

    std::string current = T(B().get_remote_name_input());
    if (current.empty() || current == last_suggestion_) {
        std::string base = p.type.empty() ? "remoto" : p.type;
        std::string cand = base;
        int n = 2;
        auto taken = [&](const std::string &c) {
            for (auto &r : remotes_)
                if (r.name == c)
                    return true;
            return false;
        };
        while (taken(cand))
            cand = base + std::to_string(n++);
        last_suggestion_ = cand;
        B().set_remote_name_input(S(cand));
    }
}

void Controller::create_remote()
{
    if (!edit_name_.empty()) {
        save_remote_edit();
        return;
    }
    const auto &list = providers();
    int idx = B().get_remote_type_index();
    if (idx < 0 || idx >= static_cast<int>(list.size()))
        return;
    const auto &p = list[idx];
    std::string name = trim(T(B().get_remote_name_input()));
    if (name.empty() || name.find_first_of(":/\\") != std::string::npos) {
        B().set_remote_error(S("Indica un nombre válido (sin «:» ni barras)."));
        return;
    }
    for (auto &r : remotes_)
        if (r.name == name) {
            B().set_remote_error(S("Ya existe un remoto con ese nombre."));
            return;
        }

    std::string type = p.type;
    std::vector<std::pair<std::string, std::string>> params = p.fixed;
    for (size_t i = 0; i < p.fields.size(); ++i) {
        const std::string val = trim(field_values_[i]);
        const std::string &key = p.fields[i].key;
        if (key == "__type") {
            type = val;
        } else if (key == "__params") {
            std::istringstream is(val);
            for (std::string kv; is >> kv;) {
                auto eq = kv.find('=');
                if (eq != std::string::npos)
                    params.push_back({ kv.substr(0, eq), kv.substr(eq + 1) });
            }
        } else if (!val.empty()) {
            params.push_back({ key, val });
        }
    }
    if (type.empty()) {
        B().set_remote_error(S("Indica el tipo de almacenamiento."));
        return;
    }

    B().set_remote_error(S(""));
    B().set_remote_busy(true);
    remote_handle_ = std::make_shared<ProcessHandle>();
    auto handle = remote_handle_;
    spawn([this, name, type, params, handle] {
        OpResult r = rc_.create_remote(name, type, params, handle.get());
        if (!r.ok)
            rc_.delete_remote(name);
        post([this, name, r, handle] {
            if (handle != remote_handle_)
                return;
            B().set_remote_busy(false);
            if (!r.ok) {
                B().set_remote_error(S(handle->killed() ? "Cancelado." : r.message));
                return;
            }
            B().set_dialog(S(""));
            toast("Remoto «" + name + "» creado.");
            add_log("info", "Remoto creado: " + name);
            load_remotes_async();
        });
    });
}

void Controller::remote_edit()
{
    int i = B().get_remote_selected();
    if (i < 0 || i >= static_cast<int>(remotes_.size()))
        return;
    std::string name = remotes_[i].name;
    std::string type = remotes_[i].type;
    spawn([this, name, type] {
        auto opts = rc_.remote_options(name);
        post([this, name, type, opts] {
            if (opts.empty()) {
                toast("No se pudo leer la configuración de «" + name + "».", true);
                return;
            }
            std::vector<ProviderField> fields;
            for (auto &p : providers())
                if (!p.type.empty() && p.type == type)
                    for (auto &f : p.fields)
                        fields.push_back(f);
            auto known = [&](const std::string &k) {
                for (auto &f : fields)
                    if (f.key == k)
                        return true;
                return false;
            };
            auto secret_key = [](const std::string &k) {
                std::string l = lower(k);
                auto ends = [&](const std::string &s) { return l.size() >= s.size() && l.compare(l.size() - s.size(), s.size(), s) == 0; };
                return l.find("pass") != std::string::npos || l.find("secret") != std::string::npos || l == "key" || ends("_key");
            };
            if (type == "drive") {
                fields.push_back({ "Client ID de Google", "client_id", "xxxx.apps.googleusercontent.com", false });
                fields.push_back({ "Client secret", "client_secret", "", true });
            }
            for (auto &[k, v] : opts)
                if (k != "type" && k != "token" && !known(k))
                    fields.push_back({ k, k, "", secret_key(k) });
            fields.push_back({ "Parámetros nuevos (opcional)", "__params", "clave=valor clave2=valor2", false });

            edit_name_ = name;
            edit_type_ = type;
            edit_keys_.clear();
            edit_initial_.clear();
            edit_secret_.clear();
            field_values_.clear();
            std::vector<FieldRow> rows;
            for (auto &f : fields) {
                auto it = opts.find(f.key);
                bool has = it != opts.end() && !it->second.empty();
                std::string initial = (!f.secret && it != opts.end()) ? it->second : "";
                edit_keys_.push_back(f.key);
                edit_initial_.push_back(initial);
                edit_secret_.push_back(f.secret);
                field_values_.push_back(initial);
                FieldRow r;
                r.label = S(f.label);
                r.hint = S(f.secret && has ? "Dejar vacío para no cambiarlo" : f.hint);
                r.secret = f.secret;
                r.value = S(initial);
                rows.push_back(std::move(r));
            }
            field_rows_->set_vector(std::move(rows));
            bool oauth = type == "drive" || type == "onedrive" || type == "dropbox" || type == "box" || type == "pcloud";
            std::string help = "Edita «" + name + "» (tipo " + type + "). Los campos de contraseña aparecen vacíos y solo cambian si escribes algo. Para quitar un valor, vacía su campo.";
            if (type == "drive")
                help += " Client ID y Client secret son tus propias credenciales de Google y se cambian a la vez. Sin ellas, Google puede bloquear el acceso con la clave compartida de rclone.";
            if (oauth)
                help += " Con «Reautenticar en el navegador» inicias sesión de nuevo sin cambiar nada más.";
            B().set_remote_oauth(oauth);
            B().set_remote_edit_name(S(name));
            B().set_remote_name_input(S(name));
            B().set_remote_help(S(help));
            B().set_remote_error(S(""));
            B().set_remote_busy(false);
            B().set_dialog(S("remote"));
        });
    });
}

void Controller::save_remote_edit()
{
    std::string name = edit_name_;
    std::vector<std::pair<std::string, std::string>> params;
    bool id_changed = false, secret_changed = false;
    std::string new_id;
    for (size_t i = 0; i < edit_keys_.size() && i < field_values_.size(); ++i) {
        const std::string &key = edit_keys_[i];
        const std::string val = trim(field_values_[i]);
        if (key == "__params") {
            std::istringstream is(val);
            for (std::string kv; is >> kv;) {
                auto eq = kv.find('=');
                if (eq != std::string::npos && eq > 0)
                    params.push_back({ kv.substr(0, eq), kv.substr(eq + 1) });
            }
            continue;
        }
        if (edit_secret_[i] ? val.empty() : val == edit_initial_[i])
            continue;
        params.push_back({ key, val });
        if (key == "client_id") {
            id_changed = true;
            new_id = val;
        } else if (key == "client_secret") {
            secret_changed = true;
        }
    }
    bool reauth = false;
    if (edit_type_ == "drive") {
        if (id_changed && new_id.empty() && !secret_changed) {
            params.push_back({ "client_secret", "" });
            secret_changed = true;
        }
        if (id_changed != secret_changed) {
            B().set_remote_error(S("Cambia el client ID y el client secret a la vez."));
            return;
        }
        reauth = id_changed;
    }
    if (params.empty()) {
        B().set_dialog(S(""));
        toast("Sin cambios.");
        return;
    }
    if (reauth && !tasks_.empty()) {
        B().set_remote_error(S("Hay transferencias en curso. Cambiar las credenciales obliga a reconectar y las cortaría."));
        return;
    }

    B().set_remote_error(S(""));
    B().set_remote_busy(true);
    remote_handle_ = std::make_shared<ProcessHandle>();
    auto handle = remote_handle_;
    spawn([this, name, params, reauth, handle] {
        OpResult r = rc_.update_remote(name, params);
        bool saved = r.ok;
        if (r.ok && reauth)
            r = rc_.reconnect_remote(name, handle.get());
        post([this, name, r, saved, handle] {
            if (handle != remote_handle_)
                return;
            B().set_remote_busy(false);
            if (saved)
                load_remotes_async();
            if (!r.ok) {
                std::string msg = handle->killed() ? "Cancelado." : r.message;
                if (saved)
                    msg += " Los cambios se guardaron, pero hay que reconectar «" + name + "».";
                B().set_remote_error(S(msg));
                return;
            }
            B().set_dialog(S(""));
            toast("Remoto «" + name + "» actualizado.");
            add_log("info", "Remoto editado: " + name);
        });
    });
}
void Controller::cancel_create_remote()
{
    if (B().get_remote_busy() && remote_handle_) {
        remote_handle_->kill();
        return;
    }
    B().set_dialog(S(""));
}

void Controller::delete_remote()
{
    int i = B().get_remote_selected();
    if (i < 0 || i >= static_cast<int>(remotes_.size()))
        return;
    std::string name = remotes_[i].name;
    confirm("Eliminar remoto",
            "Se eliminará la configuración de «" + name + "».\nLos archivos en la nube no se tocan.", "Eliminar", true,
            [this, name] {
                spawn([this, name] {
                    OpResult r = rc_.delete_remote(name);
                    post([this, r, name] {
                        if (!r.ok)
                            toast(r.message, true);
                        else
                            toast("Remoto «" + name + "» eliminado.");
                        B().set_remote_selected(-1);
                        B().set_remote_about(S(""));
                        load_remotes_async();
                    });
                });
            });
}

void Controller::reconnect_remote()
{
    int i = B().get_remote_selected();
    if (i < 0 || i >= static_cast<int>(remotes_.size()))
        return;
    std::string name = remotes_[i].name;
    toast("Se abrirá el navegador para renovar el acceso a «" + name + "»…");
    spawn([this, name] {
        OpResult r = rc_.reconnect_remote(name);
        post([this, r, name] {
            if (r.ok)
                toast("«" + name + "» reconectado.");
            else
                toast(r.message, true);
        });
    });
}

void Controller::remote_reauth()
{
    if (edit_name_.empty() || B().get_remote_busy())
        return;
    std::string name = edit_name_;
    B().set_remote_error(S(""));
    B().set_remote_busy(true);
    remote_handle_ = std::make_shared<ProcessHandle>();
    auto handle = remote_handle_;
    spawn([this, name, handle] {
        OpResult r = rc_.reconnect_remote(name, handle.get());
        post([this, name, r, handle] {
            if (handle != remote_handle_)
                return;
            B().set_remote_busy(false);
            if (!r.ok) {
                B().set_remote_error(S(handle->killed() ? "Cancelado." : r.message));
                return;
            }
            B().set_dialog(S(""));
            toast("«" + name + "» reautenticado.");
            add_log("info", "Remoto reautenticado: " + name);
        });
    });
}

void Controller::note_throttle(const std::shared_ptr<Task> &t, const std::string &src, const std::string &dst)
{
    if (t->throttled)
        return;
    t->throttled = true;
    bool shared_key = false;
    for (auto &r : remotes_) {
        if (r.type != "drive" || r.own_client)
            continue;
        std::string pre = r.name + ":";
        if (src.rfind(pre, 0) == 0 || dst.rfind(pre, 0) == 0)
            shared_key = true;
    }
    std::string msg = "Google está limitando las peticiones (cuota agotada) y la transferencia irá muy lenta.";
    if (shared_key)
        msg += " Usas la clave compartida de rclone, que Google está retirando: crea tu propio client ID en Ajustes > Google Drive.";
    add_log("warning", msg);
    toast(shared_key ? "Google limita las peticiones. Crea tu propio client ID en Ajustes." : "Google limita las peticiones (cuota agotada).", true);
}
void Controller::refresh_drive_ui()
{
    std::string keep;
    int cur = B().get_drive_remote_index();
    if (cur >= 0 && cur < static_cast<int>(drive_names_.size()))
        keep = drive_names_[cur];
    drive_names_.clear();
    std::vector<slint::SharedString> labels;
    for (auto &r : remotes_)
        if (r.type == "drive") {
            drive_names_.push_back(r.name);
            labels.push_back(S(r.name + ":"));
        }
    drive_model_->set_vector(labels);
    int idx = drive_names_.empty() ? -1 : 0;
    for (size_t i = 0; i < drive_names_.size(); ++i)
        if (drive_names_[i] == keep)
            idx = static_cast<int>(i);
    select_drive_remote(idx);
}

void Controller::select_drive_remote(int i)
{
    B().set_drive_remote_index(i);
    B().set_drive_client_id(S(""));
    B().set_drive_client_secret(S(""));
    if (i < 0 || i >= static_cast<int>(drive_names_.size())) {
        B().set_drive_status(S(""));
        B().set_drive_shared(false);
        return;
    }
    bool own = false;
    for (auto &r : remotes_)
        if (r.name == drive_names_[i])
            own = r.own_client;
    B().set_drive_shared(!own);
    B().set_drive_status(S(own ? "Usa credenciales propias." : "Usa la clave compartida de rclone: Google la está retirando y puede bloquear el acceso o ir muy lento. Pon tus propias credenciales."));
}

void Controller::save_drive_creds()
{
    if (B().get_drive_busy()) {
        if (drive_handle_)
            drive_handle_->kill();
        return;
    }
    int i = B().get_drive_remote_index();
    if (i < 0 || i >= static_cast<int>(drive_names_.size()))
        return;
    std::string id = trim(T(B().get_drive_client_id()));
    std::string secret = trim(T(B().get_drive_client_secret()));
    if (id.empty() || secret.empty()) {
        toast("Indica el client ID y el client secret.", true);
        return;
    }
    if (id.find_first_of(" \t\r\n") != std::string::npos || secret.find_first_of(" \t\r\n") != std::string::npos) {
        toast("El client ID y el secret no pueden contener espacios.", true);
        return;
    }
    if (!tasks_.empty()) {
        toast("Hay transferencias en curso. Espera a que terminen para cambiar las credenciales.", true);
        return;
    }
    std::string name = drive_names_[i];
    B().set_drive_busy(true);
    drive_handle_ = std::make_shared<ProcessHandle>();
    auto handle = drive_handle_;
    spawn([this, name, id, secret, handle] {
        OpResult r = rc_.update_remote(name, { { "client_id", id }, { "client_secret", secret } });
        if (r.ok)
            r = rc_.reconnect_remote(name, handle.get());
        post([this, name, r, handle] {
            if (handle != drive_handle_)
                return;
            B().set_drive_busy(false);
            if (!r.ok) {
                toast(handle->killed() ? "Cancelado. Las credenciales se guardaron, pero hay que reconectar «" + name + "»." : r.message, true);
            } else {
                toast("«" + name + "» ahora usa tus credenciales.");
                add_log("info", "Credenciales propias configuradas en " + name);
            }
            load_remotes_async();
        });
    });
}
// ------------------------------------------------------------------ jobs

std::string Controller::schedule_text(const Schedule &s) const
{
    if (!s.enabled)
        return "Manual";
    if (s.kind == "daily")
        return "Diario a las " + s.daily_time;
    return "Cada " + std::to_string(s.interval_minutes) + " min";
}

void Controller::refresh_jobs()
{
    std::vector<JobRow> rows;
    for (auto &j : store_.jobs) {
        JobRow r;
        r.id = S(j.id);
        r.name = S(j.name);
        r.source = S(j.source);
        std::string d;
        for (auto &x : j.destinations)
            d += (d.empty() ? "" : ", ") + x;
        r.dest = S(d);
        r.mode = S(j.options.mode == "sync" ? "Sincronizar" : j.options.mode == "move" ? "Mover" : "Copiar");
        r.schedule = S(schedule_text(j.schedule));
        r.last_run = S(format_time(j.last_run));
        r.status = S(j.last_status);
        r.running = running_jobs_.count(j.id) > 0;
        rows.push_back(std::move(r));
    }
    job_rows_->set_vector(std::move(rows));
    if (B().get_job_selected() >= static_cast<int>(store_.jobs.size()))
        B().set_job_selected(-1);
}

void Controller::open_job_form(const Job *job, bool from_panes)
{
    JobForm f;
    TransferOptions o = base_options();
    Schedule sch;
    std::string name, source, dests;
    int mode_index = 0;
    if (job) {
        editing_job_id_ = job->id;
        o = job->options;
        sch = job->schedule;
        name = job->name;
        source = job->source;
        for (auto &d : job->destinations)
            dests += (dests.empty() ? "" : "\n") + d;
    } else {
        editing_job_id_.clear();
        if (from_panes) {
            if (!pane_[0].drives_view())
                source = pane_[0].spec();
            if (!pane_[1].drives_view())
                dests = pane_[1].spec();
            mode_index = 1;
            name = "Sincronizar " + source;
        }
    }
    if (job)
        mode_index = o.mode == "sync" ? 1 : o.mode == "move" ? 2 : 0;
    f.name = S(name);
    f.source = S(source);
    f.dests = S(dests);
    f.mode_index = mode_index;
    f.transfers = S(std::to_string(o.transfers));
    f.checkers = S(std::to_string(o.checkers));
    f.dry_run = o.dry_run;
    f.checksum = o.checksum;
    f.size_only = o.size_only;
    f.ignore_existing = o.ignore_existing;
    f.update_existing = o.update_existing;
    f.bwlimit = S(job ? o.bwlimit : "");
    f.filters = S(o.filters);
    f.extra = S(job ? o.extra_flags : "");
    f.schedule_enabled = sch.enabled;
    f.schedule_kind = sch.kind == "daily" ? 1 : 0;
    f.interval = S(std::to_string(sch.interval_minutes));
    f.daily_time = S(sch.daily_time);
    B().set_job_form(f);
    B().set_job_editing(true);
}

bool Controller::save_job_from_form(std::string &id_out)
{
    JobForm f = B().get_job_form();
    Job j;
    j.name = trim(T(f.name));
    j.source = trim(T(f.source));
    j.destinations = split_lines(T(f.dests));
    if (j.name.empty()) {
        toast("Ponle un nombre al trabajo.", true);
        return false;
    }
    if (j.source.empty() || j.destinations.empty()) {
        toast("Indica el origen y al menos un destino.", true);
        return false;
    }
    for (auto &d : j.destinations)
        if (d == j.source) {
            toast("El origen y un destino son iguales.", true);
            return false;
        }
    j.options.mode = f.mode_index == 1 ? "sync" : f.mode_index == 2 ? "move" : "copy";
    j.options.transfers = parse_int(T(f.transfers), 4, 1, 64);
    j.options.checkers = parse_int(T(f.checkers), 8, 1, 128);
    j.options.dry_run = f.dry_run;
    j.options.checksum = f.checksum;
    j.options.size_only = f.size_only;
    j.options.ignore_existing = f.ignore_existing;
    j.options.update_existing = f.update_existing;
    j.options.bwlimit = trim(T(f.bwlimit));
    j.options.filters = trim(T(f.filters));
    j.options.extra_flags = trim(T(f.extra));
    j.schedule.enabled = f.schedule_enabled;
    j.schedule.kind = f.schedule_kind == 1 ? "daily" : "interval";
    j.schedule.interval_minutes = parse_int(T(f.interval), 60, 1, 100000);
    std::string tm = trim(T(f.daily_time));
    int hh = 0, mm = 0;
    if (std::sscanf(tm.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        if (j.schedule.enabled && j.schedule.kind == "daily") {
            toast("La hora debe tener el formato HH:MM (por ejemplo 02:30).", true);
            return false;
        }
        hh = 2;
        mm = 0;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    j.schedule.daily_time = buf;

    if (editing_job_id_.empty()) {
        j.id = new_id();
        j.created = now_unix();
        if (j.schedule.enabled)
            j.schedule.last_run = now_unix();
        store_.jobs.push_back(j);
        id_out = j.id;
    } else {
        Job *old = store_.find_job(editing_job_id_);
        if (!old)
            return false;
        j.id = old->id;
        j.created = old->created;
        j.last_run = old->last_run;
        j.last_status = old->last_status;
        j.schedule.last_run = old->schedule.last_run;
        if (j.schedule.enabled && (!old->schedule.enabled || old->schedule.last_run == 0))
            j.schedule.last_run = now_unix();
        *old = j;
        id_out = j.id;
    }
    if (persist())
        sync_background_scheduler();
    refresh_jobs();
    B().set_job_editing(false);
    return true;
}

void Controller::run_job(const std::string &id, bool dry, const std::string &trigger)
{
    Job *j = store_.find_job(id);
    if (!j || running_jobs_.count(id))
        return;
    if (!rc_.available()) {
        toast("rclone no está disponible.", true);
        return;
    }
    auto t = std::make_shared<Task>();
    t->id = new_id();
    t->job_id = id;
    t->job_name = j->name;
    t->trigger = trigger;
    t->mode = j->options.mode;
    t->title = (dry ? "Simulación: " : "") + j->name;
    std::string dests;
    std::vector<Step> steps;
    for (auto &d : j->destinations) {
        Step s;
        s.src = j->source;
        s.dst = d;
        s.mode = j->options.mode;
        s.opt = with_defaults(j->options);
        s.opt.dry_run = dry || j->options.dry_run;
        steps.push_back(std::move(s));
        dests += (dests.empty() ? "" : ", ") + d;
    }
    t->route = j->source + "  →  " + dests;
    j->last_run = now_unix();
    j->schedule.last_run = now_unix();
    running_jobs_.insert(id);
    refresh_jobs();
    start_task(t, std::move(steps), [this, id](bool ok, bool cancelled, const std::string &) {
        running_jobs_.erase(id);
        if (Job *j = store_.find_job(id)) {
            j->last_status = ok ? "ok" : cancelled ? "" : "error";
            persist();
        }
        refresh_jobs();
        refresh_pane(0);
        refresh_pane(1);
    });
    if (std::find(tasks_.begin(), tasks_.end(), t) == tasks_.end()) {
        running_jobs_.erase(id);
        refresh_jobs();
    }
}

void Controller::check_schedules()
{
    if (!rc_.available() || background_scheduler_active_)
        return;
    int64_t now = now_unix();
    std::vector<std::string> due;
    for (auto &j : store_.jobs) {
        if (!running_jobs_.count(j.id) && schedule_is_due(j.schedule, now))
            due.push_back(j.id);
    }
    for (auto &id : due) {
        if (Job *j = store_.find_job(id))
            add_log("info", "Ejecución programada: " + j->name);
        run_job(id, false, "scheduled");
    }
}

void Controller::sync_background_scheduler()
{
    const bool has_enabled_schedule = std::any_of(store_.jobs.begin(), store_.jobs.end(),
                                                   [](const Job &job) { return job.schedule.enabled; });
    const bool enable = store_.settings.background_schedules && has_enabled_schedule;
    OpResult result = configure_background_task(current_executable_path(), enable);
    background_scheduler_active_ = enable && result.ok;
    if (!result.ok) {
        add_log("error", "No se pudo actualizar la tarea programada de Windows: " + result.message);
        toast("No se pudo actualizar la tarea programada de Windows. Los trabajos seguirán ejecutándose mientras " +
                  std::string("Rclone Lite esté abierto. ") + result.message,
              true);
    }
}

void Controller::export_jobs()
{
    std::string path = plat::save_file_dialog("Exportar trabajos", "rclone_lite_trabajos.json",
                                              L"Archivos JSON (*.json)\0*.json\0\0", L"json");
    if (path.empty())
        return;
    if (plat::write_text_file(path, store_.export_jobs_json()))
        toast("Trabajos exportados.");
    else
        toast("No se pudo escribir el archivo.", true);
}

void Controller::import_jobs()
{
    std::string path = plat::open_file_dialog("Importar trabajos", L"Archivos JSON (*.json)\0*.json\0\0");
    if (path.empty())
        return;
    std::string text;
    if (!plat::read_text_file(path, text)) {
        toast("No se pudo leer el archivo.", true);
        return;
    }
    std::vector<Job> imported;
    ImportReport report;
    if (!store_.parse_jobs_import(text, imported, report)) {
        toast("El archivo no tiene un formato de trabajos válido.", true);
        return;
    }
    if (imported.empty()) {
        toast("No hay trabajos válidos que importar. Revisa las rutas de origen y destino.", true);
        return;
    }

    std::string text_summary = "Se importarán " + std::to_string(imported.size()) + " trabajos.";
    if (report.schedules_disabled)
        text_summary += "\n" + std::to_string(report.schedules_disabled) +
                        " programaciones quedarán desactivadas; podrás activarlas después de revisarlas.";
    if (report.sync_jobs)
        text_summary += "\n" + std::to_string(report.sync_jobs) +
                        " trabajos de sincronización pueden eliminar archivos del destino.";
    if (!report.removed_flags.empty()) {
        std::sort(report.removed_flags.begin(), report.removed_flags.end());
        report.removed_flags.erase(std::unique(report.removed_flags.begin(), report.removed_flags.end()),
                                   report.removed_flags.end());
        text_summary += "\nSe bloquearán flags potencialmente peligrosos: ";
        for (size_t i = 0; i < report.removed_flags.size() && i < 5; ++i)
            text_summary += (i ? ", " : "") + report.removed_flags[i];
        if (report.removed_flags.size() > 5)
            text_summary += " y otros";
        text_summary += ".";
    }
    if (report.skipped)
        text_summary += "\nSe omitirán " + std::to_string(report.skipped) + " elementos incompletos o inválidos.";

    confirm("Revisar importación", text_summary + "\n\n¿Quieres importar estos trabajos?", "Importar", true,
            [this, imported = std::move(imported)]() mutable {
                const size_t old_size = store_.jobs.size();
                store_.jobs.insert(store_.jobs.end(),
                                   std::make_move_iterator(imported.begin()),
                                   std::make_move_iterator(imported.end()));
                if (!persist()) {
                    store_.jobs.resize(old_size);
                    return;
                }
                sync_background_scheduler();
                refresh_jobs();
                toast("Se importaron " + std::to_string(imported.size()) + " trabajos.");
            });
}

// ------------------------------------------------------------------ mounts

void Controller::refresh_mounts()
{
    std::vector<MountRow> rows;
    for (auto &m : store_.mounts) {
        MountRow r;
        r.id = S(m.id);
        r.name = S(m.name);
        r.spec = S(m.spec);
        r.mountpoint = S(m.mountpoint);
        auto it = mount_rt_.find(m.id);
        r.state = S(it == mount_rt_.end() ? "unmounted" : it->second.state);
        r.detail = S(it == mount_rt_.end() ? "" : it->second.detail);
        rows.push_back(std::move(r));
    }
    mount_rows_->set_vector(std::move(rows));
    if (B().get_mount_selected() >= static_cast<int>(store_.mounts.size()))
        B().set_mount_selected(-1);
}

void Controller::open_mount_form(const std::string &spec, const std::string &name)
{
    MountForm f;
    f.name = S(name);
    f.spec = S(spec);
    auto letters = plat::free_drive_letters();
    std::string mp = letters.empty() ? "" : std::string(1, letters.front()) + ":";
    f.mountpoint = S(mp);
    f.cache_index = 3;
    f.read_only = false;
    f.auto_mount = false;
    B().set_mount_form(f);
    B().set_dialog(S("mount"));
}

void Controller::save_mount()
{
    MountForm f = B().get_mount_form();
    MountDef m;
    m.id = new_id();
    m.name = trim(T(f.name));
    m.spec = trim(T(f.spec));
    m.mountpoint = trim(T(f.mountpoint));
    std::replace(m.mountpoint.begin(), m.mountpoint.end(), '\\', '/');
    static const char *modes[] = { "off", "minimal", "writes", "full" };
    m.cache_mode = modes[std::clamp(f.cache_index, 0, 3)];
    m.read_only = f.read_only;
    m.auto_mount = f.auto_mount;
    if (m.name.empty())
        m.name = m.spec;
    if (m.spec.find(':') == std::string::npos) {
        toast("Indica el remoto con el formato remoto:carpeta.", true);
        return;
    }
    if (m.mountpoint.empty()) {
        toast("Indica una letra de unidad o una carpeta.", true);
        return;
    }
    store_.mounts.push_back(m);
    persist();
    refresh_mounts();
    B().set_dialog(S(""));
    B().set_mount_selected(static_cast<int>(store_.mounts.size()) - 1);
    toast("Montaje guardado.");
}

void Controller::start_mount(const std::string &id)
{
    MountDef *m = store_.find_mount(id);
    if (!m)
        return;
    auto &rt = mount_rt_[id];
    if (rt.state == "starting" || rt.state == "mounted")
        return;
    if (!rc_.available()) {
        toast("rclone no está disponible.", true);
        return;
    }
    if (!plat::winfsp_installed()) {
        B().set_winfsp_ok(false);
        toast("Para montar unidades necesitas instalar WinFsp.", true);
        return;
    }
    if (plat::path_exists(m->mountpoint)) {
        rt.state = "error";
        rt.detail = "El punto de montaje ya existe o está en uso.";
        refresh_mounts();
        toast(is_drive_letter(m->mountpoint) ? "La unidad " + m->mountpoint + " ya está en uso. Elige otra letra."
                                             : "La carpeta de montaje ya existe. WinFsp necesita que no exista.",
              true);
        return;
    }
    rt.state = "starting";
    rt.detail.clear();
    rt.handle = std::make_shared<ProcessHandle>();
    refresh_mounts();

    auto handle = rt.handle;
    MountDef def = *m;
    spawn([this, id, def, handle] {
        std::string last_err;
        auto on_log = [&](const std::string &line) {
            if (line.find("ERROR") != std::string::npos || line.find("CRITICAL") != std::string::npos)
                last_err = line;
        };
        ProcessResult r = rc_.mount(def.spec, def.mountpoint, def.cache_mode, def.read_only, def.name, on_log, handle.get());
        post([this, id, handle, r, last_err] {
            auto &rt = mount_rt_[id];
            if (rt.handle != handle)
                return;
            bool user = handle->killed();
            rt.state = (user || r.exit_code == 0) ? "unmounted" : "error";
            rt.detail = user ? "" : last_err;
            rt.handle.reset();
            refresh_mounts();
            if (rt.state == "error") {
                std::string msg = last_err.empty() ? "El montaje terminó con un error." : last_err;
                add_log("error", "Montaje: " + msg);
                toast("Montaje fallido: " + msg, true);
            }
        });
    });
}

void Controller::stop_mount(const std::string &id)
{
    auto it = mount_rt_.find(id);
    if (it == mount_rt_.end() || !it->second.handle)
        return;
    it->second.handle->kill();
}

void Controller::toggle_mount()
{
    int i = B().get_mount_selected();
    if (i < 0 || i >= static_cast<int>(store_.mounts.size()))
        return;
    const std::string id = store_.mounts[i].id;
    auto &rt = mount_rt_[id];
    if (rt.state == "starting" || rt.state == "mounted")
        stop_mount(id);
    else
        start_mount(id);
}

void Controller::poll_mounts()
{
    bool changed = false;
    for (auto &m : store_.mounts) {
        auto it = mount_rt_.find(m.id);
        if (it != mount_rt_.end() && it->second.state == "starting" && plat::path_exists(m.mountpoint)) {
            it->second.state = "mounted";
            changed = true;
        }
    }
    if (changed)
        refresh_mounts();
}

// ------------------------------------------------------------------ history / settings

void Controller::refresh_history()
{
    std::vector<HistoryRow> rows;
    for (auto &h : store_.history) {
        HistoryRow r;
        r.time = S(format_time(h.time));
        r.job = S(h.job);
        r.route = S(h.source + (h.destination.empty() ? "" : "  →  " + h.destination));
        r.status = S(h.status);
        r.size = S(format_size(h.bytes));
        r.files = S(std::to_string(h.files));
        r.duration = S(format_duration(h.seconds));
        r.trigger = S(h.trigger);
        r.error = S(h.error);
        rows.push_back(std::move(r));
    }
    history_rows_->set_vector(std::move(rows));
}

void Controller::load_settings_into_ui()
{
    auto &s = store_.settings;
    B().set_set_rclone_path(S(s.rclone_path));
    B().set_set_transfers(S(std::to_string(s.transfers)));
    B().set_set_checkers(S(std::to_string(s.checkers)));
    B().set_set_bwlimit(S(s.bwlimit));
    B().set_set_extra(S(s.extra_flags));
    B().set_set_confirm_delete(s.confirm_delete);
    B().set_set_notify_done(s.notify_done);
    B().set_set_minimize_tray(s.minimize_to_tray);
    B().set_set_auto_update(s.auto_update);
    B().set_set_auto_tune(s.auto_tune);
    B().set_set_update_existing(s.update_existing);
    B().set_set_background_schedules(s.background_schedules);
    rc_.set_auto_tune(s.auto_tune);
    ui_->global<Theme>().set_dark(s.dark_theme);
}

void Controller::apply_settings()
{
    auto &s = store_.settings;
    s.rclone_path = trim(T(B().get_set_rclone_path()));
    s.transfers = parse_int(T(B().get_set_transfers()), 4, 1, 64);
    s.checkers = parse_int(T(B().get_set_checkers()), 8, 1, 128);
    s.bwlimit = trim(T(B().get_set_bwlimit()));
    s.extra_flags = trim(T(B().get_set_extra()));
    s.confirm_delete = B().get_set_confirm_delete();
    s.notify_done = B().get_set_notify_done();
    s.minimize_to_tray = B().get_set_minimize_tray();
    s.auto_update = B().get_set_auto_update();
    s.auto_tune = B().get_set_auto_tune();
    s.update_existing = B().get_set_update_existing();
    s.background_schedules = B().get_set_background_schedules();
    rc_.set_auto_tune(s.auto_tune);
    if (persist())
        sync_background_scheduler();
    load_settings_into_ui();
    bool had = rc_.available();
    refresh_rclone_state();
    if (!rc_.available() && !s.rclone_path.empty())
        toast("No se encontró rclone en esa ruta.", true);
    else
        toast("Ajustes guardados.");
    if (!had && rc_.available()) {
        load_remotes_async();
        navigate(0, "", plat::user_profile(), false);
    }
}

// ------------------------------------------------------------------ wiring

void Controller::bind_callbacks()
{
    const Backend &b = B();

    b.on_theme_changed([this](bool dark) {
        ui_->global<Theme>().set_dark(dark);
        store_.settings.dark_theme = dark;
        persist();
    });

    b.on_dismiss_toast([this] { B().set_toast_visible(false); });

    b.on_dialog_accept([this] {
        std::string kind = T(B().get_dialog());
        if (kind == "input") {
            std::string v = trim(T(B().get_dialog_input()));
            if (v.empty())
                return;
            auto act = std::move(input_action_);
            B().set_dialog(S(""));
            if (act)
                act(v);
        } else {
            auto act = std::move(confirm_action_);
            B().set_dialog(S(""));
            if (act)
                act();
        }
    });
    b.on_dialog_cancel([this] {
        confirm_action_ = nullptr;
        input_action_ = nullptr;
        B().set_dialog(S(""));
    });

    // explorer
    b.on_pane_remote([this](int side, int i) { pane_remote(side, i); });
    b.on_pane_path([this](int side, slint::SharedString text) {
        auto &p = pane_[side];
        navigate(side, p.remote, normalize_path(p.remote.empty(), T(text)));
    });
    b.on_pane_up([this](int side) {
        auto &p = pane_[side];
        if (p.drives_view())
            return;
        navigate(side, p.remote, up_path(p.remote.empty(), p.path));
    });
    b.on_pane_back([this](int side) {
        auto &p = pane_[side];
        if (p.back.empty())
            return;
        auto prev = p.back.back();
        p.back.pop_back();
        navigate(side, prev.first, prev.second, false);
    });
    b.on_pane_refresh([this](int side) { refresh_pane(side); });
    b.on_pane_click([this](int side, int row, bool ctrl, bool shift) { pane_click(side, row, ctrl, shift); });
    b.on_pane_open([this](int side, int row) { pane_open(side, row); });
    b.on_pane_select_all([this](int side) {
        for (auto &e : pane_[side].entries)
            pane_[side].selected.insert(e.name);
        apply_selection(side);
    });
    b.on_pane_sort([this](int side, slint::SharedString key) { pane_sort(side, T(key)); });
    b.on_pane_browse([this](int side) {
        auto &p = pane_[side];
        if (!p.remote.empty()) {
            toast("Esta opción solo está disponible para el disco local.");
            return;
        }
        std::string dir = plat::pick_folder("Elegir carpeta");
        if (!dir.empty())
            navigate(side, "", normalize_path(true, dir));
    });
    b.on_copy_selected([this](bool to_right) { transfer_selected(to_right, false); });
    b.on_move_selected([this](bool to_right) { transfer_selected(to_right, true); });
    b.on_new_folder([this] { new_folder(); });
    b.on_rename_selected([this] { rename_selected(); });
    b.on_delete_selected([this] { delete_selected(); });
    b.on_copy_link([this] { copy_link(); });
    b.on_open_sync([this] { open_job_form(nullptr, true); });
    b.on_start_compare([this] { start_compare(); });

    // compare
    b.on_compare_filter_changed([this] { rebuild_compare_rows(); });
    b.on_compare_toggle([this](int i) {
        if (i < 0 || i >= static_cast<int>(cmp_visible_.size()))
            return;
        int k = cmp_visible_[i];
        cmp_sel_[k] = !cmp_sel_[k];
        auto row = cmp_model_->row_data(i);
        if (row) {
            CompareRow r = *row;
            r.selected = cmp_sel_[k] != 0;
            cmp_model_->set_row_data(i, r);
        }
    });
    b.on_compare_select_all([this](bool sel) {
        for (int k : cmp_visible_)
            cmp_sel_[k] = sel;
        rebuild_compare_rows();
    });
    b.on_compare_copy([this](bool to_right) { compare_copy(to_right); });
    b.on_compare_delete([this](bool left) { compare_delete(left); });
    b.on_compare_close([this] { close_compare(); });
    b.on_compare_cancel([this] { close_compare(); });

    // remotes
    b.on_remote_select([this](int i) { select_remote(i); });
    b.on_remote_new([this] { remote_new(); });
    b.on_remote_delete([this] { delete_remote(); });
    b.on_remote_reconnect([this] { reconnect_remote(); });
    b.on_remote_browse([this](int i) {
        if (i < 0 || i >= static_cast<int>(remotes_.size()))
            return;
        navigate(B().get_active_side(), remotes_[i].name, "");
        B().set_page(0);
    });
    b.on_remote_mount([this] {
        int i = B().get_remote_selected();
        if (i < 0 || i >= static_cast<int>(remotes_.size()))
            return;
        B().set_page(3);
        B().set_winfsp_ok(plat::winfsp_installed());
        open_mount_form(remotes_[i].name + ":", remotes_[i].name);
    });
    b.on_remotes_refresh([this] { load_remotes_async(); });
    b.on_remote_type_changed([this](int i) { apply_provider(i); });
    b.on_remote_field_edited([this](int i, slint::SharedString t) {
        if (i >= 0 && i < static_cast<int>(field_values_.size()))
            field_values_[i] = T(t);
    });
    b.on_remote_create([this] { create_remote(); });
    b.on_remote_edit([this] { remote_edit(); });
    b.on_remote_reauth([this] { remote_reauth(); });
    b.on_remote_cancel_create([this] { cancel_create_remote(); });
    b.on_drive_remote_changed([this](int i) { select_drive_remote(i); });
    b.on_drive_creds_save([this] { save_drive_creds(); });
    b.on_drive_guide([] { plat::open_url("https://rclone.org/drive/#making-your-own-client-id"); });

    // jobs
    b.on_job_select([this](int i) { B().set_job_selected(i); });
    b.on_job_new([this] { open_job_form(nullptr, false); });
    b.on_job_edit([this] {
        int i = B().get_job_selected();
        if (i >= 0 && i < static_cast<int>(store_.jobs.size()))
            open_job_form(&store_.jobs[i], false);
    });
    b.on_job_duplicate([this] {
        int i = B().get_job_selected();
        if (i < 0 || i >= static_cast<int>(store_.jobs.size()))
            return;
        Job j = store_.jobs[i];
        j.id = new_id();
        j.name += " (copia)";
        j.created = now_unix();
        j.last_run = 0;
        j.last_status.clear();
        j.schedule.last_run = now_unix();
        store_.jobs.push_back(j);
        if (persist())
            sync_background_scheduler();
        refresh_jobs();
    });
    b.on_job_delete([this] {
        int i = B().get_job_selected();
        if (i < 0 || i >= static_cast<int>(store_.jobs.size()))
            return;
        std::string id = store_.jobs[i].id;
        confirm("Eliminar trabajo", "Se eliminará el trabajo «" + store_.jobs[i].name + "».", "Eliminar", true, [this, id] {
            auto &v = store_.jobs;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Job &j) { return j.id == id; }), v.end());
            if (persist())
                sync_background_scheduler();
            B().set_job_selected(-1);
            refresh_jobs();
        });
    });
    b.on_job_run([this](bool dry) {
        int i = B().get_job_selected();
        if (i < 0 || i >= static_cast<int>(store_.jobs.size()))
            return;
        const Job &j = store_.jobs[i];
        std::string id = j.id;
        if (running_jobs_.count(id)) {
            toast("Este trabajo ya se está ejecutando.", true);
            return;
        }
        if (!dry && j.options.mode == "sync" && !j.options.dry_run) {
            confirm("Sincronizar (espejo)",
                    "El destino quedará idéntico al origen: se ELIMINARÁN en el destino los archivos que no existan en el origen.\n\n"
                    "Consejo: usa «Simular» primero para ver los cambios sin aplicarlos.",
                    "Sincronizar", true, [this, id] { run_job(id, false, "manual"); });
            return;
        }
        run_job(id, dry, "manual");
    });
    b.on_job_save([this] {
        std::string id;
        save_job_from_form(id);
    });
    b.on_job_save_run([this] {
        std::string id;
        if (!save_job_from_form(id))
            return;
        const Job *j = store_.find_job(id);
        if (j && j->options.mode == "sync" && !j->options.dry_run) {
            confirm("Sincronizar (espejo)",
                    "El destino quedará idéntico al origen: se ELIMINARÁN en el destino los archivos que no existan en el origen.",
                    "Sincronizar", true, [this, id] { run_job(id, false, "manual"); });
            return;
        }
        run_job(id, false, "manual");
    });
    b.on_job_export([this] { export_jobs(); });
    b.on_job_import([this] { import_jobs(); });
    b.on_job_use_pane([this](int side, bool is_source) {
        if (pane_[side].drives_view()) {
            toast("Abre una carpeta en ese panel primero.", true);
            return;
        }
        JobForm f = B().get_job_form();
        if (is_source)
            f.source = S(pane_[side].spec());
        else
            f.dests = S(pane_[side].spec());
        B().set_job_form(f);
    });

    // mounts
    b.on_mount_select([this](int i) { B().set_mount_selected(i); });
    b.on_mount_new([this] {
        B().set_winfsp_ok(plat::winfsp_installed());
        open_mount_form("", "");
    });
    b.on_mount_save([this] { save_mount(); });
    b.on_mount_toggle([this] { toggle_mount(); });
    b.on_mount_delete([this] {
        int i = B().get_mount_selected();
        if (i < 0 || i >= static_cast<int>(store_.mounts.size()))
            return;
        std::string id = store_.mounts[i].id;
        stop_mount(id);
        auto &v = store_.mounts;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const MountDef &m) { return m.id == id; }), v.end());
        mount_rt_.erase(id);
        persist();
        B().set_mount_selected(-1);
        refresh_mounts();
    });
    b.on_mount_open([this] {
        int i = B().get_mount_selected();
        if (i < 0 || i >= static_cast<int>(store_.mounts.size()))
            return;
        std::string mp = store_.mounts[i].mountpoint;
        if (is_drive_letter(mp))
            mp += "\\";
        plat::open_path(mp);
    });
    b.on_winfsp_download([] { plat::open_url("https://winfsp.dev/rel/"); });
    b.on_mount_pick_folder([this] {
        std::string dir = plat::pick_folder("Carpeta donde crear el punto de montaje");
        if (dir.empty())
            return;
        MountForm f = B().get_mount_form();
        std::string name = T(f.name);
        std::string clean;
        for (char c : name)
            clean += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
        if (clean.empty())
            clean = "Nube";
        std::replace(dir.begin(), dir.end(), '\\', '/');
        if (!dir.empty() && dir.back() == '/')
            dir.pop_back();
        f.mountpoint = S(dir + "/" + clean);
        B().set_mount_form(f);
    });

    b.on_history_clear([this] {
        confirm("Borrar historial", "Se borrará todo el historial de operaciones.", "Borrar", true, [this] {
            store_.history.clear();
            persist();
            refresh_history();
        });
    });

    // transfers panel
    b.on_cancel_transfer([this](slint::SharedString id) {
        for (auto &t : tasks_)
            if (t->id == T(id))
                t->handle.kill();
    });
    b.on_cancel_all([this] {
        for (auto &t : tasks_)
            t->handle.kill();
    });
    b.on_clear_done([this] { done_model_->set_vector({}); });
    b.on_clear_logs([this] { log_model_->set_vector({}); });

    // settings
    b.on_settings_apply([this] { apply_settings(); });
    b.on_settings_detect([this] {
        std::string p = Rclone::auto_detect();
        if (p.empty())
            toast("No se encontró rclone en este equipo.", true);
        else {
            B().set_set_rclone_path(S(p));
            toast("rclone encontrado: " + p);
        }
    });
    b.on_settings_browse([this] {
        std::string p = plat::open_file_dialog("Seleccionar rclone.exe", L"rclone.exe\0rclone.exe\0Ejecutables (*.exe)\0*.exe\0\0");
        if (!p.empty())
            B().set_set_rclone_path(S(p));
    });
    b.on_open_data_folder([] { plat::open_path(Store::data_dir()); });
    b.on_check_updates([this] { check_updates(true); });
    b.on_open_rclone_site([] { plat::open_url("https://rclone.org"); });
}

void Controller::start()
{
    store_.load();
    const Backend &b = B();
    b.set_app_version(S(APP_VERSION));

    b.set_remote_names(remote_names_model_);
    b.set_left_files(pane_[0].model);
    b.set_right_files(pane_[1].model);
    b.set_remotes(remote_rows_);
    b.set_remote_types(provider_labels_);
    b.set_drive_remotes(drive_model_);
    b.set_remote_fields(field_rows_);
    b.set_jobs(job_rows_);
    b.set_mounts(mount_rows_);
    b.set_history(history_rows_);
    b.set_active_transfers(active_model_);
    b.set_speed_history(speed_model_);
    b.set_done_transfers(done_model_);
    b.set_logs(log_model_);
    b.set_compare_rows(cmp_model_);

    load_settings_into_ui();
    refresh_rclone_state();
    refresh_jobs();
    refresh_mounts();
    refresh_history();
    for (const auto &warning : store_.warnings)
        plat::log_error("Almacén local: " + warning);
    if (!store_.warnings.empty())
        toast(store_.warnings.front(), true);
    bind_callbacks();
    sync_background_scheduler();

    remote_names_.push_back("Disco local");
    remote_names_model_->push_back(S("Disco local"));

    pane_[0].remote.clear();
    pane_[0].path = plat::user_profile();
    std::replace(pane_[0].path.begin(), pane_[0].path.end(), '\\', '/');
    pane_[1].remote.clear();
    pane_[1].path.clear();
    sync_pane_header(0);
    sync_pane_header(1);
    load(0);
    load(1);
    load_remotes_async();

    poll_timer_.start(slint::TimerMode::Repeated, std::chrono::seconds(1), [this] {
        ++tick_;
        sample_speed();
        if (tick_ == 6 && store_.settings.auto_update)
            check_updates(false);
        poll_mounts();
        if (tick_ % 30 == 5)
            check_schedules();
    });

    for (auto &m : store_.mounts)
        if (m.auto_mount)
            start_mount(m.id);
}

void Controller::shutdown()
{
    for (auto &t : tasks_)
        t->handle.kill();
    for (auto &[id, rt] : mount_rt_)
        if (rt.handle)
            rt.handle->kill();
    if (!store_.save())
        plat::log_error("No se pudieron guardar los datos al cerrar: " + store_.save_error);
}

} // namespace

int run_app(bool start_hidden)
{
    auto ui = AppWindow::create();
    Controller ctrl(ui);
    ctrl.start();

    plat::tray_create(
        "Rclone Lite",
        [ui]() mutable {
            ui->show();
            plat::focus_main_window();
        },
        [] { slint::quit_event_loop(); });

    ui->window().on_close_requested([&ctrl]() {
        if (ctrl.minimize_to_tray())
            return slint::CloseRequestResponse::HideWindow;
        slint::quit_event_loop();
        return slint::CloseRequestResponse::HideWindow;
    });

    if (!start_hidden)
        ui->show();
    slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);
    g_alive = false;
    plat::tray_destroy();
    ctrl.shutdown();
    return 0;
}
