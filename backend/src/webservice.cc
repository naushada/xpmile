#include "webservice.h"
#include "emailservice.hpp"
#include "http_parser.h"
#include "json.hpp"

using json = nlohmann::json;

/**
 * @brief This member function processes the DELETE for a given uri.
 *
 * @param in http request with MIME header
 * @param dbInst instance of mongodb driver
 * @return std::string
 */
std::string MicroService::handle_DELETE(std::string &in,
                                        MongodbClient &dbInst) {
  Http http(in);

  /* Action based on uri in get request */
  std::string uri(http.uri());
  std::string document("");

  if (!uri.compare("/api/v1/shipment/awblist")) {
    /** Delete Shipment */
    std::string coll("shipping");
    std::string awbNo = http.get_element("awbList");
    std::string startDate = http.get_element("startDate");
    std::string endDate = http.get_element("endDate");
    if (awbNo.length()) {
      // awbList contains value with comma seperated and converting into an
      // array
      std::string lst("[");
      std::string delim = ",";
      auto start = 0U;
      auto end = awbNo.find(delim);
      while (end != std::string::npos) {
        lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
        start = end + delim.length();
        end = awbNo.find(delim, start);
      }
      lst += "\"" + awbNo.substr(start) + "\"";
      lst += "]";

      document = "{\"shipmentNo\": {\"$in\" : " + lst + "}}";

    } else if (startDate.length() && endDate.length()) {
      // deleting awb based on start & end date - bulk delete
      document = "{\"createdOn\": {\"$gte\" : \"" + startDate + "\"," +
                 "\"$lte\" :\"" + endDate + "\"" + "}}";
    } else {

      std::string err("400 Bad Request");
      std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid "
                              "AWB Bill No.\", \"error\" : 400}");
      return (build_responseERROR(err_message, err));
    }

    bool rsp = dbInst.delete_document(coll, document);

    if (rsp) {
      std::string r("");
      r = "{\"status\": \"success\"}";
      return (build_responseOK(r));
    }
  }

  std::string err("400 Bad Request");
  std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB "
                          "Bill No.\", \"error\" : 400}");
  return (build_responseERROR(err_message, err));
}

std::int32_t MicroService::process_request(ACE_HANDLE handle,
                                           ACE_Message_Block &mb,
                                           MongodbClient &dbInst) {
  std::string http_header, http_body;
  http_header.clear();
  http_body.clear();
  std::string rsp;

  std::string req(mb.rd_ptr(), mb.length());

  if (std::string::npos != req.find("OPTIONS", 0)) {
    rsp = handle_OPTIONS(req);
  } else if (std::string::npos != req.find("GET", 0)) {
    rsp = handle_GET(req, dbInst);
  } else if (std::string::npos != req.find("POST", 0)) {
    rsp = handle_POST(req, dbInst);
  } else if (std::string::npos != req.find("PUT", 0)) {
    rsp = handle_PUT(req, dbInst);
  } else if (std::string::npos != req.find("DELETE", 0)) {
    rsp = handle_DELETE(req, dbInst);
  } else {
    /* Not supported Method */
  }

  std::int32_t ret = 0;
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l the response length is %d\n"),
             rsp.length()));

  std::int32_t toBeSent = rsp.length();
  std::int32_t offset = 0;
  do {
    ret = send(handle, (rsp.c_str() + offset), (toBeSent - offset), 0);

    if (ret < 0) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l sent to peer is failed\n")));
      break;
    }

    offset += ret;
    ret = 0;

  } while ((toBeSent != offset));

  return (ret);
}

/**
 * @brief This member function starts processing the incoming HTTP request and
 * based on HTTP method it calls respective member function.
 *
 * @param handle socker descriptor on which HTTP request is received
 * @param req HTTP request with MIME header
 * @param dbInst An instance of mongodb driver
 * @return std::int32_t
 */
std::int32_t MicroService::process_request(ACE_HANDLE handle, std::string &req,
                                           MongodbClient &dbInst) {
  std::string rsp("");
  std::int32_t ret = 0;

  if (std::string::npos != req.find("OPTIONS", 0)) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l OPTIONS request:%s\n"),
               req.c_str()));
    rsp = handle_OPTIONS(req);

  } else if (std::string::npos != req.find("GET", 0)) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l GET request:%s\n"),
               req.c_str()));
    rsp = handle_GET(req, dbInst);

  } else if (std::string::npos != req.find("POST", 0)) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l POST request:%s\n"),
               req.c_str()));
    rsp = handle_POST(req, dbInst);

  } else if (std::string::npos != req.find("PUT", 0)) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l PUT request:%s\n"),
               req.c_str()));
    rsp = handle_PUT(req, dbInst);

  } else if (std::string::npos != req.find("DELETE", 0)) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l DELETE request:%s\n"),
               req.c_str()));
    rsp = handle_DELETE(req, dbInst);

  } else {
    ACE_DEBUG(
        (LM_DEBUG,
         ACE_TEXT(
             "%D [worker:%t] %M %N:%l Method is not supported request:%s\n"),
         req.c_str()));
    /* Not supported Method */
    return (ret);
  }

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l the response length:%d\n"),
             rsp.length()));

  std::int32_t toBeSent = rsp.length();
  std::int32_t offset = 0;
  do {
    ret = send(handle, (rsp.c_str() + offset), (toBeSent - offset), 0);

    if (ret < 0) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l sent to peer is failed\n")));
      break;
    }

    offset += ret;
    ret = 0;

  } while ((toBeSent != offset));

  return (ret);
}

std::string MicroService::get_contentType(std::string ext) {
  std::string cntType("");
  /* get the extension now for content-type */
  if (!ext.compare("woff")) {
    cntType = "font/woff";
  } else if (!ext.compare("woff2")) {
    cntType = "font/woff2";
  } else if (!ext.compare("ttf")) {
    cntType = "font/ttf";
  } else if (!ext.compare("otf")) {
    cntType = "font/otf";
  } else if (!ext.compare("css")) {
    cntType = "text/css";
  } else if (!ext.compare("js")) {
    cntType = "text/javascript";
  } else if (!ext.compare("eot")) {
    cntType = "application/vnd.ms-fontobject";
  } else if (!ext.compare("html")) {
    cntType = "text/html";
  } else if (!ext.compare("svg")) {
    cntType = "image/svg+xml";
  } else if (!ext.compare("gif")) {
    cntType = "image/gif";
  } else if (!ext.compare("png")) {
    cntType = "image/png";
  } else if (!ext.compare("ico")) {
    cntType = "image/vnd.microsoft.icon";
  } else if (!ext.compare("jpg")) {
    cntType = "image/jpeg";
  } else if (!ext.compare("json")) {
    cntType = "application/json";
  } else {
    cntType = "text/html";
  }
  return (cntType);
}

/**
 * @brief This member function is used to create document in a collection for a
 * given uri.
 *
 * @param in
 * @param dbInst
 * @return std::string
 */
std::string MicroService::handle_POST(std::string &in, MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_POST(in, dbInst));

  } else if (!uri.compare(0, 14, "/api/v1/config")) {
    return (handle_config_POST(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_POST(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_POST(in, dbInst));

  } else if (!uri.compare(0, 16, "/api/v1/document")) {
    return (handle_document_POST(in, dbInst));

  } else if (!uri.compare(0, 13, "/api/v1/email")) {
    return (handle_email_POST(in, dbInst));

  } else {
    return (build_responseOK(std::string()));
  }
}

std::string MicroService::handle_config_POST(std::string &in,
                                             MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/config/db")) {
    std::string content = http.body();

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"),
                 content.length()));
      std::string ip_address("");
      auto result =
          dbInst.from_json_element_to_string(content, "ip_address", ip_address);
      std::string port("");
      result = dbInst.from_json_element_to_string(content, "port", port);
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l dbconfig ip:%s port:%u\n"),
                 ip_address.c_str(), std::stoul(port)));
      /* Apply this config if changed */
    }
  }

  return (std::string());
}

