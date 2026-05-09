#ifndef ONPREM_AGENT_HPP
#define ONPREM_AGENT_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ace/SOCK_Acceptor.h"
#include "ace/SOCK_Stream.h"

#include "dbproto.hpp"
#include "mongodbc.hpp"

/**
 * @brief Accepts local plain-WebSocket connections and dispatches DBproto
 *        requests to a shared MongodbClient.
 *
 * Runs its own accept thread.  Each accepted connection is upgraded to
 * WebSocket and serviced with a receive-dispatch-respond loop identical in
 * behaviour to WsDbAgent::run_session().
 */
class LocalWsListener {
public:
    /// @param port       TCP port to listen on (0 = OS-assigned ephemeral).
    /// @param db         IMongodbClient to dispatch requests to.
    /// @param db_name    Database name passed through to dispatch().
    LocalWsListener(std::uint16_t port, IMongodbClient& db,
                    const std::string& db_name);

    ~LocalWsListener();

    /// Bind and start the accept thread.  Returns false on bind failure.
    bool start();

    /// Signal the accept thread to exit and join it.
    void stop();

    /// Return the port the acceptor is bound to (useful when port 0 was
    /// passed to the constructor).
    std::uint16_t port() const;

private:
    void accept_loop();
    void run_session(ACE_SOCK_Stream& stream);
    std::vector<std::uint8_t> dispatch(const dbproto::DbRequest& req);

    static std::string bson_to_json(const std::vector<std::uint8_t>& bson);

    std::uint16_t      m_port;
    IMongodbClient&    m_db;
    std::string        m_dbName;
    ACE_SOCK_Acceptor m_acceptor;
    std::atomic<bool> m_stop {false};
    std::thread       m_thread;
};

#endif // ONPREM_AGENT_HPP
