<!--
Sync Impact Report
==================
Version change: 1.0.0 → 1.1.0

Rationale: Restructured the free-form constitution into the Spec Kit constitution template
(Core Principles → additional sections → Governance → version footer). All technical rules
were preserved verbatim or near-verbatim. MINOR bump because Governance was materially
expanded with an explicit amendment procedure, semantic versioning policy, and compliance
review expectations required by the Spec Kit template; per-principle rationale added.

Modified principles (numbering and titles retained):
  - I. Native Windows Architecture (NON-NEGOTIABLE) — unchanged rules; rationale added
  - II. Transparent-First Visual Design (NON-NEGOTIABLE) — unchanged rules; rationale added
  - III. Integrated Title-Bar Color Picker (NON-NEGOTIABLE) — unchanged rules; rationale added
  - IV. Native Shell Interoperability — unchanged rules; rationale added
  - V. Responsive and Familiar Explorer Experience — unchanged rules; rationale added
  - VI. Separation of Responsibilities — moved from "Engineering and Implementation
        Standards" into Core Principles; project layout and build flags relocated to
        "Technology Stack & Platform Constraints"
  - VII. Rendering and Transparency Correctness — moved into Core Principles; preferred
        technology list relocated to "Technology Stack & Platform Constraints"
  - VIII. Resource, Memory, and Thread Safety — moved into Core Principles
  - IX. Reliable File Operations — moved into Core Principles
  - X. Accessibility and Windows Integration — moved into Core Principles

Section mapping (old → new):
  - Header metadata (Platform/Language/Compiler/IDE/App type) + "1. Project Vision"
        → "Project Vision" + "Technology Stack & Platform Constraints"
  - "2. Core Constitutional Principles" + "3. Engineering and Implementation Standards"
        → "Core Principles" (I–X)
  - "4. Functional Requirements and Acceptance Criteria"
        → "Functional Requirements & Acceptance Criteria"
  - "5. Testing and Quality Gates" + "6. Development Priorities"
        → "Development Workflow & Quality Gates"
  - "7. Governance and Amendment Rules" → "Governance" (expanded)

Added sections:
  - Governance: Amendment Procedure, Versioning Policy, Compliance Review
  - Version / Ratified / Last Amended footer line

Removed sections: None (all content relocated, none dropped)

Templates requiring updates:
  - .specify/templates/plan-template.md — ✅ no change required ("Constitution Check" gates
    are derived from this file at runtime)
  - .specify/templates/spec-template.md — ✅ no change required
  - .specify/templates/tasks-template.md — ✅ no change required

Follow-up TODOs: None. RATIFICATION_DATE set to 2026-09-28 (date the original
constitution was authored in this repository).
-->

# Translucent Explorer Constitution

## Project Vision

Develop a native Windows File Explorer-style application featuring a transparent or
translucent interface, a customizable background tint, and familiar Windows file-management
functionality.

The application MUST use the Win32 API and supported Windows Shell interfaces. It MUST NOT
depend on Electron, Chromium, .NET, WinUI, Qt, MFC, or other application frameworks.

The defining feature is a color picker integrated into the upper-right area of the title
bar, immediately to the left of the Minimize button. Users MUST be able to select a color and
adjust the window's appearance while retaining control over its transparency.

## Core Principles

### I. Native Windows Architecture (NON-NEGOTIABLE)

The application MUST be implemented in modern ISO C++20 and compiled with Microsoft Visual
C++. Win32 MUST own the primary window, message loop, application lifecycle, input handling,
and native resource management.

- MUST use `CreateWindowExW`, `WNDCLASSEXW`, and a native Win32 message loop.
- MUST use Unicode Windows APIs and UTF-16 paths.
- MUST use supported Windows Shell COM interfaces for navigation and file operations
  wherever appropriate.
- MUST use Desktop Window Manager (DWM) for supported backdrop effects.
- MUST be per-monitor DPI-aware.
- MUST compile for x64 in Debug and Release configurations.
- MUST use RAII to manage native handles, COM references, and other system resources.
- MUST NOT use Qt, MFC, WPF, WinForms, Electron, Chromium, or WinUI for the user interface.
- Direct2D, DirectWrite, and Windows Imaging Component (WIC) MAY be used for native
  rendering, typography, icons, and image processing.

**Rationale**: Direct ownership of the window and message loop is required to control the
non-client frame, DWM composition, and hit testing that the translucent design and title-bar
color picker depend on; application frameworks abstract these away or conflict with them.

