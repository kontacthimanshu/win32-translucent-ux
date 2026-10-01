#pragma once

// Names a native Win32 control for UI Automation with Dynamic Annotation
// (IAccPropServices::SetHwndPropStr, T083): UIA merges the annotated Name and
// AutomationId into the control's own provider, which keeps its patterns. Used for the
// native EDITs (address while editing, inline rename). UI thread with COM initialized.

#include <windows.h>

#include <string>

namespace te
{

// Sets the control's UIA Name and AutomationId. Failures are logged; the control then
// keeps its default properties.
void AnnotateHwnd(HWND control, const std::wstring& name, const std::wstring& automationId);

// Removes what AnnotateHwnd set. Call before the control is destroyed (WM_NCDESTROY).
void ClearHwndAnnotation(HWND control);

} // namespace te
