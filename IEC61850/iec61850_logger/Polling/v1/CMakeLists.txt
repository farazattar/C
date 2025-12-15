cmake_minimum_required(VERSION 4.1)
project(iec61850_logger C)

set(CMAKE_C_STANDARD 99)

set(IEC61850_ROOT "C:/libiec61850-install")

set(IEC61850_INCLUDE_DIR
	${IEC61850_ROOT}/include/libiec61850
)

set(IEC61850_LIBRARY
	${IEC61850_ROOT}/lib/libiec61850.dll.a
)

set(MyProgram
   main.c
)

include_directories(${IEC61850_INCLUDE_DIR})

add_executable(iec61850_logger
    ${MyProgram}
)

target_link_libraries(iec61850_logger
    ${IEC61850_LIBRARY}
)
