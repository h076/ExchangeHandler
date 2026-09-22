cmake_minimum_required(VERSION 3.12)
project(market_book LANGUAGES CXX)

# C++20 is required for std::span
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost (SSL component is required for TLS)
find_package(Boost REQUIRED)

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
)

# Create a library from the connection sources
add_library(connection_lib ${CONNECTION_SOURCES})

# Link the library with Boost dependencies
# Boost::headers provides the include directories for Asio/Beast
target_link_libraries(connection_lib
    PRIVATE
        Boost::headers
)

# Install the library
install(TARGETS connection_lib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
