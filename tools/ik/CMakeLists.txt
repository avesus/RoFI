cmake_minimum_required(VERSION 3.6)
project(rofi)

set(CMAKE_CXX_STANDARD 17)

add_executable(rofi-ik executable.cpp)
target_include_directories(rofi-ik INTERFACE .)
target_link_libraries(rofi-ik PRIVATE kinematics legacy-configuration)