std::string MicroService::handle_shipment_POST(std::string &in,
                                               MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/shipment/shipping")) {
    std::string collectionName("shipping");

    /*We need newly shipment No. */
    std::string projection("{\"_id\" : false, \"shipmentNo\" : true}");
    std::string content = http.body();
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d "
                        "\n Request http_body:%s\n"),
               content.length(), content.c_str()));

    if (content.length()) {

      std::string record = dbInst.create_document(dbInst.get_database(),
                                                  collectionName, content);

      if (record.length()) {
        std::string rsp("");
        rsp = "{\"oid\" : \"" + record + "\"}";
        return (build_responseOK(rsp));
      }
    }

  } else if (!uri.compare("/api/v1/shipment/bulk/shipping")) {
    std::string content = http.body();
    std::string coll("shipping");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"),
                 content.length()));

      std::int32_t cnt =
          dbInst.create_bulk_document(dbInst.get_database(), coll, content);

      if (cnt) {
        std::string rec = "{\"createdShipments\": " + std::to_string(cnt) + "}";
        return (build_responseOK(rec));
      } else {
        std::string err("400 Bad Request");
        std::string err_message(
            "{\"status\" : \"faiure\", \"cause\" : \"Bulk Shipment Creation is "
            "failed\", \"errorCode\" : 400}");
        return (build_responseERROR(err_message, err));
      }
    }

  } else if (!uri.compare("/api/v1/shipment/thirdparty/ajoul")) {
    // std::string
    // req("{\"Shipment\":{\"reference\":\"AB100\",\"pickup_date\":null,\"pickup_time\":null,\"product_type\":\"104\",\"product_price\":null,\"destination\":\"RUH\",\"origin\":\"RUH\",\"parcel_quantity\":\"2\",\"parcel_weight\":\"4\",\"payment_mode\":\"COD\",\"service_id\":\"2\",\"description\":\"Testing
    // Create Shipment From
    // API\",\"sku\":\"423423\",\"customer_lng\":null,\"customer_lat\":null,\"sender\":{\"name\":\"Alaa\",\"address\":\"Al
    // Haram street,
    // Giza\",\"zip_code\":null,\"phone\":\"01063396459\",\"email\":\"admin@quadratechsoft.com\"},\"receiver\":{\"name\":\"Alaa\",\"address\":\"AL
    // Malki,
    // Damascuss\",\"zip_code\":\"1234\",\"phone\":\"0941951819\",\"phone2\":\"09419518549\",\"email\":\"info@quadratechsoft.com\"}},\"TrackingNumber\":\"AR222188000614391\",\"printLable\":\"https:\/\/ajoul.com\/printlabelone\/AR222188000614391\"}");
    // std::string awbNo = dbInst.get_value(req, "TrackingNumber");
    // ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l TrackingNumber %s
    // \n"), awbNo.c_str()));
    std::stringstream header("");
    // header = "Connection: close\r\n"
    //          "Cache-Control: no-cache\r\n";
    //  "Content-Type: multipart/form-data;
    //  boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

    std::stringstream apiAuthorizeAjoul("");

    apiAuthorizeAjoul
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"client_secret\"\r\n\r\n"
        << "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"client_id\"\r\n\r\n"
        << "34\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"username\"\r\n\r\n"
        << "AKjHYuCAco\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"password\"\r\n\r\n"
        << "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
    /*
    apiAuthorizeAjoul <<
    "client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o"
                      <<
    "&client_id=34&username=AKjHYuCAco&password=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n";*/

    header << "Host: www.ajoul.com\r\n"
           << "Accept: application/json\r\n"
           << "User-Agent: Balaagh/1.0\r\n"
           << "Connection: keep-alive\r\n"
           << "Cache-Control: no-cache\r\n"
           << "Content-Type: application/x-www-form-urlencoded\r\n"
           << "Content-Type: multipart/form-data; "
              "boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
           //<<  "Content-Length: " << apiAuthorizeAjoul.str().length()
           << "Content-Length: 0" << "\r\n";
    //<<  "Content-Type: multipart/form-data;
    // boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

    // header << "\r\n";

    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l the header is\n%s\n"),
               header.str().c_str()));

    std::string apiURLAjoul = "https://ajoul.com/remote/api/v1/authorize";
    ACE_SSL_SOCK_Connector client;
    ACE_SSL_SOCK_Stream conn;
    ACE_INET_Addr connectAddr("ajoul.com:443");
    ACE_Time_Value to(2, 0);

    if (client.connect(conn, connectAddr, &to) < 0) {

      ACE_ERROR(
          (LM_ERROR,
           ACE_TEXT(
               "%D [worker:%t] %M %N:%l connect to ajoul:443 is failed\n")));
      std::string err("400 Bad Request");
      std::string err_message(
          "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not "
          "rechable\", \"errorCode\" : 400}");
      return (build_responseERROR(err_message, err));

    } else {

      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l Connect to "
                          "https://ajoul.com (%u) - %s is success\n"),
                 connectAddr.get_ip_address(), connectAddr.get_host_addr()));

      std::stringstream postReq("");
      postReq
          << "POST "
             "/remote/api/v1/"
             "authorize?client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o&"
             "client_id=34&username=AKjHYuCAco&password="
             "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o HTTP/1.1\r\n"
          << header.str() << "\r\n";
      //<< apiAuthorizeAjoul.str();

      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l the request is\n%s\n"),
                 postReq.str().c_str()));

      if (conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

        ACE_ERROR((
            LM_ERROR,
            ACE_TEXT("%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
        std::string err("400 Bad Request");
        std::string err_message(
            "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not "
            "responding\", \"errorCode\" : 400}");
        return (build_responseERROR(err_message, err));

      } else {

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to "
                                      "https://ajoul.com is success\n")));

        std::array<std::uint8_t, 3048> authRsp;
        authRsp.fill(0);
        ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);

        if (len < 0) {
          ACE_ERROR(
              (LM_ERROR,
               ACE_TEXT(
                   "%D [worker:%t] %M %N:%l recv from ajoul:443 is failed\n")));
          std::string err("400 Bad Request");
          std::string err_message(
              "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is "
              "not responding to Authorize req\", \"errorCode\" : 400}");
          return (build_responseERROR(err_message, err));

        } else {
          std::string rsp((char *)authRsp.data(), len);
          ACE_DEBUG((LM_DEBUG,
                     ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"),
                     rsp.c_str()));
          Http http(rsp);
          std::string access_token =
              dbInst.get_access_token_for_ajoul(http.body());
          ACE_DEBUG(
              (LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l The access_token is - %s\n"),
               access_token.c_str()));

          /* Now building request for creating shipment */
          std::stringstream shipmentCreate("");
          std::stringstream hdr("");

          shipmentCreate
              << "{" << "\"receiver\":{" << "\"name\":\"Alaa\","
              << "\"country_code\": \"SA\"," << "\"city_code\": \"RUH\","
              << "\"address\": \"AL Malki, Damascuss\","
              << "\"zip_code\": \"1234\"," << "\"phone\": \"0941951819\","
              << "\"phone2\": \"09419518549\","
              << "\"email\": \"info@quadratechsoft.com\"" << "},"
              << "\"sender\": {" << "\"name\": \"Alaa\","
              << "\"country_code\": \"SA\"," << "\"city_code\": \"RUH\","
              << "\"address\": \"Al Haram street, Giza\","
              << "\"phone\": \"01063396459\","
              << "\"email\": \"admin@quadratechsoft.com\"" << "},"
              << "\"reference\": \"AB100\"," << "\"pick_date\": \"2018-08-06\","
              << "\"pickup_time\": \"12:49\"," << "\"product_type\": \"104\","
              << "\"payment_mode\": \"COD\"," << "\"parcel_quantity\": \"2\","
              << "\"parcel_weight\": \"4\"," << "\"service_id\": \"2\","
              << "\"description\": \"Testing Create Shipment From API\","
              << "\"sku\": \"423423\"," << "\"weight_total\": \"20\","
              << "\"total_cod_amount\": 50.9" << "}";
          hdr << "Host: www.ajoul.com\r\n"
              << "Accept: application/json\r\n"
              << "User-Agent: Balaagh/1.0\r\n"
              << "Connection: keep-alive\r\n"
              << "Cache-Control: no-cache\r\n"
              << "Content-Type: application/json\r\n"
              << "Authorization: Bearer " << access_token << "\r\n"
              << "Content-Length: " << shipmentCreate.str().length()
              << "\r\n\r\n"
              << shipmentCreate.str();
          postReq.str("");
          postReq << "POST /remote/api/v1/shipment/create HTTP/1.1\r\n"
                  << hdr.str();
          if (conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

            ACE_ERROR(
                (LM_ERROR,
                 ACE_TEXT(
                     "%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
            std::string err("400 Bad Request");
            std::string err_message(
                "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is "
                "not responding\", \"errorCode\" : 400}");
            return (build_responseERROR(err_message, err));

          } else {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to "
                                          "https://ajoul.com is success\n")));

            std::array<std::uint8_t, 3048> authRsp;
            authRsp.fill(0);
            ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);
            std::string rsp((char *)authRsp.data(), len);
            ACE_DEBUG((LM_DEBUG,
                       ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"),
                       rsp.c_str()));
            Http http(rsp);
            std::string ref("");
            std::string awbNo =
                dbInst.get_tracking_no_for_ajoul(http.body(), ref);
            ACE_DEBUG((LM_DEBUG,
                       ACE_TEXT("%D [worker:%t] %M %N:%l The Tracking Number "
                                "is - %s refNumber %s\n"),
                       awbNo.c_str(), ref.c_str()));
          }
          return (build_responseOK(http.body()));
        }
      }
    }
  }
  return (std::string());
}

std::string MicroService::handle_account_POST(std::string &in,
                                              MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/account/account")) {
    std::string collectionName("account");
    /*We need newly created account Code */
    std::string projection("{\"_id\" : false, \"accountCode\" : true}");
    std::string content = http.body();
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d "
                        "\n Request http_body:%s\n"),
               content.length(), content.c_str()));

    if (content.length()) {
      std::string oid = dbInst.create_document(dbInst.get_database(),
                                               collectionName, content);

      if (oid.length()) {
        // std::string rsp = dbInst.get_byOID(collectionName, projection, oid);
        std::string rsp("");
        rsp = "{\"oid\" : \"" + oid + "\"}";

        return (build_responseOK(rsp));
      }
    }
  }
  return (std::string());
}

std::string MicroService::handle_inventory_POST(std::string &in,
                                                MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/inventory")) {
    /* Creating sku for inventory */
    std::string content = http.body();
    std::string coll("inventory");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"),
                 content.length()));
      std::string record =
          dbInst.create_document(dbInst.get_database(), coll, content);

      if (record.length()) {
        std::string rsp("");
        rsp = "{\"oid\" : \"" + record + "\"}";
        return (build_responseOK(rsp));
      }
    }
  }
  return (std::string());
}

std::string MicroService::handle_document_POST(std::string &in,
                                               MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/document")) {
    std::string content = http.body();
    std::string coll("attachment");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"),
                 content.length()));
      std::string value("");
      auto result =
          dbInst.from_json_element_to_string(content, "corporate", value);

      if (result) {
        coll.clear();
        coll = value + "_attachment";
      }

      std::string record =
          dbInst.create_document(dbInst.get_database(), coll, content);

      if (record.length()) {
        std::string rsp("");
        rsp = "{\"oid\" : \"" + record + "\"}";
        return (build_responseOK(rsp));

      } else {
        std::string err("400 Bad Request");
        std::string err_message(
            "{\"status\" : \"faiure\", \"cause\" : \"attachment upload "
            "failed\", \"errorCode\" : 400}");
        return (build_responseERROR(err_message, err));
      }
    }
  }
  return (std::string());
}

std::string MicroService::handle_email_POST(std::string &in,
                                            MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/email")) {
    /* Send e-mail with POST request */
    // {"subject": "", "to": [user-id@domain.com, user-id1@domain.com], "body":
    // ""}
    std::string json_body = http.body();
    std::vector<std::string> out_vec;
    std::vector<std::tuple<std::string, std::string>> out_list;
    std::string subj;
    std::string body;
    std::string from;
    std::string passwd;

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email request:%s\n"),
               json_body.c_str()));
    dbInst.from_json_array_to_vector(json_body, "to", out_vec);
    dbInst.from_json_element_to_string(json_body, "subject", subj);
    dbInst.from_json_element_to_string(json_body, "emailbody", body);
    dbInst.from_json_object_to_map(json_body, "files", out_list);
    dbInst.from_json_element_to_string(json_body, "from", from);
    dbInst.from_json_element_to_string(json_body, "passwd", passwd);

    for (const auto &elm : out_vec) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l email to list:%s\n"),
                 elm.c_str()));
    }
    ACE_DEBUG(
        (LM_DEBUG,
         ACE_TEXT(
             "%D [worker:%t] %M %N:%l email subject:%s from:%s passwd:%s\n"),
         subj.c_str(), from.c_str(), passwd.c_str()));
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email body:%s\n"),
               body.c_str()));

    SMTP::Account::instance().to_email(out_vec);
    SMTP::Account::instance().email_subject(subj);
    SMTP::Account::instance().email_body(body);
    SMTP::Account::instance().from_email(from);
    SMTP::Account::instance().from_password(passwd);

    if (!out_list.empty()) {
      /* e-mail with attachment */
      SMTP::Account::instance().attachment(out_list);
    }

    SMTP::User email;
    email.startEmailTransaction();
    std::string rsp("{\"status\": \"success\"");
    return (build_responseOK(rsp));
  }
  return (std::string());
}

