// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include "update-client-common/arm_uc_metadata_header_v2.h"

#include "update-client-common/arm_uc_utilities.h"
#include "pal.h"

#define PAL_DEVICE_KEY_SIZE 32

arm_uc_error_t arm_uc_parse_internal_header_v2(const uint8_t* input,
                                               arm_uc_firmware_details_t* details)
{
    arm_uc_error_t result = { .code = ERR_INVALID_PARAMETER };

    if (input && details)
    {
        /* calculate CRC */
        uint32_t calculatedChecksum = arm_uc_crc32(input, ARM_UC_INTERNAL_HEADER_CRC_OFFSET_V2);

        /* read out CRC */
        uint32_t temp32 = arm_uc_parse_uint32(&input[ARM_UC_INTERNAL_HEADER_CRC_OFFSET_V2]);

        if (temp32 == calculatedChecksum)
        {
            /* parse content */
            details->version = arm_uc_parse_uint64(&input[ARM_UC_INTERNAL_FIRMWARE_VERSION_OFFSET_V2]);
            details->size = arm_uc_parse_uint64(&input[ARM_UC_INTERNAL_FIRMWARE_SIZE_OFFSET_V2]);

            memcpy(details->hash,
                   &input[ARM_UC_INTERNAL_FIRMWARE_HASH_OFFSET_V2],
                   ARM_UC_SHA256_SIZE);

            memcpy(details->campaign,
                   &input[ARM_UC_INTERNAL_CAMPAIGN_OFFSET_V2],
                   ARM_UC_GUID_SIZE);

            /* set result */
            result.code = ERR_NONE;
        }
    }

    return result;
}

arm_uc_error_t arm_uc_create_internal_header_v2(const arm_uc_firmware_details_t* input,
                                                arm_uc_buffer_t* output)
{
    arm_uc_error_t result = { .code = ERR_INVALID_PARAMETER };

    if (input &&
        output &&
        (output->size_max >= ARM_UC_INTERNAL_HEADER_SIZE_V2))
    {
        /* zero buffer */
        memset(output->ptr, 0, ARM_UC_INTERNAL_HEADER_SIZE_V2);

        /* MSB encode header magic and version */
        arm_uc_write_uint32(&output->ptr[0],
                            ARM_UC_INTERNAL_HEADER_MAGIC_V2);
        arm_uc_write_uint32(&output->ptr[4],
                            ARM_UC_INTERNAL_HEADER_VERSION_V2);

        /* MSB encode firmware version */
        arm_uc_write_uint64(&output->ptr[ARM_UC_INTERNAL_FIRMWARE_VERSION_OFFSET_V2],
                            input->version);

        /* MSB encode firmware size to header */
        arm_uc_write_uint64(&output->ptr[ARM_UC_INTERNAL_FIRMWARE_SIZE_OFFSET_V2],
                            input->size);

        /* raw copy firmware hash to header */
        memcpy(&output->ptr[ARM_UC_INTERNAL_FIRMWARE_HASH_OFFSET_V2],
               input->hash,
               ARM_UC_SHA256_SIZE);

        /* raw copy campaign ID to header */
        memcpy(&output->ptr[ARM_UC_INTERNAL_CAMPAIGN_OFFSET_V2],
               input->campaign,
               ARM_UC_GUID_SIZE);

        /* calculate CRC */
        uint32_t checksum = arm_uc_crc32(output->ptr,
                                         ARM_UC_INTERNAL_HEADER_CRC_OFFSET_V2);

        /* MSB encode checksum to header */
        arm_uc_write_uint32(&output->ptr[ARM_UC_INTERNAL_HEADER_CRC_OFFSET_V2],
                            checksum);

        /* set output size */
        output->size = ARM_UC_INTERNAL_HEADER_SIZE_V2;

        /* set error code */
        result.code = ERR_NONE;
    }

    return result;
}

