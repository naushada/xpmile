#ifndef __webservice_cc__
#define __webservice_cc__

#include "webservice.h"
#include "http_parser.h"
#include "emailservice.hpp"

/**
 * @brief This member function processes the DELETE for a given uri.
 * 
 * @param in http request with MIME header
 * @param dbInst instance of mongodb driver
 * @return std::string 
 */
std::string MicroService::handle_DELETE(std::string& in, MongodbClient& dbInst)
{
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());
    std::string document("");

    if(!uri.compare("/api/v1/shipment/awblist")) {
        /** Delete Shipment */
        std::string coll("shipping");
        std::string awbNo = http.get_element("awbList");
        std::string startDate = http.get_element("startDate");
        std::string endDate = http.get_element("endDate");
        if(awbNo.length()) {
            // awbList contains value with comma seperated and converting into an array 
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = awbNo.find(delim);
            while (end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }
            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";

            document = "{\"shipmentNo\": {\"$in\" : " + lst + "}}";

        } else if(startDate.length() && endDate.length()) {
            // deleting awb based on start & end date - bulk delete
            document = "{\"createdOn\": {\"$gte\" : \"" + startDate + "\"," + "\"$lte\" :\"" + endDate +"\"" +"}}";
        } else {

            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

        bool rsp = dbInst.delete_document(coll, document);

        if(rsp) {
            std::string r("");
            r = "{\"status\": \"success\"}";
            return(build_responseOK(r));
        } 
    }

    std::string err("400 Bad Request");
    std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
    return(build_responseERROR(err_message, err));
}

std::int32_t MicroService::process_request(ACE_HANDLE handle, ACE_Message_Block& mb, MongodbClient& dbInst)
{
    std::string http_header, http_body;
    http_header.clear();
    http_body.clear();
    std::string rsp;

    std::string req(mb.rd_ptr(), mb.length());

    if(std::string::npos != req.find("OPTIONS", 0)) {
      rsp = handle_OPTIONS(req);
    } else if(std::string::npos != req.find("GET", 0)) {
      rsp = handle_GET(req, dbInst); 
    } else if(std::string::npos != req.find("POST", 0)) {
      rsp = handle_POST(req, dbInst);
    } else if(std::string::npos != req.find("PUT", 0)) {
      rsp = handle_PUT(req, dbInst);
    } else if(std::string::npos != req.find("DELETE", 0)) {
      rsp = handle_DELETE(req, dbInst);  
    } else {
      /* Not supported Method */
    }

    std::int32_t ret = 0;
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the response length is %d\n"), rsp.length()));
    
    std::int32_t  toBeSent = rsp.length();
    std::int32_t offset = 0;
    do {
      ret = send(handle, (rsp.c_str() + offset), (toBeSent - offset), 0);

      if(ret < 0) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l sent to peer is failed\n")));
        break;
      }

      offset += ret;
      ret = 0;

    } while((toBeSent != offset));
    
    return(ret);
}

/**
 * @brief This member function starts processing the incoming HTTP request and based on HTTP method it calls 
 *        respective member function.
 *
 * @param handle socker descriptor on which HTTP request is received 
 * @param req HTTP request with MIME header
 * @param dbInst An instance of mongodb driver
 * @return std::int32_t 
 */
std::int32_t MicroService::process_request(ACE_HANDLE handle, std::string& req, MongodbClient& dbInst)
{
    std::string rsp("");
    std::int32_t ret = 0;

    if(std::string::npos != req.find("OPTIONS", 0)) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l OPTIONS request:%s\n"), req.c_str()));
      rsp = handle_OPTIONS(req);

    } else if(std::string::npos != req.find("GET", 0)) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l GET request:%s\n"), req.c_str()));
      rsp = handle_GET(req, dbInst); 

    } else if(std::string::npos != req.find("POST", 0)) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l POST request:%s\n"), req.c_str()));
      rsp = handle_POST(req, dbInst);

    } else if(std::string::npos != req.find("PUT", 0)) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l PUT request:%s\n"), req.c_str()));
      rsp = handle_PUT(req, dbInst);

    } else if(std::string::npos != req.find("DELETE", 0)) {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l DELETE request:%s\n"), req.c_str()));
      rsp = handle_DELETE(req, dbInst);  

    } else {
      ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Method is not supported request:%s\n"), req.c_str()));
      /* Not supported Method */
      return(ret);

    }

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the response length:%d\n"), rsp.length()));
    
    std::int32_t  toBeSent = rsp.length();
    std::int32_t offset = 0;
    do {
      ret = send(handle, (rsp.c_str() + offset), (toBeSent - offset), 0);

      if(ret < 0) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l sent to peer is failed\n")));
        break;
      }

      offset += ret;
      ret = 0;

    } while((toBeSent != offset));
    
    return(ret);
}


std::string MicroService::get_contentType(std::string ext)
{
    std::string cntType("");
    /* get the extension now for content-type */
    if(!ext.compare("woff")) {
      cntType = "font/woff";
    } else if(!ext.compare("woff2")) {
      cntType = "font/woff2";
    } else if(!ext.compare("ttf")) {
      cntType = "font/ttf";
    } else if(!ext.compare("otf")) {
      cntType = "font/otf";
    } else if(!ext.compare("css")) {
      cntType = "text/css";
    } else if(!ext.compare("js")) {
      cntType = "text/javascript";
    } else if(!ext.compare("eot")) {
      cntType = "application/vnd.ms-fontobject";
    } else if(!ext.compare("html")) {
      cntType = "text/html";
    } else if(!ext.compare("svg")) {
      cntType = "image/svg+xml";
    } else if(!ext.compare("gif")) {
      cntType ="image/gif";
    } else if(!ext.compare("png")) {
      cntType = "image/png";
    } else if(!ext.compare("ico")) {
      cntType = "image/vnd.microsoft.icon";
    } else if(!ext.compare("jpg")) {
      cntType = "image/jpeg";
    } else if(!ext.compare("json")) {
      cntType = "application/json";
    } else {
      cntType = "text/html";
    }
    return(cntType);
}

/**
 * @brief This member function is used to create document in a collection for a given uri.
 * 
 * @param in 
 * @param dbInst 
 * @return std::string 
 */
std::string MicroService::handle_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare(0, 16, "/api/v1/shipment")) {
        return(handle_shipment_POST(in, dbInst));

    } else if(!uri.compare(0, 14, "/api/v1/config")) {
        return(handle_config_POST(in, dbInst));

    } else if(!uri.compare(0, 15, "/api/v1/account")) {
        return(handle_account_POST(in, dbInst));

    } else if(!uri.compare(0, 17, "/api/v1/inventory")) {
        return(handle_inventory_POST(in, dbInst));

    } else if(!uri.compare(0, 16, "/api/v1/document")) {
        return(handle_document_POST(in, dbInst));

    } else if(!uri.compare(0, 13, "/api/v1/email")) {
        return(handle_email_POST(in, dbInst));

    } else {
        return(build_responseOK(std::string()));

    }
}

std::string MicroService::handle_config_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/config/db")) {
        std::string content = http.body();
        
        if(content.length()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"), content.length()));
            std::string ip_address("");
            auto result = dbInst.from_json_element_to_string(content, "ip_address", ip_address);
            std::string port("");
            result = dbInst.from_json_element_to_string(content, "port", port);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l dbconfig ip:%s port:%u\n"), ip_address.c_str(), std::stoul(port)));
            /* Apply this config if changed */
        }
    }

    return(std::string());
}

