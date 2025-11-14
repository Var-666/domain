cmake_minimum_required(VERSION 3.20)
project(domain CXX)

# 生成编译命令数据库，方便代码补全和静态分析工具使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost 1.74 REQUIRED)
find_package(Threads REQUIRED)
find_package(Lua REQUIRED)
find_package(spdlog REQUIRED)

# 源文件
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

set(MAIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

add_executable(domain ${MAIN_SOURCE} ${CORE_SOURCES})
add_executable(client clientTest.cpp ${CORE_SOURCES})

# 头文件目录：你自己的 + Boost 的
target_include_directories(domain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)
target_include_directories(client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(domain
    PRIVATE
        Threads::Threads
        ${LUA_LIBRARIES}
        spdlog::spdlog
)

target_link_libraries(client
    PRIVATE
        Threads::Threads
        ${LUA_LIBRARIES}
        spdlog::spdlog
)