### II. Transparent-First Visual Design (NON-NEGOTIABLE)

The application MUST support a translucent appearance across the title bar, navigation area,
folder tree, and file display area. The rendering system MUST distinguish between
translucent visual surfaces and truly transparent content.

The application MUST offer three visual modes:

1. **Acrylic** — frosted-glass appearance with background influence.
2. **Mica** — Windows 11 desktop-inspired background material.
3. **Solid** — opaque, user-selected background color.

- An additional true-opacity mode MAY be implemented only after compatibility, text
  readability, and input behavior have been validated.
- The renderer MUST preserve readable text, recognizable file icons, selected-item
  highlights, and visible keyboard focus indicators.
- Windows 11 build 22621 is the baseline for the complete Mica and Acrylic experience
  through the documented system backdrop API. Earlier Windows 11 builds MUST degrade
  gracefully.

**Rationale**: Translucency is the product's identity, but it MUST never cost legibility or
usability; explicit modes and a documented OS baseline keep behavior predictable.

### III. Integrated Title-Bar Color Picker (NON-NEGOTIABLE)

The color picker MUST reside in the upper-right corner of the main application window,
immediately to the left of the Minimize button, separated by a small visual margin.

- The title bar MUST retain conventional Windows window-management behavior, including
  dragging, resizing, maximizing, restoring, and access to the system menu.
- The application SHOULD retain the native Windows caption buttons and use a custom frame
  or client-area extension where necessary to accommodate the color picker.
- The color picker MUST NOT interfere with Minimize, Maximize, Restore, or Close hit-test
  regions.
- In a custom-frame implementation, native caption-button hit testing MUST be passed to
  `DwmDefWindowProc` before application-specific hit testing.

The color picker MUST support:

- A palette of predefined colors.
- Custom color selection.
- A preview of the selected tint.
- Supported transparency or opacity settings.
- Switching between Acrylic, Mica, and Solid modes.
- Restoring default appearance settings.
- Persisting the user's selected appearance.

Tint, backdrop mode, and opacity MUST be maintained as separate settings. The application
MUST NOT assume DWM provides exact independent opacity control for every backdrop material.

**Rationale**: The picker is the defining feature, yet placing controls in the caption area
is the most common way to break native window behavior; routing through `DwmDefWindowProc`
first preserves caption buttons and Snap Layouts.

### IV. Native Shell Interoperability

The application MUST use supported Windows Shell interfaces and MUST NOT rely on
undocumented Windows Explorer internals.

- `IExplorerBrowser` SHOULD be evaluated for native folder navigation and views.
- `IFileOperation` MUST be used for supported file operations, including copying, moving,
  renaming, and deleting files.
- Shell-hosted views MUST be evaluated for compatibility with the transparent design.
- Where an opaque native view prevents the required appearance, the architecture MAY use a
  custom-rendered file view built on supported Shell enumeration interfaces.

**Rationale**: Documented Shell interfaces give correct behavior (namespace extensions,
recycle bin, conflict UI) and remain stable across Windows updates; Explorer internals do not.

### V. Responsive and Familiar Explorer Experience

The application MUST follow familiar Windows File Explorer interaction conventions without
needing to reproduce every Microsoft Explorer feature.

The initial release MUST support:

- Folder navigation.
- An address bar.
- Back, Forward, and Up navigation.
- File listing and selection.
- Sorting.
- Opening files and folders.
- Basic file operations.
- Context-menu integration.

Additional rules:

- A navigation tree, breadcrumb bar, and search facility SHOULD be included.
- Essential functionality MUST be accessible through a keyboard.
- The application MUST remain responsive during directory enumeration and long-running file
  operations. Blocking tasks MUST NOT execute synchronously on the main UI thread.

**Rationale**: Users bring Explorer muscle memory; matching its conventions and never
freezing the UI thread are prerequisites for the app being trusted as a file manager.

### VI. Separation of Responsibilities

- The codebase MUST separate window management, appearance, Shell operations, and file-view
  rendering.
- Components MUST communicate through explicit interfaces rather than accessing one
  another's implementation details.
- The source layout SHOULD follow the recommended project structure defined in
  "Technology Stack & Platform Constraints".

**Rationale**: Appearance and Shell concerns evolve independently (e.g., swapping a
Shell-hosted view for a Direct2D view); clear boundaries make such swaps local changes.

### VII. Rendering and Transparency Correctness

