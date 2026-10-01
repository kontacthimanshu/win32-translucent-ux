#pragma once

// Resource identifiers for TranslucentExplorer.rc. Shared by the resource compiler
// and C++ code, so this header contains only #define lines.
//
// Format strings use FormatMessageW insert syntax: %1, %2, ... (positional).

// ---------------------------------------------------------------------------
// Icons and accelerators
// ---------------------------------------------------------------------------
#define IDI_APP   101
#define IDR_ACCEL 102

// ---------------------------------------------------------------------------
// Commands and the appearance popup: shared with the application code, which
// cannot include this file (T045).
// ---------------------------------------------------------------------------
#include <te/app/CommandIds.h>
#include <te/appearance/AppearanceIds.h>

// ---------------------------------------------------------------------------
// Strings: application
// ---------------------------------------------------------------------------
#define IDS_APP_TITLE        1000 // "Inference Explorer - The PC"
#define IDS_WINDOW_TITLE_FMT 1001 // "Inference Explorer - The PC" (fixed: no %1)

// ---------------------------------------------------------------------------
// Strings: status (UI contract §3). The popup's strings are in AppearanceIds.h.
// ---------------------------------------------------------------------------
#define IDS_MODE_ACRYLIC                1100
#define IDS_MODE_MICA                   1101
#define IDS_MODE_SOLID                  1102
#define IDS_MODE_TRANSPARENT            1103
#define IDS_STATUS_APPEARANCE_FMT       1130 // "%1 · Surface %2!u!%% · Tint %3!u!%%"
#define IDS_STATUS_APPEARANCE_SOLID_FMT 1131 // "%1"
#define IDS_STATUS_FALLBACK_FMT         1132 // "(fallback: %1)"
#define IDS_FALLBACK_HIGH_CONTRAST      1140
#define IDS_FALLBACK_TRANSPARENCY_OFF   1141
#define IDS_FALLBACK_UNSUPPORTED        1142
#define IDS_FALLBACK_APPLY_FAILED       1143

// Preset colour names (research R-11), in palette order.
#define IDS_COLOR_BLUE      1150
#define IDS_COLOR_NAVY      1151
#define IDS_COLOR_TEAL      1152
#define IDS_COLOR_SEA_GREEN 1153
#define IDS_COLOR_GREEN     1154
#define IDS_COLOR_GOLD      1155
#define IDS_COLOR_ORANGE    1156
#define IDS_COLOR_RED       1157
#define IDS_COLOR_ROSE      1158
#define IDS_COLOR_PURPLE    1159
#define IDS_COLOR_SLATE     1160
#define IDS_COLOR_GRAPHITE  1161

// ---------------------------------------------------------------------------
// Strings: status bar and errors (UI contract §6)
// ---------------------------------------------------------------------------
#define IDS_STATUS_ITEMS_FMT       1200 // "%1!u! items"
#define IDS_STATUS_SELECTED_FMT    1201 // "%1!u! selected"
#define IDS_ERR_PATH_NOT_FOUND_FMT 1210 // "Windows can't find '%1'. ..."
#define IDS_ERR_LOCATION_FMT       1211 // "Can't open '%1': %2"
#define IDS_ERR_CANT_OPEN_TITLE    1220
#define IDS_ERR_CANT_OPEN_BODY_FMT 1221 // "... '%1' ..."
#define IDS_CMD_OPEN_WITH          1222
#define IDS_OP_COPYING_FMT         1230 // "Copying %1!u! items…"
#define IDS_OP_MOVING_FMT          1231
#define IDS_OP_DELETING_FMT        1232
#define IDS_OP_RENAMING            1233
#define IDS_OP_COPIED_FMT          1240 // "%1!u! items copied"
#define IDS_OP_MOVED_FMT           1241
#define IDS_OP_DELETED_FMT         1242
#define IDS_OP_RENAMED             1243
#define IDS_OP_PARTIAL_FMT         1250 // "%1!u! of %2!u! items completed"
#define IDS_OP_CANCELLED_FMT       1251 // "Operation cancelled — %1!u! items completed before cancellation"
#define IDS_OP_FAILED_TITLE        1252
#define IDS_OP_FAILED_ITEM_FMT     1253 // "%1: %2"
#define IDS_ERR_INVALID_NAME       1260
#define IDS_SETTINGS_RESET         1270
#define IDS_A11Y_PICKER_NAME       1300 // "Appearance and color" (UIA name, T075)
#define IDS_A11Y_FILE_LIST         1301 // "Items" (UIA name of the file list, T076)
#define IDS_A11Y_TOOLBAR           1302 // "Navigation" (T078)
#define IDS_A11Y_ADDRESS           1303 // "Address" (T078)
#define IDS_A11Y_NAV_PANE          1304 // "Navigation pane" (T078)
#define IDS_A11Y_RENAME            1305 // "Name" (UIA name of the inline-rename edit, T083)
#define IDS_FILTER_PLACEHOLDER     1306 // "Filter" (the filter box's placeholder and UIA name, T092)
