#ifndef __mongodbc_cc__
#define __mongodbc_cc__

#include <vector>
#include <sstream>

#include "mongodbc.h"

MongodbClient::MongodbClient()
{
    mInstance = nullptr;
    mURI.clear();
    mdbName.clear();
    mMongoConnPool = nullptr;
}

MongodbClient::MongodbClient(std::string uri_str)
{
    mInstance = nullptr;
    mMongoConnPool = nullptr;
    mURI = uri_str;

    do {

        mInstance = std::make_unique<mongocxx::v_noabi::instance>();
        if(nullptr == mInstance) {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l instantiation of mongocxx::instance is failed\n")));
            break;
        }

        /* pool of connections*/
        //std::string poolUri(uri_str);

        /* reference: http://mongocxx.org/mongocxx-v3/connection-pools/ */
        //poolUri += "/?minPoolSize=10&maxPoolSize=" + std::to_string(poolSize);
        //poolUri += "&minPoolSize=10&maxPoolSize=" + std::to_string(poolSize);
        //poolUri += "&maxPoolSize=" + std::to_string();

        mongocxx::uri uri(uri_str.c_str());
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l The databse from URI is %s maxPoolSize %d\n"), uri.database().c_str()));

        mMongoConnPool = std::make_unique<mongocxx::pool>(uri);
        if(nullptr == mMongoConnPool) {
            ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Master:%t] %M %N:%l instantiation of MongoConnPool is failed\n")));
            break;
        }

        set_database(uri.database());

    }while(0);
}

MongodbClient::~MongodbClient()
{

    mInstance.reset(nullptr);
    mMongoConnPool.reset(nullptr);

#if 0
    if(nullptr != mInstance) {
        delete mInstance;
    }

    if(nullptr != mMongoConnPool) {
        delete mMongoConnPool;
    }
#endif
}

bool MongodbClient::update_collection(std::string collectionName, std::string match, std::string document)
{
    bsoncxx::document::value toUpdate = bsoncxx::from_json(document.c_str());
    bsoncxx::document::value filter = bsoncxx::from_json(match.c_str());

    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(false);
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());
    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);
    auto bulk = collection.create_bulk_write(bulk_opt);

    mongocxx::model::update_many upd(filter.view(), toUpdate.view());
    bulk.append(upd);

    auto result = bulk.execute();
    std::int32_t cnt = 0;
    if(result) {
        cnt = result->matched_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document updated is %d\n"), cnt));
        if(!cnt) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Shipment Update is failed\n")));
            return(false);
        }
        return(true);
    }
    
    return(false);
}

bool MongodbClient::delete_document(std::string collectionName, std::string doc)
{
    bsoncxx::document::value filter = bsoncxx::from_json(doc.c_str());
    auto conn = mMongoConnPool->acquire();

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(false);
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database %s\n"), collectionName.c_str()));
        return(false);
    }
    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);
    auto bulk = collection.create_bulk_write(bulk_opt);

    mongocxx::model::delete_many del(filter.view());
    bulk.append(del);

    auto result = bulk.execute();
    std::int32_t cnt = 0;

    if(result) {
        cnt = result->deleted_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document deleted is %d\n"), cnt));
    }

    if(result) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The shipment from collection is deleted \n")));
        return(true);
    } else {
        return(false);
    }
}

std::string MongodbClient::get_document(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());

    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());

    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::find opts{};
    using namespace std::literals::chrono_literals;
    std::chrono::milliseconds ms(5000);

    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());
    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);

    auto cursor = collection.find(filter.view(), opts);
    //mongocxx::v_noabi::cursor cursor = collection.find(filter.view(), opts);

    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    return(std::string(bsoncxx::to_json(*iter).c_str()));
}

std::string MongodbClient::get_documents(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());

    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());

    using namespace std::literals::chrono_literals;
    mongocxx::options::find opts{};
    std::chrono::milliseconds ms(5000);

    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);

    mongocxx::cursor cursor = collection.find(filter.view(), opts);
    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    std::stringstream result("");
    result << "[";

    for(; iter != cursor.end(); ++iter) {
        result << bsoncxx::to_json(*iter).c_str()
               << ",";
    }

    result.seekp(-1, std::ios_base::end);
    result << "]";

    return(std::string(result.str()));
}

