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
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The header is \n%s"), http.header().c_str()));

    EXPECT_EQ("/", http.get_uriName());
    EXPECT_EQ(uri.str() + header.str(), http.header());
    EXPECT_EQ(http.get_element("Host") , "www.dummyhost.com");
    EXPECT_EQ(http.get_element("Connection") , "Keep-Alive");
}

#endif /*__httpparser_cc__ */