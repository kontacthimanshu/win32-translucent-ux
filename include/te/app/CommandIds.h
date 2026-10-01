#pragma once

// Navigation command identifiers (UI contract §4; T064), shared by the accelerator table
// in resources/TranslucentExplorer.rc (through resource.h) and MainWindow. Only #define
// lines, so the resource compiler can read it.

#define IDM_BACK          40010 // Alt+Left (Backspace is handled in WM_KEYDOWN, outside edits)
#define IDM_FORWARD       40011 // Alt+Right
#define IDM_UP            40012 // Alt+Up
#define IDM_REFRESH       40013 // F5
#define IDM_FOCUS_ADDRESS 40014 // Ctrl+L, Alt+D, F4
#define IDM_FOCUS_FILTER  40015 // Ctrl+F (T092)
