

SET(MODULES
	ftcd-comm-base
	ftcd-comm-socket
	crypto-service		
	key-config-manager
	factory-configurator-client
	fcc-bundle-handler	
	secsrv-cbor
	logger
	storage
	utils
	mbed-trace-helper
	fcc-output-info-handler
)


# includes
FOREACH(module ${MODULES})
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/${module}/${module})
ENDFOREACH()

# factory-configurator-client internal includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/crypto/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/crypto-service/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/key-config-manager/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mbed-client-esfs/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/factory-configurator-client/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/logger/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/fcc-bundle-handler/source/include)

# mbed-client-pal
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mbed-client-pal/Source/PAL-Impl/Services-API)

# mbed-trace
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mbed-trace)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mbed-trace/mbed-trace)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/nanostack-libservice/mbed-client-libservice)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/logger/logger)