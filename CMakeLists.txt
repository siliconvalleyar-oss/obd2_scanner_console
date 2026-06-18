cmake_minimum_required(VERSION 3.14)
project(elm327_console
    VERSION 1.0.0
    DESCRIPTION "Escáner OBD-II profesional por consola"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BLUEZ bluez)
endif()

if(NOT BLUEZ_FOUND)
    find_library(BLUEZ_LIBRARY bluetooth)
    if(BLUEZ_LIBRARY)
        set(BLUEZ_FOUND TRUE)
        set(BLUEZ_LIBRARIES ${BLUEZ_LIBRARY})
        message(STATUS "Bluetooth (libbluetooth) encontrado via find_library: ${BLUEZ_LIBRARY}")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/elm327.cpp
    src/console_app.cpp
    src/logger.cpp
)

set(HEADERS
    include/elm327.hpp
    include/console_app.hpp
    include/logger.hpp
)

add_executable(elm327_console ${SOURCES} ${HEADERS})

target_include_directories(elm327_console PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BLUEZ_FOUND)
    target_link_libraries(elm327_console PRIVATE ${BLUEZ_LIBRARIES})
    target_include_directories(elm327_console PRIVATE ${BLUEZ_INCLUDE_DIRS})
    message(STATUS "Bluetooth detectado: si")
else()
    message(WARNING "Bluetooth NO detectado - la conexion no estara disponible")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(elm327_console PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

add_custom_target(run
    COMMAND ./elm327_console
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS elm327_console
)

install(TARGETS elm327_console DESTINATION bin)
