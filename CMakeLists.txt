# 3.14 for FetchContent_MakeAvailable
cmake_minimum_required(VERSION 3.14)
project(market_book LANGUAGES CXX)

# C++20 is required for std::span
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Write build/compile_commands.json for clangd (include paths, flags)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Boost (SSL component is required for TLS)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# simdjson (decode layer): use an installed package if there is one,
# otherwise download and build the pinned release
find_package(simdjson CONFIG QUIET)
if(NOT simdjson_FOUND)
    include(FetchContent)
    FetchContent_Declare(simdjson
        URL      https://github.com/simdjson/simdjson/archive/refs/tags/v5.0.5.tar.gz
        URL_HASH SHA256=50bb17769e1054ff1bfcd5dbbfcf33544c77f6ebdb31a318436ea0e47f78f571
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(simdjson)
endif()

# Asio and Beast are header-only libraries
if(NOT TARGET Boost::headers)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}")
endif()

# Source files for the connection module
set(CONNECTION_SOURCES
    src/connection/ws_session.cpp
    src/connection/session_state_machine.cpp
    src/connection/subscription_manager.cpp
    src/connection/reconnect_policy.cpp
    src/connection/message_source_deque.cpp
)

# Create a library from the connection sources
add_library(connection_lib ${CONNECTION_SOURCES})

# Link the library with Boost dependencies
# Boost::headers provides the include directories for Asio/Beast
# PUBLIC: the connection headers include Boost, and ws_session calls OpenSSL directly
target_link_libraries(connection_lib
    PUBLIC
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)
target_include_directories(connection_lib
    PUBLIC
        src/connection
)

# Decode module: header-only for now (the venue decoders are templates)
add_library(decode_lib INTERFACE)
target_link_libraries(decode_lib
    INTERFACE
        simdjson::simdjson
)
target_include_directories(decode_lib
    INTERFACE
        src/decode
        src/common
)

# Live smoke test against Kraken's public WebSocket feed (needs network)
option(BUILD_LIVE_TESTS "Build live venue test executables" ON)
if(BUILD_LIVE_TESTS)
    add_executable(kraken_test src/connection/kraken_test.cpp)
    target_link_libraries(kraken_test PRIVATE connection_lib)
endif()

# Install the library
install(TARGETS connection_lib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Unit tests
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()

    add_executable(subscription_manager_test
        tests/unit/connection/subscription_manager_test.cpp
    )
    target_link_libraries(subscription_manager_test
        PRIVATE
            connection_lib
    )

    add_test(NAME subscription_manager_test COMMAND subscription_manager_test)

    add_executable(reconnect_policy_test
        tests/unit/connection/reconnect_policy_test.cpp
    )
    target_link_libraries(reconnect_policy_test
        PRIVATE
            connection_lib
    )

    add_test(NAME reconnect_policy_test COMMAND reconnect_policy_test)

    add_executable(session_state_machine_test
        tests/unit/connection/session_state_machine_test.cpp
    )
    target_link_libraries(session_state_machine_test
        PRIVATE
            connection_lib
    )

    add_test(NAME session_state_machine_test COMMAND session_state_machine_test)

    add_executable(message_source_deque_test
        tests/unit/connection/message_source_deque_test.cpp
    )
    target_link_libraries(message_source_deque_test
        PRIVATE
            connection_lib
    )

    add_test(NAME message_source_deque_test COMMAND message_source_deque_test)

    add_executable(kraken_decoder_test
        tests/unit/decode/kraken_decoder_test.cpp
    )
    target_link_libraries(kraken_decoder_test
        PRIVATE
            decode_lib
    )

    add_test(NAME kraken_decoder_test COMMAND kraken_decoder_test)

    add_executable(kraken_parse_test
        tests/unit/decode/kraken_parse_test.cpp
    )
    target_link_libraries(kraken_parse_test
        PRIVATE
            decode_lib
    )

    add_test(NAME kraken_parse_test COMMAND kraken_parse_test)
endif()
