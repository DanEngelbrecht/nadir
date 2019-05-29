#include "nadir.h"

#if defined(_WIN32)

#    include <Windows.h>

namespace nadir {

struct Thread
{
    HANDLE     m_Handle;
    ThreadFunc m_ThreadFunc;
    void*      m_ContextData;
};

struct NonReentrantLock
{
    CRITICAL_SECTION m_CriticalSection;
};

struct ConditionVariable
{
    HNonReentrantLock  m_NonReentrantLock;
    CONDITION_VARIABLE m_ConditionVariable;
};

static DWORD WINAPI ThreadStartFunction(_In_ LPVOID lpParameter)
{
    Thread* thread = (Thread*)lpParameter;
    int     result = thread->m_ThreadFunc(thread->m_ContextData);
    return (DWORD)result;
}

size_t GetThreadSize()
{
    return sizeof(Thread);
}

HThread CreateThread(void* mem, ThreadFunc thread_func, uint32_t stack_size, void* context_data)
{
    Thread* thread        = (Thread*)mem;
    thread->m_ThreadFunc  = thread_func;
    thread->m_ContextData = context_data;
    thread->m_Handle      = ::CreateThread(
    0,
    stack_size,
    ThreadStartFunction,
    thread,
    0,
    0);
    if (thread->m_Handle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    return thread;
}

bool JoinThread(HThread thread, uint64_t timeout_us)
{
    if (thread->m_Handle == 0)
    {
        return true;
    }
    DWORD wait_ms = (timeout_us == TIMEOUT_INFINITE) ? INFINITE : (DWORD)(timeout_us / 1000);
    DWORD result  = ::WaitForSingleObject(thread->m_Handle, wait_ms);
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
    return ::InterlockedAdd((LONG volatile*)value, amount);
}

int32_t AtomicCAS32(TAtomic32* store, int32_t compare, int32_t value)
{
    return (int32_t)::InterlockedCompareExchangeAcquire((LONG volatile*)store, (LONG)value, (LONG)compare);
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
    HConditionVariable condition_variable  = (HConditionVariable)mem;
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
    DWORD wait_ms    = timeout_us == TIMEOUT_INFINITE ? INFINITE : (DWORD)(timeout_us / 1000);
    BOOL  was_awoken = ::SleepConditionVariableCS(&condition_variable->m_ConditionVariable, &condition_variable->m_NonReentrantLock->m_CriticalSection, wait_ms);
    UnlockNonReentrantLock(condition_variable->m_NonReentrantLock);
    return was_awoken == TRUE;
}

void DeleteConditionVariable(HConditionVariable)
{
}

struct SpinLock
{
    SRWLOCK m_Lock;
};

size_t GetSpinLockSize()
{
    return sizeof(SpinLock);
}

HSpinLock CreateSpinLock(void* mem)
{
    HSpinLock spin_lock = (HSpinLock)mem;
    ::InitializeSRWLock(&spin_lock->m_Lock);
    return spin_lock;
}

void DeleteSpinLock(HSpinLock)
{
}

void LockSpinLock(HSpinLock spin_lock)
{
    ::AcquireSRWLockExclusive(&spin_lock->m_Lock);
}

void UnlockSpinLock(HSpinLock spin_lock)
{
    ::ReleaseSRWLockExclusive(&spin_lock->m_Lock);
}

} // namespace nadir

#else

#    include <pthread.h>
#    include <unistd.h>

#    ifdef __APPLE__
#        include <os/lock.h>
#    endif // __APPLE__

#    define ALIGN_SIZE(x, align) (((x) + ((align)-1)) & ~((align)-1))

namespace nadir {
struct Thread
{
    pthread_t          m_Handle;
    ThreadFunc         m_ThreadFunc;
    void*              m_ContextData;
    HNonReentrantLock  m_ExitLock;
    HConditionVariable m_ExitConditionalVariable;
    bool               m_Exited;
};

struct NonReentrantLock
{
    pthread_mutex_t m_Mutex;
};

struct ConditionVariable
{
    HNonReentrantLock m_NonReentrantLock;
    pthread_cond_t    m_ConditionVariable;
};

static void* ThreadStartFunction(void* data)
{
    Thread* thread = (Thread*)data;
    (void)thread->m_ThreadFunc(thread->m_ContextData);
    LockNonReentrantLock(thread->m_ExitLock);
    thread->m_Exited = true;
    WakeAll(thread->m_ExitConditionalVariable);
    UnlockNonReentrantLock(thread->m_ExitLock);
    return 0;
}

size_t GetThreadSize()
{
    return ALIGN_SIZE((uint32_t)sizeof(Thread), 8u) + ALIGN_SIZE((uint32_t)GetNonReentrantLockSize(), 8u) + ALIGN_SIZE((uint32_t)GetConditionVariableSize(), 8u);
}

HThread CreateThread(void* mem, ThreadFunc thread_func, uint32_t stack_size, void* context_data)
{
    Thread* thread        = (Thread*)mem;
    thread->m_ThreadFunc  = thread_func;
    thread->m_ContextData = context_data;

    uint8_t* p                        = (uint8_t*)mem;
    thread->m_ExitLock                = CreateLock(&p[ALIGN_SIZE((uint32_t)sizeof(Thread), 8u)]);
    thread->m_ExitConditionalVariable = CreateConditionVariable(&p[ALIGN_SIZE((uint32_t)sizeof(Thread), 8u) + ALIGN_SIZE((uint32_t)GetNonReentrantLockSize(), 8u)], thread->m_ExitLock);
    thread->m_Exited                  = false;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_size != 0)
    {
        pthread_attr_setstacksize(&attr, stack_size);
    }
    int result = pthread_create(&thread->m_Handle, &attr, ThreadStartFunction, (void*)thread);
    pthread_attr_destroy(&attr);
    if (result != 0)
    {
        return 0;
    }
    return thread;
}

static bool GetTimeSpec(timespec* ts, uint64_t delay_us)
{
    if (clock_gettime(CLOCK_REALTIME, ts) == -1)
    {
        return false;
    }
    uint64_t end_ns = (uint64_t)(ts->tv_nsec) + (delay_us * 1000u);
    uint64_t wait_s = end_ns / 1000000000u;
    ts->tv_sec += wait_s;
    ts->tv_nsec = (long)(end_ns - wait_s * 1000000000u);
    return true;
}

bool JoinThread(HThread thread, uint64_t timeout_us)
{
    if (thread->m_Handle == 0)
    {
        return true;
    }
    if (timeout_us == TIMEOUT_INFINITE)
    {
        int result = ::pthread_join(thread->m_Handle, 0);
        if (result == 0)
        {
            thread->m_Handle = 0;
        }
        return result == 0;
    }
    struct timespec ts;
    if (!GetTimeSpec(&ts, timeout_us))
    {
        return false;
    }
    LockNonReentrantLock(thread->m_ExitLock);
    while (!thread->m_Exited)
    {
        if (0 != pthread_cond_timedwait(&thread->m_ExitConditionalVariable->m_ConditionVariable, &thread->m_ExitLock->m_Mutex, &ts))
        {
            break;
        }
    }
    bool exited = thread->m_Exited;
    UnlockNonReentrantLock(thread->m_ExitLock);
    if (!exited)
    {
        return false;
    }
    int result = ::pthread_join(thread->m_Handle, 0);
    if (result == 0)
    {
        thread->m_Handle = 0;
    }
    return result == 0;
}

void DeleteThread(HThread thread)
{
    DeleteConditionVariable(thread->m_ExitConditionalVariable);
    DeleteNonReentrantLock(thread->m_ExitLock);
    thread->m_Handle = 0;
}

void Sleep(uint64_t timeout_us)
{
    ::usleep((useconds_t)timeout_us);
}

int32_t AtomicAdd32(TAtomic32* value, int32_t amount)
{
    return __sync_fetch_and_add(value, amount) + amount;
}

int32_t AtomicCAS32(TAtomic32* store, int32_t compare, int32_t value)
{
    return __sync_val_compare_and_swap(store, compare, value);
}

size_t GetNonReentrantLockSize()
{
    return sizeof(NonReentrantLock);
}

HNonReentrantLock CreateLock(void* mem)
{
    HNonReentrantLock lock = (HNonReentrantLock)mem;
    if (0 != pthread_mutex_init(&lock->m_Mutex, 0))
    {
        return 0;
    }
    return lock;
}

void LockNonReentrantLock(HNonReentrantLock lock)
{
    pthread_mutex_lock(&lock->m_Mutex);
}

void UnlockNonReentrantLock(HNonReentrantLock lock)
{
    pthread_mutex_unlock(&lock->m_Mutex);
}

void DeleteNonReentrantLock(HNonReentrantLock lock)
{
    pthread_mutex_destroy(&lock->m_Mutex);
}

size_t GetConditionVariableSize()
{
    return sizeof(ConditionVariable);
}

HConditionVariable CreateConditionVariable(void* mem, HNonReentrantLock lock)
{
    HConditionVariable condition_variable  = (HConditionVariable)mem;
    condition_variable->m_NonReentrantLock = lock;
    if (0 != pthread_cond_init(&condition_variable->m_ConditionVariable, 0))
    {
        return 0;
    }
    return condition_variable;
}

void WakeOne(HConditionVariable condition_variable)
{
    LockNonReentrantLock(condition_variable->m_NonReentrantLock);
    pthread_cond_signal(&condition_variable->m_ConditionVariable);
    UnlockNonReentrantLock(condition_variable->m_NonReentrantLock);
}

void WakeAll(HConditionVariable condition_variable)
{
    pthread_cond_broadcast(&condition_variable->m_ConditionVariable);
}

bool SleepConditionVariable(HConditionVariable condition_variable, uint64_t timeout_us)
{
    if (timeout_us == TIMEOUT_INFINITE)
    {
        LockNonReentrantLock(condition_variable->m_NonReentrantLock);
        bool was_awoken = 0 == pthread_cond_wait(&condition_variable->m_ConditionVariable, &condition_variable->m_NonReentrantLock->m_Mutex);
        UnlockNonReentrantLock(condition_variable->m_NonReentrantLock);
        return was_awoken;
    }
    struct timespec ts;
    if (!GetTimeSpec(&ts, timeout_us))
    {
        return false;
    }
    LockNonReentrantLock(condition_variable->m_NonReentrantLock);
    bool was_awoken = 0 == pthread_cond_timedwait(&condition_variable->m_ConditionVariable, &condition_variable->m_NonReentrantLock->m_Mutex, &ts);
    UnlockNonReentrantLock(condition_variable->m_NonReentrantLock);
    return was_awoken;
}

void DeleteConditionVariable(HConditionVariable condition_variable)
{
    pthread_cond_destroy(&condition_variable->m_ConditionVariable);
}

#    ifdef __APPLE__

struct SpinLock
{
    os_unfair_lock m_Lock;
};

size_t GetSpinLockSize()
{
    return sizeof(SpinLock);
}

HSpinLock CreateSpinLock(void* mem)
{
    HSpinLock spin_lock                         = (HSpinLock)mem;
    spin_lock->m_Lock._os_unfair_lock_opaque    = 0;
    return spin_lock;
}

void DeleteSpinLock(HSpinLock)
{
}

void LockSpinLock(HSpinLock spin_lock)
{
    os_unfair_lock_lock(&spin_lock->m_Lock);
}

void UnlockSpinLock(HSpinLock spin_lock)
{
    os_unfair_lock_unlock(&spin_lock->m_Lock);
}

#    else

struct SpinLock
{
    pthread_spinlock_t m_Lock;
};

size_t GetSpinLockSize()
{
    return sizeof(SpinLock);
}

HSpinLock CreateSpinLock(void* mem)
{
    HSpinLock spin_lock = (HSpinLock)mem;
    if (0 != pthread_spin_init(&spin_lock->m_Lock, 0))
    {
        return 0;
    }
    return spin_lock;
}

void DeleteSpinLock(HSpinLock)
{
}

void LockSpinLock(HSpinLock spin_lock)
{
    pthread_spin_lock(&spin_lock->m_Lock);
}

void UnlockSpinLock(HSpinLock spin_lock)
{
    pthread_spin_unlock(&spin_lock->m_Lock);
}
#    endif

} // namespace nadir

#endif
