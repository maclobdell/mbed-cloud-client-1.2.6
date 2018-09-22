include_directories(${CMAKE_CURRENT_SOURCE_DIR}/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/extras/fixture/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../mbed-client-pal/Source/PAL-Impl/Services-API)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../mbed-trace)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../nanostack-libservice/mbed-client-libservice)

# temp workaround
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../mbed-client-pal/Source/Port/Platform-API)


set (PAL_TESTS_SOURCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../mbed-client-pal/Test/Unitest)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../mbed-client-pal/Examples/PlatformBSP/Include)
include_directories(${PAL_TESTS_SOURCE_DIR}/Includes)


FILE(
	GLOB_RECURSE  
	esfs_src
	"${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/source/*.h"
)

FILE(
	GLOB_RECURSE  
	esfs_test_src
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unitest/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unitest/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unitest/*.h"

	"${CMAKE_CURRENT_SOURCE_DIR}/Test/linux/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/linux/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/linux/*.cpp"
	
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/src/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/src/*.h"

	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/extras/fixture/src/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/Test/Unity/extras/fixture/src/*.h"

	${PAL_TESTS_SOURCE_DIR}/TestRunner/pal_test${OS_BRAND}.c 

)
message ( "********************************************************************* ")
message ("esfs_src = [[${esfs_src}]]")
message ( "********************************************************************* ")
message ("esfs_test_src = [[${esfs_test_src}]]")
message ( "********************************************************************* ")


CREATE_LIBRARY(esfs "${esfs_src}" "")


set (ESFS_TEST_FLAGS
	-DPAL_RUN_ALL_TESTS
)	


CREATE_TEST_LIBRARY(esfs_tests "${esfs_test_src}" "${ESFS_TEST_FLAGS}")
ADD_DEPENDENCIES(esfs_tests esfs)

