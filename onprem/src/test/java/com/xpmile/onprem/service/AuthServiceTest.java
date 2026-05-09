package com.xpmile.onprem.service;

import com.github.tomakehurst.wiremock.WireMockServer;
import com.github.tomakehurst.wiremock.core.WireMockConfiguration;
import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import org.junit.jupiter.api.*;
import org.springframework.web.client.RestTemplate;

import static com.github.tomakehurst.wiremock.client.WireMock.*;
import static org.assertj.core.api.Assertions.*;

class AuthServiceTest {

    static WireMockServer wireMock;
    AuthService authService;
    // captured once per setUp so getBaseUrl() never calls wireMock.port() on a stopped server
    int wireMockPort;

    @BeforeAll
    static void startWireMock() {
        wireMock = new WireMockServer(WireMockConfiguration.options().dynamicPort());
        wireMock.start();
    }

    @AfterAll
    static void stopWireMock() {
        wireMock.stop();
    }

    @BeforeEach
    void setUp() {
        wireMock.resetAll();
        wireMockPort = wireMock.port();
        BackendConfig config = new BackendConfig() {
            @Override
            public String getBaseUrl() { return "http://localhost:" + wireMockPort; }
        };
        authService = new AuthService(new RestTemplate(), config);
    }

    @Test
    void successfulLoginReturnsAccount() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/account/account"))
                .withQueryParam("userId", equalTo("admin"))
                .withQueryParam("password", equalTo("pass"))
                .willReturn(okJson("[{\"loginCredentials\":{\"accountCode\":\"admin\"}}]")));

        Account account = authService.login("admin", "pass");

        assertThat(account).isNotNull();
        assertThat(account.getLoginCredentials().getAccountCode()).isEqualTo("admin");
    }

    @Test
    void successfulLoginWithSingleObjectReturnsAccount() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/account/account"))
                .withQueryParam("userId", equalTo("admin"))
                .withQueryParam("password", equalTo("pass"))
                .willReturn(okJson("{\"loginCredentials\":{\"accountCode\":\"admin\"}}")));

        Account account = authService.login("admin", "pass");

        assertThat(account).isNotNull();
        assertThat(account.getLoginCredentials().getAccountCode()).isEqualTo("admin");
    }

    @Test
    void wrongPasswordThrowsAuthException() {
        // backend returns error object, not an array, on bad credentials
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/account/account"))
                .willReturn(okJson("{\"cause\":\"Invalid Credentials\",\"error\":404,\"status\":\"failure\"}")));

        assertThatThrownBy(() -> authService.login("admin", "wrong"))
                .isInstanceOf(AuthException.class)
                .hasMessageContaining("Invalid Credentials");
    }

    @Test
    void backendUnreachableThrowsAuthException() {
        wireMock.stop();
        try {
            assertThatThrownBy(() -> authService.login("admin", "pass"))
                    .isInstanceOf(AuthException.class)
                    .hasMessageContaining("Backend unavailable");
        } finally {
            wireMock.start();
        }
    }
}
