#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/rand.h>

#include "ace/INET_Addr.h"
#include "ace/SOCK_Acceptor.h"
#include "ace/SOCK_Connector.h"
#include "ace/SOCK_Stream.h"

#include "dbproto.hpp"
#include "mongodbc.hpp"
#include "onprem_agent.hpp"
#include "wsdbagent.hpp"
#include "wsframe.hpp"

// ── helpers ──────────────────────────────────────────────────────────────────

namespace {

/// Minimal IMongodbClient stub — hardcoded returns for session tests.
/// Not a reusable test fixture; lives inline in this file only.
struct StubDb : IMongodbClient {
    std::string sval = R"({"test":"ok"})";
    std::int32_t ival = 1;
    bool bval = true;
    std::vector<uint8_t> data = {0xAA, 0xBB};

    const std::string& get_database() const override {
        static std::string db = "testdb";
        return db;
    }
    std::string create_document(const std::string&, const std::string&,
                                const std::string&) override { return sval; }
    std::int32_t create_bulk_document(const std::string&, const std::string&,
                                      const std::string&) override { return ival; }
    bool update_collection(const std::string&, const std::string&,
                           const std::string&) override { return bval; }
    std::int32_t update_bulk_document(const std::string&,
                                      const std::vector<std::string>&,
                                      const std::vector<std::string>&) override { return ival; }
    bool delete_document(const std::string&, const std::string&) override { return bval; }
    std::string get_document(const std::string&, const std::string&,
                             const std::string&) override { return sval; }
    std::string get_documents(const std::string&, const std::string&,
                              const std::string&) override { return sval; }
    std::string get_documents(const std::string&, const std::string&) override { return sval; }
    std::string next_awbno(const std::string&) override { return sval; }
    std::string store_file(const std::string&, const std::string&,
                           const std::vector<uint8_t>&) override { return sval; }
    std::vector<uint8_t> fetch_file(const std::string&) override { return data; }
    std::vector<uint8_t> fetch_file_by_id(const std::string&) override { return data; }
    bool delete_file(const std::string&) override { return bval; }
};

std::string random_ws_key() {
    unsigned char bytes[16];
    RAND_bytes(bytes, sizeof(bytes));
    // Base64 encode
    static const char kTbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(24);
    int bits = 0, val = 0;
    for (int i = 0; i < 16; ++i) {
        val = (val << 8) | bytes[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += kTbl[(val >> bits) & 0x3F];
        }
    }
    if (bits > 0) out += kTbl[(val << (6 - bits)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}

/// Send a WebSocket upgrade request, return the accept key sent.
std::string send_ws_upgrade(int fd, const std::string& host = "127.0.0.1") {
    std::string key = random_ws_key();
    std::ostringstream req;
    req << "GET /ws/db HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";
    std::string s = req.str();
    ::send(fd, s.data(), s.size(), 0);
    return key;
}

/// Read HTTP response headers until \r\n\r\n, return the full header string.
std::string recv_http_response(int fd) {
    std::string h;
    h.reserve(512);
    char c;
    while (h.size() < 4096) {
        if (::recv(fd, &c, 1, 0) != 1) break;
        h += c;
        if (h.size() >= 4 && h.substr(h.size() - 4) == "\r\n\r\n") break;
    }
    return h;
}

/// Send a WebSocket binary frame (masked, per RFC 6455 client rule).
bool send_ws_binary(int fd, const std::vector<uint8_t>& payload) {
    auto frame = wsframe::encode(payload, 0x02, true);
    return ::send(fd, frame.data(), frame.size(), 0)
           == static_cast<ssize_t>(frame.size());
}

/// Read one WebSocket frame and return the decoded frame.
std::optional<wsframe::Frame> recv_ws_frame(int fd) {
    std::vector<uint8_t> buf(65536);
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) return std::nullopt;
    buf.resize(n);
    return wsframe::decode(buf);
}

/// Connect to 127.0.0.1:port and return the fd, or -1 on failure.
int tcp_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

// ── test fixture ─────────────────────────────────────────────────────────────

class OnpremAgentTest : public ::testing::Test {
protected:
    StubDb stub_;
};

// ── 1. AcceptsConnection ─────────────────────────────────────────────────────

TEST_F(OnpremAgentTest, AcceptsConnection) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());
    ASSERT_GT(listener.port(), 0);

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    ::close(fd);
    listener.stop();
}

// ── 2. WebSocketUpgrade ──────────────────────────────────────────────────────

TEST_F(OnpremAgentTest, WebSocketUpgrade) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    std::string key       = send_ws_upgrade(fd);
    std::string response  = recv_http_response(fd);
    std::string expected  = wsframe::accept_key(key);

    EXPECT_NE(response.find("101"), std::string::npos);
    EXPECT_NE(response.find(expected), std::string::npos);

    ::close(fd);
    listener.stop();
}

