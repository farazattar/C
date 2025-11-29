cmake_minimum_required(VERSION 4.1)

project(server_example_basic_io C)

set(CMKAE_C_STANDARD 99)

set(IEC61850_ROOT "C:/libiec61850-install")

set(IEC61850_INCLUDE_DIR
	${IEC61850_ROOT}/include/libiec61850
)

set(IEC61850_LIBRARY
	${IEC61850_ROOT}/lib/libiec61850.dll.a
)

set(server_example_SRCS
   server_example_basic_io.c
   static_model.c
)

include_directories(${IEC61850_INCLUDE_DIR})

IF(MSVC)
set_source_files_properties(${server_example_SRCS}
                                       PROPERTIES LANGUAGE CXX)
ENDIF(MSVC)

add_executable(server_example_basic_io
  ${server_example_SRCS}
)

target_link_libraries(server_example_basic_io
    ${IEC61850_LIBRARY}
)