std::string MicroService::handle_shipment_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/shipment/shipping")) {
        std::string collectionName("shipping");

        /*We need newly shipment No. */
        std::string projection("{\"_id\" : false, \"shipmentNo\" : true}");
        std::string content = http.body();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d \n Request http_body:%s\n"), content.length(), content.c_str()));

        if(content.length()) {

            std::string record = dbInst.create_document(dbInst.get_database(), collectionName, content);

            if(record.length()) {
                std::string rsp("");
                rsp = "{\"oid\" : \"" + record + "\"}";
                return(build_responseOK(rsp));
            }
        }

    } else if(!uri.compare("/api/v1/shipment/bulk/shipping")) {
        std::string content = http.body();
        std::string coll("shipping");

        if(content.length()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"), content.length()));

            std::int32_t cnt = dbInst.create_bulk_document(dbInst.get_database() ,coll, content);

            if(cnt) {
                std::string rec = "{\"createdShipments\": " + std::to_string(cnt) + "}";
                return(build_responseOK(rec));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Bulk Shipment Creation is failed\", \"errorCode\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        }

    } else if(!uri.compare("/api/v1/shipment/thirdparty/ajoul")) {
        //std::string req("{\"Shipment\":{\"reference\":\"AB100\",\"pickup_date\":null,\"pickup_time\":null,\"product_type\":\"104\",\"product_price\":null,\"destination\":\"RUH\",\"origin\":\"RUH\",\"parcel_quantity\":\"2\",\"parcel_weight\":\"4\",\"payment_mode\":\"COD\",\"service_id\":\"2\",\"description\":\"Testing Create Shipment From API\",\"sku\":\"423423\",\"customer_lng\":null,\"customer_lat\":null,\"sender\":{\"name\":\"Alaa\",\"address\":\"Al Haram street, Giza\",\"zip_code\":null,\"phone\":\"01063396459\",\"email\":\"admin@quadratechsoft.com\"},\"receiver\":{\"name\":\"Alaa\",\"address\":\"AL Malki, Damascuss\",\"zip_code\":\"1234\",\"phone\":\"0941951819\",\"phone2\":\"09419518549\",\"email\":\"info@quadratechsoft.com\"}},\"TrackingNumber\":\"AR222188000614391\",\"printLable\":\"https:\/\/ajoul.com\/printlabelone\/AR222188000614391\"}");
        //std::string awbNo = dbInst.get_value(req, "TrackingNumber");
        //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l TrackingNumber %s \n"), awbNo.c_str()));
        std::stringstream header("");
        //header = "Connection: close\r\n"
        //         "Cache-Control: no-cache\r\n";
                // "Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";

        std::stringstream apiAuthorizeAjoul("");

        apiAuthorizeAjoul << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
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
        apiAuthorizeAjoul << "client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o" 
                          <<  "&client_id=34&username=AKjHYuCAco&password=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o\r\n";*/
                            
        header << "Host: www.ajoul.com\r\n"
               << "Accept: application/json\r\n"
               << "User-Agent: Balaagh/1.0\r\n"
               << "Connection: keep-alive\r\n"
               << "Cache-Control: no-cache\r\n"
               << "Content-Type: application/x-www-form-urlencoded\r\n"
               << "Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
               //<<  "Content-Length: " << apiAuthorizeAjoul.str().length()
               <<  "Content-Length: 0"
               << "\r\n";
               //<<  "Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";
               
        //header << "\r\n";

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the header is\n%s\n"), header.str().c_str()));

        std::string apiURLAjoul = "https://ajoul.com/remote/api/v1/authorize";
        ACE_SSL_SOCK_Connector client;
        ACE_SSL_SOCK_Stream conn;
        ACE_INET_Addr connectAddr("ajoul.com:443");
        ACE_Time_Value to(2,0);

        if(client.connect(conn, connectAddr, &to) < 0) {

            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l connect to ajoul:443 is failed\n")));
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not rechable\", \"errorCode\" : 400}");
            return(build_responseERROR(err_message, err));

        } else {

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Connect to https://ajoul.com (%u) - %s is success\n"), 
                        connectAddr.get_ip_address(), connectAddr.get_host_addr()));
            
            std::stringstream postReq("");
            postReq << "POST /remote/api/v1/authorize?client_secret=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o&client_id=34&username=AKjHYuCAco&password=uCo9GJv4BATqU0C8491tTBooqY4CMttyg8kQyu1o HTTP/1.1\r\n" 
                                      << header.str()
                                      << "\r\n"; 
                                      //<< apiAuthorizeAjoul.str();

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the request is\n%s\n"), postReq.str().c_str()));

            if(conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

                ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not responding\", \"errorCode\" : 400}");
                return(build_responseERROR(err_message, err));

            } else {

                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to https://ajoul.com is success\n")));
                
                std::array<std::uint8_t, 3048> authRsp;
                authRsp.fill(0);
                ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);

                if(len < 0) {
                    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l recv from ajoul:443 is failed\n")));
                    std::string err("400 Bad Request");
                    std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not responding to Authorize req\", \"errorCode\" : 400}");
                    return(build_responseERROR(err_message, err));

                } else {
                    std::string rsp((char *)authRsp.data(), len);    
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"), rsp.c_str()));
                    Http http(rsp);
                    std::string access_token = dbInst.get_access_token_for_ajoul(http.body());
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The access_token is - %s\n"), access_token.c_str()));

                    /* Now building request for creating shipment */
                    std::stringstream shipmentCreate("");
                    std::stringstream hdr("");
                    
                    shipmentCreate << "{"
                                   << "\"receiver\":{"
    << "\"name\":\"Alaa\","
    << "\"country_code\": \"SA\","
    << "\"city_code\": \"RUH\","
    << "\"address\": \"AL Malki, Damascuss\","
    << "\"zip_code\": \"1234\","
    << "\"phone\": \"0941951819\","
    << "\"phone2\": \"09419518549\","
    << "\"email\": \"info@quadratechsoft.com\""
    << "},"
    << "\"sender\": {"
    << "\"name\": \"Alaa\","
    << "\"country_code\": \"SA\","
    << "\"city_code\": \"RUH\","
    << "\"address\": \"Al Haram street, Giza\","
    << "\"phone\": \"01063396459\","
    << "\"email\": \"admin@quadratechsoft.com\""
    << "},"
    << "\"reference\": \"AB100\","
    << "\"pick_date\": \"2018-08-06\","
    << "\"pickup_time\": \"12:49\","
    << "\"product_type\": \"104\","
    << "\"payment_mode\": \"COD\","
    << "\"parcel_quantity\": \"2\","
    << "\"parcel_weight\": \"4\","
    << "\"service_id\": \"2\","
    << "\"description\": \"Testing Create Shipment From API\","
    << "\"sku\": \"423423\","
    << "\"weight_total\": \"20\","
    << "\"total_cod_amount\": 50.9"
    << "}";
                    hdr << "Host: www.ajoul.com\r\n"
                        << "Accept: application/json\r\n"
                        << "User-Agent: Balaagh/1.0\r\n"
                        << "Connection: keep-alive\r\n"
                        << "Cache-Control: no-cache\r\n"
                        << "Content-Type: application/json\r\n"
                        << "Authorization: Bearer " << access_token << "\r\n"
                        <<  "Content-Length: " << shipmentCreate.str().length()
                        << "\r\n\r\n"
                        << shipmentCreate.str();
                    postReq.str("");
                    postReq << "POST /remote/api/v1/shipment/create HTTP/1.1\r\n"
                            << hdr.str();
                    if(conn.send_n(postReq.str().c_str(), postReq.str().length()) < 0) {

                        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l send to ajoul:443 is failed\n")));
                        std::string err("400 Bad Request");
                        std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"https://ajoul.com is not responding\", \"errorCode\" : 400}");
                        return(build_responseERROR(err_message, err));

                    } else {
                        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Sent to https://ajoul.com is success\n")));
                
                        std::array<std::uint8_t, 3048> authRsp;
                        authRsp.fill(0);
                        ssize_t len = conn.recv((void *)authRsp.data(), 3048, 0);
                        std::string rsp((char *)authRsp.data(), len);    
                        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Response is - %s\n"), rsp.c_str()));
                        Http http(rsp);
                        std::string ref("");
                        std::string awbNo = dbInst.get_tracking_no_for_ajoul(http.body(), ref);
                        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The Tracking Number is - %s refNumber %s\n"), awbNo.c_str(), ref.c_str()));
                    }
                    return(build_responseOK(http.body()));
                }
            }
        }
    }
    return(std::string());
}

