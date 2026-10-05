#pragma once

// Resource identifiers shared by resources/TranslucentExplorer.rc (through
// resource.h) and the application code: the appearance command, the appearance
// popup dialog and its controls, and the popup's strings (UI contract §3; T044,
// T045). Only #define lines, so the resource compiler can read it.

// Commands (accelerators, UI contract §4)
#define IDM_OPEN_APPEARANCE 40001 // Alt+Shift+C: open the appearance popup

// Appearance popup dialog and controls
#define IDD_APPEARANCE 200

#define IDC_MODE_GROUP     2000
#define IDC_MODE_ACRYLIC   2001
#define IDC_MODE_MICA      2002
#define IDC_MODE_SOLID     2003
#define IDC_MODE_REASON    2004
#define IDC_SWATCH_0       2010 // IDC_SWATCH_0 + i, i = 0..11, in kPresetPalette order
#define IDC_SWATCH_1       2011
#define IDC_SWATCH_2       2012
#define IDC_SWATCH_3       2013
#define IDC_SWATCH_4       2014
#define IDC_SWATCH_5       2015
#define IDC_SWATCH_6       2016
#define IDC_SWATCH_7       2017
#define IDC_SWATCH_8       2018
#define IDC_SWATCH_9       2019
#define IDC_SWATCH_10      2020
#define IDC_SWATCH_11      2021
#define IDC_SWATCH_ACCENT  2030
#define IDC_CUSTOM         2031
#define IDC_SWATCH_TRANSPARENT 2032 // clear glass: BackdropMode::Transparent
#define IDC_SURFACE_LABEL  2040
#define IDC_SURFACE        2041
#define IDC_TINT_LABEL     2042
#define IDC_TINT           2043
#define IDC_OPACITY_REASON 2044
#define IDC_PREVIEW_LABEL  2050
#define IDC_PREVIEW        2051
#define IDC_RESET          2060

// Slab popup dialog and controls (SlabPopup)
#define IDD_SLAB           201
#define IDC_SLAB_LABEL     2100
#define IDC_SLAB_DEFAULT   2103
// One row per edge: its check box, its thickness trackbar and the value.
#define IDC_SLAB_TOP       2104
#define IDC_SLAB_LEFT      2105
#define IDC_SLAB_BOTTOM    2106
#define IDC_SLAB_RIGHT     2108
#define IDC_SLAB_TOP_TRACK    2110
#define IDC_SLAB_LEFT_TRACK   2111
#define IDC_SLAB_BOTTOM_TRACK 2112
#define IDC_SLAB_RIGHT_TRACK  2113
#define IDC_SLAB_TOP_VALUE    2120
#define IDC_SLAB_LEFT_VALUE   2121
#define IDC_SLAB_BOTTOM_VALUE 2122
#define IDC_SLAB_RIGHT_VALUE  2123

// Popup strings (STRINGTABLE)
#define IDS_REASON_REQUIRES_22621   1110
#define IDS_REASON_TRANSPARENCY_OFF 1111
#define IDS_REASON_HIGH_CONTRAST    1112
#define IDS_REASON_SOLID_OPAQUE     1113
#define IDS_LABEL_SURFACE_OPACITY   1120
#define IDS_LABEL_TINT_STRENGTH     1121
#define IDS_LABEL_ACCENT_COLOR      1122
#define IDS_LABEL_CUSTOM_COLOR      1123
#define IDS_LABEL_RESET_DEFAULTS    1124
#define IDS_LABEL_PREVIEW           1125
