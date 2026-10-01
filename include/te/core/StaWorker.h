#pragma once

// A worker thread with its own single-threaded COM apartment and message pump
// (research R-07). Used for folder enumeration, icon extraction and IFileOperation,
// all of which require an STA.

#include <windows.h>

#include <wil/resource.h>

#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

namespace te
{

class StaWorker
{
  public:
    // A unit of work. It runs on the worker thread and should check the token
    // between steps so it can stop early (cooperative cancellation).
    using Task = std::function<void(std::stop_token)>;

    StaWorker();
    // Requests stop and joins. Tasks still queued are destroyed without running.
    ~StaWorker();

    StaWorker(const StaWorker&) = delete;
    StaWorker& operator=(const StaWorker&) = delete;
    StaWorker(StaWorker&&) = delete;
    StaWorker& operator=(StaWorker&&) = delete;

    // Thread-safe. Appends to the back of the queue (FIFO).
    void Post(Task task);
    // Thread-safe. Inserts at the front of the queue, so the newest request runs
    // next (LIFO use, e.g. icons for the rows currently visible).
    void PostFront(Task task);

    // Thread-safe, non-blocking. The running task sees its stop token set; no
    // further tasks start.
    void RequestStop();
    // Waits for the thread to exit. Does nothing if already joined or if called
    // from the worker thread itself.
    void Join();

    // Like Join, but while waiting the calling STA keeps serving incoming COM calls
    // (CoWaitForMultipleHandles). Needed when a running task calls back into an object
    // that lives in the caller's apartment (a pasted IDataObject); a plain Join would
    // deadlock. Falls back to Join on a thread without COM.
    void JoinServingComCalls();

    [[nodiscard]] bool StopRequested() const noexcept;

  private:
    void Run(std::stop_token stop);
    bool TryPop(Task& task);

    std::mutex m_mutex;
    std::deque<Task> m_tasks; // guarded by m_mutex
    wil::unique_event m_wake; // auto-reset: new task or stop request
    std::jthread m_thread;    // declared last: starts after the members above
};

} // namespace te
