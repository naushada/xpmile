#include "webservice_test.hpp"

// ── build_responseOK ─────────────────────────────────────────────────────────

TEST(MicroService, ResponseOK_NoBody)
{
    MicroService e;
    std::string rsp = e.build_responseOK("");
    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
}

TEST(MicroService, ResponseOK_WithJsonBody)
{
    MicroService e;
    const std::string body = "{\"status\":\"ok\"}";
    std::string rsp = e.build_responseOK(body);

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: " + std::to_string(body.size())));
    EXPECT_NE(std::string::npos, rsp.find("Content-Type: application/json"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(MicroService, ResponseOK_CustomContentType)
{
    MicroService e;
    const std::string body = "<html></html>";
    std::string rsp = e.build_responseOK(body, "text/html");

    EXPECT_NE(std::string::npos, rsp.find("Content-Type: text/html"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(MicroService, ResponseOK_BodyAppearsAfterHeader)
{
    MicroService e;
    const std::string body = "payload";
    std::string rsp = e.build_responseOK(body);

    auto header_end = rsp.find("\r\n\r\n");
    ASSERT_NE(std::string::npos, header_end);
    EXPECT_EQ(body, rsp.substr(header_end + 4));
}

// ── build_responseCreated ────────────────────────────────────────────────────

TEST(MicroService, ResponseCreated)
{
    MicroService e;
    std::string rsp = e.build_responseCreated();

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 201 Created"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
    EXPECT_NE(std::string::npos, rsp.find("Connection: keep-alive"));
}

// ── build_responseERROR ──────────────────────────────────────────────────────

TEST(MicroService, ResponseError_NoBody)
{
    MicroService e;
    std::string rsp = e.build_responseERROR("", "404 Not Found");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 404 Not Found"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
}

TEST(MicroService, ResponseError_WithBody)
{
    MicroService e;
    const std::string body = "{\"error\":\"bad request\"}";
    std::string rsp = e.build_responseERROR(body, "400 Bad Request");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 400 Bad Request"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: " + std::to_string(body.size())));
    EXPECT_NE(std::string::npos, rsp.find("Content-Type: application/json"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

TEST(MicroService, ResponseError_500)
{
    MicroService e;
    const std::string body = "{\"error\":\"internal\"}";
    std::string rsp = e.build_responseERROR(body, "500 Internal Server Error");

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 500 Internal Server Error"));
    EXPECT_NE(std::string::npos, rsp.find(body));
}

// ── get_contentType ──────────────────────────────────────────────────────────

TEST(MicroService, ContentType_Html)
{
    MicroService e;
    EXPECT_EQ("text/html", e.get_contentType("html"));
}

TEST(MicroService, ContentType_Css)
{
    MicroService e;
    EXPECT_EQ("text/css", e.get_contentType("css"));
}

TEST(MicroService, ContentType_Js)
{
    MicroService e;
    EXPECT_EQ("text/javascript", e.get_contentType("js"));
}

TEST(MicroService, ContentType_Json)
{
    MicroService e;
    EXPECT_EQ("application/json", e.get_contentType("json"));
}

TEST(MicroService, ContentType_Png)
{
    MicroService e;
    EXPECT_EQ("image/png", e.get_contentType("png"));
}

TEST(MicroService, ContentType_Jpg)
{
    MicroService e;
    EXPECT_EQ("image/jpeg", e.get_contentType("jpg"));
}

TEST(MicroService, ContentType_Gif)
{
    MicroService e;
    EXPECT_EQ("image/gif", e.get_contentType("gif"));
}

TEST(MicroService, ContentType_Svg)
{
    MicroService e;
    EXPECT_EQ("image/svg+xml", e.get_contentType("svg"));
}

TEST(MicroService, ContentType_Ico)
{
    MicroService e;
    EXPECT_EQ("image/vnd.microsoft.icon", e.get_contentType("ico"));
}

TEST(MicroService, ContentType_Woff)
{
    MicroService e;
    EXPECT_EQ("font/woff", e.get_contentType("woff"));
}

TEST(MicroService, ContentType_Woff2)
{
    MicroService e;
    EXPECT_EQ("font/woff2", e.get_contentType("woff2"));
}

TEST(MicroService, ContentType_Ttf)
{
    MicroService e;
    EXPECT_EQ("font/ttf", e.get_contentType("ttf"));
}

TEST(MicroService, ContentType_Otf)
{
    MicroService e;
    EXPECT_EQ("font/otf", e.get_contentType("otf"));
}

TEST(MicroService, ContentType_Eot)
{
    MicroService e;
    EXPECT_EQ("application/vnd.ms-fontobject", e.get_contentType("eot"));
}

TEST(MicroService, ContentType_UnknownFallsBackToHtml)
{
    MicroService e;
    EXPECT_EQ("text/html", e.get_contentType("xyz"));
    EXPECT_EQ("text/html", e.get_contentType(""));
}

// ── handle_OPTIONS ────────────────────────────────────────────────────────────

TEST(MicroService, HandleOptions_200OK)
{
    MicroService e;
    std::string in = "OPTIONS /api/v1/shipment HTTP/1.1\r\nHost: x.com\r\n\r\n";
    std::string rsp = e.handle_OPTIONS(in);

    EXPECT_NE(std::string::npos, rsp.find("HTTP/1.1 200 OK"));
    EXPECT_NE(std::string::npos, rsp.find("Content-Length: 0"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Origin: *"));
    EXPECT_NE(std::string::npos, rsp.find("Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE"));
}