// ── 3. SingleRequestResponse ─────────────────────────────────────────────────

TEST_F(OnpremAgentTest, SingleRequestResponse) {
    stub_.sval = R"({"awbno":"AWB000001"})";

    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    send_ws_upgrade(fd);
    recv_http_response(fd);  // consume 101

    auto [reqid, req_bson] = dbproto::build_request(
        DbOp::GET_DOCUMENT, "testdb", "shipping");

    ASSERT_TRUE(send_ws_binary(fd, req_bson));

    auto frame = recv_ws_frame(fd);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->opcode, 0x02);

    dbproto::DbResponse rsp;
    ASSERT_TRUE(dbproto::parse_response(frame->payload, rsp));
    EXPECT_TRUE(rsp.ok);
    EXPECT_EQ(rsp.reqid, reqid);
    EXPECT_EQ(rsp.sval, R"({"awbno":"AWB000001"})");

    ::close(fd);
    listener.stop();
}

// ── 4. MultipleRequestsInSequence ────────────────────────────────────────────

TEST_F(OnpremAgentTest, MultipleRequestsInSequence) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    send_ws_upgrade(fd);
    recv_http_response(fd);  // consume 101

    for (int i = 0; i < 3; ++i) {
        stub_.sval = "req-" + std::to_string(i);

        auto [reqid, req_bson] = dbproto::build_request(
            DbOp::GET_DOCUMENT, "testdb", "shipping");

        ASSERT_TRUE(send_ws_binary(fd, req_bson))
            << "send failed on request " << i;

        auto frame = recv_ws_frame(fd);
        ASSERT_TRUE(frame.has_value())
            << "recv failed on request " << i;
        EXPECT_EQ(frame->opcode, 0x02);

        dbproto::DbResponse rsp;
        ASSERT_TRUE(dbproto::parse_response(frame->payload, rsp));
        EXPECT_TRUE(rsp.ok);
        EXPECT_EQ(rsp.reqid, reqid);
        EXPECT_EQ(rsp.sval, "req-" + std::to_string(i));
    }

    ::close(fd);
    listener.stop();
}

// ── 5. ClientDisconnect_CleansUp ─────────────────────────────────────────────

TEST_F(OnpremAgentTest, ClientDisconnectCleansUp) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    // First client
    {
        int fd = tcp_connect(listener.port());
        ASSERT_GE(fd, 0);
        send_ws_upgrade(fd);
        recv_http_response(fd);

        auto [reqid, req_bson] = dbproto::build_request(
            DbOp::GET_DOCUMENT, "testdb", "shipping");
        send_ws_binary(fd, req_bson);
        auto frame = recv_ws_frame(fd);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, 0x02);

        ::close(fd);  // disconnect
    }

    // Brief pause to let the listener detect disconnection
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Second client — should be accepted
    {
        int fd = tcp_connect(listener.port());
        ASSERT_GE(fd, 0);
        send_ws_upgrade(fd);
        recv_http_response(fd);

        auto [reqid, req_bson] = dbproto::build_request(
            DbOp::GET_DOCUMENT, "testdb", "shipping");
        send_ws_binary(fd, req_bson);
        auto frame = recv_ws_frame(fd);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, 0x02);

        ::close(fd);
    }

    listener.stop();
}

// ── 6. MalformedRequest_NoCrash ──────────────────────────────────────────────

TEST_F(OnpremAgentTest, MalformedRequestNoCrash) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    send_ws_upgrade(fd);
    recv_http_response(fd);

    // Send a binary frame with non-BSON garbage
    std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF};
    send_ws_binary(fd, garbage);

    // Listener should not crash. Send a valid request to verify it's still alive.
    stub_.sval = "still-alive";
    auto [reqid, req_bson] = dbproto::build_request(
        DbOp::GET_DOCUMENT, "testdb", "shipping");
    ASSERT_TRUE(send_ws_binary(fd, req_bson));

    auto frame = recv_ws_frame(fd);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->opcode, 0x02);

    ::close(fd);
    listener.stop();
}

// ── 7. PingPong ──────────────────────────────────────────────────────────────

TEST_F(OnpremAgentTest, PingPong) {
    LocalWsListener listener(0, stub_, "testdb");
    ASSERT_TRUE(listener.start());

    int fd = tcp_connect(listener.port());
    ASSERT_GE(fd, 0);

    send_ws_upgrade(fd);
    recv_http_response(fd);

    // Send a ping frame
    auto ping = wsframe::ping_frame();
    ASSERT_EQ(::send(fd, ping.data(), ping.size(), 0),
              static_cast<ssize_t>(ping.size()));

    // Read pong
    auto frame = recv_ws_frame(fd);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->opcode, 0x0A);  // pong

    ::close(fd);
    listener.stop();
}
