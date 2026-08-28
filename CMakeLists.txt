cmake_minimum_required(VERSION 3.20)

# std::expected requires GCC 12 or newer.
if(NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER MATCHES "g\\+\\+(-11)?$")
    find_program(KH_CORE_CXX_COMPILER NAMES g++-12 g++-13 g++-14)
    if(KH_CORE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER "${KH_CORE_CXX_COMPILER}" CACHE FILEPATH "C++ compiler" FORCE)
    endif()
endif()

project(kh_core_server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -Wall -Wextra -pthread)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

file(GLOB_RECURSE COMP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/comp_core/*.cpp")
file(GLOB_RECURSE SHIELD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/shield_core/*.cpp")
file(GLOB_RECURSE DAEMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/server_daemon/*.cpp")

add_library(khcomp_core STATIC ${COMP_SOURCES})
add_library(khshield_core STATIC ${SHIELD_SOURCES})

# بناء الـ Daemon باستخدام نفس الإعدادات والمناظرة المطلقة
add_executable(kh_core_daemon ${DAEMON_SOURCES})
target_link_libraries(kh_core_daemon PRIVATE khcomp_core khshield_core pthread)
