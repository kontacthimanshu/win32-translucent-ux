#include <te/a11y/HwndAnnotation.h>

#include <oleacc.h>

#include <wil/com.h>
#include <wil/result.h>

#include <iterator>

namespace te
{

namespace
{

// UI Automation property GUIDs for Dynamic Annotation (UIAutomationCoreApi.h:
// Name_Property_GUID, AutomationId_Property_GUID), and the AccPropServices class
// (oleacc.h). Written out here, so no INITGUID translation unit or oleacc.lib is needed.
constexpr GUID kNamePropertyGuid{
    0xc3a6921b, 0x4a99, 0x44f1, {0xbc, 0xa6, 0x61, 0x18, 0x70, 0x52, 0xc4, 0x31}};
constexpr GUID kAutomationIdPropertyGuid{
    0xc82c0500, 0xb60e, 0x4310, {0xa2, 0x67, 0x30, 0x3c, 0x53, 0x1f, 0x8e, 0xe5}};
constexpr CLSID kAccPropServices{
    0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};

} // namespace

void AnnotateHwnd(HWND control, const std::wstring& name, const std::wstring& automationId)
{
    if (!control)
    {
        return;
    }
    const auto services = wil::CoCreateInstanceNoThrow<IAccPropServices>(kAccPropServices);
    if (!services)
    {
        LOG_HR_MSG(E_NOINTERFACE, "IAccPropServices unavailable; the control keeps its default name");
        return;
    }
    LOG_IF_FAILED(services->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                           kNamePropertyGuid, name.c_str()));
    LOG_IF_FAILED(services->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                           kAutomationIdPropertyGuid, automationId.c_str()));
}

void ClearHwndAnnotation(HWND control)
{
    if (!control)
    {
        return;
    }
    if (const auto services = wil::CoCreateInstanceNoThrow<IAccPropServices>(kAccPropServices))
    {
        const MSAAPROPID props[] = {kNamePropertyGuid, kAutomationIdPropertyGuid};
        (void)services->ClearHwndProps(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props,
                                       static_cast<int>(std::size(props)));
    }
}

} // namespace te
