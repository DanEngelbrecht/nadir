#pragma once

#include <stdint.h>

namespace nadir
{
    typedef struct Thread* HThread;

    typedef int32_t (*ThreadFunc)(void* context_data);

    size_t GetThreadSize();
    HThread CreateThread(void* mem, ThreadFunc thread_func, uint32_t stack_size, void* context_data);
    bool JoinThread(HThread thread, uint64_t timeout_us);
    void DeleteThread(HThread thread);

    static const uint64_t TIMEOUT_INFINITE = ((uint64_t)-1);

    typedef struct ConditionVariable* HConditionVariable;
    typedef struct NonReentrantLock* HNonReentrantLock;


    void Sleep(uint64_t timeout_us);
    typedef volatile long TAtomic32;
    int32_t AtomicAdd32(TAtomic32* value, int32_t amount);

    size_t GetNonReentrantLockSize();
    HNonReentrantLock CreateLock(void* mem);
    void LockNonReentrantLock(HNonReentrantLock lock);
    void UnlockNonReentrantLock(HNonReentrantLock lock);
    void DeleteNonReentrantLock(HNonReentrantLock lock);

    size_t GetConditionVariableSize();
    HConditionVariable CreateConditionVariable(void* mem, HNonReentrantLock lock);
    void WakeOne(HConditionVariable conditional_variable);
    void WakeAll(HConditionVariable conditional_variable);
    bool SleepConditionVariable(HConditionVariable conditional_variable, uint64_t timeout_us);
    void DeleteConditionVariable(HConditionVariable conditional_variable);

}
