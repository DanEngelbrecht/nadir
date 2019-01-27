#include "../src/nadir.h"

#include <memory>
#include <stdio.h>
#include <assert.h>

#define ALIGN_SIZE(x, align)    (((x) + ((align) - 1)) & ~((align) - 1))

typedef struct SCtx
{
} SCtx;

static SCtx* main_setup()
{
    return reinterpret_cast<SCtx*>( malloc( sizeof(SCtx) ) );
}

static void main_teardown(SCtx* ctx)
{
    free(ctx);
}

static void test_setup(SCtx* )
{
}

static void test_teardown(SCtx* )
{
}

static void test_condition_variable(SCtx* )
{
    nadir::HNonReentrantLock lock = nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize()));
    nadir::HConditionVariable condition_variable = nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock);
    ASSERT_NE(0x0, condition_variable);
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
        : count(0)
        , stop(0)
        , condition_variable(0)
        , thread(0)
    {}

    ~ThreadContext()
    {
    }

    bool CreateThread(nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
    {
        stop = in_stop;
        condition_variable = in_condition_variable;
        thread = nadir::CreateThread(malloc(nadir::GetThreadSize()), ThreadContext::Execute, 0, this);
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
        while(true)
        {
            if (nadir::SleepConditionVariable(t->condition_variable, nadir::TIMEOUT_INFINITE))
            {
                if (nadir::AtomicAdd32(t->stop, -1) >= 0)
                {
                    t->count ++;
                    break;
                }
                nadir::AtomicAdd32(t->stop, 1);
            }
        }
        return 0;
    }

    nadir::TAtomic32* stop;
    uint32_t count;
    nadir::HConditionVariable condition_variable;
    nadir::HThread thread;
};

static void test_single_thread(SCtx* )
{
    nadir::TAtomic32 stop = 0;
    nadir::HNonReentrantLock lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())));
    ASSERT_NE(0x0, lock);
    nadir::HConditionVariable condition_variable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock));
    ASSERT_NE(0x0, condition_variable);

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

static void test_many_threads(SCtx* )
{
    nadir::TAtomic32 stop = 0;
    nadir::HNonReentrantLock lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())));
    ASSERT_NE(0x0, lock);
    nadir::HConditionVariable condition_variable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), lock));
    ASSERT_NE(0x0, condition_variable);

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

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(test_condition_variable)
    TEST(test_single_thread)
    TEST(test_many_threads)
TEST_END(test)