std::string MicroService::handle_GET(std::string &in, MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);

  /* Action based on uri in get request */
  std::string uri(http.uri());
  if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_GET(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_GET(in, dbInst));

  } else if (!uri.compare(0, 16, "/api/v1/document")) {
    return (handle_document_GET(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_GET(in, dbInst));

  } else if ((!uri.compare(0, 7, "/webui/"))) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l frontend Request %s\n"),
               uri.c_str()));
    /* build the file name now */
    std::string fileName("");
    std::string ext("");

    std::size_t found = uri.find_last_of(".");
    if (found != std::string::npos) {
      ext = uri.substr((found + 1), (uri.length() - found));
      fileName = uri.substr(6, (uri.length() - 6));
      std::string newFile = "../webgui/webui/" + fileName;
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The "
                          "extension is %s\n"),
                 newFile.c_str(), ext.c_str()));
      /* Open the index.html file and send it to web browser. */
      std::ifstream ifs(newFile.c_str());
      std::stringstream _str("");

      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));
        std::string cntType("");
        cntType = get_contentType(ext);

        _str << ifs.rdbuf();
        ifs.close();
        return (build_responseOK(_str.str(), cntType));
      }
    } else {
      std::string newFile = "../webgui/webui/index.html";
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"),
                 newFile.c_str()));
      /* Open the index.html file and send it to web browser. */
      std::ifstream ifs(newFile.c_str(), std::ios::binary);
      std::stringstream _str("");
      std::string cntType("");

      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));

        cntType = "text/html";
        _str << ifs.rdbuf();
        ifs.close();

        return (build_responseOK(_str.str(), cntType));
      }
    }
  } else if (!uri.compare(0, 8, "/assets/")) {
    std::string ext;
    std::size_t found = uri.find_last_of(".");
    if (found != std::string::npos) {
      ext = uri.substr(found + 1);
      std::string newFile = "../webgui/webui" + uri;
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The "
                          "extension is %s\n"),
                 newFile.c_str(), ext.c_str()));
      std::ifstream ifs(newFile.c_str(), std::ios::binary);
      std::stringstream _str;
      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));
        _str << ifs.rdbuf();
        return (build_responseOK(_str.str(), get_contentType(ext)));
      }
    }

  } else if (!uri.compare(0, 1, "/")) {
    std::string newFile = "../webgui/webui/index.html";
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"),
               newFile.c_str()));
    std::ifstream ifs(newFile.c_str(), std::ios::binary);
    std::stringstream _str;
    if (ifs.is_open()) {
      ACE_DEBUG((
          LM_DEBUG,
          ACE_TEXT(
              "%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"),
          uri.c_str()));
      _str << ifs.rdbuf();
      return (build_responseOK(_str.str(), "text/html"));
    }
  }

  return (build_responseOK(std::string()));
}

std::string MicroService::handle_shipment_GET(std::string &in,
                                              MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/shipment/shipping")
    return {};

  const std::string collection("shipping");
  const json projection = {{"_id", false}};

  // Convert "a,b,c" to a JSON array ["a","b","c"]
  auto csv_to_array = [](const std::string &csv) {
    json arr = json::array();
    std::size_t start = 0, end;
    while ((end = csv.find(',', start)) != std::string::npos) {
      arr.push_back(csv.substr(start, end - start));
      start = end + 1;
    }
    arr.push_back(csv.substr(start));
    return arr;
  };

  // Query the collection and return the appropriate HTTP response
  auto fetch_and_respond = [&](const json &doc, const std::string &cause) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l query:%s\n"),
               doc.dump().c_str()));
    std::string record =
        dbInst.get_documents(collection, doc.dump(), projection.dump());
    if (!record.empty())
      return build_responseOK(record);
    json err = {{"status", "failure"}, {"cause", cause}, {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  };

  const auto awbNo = http.get_element("awbNo");
  const auto altRefNo = http.get_element("altRefNo");
  const auto senderRefNo = http.get_element("senderRefNo");
  const auto accountCode = http.get_element("accountCode");
  const auto fromDate = http.get_element("fromDate");
  const auto toDate = http.get_element("toDate");
  const auto country = http.get_element("country");

  if (!awbNo.empty()) {
    json doc = {{"shipment.awbno", {{"$in", csv_to_array(awbNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid AWB Bill No.");
  }

  if (!altRefNo.empty()) {
    json doc = {{"altRefNo", {{"$in", csv_to_array(altRefNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid ALT REF No.");
  }

  if (!senderRefNo.empty()) {
    json doc = {{"senderRefNo", {{"$in", csv_to_array(senderRefNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid Sender REF No.");
  }

  if (!fromDate.empty() && !toDate.empty()) {
    json doc = {{"shipment.shipmentInformation.createdOn",
                 {{"$gte", fromDate}, {"$lte", toDate}}}};
    if (!accountCode.empty())
      doc["shipment.senderInformation.accountNo"] = {
          {"$in", csv_to_array(accountCode)}};
    if (!country.empty())
      doc["shipment.receiverInformation.country"] = country;
    return fetch_and_respond(doc, "Invalid input for detailed report.");
  }

  return {};
}

std::string MicroService::handle_account_GET(std::string &in,
                                             MongodbClient &dbInst) {
  Http http(in);
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request uri:%s\n"),
             http.uri().c_str()));

  if (http.uri() != "/api/v1/account/account")
    return {};

  const std::string collection("account");
  const json projection = {{"_id", false}};

  // Fetch a single document; return OK on hit, ERROR with given status on miss
  auto fetch_one = [&](const json &doc, const std::string &http_err,
                       const json &err_body) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l query:%s\n"),
               doc.dump().c_str()));
    std::string record =
        dbInst.get_document(collection, doc.dump(), projection.dump());
    if (!record.empty())
      return build_responseOK(record);
    return build_responseERROR(err_body.dump(), http_err);
  };

  const auto userId = http.get_element("userId");
  const auto pwd = http.get_element("password");
  const auto accCode = http.get_element("accountCode");

  if (!userId.empty() && !pwd.empty()) {
    json doc = {{"loginCredentials.accountCode", userId},
                {"loginCredentials.accountPassword", pwd}};
    json err = {{"status", "failure"},
                {"cause", "Invalid Credentials"},
                {"error", 404}};
    return fetch_one(doc, "400 Bad Request", err);
  }

  if (!accCode.empty()) {
    json doc = {{"loginCredentials.accountCode", accCode}};
    json err = {{"status", "failure"},
                {"cause", "Invalid Account Code"},
                {"error", 400}};
    return fetch_one(doc, "400 Bad Request", err);
  }

  // No filter: return the full account list
  std::string record = dbInst.get_documents(collection, projection.dump());
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l account_list:%s\n"),
             record.c_str()));
  if (record.empty()) {
    json err = {{"status", "failure"},
                {"cause", "There's no customer record"},
                {"error", 404}};
    return build_responseERROR(err.dump(), "404 Not Found");
  }
  return build_responseOK(record);
}

std::string MicroService::handle_inventory_GET(std::string &in,
                                               MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/inventory")
    return {};

  const json projection = {{"_id", false}};
  const auto sku = http.get_element("sku");
  const auto accCode = http.get_element("accountCode");

  // Build filter from whichever params are present; empty doc fetches all
  json doc = json::object();
  if (!accCode.empty())
    doc["accountCode"] = accCode;
  if (!sku.empty())
    doc["sku"] = sku;

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Inventory query:%s\n"),
             doc.dump().c_str()));
  std::string record =
      dbInst.get_documents("inventory", doc.dump(), projection.dump());

  if (!record.empty())
    return build_responseOK(record);

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l No Record found\n")));
  json err = {{"status", "failure"},
              {"cause", "There's no Inventory Record"},
              {"error", 404}};
  return build_responseERROR(err.dump(), "404 Not Found");
}

std::string MicroService::handle_email_GET(std::string &in,
                                           MongodbClient &dbInst) {
  ACE_UNUSED_ARG(in);
  ACE_UNUSED_ARG(dbInst);
  return {};
}

std::string MicroService::handle_document_GET(std::string &in,
                                              MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/document")
    return {};

  const json projection = {{"_id", false}};
  const auto collection = http.get_element("corporate");
  const auto userId = http.get_element("userId");
  const auto fileName = http.get_element("file");

  json doc = {{"corporate", collection}};
  if (!userId.empty()) {
    doc["userId"] = userId;
    doc["file"] = fileName;
  }

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l attachment query:%s\n"),
             doc.dump().c_str()));
  std::string record =
      dbInst.get_documents(collection, doc.dump(), projection.dump());

  if (!record.empty())
    return build_responseOK(record);
  json err = {
      {"status", "failure"}, {"cause", "Document not found"}, {"error", 404}};
  return build_responseERROR(err.dump(), "404 Not Found");
}

std::string MicroService::handle_config_GET(std::string &in,
                                            MongodbClient &dbInst) {
  ACE_UNUSED_ARG(in);
  ACE_UNUSED_ARG(dbInst);
  return {};
}

/**
 * @brief this member function is used to Update the collection for a given uri.
 *
 * @param in
 * @param dbInst
 * @return std::string
 */
std::string MicroService::handle_PUT(std::string &in, MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);

  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_PUT(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_PUT(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_PUT(in, dbInst));

  } else {
    std::string err("400 Bad Request");
    std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment "
                            "Updated Failed\", \"error\" : 400}");
    return (build_responseERROR(err_message, err));
  }
}

std::string MicroService::handle_shipment_PUT(std::string &in,
                                              MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/shipment/shipping")
    return {};

  const std::string coll("shipping");
  const auto content = http.body();
  const auto awbNo = http.get_element("shipmentNo");
  const auto accCode = http.get_element("accountCode");
  const auto isSingleShipment = http.get_element("isSingleShipment");

  const json not_pod = {{"$ne", "Proof of Delivery"}};

  // Execute update and return success or error response
  auto do_update = [&](const json &query, const json &doc) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
               doc.dump().c_str(), query.dump().c_str()));
    if (dbInst.update_collection(coll, query.dump(), doc.dump()))
      return build_responseOK(json{{"status", "success"}}.dump());
    json err = {{"status", "failure"},
                {"cause", "Shipment Update Failed"},
                {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  };

  if (!isSingleShipment.empty()) {
    // Single-awb: exact match on awbno, $set the body
    json query = {{"shipment.awbno", awbNo},
                  {"shipment.shipmentInformation.activity.event", not_pod}};
    if (!accCode.empty())
      query["accountCode"] = accCode;
    return do_update(query, {{"$set", json::parse(content)}});
  }

  // Multi-awb: $in list from csv, $push activity
  auto csv_to_array = [](const std::string &csv) {
    json arr = json::array();
    std::size_t start = 0, end;
    while ((end = csv.find(',', start)) != std::string::npos) {
      arr.push_back(csv.substr(start, end - start));
      start = end + 1;
    }
    arr.push_back(csv.substr(start));
    return arr;
  };

  json query = {{"shipment.awbno", {{"$in", csv_to_array(awbNo)}}},
                {"shipment.shipmentInformation.activity.event", not_pod}};
  if (!accCode.empty())
    query["accountCode"] = accCode;
  return do_update(
      query,
      {{"$push",
        {{"shipment.shipmentInformation.activity", json::parse(content)}}}});
}

std::string MicroService::handle_inventory_PUT(std::string &in,
                                               MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/inventory")
    return {};

  const auto sku = http.get_element("sku");
  const auto qty = http.get_element("qty");
  const auto acc = http.get_element("accountCode");
  const auto isUpdate = http.get_element("isUpdate");

  json query = json::object();
  if (!sku.empty())
    query["sku"] = sku;
  if (!acc.empty())
    query["accountCode"] = acc;

  const int delta = isUpdate.empty() ? -std::stoi(qty) : std::stoi(qty);
  json doc = {{"$inc", {{"qty", delta}}}};

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
             doc.dump().c_str(), query.dump().c_str()));
  if (dbInst.update_collection("inventory", query.dump(), doc.dump()))
    return build_responseOK(json{{"status", "success"}}.dump());

  json err = {{"status", "failure"},
              {"cause", "Inventory Update Failed"},
              {"error", 400}};
  return build_responseERROR(err.dump(), "400 Bad Request");
}

