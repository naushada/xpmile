#ifndef MONGODBC_H
#define MONGODBC_H

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <tuple>
#include <variant>
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

/// Flat vector of UTF-8 strings extracted from a JSON array element.
using JsonStrVec = std::vector<std::string>;

/// Vector of (file-name, file-content) pairs extracted from a JSON document array.
using JsonDocList = std::vector<std::tuple<std::string, std::string>>;

/**
 * @brief Result type for @c MongodbClient::from_json().
 *
 * Alternatives:
 *  - @c std::monostate — key not found or element type not supported.
 *  - @c std::string    — the element is a UTF-8 string (@c k_utf8).
 *  - @c JsonStrVec     — the element is an array of UTF-8 strings.
 *  - @c JsonDocList    — the element is an array of sub-documents
 *                        each containing @c file-name and @c file-content fields.
 */
using JsonExtract = std::variant<std::monostate, std::string, JsonStrVec, JsonDocList>;

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

  /** @name JSON extraction utility */
  ///@{
  /**
   * @brief Extract a typed value from a JSON document by key.
   *
   * Inspects the BSON type of the element and returns the matching variant
   * alternative:
   *  - @c k_utf8 → @c std::string
   *  - @c k_array of strings → @c JsonStrVec
   *  - @c k_array of sub-documents (with @c file-name / @c file-content fields)
   *    → @c JsonDocList
   *  - key absent or type not matched → @c std::monostate
   *
   * @param json_obj  JSON document string.
   * @param key       Key to look up.
   * @return A @c JsonExtract variant holding the extracted value, or
   *         @c std::monostate on failure.
   */
  JsonExtract from_json(const std::string &json_obj,
                        const std::string &key) const;
  ///@}

private:
  std::string m_uri;
  std::string m_dbName;
  std::unique_ptr<mongocxx::pool> m_pool;
  std::unique_ptr<mongocxx::instance> m_instance;
};

#endif // MONGODBC_H
