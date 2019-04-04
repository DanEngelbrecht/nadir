#define JC_TEST_IMPLEMENTATION
#include "../third-party/jctest/src/jc_test.h"

int main(int argc, char** argv)
{
    jc_test_init(&argc, argv);
    JC_TEST_RUN_ALL();
    return 0;
}
