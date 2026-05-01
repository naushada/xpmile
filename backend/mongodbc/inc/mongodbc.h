#ifndef MONGODBC_H
#define MONGODBC_H

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <tuple>
#include <vector>

#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/stdx/string_view.hpp>
#include <bsoncxx/string/to_string.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/bulk_write.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/cursor.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>

#include "ace/Log_Msg.h"

/**
 * @brief MongoDB connection-pool client.
 *
 * Wraps a mongocxx::pool so that multiple worker threads can share a single
 * MongodbClient instance and acquire independent client handles without
 * synchronization overhead.  All public methods acquire a connection from the
 * pool, perform the operation, and release it automatically on return.
 *
 * @par Construction
 * @code
 *   MongodbClient db("mongodb://localhost:27017/mydb?maxPoolSize=10");
 *   std::string oid = db.create_document(db.get_database(), "orders", jsonDoc);
 * @endcode
 */
class MongodbClient {
public:
  /**
   * @brief Construct and initialise the connection pool from a MongoDB URI.
   * @param uri  Full MongoDB connection string including database name and
   *             optional pool-size parameters, e.g.
   *             @c "mongodb://host/db?maxPoolSize=20".
   */
  explicit MongodbClient(const std::string &uri);

  /// Default-construct an uninitialised client (pool is null).
  MongodbClient() = default;
  ~MongodbClient() = default;

  /// Return the database name extracted from the URI at construction time.
  const std::string &get_database() const { return m_dbName; }

  /// Override the active database name.
  void set_database(const std::string &dbName) { m_dbName = dbName; }

  /// Return the connection URI string.
  const std::string &get_uri() const { return m_uri; }

  /** @name Write operations */
  ///@{
  /**
   * @brief Insert a single document and return its inserted OID as a string.
   * @param dbName  Target database name.
   * @param coll    Target collection name.
   * @param doc     JSON document string to insert.
   * @return Inserted OID string, or empty on failure.
   */
  std::string create_document(const std::string &dbName,
                              const std::string &coll, const std::string &doc);

  /**
   * @brief Insert multiple documents via a bulk-write operation.
   * @param dbName  Target database name.
   * @param coll    Target collection name.
   * @param doc     JSON object whose values are the documents to insert.
   * @return Number of documents inserted.
   */
  std::int32_t create_bulk_document(const std::string &dbName,
                                    const std::string &coll,
                                    const std::string &doc);

  /**
   * @brief Update all documents matching @p filter in @p coll.
   * @param coll      Collection name.
   * @param filter    JSON match filter.
   * @param document  JSON update document (e.g. @c {"$set": {...}}).
   * @return @c true if at least one document was matched and updated.
   */
  bool update_collection(const std::string &coll, const std::string &filter,
                         const std::string &document);

  /**
   * @brief Bulk-update multiple documents using parallel filter/value arrays.
   * @param coll    Collection name.
   * @param filter  Per-document match filters.
   * @param value   Per-document update documents (parallel to @p filter).
   * @return Number of documents modified.
   */
  std::int32_t update_bulk_document(const std::string &coll,
                                    const std::vector<std::string> &filter,
                                    const std::vector<std::string> &value);

  /**
   * @brief Delete all documents matching @p doc filter from @p coll.
   * @param coll  Collection name.
   * @param doc   JSON match filter.
   * @return @c true if the bulk operation completed successfully.
   */
  bool delete_document(const std::string &coll, const std::string &doc);
  ///@}

  /** @name Read operations */
  ///@{
  /**
   * @brief Fetch the first document matching @p query.
   * @param coll        Collection name.
   * @param query       JSON match filter.
   * @param projection  JSON projection document.
   * @return First matching document as a JSON string, or empty if none found.
   */
  std::string get_document(const std::string &coll, const std::string &query,
                           const std::string &projection);

  /**
   * @brief Fetch all documents matching @p query as a JSON array string.
   * @param coll        Collection name.
   * @param query       JSON match filter.
   * @param projection  JSON projection document.
   * @return JSON array of matching documents, or empty if none found.
   */
  std::string get_documents(const std::string &coll, const std::string &query,
                            const std::string &projection);

  /**
   * @brief Fetch all documents in @p coll (no filter) as a JSON array string.
   * @param coll        Collection name.
   * @param projection  JSON projection document.
   * @return JSON array of all documents, or empty if the collection is empty.
   */
  std::string get_documents(const std::string &coll,
                            const std::string &projection);
  ///@}

  /** @name Vendor-specific helpers (Ajoul courier API) */
  ///@{
  /**
   * @brief Extract the @c access_token string from a JSON response object.
   * @param json_obj  Raw JSON string from the Ajoul auth endpoint.
   * @return Token string, or empty if the key is absent or has a non-string
   * type.
   */
  std::string get_access_token_for_ajoul(const std::string &json_obj);

  /**
   * @brief Extract the tracking number and reference from a JSON shipment
   * response.
   * @param json_obj      Raw JSON string from the Ajoul create-shipment
   * endpoint.
   * @param reference_no  Output: populated with the shipment reference string.
   * @return Tracking number string, or empty on failure.
   */
  std::string get_tracking_no_for_ajoul(const std::string &json_obj,
                                        std::string &reference_no);
  ///@}

  /** @name JSON extraction utilities */
  ///@{
  /**
   * @brief Populate a vector with the UTF-8 strings in a JSON array element.
   * @param json_obj  JSON document string.
   * @param key       Key whose value is a JSON array of strings.
   * @param vec_out   Output vector; cleared and filled on success.
   * @return @c true on success, @c false if the key is missing or not an array.
   */
  bool from_json_array_to_vector(const std::string &json_obj,
                                 const std::string &key,
                                 std::vector<std::string> &vec_out);

  /**
   * @brief Extract a single UTF-8 string element from a JSON document.
   * @param json_obj  JSON document string.
   * @param key       Key whose value is a UTF-8 string.
   * @param str_out   Output string; cleared on failure.
   * @return @c true on success, @c false if the key is missing or not a string.
   */
  bool from_json_element_to_string(const std::string &json_obj,
                                   const std::string &key,
                                   std::string &str_out);

  /**
   * @brief Extract an array of @c {file-name, file-content} tuples from JSON.
   * @param json_obj  JSON document string.
   * @param key       Key whose value is an array of document objects.
   * @param out       Output vector of @c (file-name, file-content) pairs.
   * @return @c true on success, @c false if the key is missing or not an array.
   */
  bool from_json_object_to_map(
      const std::string &json_obj, const std::string &key,
      std::vector<std::tuple<std::string, std::string>> &out);
  ///@}

private:
  std::string m_uri;
  std::string m_dbName;
  std::unique_ptr<mongocxx::pool> m_pool;
  std::unique_ptr<mongocxx::instance> m_instance;
};

#endif // MONGODBC_H
