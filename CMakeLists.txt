cmake_minimum_required(VERSION 3.24)
project(proxygen_app CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(OPENSSL_SUPPRESS_DEPRECATED)
# Homebrew prefix
set(HOMEBREW_PREFIX /opt/homebrew)

# Add include & lib paths globally
include_directories(${HOMEBREW_PREFIX}/include)
link_directories(${HOMEBREW_PREFIX}/lib)

add_executable(app src/main.cpp src/router/Router.cpp)

# Manually link the libraries you need.
# Adjust based on your code; these are the common ones for Proxygen HTTPServer + HTTP/3.
target_link_libraries(app
  PRIVATE
    proxygenhttpserver
    proxygen
    mvfst_transport
    mvfst_server
    mvfst_client
    fizz
    wangle
    folly
    brotlienc
    brotlicommon
    sodium
    ssl
    glog
    crypto
    pqxx
    z
    boost_context
    boost_filesystem
    boost_program_options
    boost_regex
    boost_thread
    pthread
)

