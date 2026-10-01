#include <te/core/ComInit.h>

#include <windows.h>

#include <ole2.h>

#include <wil/result.h>

namespace te
{

// S_FALSE ("already initialized on this thread") still counts as a successful call
// that must be balanced by an uninitialize, so only failures throw.

OleScope::OleScope()
{
    THROW_IF_FAILED(::OleInitialize(nullptr));
}

OleScope::~OleScope()
{
    ::OleUninitialize();
}

StaScope::StaScope()
{
    THROW_IF_FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
}

StaScope::~StaScope()
{
    ::CoUninitialize();
}

} // namespace te
