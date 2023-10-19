#ifndef __mongodbc_h__
#define __mongodbc_h__

#include <cstdint>
#include <iostream>
#include <vector>
#include <mutex>

#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/cursor.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/bulk_write.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/stdx/string_view.hpp>
#include <bsoncxx/string/to_string.hpp>

#include "ace/Log_Msg.h"

class Mongodbc {
    public:
        Mongodbc(std::string uri, std::string db_name);
        Mongodbc();
        ~Mongodbc();

        std::string get_dbName() const {
            return(mdbName);
        }

        void set_dbName(std::string dbName) {
            mdbName = dbName;
        }

        std::string get_uri() const {
            return(mURI);
        }

        void set_uri(std::string uri) {
            mURI = uri;
        }

        mongocxx::database& get_mongoDbInst() {
            return(mMongoDbInst);
        }

        bool update_collection(std::string coll, std::string filter, std::string document);
        bool delete_document(std::string coll, std::string shippingRecord);
        std::string create_document(std::string coll, std::string accountRecord);
        std::string get_document(std::string collectionName, std::string query, std::string fieldProjection);
        std::string get_documents(std::string collectionName, std::string query, std::string fieldProjection);
        std::string get_byOID(std::string collection, std::string projection, std::string oid);
        std::string get_documentList(std::string collectionName, std::string query, std::string fieldProjection);
        std::int32_t create_bulk_document(std::string coll, std::string doc);

    private:
        std::string mURI;
        std::string mdbName;
        mongocxx::database mMongoDbInst;
        /* Pool of db connections */
        mongocxx::pool* mMongoConnPool;
        mongocxx::instance* mInstance;
};


#endif /*__mongodbc_h__*/
