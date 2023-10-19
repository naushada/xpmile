#ifndef __webservice_cc__
#define __webservice_cc__

#include "webservice.h"
#include "http_parser.h"

ACE_Message_Block* MicroService::handle_DELETE(std::string& in, Mongodbc& dbInst)
{
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());
    std::string document("");

    if(!uri.compare("/api/deleteAwbList")) {
        /** Delete Shipment */
        std::string coll("shipping");
        std::string awbNo = http.get_element("awbList");
        std::string startDate = http.get_element("startDate");
        std::string endDate = http.get_element("endDate");
        if(awbNo.length()) {
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

ACE_INT32 MicroService::process_request(ACE_HANDLE handle, ACE_Message_Block& mb, Mongodbc& dbInst)
{
    std::string http_header, http_body;
    http_header.clear();
    http_body.clear();
    ACE_Message_Block* rsp = nullptr;

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
    if(nullptr != rsp) {

      std::string response(rsp->rd_ptr(), rsp->length());
      /** reclaim the memory now*/
      rsp->release();
      std::int32_t  toBeSent = response.length();
      std::int32_t offset = 0;
      do {
        ret = send(handle, (response.c_str() + offset), (toBeSent - offset), 0);
        if(ret < 0) {
          ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l sent to peer is failed\n")));
          break;
        }
        offset += ret;
        ret = 0;
      } while((toBeSent != offset));
    }
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

ACE_Message_Block* MicroService::handle_POST(std::string& in, Mongodbc& dbInst)
{
    /* Check for Query string */
    Http http(in);
    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/shipping")) {
        std::string collectionName("shipping");
        /*We need newly shipment No. */
        std::string projection("{\"_id\" : false, \"shipmentNo\" : true}");
        std::string content = http.body();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http request body length %d \n Request Body %s\n"), content.length(), content.c_str()));
        if(content.length()) {
            std::string record = dbInst.create_document(collectionName, content);

            if(record.length()) {
                std::string rsp("");
                rsp = "{\"oid\" : \"" + record + "\"}";
                return(build_responseOK(rsp));
            }
        } 
    } else if(!uri.compare("/api/account")) {
        std::string collectionName("account");
        /*We need newly created account Code */
        std::string projection("{\"_id\" : false, \"accountCode\" : true}");
        std::string content = http.body();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http request body length %d \n Request Body %s\n"), content.length(), content.c_str()));
        if(content.length()) {
            std::string oid = dbInst.create_document(collectionName, content);

            if(oid.length()) {
                //std::string rsp = dbInst.get_byOID(collectionName, projection, oid);
                std::string rsp("");
                rsp = "{\"oid\" : \"" + oid + "\"}";

                return(build_responseOK(rsp));
            }
        }
    } else if(!uri.compare("/api/bulk/shipping")) {
        std::string content = http.body();
        std::string coll("shipping");
        if(content.length()) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l http body length %d \n"), content.length()));
            std::int32_t cnt = dbInst.create_bulk_document(coll, content);

            if(cnt) {
                std::string rec = "{\"createdShipments\": " + std::to_string(cnt) + "}";
                return(build_responseOK(rec));
            } else {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Bulk Shipment Creation is failed\", \"errorCode\" : 400}");
                return(build_responseERROR(err_message, err));
            }
        }
    } 
    return(build_responseOK(std::string()));
}

