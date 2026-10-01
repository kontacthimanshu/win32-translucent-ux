#pragma once

// RAII scopes for COM apartment initialization (constitution Principle VIII, research R-07).
//
// Each scope initializes COM for the calling thread in its constructor and
// uninitializes it in its destructor, so it must be created and destroyed on the
// same thread. Both throw wil::ResultException if initialization fails, for example
// RPC_E_CHANGED_MODE when the thread is already in a different apartment.

namespace te
{

// OleInitialize / OleUninitialize: a single-threaded apartment plus OLE services
// (clipboard, drag and drop). Used by the UI thread.
class OleScope
{
  public:
    OleScope();
    ~OleScope();

    OleScope(const OleScope&) = delete;
    OleScope& operator=(const OleScope&) = delete;
    OleScope(OleScope&&) = delete;
    OleScope& operator=(OleScope&&) = delete;
};

// CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) / CoUninitialize.
// Used by the STA worker threads (enumeration, icons, IFileOperation).
class StaScope
{
  public:
    StaScope();
    ~StaScope();

    StaScope(const StaScope&) = delete;
    StaScope& operator=(const StaScope&) = delete;
    StaScope(StaScope&&) = delete;
    StaScope& operator=(StaScope&&) = delete;
};

} // namespace te
