/*
 * pal_mbedOS_configuration.h
 *
 *  Created on: Sep 4, 2017
 *      Author: pal
 */

#ifndef PAL_MBEDOS_CONFIGURATION_H_
/*! \brief This file sets configuration for PAL porting on mbedOS.
    \note All configurations that are configured in this file overwrite their defaults values
    \note Default Values can be found at Sources/PAL-impl/Services-API/pal_configuration.h
    \note
  */

#include "cmsis_os.h"

//!< Number partitions on SD card used by PAL File System
#ifndef PAL_NUMBER_OF_PARTITIONS
    #define PAL_NUMBER_OF_PARTITIONS 1
#endif

//!< Mount point for primary file system partition
#ifndef PAL_FS_MOUNT_POINT_PRIMARY
    #if (PAL_NUMBER_OF_PARTITIONS == 2)
        #define PAL_FS_MOUNT_POINT_PRIMARY    "/sd"                                                     //!< User should change this for the his working folder
    #else
        #define PAL_FS_MOUNT_POINT_PRIMARY    "/sd"
    #endif
#endif

//!< Mount point for secondary file system partition
#ifndef PAL_FS_MOUNT_POINT_SECONDARY
    #if (PAL_NUMBER_OF_PARTITIONS == 2)
        #define PAL_FS_MOUNT_POINT_SECONDARY    "/sd2"
    #else
        #define PAL_FS_MOUNT_POINT_SECONDARY    "/sd"                                                  //!< User should change this for the his working folder
    #endif
#endif

 //!< Change the default value to CMSIS wait forever
#ifndef PAL_RTOS_WAIT_FOREVER
    #define PAL_RTOS_WAIT_FOREVER osWaitForever
#endif

#if defined(TARGET_K64F)
 /*\brief  Starting Address for section 1 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_1_ADDRESS    0xFE000
#endif

/*\brief  Starting Address for section 2 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_2_ADDRESS    0xFF000
#endif

/*\brief  Size for section 1*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_1_SIZE       0x1000
#endif

/*\brief  Size for section 2*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_2_SIZE       0x1000
#endif

#else// defined(TARGET_K64F)

/*\brief  Starting Address for section 1 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_1_ADDRESS    0x080C0000
#endif

/*\brief  Starting Address for section 2 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_2_ADDRESS    0x080E0000
#endif

/*\brief  Size for section 1*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_1_SIZE       0x20000
#endif

/*\brief  Size for section 2*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_2_SIZE       0x20000
#endif

#endif// defined(TARGET_K64F)

#ifndef PAL_NUM_OF_THREAD_INSTANCES
    #define PAL_NUM_OF_THREAD_INSTANCES 1
#endif

//!< Max given token for a semaphore
#ifndef PAL_MAX_SEMAPHORE_COUNT
    #define PAL_MAX_SEMAPHORE_COUNT 1024
#endif


#endif /* PAL_MBEDOS_CONFIGURATION_H_ */
