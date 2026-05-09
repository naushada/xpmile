package com.xpmile.onprem.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestTemplate;

@Configuration
public class BackendConfig {

    @Value("${xpmile.backend.base-url}")
    private String baseUrl;

    public String getBaseUrl() {
        return baseUrl;
    }

    @Bean
    public RestTemplate restTemplate() {
        return new RestTemplate();
    }
}