std::string MicroService::handle_account_PUT(std::string &in,
                                             MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/account/account")
    return {};

  const auto content = http.body();
  const auto accCode = http.get_element("userId");

  json query = json::object();
  if (!accCode.empty())
    query = {{"loginCredentials.accountCode", accCode}};

  json doc = {{"$set", json::parse(content)}};

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
             doc.dump().c_str(), query.dump().c_str()));
  if (dbInst.update_collection("account", query.dump(), doc.dump()))
    return build_responseOK(json{{"status", "success"}}.dump());

  json err = {{"status", "failure"},
              {"cause", "Account Update Failed"},
              {"error", 400}};
  return build_responseERROR(err.dump(), "400 Bad Request");
}

std::string MicroService::handle_OPTIONS(std::string &in) {
  ACE_UNUSED_ARG(in);
  std::string http_header;
  http_header = "HTTP/1.1 200 OK\r\n";
  http_header +=
      "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n";
  http_header +=
      "Access-Control-Allow-Headers: DNT, User-Agent, X-Requested-With, "
      "If-Modified-Since, Cache-Control, Content-Type, Range\r\n";
  http_header += "Access-Control-Max-Age: 1728000\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";
  http_header += "Content-Type: text/plain; charset=utf-8\r\n";
  http_header += "Content-Length: 0\r\n";
  http_header += "\r\n\r\n";
  // ACE_Message_Block* rsp = nullptr;

  // ACE_NEW_RETURN(rsp, ACE_Message_Block(512), nullptr);

  // std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
  // rsp->wr_ptr(http_header.length());

  // ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d
  // response %s \n"), http_header.length(), http_header.c_str()));
  return (http_header);
}

std::string MicroService::build_responseCreated() {
  std::string http_header;
  http_header = "HTTP/1.1 201 Created\r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";
  http_header += "Content-Length: 0\r\n";
  http_header += "\r\n";

  // ACE_NEW_RETURN(rsp, ACE_Message_Block(256), nullptr);

  // std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
  // rsp->wr_ptr(http_header.length());

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"),
       http_header.length(), http_header.c_str()));
  return (http_header);
}

std::string MicroService::build_responseOK(std::string httpBody,
                                           std::string contentType) {
  std::string http_header;
  // ACE_Message_Block* rsp = nullptr;
  std::string rsp;

  http_header = "HTTP/1.1 200 OK\r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";

  if (httpBody.length()) {
    http_header +=
        "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
    http_header += "Content-Type: " + contentType + "\r\n";
    http_header += "\r\n";

  } else {
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n";
  }
  rsp = http_header;

  if (httpBody.length()) {
    rsp += httpBody;
  }

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"),
       (http_header.length() + httpBody.length()), http_header.c_str()));
  return (rsp);
}

std::string MicroService::build_responseERROR(std::string httpBody,
                                              std::string error) {
  std::string http_header;
  const std::string contentType("application/json");

  http_header = "HTTP/1.1 " + error + " \r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";

  if (httpBody.length()) {
    http_header +=
        "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
    http_header += "Content-Type: " + contentType + "\r\n";
    http_header += "\r\n";
    http_header += httpBody;
  } else {
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n";
  }

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"),
       http_header.length(), http_header.c_str()));
  return http_header;
}

ACE_INT32 MicroService::handle_signal(int signum, siginfo_t *s, ucontext_t *u) {
  ACE_UNUSED_ARG(s);
  ACE_UNUSED_ARG(u);
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l Micro service gets signal %d\n"),
             signum));
  m_continue = false;

  return (0);
}

int MicroService::open(void *arg) {
  ACE_UNUSED_ARG(arg);
  /*! Number of threads are 5, which is 2nd argument. */
  activate();
  return (0);
}

int MicroService::close(u_long flag) {
  ACE_UNUSED_ARG(flag);
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is closing\n")));
  return (0);
}

/*
 * @brief: This function is the entry point for Thread. Once the thread is
 * spawned, control comes here and It blocks on message queue. The thread is
 * termed as Worker.
 * @param: none
 * @return:
 */
int MicroService::svc() {
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is spawned\n")));

  webServer().semaphore().release();

  while (m_continue) {
    ACE_Message_Block *mb = nullptr;
    if (-1 != getq(mb)) {
      std::uint32_t offset = 0;

      switch (mb->msg_type()) {
      case ACE_Message_Block::MB_DATA: {
        ACE_DEBUG(
            (LM_DEBUG,
             ACE_TEXT(
                 "%D [worker:%t] %M %N:%l svc::ACE_Message_Block::MB_DATA\n")));
        std::string ss(mb->rd_ptr(), mb->length());
        mb->release();

        std::istringstream istrstr(ss);
        ACE_HANDLE handle;
        istrstr.read(reinterpret_cast<char *>(&handle), sizeof(ACE_HANDLE));
        std::uintptr_t inst;
        istrstr.read(reinterpret_cast<char *>(&inst), sizeof(std::uintptr_t));
        MongodbClient *dbInst = reinterpret_cast<MongodbClient *>(inst);
        istrstr.read(reinterpret_cast<char *>(&inst), sizeof(std::uintptr_t));
        WebServer *parent = reinterpret_cast<WebServer *>(inst);
        std::uint32_t len = 0;
        istrstr.read(reinterpret_cast<char *>(&len), sizeof(std::uint32_t));
        std::vector<char> str(len);
        istrstr.read(reinterpret_cast<char *>(str.data()), len);
        std::string request(str.begin(), str.end());
        process_request(handle, request, *dbInst);
        break;
      }
      case ACE_Message_Block::MB_PCSIG: {
        ACE_DEBUG(
            (LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Got MB_PCSIG \n")));
        m_continue = false;
        if (mb != nullptr) {
          mb->release();
        }
        msg_queue()->deactivate();
        webServer().semaphore().release();
        break;
      }
      default: {
        m_continue = false;
        mb->release();
        break;
      }
      }
    } else {
      ACE_ERROR(
          (LM_ERROR,
           ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is stopped\n")));
      // getq returned -1: mb was not set, do not release
      m_continue = false;
    }
  }

  return (0);
}

MicroService::MicroService(ACE_Thread_Manager *thr_mgr, WebServer *parent)
    : ACE_Task<ACE_MT_SYNCH>(thr_mgr) {
  m_continue = true;
  m_threadId = thr_mgr->thr_self();
  m_parent = parent;
}

MicroService::~MicroService() {
  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l Microservice dtor is invoked\n")));
  m_parent = nullptr;
}

/*
 * +--------------------------------------------------------------------------+
 * |                                                                          |
 * |  W   W  EEEEE  BBBBB   SSSSS  EEEEE  RRRR   V   V  EEEEE  RRRR         |
 * |  W   W  E      B    B  S      E      R   R   V   V  E      R   R        |
 * |  W W W  EEEE   BBBBB    SSS   EEEE   RRRR    V   V  EEEE   RRRR         |
 * |  W W W  E      B    B      S  E      R  R     V V   E      R  R          |
 * |   W W   EEEEE  BBBBB   SSSSS  EEEEE  R   R    V    EEEEE  R   R         |
 * |                                                                          |
 * +--------------------------------------------------------------------------+
 */

ACE_INT32 WebServer::handle_timeout(const ACE_Time_Value &tv, const void *act) {
  ACE_UNUSED_ARG(tv);
  ACE_UNUSED_ARG(act);
  return (0);
}

ACE_INT32 WebServer::handle_input(ACE_HANDLE handle) {
  ACE_UNUSED_ARG(handle);
  ACE_SOCK_Stream peerStream;
  ACE_INET_Addr peerAddr;

  if (m_server.accept(peerStream, &peerAddr) != 0) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l accept failed\n")));
    return (0);
  }

  const ACE_HANDLE fd = peerStream.get_handle();

  // The OS occasionally recycles a file descriptor whose pool entry was not
  // cleaned up (e.g. due to an earlier error path).  Nullify the stale
  // handler's handle before erasing so that its destructor does not call
  // close() on the fd we just accepted.
  auto it = m_connectionPool.find(fd);
  if (it != std::end(m_connectionPool)) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [Master:%t] %M %N:%l stale pool entry for "
                        "handle %d — removing before reuse\n"),
               fd));
    it->second->handle(ACE_INVALID_HANDLE);
    ACE_Reactor::instance()->remove_handler(
        fd, ACE_Event_Handler::READ_MASK | ACE_Event_Handler::SIGNAL_MASK |
                ACE_Event_Handler::DONT_CALL);
    m_connectionPool.erase(it);
  }

  auto connEnt = std::make_unique<WebConnection>(this);
  connEnt->handle(fd);
  connEnt->connAddr(peerAddr);

  ACE_Reactor::instance()->register_handler(connEnt.get(),
                                            ACE_Event_Handler::READ_MASK |
                                                ACE_Event_Handler::SIGNAL_MASK);

  m_connectionPool.emplace(fd, std::move(connEnt));

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l new connection handle:%d "
                      "peer %s:%d active:%zu\n"),
             fd, peerAddr.get_host_addr(), peerAddr.get_port_number(),
             m_connectionPool.size()));

  return (0);
}

ACE_INT32 WebServer::handle_signal(int signum, siginfo_t *s, ucontext_t *ctx) {
  ACE_UNUSED_ARG(s);
  ACE_UNUSED_ARG(ctx);

  ACE_ERROR((LM_ERROR,
             ACE_TEXT("%D [Master:%t] %M %N:%l signal %d (%S) received, "
                      "initiating shutdown\n"),
             signum, signum));

  // Unregister every active client connection from the reactor before
  // destroying the handlers, preventing dangling raw pointer access.
  for (auto &[fd, conn] : m_connectionPool) {
    ACE_Reactor::instance()->remove_handler(
        fd, ACE_Event_Handler::READ_MASK | ACE_Event_Handler::SIGNAL_MASK |
                ACE_Event_Handler::DONT_CALL);
  }
  m_connectionPool.clear();

  // Stop accepting new connections and deregister the server handler.
  ACE_Reactor::instance()->remove_handler(m_server.get_handle(),
                                          ACE_Event_Handler::ACCEPT_MASK |
                                              ACE_Event_Handler::SIGNAL_MASK);

  return (0);
}

ACE_INT32 WebServer::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask mask) {
  ACE_UNUSED_ARG(mask);
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l WebServer::handle_close "
                      "handle:%d\n"),
             handle));

  if (handle != ACE_INVALID_HANDLE) {
    m_server.close();
    m_stopMe = true;
  }

  return (0);
}

ACE_HANDLE WebServer::get_handle() const { return (m_server.get_handle()); }

