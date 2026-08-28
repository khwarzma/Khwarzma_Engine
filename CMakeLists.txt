cmake_minimum_required(VERSION 3.20)
project(kh_core_server CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

# مسار الـ Headers المباشر
include_directories(${CMAKE_SOURCE_DIR}/include)

# تجميع سورس كل محرك بشكل فرعي دقيق
file(GLOB_RECURSE COMP_SOURCES "src/comp_core/*.cpp")
file(GLOB_RECURSE SHIELD_SOURCES "src/shield_core/*.cpp")
file(GLOB_RECURSE DAEMON_SOURCES "src/server_daemon/*.cpp")

# بناء الاستاتيك لايبرري لكل محرك
add_library(khcomp_core STATIC ${COMP_SOURCES})
add_library(khshield_core STATIC ${SHIELD_SOURCES})

# بناء ملف الـ Daemon الرئيسي للربط
add_executable(kh_core_daemon ${DAEMON_SOURCES})

# ربط المكتبات والـ POSIX Threads
target_link_libraries(kh_core_daemon PRIVATE khcomp_core khshield_core pthread)