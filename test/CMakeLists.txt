cmake_minimum_required(VERSION 3.10)

project(esp32-assembler-tests)

add_library(commandsLib ../main/Commands.c)
add_library(stringUtilsLib ../main/StringUtils.c)

add_executable(commandTest CommandTest.c ../main/Commands.h)
target_link_libraries(commandTest
   commandsLib
   stringUtilsLib)