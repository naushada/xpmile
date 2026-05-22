package com.xpmile.onprem.service;

import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoCollection;
import com.mongodb.client.model.Filters;
import org.bson.Document;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Reads and writes the single {@code sso_config} document directly in the
 * co-located on-prem MongoDB (docs/design/sso/sso-design.md §10). The C++
 * backend reads the same document over the wsdbagent channel and hot-reloads
 * on change — an operator edit here takes effect within ~60 s, no redeploy.
 *
 * {@code clientSecret} is write-only: {@link #load()} never returns a stored
 * secret to the UI (it exposes only a synthetic {@code _hasClientSecret}
 * flag), and {@link #save(Document)} keeps the stored secret whenever the
 * caller leaves a provider's secret blank.
 */
@Service
public class SsoConfigService {

    private static final String COLLECTION = "sso_config";

    private final MongoCollection<Document> collection;

    public SsoConfigService(MongoClient mongoClient,
                            @Value("${xpmile.mongo.database:xpmile}") String database) {
        this.collection = mongoClient.getDatabase(database).getCollection(COLLECTION);
    }

    /**
     * Load the SSO configuration. Each provider carries a synthetic
     * {@code _hasClientSecret} boolean; the secret itself is stripped so it
     * never reaches the UI.
     */
    public Document load() {
        Document doc = collection.find().first();
        if (doc == null) {
            return new Document("publicBaseUrl", "")
                    .append("providers", new ArrayList<Document>());
        }
        doc.remove("_id");
        for (Document provider : providersOf(doc)) {
            String secret = provider.getString("clientSecret");
            provider.put("_hasClientSecret", secret != null && !secret.isBlank());
            provider.remove("clientSecret");
        }
        return doc;
    }

    /**
     * Persist the SSO configuration. A provider whose {@code clientSecret} is
     * blank or absent keeps the secret already stored for that provider id.
     */
    public void save(Document config) {
        Document current = collection.find().first();
        Map<String, String> storedSecrets = secretsById(current);

        for (Document provider : providersOf(config)) {
            provider.remove("_hasClientSecret");
            String secret = provider.getString("clientSecret");
            if (secret == null || secret.isBlank()) {
                String kept = storedSecrets.get(provider.getString("id"));
                provider.put("clientSecret", kept != null ? kept : "");
            }
        }
        config.remove("_id");

        if (current != null) {
            collection.replaceOne(Filters.eq("_id", current.get("_id")), config);
        } else {
            collection.insertOne(config);
        }
    }

    private static List<Document> providersOf(Document doc) {
        List<Document> providers = doc.getList("providers", Document.class);
        return providers != null ? providers : new ArrayList<>();
    }

    private static Map<String, String> secretsById(Document doc) {
        Map<String, String> secrets = new HashMap<>();
        if (doc == null) {
            return secrets;
        }
        for (Document provider : providersOf(doc)) {
            String secret = provider.getString("clientSecret");
            if (secret != null && !secret.isBlank()) {
                secrets.put(provider.getString("id"), secret);
            }
        }
        return secrets;
    }
}
