package com.xpmile.onprem.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpRequest;
import org.springframework.http.client.ClientHttpRequestExecution;
import org.springframework.http.client.ClientHttpRequestInterceptor;
import org.springframework.http.client.ClientHttpResponse;
import org.springframework.web.client.RestTemplate;

import java.io.IOException;
import java.util.List;

@Configuration
public class BackendConfig {

    @Value("${xpmile.backend.base-url}")
    private String baseUrl;

    public String getBaseUrl() {
        return baseUrl;
    }

    @Bean
    public RestTemplate restTemplate() {
        RestTemplate restTemplate = new RestTemplate();
        restTemplate.setInterceptors(List.of(new ClientHttpRequestInterceptor() {
            @Override
            public ClientHttpResponse intercept(HttpRequest request, byte[] body,
                                                ClientHttpRequestExecution execution) throws IOException {
                request.getHeaders().set("onprem-ui", "onprem-ui");
                request.getHeaders().set("client", "onprem-xpmil-v.0.1");
                return execution.execute(request, body);
            }
        }));
        return restTemplate;
    }
}
