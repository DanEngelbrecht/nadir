#include "../src/nadir.h"

#include <memory>
#include <stdio.h>
#include <assert.h>

#include "../third-party/jctest/src/jc_test.h"

#define ALIGN_SIZE(x, align) (((x) + ((align)-1)) & ~((align)-1))

TEST(Nadir, ConditionVariable)
{
    nadir::HNonReentrantLock  lock               = nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize()));
    nadir::HConditionVariable condition_variable = nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock);
    ASSERT_NE((nadir::HConditionVariable)0x0, condition_variable);
    ASSERT_TRUE(!nadir::SleepConditionVariable(condition_variable, 100));
    nadir::WakeOne(condition_variable);
    nadir::DeleteConditionVariable(condition_variable);
    nadir::DeleteNonReentrantLock(lock);
    free(condition_variable);
    free(lock);
}

struct ThreadContext
{
    ThreadContext()
        : stop(0)
        , count(0)
        , condition_variable(0)
        , thread(0)
    {
    }

    ~ThreadContext()
    {
    }

    bool CreateThread(nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
    {
        stop               = in_stop;
        condition_variable = in_condition_variable;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), ThreadContext::Execute, 0, this);
        return thread != 0;
    }

    void DisposeThread()
    {
        nadir::DeleteThread(thread);
        free(thread);
    }

    static int32_t Execute(void* context)
    {
        ThreadContext* t = (ThreadContext*)context;
        while (true)
        {
            if (nadir::SleepConditionVariable(t->condition_variable, nadir::TIMEOUT_INFINITE))
            {
                if (nadir::AtomicAdd32(t->stop, -1) >= 0)
                {
                    t->count++;
                    break;
                }
                nadir::AtomicAdd32(t->stop, 1);
            }
        }
        return 0;
    }

    nadir::TAtomic32*         stop;
    uint32_t                  count;
    nadir::HConditionVariable condition_variable;
    nadir::HThread            thread;
};

TEST(Nadir, TestSingleThread)
{
    nadir::TAtomic32         stop = 0;
    nadir::HNonReentrantLock lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())));
    ASSERT_NE((nadir::HNonReentrantLock)0x0, lock);
    nadir::HConditionVariable condition_variable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock));
    ASSERT_NE((nadir::HConditionVariable)0x0, condition_variable);

    ThreadContext thread_context;

    ASSERT_TRUE(thread_context.CreateThread(condition_variable, &stop));

    ASSERT_TRUE(!nadir::JoinThread(thread_context.thread, 1000));
    nadir::AtomicAdd32(&stop, 1);
    nadir::WakeOne(condition_variable);
    ASSERT_TRUE(nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE));

    thread_context.DisposeThread();

    nadir::DeleteConditionVariable(condition_variable);
    free(condition_variable);
    nadir::DeleteNonReentrantLock(lock);
    free(lock);
}

TEST(Nadir, TestManyThreads)
{
    nadir::TAtomic32         stop = 0;
    nadir::HNonReentrantLock lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())));
    ASSERT_NE((nadir::HNonReentrantLock)0x0, lock);
    nadir::HConditionVariable condition_variable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock));
    ASSERT_NE((nadir::HConditionVariable)0x0, condition_variable);

    static const uint32_t THREAD_COUNT = 16;

    ThreadContext thread_context[THREAD_COUNT];

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        ASSERT_TRUE(thread_context[i].CreateThread(condition_variable, &stop));
    }

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        ASSERT_TRUE(!nadir::JoinThread(thread_context[i].thread, 1000));
    }

    nadir::AtomicAdd32(&stop, 1);
    nadir::WakeOne(condition_variable);
    nadir::Sleep(1000);

    uint32_t awoken = 0;
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        if (nadir::JoinThread(thread_context[i].thread, 1000))
        {
            ++awoken;
        }
    }
    ASSERT_TRUE(awoken == 1);

    nadir::AtomicAdd32(&stop, 1);
    nadir::WakeOne(condition_variable);
    nadir::Sleep(1000);
    awoken = 0;
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        if (nadir::JoinThread(thread_context[i].thread, 1000))
        {
            ++awoken;
        }
    }
    ASSERT_TRUE(awoken == 2);

    nadir::WakeAll(condition_variable);

    nadir::AtomicAdd32(&stop, THREAD_COUNT - awoken);
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        if (nadir::JoinThread(thread_context[i].thread, 1000))
        {
            ++awoken;
        }
    }

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        thread_context[i].DisposeThread();
    }

    nadir::DeleteConditionVariable(condition_variable);
    free(condition_variable);
    nadir::DeleteNonReentrantLock(lock);
    free(lock);
}

