#ifndef __http_parser_cc__
#define __http_parser_cc__

#include "http_parser.h"

Http::Http()
{
  m_uriName.clear();
  m_tokenMap.clear();
}

Http::Http(const std::string& in)
{
  m_uriName.clear();
  m_tokenMap.clear();
  m_header.clear();
  m_body.clear();

  m_header = get_header(in);

  if(m_header.length()) {
    parse_uri(m_header);
    parse_mime_header(m_header);
    //dump();
  }

  m_body = get_body(in);
}

Http::~Http()
{
  m_tokenMap.clear();
}

void Http::parse_uri(const std::string& in)
{
  std::string delim("\r\n");
  size_t offset = in.find(delim, 0);

  if(std::string::npos != offset) {
    /* Qstring */
    std::string req = in.substr(0, offset);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The request string is %s\n"), req.c_str()));
    std::stringstream input(req);
    std::string parsed_string;
    std::string param;
    std::string value;
    bool isQsPresent = false;

    parsed_string.clear();
    param.clear();
    value.clear();

    std::int32_t c;
    while((c = input.get()) != EOF) {
      switch(c) {
        case ' ':
        {
          std::int8_t endCode[4];
          endCode[0] = (std::int8_t)input.get();
          endCode[1] = (std::int8_t)input.get();
          endCode[2] = (std::int8_t)input.get();
          endCode[3] = (std::int8_t)input.get();

          std::string p((const char *)endCode, 4);

          if(!p.compare("HTTP")) {

            if(!isQsPresent) {

              m_uriName = parsed_string;
              parsed_string.clear();

            } else {

              value = parsed_string;
              add_element(param, value);
            }
          } else {
            /* make available to stream to be get again*/
            input.unget();
            input.unget();
            input.unget();
            input.unget();
          }

          parsed_string.clear();
          param.clear();
          value.clear();
        }
          break;

        case '+':
        {
          parsed_string.push_back(' ');
        }
          break;

        case '?':
        {
          isQsPresent = true;
          m_uriName = parsed_string;
          parsed_string.clear();
        }
          break;

        case '&':
        {
          value = parsed_string;
          add_element(param, value);
          parsed_string.clear();
          param.clear();
          value.clear();
        }
          break;

        case '=':
        {
          param = parsed_string;
          parsed_string.clear();
        }
          break;

        case '%':
        {
          std::int8_t octalCode[3];
          octalCode[0] = (std::int8_t)input.get();
          octalCode[1] = (std::int8_t)input.get();
          octalCode[2] = 0;
          std::string octStr((const char *)octalCode, 3);
          std::int32_t ch = std::stoi(octStr, nullptr, 16);
          parsed_string.push_back(ch);
        }
          break;

        default:
        {
          parsed_string.push_back(c);
        }
          break;  
      }
    }
  }
}

void Http::parse_mime_header(const std::string& in)
{
  std::stringstream input(in);
  std::string param;
  std::string value;
  std::string parsed_string;
  std::string line_str;
  line_str.clear();

  /* getridof first request line 
   * GET/POST/PUT/DELETE <uri>?uriName[&param=value]* HTTP/1.1\r\n
   */
  std::getline(input, line_str);

  param.clear();
  value.clear();
  parsed_string.clear();

  /* iterating through the MIME Header of the form
   * Param: Value\r\n
   */
  while(!input.eof()) {
    line_str.clear();
    std::getline(input, line_str);
    std::stringstream _line(line_str);

    std::int32_t c;
    while((c = _line.get()) != EOF ) {
      switch(c) {
        case ':':
        {
          param = parsed_string;
          parsed_string.clear();
          /* getridof of first white space */
          c = _line.get();
          while((c = _line.get()) != EOF) {
            switch(c) {
              case '\r':
              case ' ':
                /* get rid of \r character */
                break;

              default:
                parsed_string.push_back(c);
                break;
            }
          }
          /* we hit the end of line */
          value = parsed_string;
          add_element(param, value);
          parsed_string.clear();
          param.clear();
          value.clear();
        }
          break;

        default:
          parsed_string.push_back(c);
          break;
      }
    }
  }
}

void Http::dump(void) const 
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The uriName is %s\n"), m_uriName.c_str()));
    for(auto& in: m_tokenMap) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l param %s value %s\n"), in.first.c_str(), in.second.c_str()));
    }

}

std::string Http::get_header(const std::string& in)
{

  if(std::string::npos != in.find("Content-Type: application/json")) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The content Type is application/json\n")));
    std::string body_delimeter("\r\n\r\n");
    size_t body_offset = in.find(body_delimeter.c_str(), 0, body_delimeter.length());
    if(std::string::npos != body_offset) {
      body_offset += body_delimeter.length();
      std::string document = in.substr(0, body_offset);

      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The header is %s\n"), document.c_str()));
      return(document);
    }
  }
  return(std::string(in));
}

std::string Http::get_body(const std::string& in)
{
  std::string ct = get_element("Content-Type");
  std::string contentLen = get_element("Content-Length");
  std::string body_delimeter("\r\n\r\n");
  std::string ty("application/json");

  if(ct.length() && !ct.compare("application/json") && contentLen.length()) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The content Type is application/json CL %d hdrlen %d\n"), std::stoi(contentLen), header().length()));

    size_t body_offset = in.find(body_delimeter.c_str(), 0, body_delimeter.length());

    if(std::string::npos != body_offset) {
      std::string bdy(in.substr((body_delimeter.length() + body_offset), std::stoi(contentLen)));
      //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Bodylen is %d The BODY is \n%s\n"), bdy.length(), bdy.c_str()));

      if(contentLen.length() && (in.length() == header().length() + std::stoi(contentLen))) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Bodylen is %d The BODY is \n%s\n"), bdy.length(), bdy.c_str()));
        return(bdy);
      }
    }

    return(std::string());

    #if 0
    std::string body_delimeter("\r\n\r\n");
    size_t body_offset = in.find(body_delimeter, 0);
    if(std::string::npos != body_offset) {
      body_offset += body_delimeter.length();
      std::string document = in.substr(body_offset);

      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The body is %s\n"), document.c_str()));
      return(document);
    }
    #endif
  }
  return(std::string());
}

#endif /* __http_parser_cc__ */
