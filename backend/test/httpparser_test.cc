#ifndef __httpparser_cc__
#define __httpparser_cc__

#include "httpparser_test.hpp"

void HTTPParserTest::SetUp()
{

}

void HTTPParserTest::TearDown()
{

}

void HTTPParserTest::TestBody()
{

}

HTTPParserTest::HTTPParserTest(const std::string& in)
{
    

}

TEST(HttpTestSuite, doGetTestIndex)
{
    std::stringstream uri("");
    std::stringstream header("");
    std::stringstream req("");
    std::stringstream delim("");

    uri << "GET / HTTP/1.1\r\n";
    header << "Host: " << "www.dummyhost.com\r\n"
           << "Connection: " << "Keep-Alive\r\n";
    delim  << "\r\n";
    //       << "Content-Length: " << 0 << "\r\n";
    req << uri.str() << header.str() << delim.str();
    
    Http http(req.str());
    std::cout << " Http.body() " << http.body() <<  " length: " << http.body().length() << " header.length() " << http.header().length() << std::endl;
    EXPECT_EQ("/", http.get_uriName());
    EXPECT_EQ(uri.str() + header.str(), http.header());
}

#endif /*__httpparser_cc__ */