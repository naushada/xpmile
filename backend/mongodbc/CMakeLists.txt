cmake_minimum_required(VERSION 3.16.3)

# Specify the minimum version you require.

include_directories(inc)
include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)

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
target_link_libraries(mongodbcxx ssl crypto)


