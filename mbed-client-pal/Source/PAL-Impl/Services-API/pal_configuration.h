/*
* Copyright (c) 2016 ARM Limited. All rights reserved.
* SPDX-License-Identifier: Apache-2.0
* Licensed under the Apache License, Version 2.0 (the License); you may
* not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an AS IS BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


#ifndef _PAL_COFIGURATION_H
#define _PAL_COFIGURATION_H
#include "limits.h"


#ifdef PAL_USER_DEFINED_CONFIGURATION
    #include PAL_USER_DEFINED_CONFIGURATION
#endif

/*! \brief let the user choose its platform configuration file.
    \note if the user does not specify a platform configuration file,
    \note PAL uses a default configuration set that can be found at \b Configs/pal_config folder
  */

#ifdef PAL_PLATFORM_DEFINED_CONFIGURATION
    #include PAL_PLATFORM_DEFINED_CONFIGURATION
#elif defined(__LINUX__)
    #include "palInclude_Linux.h"
#elif defined(__FREERTOS__)
    #include "palInclude_FreeRTOS.h"
#elif defined(__MBED__)
    #include "palInclude_mbedOS.h"
#else
    #error "Please specify the platform PAL_PLATFORM_DEFINED_CONFIGURATION"
#endif

/*! \file pal_configuration.h
*  \brief PAL Configuration.
*   This file contains PAL configuration information including the following:
*       1. The flags to enable or disable features.
*       2. The configuration of the number of objects provided by PAL (such as the number of threads supported) or their sizes.
*       3. The configuration of supported cipher suites.
*       4. The configuration for flash memory usage.
*       5. The configuration for the root of trust.
*/


/*
 * Network configuration
 */
//! PAL configuration options
#ifndef PAL_NET_TCP_AND_TLS_SUPPORT
    #define PAL_NET_TCP_AND_TLS_SUPPORT         true/* Add PAL support for TCP. */
#endif

#ifndef PAL_NET_ASYNCHRONOUS_SOCKET_API
    #define PAL_NET_ASYNCHRONOUS_SOCKET_API     true/* Add PAL support for asynchronous sockets. */
#endif

#ifndef PAL_NET_DNS_SUPPORT
    #define PAL_NET_DNS_SUPPORT                 true/* Add PAL support for DNS lookup. */
#endif

//values for PAL_NET_DNS_IP_SUPPORT
#define PAL_NET_DNS_ANY          0    /* if PAL_NET_DNS_IP_SUPPORT is set to PAL_NET_DNS_ANY pal_getAddressInfo will return the first available IPV4 or IPV6 address*/
#define PAL_NET_DNS_IPV4_ONLY    2    /* if PAL_NET_DNS_IP_SUPPORT is set to PAL_NET_DNS_IPV4_ONLY pal_getAddressInfo will return the first available IPV4 address*/
#define PAL_NET_DNS_IPV6_ONLY    4    /* if PAL_NET_DNS_IP_SUPPORT is set to PAL_NET_DNS_IPV6_ONLY pal_getAddressInfo will return the first available IPV6 address*/

#ifndef PAL_NET_DNS_IP_SUPPORT
    #define PAL_NET_DNS_IP_SUPPORT  0 /* sets the type of IP addresses returned by  pal_getAddressInfo*/
#endif

//! The maximum number of interfaces that can be supported at a time.
#ifndef PAL_MAX_SUPORTED_NET_INTERFACES
    #define PAL_MAX_SUPORTED_NET_INTERFACES 10
#endif

/*
 * RTOS configuration
 */

#ifndef PAL_IGNORE_UNIQUE_THREAD_PRIORITY
	#define PAL_UNIQUE_THREAD_PRIORITY true
#endif

//! The number of valid priorities limits the number of threads. If priorities are added this value should be increased.
#ifndef PAL_MAX_NUMBER_OF_THREADS
    #define PAL_MAX_NUMBER_OF_THREADS 7
#endif

//! initial time until thread stack cleanup (mbedOs only). This is the amount of time we wait before checking that a thread has completed so we can free it's stack.
#ifndef PAL_RTOS_THREAD_CLEANUP_TIMER_MILISEC
    #define PAL_RTOS_THREAD_CLEANUP_TIMER_MILISEC 200
#endif

//! This define is used to determine the size of the initial random buffer (in bytes) held by PAL for random the algorithm.
#ifndef PAL_INITIAL_RANDOM_SIZE
    #define PAL_INITIAL_RANDOM_SIZE 48
#endif

#ifndef PAL_RTOS_WAIT_FOREVER
    #define PAL_RTOS_WAIT_FOREVER UINT_MAX
