package com.xpmile.onprem.service;

import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoCollection;
import com.mongodb.client.model.Filters;
import com.mongodb.client.model.Sorts;
import org.bson.Document;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

/**
 * Manages {@code idp.idp_clients} — the registered RPs the IdP issues
 * tokens to. The cloud-side {@code IdpClientRegistry} hot-reloads this
 * collection every ~60 s ({@code WebServer::init_idp/reload_idp}).
 *
 * One document per client:
 * <pre>{@code
 * {
 *   _id:                      "xpmile-spa",                       // clientId
 *   clientName:               "xpmile SPA",
 *   clientSecretHash:         "" | "<bcrypt-like hash>",          // optional in v1
 *   redirectUris:             ["https://marvel.app/cb", ...],     // EXACT-match
 *   postLogoutRedirectUris:   ["https://marvel.app/login", ...],  // EXACT-match
 *   scopes:                   ["openid","email","profile"],
 *   grantTypes:               ["authorization_code"]
 * }
 * }</pre>
 *
 * <p>{@code _id} IS the clientId — the cloud-side registry's
 * {@code find(client_id)} does an exact-byte match. Renaming a client
 * requires create-new + delete-old.
 *
 * <p>Lives in the {@code idp} database — design's dual-database split.
 */
@Service
public class IdpClientService {

    private static final String IDP_DATABASE = "idp";
    private static final String COLLECTION   = "idp_clients";

    private final MongoCollection<Document> collection;

    public IdpClientService(MongoClient mongoClient) {
        this.collection = mongoClient.getDatabase(IDP_DATABASE).getCollection(COLLECTION);
    }

    /** List every client, sorted by clientId. */
    public List<Document> list() {
        List<Document> out = new ArrayList<>();
        for (Document doc : collection.find().sort(Sorts.ascending("_id"))) {
            out.add(doc);
        }
        return out;
    }

    /** Insert or replace a client by clientId. The caller is responsible
     *  for setting the array fields explicitly — null arrays are written
     *  as null (the cloud-side registry treats null as empty). */
    public void upsert(Document client) {
        Object id = client.get("_id");
        if (id == null) {
            throw new IllegalArgumentException("client document must have _id (clientId)");
        }
        Document existing = collection.find(Filters.eq("_id", id)).first();
        if (existing != null) {
            collection.replaceOne(Filters.eq("_id", id), client);
        } else {
            collection.insertOne(client);
        }
    }

    /** Hard delete a client by clientId. */
    public boolean delete(String clientId) {
        var r = collection.deleteOne(Filters.eq("_id", clientId));
        return r.getDeletedCount() > 0;
    }
}
