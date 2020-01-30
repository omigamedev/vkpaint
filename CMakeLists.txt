cmake_minimum_required(VERSION 3.0.0)
project(VulkanTest VERSION 0.1.0)

include_directories("C:\\VulkanSDK\\1.1.130.0\\Include")
link_directories("C:\\VulkanSDK\\1.1.130.0\\Lib")
add_compile_options("/std:c++17")

add_executable(VulkanTest VulkanTest.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
