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
    //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The header is \n%s"), http.header().c_str()));

    EXPECT_EQ("/", http.get_uriName());
    EXPECT_EQ(uri.str() + header.str(), http.header());
    EXPECT_EQ(http.get_element("Host") , "www.dummyhost.com");
    EXPECT_EQ(http.get_element("Connection") , "Keep-Alive");
}

TEST(HttpTestSuite, doPostTestShipment)
{
    std::stringstream uri("");
    std::stringstream header("");
    std::stringstream req("");
    std::stringstream delim("");
    std::stringstream document("");

    document << "{\"key1\": \"value1\", \"key2\": \"value2\"}";

    uri << "POST /api/v1/shipment/shipping HTTP/1.1\r\n";
    header << "Host: " << "www.dummyhost.com\r\n"
           << "Connection: " << "Keep-Alive\r\n"
           << "Content-Type: " << "application/json\r\n"
           << "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36\r\n"
           << "Content-Length: " << document.str().length() << "\r\n";
    delim  << "\r\n";
    
    req << uri.str() << header.str() << delim.str() << document.str();
    
    Http http(req.str());
    //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The header is \n%s"), http.header().c_str()));
    //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The get_element(User-Agent) \n%s\n"), http.get_element("User-Agent").c_str()));

    //EXPECT_EQ("/", http.get_uriName());
    EXPECT_EQ(uri.str() + header.str(), http.header());
    //EXPECT_EQ(http.get_element("Host") , "www.dummyhost.com");
    EXPECT_EQ(http.body() , document.str());
    EXPECT_EQ(req.str().length() , http.body().length() + http.header().length() + 2);
}

#endif /*__httpparser_cc__ */