std::string MongodbClient::get_documents(std::string collectionName, std::string projection)
{
    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());
    bsoncxx::document::view_or_value filter;

    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(projection.c_str());

    using namespace std::literals::chrono_literals;
    mongocxx::options::find opts{};
    std::chrono::milliseconds ms(5000);

    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);
    mongocxx::cursor cursor = collection.find({}, opts);
    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    std::stringstream result("");
    result << "[";

    for(; iter != cursor.end(); ++iter) {
        result << bsoncxx::to_json(*iter).c_str()
               << ",";
    }

    result.seekp(-1, std::ios_base::end);
    result << "]";

    return(std::string(result.str()));
}

/**
 * @brief this member function insert singlr document in a collection and returns the inserted OID
 * 
 * @param dbName 
 * @param collectionName 
 * @param doc 
 * @return std::string 
 */
std::string MongodbClient::create_document(std::string dbName, std::string collectionName, std::string doc)
{
    bsoncxx::document::value document = bsoncxx::from_json(doc.c_str());
    
    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection:%s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn->database(dbName.c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse:%s\n"), dbName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());
    bsoncxx::stdx::optional<mongocxx::result::insert_one> result = collection.insert_one(document.view());

    if(result) {
        bsoncxx::oid oid = result->inserted_id().get_oid().value;
        std::string JobID = oid.to_string();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The inserted Oid:%s\n"), JobID.c_str()));
        return(JobID);
    }

    return(std::string());
}

std::string MongodbClient::get_byOID(std::string coll, std::string projection, std::string oid)
{
    auto conn = mMongoConnPool->acquire();
    mongocxx::database dbInst = conn->database(get_database().c_str());
    auto collection = dbInst.collection(coll.c_str());

    std::string query("{\"_id\" : {\"$oid\": \"");
    query += oid + "\"}}";

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The query is %s\n"), query.c_str()));
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());
    mongocxx::options::find opts{};
    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(projection.c_str());
    auto resultFormat = opts.projection(outputProjection);
    mongocxx::v_noabi::cursor cursor = collection.find(filter.view(), {});
    mongocxx::cursor::iterator iter = cursor.begin();
    bsoncxx::document::view res = *cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The Response is %s \n"), bsoncxx::to_json(res).c_str()));

    std::stringstream rsp("");
    rsp << bsoncxx::to_json(*iter);
    return(rsp.str());

}

std::string MongodbClient::get_documentList(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());
    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn->database(get_database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    using namespace std::literals::chrono_literals;
    std::chrono::milliseconds ms = 5s;
    mongocxx::options::find opts{};
    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());
    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);
    mongocxx::v_noabi::cursor cursor = std::move(collection.find(filter.view(), opts));
    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    std::stringstream result("");
    result << "[";

    for (auto&& doc : cursor) {
        result << bsoncxx::to_json(doc).c_str()
               << ",";
    }
    result.seekp(-1, std::ios_base::end);
    result << "]";
    return(std::string(result.str()));
}

/**
 * @brief This member function insert the multiple documents in a collection for a given uri.
 * 
 * @param collectionName 
 * @param doc 
 * @return std::int32_t 
 */
std::int32_t MongodbClient::create_bulk_document(std::string dbName, std::string collectionName, std::string doc)
{
    std::int32_t cnt = 0;
    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);

    bsoncxx::document::value new_shipment = bsoncxx::from_json(doc.c_str());
    auto conn = mMongoConnPool->acquire();
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection:%s\n"), collectionName.c_str()));
        return(cnt);
    }

    mongocxx::database dbInst = conn->database(dbName.c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database:%s\n"), dbName.c_str()));
        return(cnt);
    }

    auto collection = dbInst.collection(collectionName.c_str());

    auto bulk = collection.create_bulk_write(bulk_opt);

    bsoncxx::document::view dock_view = new_shipment.view();
    auto iter = dock_view.begin();
    
    for(; iter != dock_view.end(); ++iter) {
        bsoncxx::document::element elm = *iter;
        mongocxx::model::insert_one insert_op(elm.get_document().value);
        bulk.append(insert_op);
    }

    auto result = bulk.execute();

    if(result) {
        cnt = result->inserted_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document created cnt:%d\n"), cnt));
    }

    return(cnt);
}

