# Explorer Keyboard Shortcuts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use the native OpenCode workflow to implement this plan task-by-task.

**Goal:** Add safe, discoverable keyboard shortcuts for common dual-pane explorer actions.

**Architecture:** Define key bindings in the Slint window and dispatch existing `Backend` callbacks, so validation, confirmation, and operation behavior remain centralized in the controller. Limit the initial bindings to function keys that do not conflict with normal text editing.

**Tech Stack:** Slint 1.19, C++20, existing CMake/CTest build.

**Spec:** User request to continue the next improvements from the project review; keyboard shortcuts are the next item.

## Global Constraints

- Keep shortcut actions routed through existing backend callbacks.
- Do not intercept text editing, dialog, or non-explorer interactions.
- Preserve existing confirmation for delete and destructive operations.
- Build with the repository's `build.bat`; run CTest before publishing.

## Review Focus

- Dialogs and comparison overlay are open: explorer shortcuts must not fire.
- A text field has focus: shortcuts must not consume ordinary typing/editing keys.
- No files are selected: controller validation and feedback remain in effect.
- The active pane changes: copy/move must use the selected active pane as source and the opposite pane as destination.
- Delete is invoked by shortcut: existing confirmation must remain.

---

### Task 1: Explorer shortcuts

**Files:**
- Modify: `ui/app.slint`
- Modify: `ui/backend.slint` only if a shortcut callback is needed
- Test: `tests/logic_tests.cpp` or build-time Slint validation

**Interfaces:**
- Consume the existing copy, move, rename, delete, and new-folder backend callbacks.
- Function-key mappings: F2 rename, F5 copy to the opposite pane, F6 move to the opposite pane, F7 new folder, F8 delete.

- [ ] Add scoped Slint key bindings that are active only on the explorer page with no modal or comparison overlay.
- [ ] Route actions through existing backend callbacks, deriving direction from `Backend.active-side`.
- [ ] Add concise in-product shortcut discoverability without changing toolbar action semantics.
- [ ] Run `build.bat` and CTest; verify delete still invokes its existing confirmation.
- [ ] Commit and push this feature separately to the current branch.
