cmake_minimum_required(VERSION 3.20)
project(domain CXX)

set(CMAKE_CXX_STANDARD 20)

# 源文件
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# 主程序入口
set(MAIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# 可执行文件
add_executable(domain ${MAIN_SOURCE} ${CORE_SOURCES} )

# 头文件目录
target_include_directories(domain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 线程库（必加，否则 Mac/Linux 上有时会缺 pthread）
find_package(Threads REQUIRED)
target_link_libraries(domain PRIVATE Threads::Threads)
