cmake_minimum_required(VERSION 3.20)
project(G3D VERSION 0.1.0 LANGUAGES CXX)

option(G3D_BUILD_TOOLS "Build G3D command-line tools" ON)
option(G3D_BUILD_TESTS "Build G3D tests" ON)

add_library(g3d STATIC src/model.cpp)
add_library(G3D::g3d ALIAS g3d)
target_include_directories(g3d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(g3d PUBLIC cxx_std_20)
set_target_properties(g3d PROPERTIES CXX_EXTENSIONS OFF)

if(G3D_BUILD_TOOLS)
    add_executable(g3d-tool tools/g3d_tool.cpp)
    target_link_libraries(g3d-tool PRIVATE G3D::g3d)
endif()

if(G3D_BUILD_TESTS)
    enable_testing()
    add_executable(g3d-tests tests/test_model.cpp)
    target_link_libraries(g3d-tests PRIVATE G3D::g3d)
    add_test(NAME g3d-smoke COMMAND g3d-tests)
endif()

include(GNUInstallDirs)
install(TARGETS g3d EXPORT G3DTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(G3D_BUILD_TOOLS)
    install(TARGETS g3d-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
