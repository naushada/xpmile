package com.xpmile.onprem.service;

import com.xpmile.onprem.config.BackendConfig;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestTemplate;

@Service
public class StatusService {

    public record BackendStatus(boolean agentOk, boolean dbOk) {
        public static final BackendStatus UP   = new BackendStatus(true,  true);
        public static final BackendStatus DOWN = new BackendStatus(false, false);
    }

    private final RestTemplate probeTemplate;
    private final BackendConfig backendConfig;

    public StatusService(RestTemplate restTemplate, BackendConfig backendConfig) {
        this.backendConfig = backendConfig;
        SimpleClientHttpRequestFactory factory = new SimpleClientHttpRequestFactory();
        factory.setConnectTimeout(3_000);
        factory.setReadTimeout(5_000);
        probeTemplate = new RestTemplate(factory);
        probeTemplate.setInterceptors(restTemplate.getInterceptors());
    }

    /**
     * Probes the backend with a lightweight account query.
     * Any JSON response (200 or 4xx) means the agent and DB are reachable.
     * A timeout or 5xx means the agent or DB is down.
     */
    public BackendStatus check() {
        String url = backendConfig.getBaseUrl()
                + "/api/v1/account/account?userId=__probe__&password=x";
        try {
            probeTemplate.getForObject(url, String.class);
            return BackendStatus.UP;
        } catch (HttpClientErrorException e) {
            return BackendStatus.UP;
        } catch (Exception e) {
            return BackendStatus.DOWN;
        }
    }
}
