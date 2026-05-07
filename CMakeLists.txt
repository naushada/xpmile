cmake_minimum_required(VERSION 3.16.3)
project(microservice)

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
include_directories(modules/module/oauth2/inc)
include_directories(modules/module/whatsapp/inc)
include_directories(modules/module/thirdparty)
include_directories(modules/module/wsdbproxy/inc)
include_directories(modules/module/wsdbagent/inc)
include_directories(/usr/local/ACE_TAO-7.0.0/include)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

file(GLOB MODULE_WEBSERVICE_SOURCES "modules/module/webservice/src/*.cpp")
file(GLOB MODULE_HTTP_SOURCES       "modules/module/http/src/*.cpp")
file(GLOB MODULE_EMAIL_SOURCES      "modules/module/email/src/*.cpp")
file(GLOB MODULE_OAUTH2_SOURCES     "modules/module/oauth2/src/*.cpp")
file(GLOB MODULE_WHATSAPP_SOURCES   "modules/module/whatsapp/src/*.cpp")
file(GLOB MODULE_WSDBPROXY_SOURCES  "modules/module/wsdbproxy/src/*.cpp")

add_executable(uniservice
    ${MODULE_WEBSERVICE_SOURCES}
    ${MODULE_HTTP_SOURCES}
    ${MODULE_EMAIL_SOURCES}
    ${MODULE_OAUTH2_SOURCES}
    ${MODULE_WHATSAPP_SOURCES}
    ${MODULE_WSDBPROXY_SOURCES}
)
target_link_libraries(uniservice pthread)
target_link_libraries(uniservice ACE)
target_link_libraries(uniservice ACE_SSL)
target_link_libraries(uniservice ssl crypto)
target_link_libraries(uniservice mongodbcxx)
target_link_libraries(uniservice z)

file(GLOB MODULE_WSDBAGENT_SOURCES "modules/module/wsdbagent/src/*.cpp")

add_executable(wsdbagent
    ${MODULE_WSDBAGENT_SOURCES}
    modules/module/wsdbproxy/src/dbproto.cpp
    modules/module/wsdbproxy/src/wsframe.cpp
)
target_link_libraries(wsdbagent pthread)
target_link_libraries(wsdbagent ACE)
target_link_libraries(wsdbagent ACE_SSL)
target_link_libraries(wsdbagent ssl crypto)
target_link_libraries(wsdbagent mongodbcxx)
target_link_libraries(wsdbagent z)
