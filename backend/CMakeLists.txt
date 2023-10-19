cmake_minimum_required(VERSION 2.8.9)
project(microservice)

add_subdirectory(mongodbc)

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(mongodbc/inc)


include_directories(inc)
include_directories(/usr/local/ACE_TAO-7.0.0/include)
link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Complier flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "src/*.cc")

add_executable(uniservice ${SOURCES})
target_link_libraries(uniservice pthread)
target_link_libraries(uniservice ACE)
target_link_libraries(uniservice mongodbcxx)
