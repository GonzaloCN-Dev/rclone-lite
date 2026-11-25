# Transfer Safety Release Implementation Plan

**Goal:** Improve transfer safety, sync previews, scheduling diagnostics and update behavior before publishing a new release.

**Architecture:** Keep Windows/Slint UI and rclone execution paths consistent. Validate destructive previews using rclone itself, protect overlapping writes across foreground and background processes, and record only failure causes supported by evidence.

**Tech Stack:** C++20, Slint, Windows Task Scheduler, rclone, CMake/CTest, GitHub Actions.

## Global constraints

- Preserve existing remote credentials, jobs and settings.
- Never assert a machine was powered off or offline without evidence.
- A dry run is only a preview; remote contents can change before execution.
- Background jobs must not silently bypass conflict protections.

## Review focus

- A new transfer starts while an update is downloading: recheck before launching the installer.
- Paths overlap by directory boundary; local case and remote semantics differ.
- Multiple destinations and background processes must not escape conflict detection.
- Preview errors or truncated output must not be presented as a complete safe plan.
- Missed schedules cannot always be distinguished from a disabled scheduler or signed-out user.

## Tasks

- [x] Protect active operations during updates; decision logic is tested and both UI checks are in place. Background work is explicitly documented as outside this process-local check.
- [x] Prevent a concurrent foreground or headless rclone transfer with a shared Windows mutex; integration test covers lock contention.
- [x] Require a dry-run preview for manual sync operations before the destructive confirmation; simulation results remain visible in Transferencias.
- [x] Show the next scheduled run or an overdue schedule in the jobs list; last outcome remains visible through the existing status.
- [ ] Run full Windows build, local and real-rclone integration tests, then publish and verify release assets.