WebServer::WebServer(std::string ipStr, ACE_UINT16 listenPort,
                     ACE_UINT32 workerPool, std::string dbUri,
                     std::string dbConnPool, std::string dbName) {
  std::string addr;
  addr.clear();

  if (ipStr.length()) {
    addr = ipStr;
    addr += ":";
    addr += std::to_string(listenPort);
    m_listen.set_address(addr.c_str(), addr.length());
  } else {
    addr = std::to_string(listenPort);
    m_listen.set_port_number(listenPort);
  }

  /* Stop the Webserver when this m_stopMe becomes true. */
  m_stopMe = false;

  /* Mongo DB interface */
  std::string uri("mongodb://127.0.0.1:27017");
  std::string _dbName("bayt");
  std::uint32_t _pool = 50;

  ACE_UNUSED_ARG(_pool);

  if (dbUri.length()) {
    uri.assign(dbUri);
  }

  if (dbConnPool.length()) {
    _pool = std::stoi(dbConnPool);
  }

  if (dbName.length()) {
    _dbName.assign(dbName);
  }

  // mMongodbc = new MongodbClient(uri);
  mMongodbc = std::make_unique<MongodbClient>(uri);

#if 0
    //ACE_NEW_NORETURN(m_semaphore, ACE_Semaphore());
    m_semaphore = std::make_unique<ACE_Semaphore>();

    m_workerPool.clear();
    std::uint32_t cnt;

    for(cnt = 0; cnt < workerPool; ++cnt) {

        semaphore().acquire();
        MicroService* worker = nullptr;
        ACE_NEW_NORETURN(worker, MicroService(ACE_Thread_Manager::instance(), this));
        m_workerPool.push_back(worker);
        worker->open();
        
    }

    m_currentWorker = std::begin(m_workerPool);
#endif
  /* Start listening for incoming connection */
  int reuse_addr = 1;
  if (m_server.open(m_listen, reuse_addr)) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [Master:%t] %M %N:%l Starting of WebServer failed "
                        "- opening of port %d hostname %s\n"),
               m_listen.get_port_number(), m_listen.get_host_name()));
  }
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l webserver handle:%d \n"),
             m_server.get_handle()));
}

WebServer::~WebServer() {
  /*
  if(nullptr != mMongodbc) {
      delete mMongodbc;
      mMongodbc = nullptr;
  }*/
  mMongodbc.reset(nullptr);

  // unique_ptr elements are deleted automatically on erase/clear
  m_workerPool.clear();
  m_semaphore.reset();
}

bool WebServer::start() {
  ACE_Reactor::instance()->register_handler(
      this, ACE_Event_Handler::ACCEPT_MASK | ACE_Event_Handler::SIGNAL_MASK);
  /* subscribe for signal */
  ACE_Sig_Set ss;
  ss.empty_set();
  ss.sig_add(SIGINT);
  ss.sig_add(SIGTERM);
  ACE_Reactor::instance()->register_handler(&ss, this);

  ACE_Time_Value to(1, 0);

  while (!m_stopMe) {
    ACE_INT32 ret = ACE_Reactor::instance()->handle_events(to);
    if (ret < 0)
      break;
  }

  ACE_Reactor::instance()->remove_handler(ss);
  return (0);
}

bool WebServer::stop() { return (true); }

WebConnection::WebConnection(WebServer *parent) {
  m_handle = -1;
  m_parent = parent;
}

WebConnection::~WebConnection() {
  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [Master:%t] %M %N:%l handle:%d webconnection closing\n"),
       m_handle));
  if (m_handle != ACE_INVALID_HANDLE)
    ::close(m_handle);
}

ACE_INT32 WebConnection::handle_input(ACE_HANDLE handle) {
  char tmp[65536];
  ssize_t rc = ::recv(handle, tmp, sizeof(tmp), 0);

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l handle_input handle:%d rc:%d\n"),
             handle, rc));

  if (rc == 0) {
    // Peer closed the connection cleanly
    parent()->connectionPool().erase(handle);
    return (-1);
  }

  if (rc < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Spurious reactor wakeup — data not ready yet, keep the connection open
      return (0);
    }
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [Master:%t] %M %N:%l recv error errno:%d\n"),
               errno));
    parent()->connectionPool().erase(handle);
    return (-1);
  }

  m_recvBuf.append(tmp, rc);

  // Process all complete messages that have accumulated in the buffer.
  // A single recv() may deliver more than one pipelined request, and a
  // single request may arrive across multiple recv() calls, so we loop
  // until no full message remains.
  while (!m_recvBuf.empty()) {
    std::size_t msgLen = Http::message_length(m_recvBuf);

    if (msgLen == 0 || m_recvBuf.size() < msgLen) {
      // Headers not yet complete, or body not yet fully received — wait
      // for the next handle_input() call to deliver the remaining bytes.
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [Master:%t] %M %N:%l partial message buffered "
                          "(%zu bytes), waiting for more\n"),
                 m_recvBuf.size()));
      break;
    }

    std::string request = m_recvBuf.substr(0, msgLen);
    m_recvBuf.erase(0, msgLen);

    ACE_DEBUG(
        (LM_DEBUG,
         ACE_TEXT("%D [Master:%t] %M %N:%l complete request (%zu bytes):\n%s"),
         msgLen, request.c_str()));

    WebServiceEntry wentry;
    wentry.process_request(handle, request, *(parent()->mongodbcInst()));
  }

  return (0);
}

ACE_INT32 WebConnection::handle_signal(int signum, siginfo_t *s,
                                       ucontext_t *u) {
  ACE_UNUSED_ARG(s);
  ACE_UNUSED_ARG(u);
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l signal %d (%S) received on "
                      "handle %d\n"),
             signum, signum, m_handle));
  return (0);
}

ACE_INT32 WebConnection::handle_close(ACE_HANDLE handle,
                                      ACE_Reactor_Mask mask) {
  ACE_UNUSED_ARG(mask);
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [Master:%t] %M %N:%l WebConnection::handle_close "
                      "handle %d\n"),
             handle));
  return (0);
}

ACE_HANDLE WebConnection::get_handle() const {
  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT(
           "%D [Master:%t] %M %N:%l WebConnection::get_handle - handle %d\n"),
       m_handle));
  return (m_handle);
}

/**
 * @brief This member function processes the DELETE for a given uri.
 *
 * @param in http request with MIME header
 * @param dbInst instance of mongodb driver
 * @return std::string
 */
std::string WebServiceEntry::handle_DELETE(std::string &in,
                                           MongodbClient &dbInst) {
  Http http(in);

  if (http.uri() != "/api/v1/shipment/awblist") {
    json err = {{"status", "failure"},
                {"cause", "Invalid AWB Bill Number"},
                {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  }

  const std::string coll("shipping");
  const auto awbNo = http.get_element("awbList");
  const auto startDate = http.get_element("startDate");
  const auto endDate = http.get_element("endDate");

  auto csv_to_array = [](const std::string &csv) {
    json arr = json::array();
    std::size_t start = 0, end;
    while ((end = csv.find(',', start)) != std::string::npos) {
      arr.push_back(csv.substr(start, end - start));
      start = end + 1;
    }
    arr.push_back(csv.substr(start));
    return arr;
  };

  json filter;
  if (!awbNo.empty()) {
    filter = {{"shipmentNo", {{"$in", csv_to_array(awbNo)}}}};
  } else if (!startDate.empty() && !endDate.empty()) {
    filter = {{"createdOn", {{"$gte", startDate}, {"$lte", endDate}}}};
  } else {
    json err = {
        {"status", "failure"}, {"cause", "Invalid AWB Number"}, {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  }

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l delete filter:%s\n"),
             filter.dump().c_str()));

  if (dbInst.delete_document(coll, filter.dump()))
    return build_responseOK(json{{"status", "success"}}.dump());

  json err = {{"status", "failure"},
              {"cause", "Delete operation failed"},
              {"error", 400}};
  return build_responseERROR(err.dump(), "400 Bad Request");
}

/**
 * @brief This member function starts processing the incoming HTTP request and
 * based on HTTP method it calls respective member function.
 *
 * @param handle socker descriptor on which HTTP request is received
 * @param req HTTP request with MIME header
 * @param dbInst An instance of mongodb driver
 * @return std::int32_t
 */
std::int32_t WebServiceEntry::process_request(ACE_HANDLE handle,
                                              std::string &req,
                                              MongodbClient &dbInst) {
  Http http(req);
  const std::string &method = http.method();

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l %s %s\n"),
             method.c_str(), http.uri().c_str()));

  std::string rsp;
  if (method == "OPTIONS")
    rsp = handle_OPTIONS(req);
  else if (method == "GET")
    rsp = handle_GET(req, dbInst);
  else if (method == "POST")
    rsp = handle_POST(req, dbInst);
  else if (method == "PUT")
    rsp = handle_PUT(req, dbInst);
  else if (method == "DELETE")
    rsp = handle_DELETE(req, dbInst);
  else {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l unsupported method: %s\n"),
               method.c_str()));
    return (0);
  }

  if (rsp.empty())
    return (0);

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l response length:%zu\n"),
             rsp.length()));

  const std::int32_t total = static_cast<std::int32_t>(rsp.length());
  std::int32_t offset = 0;

  while (offset < total) {
    std::int32_t sent = ::send(handle, rsp.c_str() + offset, total - offset, 0);
    if (sent < 0) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l send failed errno:%d\n"),
                 errno));
      return (-1);
    }
    offset += sent;
  }

  return (0);
}

std::string WebServiceEntry::get_contentType(std::string ext) {
  std::string cntType("");
  /* get the extension now for content-type */
  if (!ext.compare("woff")) {
    cntType = "font/woff";
  } else if (!ext.compare("woff2")) {
    cntType = "font/woff2";
  } else if (!ext.compare("ttf")) {
    cntType = "font/ttf";
  } else if (!ext.compare("otf")) {
    cntType = "font/otf";
  } else if (!ext.compare("css")) {
    cntType = "text/css";
  } else if (!ext.compare("js")) {
    cntType = "text/javascript";
  } else if (!ext.compare("eot")) {
    cntType = "application/vnd.ms-fontobject";
  } else if (!ext.compare("html")) {
    cntType = "text/html";
  } else if (!ext.compare("svg")) {
    cntType = "image/svg+xml";
  } else if (!ext.compare("gif")) {
    cntType = "image/gif";
  } else if (!ext.compare("png")) {
    cntType = "image/png";
  } else if (!ext.compare("ico")) {
    cntType = "image/vnd.microsoft.icon";
  } else if (!ext.compare("jpg")) {
    cntType = "image/jpeg";
  } else if (!ext.compare("json")) {
    cntType = "application/json";
  } else {
    cntType = "text/html";
  }
  return (cntType);
}

/**
 * @brief This member function is used to create document in a collection for a
 * given uri.
 *
 * @param in
 * @param dbInst
 * @return std::string
 */
std::string WebServiceEntry::handle_POST(std::string &in,
                                         MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_POST(in, dbInst));

  } else if (!uri.compare(0, 14, "/api/v1/config")) {
    return (handle_config_POST(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_POST(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_POST(in, dbInst));

  } else if (!uri.compare(0, 16, "/api/v1/document")) {
    return (handle_document_POST(in, dbInst));

  } else if (!uri.compare(0, 13, "/api/v1/email")) {
    return (handle_email_POST(in, dbInst));

  } else {
    return (build_responseOK(std::string()));
  }
}

std::string WebServiceEntry::handle_config_POST(std::string &in,
                                                MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/config/db")) {
    std::string content = http.body();

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"),
                 content.length()));
      std::string ip_address("");
      auto result =
          dbInst.from_json_element_to_string(content, "ip_address", ip_address);
      std::string port("");
      result = dbInst.from_json_element_to_string(content, "port", port);
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l dbconfig ip:%s port:%u\n"),
                 ip_address.c_str(), std::stoul(port)));
      /* Apply this config if changed */
    }
  }

  return (std::string());
}