Rendering MUST be designed around the actual capabilities of the Windows compositor.

- The application MUST use documented DWM backdrop APIs for Mica and Acrylic.
- Color tinting and independent opacity MUST be implemented separately where the chosen
  rendering technology supports them.
- The implementation MUST NOT claim that DWM provides precise, unrestricted opacity control
  for every system backdrop material.
- A custom Direct2D file view SHOULD be used if full-surface translucency is a strict
  requirement and a native Shell-hosted view cannot provide it.
- Technology choices MUST follow the preferred rendering stack listed in "Technology Stack
  & Platform Constraints" unless an amendment justifies a deviation.

**Rationale**: System backdrops have fixed material behavior; designing around real
compositor capabilities avoids features that cannot be delivered reliably.

### VIII. Resource, Memory, and Thread Safety

- All Windows resources MUST have explicit ownership and deterministic cleanup.
- COM interfaces MUST be managed with RAII-based smart pointers.
- Every thread interacting with COM MUST initialize the appropriate apartment model and
  comply with the threading requirements of its COM interfaces.
- Shell file operations using `IFileOperation` MUST execute in a single-threaded apartment.
- Background enumeration MUST support cancellation.
- Stale asynchronous results MUST NOT overwrite the content of a newly navigated folder.

**Rationale**: Leaked handles, apartment violations, and race conditions are the dominant
failure modes of native Shell applications and are difficult to diagnose after the fact.

### IX. Reliable File Operations

File operations MUST prioritize data integrity over visual responsiveness or convenience.

- Copy, move, rename, and delete operations MUST report failures, permission problems, and
  naming conflicts.
- Destructive actions MUST present appropriate confirmation or use documented Windows Shell
  confirmation and recycle-bin behavior.
- The application MUST NOT silently overwrite user files or permanently delete them without
  explicit user action.
- File-system paths, including long paths and Unicode filenames, MUST be handled correctly
  within the capabilities of the selected Windows APIs.

**Rationale**: A file manager that loses data is unacceptable regardless of how it looks;
file safety outranks every visual requirement.

### X. Accessibility and Windows Integration

- The application MUST respect operating-system settings for dark and light themes, text
  scaling, high contrast, reduced motion, and keyboard navigation wherever applicable.
- All custom controls, including the title-bar color picker, MUST expose accessible names,
  states, and keyboard interactions.
- Custom-rendered file items MUST provide suitable UI Automation support.

The application MUST preserve standard window behavior, including:

- System menu.
- Taskbar representation.
- Alt+Tab.
- Window snapping.
- Maximize-button Snap Layout interaction where supported by the chosen frame design.

**Rationale**: Custom frames and custom-rendered views lose built-in accessibility and
shell integration by default; these MUST be restored explicitly.

## Technology Stack & Platform Constraints

### Platform Baseline

| Item | Requirement |
|------|-------------|
| Platform | Windows 11+ (build 22621 baseline for full Mica/Acrylic) |
| Language | C++20 (ISO) |
| Compiler | Microsoft Visual C++ (MSVC) |
| Development environment | Visual Studio 2022 or later |
| Application type | Native Win32 desktop application |
| Target architecture | x64 (Debug and Release) |

### Preferred Technologies

| Concern | Technology |
|---------|------------|
| Main window and event handling | Win32 |
| Window backdrop | DWM |
| Custom title-bar content | Win32 and Direct2D |
| Color picker | Native custom popup |
| Text | DirectWrite |
| Icons and images | Windows Shell and WIC |
| Custom translucent file view | Direct2D |
| Settings persistence | Native C++ with JSON or another documented local format |

### Build Configuration

- CMake with the Visual Studio generator SHOULD be used for reproducible MSVC builds.
- The project MUST use warning level `/W4`, treat new application-code warnings as errors,
  and use `/permissive-` where supported.

### Recommended Project Structure

```text
TranslucentExplorer/
  .specify/
    memory/
      constitution.md
  specs/
  src/
    app/
      Application.cpp
      MainWindow.cpp
    window/
      CustomTitleBar.cpp
      CaptionHitTesting.cpp
      DpiManager.cpp
    appearance/
      BackdropManager.cpp
      ColorPicker.cpp
      ThemeManager.cpp
    shell/
      ShellNavigator.cpp
      DirectoryEnumerator.cpp
      FileOperationService.cpp
    ui/
      NavigationPane.cpp
      AddressBar.cpp
      FileView.cpp
      StatusBar.cpp
    settings/
      SettingsManager.cpp
  include/
  resources/
  tests/
  CMakeLists.txt
  README.md
```

