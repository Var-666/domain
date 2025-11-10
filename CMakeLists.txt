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

# 找 Boost（不再指定 COMPONENTS system）
find_package(Boost 1.74 REQUIRED)

# 找线程库
find_package(Threads REQUIRED)

# 源文件
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

set(MAIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

add_executable(domain ${MAIN_SOURCE} ${CORE_SOURCES})

# 头文件目录：你自己的 + Boost 的
target_include_directories(domain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
)

# 链接库：只需要线程库就够了（Boost 这里只用头文件）
target_link_libraries(domain
    PRIVATE
        Threads::Threads
)