#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ace/INET_Addr.h"
#include "ace/SOCK_Acceptor.h"
#include "ace/SOCK_Connector.h"
#include "ace/SOCK_Stream.h"

#include "dbproto.hpp"
#include "mongodbc.hpp"
#include "onprem_agent.hpp"
#include "wsdbagent.hpp"
#include "wsframe.hpp"

// ── helpers (duplicated from onprem_agent_test.cc — ok for separate test file) ──

namespace {

/// Minimal IMongodbClient stub for Phase 2 tests
struct StubDb2 : IMongodbClient {
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

/// Read HTTP response headers until \r\n\r\n
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

/// Send a WebSocket binary frame (masked)
bool send_ws_binary(int fd, const std::vector<uint8_t>& payload) {
    auto frame = wsframe::encode(payload, 0x02, true);
    return ::send(fd, frame.data(), frame.size(), 0)
           == static_cast<ssize_t>(frame.size());
}

/// Read one WebSocket frame
std::optional<wsframe::Frame> recv_ws_frame(int fd) {
    std::vector<uint8_t> buf(65536);
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) return std::nullopt;
    buf.resize(n);
    return wsframe::decode(buf);
}

/// TCP connect to 127.0.0.1:port
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

/// Send a WebSocket upgrade request
std::string send_ws_upgrade(int fd) {
    // Use a fixed key for simplicity
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::ostringstream req;
    req << "GET /ws/db HTTP/1.1\r\n"
        << "Host: 127.0.0.1\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";
    std::string s = req.str();
    ::send(fd, s.data(), s.size(), 0);
    return key;
}

/// Minimal fake cloud server — binds, accepts, does server-side WS upgrade.
struct FakeCloudServer {
    int listen_fd = -1;
    int client_fd = -1;
    std::uint16_t bound_port = 0;

    bool start() {
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) return false;
        int opt = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;
        if (::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd, (sockaddr*)&addr, &len);
        bound_port = ntohs(addr.sin_port);
        ::listen(listen_fd, 1);
        return true;
    }

    bool accept_and_upgrade() {
        client_fd = ::accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) return false;
        std::string headers = recv_http_response(client_fd);
        if (headers.find("Sec-WebSocket-Key:") == std::string::npos) return false;
        auto pos = headers.find("Sec-WebSocket-Key:");
        pos += 19;
        while (pos < headers.size() && headers[pos] == ' ') ++pos;
        auto end = headers.find("\r\n", pos);
        std::string key = headers.substr(pos, end - pos);
        std::string accept = wsframe::accept_key(key);
        std::ostringstream rsp;
        rsp << "HTTP/1.1 101 Switching Protocols\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Accept: " << accept << "\r\n"
            << "\r\n";
        std::string rsp_str = rsp.str();
        return ::send(client_fd, rsp_str.data(), rsp_str.size(), 0)
               == static_cast<ssize_t>(rsp_str.size());
    }

    void close_client() {
        if (client_fd >= 0) { ::close(client_fd); client_fd = -1; }
    }

    ~FakeCloudServer() {
        close_client();
        if (listen_fd >= 0) ::close(listen_fd);
    }
};

} // namespace

// ── test fixture ─────────────────────────────────────────────────────────────

class OnpremAgentPhase2Test : public ::testing::Test {
protected:
    StubDb2 stub_;
};

// ── 8. LocalWsPort_Zero_NoListener ───────────────────────────────────────────

TEST_F(OnpremAgentPhase2Test, LocalWsPortZeroNoListener) {
    WsDbAgent agent("127.0.0.1", 443, false, stub_, "testdb", -1);
    EXPECT_EQ(agent.local_ws_port(), 0);
}

// ── 9. LocalWsPort_StartsListener ────────────────────────────────────────────

