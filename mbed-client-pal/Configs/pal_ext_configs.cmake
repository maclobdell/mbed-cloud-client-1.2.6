SET(PAL_BSP_DIR ${NEW_CMAKE_SOURCE_DIR}/mbed-client-pal/Configs/)
SET(PAL_TLS_BSP_DIR ${PAL_BSP_DIR}/${TLS_LIBRARY})
SET(PAL_PLATFORM_BSP_DIR ${PAL_BSP_DIR}/pal_config)


if (${TLS_LIBRARY} MATCHES mbedTLS)
	# PAL specific configurations for mbedTLS
    if (NOT (${OS_BRAND} MATCHES "FreeRTOS"))
	    add_definitions(-DMBEDTLS_CONFIG_FILE="\\"${PAL_TLS_BSP_DIR}/mbedTLSConfig_${OS_BRAND}.h"\\")
    else()
        add_definitions(-DMBEDTLS_CONFIG_FILE=\"${PAL_TLS_BSP_DIR}/mbedTLSConfig_${OS_BRAND}.h\")
    endif()
    message("PAL_TLS_BSP_DIR ${PAL_TLS_BSP_DIR}/pal_${OS_BRAND}.h")
endif()