TEST(Nadir, TestSpinLock)
{
    nadir::HSpinLock spin_lock = nadir::CreateSpinLock(malloc(nadir::GetSpinLockSize()));
    ASSERT_TRUE(spin_lock != 0);
    nadir::LockSpinLock(spin_lock);
    nadir::UnlockSpinLock(spin_lock);
    nadir::DeleteSpinLock(spin_lock);
    free(spin_lock);
}

TEST(Nadir, TestAtomicFilo)
{
    nadir::TAtomic32 link_array[16];
    link_array[0] = 0;
    nadir::TAtomic32 generation = 0;
    Push(&generation, link_array, 1);
    ASSERT_EQ(1, Pop(link_array));
    ASSERT_EQ(0, Pop(link_array));

    Push(&generation, link_array, 1);
    Push(&generation, link_array, 2);
    Push(&generation, link_array, 3);
    ASSERT_EQ(3, Pop(link_array));
    ASSERT_EQ(2, Pop(link_array));
    Push(&generation, link_array, 2);
    Push(&generation, link_array, 3);
    ASSERT_EQ(3, Pop(link_array));
    ASSERT_EQ(2, Pop(link_array));
    ASSERT_EQ(1, Pop(link_array));
    ASSERT_EQ(0, Pop(link_array));
}

TEST(Nadir, TestAtomicFiloThreads)
{
    static const uint16_t ENTRY_COUNT = 127;
    nadir::TAtomic32 link_array[ENTRY_COUNT + 1];
    nadir::TAtomic32 data_array[ENTRY_COUNT];
    link_array[0] = 0;
    nadir::TAtomic32 generation = 0;
    nadir::TAtomic32 insert_count = 1;

    struct FiloThread
    {
        static int32_t Execute(void* context)
        {
            FiloThread* t = (FiloThread*)context;
            while((*t->m_InsertCount) >= 0)
            {
                uint16_t index = Pop(t->m_LinkArray);
                if (index != 0)
                {
                    int32_t new_value = nadir::AtomicAdd32(&t->m_DataArray[index - 1], 1);
                    if (new_value == 1)
                    {
                        Push(t->m_Generation, t->m_LinkArray, index);
                    }
                }
            }
            return 0;
        }
        nadir::TAtomic32* m_Generation;
        nadir::TAtomic32* m_LinkArray;
        nadir::TAtomic32* m_DataArray;
        nadir::TAtomic32* m_InsertCount;
        nadir::HThread m_Thread;
    };

    for (uint32_t i = 0; i < ENTRY_COUNT; ++i)
    {
        data_array[i] = 0;
    }

    FiloThread threads[128];
    for (uint32_t i = 0; i < 128; ++i)
    {
        threads[i].m_Generation = &generation;
        threads[i].m_LinkArray = link_array;
        threads[i].m_DataArray = data_array;
        threads[i].m_InsertCount = &insert_count;
        threads[i].m_Thread = nadir::CreateThread(malloc(nadir::GetThreadSize()), FiloThread::Execute, 0, &threads[i]);
    }

    for (uint16_t i = 1; i <= ENTRY_COUNT; ++i)
    {
        Push(&generation, link_array, i);
    }

    uint32_t touched_count = 0;
    do
    {
        touched_count = 0;
        for (uint16_t i = 0; i < ENTRY_COUNT; ++i)
        {
            if (data_array[i] == 2)
            {
                ++touched_count;
            }
        }
    } while (touched_count != ENTRY_COUNT);
    nadir::AtomicAdd32(&insert_count, -1);

    for (uint32_t i = 0; i < 128; ++i)
    {
        nadir::JoinThread(threads[i].m_Thread, nadir::TIMEOUT_INFINITE);
    }

    for (uint32_t i = 0; i < 128; ++i)
    {
        nadir::DeleteThread(threads[i].m_Thread);
    }

    for (uint32_t i = 0; i < 128; ++i)
    {
        free(threads[i].m_Thread);
    }
}