std::string MicroService::handle_account_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/account/account")) {
        std::string collectionName("account");
        /*We need newly created account Code */
        std::string projection("{\"_id\" : false, \"accountCode\" : true}");
        std::string content = http.body();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http request body length:%d \n Request http_body:%s\n"), content.length(), content.c_str()));

        if(content.length()) {
            std::string oid = dbInst.create_document(dbInst.get_database(), collectionName, content);

            if(oid.length()) {
                //std::string rsp = dbInst.get_byOID(collectionName, projection, oid);
                std::string rsp("");
                rsp = "{\"oid\" : \"" + oid + "\"}";

                return(build_responseOK(rsp));
            }
        }
    }
    return(std::string());
}

std::string MicroService::handle_inventory_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/inventory")) {
        /* Creating sku for inventory */
        std::string content = http.body();
        std::string coll("inventory");

        if(content.length()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http body length:%d \n"), content.length()));
            std::string record = dbInst.create_document(dbInst.get_database(), coll, content);

            if(record.length()) {
                std::string rsp("");
                rsp = "{\"oid\" : \"" + record + "\"}";
                return(build_responseOK(rsp));
            }
        }
    }
    return(std::string());
}

std::string MicroService::handle_document_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/document")) {
        std::string content = http.body();
        std::string coll("attachment");

        if(content.length()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"), content.length()));
            std::string value("");
            auto result = dbInst.from_json_element_to_string(content, "corporate", value);

            if(!result) {
                coll.clear();
                coll = value + "_attachment";
            }

            std::string record = dbInst.create_document(dbInst.get_database(), coll, content);

            if(record.length()) {
                std::string rsp("");
                rsp = "{\"oid\" : \"" + record + "\"}";
                return(build_responseOK(rsp));

            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"attachment upload failed\", \"errorCode\" : 400}");
                return(build_responseERROR(err_message, err));

            }
        }
    }
    return(std::string());
}

std::string MicroService::handle_email_POST(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/email")) {
        /* Send e-mail with POST request */
        // {"subject": "", "to": [user-id@domain.com, user-id1@domain.com], "body": ""}
        std::string json_body = http.body();
        std::vector<std::string> out_vec;
        std::vector<std::tuple<std::string, std::string>> out_list;
        std::string subj;
        std::string body;
        std::string from;
        std::string passwd;

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email request:%s\n"), json_body.c_str()));
        dbInst.from_json_array_to_vector(json_body, "to", out_vec);
        dbInst.from_json_element_to_string(json_body, "subject", subj);
        dbInst.from_json_element_to_string(json_body, "emailbody", body);
        dbInst.from_json_object_to_map(json_body, "files", out_list);
        dbInst.from_json_element_to_string(json_body, "from", from);
        dbInst.from_json_element_to_string(json_body, "passwd", passwd);

        for(const auto& elm: out_vec) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email to list:%s\n"), elm.c_str()));
        }
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email subject:%s from:%s passwd:%s\n"), subj.c_str(), from.c_str(), passwd.c_str()));
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email body:%s\n"), body.c_str()));

        SMTP::Account::instance().to_email(out_vec);
        SMTP::Account::instance().email_subject(subj);
        SMTP::Account::instance().email_body(body);
        SMTP::Account::instance().from_email(from);
        SMTP::Account::instance().from_password(passwd);

        if(!out_list.empty()) {
            /* e-mail with attachment */
            SMTP::Account::instance().attachment(out_list);
        }

        SMTP::User email;
        email.startEmailTransaction();
        std::string rsp("{\"status\": \"success\"");
        return(build_responseOK(rsp));

    }
    return(std::string());
}

std::string MicroService::handle_GET(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());
    if(!uri.compare(0, 16, "/api/v1/shipment")) {
        return(handle_shipment_GET(in, dbInst));

    } else if(!uri.compare(0, 17, "/api/v1/inventory")) {
        return(handle_inventory_GET(in, dbInst));

    } else if(!uri.compare(0, 16, "/api/v1/document")) {
        return(handle_document_GET(in, dbInst));
        
    } else if(!uri.compare(0, 15, "/api/v1/account")) {
        return(handle_account_GET(in, dbInst));

    } else if((!uri.compare(0, 7, "/webui/"))) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l frontend Request %s\n"), uri.c_str()));
        /* build the file name now */
        std::string fileName("");
        std::string ext("");

        std::size_t found = uri.find_last_of(".");
        if(found != std::string::npos) {
          ext = uri.substr((found + 1), (uri.length() - found));
          fileName = uri.substr(6, (uri.length() - 6));
          std::string newFile = "../webgui/webui/" + fileName;
          ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The extension is %s\n"), newFile.c_str(), ext.c_str()));
          /* Open the index.html file and send it to web browser. */
          std::ifstream ifs(newFile.c_str());
          std::stringstream _str("");

          if(ifs.is_open()) {
              ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"), uri.c_str()));
              std::string cntType("");
              cntType = get_contentType(ext); 

              _str << ifs.rdbuf();
              ifs.close();
              return(build_responseOK(_str.str(), cntType));
          }
        }
    } else if(!uri.compare(0, 8, "/assets/")) {
        /* build the file name now */
        std::string fileName("");
        std::string ext("");

        std::size_t found = uri.find_last_of(".");
        if(found != std::string::npos) {
          ext = uri.substr((found + 1), (uri.length() - found));
          std::string newFile = "../webgui/webui" + uri;
          ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s The extension is %s\n"), newFile.c_str(), ext.c_str()));
          /* Open the index.html file and send it to web browser. */
          std::ifstream ifs(newFile.c_str(), ios::binary);
          std::stringstream _str("");
          std::string cntType("");

          if(ifs.is_open()) {
              ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"), uri.c_str()));

              cntType = get_contentType(ext);
              _str << ifs.rdbuf();
              ifs.close();

              return(build_responseOK(_str.str(), cntType));
          }
        }

    } else if((!uri.compare(0, 7, "/webui/"))) {
        std::string newFile = "../webgui/webui/index.html";
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"), newFile.c_str()));
        /* Open the index.html file and send it to web browser. */
        std::ifstream ifs(newFile.c_str(), ios::binary);
        std::stringstream _str("");
        std::string cntType("");

        if(ifs.is_open()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"), uri.c_str()));

            cntType = "text/html";
            _str << ifs.rdbuf();
            ifs.close();

            return(build_responseOK(_str.str(), cntType));
        }

    } else if(!uri.compare(0, 1, "/")) {
        std::string newFile = "../webgui/webui/index.html";
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l newFile Name is %s \n"), newFile.c_str()));
        /* Open the index.html file and send it to web browser. */
        std::ifstream ifs(newFile.c_str(), ios::binary);
        std::stringstream _str("");
        std::string cntType("");

        if(ifs.is_open()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request file %s - open successfully.\n"), uri.c_str()));

            cntType = "text/html";
            _str << ifs.rdbuf();
            ifs.close();

            return(build_responseOK(_str.str(), cntType));
        }
    }

    return(build_responseOK(std::string()));
}

