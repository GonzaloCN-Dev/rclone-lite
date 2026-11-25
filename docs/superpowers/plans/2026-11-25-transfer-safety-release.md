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

- [ ] Protect active operations during updates; decision logic is tested and both UI checks are in place, but end-to-end and background-process coverage remain.
- [ ] Design and test conflict detection across UI and headless processes, including nested destinations.
- [ ] Implement and test rclone-backed sync preview and explicit deletion confirmation.
- [ ] Show last run, next due time and evidence-based schedule diagnostics, with persistence tests.
- [ ] Run full Windows build, local and real-rclone integration tests, then publish and verify release assets.
