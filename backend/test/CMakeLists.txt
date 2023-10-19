cmake_minimum_required(VERSION 3.16.3)
project(offtarget)
find_package(GTest REQUIRED)

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(../mongodbc/inc)
include_directories(../inc)
include_directories(test)
include_directories(/usr/local/ACE_TAO-7.0.0/include)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Complier flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "*.cc" "../mongodbc/src/mongodbc.cc" 
                         "../src/emailservice.cc" 
                         "../src/emailservice_fsm.cc")

enable_testing()

add_executable(offtarget ${SOURCES})
target_link_libraries(offtarget gtest_main)

target_link_libraries(offtarget pthread)
target_link_libraries(offtarget ACE)
target_link_libraries(offtarget ACE_SSL)
target_link_libraries(offtarget ssl crypto)
target_link_libraries(offtarget mongodbcxx)
target_link_libraries(offtarget gtest)

add_test(offtarget_gtests offtarget)
gtest_discover_tests(offtarget)