std::string MicroService::handle_shipment_GET(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());
    std::string collectionName("shipping");

    if(!uri.compare("/api/v1/shipment/shipping")) {
        
        auto awbNo = http.get_element("awbNo");
        auto accountCode = http.get_element("accountCode");

        if(awbNo.length() && accountCode.length()) {

            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = awbNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }

            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";
        
            /* do an authentication with DB now */
            /*
            auto document = "{\"shipmentNo\" : {\"$in\" :" +
                               lst + "}, \"accountCode\": \"" +
                               accountCode + "\"}";*/
            std::stringstream document("");
            #if 0
            document << "{\"shipment.awbno\": {\"$in\": " << lst << "}"
                     << "\"shipment.awbno\": {\"$in\": "
            #endif
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document.str(), projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awb shipment(s):%s\n"), record.c_str()));
            return(build_responseOK(record));

        } else if(awbNo.length()) {

            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = awbNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }

            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";
        
            /* do an authentication with DB now */
            std::stringstream document("");
            document << "{\"shipment.awbno\": {\"$in\": " << lst << "}}";
            /*
            auto document = "{\"shipmentNo\" : {\"$in\" :" +
                               lst + "}}";*/
            
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document.str(), projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l document:%s awbNo response:%s\n"), document.str().c_str(),record.c_str()));
            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("altRefNo").length() && http.get_element("accountCode").length()) {
            
            auto altRefNo = http.get_element("altRefNo");
            auto accCode = http.get_element("accountCode");
            
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = altRefNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + altRefNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = altRefNo.find(delim, start);
            }

            lst += "\"" + altRefNo.substr(start) + "\"";
            lst += "]";
        
            /* do an authentication with DB now */
            auto document = "{\"altRefNo\" : {\"$in\" :" +
                               lst + "}, \"accountCode\": \"" +
                               accountCode + "\" " +
                            "}";
        
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l altRefNo response:%s\n"), record.c_str()));

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid ALT REF No.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
            
        } else if(http.get_element("altRefNo").length()) {
            auto altRefNo = http.get_element("altRefNo");
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = altRefNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + altRefNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = altRefNo.find(delim, start);
            }

            lst += "\"" + altRefNo.substr(start) + "\"";
            lst += "]";
        
            /* do an authentication with DB now */
            auto document = "{\"altRefNo\" : {\"$in\" :" +
                               lst + "}" +
                            "}";
            
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l altRefNo response:%s\n"), record.c_str()));

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid ALT REF No.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("senderRefNo").length() && http.get_element("accountCode").length()) {
        
            auto senderRefNo = http.get_element("senderRefNo");
            auto accCode = http.get_element("accountCode");
            
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = senderRefNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + senderRefNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = senderRefNo.find(delim, start);
            }

            lst += "\"" + senderRefNo.substr(start) + "\"";
            lst += "]";
        
            auto document = "{\"senderRefNo\" : {\"$in\" :" +
                               lst + "}, \"accountCode\": \"" +
                               accountCode + "\" " +
                            "}";
        
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l senderRefNo response:%s\n"), record.c_str()));

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Sender REF No.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
            
        } else if(http.get_element("senderRefNo").length()) {

            auto senderRefNo = http.get_element("senderRefNo");
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = senderRefNo.find(delim);

            while(end != std::string::npos)
            {
                lst += "\"" + senderRefNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = senderRefNo.find(delim, start);
            }

            lst += "\"" + senderRefNo.substr(start) + "\"";
            lst += "]";
        
            auto document = "{\"senderRefNo\" : {\"$in\" :" +
                               lst + "}" +
                            "}";
            
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l senderRefNo response:%s\n"), record.c_str()));

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Sender REF No.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("fromDate").length() && 
                  http.get_element("toDate").length() && 
                  http.get_element("country").length() && 
                  http.get_element("accountCode").length()) {
        
            auto fromDate = http.get_element("fromDate");
            auto toDate = http.get_element("toDate");
            auto country = http.get_element("country");
            auto accCode = http.get_element("accountCode");
            std::string document("");

            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = accCode.find(delim);
            while (end != std::string::npos)
            {
                lst += accCode.substr(start, end - start)  + delim;
                start = end + delim.length();
                end = accCode.find(delim, start);
            }
            lst +=  accCode.substr(start);
            lst += "]";

            document = "{\"shipment.senderInformation.accountNo\" : {\"$in\" : " +
                        lst + 
                        "}," +
                        "\"shipment.shipmentInformation.createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}," +
                        "\"shipment.receiverInformation.country\" :\"" + country + "\"}"; 
        
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The query:%s\n"), document.c_str()));

            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid input for detailed report.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("fromDate").length() && 
                  http.get_element("toDate").length() && 
                  http.get_element("accountCode").length()) {

            auto fromDate = http.get_element("fromDate");
            auto toDate = http.get_element("toDate");
            auto accCode = http.get_element("accountCode");
            std::string document("");

            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = accCode.find(delim);
            while (end != std::string::npos)
            {
                lst += accCode.substr(start, end - start)  + delim;
                start = end + delim.length();
                end = accCode.find(delim, start);
            }
            lst +=  accCode.substr(start);
            lst += "]";

            document = "{\"shipment.senderInformation.accountNo\" : {\"$in\" : " +
                        lst + 
                        "}," +
                        "\"shipment.shipmentInformation.createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}}"; 
        
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The query:%s\n"), document.c_str()));

            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid input for detailed report.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("fromDate").length() && 
                  http.get_element("toDate").length() && 
                  http.get_element("country").length()) {

            auto fromDate = http.get_element("fromDate");
            auto toDate = http.get_element("toDate");
            auto country = http.get_element("country");
            
            std::string document("");

            document = "{\"shipment.shipmentInformation.createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}," +
                        "\"shipment.receiverInformation.country\" :\"" + country + "\"}"; 
        
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The query:%s\n"), document.c_str()));

            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid input for detailed report.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else if(http.get_element("fromDate").length() && 
                  http.get_element("toDate").length()) {

            auto fromDate = http.get_element("fromDate");
            auto toDate = http.get_element("toDate");
            std::string document("");

            document = "{\"shipment.shipmentInformation.createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}}"; 
        
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The query:%s\n"), document.c_str()));

            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, document, projection);

            if(record.length()) {
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid input for detailed report.\", \"error\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        }
    }
    return(std::string());
}

std::string MicroService::handle_account_GET(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Request uri:%s\n"), uri.c_str()));

    if(!uri.compare("/api/v1/account/account")) {
        std::string collectionName("account");

        /* user is trying to log in - authenticate now */
        auto user = http.get_element("userId");
	    auto pwd = http.get_element("password");

        if(user.length() && pwd.length()) {
            /* do an authentication with DB now */
            std::string document = "{\"loginCredentials.accountCode\" : \"" +  user + "\", " + 
                                   "\"loginCredentials.accountPassword\" : \"" + pwd + "\"" + 
                                    "}";
            //std::string projection("{\"accountCode\" : true, \"_id\" : false}");
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_document(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l User details:%s\n"), record.c_str()));

            if(!record.length()) {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Credentials\", \"errorCode\" : 404}");
                return(build_responseERROR(err_message, err));
            } else {
                return(build_responseOK(record));
            }
        } else if(http.get_element("accountCode").length()) {

            auto accCode = http.get_element("accountCode");
            
            /* do an authentication with DB now */
            std::string document = "{\"loginCredentials.accountCode\" : \"" +  accCode + "\" " + 
                                    "}";
            //std::string projection("{\"accountCode\" : true, \"_id\" : false}");
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_document(collectionName, document, projection);
            if(record.length()) {
                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Customer Account Info %s\n"), record.c_str()));
                return(build_responseOK(record));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Account Code\", \"errorCode\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        } else {
            // for account list 
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_documents(collectionName, projection);
            if(!record.length()) {
                /* No Customer Account is found */
                std::string err("404 Not Found");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"There\'s no customer record\", \"error\" : 404}");
                return(build_responseERROR(err_message, err));
            }

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l account_list:%s\n"), record.c_str()));
            return(build_responseOK(record));
        }
    }
    return(std::string());
}