#endif

/*
 * TLS configuration
 */
//! The the maximum number of TLS contexts supported.
#ifndef PAL_MAX_NUM_OF_TLS_CTX
    #define PAL_MAX_NUM_OF_TLS_CTX 1
#endif

//! The maximum number of supported cipher suites.
#ifndef PAL_MAX_ALLOWED_CIPHER_SUITES
    #define PAL_MAX_ALLOWED_CIPHER_SUITES 1
#endif

//! This value is in milliseconds. 1000 = 1 second.
#ifndef PAL_DTLS_PEER_MIN_TIMEOUT
    #define PAL_DTLS_PEER_MIN_TIMEOUT 1000
#endif

//! The debug threshold for TLS API.
#ifndef PAL_TLS_DEBUG_THRESHOLD
    #define PAL_TLS_DEBUG_THRESHOLD 5
#endif

//! Define the cipher suites for TLS (only one cipher suite per device available).
#define PAL_TLS_PSK_WITH_AES_128_CBC_SHA256_SUITE           0x01
#define PAL_TLS_PSK_WITH_AES_128_CCM_8_SUITE                0x02
#define PAL_TLS_PSK_WITH_AES_256_CCM_8_SUITE                0x04
#define PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8_SUITE        0x08
#define PAL_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256_SUITE   0x10
#define PAL_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384_SUITE   0x20

//! Use the default cipher suite for TLS/DTLS operations
#define PAL_TLS_CIPHER_SUITE PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8_SUITE

/*
 * UPDATE configuration
 */

#define PAL_UPDATE_USE_FLASH 1
#define PAL_UPDATE_USE_FS    2

#ifndef PAL_UPDATE_IMAGE_LOCATION
	#define PAL_UPDATE_IMAGE_LOCATION PAL_UPDATE_USE_FS     //!< Choose the storage correct Storage option, File System or Flash
#endif

//! Certificate date validation in Unix time format.
#ifndef PAL_CRYPTO_CERT_DATE_LENGTH
    #define PAL_CRYPTO_CERT_DATE_LENGTH sizeof(uint64_t)
#endif


/*
 * FS configuration
 */


/* !\brief file system configurations
 * PAL_NUMBER_OF_PARTITIONS
 * 0 - Default behavior for the platform (Described by either 1 or 2 below).
 * 1 - There is a single partition in which the ARM client applications create and remove files (but do not format it).
 * 2 - There are two partitions in which ARM client applications may format or create and remove files,
 *     depending on PAL_PRIMARY_PARTITION_PRIVATE and PAL_SECONDARY_PARTITION_PRIVATE
 */
#ifndef PAL_NUMBER_OF_PARTITIONS
    #define PAL_NUMBER_OF_PARTITIONS 1 // Default partitions
#endif

#if (PAL_NUMBER_OF_PARTITIONS > 2)
#error "PAL_NUMBER_OF_PARTITIONS cannot be more then 2"
#endif

// PAL_PRIMARY_PARTITION_PRIVATE
// 1 if the primary partition is exclusively dedicated to the ARM client applications.
// 0 if the primary partition is used for storing other files as well.
#ifndef PAL_PRIMARY_PARTITION_PRIVATE
    #define PAL_PRIMARY_PARTITION_PRIVATE 0
#endif

//! PAL_SECONDARY_PARTITION_PRIVATE
//! 1 if the secondary partition is exclusively dedicated to the ARM client applications.
//! 0 if the secondary partition is used for storing other files as well.
#ifndef PAL_SECONDARY_PARTITION_PRIVATE
    #define PAL_SECONDARY_PARTITION_PRIVATE 0
#endif

//! This define is the location of the primary mount point for the file system
#ifndef PAL_FS_MOUNT_POINT_PRIMARY
    #define PAL_FS_MOUNT_POINT_PRIMARY  ""
#endif

//! This define is the location of the secondary mount point for the file system
#ifndef PAL_FS_MOUNT_POINT_SECONDARY
    #define PAL_FS_MOUNT_POINT_SECONDARY ""
#endif


// Update

#ifndef PAL_UPDATE_FIRMWARE_MOUNT_POINT
    #define PAL_UPDATE_FIRMWARE_MOUNT_POINT PAL_FS_MOUNT_POINT_PRIMARY
#endif
//! The location of the firmware update folder
#ifndef PAL_UPDATE_FIRMWARE_DIR
    #define PAL_UPDATE_FIRMWARE_DIR PAL_UPDATE_FIRMWARE_MOUNT_POINT "/firmware"
#endif

#endif //_PAL_COFIGURATION_H
