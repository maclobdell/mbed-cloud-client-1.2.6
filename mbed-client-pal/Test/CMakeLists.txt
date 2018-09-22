include_directories(../Source/PAL-Impl/Services-API) 
include_directories(../Source/Port/Platform-API)
option(SPLIT_BINARIES "Choose whether to split the tests into 2 binaries or not" OFF)

if (${OS_BRAND} MATCHES FreeRTOS)
	add_definitions(-DUNITY_OUTPUT_CHAR=unity_output_char)
endif()

#on Linux, we might find different names for the network interface
#so we have to find it, and pass to the code.
if (${OS_BRAND} MATCHES Linux)
  add_definitions(-DPAL_NO_FATFS_SD_TEST=1)
  if (NOT USE_ETH_INTERFACE)
    execute_process(COMMAND ip -o -4 route show to default 
	                  COMMAND awk "{print $5}" 
			  COMMAND head -1 
	                  OUTPUT_STRIP_TRAILING_WHITESPACE
	                  OUTPUT_VARIABLE  ETHNAME)
    set(TMPD \"${ETHNAME}\")
    add_definitions(-DPAL_LINUX_ETH=${TMPD})
    message( "ETHNAME = ${ETHNAME}")
  else()
    add_definitions(-DPAL_LINUX_ETH=${USE_ETH_INTERFACE})
    message("Using ${USE_ETH_INTERFACE} for network")
  endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
	add_definitions(-DDEBUG)
endif()

include_directories(Unitest)
include_directories(Common)  
#include_directories(Unitest/Networking) 
include_directories(Unity/src) 
include_directories(Unity/extras/fixture/src)


set (PAL_TESTS_SOURCE_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/Unitest/)
set (PAL_UNITY_SOURCE_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/Unity/src/)
set (PAL_UNITY_FIXTURE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/Unity/extras/fixture/src)
set (PAL_TEST_BSP_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../Examples/PlatformBSP)
set (PAL_UTILS_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/../Utils/memoryProfiler/Other)

set(PAL_TEST_RTOS_SRCS 
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/RTOS/pal_rtos_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/RTOS/pal_rtos_test.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/RTOS/pal_rtos_test_utils.c
)

set(PAL_TEST_NETWORK_SRCS 
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Networking/pal_socket_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Networking/pal_socket_test.c
)

set(PAL_TEST_FS_SRCS 
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/FileSystem/pal_fileSystem_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/FileSystem/pal_fileSystem_test.c
)

set(PAL_TEST_TLS_SRCS 
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/TLS/pal_tls_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/TLS/pal_tls_test.c
)

set(PAL_TEST_CRYPTO_SRCS 
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Crypto/pal_crypto_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Crypto/pal_crypto_test.c
)

set(PAL_TEST_UPDATE_SRCS
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Update/pal_update_test_runner.c
	${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Update/pal_update_test.c
)

set(PAL_TEST_FLASH_SRCS
    ${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Storage/pal_internalFlash_test_runner.c
    ${PAL_TESTS_SOURCE_DIR}/PAL_Modules/Storage/pal_internalFlash_test.c
)

set(PAL_TEST_COMMON_SRCS
	${PAL_TESTS_SOURCE_DIR}/TestRunner/test_Runner.c
	${PAL_TESTS_SOURCE_DIR}/pal_test_main.c
	${PAL_UNITY_SOURCE_DIR}unity.c
	${PAL_UNITY_FIXTURE_DIR}/unity_fixture.c
	${PAL_TESTS_SOURCE_DIR}/TestRunner/pal_test${OS_BRAND}.c 
)

set(PAL_TEST_BSP_SRCS
    ${PAL_TEST_BSP_DIR}/${PAL_TARGET_DEVICE}_${OS_BRAND}/BoardInit.c
    ${PAL_TEST_BSP_DIR}/${PAL_TARGET_DEVICE}_${OS_BRAND}/FileSystemInit.c
    ${PAL_TEST_BSP_DIR}/${PAL_TARGET_DEVICE}_${OS_BRAND}/NetworkInit.c
	${PAL_TEST_BSP_DIR}/pal_insecure_ROT.c
)

set(PAL_TEST_FAKE_ROT_SRCS
	${PAL_TEST_BSP_DIR}/pal_insecure_ROT.c
)


if (PAL_MEMORY_STATISTICS) #currently working only in gcc based compilers
    list (APPEND PAL_TEST_BSP_SRCS  ${PAL_UTILS_DIR}/pal_memory.c)
    add_definitions(-DPAL_MEMORY_STATISTICS=1)
endif() 

include_directories(${PAL_TEST_BSP_DIR}/Include/)
include_directories(${PAL_TESTS_SOURCE_DIR}/Includes/)

set(test_src ${PAL_TEST_BSP_SRCS}; ${PAL_TEST_COMMON_SRCS}; ${PAL_TEST_NETWORK_SRCS}; ${PAL_TEST_TLS_SRCS}; ${PAL_TEST_CRYPTO_SRCS}; ${PAL_TEST_RTOS_SRCS}; ${PAL_TEST_FLASH_SRCS}; ${PAL_TEST_FS_SRCS}; ${PAL_TEST_UPDATE_SRCS}; ${PAL_TEST_FAKE_ROT_SRCS} )
set (PAL_TEST_FLAGS
	-DPAL_TEST_RTOS
	-DPAL_TEST_FS
	-DPAL_TEST_UPDATE
	-DPAL_TEST_NETWORK
	-DPAL_TEST_TLS
	-DPAL_TEST_CRYPTO
	-DPAL_TEST_FLASH
)
	
CREATE_TEST_LIBRARY(palTests "${test_src}" "${PAL_TEST_FLAGS}")


CREATE_LIBRARY(palBringup "${PAL_TEST_BSP_SRCS}" "")