std::string MicroService::handle_inventory_GET(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/inventory")) {
      /* GET for inventory - could be all or based on sku */
        std::string document("");
        auto sku = http.get_element("sku");
        auto accCode = http.get_element("accountCode");

        if(accCode.length() > 0 && sku.length() > 0) {
            document = "{\"accountCode\": \"" + accCode + "\", \"sku\" : \""  + sku + "\"}"; 

        } else if (accCode.length() > 0) {
            document = "{\"accountCode\" : \""  + accCode + "\"}"; 
        } else {
            document = "{\"sku\" : \""  + sku + "\"}"; 
        }

        std::string collectionName("inventory");
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Inventory Querying\n")));

        if(record.length()) {
            return(build_responseOK(record));

        } else {
            /* No Customer Account is found */
            std::string err("404 Not Found");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"There\'s no Inventory Record\", \"error\" : 404}");
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l No Record is found \n")));
            return(build_responseERROR(err_message, err));
        }
    }
    return(std::string());
}

std::string MicroService::handle_email_GET(std::string& in, MongodbClient& dbInst)
{

}

std::string MicroService::handle_document_GET(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/document")) {
        //Getting the contents of attachment
        std::stringstream criteria("");
        std::string collection = http.get_element("corporate");
        std::string userId = http.get_element("userId");
        auto fileName = http.get_element("file");

        if(userId.length() > 0) {
            criteria << "{\"corporate\": \"" 
                     << collection 
                     << "\", \"userId\": \"" 
                     << userId 
                     << "\", \"file\":\"" 
                     << fileName 
                     << "\"}"; 
        } else {
            criteria << "{\"corporate\" : \""  
                     << collection
                     << "\"}"; 
        }

        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collection, criteria.str(), projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l attachment Querying for criteria %s\n"), criteria.str().c_str()));

    }
    return(std::string());
}

std::string MicroService::handle_config_GET(std::string& in, MongodbClient& dbInst)
{

}

/**
 * @brief this member function is used to Update the collection for a given uri.
 * 
 * @param in 
 * @param dbInst 
 * @return std::string 
 */
std::string MicroService::handle_PUT(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare(0, 16, "/api/v1/shipment")) {
        return(handle_shipment_PUT(in, dbInst));

    } else if(!uri.compare(0, 17, "/api/v1/inventory")) {
        return(handle_inventory_PUT(in, dbInst));

    } else if(!uri.compare(0, 15, "/api/v1/account")) {
        return(handle_account_PUT(in, dbInst));

    } else {
        std::string err("400 Bad Request");
        std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment Updated Failed\", \"error\" : 400}");
        return(build_responseERROR(err_message, err));
    }
}

std::string MicroService::handle_shipment_PUT(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/shipment/shipping")) {
        /** Update on Shipping */
        std::string coll("shipping");
        std::string content = http.body();
        std::string awbNo = http.get_element("shipmentNo");
        std::string accCode = http.get_element("accountCode");
        std::string isSingleShipment = http.get_element("isSingleShipment");

        if(isSingleShipment.length() > 0) {
            std::string query("");
            if(awbNo.length() && accCode.length()) {
                query = "{\"shipment.awbno\" : \"" +
                                    awbNo + "\"," + "\"shipment.shipmentInformation.activity.event\" :" + "{\"$ne\" : \"Proof of Delivery\"}" +
                                    ",\"accountCode\": \"" + accCode + "\"}"; 
        
            } else if(awbNo.length()) {
                query = "{\"shipment.awbno\" :\"" +
                             awbNo + "\"," + "\"shipment.shipmentInformation.activity.event\" :" + "{\"$ne\" : \"Proof of Delivery\"}"+ "}";
            }
            
            std::string document = "{\"$set\": " + content + "}";

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Updating document:%s\n query:%s\n"), document.c_str(), query.c_str()));
            bool rsp = dbInst.update_collection(coll, query, document);
            if(rsp) {
                std::string r("");
                r = "{\"status\": \"success\"}";
                return(build_responseOK(r));
            }
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment Updated Failed\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));

        } else {

            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = awbNo.find(delim);

            while (end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }

            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";

            std::string query("");
            if(awbNo.length() && accCode.length()) {
                query = "{\"shipment.awbno\" : {\"$in\" :" +
                                        lst + "}," + "\"shipment.shipmentInformation.activity.event\" :" + "{\"$ne\" : \"Proof of Delivery\"}" +
                                        ",\"accountCode\": \"" + accCode + "\"}"; 
        
            } else if(awbNo.length()) {
                query = "{\"shipment.awbno\" : {\"$in\" :" +
                             lst + "}," + "\"shipment.shipmentInformation.activity.event\" :" + "{\"$ne\" : \"Proof of Delivery\"}"+ "}";
            }

            std::string document = "{\"$push\": {\"shipment.shipmentInformation.activity\" : " + content + "}}";

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Updating document:%s\n query:%s\n"), document.c_str(), query.c_str()));
            bool rsp = dbInst.update_collection(coll, query, document);
        
            if(rsp) {
                std::string r("");
                r = "{\"status\": \"success\"}";
                return(build_responseOK(r));
            }
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment Updated Failed\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }
    }

    return(std::string());
}

std::string MicroService::handle_inventory_PUT(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/inventory")) {
      /* Updating inventory */
        std::string coll("inventory");
        std::string content = http.body();

        std::string sku = http.get_element("sku");
        std::string qty = http.get_element("qty");
        std::string acc = http.get_element("accountCode");
        std::string isUpdate = http.get_element("isUpdate");
        std::string query;

        if(sku.length() && qty.length() & acc.length()) {
          query = "{\"sku\" : \"" + sku +"\"," + "\"accountCode\" :" + "\"" + acc + "\"}";

        } else if(sku.length()) {
          query = "{\"sku\" : \"" + sku + "\"}";

        }

        std::string document("");
        if(isUpdate.length()) {
            document = "{\"$inc\": {\"qty\": " + qty + "}}";
        } else {
            document = "{\"$inc\": {\"qty\" : -" + qty + "}}";
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Updating document:%s\n query:%s\n"), document.c_str(), query.c_str()));
        bool rsp = dbInst.update_collection(coll, query, document);

        if(rsp) {
            std::string r("");
            r = "{\"status\": \"success\"}";
            return(build_responseOK(r));
        }

        std::string err("400 Bad Request");
        std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment Updated Failed\", \"error\" : 400}");
        return(build_responseERROR(err_message, err));
    }

    return(std::string());
}

std::string MicroService::handle_account_PUT(std::string& in, MongodbClient& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/v1/account/account")) {
        /* Updating inventory */
        std::string coll("account");
        std::string content = http.body();
        std::string accCode = http.get_element("userId");
      
        std::string query;

        if(accCode.length()) {
          query = "{\"loginCredentials.accountCode\" : \"" + accCode + "\"}";
        }

        std::string document;
        document = "{\"$update\":" + content + "}";
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Updating document:%s\n query:%s\n"), document.c_str(), query.c_str()));
        bool rsp = dbInst.update_collection(coll, query, document);

        if(rsp) {
            std::string r("");
            r = "{\"status\": \"success\"}";
            return(build_responseOK(r));
        }

        std::string err("400 Bad Request");
        std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Shipment Updated Failed\", \"error\" : 400}");
        return(build_responseERROR(err_message, err));
    }

    return(std::string());
}