std::string WebServiceEntry::handle_shipment_POST(std::string &in,
                                                  MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/shipment/shipping")) {
    std::string collectionName("shipping");

    /*We need newly shipment No. */
    std::string projection("{\"_id\" : false, \"shipmentNo\" : true}");
    std::string content = http.body();
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d "
                        "\n Request http_body:%s\n"),
               content.length(), content.c_str()));

    if (content.length()) {

      std::string record = dbInst.create_document(dbInst.get_database(),
                                                  collectionName, content);

      if (record.length()) {
        std::string rsp("");
        rsp = "{\"oid\" : \"" + record + "\"}";
        return (build_responseOK(rsp));
      }
    }

  } else if (!uri.compare("/api/v1/shipment/bulk/shipping")) {
    std::string content = http.body();
    std::string coll("shipping");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"),
                 content.length()));

      std::int32_t cnt =
          dbInst.create_bulk_document(dbInst.get_database(), coll, content);

      if (cnt) {
        // std::string rec = "{\"createdShipments\": " + std::to_string(cnt) +
        // "}";
        json rec = json::object();
        rec = {{"createdShipments", std::to_string(cnt)}};
        return (build_responseOK(rec.dump()));
      } else {
        std::string err("400 Bad Request");
        // std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Bulk
        // Shipment Creation is failed\", \"errorCode\" : 400}");
        json err_message = json::object();
        err_message = {{"status", "failure"},
                       {"cause", "Bulk Shipment Creation Failed"},
                       {"error", 400}};
        return (build_responseERROR(err_message.dump(), err));
      }
    }

  } else if (!uri.compare("/api/v1/shipment/thirdparty/ajoul")) {
    // std::string
    // req("{\"Shipment\":{\"reference\":\"AB100\",\"pickup_date\":null,\"pickup_time\":null,\"product_type\":\"104\",\"product_price\":null,\"destination\":\"RUH\",\"origin\":\"RUH\",\"parcel_quantity\":\"2\",\"parcel_weight\":\"4\",\"payment_mode\":\"COD\",\"service_id\":\"2\",\"description\":\"Testing
    // Create Shipment From
    // API\",\"sku\":\"423423\",\"customer_lng\":null,\"customer_lat\":null,\"sender\":{\"name\":\"Alaa\",\"address\":\"Al
    // Haram street,
    // Giza\",\"zip_code\":null,\"phone\":\"01063396459\",\"email\":\"admin@quadratechsoft.com\"},\"receiver\":{\"name\":\"Alaa\",\"address\":\"AL
    // Malki,
    // Damascuss\",\"zip_code\":\"1234\",\"phone\":\"0941951819\",\"phone2\":\"09419518549\",\"email\":\"info@quadratechsoft.com\"}},\"TrackingNumber\":\"AR222188000614391\",\"printLable\":\"https:\/\/ajoul.com\/printlabelone\/AR222188000614391\"}");
    // std::string awbNo = dbInst.get_value(req, "TrackingNumber");
    // ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l TrackingNumber %s
    // \n"), awbNo.c_str()));
    std::stringstream header("");
    // header = "Connection: close\r\n"
    //          "Cache-Control: no-cache\r\n";
    //  "Content-Type: multipart/form-data;
    //  boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

    std::stringstream apiAuthorizeAjoul("");

    apiAuthorizeAjoul
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"client_secret\"\r\n\r\n"
        << "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"client_id\"\r\n\r\n"
        << "34\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"username\"\r\n\r\n"
        << "AKjHYuCAco\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        << "Content-Disposition: form-data; name=\"password\"\r\n\r\n"
        << "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n"
        << "------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
    /*
    apiAuthorizeAjoul <<
    "client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o"
                      <<
    "&client_id=34&username=AKjHYuCAco&password=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n";*/

    header << "Host: www.ajoul.com\r\n"
           << "Accept: application/json\r\n"
           << "User-Agent: Balaagh/1.0\r\n"
           << "Connection: keep-alive\r\n"
           << "Cache-Control: no-cache\r\n"
           << "Content-Type: application/x-www-form-urlencoded\r\n"
           << "Content-Type: multipart/form-data; "
              "boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
           //<<  "Content-Length: " << apiAuthorizeAjoul.str().length()
           << "Content-Length: 0" << "\r\n";
    //<<  "Content-Type: multipart/form-data;
    // boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

    // header << "\r\n";

    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l the header is\n%s\n"),
               header.str().c_str()));

    std::string apiURLAjoul = "https://ajoul.com/remote/api/v1/authorize";
    ACE_SSL_SOCK_Connector client;
    ACE_SSL_SOCK_Stream conn;
    ACE_INET_Addr connectAddr("ajoul.com:443");
    ACE_Time_Value to(2, 0);

    if (client.connect(conn, connectAddr, &to) < 0) {

      ACE_ERROR(
          (LM_ERROR,
           ACE_TEXT(
               "%D [worker:%t] %M %N:%l connect to ajoul:443 is failed\n")));
      std::string err("400 Bad Request");
      std::string err_message(
          "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not "
          "rechable\", \"errorCode\" : 400}");
      return (build_responseERROR(err_message, err));

    } else {

      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l Connect to "
                          "https://ajoul.com (%u) - %s is success\n"),
                 connectAddr.get_ip_address(), connectAddr.get_host_addr()));

      std::stringstream postReq("");
      postReq
          << "POST "
             "/remote/api/v1/"
             "authorize?client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o&"
             "client_id=34&username=AKjHYuCAco&password="
             "uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o HTTP/1.1\r\n"
          << header.str() << "\r\n";
      //<< apiAuthorizeAjoul.str();

      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l the request is\n%s\n"),
                 postReq.str().c_str()));

      if (conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

        ACE_ERROR((
            LM_ERROR,
            ACE_TEXT("%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
        std::string err("400 Bad Request");
        std::string err_message(
            "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not "
            "responding\", \"errorCode\" : 400}");
        return (build_responseERROR(err_message, err));

      } else {

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to "
                                      "https://ajoul.com is success\n")));

        std::array<std::uint8_t, 3048> authRsp;
        authRsp.fill(0);
        ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);

        if (len < 0) {
          ACE_ERROR(
              (LM_ERROR,
               ACE_TEXT(
                   "%D [worker:%t] %M %N:%l recv from ajoul:443 is failed\n")));
          std::string err("400 Bad Request");
          std::string err_message(
              "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is "
              "not responding to Authorize req\", \"errorCode\" : 400}");
          return (build_responseERROR(err_message, err));

        } else {
          std::string rsp((char *)authRsp.data(), len);
          ACE_DEBUG((LM_DEBUG,
                     ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"),
                     rsp.c_str()));
          Http http(rsp);
          std::string access_token =
              dbInst.get_access_token_for_ajoul(http.body());
          ACE_DEBUG(
              (LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l The access_token is - %s\n"),
               access_token.c_str()));

          /* Now building request for creating shipment */
          std::stringstream shipmentCreate("");
          std::stringstream hdr("");

          shipmentCreate
              << "{" << "\"receiver\":{" << "\"name\":\"Alaa\","
              << "\"country_code\": \"SA\"," << "\"city_code\": \"RUH\","
              << "\"address\": \"AL Malki, Damascuss\","
              << "\"zip_code\": \"1234\"," << "\"phone\": \"0941951819\","
              << "\"phone2\": \"09419518549\","
              << "\"email\": \"info@quadratechsoft.com\"" << "},"
              << "\"sender\": {" << "\"name\": \"Alaa\","
              << "\"country_code\": \"SA\"," << "\"city_code\": \"RUH\","
              << "\"address\": \"Al Haram street, Giza\","
              << "\"phone\": \"01063396459\","
              << "\"email\": \"admin@quadratechsoft.com\"" << "},"
              << "\"reference\": \"AB100\"," << "\"pick_date\": \"2018-08-06\","
              << "\"pickup_time\": \"12:49\"," << "\"product_type\": \"104\","
              << "\"payment_mode\": \"COD\"," << "\"parcel_quantity\": \"2\","
              << "\"parcel_weight\": \"4\"," << "\"service_id\": \"2\","
              << "\"description\": \"Testing Create Shipment From API\","
              << "\"sku\": \"423423\"," << "\"weight_total\": \"20\","
              << "\"total_cod_amount\": 50.9" << "}";
          hdr << "Host: www.ajoul.com\r\n"
              << "Accept: application/json\r\n"
              << "User-Agent: Balaagh/1.0\r\n"
              << "Connection: keep-alive\r\n"
              << "Cache-Control: no-cache\r\n"
              << "Content-Type: application/json\r\n"
              << "Authorization: Bearer " << access_token << "\r\n"
              << "Content-Length: " << shipmentCreate.str().length()
              << "\r\n\r\n"
              << shipmentCreate.str();
          postReq.str("");
          postReq << "POST /remote/api/v1/shipment/create HTTP/1.1\r\n"
                  << hdr.str();
          if (conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

            ACE_ERROR(
                (LM_ERROR,
                 ACE_TEXT(
                     "%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
            std::string err("400 Bad Request");
            std::string err_message(
                "{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is "
                "not responding\", \"errorCode\" : 400}");
            return (build_responseERROR(err_message, err));

          } else {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to "
                                          "https://ajoul.com is success\n")));

            std::array<std::uint8_t, 3048> authRsp;
            authRsp.fill(0);
            ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);
            std::string rsp((char *)authRsp.data(), len);
            ACE_DEBUG((LM_DEBUG,
                       ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"),
                       rsp.c_str()));
            Http http(rsp);
            std::string ref("");
            std::string awbNo =
                dbInst.get_tracking_no_for_ajoul(http.body(), ref);
            ACE_DEBUG((LM_DEBUG,
                       ACE_TEXT("%D [worker:%t] %M %N:%l The Tracking Number "
                                "is - %s refNumber %s\n"),
                       awbNo.c_str(), ref.c_str()));
          }
          return (build_responseOK(http.body()));
        }
      }
    }
  }
  return (std::string());
}

std::string WebServiceEntry::handle_account_POST(std::string &in,
                                                 MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/account/account")) {
    std::string collectionName("account");
    /*We need newly created account Code */
    std::string projection("{\"_id\" : false, \"accountCode\" : true}");
    std::string content = http.body();
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d "
                        "\n Request http_body:%s\n"),
               content.length(), content.c_str()));

    if (content.length()) {
      std::string oid = dbInst.create_document(dbInst.get_database(),
                                               collectionName, content);

      if (oid.length()) {
        // std::string rsp = dbInst.get_byOID(collectionName, projection, oid);
        // std::string rsp("");
        json rsp = json::object();
        rsp = {{"oid", oid}};
        // rsp = "{\"oid\" : \"" + oid + "\"}";

        return (build_responseOK(rsp.dump()));
      }
    }
  }
  return (std::string());
}

std::string WebServiceEntry::handle_inventory_POST(std::string &in,
                                                   MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/inventory")) {
    /* Creating sku for inventory */
    std::string content = http.body();
    std::string coll("inventory");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"),
                 content.length()));
      std::string record =
          dbInst.create_document(dbInst.get_database(), coll, content);

      if (record.length()) {
        // std::string rsp("");
        json rsp = json::object();
        rsp = {{"oid", record}};
        // rsp = "{\"oid\" : \"" + record + "\"}";
        return (build_responseOK(rsp.dump()));
      }
    }
  }
  return (std::string());
}

