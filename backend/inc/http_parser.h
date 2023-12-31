#ifndef __http_parser_h__
#define __http_parser_h__

#include <unordered_map>
#include <string>
#include <algorithm>
#include <sstream>

#include "ace/Log_Msg.h"

class Http {
  public:
    Http();
    Http(const std::string& in);
    ~Http();

    std::string uri() const {
      return(m_uriName);
    }

    void uri(std::string uriName) {
      m_uriName = uriName;
    }

    std::string method(const std::string& in) {
      std::stringstream ss(in);
      ss >> m_method;
      return(m_method);
    }

    std::string method() const {
      return(m_method);
    }

    void add_element(std::string key, std::string value) {
        m_tokenMap.insert(std::pair(key, value));
    }

    std::string get_element(const std::string key) {
        auto it = std::find_if(m_tokenMap.begin(), m_tokenMap.end(), [&](const std::pair<std::string, std::string>& in) ->bool {return (in.first == key);});
        if(it != m_tokenMap.end()) {
            return(it->second);
        }
        return std::string();
    }

    std::string body() {
      return m_body;
    }

    std::string header() {
      return m_header;
    }

    void parse_uri(const std::string& in);
    void parse_mime_header(const std::string& in);
    void dump(void) const;
    std::string get_body(const std::string& in);
    std::string get_header(const std::string& in);

  private:
    std::unordered_map<std::string, std::string> m_tokenMap;
    std::string m_uriName;
    std::string m_header;
    std::string m_body;
    std::string m_method;
};

#endif /* __http_parser_h__ */