std::string MicroService::handle_OPTIONS(std::string& in)
{
    ACE_UNUSED_ARG(in);
    std::string http_header;
    http_header = "HTTP/1.1 200 OK\r\n";
    http_header += "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n";
    http_header += "Access-Control-Allow-Headers: DNT, User-Agent, X-Requested-With, If-Modified-Since, Cache-Control, Content-Type, Range\r\n";
    http_header += "Access-Control-Max-Age: 1728000\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";
    http_header += "Content-Type: text/plain; charset=utf-8\r\n";
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n\r\n";
   // ACE_Message_Block* rsp = nullptr;

    //ACE_NEW_RETURN(rsp, ACE_Message_Block(512), nullptr);

    //std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    //rsp->wr_ptr(http_header.length());

    //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"), http_header.length(), http_header.c_str()));
    return(http_header);
}

std::string MicroService::build_responseCreated()
{
    std::string http_header;
    ACE_Message_Block* rsp = nullptr;

    http_header = "HTTP/1.1 201 Created\r\n";
    http_header += "Connection: keep-alive\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";
    http_header += "Content-Length: 0\r\n";

    //ACE_NEW_RETURN(rsp, ACE_Message_Block(256), nullptr);

    //std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    //rsp->wr_ptr(http_header.length());

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"), http_header.length(), http_header.c_str()));
    return(http_header);
}

std::string MicroService::build_responseOK(std::string httpBody, std::string contentType)
{
    std::string http_header;
    //ACE_Message_Block* rsp = nullptr;
    std::string rsp;

    http_header = "HTTP/1.1 200 OK\r\n";
    http_header += "Connection: keep-alive\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";

    if(httpBody.length()) {
        http_header += "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
        http_header += "Content-Type: " + contentType + "\r\n";
        http_header += "\r\n";

    } else {
        http_header += "Content-Length: 0\r\n";
    }
    rsp = http_header;

    if(httpBody.length()) {
        rsp += httpBody;
    }

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"), (http_header.length() + httpBody.length()), http_header.c_str()));
    return(rsp);
}

std::string MicroService::build_responseERROR(std::string httpBody, std::string error)
{
    std::string http_header;
    std::string contentType("application/json");

    http_header = "HTTP/1.1 " + error + " \r\n";
    http_header += "Connection: keep-alive\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";

    if(httpBody.length()) {
        http_header += "Content-Length: " + std::to_string(httpBody.length()) + "\r\n";
        http_header += "Content-Type: " + contentType + "\r\n";
        http_header += "\r\n";

    } else {
        http_header += "Content-Length: 0\r\n";
    }

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length:%d response header:%s"), (http_header.length() + httpBody.length()), http_header.c_str()));
    return(http_header);
}

ACE_INT32 MicroService::handle_signal(int signum, siginfo_t *s, ucontext_t *u)
{
    ACE_UNUSED_ARG(s);
    ACE_UNUSED_ARG(u);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Micro service gets signal %d\n"), signum));
    m_continue = false;

    return(0);
}

int MicroService::open(void *arg)
{
    ACE_UNUSED_ARG(arg);
    /*! Number of threads are 5, which is 2nd argument. */
    activate();
    return(0);
}

int MicroService::close(u_long flag)
{
    ACE_UNUSED_ARG(flag);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is closing\n")));
    return(0);
}

/*
 * @brief: This function is the entry point for Thread. Once the thread is spawned, control comes here
 *         and It blocks on message queue. The thread is termed as Worker.
 * @param: none
 * @return: 
 */
int MicroService::svc() 
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is spawned\n")));

    webServer().semaphore().release();

    while(m_continue) {
      ACE_Message_Block *mb = nullptr;
        if(-1 != getq(mb)) {
          std::uint32_t offset = 0;

            switch (mb->msg_type())
            {
            case ACE_Message_Block::MB_DATA:
            {
                /*_ _ _ _ _  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
                 | 4-bytes handle   | 4-bytes db instance pointer   | request (payload) |
                 |_ _ _ _ _ _ _ _ _ |_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _|_ _ _ _ _ _ _ _ _ _|
                */
                ACE_HANDLE handle = *((ACE_HANDLE *)&mb->rd_ptr()[offset]);
                //mb->rd_ptr(sizeof(ACE_HANDLE));
                offset += sizeof(ACE_HANDLE);

                std::uintptr_t inst = *((std::uintptr_t *)&mb->rd_ptr()[offset]);
                MongodbClient* dbInst = reinterpret_cast<MongodbClient*>(inst);
                //mb->rd_ptr(sizeof(uintptr_t));
                offset += sizeof(std::uintptr_t);
                /* Parent instance */
                std::uintptr_t parent_inst = *((std::uintptr_t *)&mb->rd_ptr()[offset]);
                WebServer* parent = reinterpret_cast<WebServer*>(parent_inst);
                //mb->rd_ptr(sizeof(uintptr_t));
                offset += sizeof(uintptr_t);

                ACE_UNUSED_ARG(parent);

                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l URI %s dbName %s\n"), dbInst->get_uri().c_str(), dbInst->get_database().c_str()));
                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l handle %d length %d \n"), handle, mb->length()));

                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l httpReq length %d\n"), mb->length()));
               
                std::string request((char *)&mb->rd_ptr()[offset], (mb->length() - offset)); 
                /*! Process The Request */
                //process_request(handle, *mb, *dbInst);
                process_request(handle, request, *dbInst);
                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l mb->reference_count() %d \n"), mb->reference_count()));
                mb->release();
		
                break;
            }
            case ACE_Message_Block::MB_PCSIG:
                {
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Got MB_PCSIG \n")));
                    m_continue = false;
                    if(mb != nullptr) {
                        mb->release();
                    }
                    msg_queue()->deactivate();
                    webServer().semaphore().release();
                    break;
                }
            default:
                {
                    m_continue = false;
                    mb->release();
                    break;
                }
            }
        } else {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l Micro service is stopped\n")));
            mb->release();
            m_continue = false;
        }
    }

    return(0);
}

MicroService::MicroService(ACE_Thread_Manager* thr_mgr, WebServer *parent) :
    ACE_Task<ACE_MT_SYNCH>(thr_mgr)
{
    m_continue = true;
    m_threadId = thr_mgr->thr_self();
    m_parent = parent;
}

MicroService::~MicroService()
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Microservice dtor is invoked\n")));
    m_parent = nullptr;
}

/*
 * _                  _   _ _ _    _          _ _ _ _  _ _ _ _ _ _ _ _ _   _        _  _ _ _ _ _ _ _ _ 
 * \\       /\       //  // _ \ \ ||         ||      || ||      ||      \\ \\      // ||      ||      \\
 *  \\     //\\     //_ //_ _ _\_\||_ _ _    || _  _    ||_ _   ||      //  \\    //  ||_ _   ||      //
 *   \\   //  \\   // -//         ||     \\          || ||      || _ //      \\  //   ||      || _ //
 *    \\_//    \\_//   \\_ _ _ _  ||_ _ _//  || _ _ _|| ||_ _ _ ||     \\     \\//    ||_ _ _ ||     \\
 *
 *                
 */


