#include "onprem_agent.hpp"

#include <cstring>
#include <poll.h>
#include <sstream>

#include <bsoncxx/json.hpp>

#include "ace/INET_Addr.h"
#include "ace/Log_Msg.h"
#include "ace/SOCK_Acceptor.h"

#include "json.hpp"
#include "wsframe.hpp"

// ── recv_ready (same as wsdbagent.cpp:194) ──────────────────────────────────

static bool recv_ready(int fd, int timeout_secs) {
    struct pollfd pfd = {fd, POLLIN, 0};
    int r = ::poll(&pfd, 1, timeout_secs * 1000);
    if (r < 0) return true;  // error → let recv detect it
    return r > 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LocalWsListener
// ═══════════════════════════════════════════════════════════════════════════════

LocalWsListener::LocalWsListener(std::uint16_t port, IMongodbClient& db,
                                 const std::string& db_name)
    : m_port(port), m_db(db), m_dbName(db_name) {}

LocalWsListener::~LocalWsListener() { stop(); }

bool LocalWsListener::start() {
    ACE_INET_Addr addr(m_port, "0.0.0.0");
    if (m_acceptor.open(addr) == -1) {
        ACE_ERROR((LM_ERROR,
                   ACE_TEXT("%D [LocalWsListener] bind failed on port %u\n"),
                   m_port));
        return false;
    }
    // Retrieve the assigned port (relevant when m_port was 0)
    ACE_INET_Addr local;
    m_acceptor.get_local_addr(local);
    m_port = local.get_port_number();

    m_stop.store(false);
    m_thread = std::thread(&LocalWsListener::accept_loop, this);
    return true;
}

void LocalWsListener::stop() {
    m_stop.store(true);
    // Close the acceptor to unblock accept() in the worker thread
    m_acceptor.close();
    if (m_thread.joinable()) m_thread.join();
}

std::uint16_t LocalWsListener::port() const { return m_port; }

// ── accept_loop ─────────────────────────────────────────────────────────────

void LocalWsListener::accept_loop() {
    ACE_Time_Value timeout(0, 500000);  // 500 ms
    ACE_INET_Addr   client_addr;

    while (!m_stop.load()) {
        ACE_SOCK_Stream stream;
        if (m_acceptor.accept(stream, &client_addr, &timeout) == -1) {
            if (m_stop.load()) break;
            continue;
        }

        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [LocalWsListener] accepted connection\n")));

        // WebSocket upgrade — server side. Read character-by-character
        // until \r\n\r\n, same pattern as WsDbAgent::connect_and_handshake().
        std::string headers;
        headers.reserve(512);
        {
            char c = 0;
            ACE_Time_Value tv(5, 0);  // 5 s timeout for upgrade
            while (headers.size() < 4096) {
                if (stream.recv_n(&c, 1, &tv) != 1) break;
                headers += c;
                if (headers.size() >= 4 &&
                    headers.substr(headers.size() - 4) == "\r\n\r\n")
                    break;
            }
        }
        if (headers.size() < 4 ||
            headers.substr(headers.size() - 4) != "\r\n\r\n") {
            stream.close();
            continue;
        }

        // Extract Sec-WebSocket-Key
        auto pos = headers.find("Sec-WebSocket-Key:");
        if (pos == std::string::npos) { stream.close(); continue; }
        pos += 19;  // strlen("Sec-WebSocket-Key:")
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
        if (stream.send_n(rsp_str.data(), rsp_str.size())
            != static_cast<ssize_t>(rsp_str.size())) {
            stream.close();
            continue;
        }

        run_session(stream);
        stream.close();
    }
}

// ── run_session (same logic as WsDbAgent::run_session at wsdbagent.cpp:204) ─

void LocalWsListener::run_session(ACE_SOCK_Stream& stream) {
    constexpr int PING_INTERVAL_S = 30;
    std::vector<uint8_t> buf;  // persistent across reads

    while (!m_stop.load()) {
        int fd = static_cast<int>(stream.get_handle());

        if (!recv_ready(fd, PING_INTERVAL_S)) {
            auto ping_frame = wsframe::encode({}, 0x09, false);
            if (stream.send_n(ping_frame.data(), ping_frame.size())
                != static_cast<ssize_t>(ping_frame.size())) {
                break;
            }
            continue;
        }

        // Read available bytes into persistent buffer
        char tmp[8192];
        ssize_t nr = ACE_OS::recv(fd, tmp, sizeof(tmp), 0);
        if (nr <= 0) break;
        buf.insert(buf.end(), tmp, tmp + nr);

        // Try to decode a complete frame. May need more reads.
        auto decoded = wsframe::decode(buf);
        if (!decoded) continue;  // incomplete frame, read more

        switch (decoded->opcode) {
        case 0x02: {  // binary — BSON request
            dbproto::DbRequest req;
            if (!dbproto::parse_request(decoded->payload, req)) {
                ACE_ERROR((LM_ERROR,
                           ACE_TEXT("%D [LocalWsListener] malformed request\n")));
                continue;
            }
            auto rsp_bson = dispatch(req);
            auto rsp_frame = wsframe::encode(rsp_bson, 0x02, false);
            if (stream.send_n(rsp_frame.data(), rsp_frame.size())
                != static_cast<ssize_t>(rsp_frame.size())) {
                break;
            }
            break;
        }
        case 0x09: {  // ping → pong
            auto pong = wsframe::encode(decoded->payload, 0x0A, false);
            if (stream.send_n(pong.data(), pong.size())
                != static_cast<ssize_t>(pong.size())) {
                break;
            }
            break;
        }
        case 0x08: {  // close
            auto close_frame = wsframe::close_frame();
            stream.send_n(close_frame.data(), close_frame.size());
            return;
        }
        default:
            break;
        }
    }
}

// ── dispatch (same logic as WsDbAgent::dispatch at wsdbagent.cpp:276) ───────

std::vector<std::uint8_t> LocalWsListener::dispatch(
    const dbproto::DbRequest& req) {
    dbproto::DbResponse rsp;
    rsp.reqid = req.reqid;
    rsp.ok    = false;

    try {
        switch (req.op) {

        case DbOp::CREATE_DOCUMENT:
            rsp.sval = m_db.create_document(m_dbName, req.coll,
                                            bson_to_json(req.doc));
            rsp.ok   = !rsp.sval.empty();
            break;

        case DbOp::CREATE_BULK_DOCUMENT: {
            std::string json_str(req.doc.begin(), req.doc.end());
            rsp.ival = m_db.create_bulk_document(m_dbName, req.coll, json_str);
            rsp.ok   = (rsp.ival >= 0);
            break;
        }

        case DbOp::UPDATE_COLLECTION:
            rsp.bval = m_db.update_collection(req.coll,
                                              bson_to_json(req.doc),
                                              bson_to_json(req.doc2));
            rsp.ok   = true;
            break;

        case DbOp::UPDATE_BULK_DOCUMENT: {
            std::string fa_str(req.doc.begin(),  req.doc.end());
            std::string va_str(req.doc2.begin(), req.doc2.end());
            auto fa = nlohmann::json::parse(fa_str)
                          .get<std::vector<std::string>>();
            auto va = nlohmann::json::parse(va_str)
                          .get<std::vector<std::string>>();
            rsp.ival = m_db.update_bulk_document(req.coll, fa, va);
            rsp.ok   = (rsp.ival >= 0);
            break;
        }

        case DbOp::DELETE_DOCUMENT:
            rsp.bval = m_db.delete_document(req.coll,
                                            bson_to_json(req.doc));
            rsp.ok   = true;
            break;

        case DbOp::GET_DOCUMENT:
            rsp.sval = m_db.get_document(req.coll,
                                         bson_to_json(req.doc),
                                         bson_to_json(req.doc2));
            rsp.ok   = !rsp.sval.empty();
            break;

        case DbOp::GET_DOCUMENTS_QUERIED:
            rsp.sval = m_db.get_documents(req.coll,
                                          bson_to_json(req.doc),
                                          bson_to_json(req.doc2));
            rsp.ok   = true;
            break;

        case DbOp::GET_DOCUMENTS_ALL:
            rsp.sval = m_db.get_documents(req.coll,
                                          bson_to_json(req.doc2));
            rsp.ok   = true;
            break;

        case DbOp::NEXT_AWBNO:
            rsp.sval = m_db.next_awbno(req.sval);
            rsp.ok   = !rsp.sval.empty();
            break;

        case DbOp::STORE_FILE: {
            auto sep = req.sval.find('|');
            std::string filename = req.sval.substr(0, sep);
            std::string content_type = (sep != std::string::npos)
                                           ? req.sval.substr(sep + 1) : "";
            rsp.sval = m_db.store_file(filename, content_type, req.doc);
            rsp.ok   = !rsp.sval.empty();
            break;
        }

        case DbOp::FETCH_FILE:
            rsp.data = m_db.fetch_file(req.sval);
            rsp.ok   = !rsp.data.empty();
            break;

        case DbOp::FETCH_FILE_BY_ID:
            rsp.data = m_db.fetch_file_by_id(req.sval);
            rsp.ok   = !rsp.data.empty();
            break;

        case DbOp::DELETE_FILE:
            rsp.bval = m_db.delete_file(req.sval);
            rsp.ok   = true;
            break;

        default:
            rsp.errmsg = "unknown op";
            break;
        }
    } catch (const std::exception& e) {
        rsp.ok     = false;
        rsp.errmsg = e.what();
    }

    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [LocalWsListener] op=%d reqid=%d ok=%d\n"),
               static_cast<int>(req.op), req.reqid, (int)rsp.ok));

    return dbproto::build_response(rsp);
}

// ── bson_to_json (same as WsDbAgent::bson_to_json at wsdbagent.cpp:472) ────

std::string LocalWsListener::bson_to_json(
    const std::vector<std::uint8_t>& bson) {
    if (bson.empty()) return "{}";
    try {
        bsoncxx::document::view v{bson.data(), bson.size()};
        return bsoncxx::to_json(v);
    } catch (...) {
        return "{}";
    }
}