## Functional Requirements & Acceptance Criteria

- **FR-001**: Launch as a standalone x64 Win32 application with a functioning native
  Windows message loop.
- **FR-002**: Display supported Mica and Acrylic backdrops with a reliable opaque fallback.
- **FR-003**: Open a color picker immediately to the left of Minimize without disrupting
  caption buttons.
- **FR-004**: Apply a tint, switch backdrop modes, and persist supported visual preferences.
- **FR-005**: Navigate local folders using an address bar, Back, Forward, and Up controls.
- **FR-006**: Show filenames, appropriate icons, sizes, modification dates, and file types.
- **FR-007**: Support file selection, opening, copying, moving, renaming, and deleting.
- **FR-008**: Support common keyboard shortcuts with visible keyboard focus.
- **FR-009**: Respond correctly to DPI, display-scale, and supported theme changes.
- **FR-010**: Handle inaccessible paths, disconnected drives, and cancelled operations
  without crashing.

## Development Workflow & Quality Gates

### Incremental Delivery

The project MUST be implemented incrementally, with the transparent UI and title-bar color
picker established before advanced file-management features.

1. **Phase 1**: Native Win32 application shell, MSVC build, and custom title-bar
   integration.
2. **Phase 2**: DWM backdrops, color picker, tint selection, and persisted appearance
   settings.
3. **Phase 3**: Shell navigation, address bar, navigation pane, and file display.
4. **Phase 4**: File operations, keyboard shortcuts, context menus, and error handling.
5. **Phase 5**: Accessibility, DPI support, performance optimization, and release
   packaging.

A custom-rendered file view SHOULD be used where necessary to meet transparency
requirements. Reusing the native Explorer browser remains permissible when it does not
compromise the agreed appearance.

### Stage Validation

Each major implementation stage MUST have a reproducible validation procedure before
development proceeds to the next stage.

### Release-Readiness Gates

A release MUST satisfy all of the following:

- The application builds in MSVC x64 Debug and Release configurations without new compiler
  warnings.
- Window controls and hit testing function at 100%, 150%, and 200% display scaling.
- The color picker remains correctly positioned and operable when resized, maximized, or
  moved between monitors.
- Changing backdrop and tint settings never makes navigation, filenames, or selection
  indicators unreadable.
- File operations preserve content and report permission errors and naming conflicts.
- Cancelling folder enumeration or rapidly navigating between folders does not display
  stale results.
- Resource and memory checks show no unresolved application-owned leaks during normal
  navigation and repeated window creation and destruction.
- Accessibility checks confirm usable keyboard navigation, accessible custom controls, and
  visible focus.

### Performance Testing

Performance tests SHOULD include:

- Folders containing at least 10,000 items.
- Large icon sets.
- Repeated navigation between local and network locations.

No fixed latency guarantee may be claimed without testing on defined hardware.

## Governance

This constitution is the authoritative source of engineering principles for Translucent
Explorer and supersedes conflicting practices, conventions, or generated guidance. Feature
specifications, implementation plans, and generated code MUST comply with it.

### Amendment Procedure

Any proposed architectural change or amendment MUST document:

- The reason for the change.
- Its compatibility with the native Win32 requirement.
- Its effects on transparency and accessibility.
- The tests required to validate it.

Amendments MUST update the constitution's version number, record the Last Amended date,
include a Sync Impact Report, and explain any changes to non-negotiable principles.

No amendment may silently weaken file-safety requirements or introduce undocumented Windows
APIs as required application dependencies.

### Versioning Policy

The constitution follows semantic versioning (`MAJOR.MINOR.PATCH`):

- **MAJOR**: Backward-incompatible removal or redefinition of a principle or governance
  rule, including any change to a NON-NEGOTIABLE principle.
- **MINOR**: A new principle or section, or materially expanded guidance.
- **PATCH**: Clarifications, wording, and typo fixes with no semantic change.

### Compliance Review

- Every implementation plan MUST pass a Constitution Check against Principles I–X before
  design work begins and again after design is complete.
- Code reviews MUST verify compliance with the Core Principles and the Release-Readiness
  Gates; any deviation MUST be justified in the plan's complexity tracking.
- Unjustified violations of a NON-NEGOTIABLE principle block merge.

**Version**: 1.1.0 | **Ratified**: 2026-09-28 | **Last Amended**: 2026-09-28
