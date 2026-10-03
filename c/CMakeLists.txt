cmake_minimum_required(VERSION 3.16)
project(aura_parkour_viewport C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PARKOUR_COMMON
    term.c
    clock.c
    sample.c
    step.c
    render.c
    glide.c)

add_executable(parkour_blit ${PARKOUR_COMMON} main.c)
target_compile_options(parkour_blit PRIVATE -O2 -Wall -Wextra -pedantic)
target_link_libraries(parkour_blit PRIVATE m)

add_executable(parkour_play ${PARKOUR_COMMON} play.c)
target_compile_options(parkour_play PRIVATE -O2 -Wall -Wextra -pedantic)
target_link_libraries(parkour_play PRIVATE m)

# Windowed corridor. Optional: headless hosts without raylib keep parkour_play.
find_package(raylib QUIET)
if(raylib_FOUND)
  add_executable(parkour_view view3d.c glide.c clock.c sample.c)
  target_compile_options(parkour_view PRIVATE -O2 -Wall -Wextra -pedantic)
  target_link_libraries(parkour_view PRIVATE raylib m)
  message(STATUS "parkour_view: raylib window enabled")
else()
  message(STATUS "parkour_view: raylib not found, ANSI parkour_play only")
endif()