arm_uc_error_t arm_uc_parse_external_header_v2(const uint8_t* input,
                                               arm_uc_firmware_details_t* details)
{
    arm_uc_error_t result = { .code = ERR_INVALID_PARAMETER };

    if (input && details)
    {

        /* read 128 bit root-of-trust from PAL */
        uint8_t key[PAL_DEVICE_KEY_SIZE] = { 0 };
        palStatus_t status = pal_osGetDeviceKey(palOsStorageHmacSha256,
                                                key,
                                                PAL_DEVICE_KEY_SIZE);

        if (status == PAL_SUCCESS)
        {
            arm_uc_hash_t hmac = { 0 };
            size_t length = 0;

            /* calculate header HMAC */
            status = pal_mdHmacSha256(key, PAL_DEVICE_KEY_SIZE,
                                      input, ARM_UC_EXTERNAL_HMAC_OFFSET_V2,
                                      hmac, &length);

            if ((status == PAL_SUCCESS) && (length == ARM_UC_SHA256_SIZE))
            {
                int diff = memcmp(&input[ARM_UC_EXTERNAL_HMAC_OFFSET_V2],
                                  hmac,
                                  ARM_UC_SHA256_SIZE);

                if (diff == 0)
                {
                    details->version = arm_uc_parse_uint64(&input[ARM_UC_EXTERNAL_FIRMWARE_VERSION_OFFSET_V2]);
                    details->size = arm_uc_parse_uint64(&input[ARM_UC_EXTERNAL_FIRMWARE_SIZE_OFFSET_V2]);

                    memcpy(details->hash,
                           &input[ARM_UC_EXTERNAL_FIRMWARE_HASH_OFFSET_V2],
                           ARM_UC_SHA256_SIZE);

                    memcpy(details->campaign,
                           &input[ARM_UC_EXTERNAL_CAMPAIGN_OFFSET_V2],
                           ARM_UC_GUID_SIZE);

                    details->signatureSize = 0;

                    result.code = ERR_NONE;
                }
            }
        }
    }

    return result;
}

arm_uc_error_t arm_uc_create_external_header_v2(const arm_uc_firmware_details_t* input,
                                                arm_uc_buffer_t* output)
{
    arm_uc_error_t result = { .code = ERR_INVALID_PARAMETER };

    if (input &&
        output &&
        (output->size_max >= ARM_UC_EXTERNAL_HEADER_SIZE_V2))
    {
        /* zero buffer and reset size*/
        memset(output->ptr, 0, ARM_UC_EXTERNAL_HEADER_SIZE_V2);
        output->size = 0;

        /* MSB encode header magic and version */
        arm_uc_write_uint32(&output->ptr[0],
                            ARM_UC_EXTERNAL_HEADER_MAGIC_V2);
        arm_uc_write_uint32(&output->ptr[4],
                            ARM_UC_EXTERNAL_HEADER_VERSION_V2);

        /* MSB encode firmware version */
        arm_uc_write_uint64(&output->ptr[ARM_UC_EXTERNAL_FIRMWARE_VERSION_OFFSET_V2],
                            input->version);

        /* MSB encode firmware size to header */
        arm_uc_write_uint64(&output->ptr[ARM_UC_EXTERNAL_FIRMWARE_SIZE_OFFSET_V2],
                            input->size);

        /* raw copy firmware hash to header */
        memcpy(&output->ptr[ARM_UC_EXTERNAL_FIRMWARE_HASH_OFFSET_V2],
               input->hash,
               ARM_UC_SHA256_SIZE);

        /* MSB encode payload size to header */
        arm_uc_write_uint64(&output->ptr[ARM_UC_EXTERNAL_PAYLOAD_SIZE_OFFSET_V2],
                            input->size);

        /* raw copy payload hash to header */
        memcpy(&output->ptr[ARM_UC_EXTERNAL_PAYLOAD_HASH_OFFSET_V2],
               input->hash,
               ARM_UC_SHA256_SIZE);

        /* raw copy campaign ID to header */
        memcpy(&output->ptr[ARM_UC_EXTERNAL_CAMPAIGN_OFFSET_V2],
               input->campaign,
               ARM_UC_GUID_SIZE);

        /* read 128 bit root-of-trust from PAL */
        uint8_t key[PAL_DEVICE_KEY_SIZE] = { 0 };
        palStatus_t status = pal_osGetDeviceKey(palOsStorageHmacSha256,
                                                key,
                                                PAL_DEVICE_KEY_SIZE);

        if (status == PAL_SUCCESS)
        {
            /* calculate header HMAC */
            size_t length = 0;
            status = pal_mdHmacSha256(key, PAL_DEVICE_KEY_SIZE,
                                      output->ptr, ARM_UC_EXTERNAL_HMAC_OFFSET_V2,
                                      &output->ptr[ARM_UC_EXTERNAL_HMAC_OFFSET_V2],
                                      &length);

            /* set buffer size and return code upon success */
            if ((status == PAL_SUCCESS) && (length == ARM_UC_SHA256_SIZE))
            {
                output->size = ARM_UC_EXTERNAL_HEADER_SIZE_V2;

                result.code = ERR_NONE;
            }
        }
    }

    return result;
}
