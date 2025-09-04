cmake_minimum_required(VERSION 3.20)
project(GameServer VERSION 1.0.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译器警告
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    
    # 调试模式额外选项
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address -fsanitize=undefined")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
endif()

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖
find_package(Threads REQUIRED)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
file(GLOB_RECURSE SOURCES 
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.cc
)

# 核心库
add_library(gameserver_core STATIC
    src/network/socket.cpp
    src/network/epoll_poller.cpp
    src/network/event_loop.cpp
    src/network/tcp_server.cpp
    src/network/tcp_connection.cpp
    src/network/buffer.cpp
    src/utils/memory_pool.cpp
    src/utils/thread_pool.cpp
    src/utils/logger.cpp
    src/utils/timer.cpp
)

target_link_libraries(gameserver_core
    Threads::Threads
)

# Echo服务器示例
add_executable(echo_server
    examples/echo_server.cpp
)

target_link_libraries(echo_server
    gameserver_core
    Threads::Threads
)

# 聊天室服务器示例
add_executable(chat_server
    examples/chat_server.cpp
)

target_link_libraries(chat_server
    gameserver_core
    Threads::Threads
)

# 压力测试客户端
add_executable(stress_client
    examples/stress_client.cpp
)

target_link_libraries(stress_client
    gameserver_core
    Threads::Threads
)

# 单元测试
enable_testing()

add_executable(test_buffer
    tests/test_buffer.cpp
)
target_link_libraries(test_buffer gameserver_core)
add_test(NAME buffer_test COMMAND test_buffer)

add_executable(test_memory_pool
    tests/test_memory_pool.cpp
)
target_link_libraries(test_memory_pool gameserver_core)
add_test(NAME memory_pool_test COMMAND test_memory_pool)

# 性能测试
add_executable(benchmark
    tests/benchmark.cpp
)
target_link_libraries(benchmark gameserver_core Threads::Threads)

# 安装规则
install(TARGETS echo_server chat_server
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

# 打包配置
include(CPack)
set(CPACK_PACKAGE_NAME "GameServer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ;ZIP")