std::string WebServiceEntry::handle_document_POST(std::string &in,
                                                  MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/document")) {
    std::string content = http.body();
    std::string coll("attachment");

    if (content.length()) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"),
                 content.length()));
      std::string value("");
      auto result =
          dbInst.from_json_element_to_string(content, "corporate", value);

      if (result) {
        coll.clear();
        coll = value + "_attachment";
      }

      std::string record =
          dbInst.create_document(dbInst.get_database(), coll, content);

      if (record.length()) {
        // std::string rsp("");
        json rsp = json::object();
        // rsp = "{\"oid\" : \"" + record + "\"}";
        rsp = {{"oid", record}};
        return (build_responseOK(rsp));

      } else {
        std::string err("400 Bad Request");
        // std::string err_message("{\"status\" : \"faiure\", \"cause\" :
        // \"attachment upload failed\", \"errorCode\" : 400}");
        json err_message = json::object();
        err_message = {{"status", "failure"},
                       {"cause", "Attachment Upload Failed"},
                       {"error", 400}};
        return (build_responseERROR(err_message.dump(), err));
      }
    }
  }
  return (std::string());
}

std::string WebServiceEntry::handle_email_POST(std::string &in,
                                               MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);
  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare("/api/v1/email")) {
    /* Send e-mail with POST request */
    // {"subject": "", "to": [user-id@domain.com, user-id1@domain.com], "body":
    // ""}
    std::string json_body = http.body();
    std::vector<std::string> out_vec;
    std::vector<std::tuple<std::string, std::string>> out_list;
    std::string subj;
    std::string body;
    std::string from;
    std::string passwd;

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email request:%s\n"),
               json_body.c_str()));
    dbInst.from_json_array_to_vector(json_body, "to", out_vec);
    dbInst.from_json_element_to_string(json_body, "subject", subj);
    dbInst.from_json_element_to_string(json_body, "emailbody", body);
    dbInst.from_json_object_to_map(json_body, "files", out_list);
    dbInst.from_json_element_to_string(json_body, "from", from);
    dbInst.from_json_element_to_string(json_body, "passwd", passwd);

    for (const auto &elm : out_vec) {
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l email to list:%s\n"),
                 elm.c_str()));
    }
    ACE_DEBUG(
        (LM_DEBUG,
         ACE_TEXT(
             "%D [worker:%t] %M %N:%l email subject:%s from:%s passwd:%s\n"),
         subj.c_str(), from.c_str(), passwd.c_str()));
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email body:%s\n"),
               body.c_str()));

    SMTP::Account::instance().to_email(out_vec);
    SMTP::Account::instance().email_subject(subj);
    SMTP::Account::instance().email_body(body);
    SMTP::Account::instance().from_email(from);
    SMTP::Account::instance().from_password(passwd);

    if (!out_list.empty()) {
      /* e-mail with attachment */
      SMTP::Account::instance().attachment(out_list);
    }

    SMTP::User email;
    email.startEmailTransaction();
    std::string rsp("{\"status\": \"success\"");
    return (build_responseOK(rsp));
  }
  return (std::string());
}

std::string WebServiceEntry::handle_GET(std::string &in,
                                        MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);

  /* Action based on uri in get request */
  std::string uri(http.uri());
  if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_GET(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_GET(in, dbInst));

  } else if (!uri.compare(0, 16, "/api/v1/document")) {
    return (handle_document_GET(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_GET(in, dbInst));

  } else if ((!uri.compare(0, 7, "/webui/"))) {
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l frontend Request %s\n"),
               uri.c_str()));
    /* build the file name now */
    std::string fileName("");
    std::string ext("");

    std::size_t found = uri.find_last_of(".");
    if (found != std::string::npos) {
      ext = uri.substr((found + 1), (uri.length() - found));
      fileName = uri.substr(6, (uri.length() - 6));
      std::string newFile = "../webgui/webui/" + fileName;
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The "
                          "extension is %s\n"),
                 newFile.c_str(), ext.c_str()));
      /* Open the index.html file and send it to web browser. */
      std::ifstream ifs(newFile.c_str());
      std::stringstream _str("");

      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));
        std::string cntType("");
        cntType = get_contentType(ext);

        _str << ifs.rdbuf();
        ifs.close();
        return (build_responseOK(_str.str(), cntType));
      }
    } else {
      std::string newFile = "../webgui/webui/index.html";
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"),
                 newFile.c_str()));
      /* Open the index.html file and send it to web browser. */
      std::ifstream ifs(newFile.c_str(), std::ios::binary);
      std::stringstream _str("");
      std::string cntType("");

      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));

        cntType = "text/html";
        _str << ifs.rdbuf();
        ifs.close();

        return (build_responseOK(_str.str(), cntType));
      }
    }
  } else if (!uri.compare(0, 8, "/assets/")) {
    std::string ext;
    std::size_t found = uri.find_last_of(".");
    if (found != std::string::npos) {
      ext = uri.substr(found + 1);
      std::string newFile = "../webgui/webui" + uri;
      ACE_DEBUG((LM_DEBUG,
                 ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The "
                          "extension is %s\n"),
                 newFile.c_str(), ext.c_str()));
      std::ifstream ifs(newFile.c_str(), std::ios::binary);
      std::stringstream _str;
      if (ifs.is_open()) {
        ACE_DEBUG((LM_DEBUG,
                   ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open "
                            "successfully.\n"),
                   uri.c_str()));
        _str << ifs.rdbuf();
        return (build_responseOK(_str.str(), get_contentType(ext)));
      }
    }

  } else if (!uri.compare(0, 1, "/")) {
    std::string newFile = "../webgui/webui/index.html";
    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"),
               newFile.c_str()));
    std::ifstream ifs(newFile.c_str(), std::ios::binary);
    std::stringstream _str;
    if (ifs.is_open()) {
      ACE_DEBUG((
          LM_DEBUG,
          ACE_TEXT(
              "%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"),
          uri.c_str()));
      _str << ifs.rdbuf();
      return (build_responseOK(_str.str(), "text/html"));
    }
  }

  return (build_responseOK(std::string()));
}

std::string WebServiceEntry::handle_shipment_GET(std::string &in,
                                                 MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/shipment/shipping")
    return {};

  const std::string collection("shipping");
  const json projection = {{"_id", false}};

  // Convert "a,b,c" to a JSON array ["a","b","c"]
  auto csv_to_array = [](const std::string &csv) {
    json arr = json::array();
    std::size_t start = 0, end;
    while ((end = csv.find(',', start)) != std::string::npos) {
      arr.push_back(csv.substr(start, end - start));
      start = end + 1;
    }
    arr.push_back(csv.substr(start));
    return arr;
  };

  // Query the collection and return the appropriate HTTP response
  auto fetch_and_respond = [&](const json &doc, const std::string &cause) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l query:%s\n"),
               doc.dump().c_str()));
    std::string record =
        dbInst.get_documents(collection, doc.dump(), projection.dump());
    if (!record.empty())
      return build_responseOK(record);
    json err = {{"status", "failure"}, {"cause", cause}, {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  };

  const auto awbNo = http.get_element("awbNo");
  const auto altRefNo = http.get_element("altRefNo");
  const auto senderRefNo = http.get_element("senderRefNo");
  const auto accountCode = http.get_element("accountCode");
  const auto fromDate = http.get_element("fromDate");
  const auto toDate = http.get_element("toDate");
  const auto country = http.get_element("country");

  if (!awbNo.empty()) {
    json doc = {{"shipment.awbno", {{"$in", csv_to_array(awbNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid AWB Bill No.");
  }

  if (!altRefNo.empty()) {
    json doc = {{"altRefNo", {{"$in", csv_to_array(altRefNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid ALT REF No.");
  }

  if (!senderRefNo.empty()) {
    json doc = {{"senderRefNo", {{"$in", csv_to_array(senderRefNo)}}}};
    if (!accountCode.empty())
      doc["accountCode"] = accountCode;
    return fetch_and_respond(doc, "Invalid Sender REF No.");
  }

  if (!fromDate.empty() && !toDate.empty()) {
    json doc = {{"shipment.shipmentInformation.createdOn",
                 {{"$gte", fromDate}, {"$lte", toDate}}}};
    if (!accountCode.empty())
      doc["shipment.senderInformation.accountNo"] = {
          {"$in", csv_to_array(accountCode)}};
    if (!country.empty())
      doc["shipment.receiverInformation.country"] = country;
    return fetch_and_respond(doc, "Invalid input for detailed report.");
  }

  return {};
}

std::string WebServiceEntry::handle_account_GET(std::string &in,
                                                MongodbClient &dbInst) {
  Http http(in);
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request uri:%s\n"),
             http.uri().c_str()));

  if (http.uri() != "/api/v1/account/account")
    return {};

  const std::string collection("account");
  const json projection = {{"_id", false}};

  // Fetch a single document; return OK on hit, ERROR with given status on miss
  auto fetch_one = [&](const json &doc, const std::string &http_err,
                       const json &err_body) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l query:%s\n"),
               doc.dump().c_str()));
    std::string record =
        dbInst.get_document(collection, doc.dump(), projection.dump());
    if (!record.empty())
      return build_responseOK(record);
    return build_responseERROR(err_body.dump(), http_err);
  };

  const auto userId = http.get_element("userId");
  const auto pwd = http.get_element("password");
  const auto accCode = http.get_element("accountCode");

  if (!userId.empty() && !pwd.empty()) {
    json doc = {{"loginCredentials.accountCode", userId},
                {"loginCredentials.accountPassword", pwd}};
    json err = {{"status", "failure"},
                {"cause", "Invalid Credentials"},
                {"error", 404}};
    return fetch_one(doc, "400 Bad Request", err);
  }

  if (!accCode.empty()) {
    json doc = {{"loginCredentials.accountCode", accCode}};
    json err = {{"status", "failure"},
                {"cause", "Invalid Account Code"},
                {"error", 400}};
    return fetch_one(doc, "400 Bad Request", err);
  }

  // No filter: return the full account list
  std::string record = dbInst.get_documents(collection, projection.dump());
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l account_list:%s\n"),
             record.c_str()));
  if (record.empty()) {
    json err = {{"status", "failure"},
                {"cause", "There's no customer record"},
                {"error", 404}};
    return build_responseERROR(err.dump(), "404 Not Found");
  }
  return build_responseOK(record);
}

std::string WebServiceEntry::handle_inventory_GET(std::string &in,
                                                  MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/inventory")
    return {};

  const json projection = {{"_id", false}};
  const auto sku = http.get_element("sku");
  const auto accCode = http.get_element("accountCode");

  // Build filter from whichever params are present; empty doc fetches all
  json doc = json::object();
  if (!accCode.empty())
    doc["accountCode"] = accCode;
  if (!sku.empty())
    doc["sku"] = sku;

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Inventory query:%s\n"),
             doc.dump().c_str()));
  std::string record =
      dbInst.get_documents("inventory", doc.dump(), projection.dump());

  if (!record.empty())
    return build_responseOK(record);

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l No Record found\n")));
  json err = {{"status", "failure"},
              {"cause", "There's no Inventory Record"},
              {"error", 404}};
  return build_responseERROR(err.dump(), "404 Not Found");
}

std::string WebServiceEntry::handle_email_GET(std::string &in,
                                              MongodbClient &dbInst) {
  ACE_UNUSED_ARG(in);
  ACE_UNUSED_ARG(dbInst);
  return {};
}

std::string WebServiceEntry::handle_document_GET(std::string &in,
                                                 MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/document")
    return {};

  const json projection = {{"_id", false}};
  const auto collection = http.get_element("corporate");
  const auto userId = http.get_element("userId");
  const auto fileName = http.get_element("file");

  json doc = {{"corporate", collection}};
  if (!userId.empty()) {
    doc["userId"] = userId;
    doc["file"] = fileName;
  }

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [worker:%t] %M %N:%l attachment query:%s\n"),
             doc.dump().c_str()));
  std::string record =
      dbInst.get_documents(collection, doc.dump(), projection.dump());

  if (!record.empty())
    return build_responseOK(record);
  json err = {
      {"status", "failure"}, {"cause", "Document not found"}, {"error", 404}};
  return build_responseERROR(err.dump(), "404 Not Found");
}