ACE_INT32 WebServer::handle_timeout(const ACE_Time_Value& tv, const void* act)
{
    ACE_UNUSED_ARG(tv);
    std::uintptr_t _handle = reinterpret_cast<std::uintptr_t>(act);

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebServer::handle_timedout for connection %d\n"), _handle));
    auto conIt = m_connectionPool.find(_handle);

    if(conIt != std::end(m_connectionPool)) {

        WebConnection* connEnt = conIt->second;
        m_connectionPool.erase(conIt);
        /* let the reactor call handle_close on this handle */
        ACE_Reactor::instance()->remove_handler(_handle, ACE_Event_Handler::READ_MASK | 
                                                         ACE_Event_Handler::TIMER_MASK | 
                                                         ACE_Event_Handler::SIGNAL_MASK);
        stop_conn_cleanup_timer(connEnt->timerId());
        /* reclaim the heap memory */
        delete connEnt;
        close(_handle);

    } else {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebServer::handle_timedout no connEnt found for handle %d\n"), _handle));
    }

    return(0);
}

ACE_INT32 WebServer::handle_input(ACE_HANDLE handle)
{
    ACE_UNUSED_ARG(handle);
    int ret_status = 0;
    ACE_SOCK_Stream peerStream;
    ACE_INET_Addr peerAddr;
    WebConnection* connEnt = nullptr;

    ret_status = m_server.accept(peerStream, &peerAddr);

    if(!ret_status) {
        auto it = m_connectionPool.find(peerStream.get_handle());
        if(it != std::end(m_connectionPool)) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Existing connection on handle %d found in connection pool\n"), peerStream.get_handle()));
            connEnt = it->second;
        } else {
            ACE_NEW_RETURN(connEnt, WebConnection(this), -1);
            m_connectionPool[peerStream.get_handle()] = connEnt;
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l New connection is created and handle is %d peer ip address %u (%s) peer port %d\n"), 
                peerStream.get_handle(),
                peerAddr.get_ip_address(), peerAddr.get_host_addr(), peerAddr.get_port_number()));

            /*! Start Handle Cleanup Timer to get rid of this handle from connectionPool*/
            long tId = start_conn_cleanup_timer(peerStream.get_handle());
            connEnt->timerId(tId);
            connEnt->handle(peerStream.get_handle());
            connEnt->connAddr(peerAddr);

            ACE_Reactor::instance()->register_handler(connEnt, 
                                                      ACE_Event_Handler::READ_MASK |
                                                      ACE_Event_Handler::TIMER_MASK | 
                                                      ACE_Event_Handler::SIGNAL_MASK);
        }
    } else {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Accept to new connection failed\n")));
    }

    return(0);
}

ACE_INT32 WebServer::handle_signal(int signum, siginfo_t* s, ucontext_t* ctx)
{
    ACE_UNUSED_ARG(s);
    ACE_UNUSED_ARG(ctx);

    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Signal Number %d and its name %S is received for WebServer\n"), signum, signum));

    if(!workerPool().empty()) {
        std::for_each(workerPool().begin(), workerPool().end(), [&](MicroService* ms) ->void {

            semaphore().acquire();
            ACE_Message_Block* req = nullptr;
            ACE_NEW_NORETURN(req, ACE_Message_Block((size_t)MemorySize::SIZE_1KB));
            req->msg_type(ACE_Message_Block::MB_PCSIG);
            ms->putq(req);
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Sending to Worker Node\n")));

        });
    }

    if(!connectionPool().empty()) {
      for(auto it = connectionPool().begin(); it != connectionPool().end();) {

        auto wc = it->second;
        it = connectionPool().erase(it);
        stop_conn_cleanup_timer(wc->timerId());
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l its name %S is received for WebServer\n"), signum));
        delete wc;

      }
      
      connectionPool().clear();
    }

    ACE_Reactor::instance()->remove_handler(m_server.get_handle(), ACE_Event_Handler::ACCEPT_MASK | 
                                                         ACE_Event_Handler::TIMER_MASK | 
                                                         ACE_Event_Handler::SIGNAL_MASK);

    return(0);
}

ACE_INT32 WebServer::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask mask)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Closing connection on handle %d for webServer\n"), handle));
    if(handle > 0) {
        m_server.close();
        m_stopMe = true;
    }
    ACE_UNUSED_ARG(mask);
    
    return(0);
}

ACE_HANDLE WebServer::get_handle() const
{
    return(m_server.get_handle());
}

WebServer::WebServer(std::string ipStr, ACE_UINT16 listenPort, ACE_UINT32 workerPool,
                     std::string dbUri,
                     std::string dbConnPool,
                     std::string dbName)
{
    std::string addr;
    addr.clear();

    if(ipStr.length()) {
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
    
    if(dbUri.length()) {
        uri.assign(dbUri);
    }

    if(dbConnPool.length()) {
        _pool = std::stoi(dbConnPool);
    }

    if(dbName.length()) {
        _dbName.assign(dbName);
    }

    //mMongodbc = new MongodbClient(uri);
    mMongodbc = std::make_unique<MongodbClient>(uri);

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

    /* Start listening for incoming connection */
    int reuse_addr = 1;
    if(m_server.open(m_listen, reuse_addr)) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Starting of WebServer failed - opening of port %d hostname %s\n"), 
                m_listen.get_port_number(), m_listen.get_host_name()));
    }
}

WebServer::~WebServer()
{
    /*
    if(nullptr != mMongodbc) {
        delete mMongodbc;
        mMongodbc = nullptr;
    }*/
    mMongodbc.reset(nullptr);

    if(!workerPool().empty()) {
        for(auto it = workerPool().begin(); it != workerPool().end();) {
            
            auto ent = *it;
            delete ent;
            it = workerPool().erase(it);

        }
        workerPool().clear();
    }

    //delete m_semaphore;
    m_semaphore.reset(nullptr);
}

bool WebServer::start()
{
    ACE_Reactor::instance()->register_handler(this, ACE_Event_Handler::ACCEPT_MASK | 
                                                    ACE_Event_Handler::TIMER_MASK |
                                                    ACE_Event_Handler::SIGNAL_MASK); 
    /* subscribe for signal */
    ACE_Sig_Set ss;
    ss.empty_set();
    ss.sig_add(SIGINT);
    ss.sig_add(SIGTERM);
    ACE_Reactor::instance()->register_handler(&ss, this); 

    ACE_Time_Value to(1,0);

    while(!m_stopMe) {
        ACE_INT32 ret = ACE_Reactor::instance()->handle_events(to);
        if(ret < 0) break;
    }

    ACE_Reactor::instance()->remove_handler(ss); 
    return(0);
}

bool WebServer::stop()
{
    return(true);
}

long WebServer::start_conn_cleanup_timer(ACE_HANDLE handle, ACE_Time_Value to)
{
    long timerId = -1;
    /* 30 minutes */
    //ACE_Time_Value to(1800,0);
    timerId = ACE_Reactor::instance()->schedule_timer(this, (const void *)handle, to);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l webserver cleanup timer is started for handle %d\n"), handle));
    return(timerId);
}

void WebServer::stop_conn_cleanup_timer(long timerId) 
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l webserver connection cleanup timer is stopped\n")));
    if(ACE_Reactor::instance()->cancel_timer(timerId)) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Running timer is stopped succesfully\n")));
    }
}
void WebServer::restart_conn_cleanup_timer(ACE_HANDLE handle, ACE_Time_Value to)
{
    //ACE_Time_Value to(20,0);
    auto conIt = connectionPool().find(handle);

    if(conIt != std::end(connectionPool())) {
        auto connEnt = conIt->second;
        long tId = connEnt->timerId();

        if(!ACE_Reactor::instance()->reset_timer_interval(tId, to)) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l webserver connection cleanup timer is re-started for handle %d\n"), handle));
        }
    }

}

