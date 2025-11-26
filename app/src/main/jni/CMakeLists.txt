cmake_minimum_required(VERSION 3.22.1)

project("scashminer")

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the native library
add_library(scashminer SHARED
        scashminer.cpp)

# Link Android libraries
find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(scashminer
        ${log-lib}
        ${android-lib})
