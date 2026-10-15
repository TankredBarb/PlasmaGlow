cmake_minimum_required(VERSION 3.19)

project(plasmaglow_lite LANGUAGES C)

set(CMAKE_AUTOMOC OFF)
include(GNUInstallDirs)
find_package(X11 REQUIRED)
find_library(XNVCTRL_LIBRARY NAMES XNVCtrl libXNVCtrl.so.0 REQUIRED)

add_executable(plasmaglow-lite plasmaglow-lite.c)
add_executable(vibrance vibrance.c)
foreach(target IN ITEMS plasmaglow-lite vibrance)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_include_directories(${target} PRIVATE include)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${target} PRIVATE X11::X11 ${XNVCTRL_LIBRARY} m)
endforeach()

install(TARGETS plasmaglow-lite vibrance RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES plasmaglow-lite.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