TEST_F(OnpremAgentPhase2Test, LocalWsPortStartsListener) {
    stub_.sval = R"({"awbno":"LOCAL001"})";

    // Start fake cloud server so agent has something to connect to
    FakeCloudServer cloud;
    ASSERT_TRUE(cloud.start());

    // Agent with local_ws_port=0 (ephemeral) connecting to fake cloud
    WsDbAgent agent("127.0.0.1", cloud.bound_port, false, stub_, "testdb", 0);  // 0 = ephemeral

    // Start run() in background
    std::thread agent_thread([&] { agent.run(1); });

    // Wait for agent to connect to cloud (upgrade handshake)
    ASSERT_TRUE(cloud.accept_and_upgrade());

    // LocalWsListener should be running on ephemeral port
    auto local_port = agent.local_ws_port();
    ASSERT_GT(local_port, 0);

    // Connect local client and send request
    int local_fd = tcp_connect(local_port);
    ASSERT_GE(local_fd, 0);

    send_ws_upgrade(local_fd);
    std::string upgrade_rsp = recv_http_response(local_fd);
    EXPECT_NE(upgrade_rsp.find("101"), std::string::npos);

    auto [reqid, bson] = dbproto::build_request(
        DbOp::GET_DOCUMENT, "testdb", "shipping");
    ASSERT_TRUE(send_ws_binary(local_fd, bson));

    auto frame = recv_ws_frame(local_fd);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->opcode, 0x02);

    dbproto::DbResponse rsp;
    ASSERT_TRUE(dbproto::parse_response(frame->payload, rsp));
    EXPECT_TRUE(rsp.ok);
    EXPECT_EQ(rsp.sval, R"({"awbno":"LOCAL001"})");

    ::close(local_fd);
    cloud.close_client();
    agent.stop();
    agent_thread.join();
}

// ── 10. BothPaths_Concurrent ─────────────────────────────────────────────────

TEST_F(OnpremAgentPhase2Test, BothPathsConcurrent) {
    // Start fake cloud server
    FakeCloudServer cloud;
    ASSERT_TRUE(cloud.start());

    stub_.sval = "AWB000042";

    // Agent connecting to fake cloud + local WS on ephemeral port
    WsDbAgent agent("127.0.0.1", cloud.bound_port, false, stub_, "testdb", 0);

    std::thread agent_thread([&] { agent.run(1); });

    // Cloud side: accept and do upgrade
    ASSERT_TRUE(cloud.accept_and_upgrade());

    // Send request from cloud side → should go through agent to stub DB
    auto [cloud_reqid, cloud_bson] = dbproto::build_request(
        DbOp::NEXT_AWBNO, "testdb", "shipping", {}, {}, "AWB");
    ASSERT_TRUE(send_ws_binary(cloud.client_fd, cloud_bson));

    auto cloud_frame = recv_ws_frame(cloud.client_fd);
    ASSERT_TRUE(cloud_frame.has_value());
    EXPECT_EQ(cloud_frame->opcode, 0x02);

    dbproto::DbResponse cloud_rsp;
    ASSERT_TRUE(dbproto::parse_response(cloud_frame->payload, cloud_rsp));
    EXPECT_TRUE(cloud_rsp.ok);
    EXPECT_EQ(cloud_rsp.sval, "AWB000042");

    // Local side: connect to LocalWsListener
    auto local_port = agent.local_ws_port();
    ASSERT_GT(local_port, 0);

    int local_fd = tcp_connect(local_port);
    ASSERT_GE(local_fd, 0);

    send_ws_upgrade(local_fd);
    std::string upgrade_rsp = recv_http_response(local_fd);
    EXPECT_NE(upgrade_rsp.find("101"), std::string::npos);

    stub_.sval = R"({"awbno":"LOCAL042"})";
    auto [local_reqid, local_bson] = dbproto::build_request(
        DbOp::GET_DOCUMENT, "testdb", "shipping");
    ASSERT_TRUE(send_ws_binary(local_fd, local_bson));

    auto local_frame = recv_ws_frame(local_fd);
    ASSERT_TRUE(local_frame.has_value());
    EXPECT_EQ(local_frame->opcode, 0x02);

    dbproto::DbResponse local_rsp;
    ASSERT_TRUE(dbproto::parse_response(local_frame->payload, local_rsp));
    EXPECT_TRUE(local_rsp.ok);
    EXPECT_EQ(local_rsp.sval, R"({"awbno":"LOCAL042"})");

    ::close(local_fd);
    cloud.close_client();
    agent.stop();
    agent_thread.join();
}