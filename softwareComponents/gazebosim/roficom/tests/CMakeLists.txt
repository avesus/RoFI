cmake_minimum_required(VERSION 3.11)

add_executable(test-gazebosimRoficom test_main.cpp test_roficom_connect.cpp)
target_link_libraries(test-gazebosimRoficom Catch2 roficomPlugin)
