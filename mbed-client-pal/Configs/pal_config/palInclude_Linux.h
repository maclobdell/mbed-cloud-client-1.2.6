/*
 * pal_mbedOS_configuration.h
 *
 *  Created on: Sep 4, 2017
 *      Author: pal
 */
#ifndef PAL_MBEDOS_CONFIGURATION_H_
/*! \brief This file sets configuration for PAL porting on Linux.
    \note All configurations that are configured in this file overwrite their defaults values
    \note Default Values can be found at Sources/PAL-impl/Services-API/pal_configuration.h
    \note
  */

//!< Number partitions on SD card used by PAL File System
#ifndef PAL_NUMBER_OF_PARTITIONS
    #define PAL_NUMBER_OF_PARTITIONS 1
#endif

//!< User should change this for the his working folder
#ifndef PAL_FS_MOUNT_POINT_PRIMARY
    #if (PAL_NUMBER_OF_PARTITIONS == 2)
        #define PAL_FS_MOUNT_POINT_PRIMARY    "./pal_pri"
    #else
        #define PAL_FS_MOUNT_POINT_PRIMARY    "./pal"
    #endif
#endif

 //!< User should change this for the his working folder
#ifndef PAL_FS_MOUNT_POINT_SECONDARY
    #if (PAL_NUMBER_OF_PARTITIONS == 2)
        #define PAL_FS_MOUNT_POINT_SECONDARY    "./pal_sec"
    #else
        #define PAL_FS_MOUNT_POINT_SECONDARY    "./pal"
    #endif
#endif

#ifndef PAL_NET_MAX_IF_NAME_LENGTH
    #define PAL_NET_MAX_IF_NAME_LENGTH   16  //15 + '\0'
#endif

#ifndef PAL_NET_TEST_MAX_ASYNC_SOCKETS
    #define PAL_NET_TEST_MAX_ASYNC_SOCKETS 5
#endif

#ifndef PAL_NET_TEST_ASYNC_SOCKET_MANAGER_THREAD_STACK_SIZE
    #define PAL_NET_TEST_ASYNC_SOCKET_MANAGER_THREAD_STACK_SIZE (1024*4)
#endif

#ifndef PAL_RTOS_HIGH_RES_TIMER_THREAD_STACK_SIZE
    #define PAL_RTOS_HIGH_RES_TIMER_THREAD_STACK_SIZE (4096*4)
#endif

#ifndef PAL_FORMAT_CMD_MAX_LENGTH
    #define PAL_FORMAT_CMD_MAX_LENGTH 256
#endif

#ifndef PAL_DEVICE_NAME_MAX_LENGTH
    #define PAL_DEVICE_NAME_MAX_LENGTH  128
#endif

#ifndef PAL_PARTITION_FORMAT_TYPE
    #define PAL_PARTITION_FORMAT_TYPE "ext4"
#endif

/*\brief  overwrite format command with remove all file and directory*/
#ifndef PAL_FS_RM_INSTEAD_OF_FORMAT
    #define PAL_FS_RM_INSTEAD_OF_FORMAT 0
#endif

#ifndef PAL_FS_FORMAT_COMMAND
    #define PAL_FS_FORMAT_COMMAND "mkfs -F -t %s %s"
#endif


#ifndef PARTITION_FORMAT_ADDITIONAL_PARAMS
    #define PARTITION_FORMAT_ADDITIONAL_PARAMS NULL
#endif

 /*\brief  Starting Address for section 1 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_1_ADDRESS    0
#endif

/*\brief  Starting Address for section 2 Minimum requirement size is 1KB and section must be consecutive sectors*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_ADDRESS
    #define PAL_INTERNAL_FLASH_SECTION_2_ADDRESS    0
#endif

/*\brief  Size for section 1*/
#ifndef PAL_INTERNAL_FLASH_SECTION_1_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_1_SIZE       0
#endif

/*\brief  Size for section 2*/
#ifndef PAL_INTERNAL_FLASH_SECTION_2_SIZE
    #define PAL_INTERNAL_FLASH_SECTION_2_SIZE       0
#endif


#endif /* PAL_MBEDOS_CONFIGURATION_H_ */
