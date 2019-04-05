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

    ASSERT_TRUE(!nadir::JoinThread(thread_context.thread, 2000));
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

    nadir::AtomicAdd32(&stop, THREAD_COUNT - awoken);
    nadir::WakeAll(condition_variable);

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

static const uint32_t GENERATION_BITS = 9;
static const uint32_t GENERATION_MASK = ((1u << GENERATION_BITS) -1);
static const uint32_t INDEX_BITS      = ((sizeof(uint32_t) * 8) - (GENERATION_BITS + 1));   // We skip one bit between gen and index so 0xffffffff will never be a valid
static const uint32_t INDEX_MASK      = ((1u << INDEX_BITS) - 1);
static const uint32_t INVALID_ENTRY   = (1u << (INDEX_BITS + 1));

static void Init(nadir::TAtomic32* generation, nadir::TAtomic32* link_array, const uint32_t entry_count)
{
    *generation = 0;
    link_array[0] = 0;
    for (uint32_t i = 1; i <= entry_count; ++i)
    {
        link_array[i] = (long)INVALID_ENTRY;
    }
}

static uint32_t NextGeneration(nadir::TAtomic32* generation)
{
    uint32_t gen = (uint32_t)nadir::AtomicAdd32(generation, 1);
    return gen & GENERATION_MASK;
}

static void Push(nadir::TAtomic32* generation, nadir::TAtomic32* link_array, uint32_t index)
{
    assert(index > 0);
    assert(index <= INDEX_MASK);
    assert(((uint32_t)link_array[index]) == INVALID_ENTRY);

    uint32_t gen = NextGeneration(generation);

    int32_t new_head = (int32_t)((gen << INDEX_BITS) + index);

    int32_t current_head = link_array[0];
    link_array[index] = current_head;

    while (!nadir::AtomicCAS32(&link_array[0], current_head, new_head))
    {
        current_head = link_array[0];
        link_array[index] = current_head;
    }
}

static uint32_t Pop(nadir::TAtomic32* link_array)
{
    while(true)
    {
        int32_t current_head = link_array[0];
        uint32_t head_index = ((uint32_t)(current_head) & INDEX_MASK);
        if (head_index == 0)
        {
            return 0;
        }
        assert(head_index <= INDEX_MASK);
        int32_t next = link_array[head_index];
        if(next == (long)INVALID_ENTRY)
        {
            // We have a stale head, try again
            continue;
        }

        if (nadir::AtomicCAS32(&link_array[0], current_head, next))
        {
            link_array[head_index] = (long)INVALID_ENTRY;
            return head_index;
        }
    }
}

TEST(Nadir, TestAtomicFilo)
{
    const uint32_t entry_count = 16;
    nadir::TAtomic32 generation = 0;
    nadir::TAtomic32 link_array[entry_count + 1];
    Init(&generation, link_array, entry_count);
    Push(&generation, link_array, 1);
    ASSERT_EQ(1u, Pop(link_array));
    ASSERT_EQ(0u, Pop(link_array));

    Push(&generation, link_array, 1);
    Push(&generation, link_array, 2);
    Push(&generation, link_array, 3);
    ASSERT_EQ(3u, Pop(link_array));
    ASSERT_EQ(2u, Pop(link_array));
    Push(&generation, link_array, 2);
    Push(&generation, link_array, 3);
    ASSERT_EQ(3u, Pop(link_array));
    ASSERT_EQ(2u, Pop(link_array));
    ASSERT_EQ(1u, Pop(link_array));
    ASSERT_EQ(0u, Pop(link_array));
}

TEST(Nadir, TestAtomicFiloThreads)
{
    #define ENTRY_BREAK_COUNT 751
    static const uint32_t ENTRY_COUNT = 3912;
    nadir::TAtomic32 generation = 0;
    nadir::TAtomic32 link_array[ENTRY_COUNT + 1];
    Init(&generation, link_array, ENTRY_COUNT);
    struct Data
    {
        Data()
         : m_Busy(0)
         , m_Counter(0)
        {
        }
        nadir::TAtomic32 m_Busy;
        nadir::TAtomic32 m_Counter;
    };
    Data data_array[ENTRY_COUNT];
    nadir::TAtomic32 insert_count = 1;

    struct FiloThread
    {
        static int32_t Execute(void* context)
        {
            uint32_t fail_get_count = 0;
            FiloThread* t = (FiloThread*)context;
            while((*t->m_InsertCount) > 0)
            {
                uint32_t index = Pop(t->m_LinkArray);
                assert(index <= t->m_EntryCount);
                if (index != 0)
                {
                    fail_get_count = 0;
                    long busy_counter = nadir::AtomicAdd32(&t->m_DataArray[index - 1].m_Busy, 1);
                    assert(1 == busy_counter);
                    int32_t new_value = nadir::AtomicAdd32(&t->m_DataArray[index - 1].m_Counter, 1);
                    if (new_value < ENTRY_BREAK_COUNT)
                    {
                        busy_counter = nadir::AtomicAdd32(&t->m_DataArray[index - 1].m_Busy, -1);
                        assert(0 == busy_counter);
                        Push(t->m_Generation, t->m_LinkArray, index);
                    }
                    else
                    {
                        assert(new_value == ENTRY_BREAK_COUNT);
                    }
                }
                else if (++fail_get_count > 50)
                {
                    nadir::Sleep(1000);
                    fail_get_count = 0;
                }
            }
            return 0;
        }
        uint32_t m_EntryCount;
        nadir::TAtomic32* m_Generation;
        nadir::TAtomic32* m_LinkArray;
        Data* m_DataArray;
        nadir::TAtomic32* m_InsertCount;
        nadir::HThread m_Thread;
    };

    static const uint32_t THREAD_COUNT = 128;
    FiloThread threads[THREAD_COUNT];
    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        threads[i].m_EntryCount = ENTRY_COUNT;
        threads[i].m_Generation = &generation;
        threads[i].m_LinkArray = link_array;
        threads[i].m_DataArray = data_array;
        threads[i].m_InsertCount = &insert_count;
        threads[i].m_Thread = nadir::CreateThread(malloc(nadir::GetThreadSize()), FiloThread::Execute, 0, &threads[i]);
    }

    for (uint32_t i = 1; i <= ENTRY_COUNT; ++i)
    {
        Push(&generation, link_array, i);
    }

    uint32_t untouched_count = 0;
    uint32_t touched_count = 0;
    for (uint32_t times = 0; times < (uint32_t)ENTRY_COUNT * 100u; ++times)
    {
        touched_count = 0;
        untouched_count = 0;
        for (uint32_t i = 0; i < ENTRY_COUNT; ++i)
        {
            if (data_array[i].m_Counter == ENTRY_BREAK_COUNT)
            {
                ++touched_count;
            }
            else
            {
                ++untouched_count;
            }
        }
        if (touched_count == ENTRY_COUNT)
        {
            nadir::AtomicAdd32(&insert_count, -1);
            break;
        }
        nadir::Sleep(1000);
    }
    ASSERT_EQ(touched_count, ENTRY_COUNT);
    ASSERT_EQ(untouched_count, 0u);

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        nadir::JoinThread(threads[i].m_Thread, nadir::TIMEOUT_INFINITE);
    }

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        nadir::DeleteThread(threads[i].m_Thread);
    }

    for (uint32_t i = 0; i < THREAD_COUNT; ++i)
    {
        free(threads[i].m_Thread);
    }
    #undef ENTRY_BREAK_COUNT
}
