package com.xpmile.onprem.config;

import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoClients;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * MongoDB connection for the on-prem app.
 *
 * The on-prem Vaadin app runs on the same machine as MongoDB, so the
 * SSO-config admin view writes the {@code sso_config} document directly to the
 * co-located database — there is no internet-facing config-write endpoint to
 * defend (docs/design/sso/sso-design.md §10).
 */
@Configuration
public class MongoConfig {

    @Bean(destroyMethod = "close")
    public MongoClient mongoClient(
            @Value("${xpmile.mongo.uri:mongodb://localhost:27017}") String uri) {
        return MongoClients.create(uri);
    }
}