/** 
 * 
*/
std::string MongodbClient::get_documentEx(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());
    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::find opts{};
    using namespace std::literals::chrono_literals;
    std::chrono::milliseconds ms = 5s;
    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());
    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);
    auto cursor = collection.find(filter.view(), opts);
    //mongocxx::v_noabi::cursor cursor = collection.find(filter.view(), opts);

    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    return(std::string(bsoncxx::to_json(*iter).c_str()));
}

std::string MongodbClient::get_documentsEx(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());

    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());

    using namespace std::literals::chrono_literals;
    mongocxx::options::find opts{};
    std::chrono::milliseconds ms = 5s;

    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);
    mongocxx::v_noabi::cursor cursor = collection.find(filter.view(), opts);
    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    std::stringstream result("");
    result << "[";

    for(; iter != cursor.end(); ++iter) {
        result << bsoncxx::to_json(*iter).c_str()
               << ",";
    }

    result.seekp(-1, std::ios_base::end);
    result << "]";

    return(std::string(result.str()));
}

bool MongodbClient::update_collectionEx(std::string collectionName, std::string match, std::string document)
{
    bsoncxx::document::value toUpdate = bsoncxx::from_json(document.c_str());
    bsoncxx::document::value filter = bsoncxx::from_json(match.c_str());

    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(false);
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);
    auto bulk = collection.create_bulk_write(bulk_opt);

    mongocxx::model::update_many upd(filter.view(), toUpdate.view());
    bulk.append(upd);

    auto result = bulk.execute();
    std::int32_t cnt = 0;
    if(result) {
        cnt = result->matched_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document updated is %d\n"), cnt));
        if(!cnt) {
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l Shipment Update is failed\n")));
            return(false);
        }
        return(true);
    }

    return(false);
}

bool MongodbClient::delete_documentEx(std::string collectionName, std::string doc)
{
    bsoncxx::document::value filter = bsoncxx::from_json(doc.c_str());
    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(false);
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database %s\n"), collectionName.c_str()));
        return(false);
    }
    auto collection = dbInst.collection(collectionName.c_str());

    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);
    auto bulk = collection.create_bulk_write(bulk_opt);

    mongocxx::model::delete_many del(filter.view());
    bulk.append(del);

    auto result = bulk.execute();
    std::int32_t cnt = 0;

    if(result) {
        cnt = result->deleted_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document deleted is %d\n"), cnt));
    }

    if(result) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The shipment from collection is deleted \n")));
        return(true);
    } else {
        return(false);
    }
}

std::string MongodbClient::create_documentEx(std::string collectionName, std::string doc)
{
    bsoncxx::document::value document = bsoncxx::from_json(doc.c_str());
    
    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);
    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for databse %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());
    bsoncxx::stdx::optional<mongocxx::result::insert_one> result = collection.insert_one(document.view());

    if(result) {
        bsoncxx::oid oid = result->inserted_id().get_oid().value;
        std::string JobID = oid.to_string();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Worker:%t] %M %N:%l The inserted Oid is %s\n"), JobID.c_str()));
        return(JobID);
    }

    return(std::string());
}

std::string MongodbClient::get_documentListEx(std::string collectionName, std::string query, std::string fieldProjection)
{
    std::string json_object;
    bsoncxx::document::value filter = bsoncxx::from_json(query.c_str());
    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(std::string());
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database %s\n"), collectionName.c_str()));
        return(std::string());
    }

    auto collection = dbInst.collection(collectionName.c_str());

    using namespace std::literals::chrono_literals;
    std::chrono::milliseconds ms = 5s;
    mongocxx::options::find opts{};
    bsoncxx::document::view_or_value outputProjection = bsoncxx::from_json(fieldProjection.c_str());
    opts.max_time(ms).no_cursor_timeout(false).projection(outputProjection);
    mongocxx::v_noabi::cursor cursor = collection.find(filter.view(), opts);
    mongocxx::cursor::iterator iter = cursor.begin();

    if(iter == cursor.end()) {
        return(std::string());
    }

    std::stringstream result("");
    result << "[";

    for (auto&& doc : cursor) {
        result << bsoncxx::to_json(doc).c_str()
               << ",";
    }
    result.seekp(-1, std::ios_base::end);
    result << "]";
    return(std::string(result.str()));
}