std::string WebServiceEntry::handle_config_GET(std::string &in,
                                               MongodbClient &dbInst) {
  ACE_UNUSED_ARG(in);
  ACE_UNUSED_ARG(dbInst);
  return {};
}

/**
 * @brief this member function is used to Update the collection for a given uri.
 *
 * @param in
 * @param dbInst
 * @return std::string
 */
std::string WebServiceEntry::handle_PUT(std::string &in,
                                        MongodbClient &dbInst) {
  /* Check for Query string */
  Http http(in);

  /* Action based on uri in get request */
  std::string uri(http.uri());

  if (!uri.compare(0, 28, "/api/v1/shipment/bulk/altref")) {
    return (handle_altref_update_shipment_PUT(in, dbInst));

  } else if (!uri.compare(0, 16, "/api/v1/shipment")) {
    return (handle_shipment_PUT(in, dbInst));

  } else if (!uri.compare(0, 17, "/api/v1/inventory")) {
    return (handle_inventory_PUT(in, dbInst));

  } else if (!uri.compare(0, 15, "/api/v1/account")) {
    return (handle_account_PUT(in, dbInst));

  } else {
    std::string err("400 Bad Request");
    // std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment
    // Updated Failed\", \"error\" : 400}");
    json err_message = json::object();
    err_message = {{"status", "failure"},
                   {"cuase", "URI is not supported"},
                   {"error", 400}};
    return (build_responseERROR(err_message.dump(), err));
  }
}

std::string
WebServiceEntry::handle_altref_update_shipment_PUT(std::string &in,
                                                   MongodbClient &dbInst) {
  Http http(in);
  auto body = http.body();

  if (body.length()) {
    json content = json::parse(body);
    auto awbno = content["awbno"];
    auto altrefno = content["altrefno"];
    // json filter = json::array();
    // json value = json::array();
    std::vector<std::string> filter;
    std::vector<std::string> value;

    ACE_DEBUG((LM_DEBUG,
               ACE_TEXT("%D [worker:%t] %M %N:%l awbno:%s altref:%s\n"),
               awbno.dump().c_str(), altrefno.dump().c_str()));

    for (auto it = awbno.begin(); it != awbno.end(); ++it) {
      json elm = json::object();
      elm = {{"shipment.awbno", *it}};
      filter.push_back(elm.dump());
    }

    for (auto it = altrefno.begin(); it != altrefno.end(); ++it) {
      json elm = json::object();
      elm = {{"$set", {{"shipment.altRefNo", *it}}}};
      value.push_back(elm.dump());
    }

    auto collection = "shipping";
    // std::vector<std::string> filter_doc(filter.begin(), filter.end());
    // std::vector<std::string> value_doc(value.begin(), value.end());

    // ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l filter:%s
    // value:%s\n"), filter.dump().c_str(), value.dump().c_str()));
    auto record = dbInst.update_bulk_document(collection, filter, value);
    if (record) {
      // std::string rec = "{\"createdShipments\": " + std::to_string(cnt) +
      // "}";
      json rec = json::object();
      rec = {{"status", "success"},
             {"updatedShipments", std::to_string(record)}};
      return (build_responseOK(rec.dump()));
    } else {
      std::string err("400 Bad Request");
      // std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Bulk
      // Shipment Creation is failed\", \"errorCode\" : 400}");
      json err_message = json::object();
      err_message = {{"status", "failure"},
                     {"cause", "Bulk ALT REF Update Failed"},
                     {"error", 400}};
      return (build_responseERROR(err_message.dump(), err));
    }
  }
  return (std::string());
}

std::string WebServiceEntry::handle_shipment_PUT(std::string &in,
                                                 MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/shipment/shipping")
    return {};

  const std::string coll("shipping");
  const auto content = http.body();
  const auto awbNo = http.get_element("shipmentNo");
  const auto accCode = http.get_element("accountCode");
  const auto isSingleShipment = http.get_element("isSingleShipment");

  const json not_pod = {{"$ne", "Proof of Delivery"}};

  // Execute update and return success or error response
  auto do_update = [&](const json &query, const json &doc) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
               doc.dump().c_str(), query.dump().c_str()));
    if (dbInst.update_collection(coll, query.dump(), doc.dump()))
      return build_responseOK(json{{"status", "success"}}.dump());
    json err = {{"status", "failure"},
                {"cause", "Shipment Update Failed"},
                {"error", 400}};
    return build_responseERROR(err.dump(), "400 Bad Request");
  };

  if (!isSingleShipment.empty()) {
    // Single-awb: exact match on awbno, $set the body
    json query = {{"shipment.awbno", awbNo},
                  {"shipment.shipmentInformation.activity.event", not_pod}};
    if (!accCode.empty())
      query["accountCode"] = accCode;
    return do_update(query, {{"$set", json::parse(content)}});
  }

  // Multi-awb: $in list from csv, $push activity
  auto csv_to_array = [](const std::string &csv) {
    json arr = json::array();
    std::size_t start = 0, end;
    while ((end = csv.find(',', start)) != std::string::npos) {
      arr.push_back(csv.substr(start, end - start));
      start = end + 1;
    }
    arr.push_back(csv.substr(start));
    return arr;
  };

  json query = {{"shipment.awbno", {{"$in", csv_to_array(awbNo)}}},
                {"shipment.shipmentInformation.activity.event", not_pod}};
  if (!accCode.empty())
    query["accountCode"] = accCode;
  return do_update(
      query,
      {{"$push",
        {{"shipment.shipmentInformation.activity", json::parse(content)}}}});
}

std::string WebServiceEntry::handle_inventory_PUT(std::string &in,
                                                  MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/inventory")
    return {};

  const auto sku = http.get_element("sku");
  const auto qty = http.get_element("qty");
  const auto acc = http.get_element("accountCode");
  const auto isUpdate = http.get_element("isUpdate");

  json query = json::object();
  if (!sku.empty())
    query["sku"] = sku;
  if (!acc.empty())
    query["accountCode"] = acc;

  const int delta = isUpdate.empty() ? -std::stoi(qty) : std::stoi(qty);
  json doc = {{"$inc", {{"qty", delta}}}};

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
             doc.dump().c_str(), query.dump().c_str()));
  if (dbInst.update_collection("inventory", query.dump(), doc.dump()))
    return build_responseOK(json{{"status", "success"}}.dump());

  json err = {{"status", "failure"},
              {"cause", "Inventory Update Failed"},
              {"error", 400}};
  return build_responseERROR(err.dump(), "400 Bad Request");
}

std::string WebServiceEntry::handle_account_PUT(std::string &in,
                                                MongodbClient &dbInst) {
  Http http(in);
  if (http.uri() != "/api/v1/account/account")
    return {};

  const auto content = http.body();
  const auto accCode = http.get_element("userId");

  json query = json::object();
  if (!accCode.empty())
    query = {{"loginCredentials.accountCode", accCode}};

  json doc = {{"$set", json::parse(content)}};

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l doc:%s query:%s\n"),
             doc.dump().c_str(), query.dump().c_str()));
  if (dbInst.update_collection("account", query.dump(), doc.dump()))
    return build_responseOK(json{{"status", "success"}}.dump());

  json err = {{"status", "failure"},
              {"cause", "Account Update Failed"},
              {"error", 400}};
  return build_responseERROR(err.dump(), "400 Bad Request");
}

std::string WebServiceEntry::handle_OPTIONS(std::string &in) {
  ACE_UNUSED_ARG(in);
  std::string http_header;
  http_header = "HTTP/1.1 200 OK\r\n";
  http_header +=
      "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n";
  http_header +=
      "Access-Control-Allow-Headers: DNT, User-Agent, X-Requested-With, "
      "If-Modified-Since, Cache-Control, Content-Type, Range\r\n";
  http_header += "Access-Control-Max-Age: 1728000\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";
  http_header += "Content-Type: text/plain; charset=utf-8\r\n";
  http_header += "Content-Length: 0\r\n";
  http_header += "\r\n";
  // ACE_Message_Block* rsp = nullptr;

  // ACE_NEW_RETURN(rsp, ACE_Message_Block(512), nullptr);

  // std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
  // rsp->wr_ptr(http_header.length());

  // ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d
  // response %s \n"), http_header.length(), http_header.c_str()));
  return (http_header);
}

std::string WebServiceEntry::build_responseCreated() {
  std::string http_header;
  http_header = "HTTP/1.1 201 Created\r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";
  http_header += "Content-Length: 0\r\n";
  http_header += "\r\n";

  // ACE_NEW_RETURN(rsp, ACE_Message_Block(256), nullptr);

  // std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
  // rsp->wr_ptr(http_header.length());

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"),
       http_header.length(), http_header.c_str()));
  return (http_header);
}

std::string WebServiceEntry::build_responseOK(std::string httpBody,
                                              std::string contentType) {
  std::string http_header;
  // ACE_Message_Block* rsp = nullptr;
  std::string rsp;

  http_header = "HTTP/1.1 200 OK\r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";

  if (httpBody.length()) {
    http_header +=
        "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
    http_header += "Content-Type: " + contentType + "\r\n";
    http_header += "\r\n";

  } else {
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n";
  }
  rsp = http_header;

  if (httpBody.length()) {
    rsp += httpBody;
  }

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"),
       (http_header.length() + httpBody.length()), http_header.c_str()));
  return (rsp);
}

std::string WebServiceEntry::build_responseERROR(std::string httpBody,
                                                 std::string error) {
  std::string http_header;
  std::string contentType("application/json");

  http_header = "HTTP/1.1 " + error + " \r\n";
  http_header += "Connection: keep-alive\r\n";
  http_header += "Access-Control-Allow-Origin: *\r\n";

  if (httpBody.length()) {
    http_header +=
        "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
    http_header += "Content-Type: " + contentType + "\r\n";
    http_header += "\r\n";
    http_header += httpBody;

  } else {
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n";
  }

  ACE_DEBUG(
      (LM_DEBUG,
       ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"),
       (http_header.length() + httpBody.length()), http_header.c_str()));
  return (http_header);
}
