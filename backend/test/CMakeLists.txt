cmake_minimum_required(VERSION 3.16.3)
project(offtarget)
find_package(GTest REQUIRED)

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(../../modules/module/webservice/inc)
include_directories(../../modules/module/http/inc)
include_directories(../../modules/module/mongodb/inc)
include_directories(../../modules/module/email/inc)
include_directories(../../modules/module/oauth2/inc)
include_directories(../../modules/module/whatsapp/inc)
include_directories(../../modules/module/thirdparty)
include_directories(/usr/local/ACE_TAO-7.0.0/include)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "*.cc"
    "../../modules/module/mongodb/src/mongodbc.cpp"
    "../../modules/module/email/src/emailservice.cpp"
    "../../modules/module/email/src/emailservice_fsm.cpp"
    "../../modules/module/http/src/http_parser.cpp"
    "../../modules/module/webservice/src/webservice.cpp")

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

add_test(offtarget_gtests offtarget)
gtest_discover_tests(offtarget)
