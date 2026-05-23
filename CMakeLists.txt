cmake_minimum_required(VERSION 3.16.3)
project(microservice)

# xmlsec1 — SAML XML-DSig verification (Phase E). pkg-config supplies the exact
# include dirs, defines, and libs the library was built with.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XMLSEC REQUIRED xmlsec1-openssl)

add_subdirectory(modules/module/mongodb)

option(BUILD_TESTS "Build test suite (requires GTest)" ON)
if(BUILD_TESTS)
  add_subdirectory(test)
endif()

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(modules/module/webservice/inc)
include_directories(modules/module/http/inc)
include_directories(modules/module/mongodb/inc)
include_directories(modules/module/email/inc)
include_directories(modules/module/sso/inc)
include_directories(modules/module/whatsapp/inc)
include_directories(modules/module/thirdparty)
include_directories(modules/module/wsdbproxy/inc)
include_directories(modules/module/wsdbagent/inc)
include_directories(modules/module/security/inc)
include_directories(/usr/local/ACE_TAO-7.0.0/include)
include_directories(/usr/include/libxml2)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

file(GLOB MODULE_WEBSERVICE_SOURCES "modules/module/webservice/src/*.cpp")
file(GLOB MODULE_HTTP_SOURCES       "modules/module/http/src/*.cpp")
file(GLOB MODULE_EMAIL_SOURCES      "modules/module/email/src/*.cpp")
file(GLOB MODULE_SSO_SOURCES     "modules/module/sso/src/*.cpp")
file(GLOB MODULE_WHATSAPP_SOURCES   "modules/module/whatsapp/src/*.cpp")
file(GLOB MODULE_WSDBPROXY_SOURCES  "modules/module/wsdbproxy/src/*.cpp")
file(GLOB MODULE_SECURITY_SOURCES  "modules/module/security/src/*.cpp")

add_executable(uniservice
    ${MODULE_WEBSERVICE_SOURCES}
    ${MODULE_HTTP_SOURCES}
    ${MODULE_EMAIL_SOURCES}
    ${MODULE_SSO_SOURCES}
    ${MODULE_WHATSAPP_SOURCES}
    ${MODULE_WSDBPROXY_SOURCES}
    ${MODULE_SECURITY_SOURCES}
)
target_link_libraries(uniservice pthread)
target_link_libraries(uniservice ACE)
target_link_libraries(uniservice ACE_SSL)
target_link_libraries(uniservice ssl crypto)
target_link_libraries(uniservice mongodbcxx)
target_link_libraries(uniservice z)
target_link_libraries(uniservice xml2)
target_include_directories(uniservice PRIVATE ${XMLSEC_INCLUDE_DIRS})
target_compile_options(uniservice PRIVATE ${XMLSEC_CFLAGS_OTHER})
target_link_libraries(uniservice ${XMLSEC_LIBRARIES})

file(GLOB MODULE_WSDBAGENT_SOURCES "modules/module/wsdbagent/src/*.cpp")

add_executable(wsdbagent
    ${MODULE_WSDBAGENT_SOURCES}
    modules/module/wsdbproxy/src/dbproto.cpp
    modules/module/wsdbproxy/src/wsframe.cpp
    modules/module/security/src/innertls.cpp
)
target_link_libraries(wsdbagent pthread)
target_link_libraries(wsdbagent ACE)
target_link_libraries(wsdbagent ACE_SSL)
target_link_libraries(wsdbagent ssl crypto)
target_link_libraries(wsdbagent mongodbcxx)
target_link_libraries(wsdbagent z)
