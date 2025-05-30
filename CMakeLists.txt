cmake_minimum_required(VERSION 3.13)

project(6502Emu)
set(CMAKE_CXX_STANDARD 14)

set(SRC_FILES
	src/main.cpp
	src/rampage.cpp
)

add_executable(main ${SRC_FILES})