std::int32_t MongodbClient::create_bulk_documentEx(std::string collectionName, std::string doc)
{
    std::int32_t cnt = 0;
    mongocxx::options::bulk_write bulk_opt;
    mongocxx::write_concern wc;
    bulk_opt.ordered(false);
    wc.acknowledge_level(mongocxx::write_concern::level::k_default);
    bulk_opt.write_concern(wc);

    bsoncxx::document::value new_shipment = bsoncxx::from_json(doc.c_str());
    mongocxx::uri uri(get_uri());
    mongocxx::client conn(uri);

    if(!conn) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for collection %s\n"), collectionName.c_str()));
        return(cnt);
    }

    mongocxx::database dbInst = conn.database(uri.database().c_str());
    if(!dbInst) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l acquiring DB client failed for database %s\n"), collectionName.c_str()));
        return(cnt);
    }

    auto collection = dbInst.collection(collectionName.c_str());

    auto bulk = collection.create_bulk_write(bulk_opt);

    bsoncxx::document::view dock_view = new_shipment.view();
    auto iter = dock_view.begin();
    
    for(; iter != dock_view.end(); ++iter) {
        bsoncxx::document::element elm = *iter;
        mongocxx::model::insert_one insert_op(elm.get_document().value);
        bulk.append(insert_op);
    }

    auto result = bulk.execute();

    if(result) {
        cnt = result->inserted_count();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l bulk document created is %d\n"), cnt));
    }

    return(cnt);
}
/*
std::int32_t MongodbClient::update_bulk_shipment(std::string bulkShipment)
{

}
*/


std::string MongodbClient::get_access_token_for_ajoul(std::string json_obj)
{
  //const bsoncxx::document::element
  bsoncxx::document::view doc = bsoncxx::from_json(json_obj.c_str()).view();
  
  //bsoncxx::document::value doc(json_obj.c_str(), json_obj.length());
  
  auto it = doc.find("access_token");
  if(it == doc.end()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l Element access_token is not found\n")));
    return(std::string());
  }

  //bsoncxx::document::element elm_value = doc["access_token"];
  bsoncxx::document::element elm_value = *it;
  //bsoncxx::document::view& doc
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the type is %d\n"), elm_value.type()));
  if(elm_value && elm_value.type() == bsoncxx::type::k_utf8) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is %d\n"), elm_value.get_utf8().value.length()));
    std::string res(elm_value.get_utf8().value.data(), elm_value.get_utf8().value.length());
    return(res);
  }
  //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The access token is %s\n"), elm_value.get_utf8().value.data()));
  return(std::string());
}

std::string MongodbClient::get_tracking_no_for_ajoul(std::string json_obj, std::string& reference_no)
{
  bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
  bsoncxx::document::view doc = doc_val.view();

  auto it = doc.find("Shipment");
  if(it == doc.end()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l The Shipment object not found \n")));
    return(std::string());    
  }
  
  auto tt = it->get_document().value;
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is  %d value %s\n"), tt.length(), tt.data()));  
  reference_no = tt["reference"].get_utf8().value.to_string();
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the reference is %s\n"), reference_no.c_str()));  

  bsoncxx::document::element elm_value = doc["TrackingNumber"];

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the type is %d\n"), elm_value.type()));
  if(elm_value && elm_value.type() == bsoncxx::type::k_utf8) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is %d\n"), elm_value.get_utf8().value.length()));
    std::string res(elm_value.get_utf8().value.data(), elm_value.get_utf8().value.length());
    return(res);
  }
  return(std::string());
}

