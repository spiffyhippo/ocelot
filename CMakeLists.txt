cmake_minimum_required(VERSION 3.1)
project(ocelot
    VERSION 2.0.0
)

# Make a subdirectory, and run cmake there (mkdir build; cd build; cmake ..)
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
   message(FATAL_ERROR "Source and build directories cannot be the same.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")
set(CMAKE_CXX_STANDARD 17)  # Updated to C++17 for Boost.Beast and std::string_view
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wfatal-errors")

message(STATUS "CMAKE_MODULE_PATH='${CMAKE_MODULE_PATH}'")
find_package(Jemalloc)
find_package(Tcmalloc)
if(JEMALLOC_FOUND)
    option(USE_JEMALLOC "Use jemalloc instead of system-provided malloc" ON)
    if(TCMALLOC_FOUND)
        option(USE_TCMALLOC "Use tcmalloc (Google's thread-cacheing malloc) instead of system-provided malloc" OFF)
    else()
        set(USE_TCMALLOC OFF)
    endif()
else()
    set(USE_JEMALLOC OFF)
    if(TCMALLOC_FOUND)
        option(USE_TCMALLOC "Use tcmalloc (Google's thread-cacheing malloc) instead of system-provided malloc" ON)
    else()
        set(USE_TCMALLOC OFF)
    endif()
endif()

# Boost Libraries
find_package(Boost COMPONENTS system iostreams REQUIRED)  # Added beast as part of boost
find_package(Boost COMPONENTS beast REQUIRED)             # Boost.Beast for HTTP parsing
message(STATUS "Boost_INCLUDE_DIRS='${Boost_INCLUDE_DIRS}'")
message(STATUS "Boost_LIBRARIES='${Boost_LIBRARIES}'")
include_directories(${Boost_INCLUDE_DIRS})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# MySQL
find_package(MYSQL REQUIRED)
find_package(MYSQLPP REQUIRED)

# LibEv
find_package(LibEv REQUIRED)
include_directories(${LIBEV_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/libs)

file(GLOB HEADERS src/*.h)
file(GLOB SOURCES src/*.cpp)

# Add executable target for ocelot
add_executable(ocelot ${HEADERS} ${SOURCES})

# Link Libraries
SET(LINK_LIBRARIES
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_IOSTREAMS_LIBRARY}
    ${Boost_BEAST_LIBRARY}     # Added Boost.Beast library to link
    ${LIBEV_LIBRARY}
    ${MYSQLPP_LIBRARY}
    Threads::Threads
)

if (USE_JEMALLOC)
    message(STATUS "Build with jemalloc")
    list(APPEND LINK_LIBRARIES ${JEMALLOC_LIBRARY})
elseif (USE_TCMALLOC)
    message(STATUS "Build with tcmalloc")
    list(APPEND LINK_LIBRARIES ${TCMALLOC_LIBRARY})
endif()

# Link target executable with necessary libraries
target_link_libraries(
    ocelot
    ${LINK_LIBRARIES}
)

