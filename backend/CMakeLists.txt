cmake_minimum_required(VERSION 3.16.3)
project(microservice)

add_subdirectory(mongodbc)
add_subdirectory(test)

include_directories(/usr/local/include/mongocxx/v_noabi)
include_directories(/usr/local/include/bsoncxx/v_noabi)
include_directories(mongodbc/inc)


include_directories(inc)
include_directories(/usr/local/ACE_TAO-7.0.0/include)

link_directories(/usr/local/ACE_TAO-7.0.0/lib)

#Adding Complier flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -g -Wall -Wextra")

#However, the file(GLOB...) allows for wildcard additions:
file(GLOB SOURCES "src/*.cc")

add_executable(uniservice ${SOURCES})
target_link_libraries(uniservice pthread)
target_link_libraries(uniservice ACE)
target_link_libraries(uniservice ACE_SSL)
target_link_libraries(uniservice ssl crypto)
target_link_libraries(uniservice mongodbcxx)
