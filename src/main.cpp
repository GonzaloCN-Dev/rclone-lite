#include <cstdlib>
#include <cstring>

#include "platform.h"
#include "scheduler.h"

int run_app(bool start_hidden);

int main(int argc, char **argv)
{
    bool hidden = false;
    bool run_due_jobs = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--minimized") == 0)
            hidden = true;
        else if (std::strcmp(argv[i], "--run-due-jobs") == 0)
            run_due_jobs = true;

    if (run_due_jobs) {
        rl::plat::install_crash_handlers();
        rl::plat::kill_children_on_exit();
        return rl::run_scheduled_jobs_headless();
    }
    if (rl::plat::signal_existing_instance())
        return 0;

    // The OpenGL renderer shows a blank window on some drivers; software is reliable.
    if (!std::getenv("SLINT_BACKEND"))
        _putenv_s("SLINT_BACKEND", "winit-software");
    rl::plat::install_crash_handlers();
    rl::plat::kill_children_on_exit();
    return run_app(hidden);
}
