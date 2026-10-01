Feature Specification: Translucent Explorer
Feature Branch: `001-translucent-explorer`  
Created: 2026-09-28  
Status: Draft  
Input: Translucent Explorer Constitution, version 1.1.0 (ratified 2026-09-28)  
Scope: Initial Windows desktop release
1. Overview
Translucent Explorer is a native Windows 11+ File Explorer-style application with a transparent-first interface and a title-bar color picker positioned immediately to the left of the Minimize button. It supports Acrylic, Mica and Solid appearance modes, color-tint customization, persistent visual preferences, conventional folder navigation and safe file management.
The application uses C++20, MSVC, native Win32 window management and supported Windows Shell interfaces. Its initial release emphasizes a usable translucent window and an integrated color picker before adding advanced file management. Windows 11 build 22621 is the baseline for the complete Mica/Acrylic experience; earlier Windows 11 builds receive a graceful fallback.
Scope boundaries
In scope: Native main window, integrated color picker, three appearance modes, tint preferences, navigation, folder/file listing, sorting, selection, opening, basic file operations, context menus, keyboard accessibility, DPI handling, asynchronous enumeration and resilience.
Conditional, based on feasibility: Custom-rendered Direct2D file view when a Shell-hosted view is too opaque; independent opacity adjustment only for rendering modes that support it; native Shell browser reuse when compatible with the required appearance.
Should-have, not required to complete the initial release: Navigation tree, breadcrumb bar and search facility.
Out of scope for the initial release: Full feature parity with Windows Explorer, undocumented Explorer integrations, cloud-provider sync, custom Shell extensions, an unrestricted compositor-opacity guarantee, and an unvalidated true-opacity mode.
2. User Scenarios & Testing (mandatory)
User Story 1 — Launch and use a translucent Explorer window (Priority: P1)
As a Windows user, I want to launch a familiar file-manager window with a translucent appearance so that I can navigate files without an opaque, visually disconnected interface.
Why this priority: The transparent-first native window is the product's identity and a prerequisite for every other workflow.
Independent Test: Launch the application on a supported Windows 11 system, inspect the title bar and primary surfaces, switch between supported backdrop modes and confirm window management still works.
Acceptance Scenarios:
Given Windows 11 build 22621 or later, When the user launches the application, Then a native resizable desktop window opens with a supported default backdrop and readable navigation and content regions.
Given the application is open, When the user selects Acrylic, Mica or Solid, Then the selected supported mode is applied without restarting or losing the current folder.
Given the selected system backdrop cannot be applied, When the application detects the failure, Then it uses a readable fallback without closing or misrepresenting the active visual effect.
Given the window is visible, When the user drags, resizes, maximizes, restores, minimizes or closes it, Then familiar native Windows behavior is preserved.
Given transparency reduces contrast or high contrast is enabled, When the application renders navigation, file names, selections and focus, Then all essential content remains legible.
---
User Story 2 — Change color from the title bar (Priority: P1)
As a user, I want a color picker immediately to the left of Minimize so that I can customize the Explorer window's appearance directly from the title bar.
Why this priority: The integrated color picker is the defining feature required by the constitution.
Independent Test: Open the picker, select a preset and custom color, preview the result, change supported appearance settings, reset defaults and restart the application to check persistence.
Acceptance Scenarios:
Given the main window is open, When the user looks immediately left of the Minimize button, Then a distinct, discoverable color-picker control appears with a small separating margin.
Given the picker is closed, When the user activates it with the pointer or keyboard, Then an accessible native popup appears without moving or disabling caption buttons.
Given the popup is open, When the user chooses a preset or custom color, Then the selected color is previewed and applied to supported tintable surfaces.
Given the popup is open, When the user changes Acrylic, Mica or Solid mode, Then the corresponding supported appearance is applied and the current folder and file selection remain unchanged.
Given a rendering mode does not support independent opacity, When the user inspects opacity settings, Then the interface explains or disables the unsupported control rather than implying exact compositor opacity.
Given customized appearance settings, When the user selects Reset, Then documented defaults are restored.
Given appearance settings have been saved, When the application restarts, Then the saved values are restored or a safe default is used if a value is no longer supported.
Given the picker is present, When the user activates Minimize, Maximize, Restore, Close or Snap Layouts where supported, Then caption operations work without overlapping picker hit regions.
---
User Story 3 — Navigate and inspect files (Priority: P2)
As a user, I want familiar folder navigation, a usable address bar and a file listing so that I can browse my local files without switching to another file manager.
Why this priority: This makes the visual shell a functional Explorer-style application.
Independent Test: Browse a set of nested folders, use Back/Forward/Up, enter a path, sort entries, select items and open files with their associated applications.
Acceptance Scenarios:
Given a valid local folder path, When the user enters it in the address bar, Then its available folders and files appear with their names, icons, types, sizes and modification dates where applicable.
Given the user navigates from folder A to B to C, When Back and Forward are used, Then the correct visited locations are restored.
Given a nested folder is open, When Up is selected, Then the parent folder opens without creating an incorrect history entry.
Given folder items are displayed, When the user sorts by a supported field, Then their display order reflects that field and direction.
Given a file has an associated application, When the user activates Open, Then Windows opens the file through its normal association or returns a clear failure.
Given a folder is inaccessible or a drive is disconnected, When navigation is attempted, Then a clear recoverable error appears and the application remains responsive.
Given the user navigates rapidly between directories, When an earlier enumeration completes after a newer one, Then only results belonging to the current navigation request are shown.
---
User Story 4 — Manage files safely (Priority: P2)
As a user, I want to copy, move, rename and delete selected files while receiving appropriate confirmations and error messages so that I can manage content without accidental data loss.
Why this priority: File safety takes precedence over visual polish under the constitution.
Independent Test: Carry out operations against temporary test files, including deliberate naming conflicts, insufficient permissions and cancellation.
Acceptance Scenarios:
Given one or more selected files, When the user initiates copy or move, Then the operation uses supported Shell behavior and reports completion or failure.
Given a destination naming conflict, When a copy or move is initiated, Then the user is offered appropriate Shell conflict handling and no existing file is silently overwritten.
Given a selected file, When the user renames it, Then the updated name is reflected only after the underlying operation succeeds.
Given a user requests deletion, When confirmation or applicable Shell recycle-bin handling is required, Then the user receives it; permanent deletion never occurs silently.
Given an operation fails because of access denial, path limitations or a disconnected volume, When the failure occurs, Then the user receives an actionable error and existing file content remains protected.
Given a long-running operation, When the user continues interacting with the window, Then the main interface remains responsive and displays appropriate operation state.
Given a file or folder is selected, When the user invokes its context menu, Then applicable supported Shell commands are available and operate on the intended item.
---
User Story 5 — Use native Windows interaction and accessibility (Priority: P2)
As a keyboard, high-DPI or assistive-technology user, I want the application to follow native Windows interaction conventions so that its custom appearance does not reduce usability.
Why this priority: The custom frame and file view must preserve familiar native behavior and accessibility.
Independent Test: Operate the application without a mouse, inspect custom controls through accessibility tooling and move the window between displays with different scaling.
Acceptance Scenarios:
Given the user navigates by keyboard, When focus moves between address bar, picker, navigation controls and file list, Then focus remains visible and each control is operable.
Given the operating system is set to 100%, 150% or 200% scaling, When the window opens or changes monitor, Then the picker, caption buttons and file content are correctly laid out without clipping or overlapping.
Given a screen reader examines a custom control, When it receives accessibility information, Then the control provides an appropriate accessible name, role and current state.
Given Windows dark/light, text-scaling, high-contrast or reduced-motion settings change, When the application receives the relevant change, Then affected visuals and interactions adapt where supported.
Given the custom title bar is active, When the user opens the system menu, uses Alt+Tab, snaps the window or invokes supported maximize-button Snap Layouts, Then the expected Windows interaction remains available.
---
User Story 6 — Remain responsive with large folders (Priority: P3)
As a user, I want navigation and visual customization to remain responsive when browsing large folders or unreliable locations.
Why this priority: Responsiveness and correctness must hold under realistic workloads.
Independent Test: Browse a directory with at least 10,000 entries, repeatedly change locations, disconnect a test drive or network destination where applicable and perform repeated window lifecycle tests.
Acceptance Scenarios:
Given a folder containing at least 10,000 items, When it is opened, Then enumeration and item presentation proceed without blocking the UI thread.
Given a long-running enumeration, When the user navigates elsewhere or closes the window, Then pending work is cancelled or safely disregarded.
Given many icons or thumbnails are being retrieved, When the user scrolls or changes the folder, Then the interface continues accepting input and never presents results from an obsolete folder as current.
Given repeated window creation and destruction, When application-owned resources are inspected, Then there are no unresolved ownership leaks.
3. Edge Cases
Unsupported or partially supported backdrops: Fall back to another supported mode or Solid, communicate the active mode accurately and preserve legibility.
Tint visibility differs by mode: Apply the tint only where supported; the picker must accurately preview the application's tintable surfaces rather than promise precise system-backdrop tint.
Opacity unsupported: Disable or label exact-opacity adjustment as unavailable; never fake a guaranteed numeric compositor effect.
High contrast or reduced transparency: Honor system preferences where applicable and provide an opaque, legible fallback.
Very narrow windows: Preserve all native caption buttons and the picker; adapt other title-bar content rather than overlaying caption hit regions.
Maximized or mixed-DPI windows: Recalculate caption and popup positions using the current monitor's scaling and work area; do not position the popup outside the visible display.
Color picker dismissed without selection: Close cleanly without accidental file-list interaction; retain or restore the intended selection state.
Inaccessible, missing or disconnected location: Show a recoverable error and maintain a usable current or fallback view.
Long and Unicode paths: Use compatible Shell/Unicode handling and report operations that the selected API cannot complete.
Conflicts, access denied and cancelled operations: Surface supported Shell prompts and truthful completion status; do not silently replace or destroy content.
Rapid folder changes: Invalidate stale requests and prevent obsolete item lists, icon results or errors from replacing the current view.
Unavailable file association: Explain that the file cannot be opened with its current association and leave the window responsive.
Corrupted settings file: Restore safe defaults while preserving the ability to reconfigure appearance.
Native Shell view cannot render translucently: Use the constitution-permitted custom file-view path; do not present an opaque view as compliant with full-surface translucency.
4. Requirements (mandatory)
Functional Requirements
FR-001 — Native startup: The application MUST start as a standalone x64 Win32 desktop program with a working message loop and native window lifecycle.
FR-002 — Appearance modes: The application MUST provide Acrylic, Mica and Solid modes, applying documented supported backdrops and a reliable opaque fallback.
FR-003 — Translucent surfaces: The title bar, navigation area, folder tree when present, and file display area MUST meet the translucent visual requirement wherever supported by the selected rendering implementation.
FR-004 — Picker placement: The color picker MUST be immediately to the left of Minimize with a small visible margin and MUST NOT overlap native caption-button hit targets.
FR-005 — Picker controls: The popup MUST expose preset colors, a custom color choice, tint preview, supported opacity controls, backdrop-mode switching and Reset.
FR-006 — Independent settings: Tint, backdrop mode and opacity MUST be separate persisted preferences, and unsupported opacity controls MUST be presented accurately.
FR-007 — Settings recovery: Settings MUST persist between launches and recover to readable defaults when missing, corrupted or unsupported.
FR-008 — Native caption behavior: The window MUST preserve drag, resize, minimize, maximize, restore, close and system-menu behavior, as well as supported snapping and Snap Layouts.
FR-009 — Navigation: Users MUST be able to navigate local folders through an address bar and Back, Forward and Up controls.
FR-010 — File listing: The file view MUST display filenames, suitable icons, sizes, modification dates and types where the underlying Shell item exposes them.
FR-011 — Item interaction: Users MUST be able to select items, sort displayed files and folders, and open them using appropriate Windows behavior.
FR-012 — File operations: The application MUST support copy, move, rename and delete using supported Windows Shell file-operation behavior.
FR-013 — Data safety: The application MUST report permission errors and naming conflicts, use appropriate confirmation or recycle-bin behavior, and MUST NOT silently overwrite or permanently delete user files.
FR-014 — Context menus: Users MUST have access to applicable supported native Shell context-menu actions for selected items.
FR-015 — Keyboard access: Essential navigation, selection, file operations and color-picker interactions MUST be available from the keyboard with visible focus.
FR-016 — Accessibility: Custom UI controls and any custom-rendered file items MUST expose suitable accessibility information and respect supported Windows accessibility preferences.
FR-017 — Display scaling: The window and custom controls MUST function at 100%, 150% and 200% display scale, including monitor transitions.
FR-018 — Asynchronous enumeration: Potentially blocking folder enumeration and file tasks MUST NOT freeze the UI thread; background enumeration MUST support cancellation or safe invalidation.
FR-019 — Stale-result prevention: Completion events, file metadata and icons from an earlier navigation request MUST NOT replace data for the current location.
FR-020 — Recoverable failures: Invalid paths, inaccessible locations, disconnected drives, unsupported rendering settings and operation failures MUST NOT crash the application.
FR-021 — Optional navigation aids: A navigation tree, breadcrumb bar and search facility SHOULD be provided when compatible with the initial delivery scope and translucent design.
FR-022 — Accurate appearance reporting: The interface MUST NOT claim exact opacity or tint effects that the chosen DWM backdrop cannot provide.
Constitutional / Technical Constraints
TC-001: Implementation MUST use ISO C++20 and MSVC; Visual Studio 2022 or newer; Windows 11+; x64 Debug and Release.
TC-002: Win32 MUST own the window, message pump, lifecycle and native event handling. Use `CreateWindowExW`, `WNDCLASSEXW` and Unicode Windows APIs.
TC-003: No Electron, Chromium, .NET, WinUI, Qt, MFC, WPF or WinForms UI dependency is permitted.
TC-004: DWM MUST provide supported system backdrops. Direct2D/DirectWrite/WIC MAY provide custom rendering, typography and image support.
TC-005: Use supported Windows Shell COM interfaces; evaluate `IExplorerBrowser` for navigation and views; use `IFileOperation` for the relevant file operations.
TC-006: If custom-frame hit testing is used, pass native caption-button tests to `DwmDefWindowProc` before custom processing.
TC-007: If a Shell-hosted file view prevents required translucency, the implementation MAY adopt a custom Direct2D file view based on supported Shell enumeration.
TC-008: All Windows and COM resources MUST have explicit RAII ownership and deterministic cleanup; every COM-using thread MUST observe COM apartment requirements; `IFileOperation` MUST run in an STA.
TC-009: Separate window management, appearance, Shell integration, file-view rendering and settings through explicit interfaces.
TC-010: Use documented APIs only. Do not rely on undocumented Explorer implementation details.
TC-011: Prefer CMake with the Visual Studio generator; build with `/W4` and `/permissive-` where supported and treat new application-code warnings as errors.
TC-012: Conduct Constitution Checks before and after design against all ten principles of constitution v1.1.0.
Key Entities
AppearanceSettings: Selected backdrop mode, selected tint, optional supported opacity value and default/restored-state metadata.
WindowState: Active size, position, DPI, maximize state, active caption layout and rendering capability status.
Location: Navigable Windows Shell location or supported local filesystem path, with display name and parent relationship where available.
NavigationHistory: Ordered visited locations and current history position for Back/Forward behavior.
FileItem: Shell identity, name, type, applicable size, modification date, icon reference, selection state and supported operations.
DirectoryRequest: Requested location, unique navigation generation, cancellation state and enumeration status.
FileOperationRequest: Operation type, source items, destination if applicable, user-confirmation state and progress/completion information.
5. Success Criteria (mandatory)
SC-001: On the baseline supported Windows 11 build, the initial window displays its supported chosen backdrop and all principal navigation/content surfaces remain readable; on unsupported configurations it uses a legible fallback.
SC-002: Every test of the title-bar picker at normal, maximized and resized states places it immediately before Minimize without overlap or loss of native caption actions.
SC-003: Preset and custom color selection, backdrop switching and Reset produce their documented supported results; persisted settings restore correctly after restart.
SC-004: The application's native title-bar behavior, system menu, taskbar presence, Alt+Tab, window snapping and supported Snap Layouts pass the defined manual interaction test suite.
SC-005: The navigation test suite passes address-bar navigation, Back/Forward/Up, file listing, sorting, selection and open operations on prepared local test folders.
SC-006: All file-operation test cases involving success, conflicts, denied permissions, cancellation and deletion confirmation finish with truthful status and no unintended data loss.
SC-007: At 100%, 150% and 200% scaling, the title-bar picker, caption controls and primary content have no blocking overlap or clipping; moving between differently scaled monitors passes the same checks.
SC-008: The keyboard and accessibility test suite confirms that all essential controls are reachable and operable, show focus and expose appropriate accessible descriptions.
SC-009: When loading a prepared folder containing at least 10,000 items, the window continues responding to user input; rapid navigation never displays obsolete enumeration results as current data.
SC-010: Both MSVC x64 Debug and Release builds complete with no new compiler warnings, and normal navigation plus repeated window creation/destruction show no unresolved application-owned resource leaks.
SC-011: The five constitutional delivery phases each have documented and passing stage-validation evidence before work is accepted into the next phase.
These criteria are pass/fail conditions. No specific navigation latency or frame-rate target is asserted without measured results on documented test hardware.
6. Assumptions and Dependencies
Users run Windows 11; build 22621 or later is expected for the complete Acrylic/Mica backdrop experience.
Microsoft Visual C++ and a compatible Windows SDK are available during development; the delivered application is native x64.
The initial release operates primarily on local Windows Shell locations and supported filesystem folders. Network location testing is required for responsiveness, but full cloud-provider integration is not in scope.
Existing Windows file associations, Shell dialogs and supported context menus are reused instead of being reimplemented.
The exact default tint, preset palette, opacity-control ranges and initial launch directory are product-design decisions to resolve in the implementation plan; they are not fixed by constitution v1.1.0.
The app will select a custom-rendered file view if the Shell-hosted approach fails the agreed transparency acceptance tests.
7. Delivery Slices and Dependencies
Phase	Scope	Exit condition
1	Native Win32 window, MSVC build, title-bar control placement and hit testing	FR-001, FR-004 and FR-008 basic behavior validated
2	DWM backdrops, picker, tint and persistence	FR-002, FR-003, FR-005–007 and FR-022 validated
3	Shell navigation and file-view presentation	FR-009–011 and FR-018–019 validated
4	File operations, context menus and error recovery	FR-012–014 and FR-020 validated
5	Accessibility, DPI, performance and release gates	FR-015–017 and SC-001–011 complete
Each phase requires a reproducible validation procedure. Plans MUST perform a Constitution Check before and after design, and any deviation must be justified under the attached constitution's governance rules.
8. Requirement Traceability
Constitution principle	Specification coverage
I. Native Windows Architecture	TC-001–004, SC-010
II. Transparent-First Visual Design	User Story 1, FR-002–003, SC-001
III. Integrated Title-Bar Color Picker	User Story 2, FR-004–008, SC-002–004
IV. Native Shell Interoperability	User Stories 3–4, TC-005, TC-007
V. Responsive and Familiar Explorer Experience	User Stories 3 and 6, FR-009–011, FR-018–021
VI. Separation of Responsibilities	TC-009
VII. Rendering and Transparency Correctness	FR-002–003, FR-022, TC-004, TC-007
VIII. Resource, Memory and Thread Safety	FR-018–019, TC-008, SC-009–010
IX. Reliable File Operations	User Story 4, FR-012–014, SC-006
X. Accessibility and Windows Integration	User Story 5, FR-008, FR-015–017, SC-004, SC-007–008
