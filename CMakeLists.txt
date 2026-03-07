cmake_minimum_required(VERSION 3.16)
project(easygamma CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)

# Check if wayland-client and wayland-scanner are available
pkg_check_modules(WAYLAND_CLIENT wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner)

set(SOURCES main.cpp)
set(COMPILE_DEFS "")

if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER)
    message(STATUS "Wayland support: ON (wlr-gamma-control protocol)")

    # Generate protocol bindings from XML
    add_custom_command(
        OUTPUT  wlr-gamma-control-unstable-v1-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} client-header
                ${CMAKE_CURRENT_SOURCE_DIR}/wlr-gamma-control-unstable-v1.xml
                wlr-gamma-control-unstable-v1-client-protocol.h
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wlr-gamma-control-unstable-v1.xml
    )
    add_custom_command(
        OUTPUT  wlr-gamma-control-unstable-v1-protocol.c
        COMMAND ${WAYLAND_SCANNER} private-code
                ${CMAKE_CURRENT_SOURCE_DIR}/wlr-gamma-control-unstable-v1.xml
                wlr-gamma-control-unstable-v1-protocol.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wlr-gamma-control-unstable-v1.xml
    )

    list(APPEND SOURCES
        gamma_wayland.c
        wlr-gamma-control-unstable-v1-protocol.c
        wlr-gamma-control-unstable-v1-client-protocol.h
    )
    list(APPEND COMPILE_DEFS USE_WAYLAND)
else()
    message(STATUS "Wayland support: OFF (wayland-client or wayland-scanner not found)")
endif()

add_executable(easygamma ${SOURCES})

target_include_directories(easygamma PRIVATE
    ${GTKMM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(easygamma
    ${GTKMM_LIBRARIES}
)

if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER)
    target_include_directories(easygamma PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_link_libraries(easygamma ${WAYLAND_CLIENT_LIBRARIES})
    target_compile_definitions(easygamma PRIVATE ${COMPILE_DEFS})
endif()

target_compile_options(easygamma PRIVATE ${GTKMM_CFLAGS_OTHER})

install(TARGETS easygamma DESTINATION bin)
install(FILES easygamma.desktop DESTINATION share/applications OPTIONAL)
