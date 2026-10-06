cmake_minimum_required(VERSION 3.22.1)
project(idoly_localify LANGUAGES CXX)

find_package(xdl REQUIRED CONFIG)
find_package(shadowhook REQUIRED CONFIG)

add_library(idoly_localify SHARED localify.cpp)
target_compile_features(idoly_localify PRIVATE cxx_std_17)
# Optimize dictionary and MasterDB parsing in all build types.
target_compile_options(idoly_localify PRIVATE -O2)
target_include_directories(idoly_localify PRIVATE third_party)
target_link_libraries(idoly_localify PRIVATE xdl::xdl shadowhook::shadowhook log)
