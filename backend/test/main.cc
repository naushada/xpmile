#ifndef __main_cc__
#define __main_cc__

#include <gtest/gtest.h>


int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
    return(0);
}

#endif /*__main_cc__*/