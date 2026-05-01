#include "webservice_test.hpp"

// ── build_responseOK ─────────────────────────────────────────────────────────

TEST(WebServiceEntry, ResponseOK_NoBody)
{
    WebServiceEntry e;
    std::string rsp = e.build_responseOK("");
    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
}

TEST(WebServiceEntry, ResponseOK_WithJsonBody)
{
    WebServiceEntry e;
    const std::string body = "{\"status\":\"ok\"}";
    std::string rsp = e.build_responseOK(body);

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: " + std::to_string(body.size())));
    EXPECT_NE(std::string::npos, rsp.find("Content-Type: application/json"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(WebServiceEntry, ResponseOK_CustomContentType)
{
    WebServiceEntry e;
    const std::string body = "<html></html>";
    std::string rsp = e.build_responseOK(body, "text/html");

    EXPECT_NE(std::string::npos, rsp.find("Content-Type: text/html"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(WebServiceEntry, ResponseOK_BodyAppearsAfterHeader)
{
    WebServiceEntry e;
    const std::string body = "payload";
    std::string rsp = e.build_responseOK(body);

    auto header_end = rsp.find("\r\n\r\n");
    ASSERT_NE(std::string::npos, header_end);
    EXPECT_EQ(body, rsp.substr(header_end + 4));
}

// ── build_responseCreated ────────────────────────────────────────────────────

TEST(WebServiceEntry, ResponseCreated)
{
    WebServiceEntry e;
    std::string rsp = e.build_responseCreated();

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 201 Created"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
    EXPECT_NE(std::string::npos, rsp.find("Connection: keep-alive"));
}

// ── build_responseERROR ──────────────────────────────────────────────────────

TEST(WebServiceEntry, ResponseError_NoBody)
{
    WebServiceEntry e;
    std::string rsp = e.build_responseERROR("", "404 Not Found");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 404 Not Found"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
}

TEST(WebServiceEntry, ResponseError_WithBody)
{
    WebServiceEntry e;
    const std::string body = "{\"error\":\"bad request\"}";
    std::string rsp = e.build_responseERROR(body, "400 Bad Request");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 400 Bad Request"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: " + std::to_string(body.size())));
    EXPECT_NE(std::string::npos, rsp.find("Content-Type: application/json"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(WebServiceEntry, ResponseError_500)
{
    WebServiceEntry e;
    const std::string body = "{\"error\":\"internal\"}";
    std::string rsp = e.build_responseERROR(body, "500 Internal Server Error");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 500 Internal Server Error"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

// ── get_contentType ──────────────────────────────────────────────────────────

TEST(WebServiceEntry, ContentType_Html)
{
    WebServiceEntry e;
    EXPECT_EQ("text/html", e.get_contentType("html"));
}

TEST(WebServiceEntry, ContentType_Css)
{
    WebServiceEntry e;
    EXPECT_EQ("text/css", e.get_contentType("css"));
}

TEST(WebServiceEntry, ContentType_Js)
{
    WebServiceEntry e;
    EXPECT_EQ("text/javascript", e.get_contentType("js"));
}

TEST(WebServiceEntry, ContentType_Json)
{
    WebServiceEntry e;
    EXPECT_EQ("application/json", e.get_contentType("json"));
}

TEST(WebServiceEntry, ContentType_Png)
{
    WebServiceEntry e;
    EXPECT_EQ("image/png", e.get_contentType("png"));
}

TEST(WebServiceEntry, ContentType_Jpg)
{
    WebServiceEntry e;
    EXPECT_EQ("image/jpeg", e.get_contentType("jpg"));
}

TEST(WebServiceEntry, ContentType_Gif)
{
    WebServiceEntry e;
    EXPECT_EQ("image/gif", e.get_contentType("gif"));
}

TEST(WebServiceEntry, ContentType_Svg)
{
    WebServiceEntry e;
    EXPECT_EQ("image/svg+xml", e.get_contentType("svg"));
}

TEST(WebServiceEntry, ContentType_Ico)
{
    WebServiceEntry e;
    EXPECT_EQ("image/vnd.microsoft.icon", e.get_contentType("ico"));
}

TEST(WebServiceEntry, ContentType_Woff)
{
    WebServiceEntry e;
    EXPECT_EQ("font/woff", e.get_contentType("woff"));
}

TEST(WebServiceEntry, ContentType_Woff2)
{
    WebServiceEntry e;
    EXPECT_EQ("font/woff2", e.get_contentType("woff2"));
}

TEST(WebServiceEntry, ContentType_Ttf)
{
    WebServiceEntry e;
    EXPECT_EQ("font/ttf", e.get_contentType("ttf"));
}

TEST(WebServiceEntry, ContentType_Otf)
{
    WebServiceEntry e;
    EXPECT_EQ("font/otf", e.get_contentType("otf"));
}

TEST(WebServiceEntry, ContentType_Eot)
{
    WebServiceEntry e;
    EXPECT_EQ("application/vnd.ms-fontobject", e.get_contentType("eot"));
}

TEST(WebServiceEntry, ContentType_UnknownFallsBackToHtml)
{
    WebServiceEntry e;
    EXPECT_EQ("text/html", e.get_contentType("xyz"));
    EXPECT_EQ("text/html", e.get_contentType(""));
}

// ── handle_OPTIONS ────────────────────────────────────────────────────────────

TEST(WebServiceEntry, HandleOptions_200OK)
{
    WebServiceEntry e;
    std::string in = "OPTIONS /api/v1/shipment HTTP/1.1\r\nHost: x.com\r\n\r\n";
    std::string rsp = e.handle_OPTIONS(in);

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE"));
}
