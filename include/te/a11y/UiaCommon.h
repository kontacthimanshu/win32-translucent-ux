#pragma once

// Small helpers shared by the UI Automation providers (T075–T078).

#include <objbase.h>

#include <UIAutomation.h>

#include <initializer_list>
#include <iterator>
#include <string>

namespace te::uia
{

// UIA_E_ELEMENTNOTAVAILABLE is a plain integer literal; wil wants an HRESULT.
inline constexpr HRESULT kNotAvailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
inline constexpr HRESULT kInvalidOperation = static_cast<HRESULT>(UIA_E_INVALIDOPERATION);

inline void SetBool(VARIANT* value, bool flag)
{
    value->vt = VT_BOOL;
    value->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
}

inline void SetString(VARIANT* value, const std::wstring& text)
{
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(text.c_str());
}

inline void SetInt(VARIANT* value, int number)
{
    value->vt = VT_I4;
    value->lVal = number;
}

// A runtime ID {UiaAppendRuntimeId, parts...}, unique among the window's fragments.
inline HRESULT MakeRuntimeId(std::initializer_list<int> parts, SAFEARRAY** result)
{
    *result = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(parts.size() + 1));
    if (!*result)
    {
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    int append = UiaAppendRuntimeId;
    HRESULT hr = SafeArrayPutElement(*result, &index, &append);
    for (int part : parts)
    {
        ++index;
        if (SUCCEEDED(hr))
        {
            hr = SafeArrayPutElement(*result, &index, &part);
        }
    }
    if (FAILED(hr))
    {
        SafeArrayDestroy(*result);
        *result = nullptr;
    }
    return hr;
}

// A SAFEARRAY of VT_UNKNOWN from providers (for GetSelection, GetColumnHeaders).
template <class Container> HRESULT MakeProviderArray(const Container& providers, SAFEARRAY** result)
{
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(std::size(providers)));
    if (!*result)
    {
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    for (const auto& provider : providers)
    {
        IUnknown* unknown = provider.Get();
        const HRESULT hr = SafeArrayPutElement(*result, &index, unknown); // AddRefs
        if (FAILED(hr))
        {
            SafeArrayDestroy(*result);
            *result = nullptr;
            return hr;
        }
        ++index;
    }
    return S_OK;
}

inline bool Contains(const UiaRect& rect, double x, double y)
{
    return x >= rect.left && y >= rect.top && x < rect.left + rect.width && y < rect.top + rect.height;
}

} // namespace te::uia
