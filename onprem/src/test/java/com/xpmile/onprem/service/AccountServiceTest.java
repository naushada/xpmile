package com.xpmile.onprem.service;

import com.github.tomakehurst.wiremock.WireMockServer;
import com.github.tomakehurst.wiremock.core.WireMockConfiguration;
import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.model.LoginCredentials;
import org.junit.jupiter.api.*;
import org.springframework.web.client.RestTemplate;

import static com.github.tomakehurst.wiremock.client.WireMock.*;
import static org.assertj.core.api.Assertions.*;

class AccountServiceTest {

    static WireMockServer wireMock;
    AccountService service;

    @BeforeAll
    static void startWireMock() {
        wireMock = new WireMockServer(WireMockConfiguration.options().dynamicPort());
        wireMock.start();
    }

    @AfterAll
    static void stopWireMock() { wireMock.stop(); }

    @BeforeEach
    void setUp() {
        wireMock.resetAll();
        BackendConfig config = new BackendConfig() {
            @Override public String getBaseUrl() { return "http://localhost:" + wireMock.port(); }
        };
        service = new AccountService(new RestTemplate(), config);
    }

    @Test
    void createAccountPostsCorrectBody() {
        wireMock.stubFor(post(urlPathEqualTo("/api/v1/account/account"))
                .willReturn(okJson("{\"loginCredentials\":{\"accountCode\":\"ACC001\"}}")));

        Account account = new Account();
        account.setLoginCredentials(new LoginCredentials("ACC001", "secret"));

        assertThatNoException().isThrownBy(() -> service.createAccount(account));
        wireMock.verify(postRequestedFor(urlPathEqualTo("/api/v1/account/account"))
                .withRequestBody(containing("loginCredentials")));
    }

    @Test
    void createAccountWithAutoGenSetsFlag() {
        wireMock.stubFor(post(urlPathEqualTo("/api/v1/account/account"))
                .willReturn(okJson("{}")));

        Account account = new Account();
        account.setAccountCodeAutoGen(true);
        service.createAccount(account);

        wireMock.verify(postRequestedFor(urlPathEqualTo("/api/v1/account/account"))
                .withRequestBody(containing("\"isAccountCodeAutoGen\":true")));
    }

    @Test
    void getAccountReturnsAccountForUserId() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/account/account"))
                .withQueryParam("userId", equalTo("ACC001"))
                .willReturn(okJson("[{\"loginCredentials\":{\"accountCode\":\"ACC001\"}}]")));

        Account result = service.getAccount("ACC001");

        assertThat(result).isNotNull();
        assertThat(result.getLoginCredentials().getAccountCode()).isEqualTo("ACC001");
    }
}
