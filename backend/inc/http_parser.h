#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <unordered_map>
#include <string>

#include "ace/Log_Msg.h"

/**
 * @brief Parses a raw HTTP/1.1 request message into its constituent parts.
 *
 * Handles the request line (method + URI + query string), MIME headers,
 * chunked transfer-encoding (RFC 7230 §4.1), and gzip/deflate
 * Content-Encoding.  Protocol-buffer and other binary content-types are
 * extracted as raw bytes without further interpretation.
 *
 * @par Typical usage
 * @code
 *   Http h(raw_request_string);
 *   if (h.method() == "GET" && h.uri() == "/api/v1/shipment") { ... }
 *   std::string body = h.body();          // decoded and decompressed
 *   std::string host = h.get_element("Host");
 * @endcode
 */
class Http {
public:
    /// Construct an empty parser instance (no message parsed).
    Http() = default;

    /**
     * @brief Parse a complete HTTP/1.1 request message.
     * @param in Raw bytes of the HTTP request, including headers and body.
     */
    explicit Http(const std::string& in);

    ~Http() = default;

    /**
     * @brief Return the decoded request URI path (no query string).
     * @return Percent-decoded URI path, e.g. @c "/api/v1/shipment".
     */
    const std::string& uri() const { return m_uriName; }

    /**
     * @brief Override the parsed URI path.
     * @param name New URI path value.
     */
    void uri(std::string name) { m_uriName = std::move(name); }

    /**
     * @brief Return the HTTP request method.
     * @return Method string, e.g. @c "GET", @c "POST", @c "PUT".
     */
    const std::string& method() const { return m_method; }

    /**
     * @brief Store a key/value pair (query-string parameter or MIME header).
     * @param key   Header name or query-string key (case-sensitive).
     * @param value Associated value.
     */
    void add_element(std::string key, std::string value);

    /**
     * @brief Look up a previously stored key.
     * @param key Header name or query-string key.
     * @return The associated value, or an empty string if not found.
     */
    std::string get_element(const std::string& key) const;

    /**
     * @brief Return the decoded and decompressed request body.
     *
     * Transfer-Encoding (chunked) is decoded first; Content-Encoding
     * (gzip / deflate) is decompressed second, matching the HTTP layering
     * specification.
     *
     * @return Body bytes as a string (may contain binary data).
     */
    const std::string& body() const { return m_body; }

    /**
     * @brief Return the raw header section, including the trailing CRLFCRLF.
     * @return Header bytes up to and including the blank-line separator.
     */
    const std::string& header() const { return m_header; }

    /**
     * @brief Parse the request line into method, URI path, and query params.
     * @param in Full raw request string.
     */
    void parse_uri(const std::string& in);

    /**
     * @brief Parse all MIME header lines into the internal key/value store.
     * @param in Full raw request string (parsing starts after the first line).
     */
    void parse_mime_header(const std::string& in);

    /// Write parsed fields to the ACE debug log.
    void dump() const;

    /**
     * @brief Determine the total wire length of the HTTP/1.1 message in @p buf.
     *
     * Intended for use in socket read loops: peek a chunk of bytes, call this
     * function, then allocate and receive exactly that many bytes.
     *
     * Rules applied in order:
     *  1. If the header separator (CRLFCRLF) is not yet present → returns 0.
     *  2. @c Transfer-Encoding: chunked → scans for the terminal @c 0\\r\\n\\r\\n
     *     chunk; returns 0 if it is not yet in @p buf.
     *  3. @c Content-Length: N → returns header_length + N (covers plain, gzip,
     *     and deflate bodies, all of which are framed by Content-Length on the wire).
     *  4. No body headers (GET, DELETE, OPTIONS …) → returns header length only.
     *
     * @param buf Raw bytes received from the socket (peek or partial read).
     * @return Total byte count of the complete message, or 0 if more data is needed.
     */
    static std::size_t message_length(const std::string& buf);

private:
    /**
     * @brief Extract the header section from the raw request.
     * @param in Raw request bytes.
     * @return Substring from the start up to and including CRLFCRLF.
     */
    std::string get_header(const std::string& in);

    /**
     * @brief Extract, decode, and decompress the body from the raw request.
     * @param in Raw request bytes.
     * @return Decoded body, or an empty string when there is no body.
     */
    std::string get_body(const std::string& in);

    std::unordered_map<std::string, std::string> m_tokenMap;
    std::string m_uriName;
    std::string m_header;
    std::string m_body;
    std::string m_method;
};

#endif // HTTP_PARSER_H
