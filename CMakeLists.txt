cmake_minimum_required(VERSION 3.10)
project(labelprinter)

set(CMAKE_C_STANDARD 11)

add_executable(labelprinter)

target_sources(labelprinter PRIVATE
    src/main.c
)

target_link_libraries(labelprinter PRIVATE
    kernel32
    user32
    gdi32
    winspool
)
