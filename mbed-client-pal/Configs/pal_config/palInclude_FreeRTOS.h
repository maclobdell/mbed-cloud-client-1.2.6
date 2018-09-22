/*
 * pal_mbedOS_configuration.h
 *
 *  Created on: Sep 4, 2017
 *      Author: pal
 */

#ifndef PAL_FREERTOS_CONFIGURATION_H_
/*! \brief This file sets configuration for PAL porting on FreeRTOS.
    \note All configurations that are configured in this file overwrite their defaults values
    \note Default Values can be found at Sources/PAL-impl/Services-API/pal_configuration.h
    \note
  */


//!< Number partitions on SD card used by PAL File System;
#ifndef PAL_NUMBER_OF_PARTITIONS
    #define PAL_NUMBER_OF_PARTITIONS 1
#endif

//!< Mount point for primary file system partition
#ifndef PAL_FS_MOUNT_POINT_PRIMARY
    #if (PAL_NUMBER_OF_PARTITIONS == 0)
        #define PAL_FS_MOUNT_POINT_PRIMARY    "2:"
    #elif (PAL_NUMBER_OF_PARTITIONS == 1)
        #define PAL_FS_MOUNT_POINT_PRIMARY    "0:"
    #else
        #define PAL_FS_MOUNT_POINT_PRIMARY    "0:"
    #endif
#endif

//!< Mount point for secondary file system partition
#ifndef PAL_FS_MOUNT_POINT_SECONDARY
    #if (PAL_NUMBER_OF_PARTITIONS == 0)
        #define PAL_FS_MOUNT_POINT_SECONDARY    "2:"
    #elif (PAL_NUMBER_OF_PARTITIONS == 1)
        #define PAL_FS_MOUNT_POINT_SECONDARY    "0:"
    #else
        #define PAL_FS_MOUNT_POINT_SECONDARY    "1:"
    #endif
#endif

 //!< Max number of allowed timer
#ifndef PAL_MAX_NUM_OF_TIMERS
    #define PAL_MAX_NUM_OF_TIMERS 5
#endif

//!< Max given token for a semaphore
#ifndef PAL_SEMAPHORE_MAX_COUNT
    #define PAL_SEMAPHORE_MAX_COUNT 255
#endif

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

#endif /* PAL_FREERTOS_CONFIGURATION_H_ */
