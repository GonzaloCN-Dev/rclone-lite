# High-Priority Release Implementation Plan

> **For agentic workers:** Implement this plan natively in the current OpenCode session; the user requested implementation and publication.

**Goal:** Let users opt into scheduled jobs while Rclone Lite is closed, exercise real rclone operations in automated tests, and build/publish releases through GitHub Actions.

**Architecture:** The app registers one per-user Windows Scheduled Task that polls due jobs once per minute only after the user enables the setting. A headless `--run-due-jobs` mode reads the same local store and runs due jobs without the UI. CTest gains local-filesystem integration tests against a pinned real rclone binary, and a Windows GitHub Actions workflow builds/tests on pushes and publishes the installer on version tags.

**Tech Stack:** C++20, Slint, Windows Task Scheduler (`schtasks.exe`), CMake/CTest, GitHub Actions.

**Spec:** User request in conversation: implement all high-priority improvements and publish a new GitHub release.

## Global Constraints

- Existing schedules continue to run inside the app unless the user enables background scheduling.
- The background task runs as the current Windows user and never stores account credentials.
- Headless runs must update job status and operation history in `%APPDATA%\RcloneLite`.
- Integration tests use temporary local folders and a pinned rclone release; no cloud account is required.
- A tag-triggered workflow must build and test before publishing installer assets.

## Review Focus

- No scheduled jobs or the setting is off: no background task is registered.
- Task registration fails or is unavailable: in-app scheduling remains available and an actionable error is shown.
- Rclone process fails or is cancelled: job status/history records failure without losing other destinations.
- A task is started while the UI is open: scheduled jobs are not run concurrently by both processes.
- CI runs without rclone in PATH: the pinned test binary is explicitly configured.

---

### Task 1: Background scheduled-job execution

**Files:**
- Create: `src/scheduler.h`, `src/scheduler.cpp`
- Modify: `src/main.cpp`, `src/controller.cpp`, `src/store.h`, `src/store.cpp`, `ui/backend.slint`, `ui/pages.slint`, `CMakeLists.txt`
- Test: `tests/logic_tests.cpp`

**Interfaces:**
- `std::vector<std::string> background_task_arguments(const std::string &exe)` returns the stable per-user task action.
- `OpResult configure_background_task(const std::string &exe, bool enabled)` creates or removes the Windows task.
- `int run_due_jobs_headless()` loads the store, executes due jobs, records outcomes, and returns a nonzero code if any job fails.

- [x] Test task action arguments, opt-in persistence, and schedule due-time boundaries.
- [x] Implement headless mode before single-instance signaling; keep ordinary UI launches unchanged.
- [x] Register/remove the task when the setting is applied and at app startup; preserve the in-app scheduler as fallback.
- [x] Run build and CTest; manually verify task registration/removal and headless execution on Windows.

### Task 2: Real-rclone integration coverage

**Files:**
- Create: `tests/rclone_integration_tests.cpp`
- Modify: `CMakeLists.txt`, `.github/workflows/windows.yml`

**Interfaces:**
- Integration test reads `RCLONE_EXE`; return CTest skip code 77 only when it is unset.

- [x] Test wrapper copy, move, `--files-from-raw`, and selective delete against temporary local directories.
- [x] Verify the tests fail when expected destination files or source removals are absent.
- [x] Run against pinned rclone v1.76.0 locally and in CI.

### Task 3: CI and release automation

**Files:**
- Create: `.github/workflows/windows.yml`

- [x] Build and run CTest on Windows for pushes and pull requests.
- [x] On `v*` tags, install Inno Setup, build the installer, generate its SHA-256 file, and publish both assets with the release notes.
- [x] Verify workflow permissions are limited to `contents: write` only for release publication.

### Task 4: Versioned release

**Files:**
- Modify: `CMakeLists.txt`, `installer/RcloneLite.iss`, `README.md`, `RELEASE_NOTES.md`

- [x] Bump the app and installer version to 1.3.0 and document these improvements.
- [x] Build locally, run CTest, create installer and verify its SHA-256.
- [ ] Create and push tag `v1.3.0`; wait for GitHub Actions and verify the published release assets.
