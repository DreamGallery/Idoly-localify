cmake_minimum_required(VERSION 3.22.1)
project(idoly_localify LANGUAGES CXX)

find_package(xdl REQUIRED CONFIG)
find_package(shadowhook REQUIRED CONFIG)

add_library(idoly_runtime_core STATIC
    runtime/dictionary_catalog.cpp
    runtime/dictionary_runtime.cpp)
target_compile_features(idoly_runtime_core PUBLIC cxx_std_17)
set_target_properties(idoly_runtime_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(idoly_runtime_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} third_party)
target_compile_options(idoly_runtime_core PRIVATE -O2)

add_library(idoly_localify SHARED localify.cpp runtime/preview_access.cpp)
target_compile_features(idoly_localify PRIVATE cxx_std_17)
# Optimize dictionary and MasterDB parsing in all build types.
target_compile_options(idoly_localify PRIVATE -O2)
target_include_directories(idoly_localify PRIVATE third_party)
target_link_libraries(idoly_localify PRIVATE idoly_runtime_core xdl::xdl shadowhook::shadowhook log)
