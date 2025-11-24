cmake_minimum_required(VERSION 3.20)
project(domain CXX)

# 生成编译命令数据库，方便代码补全和静态分析工具使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FETCHCONTENT_QUIET OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_TYPE Debug)


if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost 1.74 REQUIRED)
find_package(Threads REQUIRED)
find_package(Lua REQUIRED)
find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
endif()

# 源文件
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

set(MAIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/examples/server/main.cpp)
set(CLIENT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/examples/client/clientTest.cpp)

add_executable(domain ${MAIN_SOURCE} ${CORE_SOURCES})
add_executable(client ${CLIENT_SOURCE} ${CORE_SOURCES} sdk/GatewayClient.cpp)

# 头文件目录：你自己的 + Boost 的
target_include_directories(domain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Server
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Routes
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Util
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Client
        ${Boost_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)
target_include_directories(client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Server
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Routes
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Util
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Client
        ${Boost_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(domain
    PRIVATE
        Threads::Threads
        ${LUA_LIBRARIES}
        spdlog::spdlog
        protobuf::libprotobuf
        nlohmann_json
)

target_link_libraries(client
    PRIVATE
        Threads::Threads
        ${LUA_LIBRARIES}
        spdlog::spdlog
        protobuf::libprotobuf
        nlohmann_json
)

# SDK 头/源目录
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
target_sources(client PRIVATE
    sdk/GatewayClient.cpp
)
