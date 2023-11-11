#ifndef __httpparser_test_hpp__
#define __httpparser_test_hpp__

#include <gtest/gtest.h>
#include <sstream>
#include "http_parser.h"

class HTTPParserTest : public ::testing::Test
{
    public:
        HTTPParserTest(const std::string& in);
        ~HTTPParserTest() = default;
     
        virtual void SetUp() override;
        virtual void TearDown() override;
        virtual void TestBody() override;
};

#endif /*__emailservice_test_hpp__*/