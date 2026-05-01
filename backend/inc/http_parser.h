#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <unordered_map>
#include <string>

#include "ace/Log_Msg.h"

class Http {
public:
    Http() = default;
    explicit Http(const std::string& in);
    ~Http() = default;

    const std::string& uri() const { return m_uriName; }
    void uri(std::string name) { m_uriName = std::move(name); }

    const std::string& method() const { return m_method; }

    void add_element(std::string key, std::string value);
    std::string get_element(const std::string& key) const;

    const std::string& body() const { return m_body; }
    const std::string& header() const { return m_header; }

    void parse_uri(const std::string& in);
    void parse_mime_header(const std::string& in);
    void dump() const;

private:
    std::string get_header(const std::string& in);
    std::string get_body(const std::string& in);

    std::unordered_map<std::string, std::string> m_tokenMap;
    std::string m_uriName;
    std::string m_header;
    std::string m_body;
    std::string m_method;
};

#endif // HTTP_PARSER_H
