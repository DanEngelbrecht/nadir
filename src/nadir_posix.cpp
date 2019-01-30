#include "nadir.h"

#if !defined(_WIN32)

namespace nadir
{
    struct Thread
    {
        HANDLE m_Handle;
        ThreadFunc m_ThreadFunc;
        void* m_ContextData;
    };

    struct NonReentrantLock
    {
        CRITICAL_SECTION m_CriticalSection;
    };

    struct ConditionVariable
    {
        HNonReentrantLock m_NonReentrantLock;
        CONDITION_VARIABLE m_ConditionVariable;
    };

    static DWORD WINAPI ThreadStartFunction(_In_ LPVOID lpParameter)
    {
        Thread* thread = (Thread*)lpParameter;
        int result = thread->m_ThreadFunc(thread->m_ContextData);
        return (DWORD)result;
    }

    size_t GetThreadSize()
    {
        return sizeof(Thread);
    }

    HThread CreateThread(void* mem, ThreadFunc thread_func, uint32_t stack_size, void* context_data)
    {
        Thread* thread = (Thread*)mem;
        thread->m_ThreadFunc = thread_func;
        thread->m_ContextData = context_data;
        thread->m_Handle = ::CreateThread(
            0,
            stack_size,
            ThreadStartFunction,
            thread,
            0,
            0);
        return thread;
    }

    bool JoinThread(HThread thread, uint64_t timeout_us)
    {
        DWORD wait_ms = (timeout_us == TIMEOUT_INFINITE) ? INFINITE : (DWORD)(timeout_us / 1000);
        DWORD result = ::WaitForSingleObject(thread->m_Handle, wait_ms);
        return result == WAIT_OBJECT_0;
    }

    void DeleteThread(HThread thread)
    {
        ::CloseHandle(thread->m_Handle);
        thread->m_Handle = INVALID_HANDLE_VALUE;
    }

    void Sleep(uint64_t timeout_us)
    {
        DWORD wait_ms = timeout_us == TIMEOUT_INFINITE ? INFINITE : (DWORD)(timeout_us / 1000);
        ::Sleep(wait_ms);
    }

    int32_t AtomicAdd32(TAtomic32* value, int32_t amount)
    {
        return ::InterlockedAdd(value, amount);
    }

    size_t GetNonReentrantLockSize()
    {
        return sizeof(NonReentrantLock);
    }

    HNonReentrantLock CreateLock(void* mem)
    {
        HNonReentrantLock lock = (HNonReentrantLock)mem;
        ::InitializeCriticalSection(&lock->m_CriticalSection);
        return lock;
    }

    void LockNonReentrantLock(HNonReentrantLock lock)
    {
        ::EnterCriticalSection(&lock->m_CriticalSection);
    }

    void UnlockNonReentrantLock(HNonReentrantLock lock)
    {
        ::LeaveCriticalSection(&lock->m_CriticalSection);
    }

    void DeleteNonReentrantLock(HNonReentrantLock lock)
    {
        ::DeleteCriticalSection(&lock->m_CriticalSection);
    }

    size_t GetConditionVariableSize()
    {
        return sizeof(ConditionVariable);
    }
    HConditionVariable CreateConditionVariable(void* mem, HNonReentrantLock lock)
    {
        HConditionVariable condition_variable = (HConditionVariable)mem;
        condition_variable->m_NonReentrantLock = lock;
        ::InitializeConditionVariable(&condition_variable->m_ConditionVariable);
        return condition_variable;
    }

    void WakeOne(HConditionVariable condition_variable)
    {
        ::WakeConditionVariable(&condition_variable->m_ConditionVariable);
    }

    void WakeAll(HConditionVariable condition_variable)
    {
        ::WakeAllConditionVariable(&condition_variable->m_ConditionVariable);
    }

    bool SleepConditionVariable(HConditionVariable condition_variable, uint64_t timeout_us)
    {
        LockNonReentrantLock(condition_variable->m_NonReentrantLock);
        DWORD wait_ms = timeout_us == TIMEOUT_INFINITE ? INFINITE : (DWORD)(timeout_us / 1000);
        BOOL was_awoken = ::SleepConditionVariableCS(&condition_variable->m_ConditionVariable, &condition_variable->m_NonReentrantLock->m_CriticalSection, wait_ms);
        UnlockNonReentrantLock(condition_variable->m_NonReentrantLock);
        return was_awoken == TRUE;
    }

    void DeleteConditionVariable(HConditionVariable )
    {
    }
}

#endif
