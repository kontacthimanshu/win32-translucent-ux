#include <te/core/StaWorker.h>

#include <te/core/ComInit.h>

#include <combaseapi.h>

#include <wil/result.h>

#include <exception>
#include <utility>

namespace te
{

StaWorker::StaWorker() : m_wake(wil::EventOptions::None)
{
    m_thread = std::jthread([this](std::stop_token stop) { Run(std::move(stop)); });
}

StaWorker::~StaWorker()
{
    RequestStop();
    Join();
}

void StaWorker::Post(Task task)
{
    {
        std::scoped_lock lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }
    m_wake.SetEvent();
}

void StaWorker::PostFront(Task task)
{
    {
        std::scoped_lock lock(m_mutex);
        m_tasks.push_front(std::move(task));
    }
    m_wake.SetEvent();
}

void StaWorker::RequestStop()
{
    // The stop_callback registered in Run() wakes the thread.
    m_thread.request_stop();
}

void StaWorker::Join()
{
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
    {
        m_thread.join();
    }
}

void StaWorker::JoinServingComCalls()
{
    if (!m_thread.joinable() || m_thread.get_id() == std::this_thread::get_id())
    {
        return;
    }
    HANDLE thread = m_thread.native_handle();
    DWORD index = 0;
    const HRESULT hr = CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS, INFINITE, 1, &thread, &index);
    LOG_HR_IF(hr, FAILED(hr) && hr != CO_E_NOTINITIALIZED); // no COM on this thread: plain join
    m_thread.join();
}

bool StaWorker::StopRequested() const noexcept
{
    return m_thread.get_stop_token().stop_requested();
}

bool StaWorker::TryPop(Task& task)
{
    std::scoped_lock lock(m_mutex);
    if (m_tasks.empty())
    {
        return false;
    }
    task = std::move(m_tasks.front());
    m_tasks.pop_front();
    return true;
}

void StaWorker::Run(std::stop_token stop)
{
    // Wake the wait below whenever a stop is requested, from any thread,
    // including the jthread destructor.
    const std::stop_callback wakeOnStop(stop, [this] { m_wake.SetEvent(); });

    try
    {
        const StaScope sta;

        while (!stop.stop_requested())
        {
            // Pump window messages for windows created on this thread (for
            // example the Shell's IFileOperation progress UI) and for COM calls
            // marshalled into this apartment.
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            Task task;
            if (TryPop(task))
            {
                try
                {
                    task(stop);
                }
                catch (...)
                {
                    // A failing task must not take the worker down with it.
                    LOG_CAUGHT_EXCEPTION();
                }
                continue;
            }

            const HANDLE handles[] = {m_wake.get()};
            MsgWaitForMultipleObjectsEx(1, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    catch (...)
    {
        // COM initialization failed; the thread exits and queued tasks are
        // destroyed with the worker.
        LOG_CAUGHT_EXCEPTION();
    }
}

} // namespace te