WebConnection::WebConnection(WebServer* parent)
{
    m_timerId = -1;
    m_handle = -1;
    m_parent = parent;
    m_req = NULL;
    m_expectedLength = -1;
}

WebConnection::~WebConnection()
{
    close(m_handle);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebConnection dtor is invoked\n")));
}

ACE_INT32 WebConnection::handle_timeout(const ACE_Time_Value &tv, const void *act)
{
    (void)tv;
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Webconnection::handle_timedout\n")));
    std::uintptr_t handle = reinterpret_cast<std::uintptr_t>(act);
    auto conIt = m_parent->connectionPool().find(handle);

    if(conIt != std::end(m_parent->connectionPool())) {
        m_parent->connectionPool().erase(conIt);
    }
    return(0);
}

ACE_INT32 WebConnection::handle_input(ACE_HANDLE handle)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l handle_input on handle %d\n"), m_handle));
    if(!isBufferingOfRequestCompleted()) {
        return(0);
    }

    if(!m_expectedLength) {

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Request of length %d on connection %d" 
                                        " memory will be reclaimed upon timer expiry\n"), m_expectedLength, m_handle));
        if(timerId() > 0) {
            /* start 1/2 second timer i.e. 500 milli second*/
            ACE_Time_Value to(0,1);
            parent()->stop_conn_cleanup_timer(timerId());
            m_timerId = parent()->start_conn_cleanup_timer(handle, to);
            return(-1);
        }

        return(-1);
    }

    /* Request is buffered now start processing it */
    ACE_Message_Block* req = NULL;

    ACE_NEW_NORETURN(req, ACE_Message_Block((size_t) (m_expectedLength + 512)));
    req->reset();
    req->msg_type(ACE_Message_Block::MB_DATA);

    /*_ _ _ _ _  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ 
     | 4-bytes handle   | 4-bytes db instance pointer   | 4 bytes Parent Instance |request (payload) |
     |_ _ _ _ _ _ _ _ _ |_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _|_ _ _ _ _ _ _ _ _ __ __ _|_ _ _ _ _ _ _ _ _ |
     */
    *((ACE_HANDLE *)req->wr_ptr()) = handle;
    //req->copy((char *)&handle, sizeof(ACE_HANDLE));
    req->wr_ptr(sizeof(ACE_HANDLE));

    /* db instance */
    std::uintptr_t inst = reinterpret_cast<std::uintptr_t>(parent()->mongodbcInst());
    *((std::uintptr_t* )req->wr_ptr()) = inst;
    //req->copy((char *)&inst, sizeof(uintptr_t));
    req->wr_ptr(sizeof(uintptr_t));

    /* parent instance */
    std::uintptr_t parent_inst = reinterpret_cast<std::uintptr_t>(parent());
    *((std::uintptr_t* )req->wr_ptr()) = parent_inst;
    //req->copy((char *)&parent_inst, sizeof(uintptr_t));
    req->wr_ptr(sizeof(uintptr_t));

    std::int32_t len = m_req->length();
    std::memcpy(req->wr_ptr(), m_req->rd_ptr(), len);
    //req->copy(m_req->rd_ptr(), len);
    req->wr_ptr(len);

    /* Reclaim the memory now */
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l m_req->reference_count() %d \n"), m_req->reference_count()));
    m_req->release();
    
    m_expectedLength = -1;

    auto it = m_parent->currentWorker();
    MicroService* mEnt = *it;
    if(mEnt->putq(req) < 0) {
        req->release();
	}

    return(0);
}

ACE_INT32 WebConnection::handle_signal(int signum, siginfo_t *s, ucontext_t *u)
{
    ACE_UNUSED_ARG(s);
    ACE_UNUSED_ARG(u);

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l signal number - %d (%S) is received\n"), signum));
    if(m_timerId > 0) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Running timer is stopped for signal (%S)\n"), signum));
        m_parent->stop_conn_cleanup_timer(m_timerId);
    }
    return(0);
}

ACE_INT32 WebConnection::handle_close (ACE_HANDLE handle, ACE_Reactor_Mask mask)
{
    ACE_UNUSED_ARG(mask);
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebConnection::handle_close handle %d will be closed upon timer expiry\n"), handle));
    return(0);
}

ACE_HANDLE WebConnection::get_handle() const
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebConnection::get_handle - handle %d\n"), m_handle));
    return(m_handle);
}

bool WebConnection::isCompleteRequestReceived()
{
    if((m_req != NULL) && ((ACE_INT32)m_req->length() == m_expectedLength)) {
        return(true);
    }

    return(false);
}

bool WebConnection::isBufferingOfRequestCompleted() 
{
    /* This is a new Request */
    std::array<std::uint8_t, 2048> scratch_pad;
    std::int32_t len = -1;
    /** read first 2048 bytes or less */
    scratch_pad.fill(0);

    if(m_expectedLength < 0) {
        len = recv(handle(), scratch_pad.data(), scratch_pad.size(), 0);

        if(len < 0) {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
            return(len);
        } else {

            if(!len) {
                /* Connection is closed now */
                m_expectedLength = len;
                return(true);
            }

            std::string pre_process_req((const char*)scratch_pad.data(), len);
            Http http(pre_process_req);

            if(http.header().length()) {
                std::string CT = http.get_element("Content-Type");
                std::string CL = http.get_element("Content-Length");

                if(CT.length() && !CT.compare("application/json")) {
                    /* This Must be POST or PUT Method. */
                    if(CL.length()) {
                        std::int32_t expected_length = http.header().length() + std::stoi(CL);
                        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Expected Length %d received length %d\n"), expected_length, len));

                        /* +512 is to avoid buffer overflow */
                        ACE_NEW_NORETURN(m_req, ACE_Message_Block((size_t)(expected_length + 512)));
                        /* copy the read buffer into m_req data member */
                        std::memcpy(m_req->wr_ptr(), scratch_pad.data(), len);
                        m_req->wr_ptr(len);
                        //m_req->copy((char *)scratch_pad.data(), len);
                        m_expectedLength = expected_length;

                    } else {

                        /* +512 is to avoid buffer overflow */
                        ACE_NEW_NORETURN(m_req, ACE_Message_Block((size_t)(len + 512)));
                        /* copy the read buffer into m_req data member */
                        std::memcpy(m_req->wr_ptr(), scratch_pad.data(), len);
                        m_req->wr_ptr(len);
                        m_expectedLength = len;

                    }
                } else {
                    /* Content-Length: is not present in the Header */
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Expected Length %d received length %d\n"), len, len));
                    /* +512 is to avoid buffer overflow */
                    ACE_NEW_NORETURN(m_req, ACE_Message_Block((size_t)(len + 512)));
                    /* copy the read buffer into m_req data member */
                    std::memcpy(m_req->wr_ptr(), scratch_pad.data(), len);
                    m_req->wr_ptr(len);
                    //m_req->copy((char *)scratch_pad.data(), len);
                    m_expectedLength = len;
                }
            }

            return(isCompleteRequestReceived());
        }
    } else {

        /* Keep buffering the remaining request */
        std::int32_t remainingLength = m_expectedLength - m_req->length();
        std::int32_t offset = 0;
        char* buf = (char* )m_req->wr_ptr();

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Reading in loop for handle %d\n"), m_handle));
        do {

            len = recv(handle(), (buf + offset), (remainingLength - offset), 0);
            if(len < 0) {
                ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
                return(true);
            }

            offset += len;
            m_req->wr_ptr(len);

        }while(offset != remainingLength);

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l offset %d remainingLength %d\n"), offset, remainingLength));

        if(len < 0) {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
            return(true);
        } else {
            /* do we have all contents of a request? */
            return(isCompleteRequestReceived());
        }
    }
}

#endif /* __webservice_cc__*/