std::uint32_t MongodbClient::from_json_array_to_vector(const std::string json_obj, const std::string key, std::vector<std::string>& vec_out)
{
  bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
  bsoncxx::document::view doc = doc_val.view();

  auto it = doc.find(key);
  if(it == doc.end()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l the element:%s not found in the json document\n"), key.c_str()));
    return(1);    
  }

  bsoncxx::document::element elm_value = *it;
  if(elm_value && bsoncxx::type::k_array == elm_value.type()) {
     bsoncxx::array::view to(elm_value.get_array().value);
     for(bsoncxx::array::element elm : to) {
        if(bsoncxx::type::k_utf8 == elm.type()) {
            std::string tmp(elm.get_utf8().value.data(), elm.get_utf8().value.length());
            vec_out.push_back(tmp);
        }
     }
  } else {
    vec_out.clear();
  }
  
  return(0);

#if 0
  auto tt = it->get_document().value;
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is  %d value %s\n"), tt.length(), tt.data()));  
  reference_no = tt["reference"].get_utf8().value.to_string();
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the reference is %s\n"), reference_no.c_str()));  

  bsoncxx::document::element elm_value = doc["TrackingNumber"];

  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the type is %d\n"), elm_value.type()));
  if(elm_value && elm_value.type() == bsoncxx::type::k_utf8) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is %d\n"), elm_value.get_utf8().value.length()));
    std::string res(elm_value.get_utf8().value.data(), elm_value.get_utf8().value.length());
    return(res);
  }
  return(std::string());
  #endif
}

std::uint32_t MongodbClient::from_json_element_to_string(const std::string json_obj, const std::string key, std::string& str_out)
{

  bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
  bsoncxx::document::view doc = doc_val.view();

  auto it = doc.find(key);
  if(it == doc.end()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l The element:%s not found in json document\n"), key.c_str()));
    str_out.clear();
    return(1);    
  }

  bsoncxx::document::element elm_value = *it;
  if(elm_value && bsoncxx::type::k_utf8 == elm_value.type()) {
      std::string elm(elm_value.get_utf8().value.data(), elm_value.get_utf8().value.length());
      str_out = elm;
  } else {
    str_out.clear();
  }
  
  return(0);
}

std::uint32_t MongodbClient::from_json_object_to_map(const std::string json_obj, const std::string key, std::vector<std::tuple<std::string, std::string>>& out)
{
    bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
    bsoncxx::document::view doc = doc_val.view();

    auto it = doc.find(key);
    if(it == doc.end()) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l the element:%s not found in the json document\n"), key.c_str()));
        return(1);    
    }

    bsoncxx::document::element elm_value = *it;
    if(elm_value && bsoncxx::type::k_array == elm_value.type()) {
       bsoncxx::array::view to(elm_value.get_array().value);
       for(bsoncxx::array::element elm : to) {
          if(bsoncxx::type::k_document == elm.type()) {
              /* get the document now */
              auto doc = elm.get_document().value;
              ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l file-name:%s file-content:%s\n"), doc["file-name"].get_utf8().value.data(), 
                         doc["file-content"].get_utf8().value.data()));
              #if 0
              for(auto it = doc.begin(); it != doc.end(); ++it) {
                  out.push_back(std::make_tuple(doc["file-name"].get_utf8().value.data(), doc["file-content"].get_utf8().value.data()));
              }
              #endif
              out.push_back(std::make_tuple(doc["file-name"].get_utf8().value.data(), doc["file-content"].get_utf8().value.data()));
          }
          #if 0
          if(bsoncxx::type::k_utf8 == elm.type()) {
              std::string tmp(elm.get_utf8().value.data(), elm.get_utf8().value.length());
              vec_out.push_back(tmp);
          }
          #endif
       }
    } else {
        out.clear();
    }
  
    return(0);
}

#if 0
std::string MongodbClient::get_tracking_no_for_ajoul(std::string json_obj, std::string& reference_no)
{
  //const bsoncxx::document::element
  bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
  //auto doc = bsoncxx::from_json(json_obj.c_str()).view();
  bsoncxx::document::view doc = doc_val.view();

  if(doc.empty()) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The doc is empty\n")));
  }


  for(auto it = doc.begin(); it != doc.end(); ++it) {
    if(it->type() == bsoncxx::type::k_document) {
        for(auto subIt = it->get_document().value.begin(); subIt != it->get_document().value.end(); ++subIt) {
            switch(subIt->type()) {
                case bsoncxx::type::k_null:
                    break;
                case bsoncxx::type::k_utf8:
                    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The element is %s Key is %s\n"), subIt->get_utf8().value.data(), subIt->key().data()));
                    break;
                case bsoncxx::type::k_array:
                case bsoncxx::type::k_binary:
                case bsoncxx::type::k_bool:
                case bsoncxx::type::k_date:
                case bsoncxx::type::k_document:
                case bsoncxx::type::k_double:
                case bsoncxx::type::k_int32:
                    break;
            }
        }
    } else if(it->type() == bsoncxx::type::k_utf8) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The element is %s the key is %s\n"), it->get_utf8().value.data(), it->key().data()));
    }
  }