ACE_Message_Block* MicroService::handle_GET(std::string& in, Mongodbc& dbInst)
{
    size_t ct_offset = 0, cl_offset = 0;
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l API Name %s\n"), uri.c_str()));
    if(!uri.compare("/api/login")) {
        std::string collectionName("account");

        /* user is trying to log in - authenticate now */
        auto user = http.get_element("userId");
	    auto pwd = http.get_element("password");

        if(user.length() && pwd.length()) {
            /* do an authentication with DB now */
            std::string document = "{\"accountCode\" : \"" +  user + "\", " + 
                                   "\"accountPassword\" : \"" + pwd + "\"" + 
                                    "}";
            //std::string projection("{\"accountCode\" : true, \"_id\" : false}");
            std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_document(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l User or Customer details %s\n"), record.c_str()));
            if(!record.length()) {
                std::string err("400 Bad Request");
                std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Credentials\", \"errorCode\" : 404}");
                return(build_responseERROR(err_message, err));
            } else {
                return(build_responseOK(record));
            }
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"User Id or Password Not provided\", \"errorCode\" : 400}");
            return(build_responseERROR(err_message, err));
        }
    } else if(!uri.compare("/api/account")) {
        std::string collectionName("account");

        /* user is trying to log in - authenticate now */
        auto user = http.get_element("accountCode");

        if(user.length()) {
            /* do an authentication with DB now */
            std::string document = "{\"accountCode\" : \"" +  user + "\" " + 
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
        }

    } else if(!uri.compare("/api/accountlist")) {
        std::string collectionName("account");

        /* do an authentication with DB now */
        std::string query = "{\"role\" : \"Customer\" }";

        //std::string projection("{\"accountCode\" : true, \"_id\" : false}");
        std::string projection("{\"_id\" : false, \"accountCode\": true, \"name\" : true}");
        std::string record = dbInst.get_documents(collectionName, query, projection);
        if(!record.length()) {
            /* No Customer Account is found */
            std::string err("404 Not Found");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"There\'s no customer record\", \"error\" : 404}");
            return(build_responseERROR(err_message, err));
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Customer Account Info %s\n"), record.c_str()));
        return(build_responseOK(record));

    } else if(!uri.compare("/api/shipping") || (!uri.compare("/api/shipment"))) {
        std::string collectionName("shipping");
        auto awbNo = http.get_element("shipmentNo");
        auto accountCode = http.get_element("accountCode");

        if(awbNo.length() && accountCode.length()) {
            /* do an authentication with DB now */
            std::string document = "{\"accountCode\" : \"" +
                                     accountCode + "\" ," +
                                    "\"shipmentNo\" : \"" +
                                    awbNo + "\"" +
                                    "}";
           std::string projection("{\"_id\" : false}");
            std::string record = dbInst.get_document(collectionName, document, projection);
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l AWB Way Bills %s\n"), record.c_str()));
            return(build_responseOK(record));
        }
    } else if((!uri.compare("/api/altrefno"))) {
        std::string collectionName("shipping");
        auto altRefNo = http.get_element("altRefNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        if(altRefNo.length() && accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"altRefNo\" : \"" +
                        altRefNo + "\", \"accountCode\" :\"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"altRefNo\" : \"" +
                        altRefNo + "\" " +
                        "}";

        }
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_document(collectionName, document, projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l altRefNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid ALT REF No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

    } else if((!uri.compare("/api/awbno"))) {
        std::string collectionName("shipping");
        auto awbNo = http.get_element("shipmentNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        if(awbNo.length() && accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"shipmentNo\" : \"" +
                        awbNo + "\",\"accountCode\": \"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"shipmentNo\" : \"" +
                        awbNo + "\" " +
                        "}";
        }

        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_document(collectionName, document, projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awbNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

    } else if(!uri.compare("/api/awbnolist")) {
        std::string collectionName("shipping");
        auto awbNo = http.get_element("shipmentNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        std::string lst("[");
        std::string delim = ",";
        auto start = 0U;
        if(awbNo.length()) {

            auto end = awbNo.find(delim);
            while (end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }
            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";
        }

        if(accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"shipmentNo\" : {\"$in\" :" +
                        lst + "},\"accountCode\": \"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"shipmentNo\" : {\"$in\" : " +
                        lst + 
                        "}}";
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l DB Query %s\n"), document.c_str()));
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awbNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

    } else if(!uri.compare("/api/altrefnolist")) {
        std::string collectionName("shipping");
        auto awbNo = http.get_element("shipmentNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        std::string lst("[");
        std::string delim = ",";
        auto start = 0U;
        if(awbNo.length()) {

            auto end = awbNo.find(delim);
            while (end != std::string::npos)
            {
                lst += "\"" + awbNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = awbNo.find(delim, start);
            }
            lst += "\"" + awbNo.substr(start) + "\"";
            lst += "]";
        }

        if(accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"altRefNo\" : {\"$in\" :" +
                        lst + "},\"accountCode\": \"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"altRefNo\" : {\"$in\" : " +
                        lst + 
                        "}}";
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l DB Query %s\n"), document.c_str()));
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awbNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

    } else if(!uri.compare("/api/senderRefNoList")) {
        std::string collectionName("shipping");
        auto refNo = http.get_element("senderRefNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        std::string lst("[");
        std::string delim = ",";
        auto start = 0U;
        if(refNo.length()) {

            auto end = refNo.find(delim);
            while (end != std::string::npos)
            {
                lst += "\"" + refNo.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = refNo.find(delim, start);
            }
            lst += "\"" + refNo.substr(start) + "\"";
            lst += "]";
        }

        if(accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"referenceNo\" : {\"$in\" :" +
                        lst + "},\"accountCode\": \"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"referenceNo\" : {\"$in\" : " +
                        lst + 
                        "}}";
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l DB Query %s\n"), document.c_str()));
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awbNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid Reference No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }

    } else if(!uri.compare("/api/senderRefNo")) {
        std::string collectionName("shipping");
        auto refNo = http.get_element("senderRefNo");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        if(refNo.length() && accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"referenceNo\" : \"" +
                        refNo + "\",\"accountCode\": \"" +
                        accCode + "\" " +
                        "}";
        } else {
            document = "{\"referenceNo\" : \"" +
                        refNo + "\" " +
                        "}";
        }

        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_document(collectionName, document, projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l RefNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }
    } else if(!uri.compare("/api/detailed_report")) {
        std::string collectionName("shipping");
        auto fromDate = http.get_element("fromDate");
        auto toDate = http.get_element("toDate");
        auto country = http.get_element("country");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        if(fromDate.length() && toDate.length() && country.length() && accCode.length()) {
            std::string lst("[");
            std::string delim = ",";
            auto start = 0U;
            auto end = accCode.find(delim);
            while (end != std::string::npos)
            {
                lst += "\"" + accCode.substr(start, end - start) + "\"" + delim;
                start = end + delim.length();
                end = accCode.find(delim, start);
            }
            lst += "\"" + accCode.substr(start) + "\"";
            lst += "]";

            document = "{\"accountCode\" : {\"$in\" : " +
                        lst + 
                        "}," +
                        "\"createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}," +
                        "\"country\" :\"" + country + "\"}"; 
        } else if(fromDate.length() && toDate.length() && country.length()) {

            document = "{\"createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}," +
                        "\"country\" :\"" + country + "\"}"; 
        }

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l DB Query %s\n"), document.c_str()));
        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l awbNo Response %s\n"), record.c_str()));
        if(record.length()) {
            return(build_responseOK(record));
        } else {
            std::string err("400 Bad Request");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"Invalid AWB Bill No.\", \"error\" : 400}");
            return(build_responseERROR(err_message, err));
        }
    } else if((!uri.compare("/api/shipmentlist"))) {
        std::string collectionName("shipping");
        auto fromDate = http.get_element("fromDate");
        auto toDate = http.get_element("toDate");
        auto accCode = http.get_element("accountCode");
        std::string document("");

        if(fromDate.length() && toDate.length() && accCode.length()) {
            /* do an authentication with DB now */
            document = "{\"$and\": [{\"accountCode\": \"" + accCode + "\"}, {\"createdOn\" : {\"$gte\": \""  + fromDate + "\"," + 
                        "\"$lte\": \"" + toDate + "\"}}]}";

        } else {
            /* do an authentication with DB now */
            document = "{\"createdOn\" : {\"$gte\": \""  + fromDate + "\"" + 
                        ", \"$lte\": \"" + toDate + "\"" + "}}";
        }

        std::string projection("{\"_id\" : false}");
        std::string record = dbInst.get_documents(collectionName, document, projection);
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l shipmentList Query %s\n"), document.c_str()));

        if(record.length()) {
            return(build_responseOK(record));

        } else {
            /* No Customer Account is found */
            std::string err("404 Not Found");
            std::string err_message("{\"status\" : \"faiure\", \"cause\" : \"There\'s no Accountrecord\", \"error\" : 404}");
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l No Record is found \n")));
            return(build_responseERROR(err_message, err));
        }

    } else if((!uri.compare(0, 6, "/bayt/"))) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l frontend Request %s\n"), uri.c_str()));
        /* build the file name now */
        std::string fileName("");
        std::string ext("");

        std::size_t found = uri.find_last_of(".");
        if(found != std::string::npos) {
          ext = uri.substr((found + 1), (uri.length() - found));
          fileName = uri.substr(6, (uri.length() - 6));
          std::string newFile = "../webgui/ui/" + fileName;
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
          std::string newFile = "../webgui/ui" + uri;
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

    } else if((!uri.compare(0, 6, "/bayt/"))) {
        std::string newFile = "../webgui/ui/index.html";
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
        std::string newFile = "../webgui/ui/index.html";
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

ACE_Message_Block* MicroService::handle_PUT(std::string& in, Mongodbc& dbInst)
{
    /* Check for Query string */
    Http http(in);

    /* Action based on uri in get request */
    std::string uri(http.get_uriName());

    if(!uri.compare("/api/shipment")) {
        /** Update on Shipping */
        std::string coll("shipping");
        std::string content = http.body();
        std::string awbNo = http.get_element("shipmentNo");
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

        #if 0
        std::string accountCode = http.get_element("accountCode");
        std::string query = "{\"accountCode\" : \"" +
                             accountCode + "\" ," +
                             "\"shipmentNo\" : \"" +
                             awbNo + "\"" +
                             "}";
        #endif
        std::string query = "{\"shipmentNo\" : {\"$in\" :" +
                             lst + "}," + "\"activity.event\" :" + "{\"$ne\" : \"Proof of Delivery\"}"+ "}";

        #if 0
        std::string query = "{\"shipmentNo\" : " +
                             lst + "}";

        #endif
        std::string document = "{\"$push\": {\"activity\" : " + content + "}}";

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Updating document %s\n Query %s\n"), document.c_str(), query.c_str()));
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

    return(build_responseOK(std::string()));
}

ACE_Message_Block* MicroService::handle_OPTIONS(std::string& in)
{
    std::string http_header;
    http_header = "HTTP/1.1 200 OK\r\n";
    http_header += "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n";
    http_header += "Access-Control-Allow-Headers: DNT, User-Agent, X-Requested-With, If-Modified-Since, Cache-Control, Content-Type, Range\r\n";
    http_header += "Access-Control-Max-Age: 1728000\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";
    http_header += "Content-Type: text/plain; charset=utf-8\r\n";
    http_header += "Content-Length: 0\r\n";
    http_header += "\r\n\r\n";
    ACE_Message_Block* rsp = nullptr;

    ACE_NEW_RETURN(rsp, ACE_Message_Block(512), nullptr);

    std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    rsp->wr_ptr(http_header.length());

    //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"), http_header.length(), http_header.c_str()));
    return(rsp);
}

ACE_Message_Block* MicroService::build_responseCreated()
{
    std::string http_header;
    ACE_Message_Block* rsp = nullptr;

    http_header = "HTTP/1.1 201 Created\r\n";
    http_header += "Connection: keep-alive\r\n";
    http_header += "Access-Control-Allow-Origin: *\r\n";
    http_header += "Content-Length: 0\r\n";

    ACE_NEW_RETURN(rsp, ACE_Message_Block(256), nullptr);

    std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    rsp->wr_ptr(http_header.length());

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response %s \n"), http_header.length(), http_header.c_str()));
    return(rsp);
}

ACE_Message_Block* MicroService::build_responseOK(std::string httpBody, std::string contentType)
{
    std::string http_header;
    ACE_Message_Block* rsp = nullptr;

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

    ACE_NEW_RETURN(rsp, ACE_Message_Block(std::size_t(MemorySize::SIZE_1KB) + httpBody.length()), nullptr);

    std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    rsp->wr_ptr(http_header.length());

    if(httpBody.length()) {
        std::memcpy(rsp->wr_ptr(), httpBody.c_str(), httpBody.length());
        rsp->wr_ptr(httpBody.length());
    }

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response header\n%s"), (http_header.length() + httpBody.length()), http_header.c_str()));
    return(rsp);
}

ACE_Message_Block* MicroService::build_responseERROR(std::string httpBody, std::string error)
{
    std::string http_header;
    ACE_Message_Block* rsp = nullptr;
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

    ACE_NEW_RETURN(rsp, ACE_Message_Block(std::size_t(MemorySize::SIZE_1KB) + httpBody.length()), nullptr);

    std::memcpy(rsp->wr_ptr(), http_header.c_str(), http_header.length());
    rsp->wr_ptr(http_header.length());
    std::memcpy(rsp->wr_ptr(), httpBody.c_str(), httpBody.length());
    rsp->wr_ptr(httpBody.length());

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l respone length %d response header\n%s"), (http_header.length() + httpBody.length()), http_header.c_str()));
    return(rsp);
}

ACE_INT32 MicroService::handle_signal(int signum, siginfo_t *s, ucontext_t *u)
{

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Micro service gets signal %d\n"), signum));
    m_continue = false;

    return(0);
}

int MicroService::open(void *arg)
{
    /*! Number of threads are 5, which is 2nd argument. */
    activate();
    return(0);
}

int MicroService::close(u_long flag)
{
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
    while(m_continue) {
        ACE_Message_Block *mb = nullptr;
        if(-1 != getq(mb)) {
            switch (mb->msg_type())
            {
            case ACE_Message_Block::MB_DATA:
            {
                /*_ _ _ _ _  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
                 | 4-bytes handle   | 4-bytes db instance pointer   | request (payload) |
                 |_ _ _ _ _ _ _ _ _ |_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _|_ _ _ _ _ _ _ _ _ _|
                */
                ACE_HANDLE handle = *((ACE_HANDLE *)mb->rd_ptr());
                mb->rd_ptr(sizeof(ACE_HANDLE));

                std::uintptr_t inst = *((std::uintptr_t *)mb->rd_ptr());
                Mongodbc* dbInst = reinterpret_cast<Mongodbc*>(inst);
                mb->rd_ptr(sizeof(uintptr_t));

                /* Parent instance */
                std::uintptr_t parent_inst = *((std::uintptr_t *)mb->rd_ptr());
                WebServer* parent = reinterpret_cast<WebServer*>(parent_inst);
                mb->rd_ptr(sizeof(uintptr_t));

                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l URI %s dbName %s\n"), dbInst->get_uri().c_str(), dbInst->get_dbName().c_str()));
                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l handle %d length %d \n"), handle, mb->length()));

                ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l httpReq length %d\n"), mb->length()));
                /*! Process The Request */
                process_request(handle, *mb, *dbInst);
                mb->release();
                break;
            }
            case ACE_Message_Block::MB_PCSIG:
                {
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Got MB_PCSIG \n")));
                    m_continue = false;
                    if(mb != NULL) {
                        mb->release();
                    }
                    msg_queue()->deactivate();
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

MicroService::MicroService(ACE_Thread_Manager* thr_mgr) :
    ACE_Task<ACE_MT_SYNCH>(thr_mgr)
{
    m_continue = true;
    m_threadId = thr_mgr->thr_self();
}

MicroService::~MicroService()
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Microservice dtor is invoked\n")));
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

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l WebServer::handle_timedout\n")));
    std::uintptr_t _handle = reinterpret_cast<std::uintptr_t>(act);
    auto conIt = m_connectionPool.find(_handle);

    if(conIt != std::end(m_connectionPool)) {
        WebConnection* connEnt = conIt->second;
        m_connectionPool.erase(conIt);
        /* let the reactor call handle_close on this handle */
        ACE_Reactor::instance()->remove_handler(_handle, ACE_Event_Handler::READ_MASK | ACE_Event_Handler::TIMER_MASK | ACE_Event_Handler::SIGNAL_MASK);
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
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l New connection is created and handle is %d\n"), peerStream.get_handle()));
            /*! Start Handle Cleanup Timer to get rid of this handle from connectionPool*/
            long tId = start_conn_cleanup_timer(peerStream.get_handle());
            connEnt->timerId(tId);
            connEnt->handle(peerStream.get_handle());
            connEnt->connAddr(peerAddr);
            ACE_Reactor::instance()->register_handler(connEnt, 
                                                      ACE_Event_Handler::READ_MASK|ACE_Event_Handler::TIMER_MASK | ACE_Event_Handler::SIGNAL_MASK);
        }
    } else {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Accept to new connection failed\n")));
    }

    return(0);
}

ACE_INT32 WebServer::handle_signal(int signum, siginfo_t* s, ucontext_t* ctx)
{
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Signal Number %d and its name %S is received for WebServer\n"), signum, signum));

    if(!workerPool().empty()) {
        std::for_each(workerPool().begin(), workerPool().end(), [&](MicroService* ms) {
            ACE_Message_Block* req = nullptr;
            ACE_NEW_RETURN(req, ACE_Message_Block((size_t)MemorySize::SIZE_1KB), -1);
            req->msg_type(ACE_Message_Block::MB_PCSIG);
            ms->putq(req);
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l Sending to Worker Node\n")));
        });
    }

    if(!connectionPool().empty()) {
      for(auto it = connectionPool().begin(); it != connectionPool().end(); ++it) {
        auto wc = it->second;
        stop_conn_cleanup_timer(wc->timerId());
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l its name %S is received for WebServer\n"), signum));
        delete wc;
      }
      connectionPool().clear();
    }
    return(0);
}

ACE_INT32 WebServer::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask mask)
{
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

    int reuse_addr = 1;
    if(m_server.open(m_listen, reuse_addr)) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l Starting of WebServer failed - opening of port %d hostname %s\n"), m_listen.get_port_number(), m_listen.get_host_name()));
    }

    m_workerPool.clear();
    std::uint32_t cnt;

    for(cnt = 0; cnt < workerPool; ++cnt) {
        MicroService* worker = nullptr;
        ACE_NEW_NORETURN(worker, MicroService(ACE_Thread_Manager::instance()));
        m_workerPool.push_back(worker);
        worker->open();
    }

    m_currentWorker = std::begin(m_workerPool);

    /* Mongo DB interface */
    std::string uri("mongodb://127.0.0.1:27017");
    std::string _dbName("bayt");
    std::uint32_t _pool = 50;

    if(dbUri.length()) {
        uri.assign(dbUri);
    }

    if(dbConnPool.length()) {
        _pool = std::stoi(dbConnPool);
    }

    if(dbName.length()) {
        _dbName.assign(dbName);
    }

    mMongodbc = new Mongodbc(uri, _dbName);
}

WebServer::~WebServer()
{
    if(nullptr != mMongodbc) {
        delete mMongodbc;
        mMongodbc = nullptr;
    }

    if(!workerPool().empty()) {
        for(auto it = workerPool().begin(); it != workerPool().end(); ++it) {
            auto ent = *it;
            delete ent;
        }
    }
}

bool WebServer::start()
{
    int ret_status = 0;
    ACE_Reactor::instance()->register_handler(this, ACE_Event_Handler::ACCEPT_MASK | ACE_Event_Handler::TIMER_MASK | ACE_Event_Handler::SIGNAL_MASK); 
    /* subscribe for signal */
    ACE_Sig_Set ss;
    ss.empty_set();
    ss.sig_add(SIGINT);
    ss.sig_add(SIGTERM);
    ACE_Reactor::instance()->register_handler(&ss, this); 

    ACE_Time_Value to(1,0);

    while(!m_stopMe) {
        ACE_Reactor::instance()->handle_events(to);
    }

    return(m_stopMe);
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
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l WebConnection dtor is invoked\n")));
}

ACE_INT32 WebConnection::handle_timeout(const ACE_Time_Value &tv, const void *act)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Webconnection::handle_timedout\n")));
    std::uintptr_t handle = reinterpret_cast<std::uintptr_t>(act);
    auto conIt = m_parent->connectionPool().find(handle);

    if(conIt != std::end(m_parent->connectionPool())) {
        auto connEnt = conIt->second;
        m_parent->connectionPool().erase(conIt);
    }
    return(0);
}

ACE_INT32 WebConnection::handle_input(ACE_HANDLE handle)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l handle_input on handle %d\n"), m_handle));
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
    req->msg_type(ACE_Message_Block::MB_DATA);

    /*_ _ _ _ _  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ 
     | 4-bytes handle   | 4-bytes db instance pointer   | 4 bytes Parent Instance |request (payload) |
     |_ _ _ _ _ _ _ _ _ |_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _|_ _ _ _ _ _ _ _ _ __ __ _|_ _ _ _ _ _ _ _ _ |
     */
    *((ACE_HANDLE *)req->wr_ptr()) = handle;
    req->wr_ptr(sizeof(ACE_HANDLE));

    /* db instance */
    std::uintptr_t inst = reinterpret_cast<std::uintptr_t>(parent()->mongodbcInst());
    *((std::uintptr_t* )req->wr_ptr()) = inst;
    req->wr_ptr(sizeof(uintptr_t));

    /* parent instance */
    std::uintptr_t parent_inst = reinterpret_cast<std::uintptr_t>(parent());
    *((std::uintptr_t* )req->wr_ptr()) = parent_inst;
    req->wr_ptr(sizeof(uintptr_t));

    std::int32_t len = m_req->length();
    std::memcpy(req->wr_ptr(), m_req->rd_ptr(), len);
    req->wr_ptr(len);

    /* Reclaim the memory now */
    m_req->release();
    m_expectedLength = -1;

    auto it = m_parent->currentWorker();
    MicroService* mEnt = *it;
    mEnt->putq(req);
    return(0);
}

ACE_INT32 WebConnection::handle_signal(int signum, siginfo_t *s, ucontext_t *u)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l signal number - %d (%S) is received\n"), signum));
    if(m_timerId > 0) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Running timer is stopped for signal (%S)\n"), signum));
        m_parent->stop_conn_cleanup_timer(m_timerId);
    }
    return(0);
}

ACE_INT32 WebConnection::handle_close (ACE_HANDLE handle, ACE_Reactor_Mask mask)
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l WebConnection::handle_close handle %d will be closed upon timer expiry\n"), handle));
    return(0);
}

ACE_HANDLE WebConnection::get_handle() const
{
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l WebConnection::get_handle - handle %d\n"), m_handle));
    return(m_handle);
}

bool WebConnection::isCompleteRequestReceived()
{
    if((m_req != NULL) && (m_req->length() == m_expectedLength)) {
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
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
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
                        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Expected Length %d received length %d\n"), expected_length, len));
                        /* +512 is to avoid buffer overflow */
                        ACE_NEW_NORETURN(m_req, ACE_Message_Block((size_t)(expected_length + 512)));
                        /* copy the read buffer into m_req data member */
                        std::memcpy(m_req->wr_ptr(), scratch_pad.data(), len);
                        m_req->wr_ptr(len);
                        m_expectedLength = expected_length;
                    }
                } else {
                    /* Content-Length: is not present in the Header */
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Expected Length %d received length %d\n"), len, len));
                    /* +512 is to avoid buffer overflow */
                    ACE_NEW_NORETURN(m_req, ACE_Message_Block((size_t)(len + 512)));
                    /* copy the read buffer into m_req data member */
                    std::memcpy(m_req->wr_ptr(), scratch_pad.data(), len);
                    m_req->wr_ptr(len);
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
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Reading in loop for handle %d\n"), m_handle));
        do {

            len = recv(handle(), (buf + offset), (remainingLength - offset), 0);
            if(len < 0) {
                ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
                return(true);
            }

            offset += len;
            m_req->wr_ptr(len);
        }while(offset != remainingLength);

        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l offset %d remainingLength %d\n"), offset, remainingLength));
        if(len < 0) {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [worker:%t] %M %N:%l Receive failed for handle %d\n"), m_handle));
            return(true);
        } else {
            /* do we have all contents of a request? */
            return(isCompleteRequestReceived());
        }
    }
}

#endif /* __webservice_cc__*/
