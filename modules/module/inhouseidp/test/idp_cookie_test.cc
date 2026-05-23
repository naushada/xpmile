// modules/module/inhouseidp/test/idp_cookie_test.cc

#include <gtest/gtest.h>

#include "idp_cookie.hpp"

TEST(IdpCookie, BuildSessionCookie_ContainsNameValueAndAllRequiredFlags) {
  auto c = idp::build_idp_session_cookie("ABC", 3600);
  EXPECT_NE(c.find("xpmile_idp_session=ABC"), std::string::npos);
  EXPECT_NE(c.find("HttpOnly"),                 std::string::npos);
  EXPECT_NE(c.find("Secure"),                   std::string::npos);
  EXPECT_NE(c.find("SameSite=Lax"),             std::string::npos);
  EXPECT_NE(c.find("Path=/api/v1/idp/"),        std::string::npos);
  EXPECT_NE(c.find("Max-Age=3600"),             std::string::npos);
}

TEST(IdpCookie, BuildSessionCookie_DefaultMaxAge_Is_12h) {
  auto c = idp::build_idp_session_cookie("X");
  EXPECT_NE(c.find("Max-Age=43200"), std::string::npos);
}

TEST(IdpCookie, BuildPendingCookie_DistinctName_DefaultShortTtl) {
  auto c = idp::build_idp_pending_cookie("RT1");
  EXPECT_NE(c.find("xpmile_idp_pending=RT1"), std::string::npos);
  EXPECT_NE(c.find("Max-Age=600"),             std::string::npos);
}

TEST(IdpCookie, BuildExpiredCookie_MaxAge0_EmptyValue) {
  auto c = idp::build_expired_cookie(idp::kIdpSessionCookieName);
  EXPECT_NE(c.find("xpmile_idp_session="), std::string::npos);
  EXPECT_NE(c.find("Max-Age=0"),           std::string::npos);
}

TEST(IdpCookie, ParseCookie_SingleCookie) {
  EXPECT_EQ(idp::parse_cookie("xpmile_idp_session=ABC", "xpmile_idp_session"),
            "ABC");
}

TEST(IdpCookie, ParseCookie_MultiCookieHeader) {
  const std::string header = "a=1; xpmile_idp_session=ABC; b=2";
  EXPECT_EQ(idp::parse_cookie(header, "xpmile_idp_session"), "ABC");
  EXPECT_EQ(idp::parse_cookie(header, "a"), "1");
  EXPECT_EQ(idp::parse_cookie(header, "b"), "2");
}

TEST(IdpCookie, ParseCookie_AbsentReturnsEmpty) {
  EXPECT_EQ(idp::parse_cookie("a=1; b=2", "xpmile_idp_session"), "");
  EXPECT_EQ(idp::parse_cookie("", "x"), "");
  EXPECT_EQ(idp::parse_cookie("name=value", ""), "");
}

TEST(IdpCookie, ParseCookie_ToleratesExtraWhitespace) {
  EXPECT_EQ(idp::parse_cookie("  a=1 ;  xpmile_idp_session=ABC ;  b=2  ",
                                "xpmile_idp_session"),
            "ABC ");
  // (trailing space in the value is preserved; cookie spec says values
  //  are up to the next ';' so we don't strip it here)
}

TEST(IdpCookie, ParseCookie_PrefixMatchDoesNotConfuse) {
  EXPECT_EQ(idp::parse_cookie("xpmile_idp_pending=RT", "xpmile_idp_session"), "");
  EXPECT_EQ(idp::parse_cookie("xpmile_idp_session_other=NO", "xpmile_idp_session"), "");
}
