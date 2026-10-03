cmake_minimum_required(VERSION 3.16)
project(aura_parkour_viewport C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_executable(parkour_blit
    term.c
    clock.c
    sample.c
    step.c
    render.c
    main.c)
target_compile_options(parkour_blit PRIVATE -O2 -Wall -Wextra -pedantic)
