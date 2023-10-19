#cmake_minimum_required(VERSION 2.8.9)
#project(microservice)

# Specify the minimum version you require.
#find_package (mongoc-1.0 REQUIRED)

include_directories(inc)
include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)

link_directories(/usr/lib/x86_64-linux-gnu)

include_directories(/usr/local/ACE_TAO-7.0.0/include)
link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Complier flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "src/*.cc")

add_library(mongodbcxx STATIC ${SOURCES})

target_link_libraries(mongodbcxx bsoncxx)
target_link_libraries(mongodbcxx mongocxx)
target_link_libraries(mongodbcxx ACE)