#if 0
  for(auto it = doc.begin(); it != doc.end(); ++it) {
    if(it->type() == bsoncxx::type::k_document) {
        auto elm = it->get_document().value;
        auto elmIt = elm.find("Shipment");
        if(elmIt != elm.end()) {
            auto ent = *elmIt;

            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The value of reference is %s\n"), ent["reference"]));
            break;
        }
        elmIt = elm.find("TrackingNumber");
        if(elmIt != elm.end()) {
            auto ent = *elmIt;
            ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The Tracking Number is %s\n"), ent["TrackingNumber"]));
        }
    } else if(it->type() == bsoncxx::type::k_utf8) {
        auto elmIt = it->get_utf8().value.to_string();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The value k_utf8 is %s\n"), elmIt));
    }
  }
#endif

  //bsoncxx::document::value doc(json_obj.c_str(), json_obj.length());
  auto it = doc.find("Shipment");
  if(it == doc.end()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("%D [Worker:%t] %M %N:%l The Shipment object not found \n")));
    return(std::string());    
  }

  //bsoncxx::document::element sub_doc = doc["Shipment"];

  
  auto tt = it->get_document().value;
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is  %d value %s\n"), tt.length(), tt.data()));  
  reference_no = tt["reference"].get_utf8().value.to_string();
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the reference is %s\n"), reference_no.c_str()));  

  bsoncxx::document::element elm_value = doc["TrackingNumber"];
  //bsoncxx::document::view& doc
  ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the type is %d\n"), elm_value.type()));
  if(elm_value && elm_value.type() == bsoncxx::type::k_utf8) {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l the length is %d\n"), elm_value.get_utf8().value.length()));
    std::string res(elm_value.get_utf8().value.data(), elm_value.get_utf8().value.length());
    return(res);
  }
  //ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The access token is %s\n"), elm_value.get_utf8().value.data()));
  return(std::string());
}


bool MongodbClient::is_key_found_in_document(bsoncxx::document::view doc, std::string key)
{
  std::find_if(doc.begin(), doc.end(), [&](auto elm) { return(elm.key().data() == key);});
}

std::string MongodbClient::get_key_value(bsoncxx::document::view doc, std::string key)
{

  for(auto it = doc.begin(); it != doc.end(); ++it) {
    if(it->type() == bsoncxx::type::k_document) {
      auto elm = it->get_document().value;
      repeat:
      if(is_key_found_in_document(elm, key)) {
        for(auto foundIt = elm.begin(); foundIt != elm.end(); ++foundIt) {
          switch(foundIt->type()) {
            case bsoncxx::type::k_null:
              break;
            case bsoncxx::type::k_utf8: {
              ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l The element is %s Key is %s\n"), foundIt->get_utf8().value.data(), foundIt->key().data()));
              if(foundIt->key().data() == key) {
                std::string rsp(foundIt->get_utf8().value.data(), foundIt->get_utf8().value.length());
                return(rsp);
              }
            }

            case bsoncxx::type::k_array:
            case bsoncxx::type::k_binary:
            case bsoncxx::type::k_bool:
            case bsoncxx::type::k_date:
              break;
            case bsoncxx::type::k_document: {
              //return(get_key_value((*foundIt).get_document(), key));
              elm = foundIt->get_document().value;
              goto repeat;
              //break;
            }
            case bsoncxx::type::k_double:
            case bsoncxx::type::k_int32:
              break;
          }
        }
      }
    }
  }
}

std::string MongodbClient::get_value(std::string json_obj, std::string key)
{
  bsoncxx::document::value doc_val = bsoncxx::from_json(json_obj.c_str());
  //auto doc = bsoncxx::from_json(json_obj.c_str()).view();
  bsoncxx::document::view doc = doc_val.view();
  return(get_key_value(doc, key));
}
#endif

#endif /* __mongodbc_cc__*/
