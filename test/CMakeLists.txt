cmake_minimum_required(VERSION 3.16.3)
project(offtarget)
find_package(GTest REQUIRED)

# xmlsec1 — SAML XML-DSig verification + the test-side signing scaffold.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XMLSEC REQUIRED xmlsec1-openssl)

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(../modules/module/webservice/inc)
include_directories(../modules/module/webservice/test)
include_directories(../modules/module/http/inc)
include_directories(../modules/module/http/test)
include_directories(../modules/module/mongodb/inc)
include_directories(../modules/module/email/inc)
include_directories(../modules/module/email/test)
include_directories(../modules/module/sso/inc)
include_directories(../modules/module/sso/test)
include_directories(../modules/module/inhouseidp/inc)
include_directories(../modules/module/inhouseidp/test)
include_directories(../modules/module/whatsapp/inc)
include_directories(../modules/module/thirdparty)
include_directories(../modules/module/wsdbproxy/inc)
include_directories(../modules/module/wsdbproxy/test)
include_directories(../modules/module/security/inc)
include_directories(/usr/local/ACE_TAO-7.0.0/include)
include_directories(/usr/include/libxml2)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "*.cc"
    "../modules/module/mongodb/src/mongodbc.cpp"
    "../modules/module/email/src/emailservice.cpp"
    "../modules/module/email/src/emailservice_fsm.cpp"
    "../modules/module/email/test/emailservice_test.cc"
    "../modules/module/http/src/http_parser.cpp"
    "../modules/module/http/test/httpparser_test.cc"
    "../modules/module/webservice/src/webservice.cpp"
    "../modules/module/webservice/test/webservice_test.cc"
    "../modules/module/wsdbproxy/src/dbproto.cpp"
    "../modules/module/wsdbproxy/src/wsframe.cpp"
    "../modules/module/wsdbproxy/src/wsdbproxy.cpp"
    "../modules/module/security/src/innertls.cpp"
    "../modules/module/wsdbproxy/test/dbproto_test.cc"
    "../modules/module/wsdbproxy/test/wsframe_test.cc"
    "../modules/module/wsdbproxy/test/wsdbserver_test.cc"
    "../modules/module/wsdbproxy/test/wsproxy_test.cc"
    "../modules/module/security/test/innertls_test.cc"
    "../modules/module/sso/src/sso_cookie.cpp"
    "../modules/module/sso/src/sso_config.cpp"
    "../modules/module/sso/src/sso_session.cpp"
    "../modules/module/sso/src/sso_http_client.cpp"
    "../modules/module/sso/src/sso_util.cpp"
    "../modules/module/sso/src/sso_oidc.cpp"
    "../modules/module/sso/src/sso_jwt.cpp"
    "../modules/module/sso/src/sso_registry.cpp"
    "../modules/module/sso/src/sso_provisioning.cpp"
    "../modules/module/sso/src/sso_endpoints.cpp"
    "../modules/module/sso/src/saml_provider.cpp"
    "../modules/module/sso/src/saml_response.cpp"
    "../modules/module/sso/src/saml_signature.cpp"
    "../modules/module/sso/src/sso_csrf.cpp"
    "../modules/module/sso/src/sso_authz.cpp"
    "../modules/module/sso/test/sso_test.cc"
    "../modules/module/inhouseidp/src/wsdb_jwt_signer.cpp"
    "../modules/module/inhouseidp/test/sign_jwt_test.cc"
    "../modules/module/mongodb/test/mongodbc_test.cc")

enable_testing()

add_executable(offtarget ${SOURCES})
target_link_libraries(offtarget gtest_main)

target_link_libraries(offtarget pthread)
target_link_libraries(offtarget ACE)
target_link_libraries(offtarget ACE_SSL)
target_link_libraries(offtarget ssl crypto)
target_link_libraries(offtarget mongodbcxx)
target_link_libraries(offtarget gtest)
target_link_libraries(offtarget z)
target_link_libraries(offtarget xml2)
target_include_directories(offtarget PRIVATE ${XMLSEC_INCLUDE_DIRS})
target_compile_options(offtarget PRIVATE ${XMLSEC_CFLAGS_OTHER})
target_link_libraries(offtarget ${XMLSEC_LIBRARIES})

add_test(offtarget_gtests offtarget)
gtest_discover_tests(offtarget)
