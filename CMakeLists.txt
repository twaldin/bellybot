cmake_minimum_required(VERSION 3.16)
project(bellybot)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Werror")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

include_directories(inc /opt/homebrew/include
/opt/homebrew/opt/curl/include
/opt/homebrew/Cellar/rapidjson/1.1.0/include/
external/jwt-cpp/include/)
link_directories(/opt/homebrew/opt/curl/lib)

# Main executable
add_executable(bellybot src/main.cpp)
target_link_libraries(bellybot curl pthread OpenSSL::SSL OpenSSL::Crypto)



