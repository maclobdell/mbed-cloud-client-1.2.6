// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
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

#include <string.h>
#include "fcc_sotp.h"
#include "pv_error_handling.h"


static bool get_sotp_type(fcc_sotp_type_e sotp_type, size_t *required_size_out)
{
    size_t required_size;

    switch (sotp_type) {
        case FCC_SOTP_TYPE_ROT:
            required_size = FCC_ROT_SIZE;
            break;
        case FCC_SOTP_TYPE_FACTORY_DISABLE:
            required_size = FCC_FACTORY_DISABLE_FLAG_SIZE;
            break;
        case FCC_SOTP_TYPE_ENTROPY:
            required_size = FCC_ENTROPY_SIZE;
            break;
        default:
            SA_PV_LOG_ERR("Non existant sotp_type provided");
            return false;
    }

    // Success
    *required_size_out = required_size;

    return true;
}

fcc_status_e fcc_sotp_data_store(const uint8_t *data, size_t data_size, fcc_sotp_type_e sotp_type)
{
    bool success;
    size_t required_size = 0;
    int64_t aligned_8_bytes_buffer[MAX_SOTP_BUFFER_SIZE / 8];

    SA_PV_LOG_INFO_FUNC_ENTER("data_size = %" PRIu32 " sotp_type = %d", (uint32_t)data_size, (int)sotp_type);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((data == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid param data");

    success = get_sotp_type(sotp_type, &required_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!success), FCC_STATUS_ERROR, "Failed for get_sotp_type()");

    // Assert that buffer provided is of correct size
    SA_PV_ERR_RECOVERABLE_RETURN_IF((data_size != required_size), FCC_STATUS_ERROR, "Wrong buf_size provided. Must be size of exactly %" PRIu32 " bytes", (uint32_t)required_size);

    // Write buf to SOTP. Cast is OK since size must be divisible by 8
    
    /*
    * Copy from data (uint8_t*) to aligned_8_bytes_buffer (uint64_t*) to make sure that data is 8 byte aligned.
    * Since SOTP_Set() gets a pointer to int64_t, if it is not aligned, and we just cast it to uint8_t*,
    * ARMCC functions like memcpy will assume 8 byte alignment resulting in possible access of unallocated memory. 
    */
    memcpy(aligned_8_bytes_buffer, data, data_size);

    success = SOTP_Set((uint8_t)sotp_type, (data_size >> 3), aligned_8_bytes_buffer);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    // FIXME: When SOTP returns real status - we must translate the error
    return success ? FCC_STATUS_SUCCESS : FCC_STATUS_ERROR;
}

fcc_status_e fcc_sotp_data_retrieve(uint8_t *data_out, size_t data_size_max, size_t *data_actual_size_out, fcc_sotp_type_e sotp_type)
{
    bool success;
    size_t required_size = 0;
    int64_t aligned_8_bytes_buffer[MAX_SOTP_BUFFER_SIZE / 8];

    uint8_t data_size_as_array_of_int64; /* store as array of int64_t */

    SA_PV_LOG_INFO_FUNC_ENTER("data_out = %" PRIu32 " sotp_type = %d", (uint32_t)data_size_max, (int)sotp_type);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((data_out == NULL), FCC_STATUS_INVALID_PARAMETER, "invalid param data_out");

    success = get_sotp_type(sotp_type, &required_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!success), FCC_STATUS_ERROR, "Failed for get_sotp_type()");

    // Assert that buffer provided is of correct size
    SA_PV_ERR_RECOVERABLE_RETURN_IF((data_size_max < required_size), FCC_STATUS_ERROR, "Wrong data_size provided. Must be size of exactly %" PRIu32 " bytes", (uint32_t)required_size);

    // Retrieve buf from SOTP. Cast is OK since size must be multiple of 8
    // FIXME: When SOTP returns real status - we must translate the error
    success = SOTP_Get((uint8_t)sotp_type, aligned_8_bytes_buffer, &data_size_as_array_of_int64);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!success), FCC_STATUS_ERROR, "SOTP_Get failed");

    // Copy from aligned buffer to callers uint8_t* buffer
    memcpy(data_out, aligned_8_bytes_buffer, data_size_as_array_of_int64 * sizeof(int64_t));

    // Convert back to bytes
    *data_actual_size_out = (size_t)(data_size_as_array_of_int64 << 3);
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    // FIXME: When SOTP returns real status - we must translate the error
    return FCC_STATUS_SUCCESS;
}







////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FIXME: All code from here on should be removed once SOTP APIs are implemented
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
* These globals mock the SOTP storage.
* Should be removed once SOTP is implemented.
*/
sotp_entree_s g_sotp_rot = { 0 };
sotp_entree_s g_sotp_factory_disabled = { 0 };
sotp_entree_s g_sotp_entropy = { 0 };

// API that mocks future SOTP_Set()
bool SOTP_Set(uint8_t type, uint8_t size, const int64_t *data)
{
    sotp_entree_s entree;

    entree.data_size_in_bytes = (size << 3);
    memcpy(entree.data, data, entree.data_size_in_bytes);
    entree.type = type;
    entree.write_disabled = true;

    switch (type) {
        case FCC_SOTP_TYPE_ENTROPY:
            if (!g_sotp_entropy.write_disabled) {
                g_sotp_entropy = entree;
            } else {
                return false;
            }
            break;
        case FCC_SOTP_TYPE_ROT:
            if (!g_sotp_rot.write_disabled) {
                g_sotp_rot = entree;
            } else {
                return false;
            }
            break;
        case FCC_SOTP_TYPE_FACTORY_DISABLE:
            if (!g_sotp_factory_disabled.write_disabled) {
                g_sotp_factory_disabled = entree;
            } else {
                return false;
            }
            break;
        default:
            return false;
    }

    return true;
}

// API that mocks future SOTP_Get()
bool SOTP_Get(uint8_t type, int64_t *data_out, uint8_t *data_size_out)
{
    switch (type) {
        case FCC_SOTP_TYPE_ENTROPY:
            *data_size_out = g_sotp_entropy.data_size_in_bytes >> 3;
            memcpy(data_out, g_sotp_entropy.data, g_sotp_entropy.data_size_in_bytes);
            break;
        case FCC_SOTP_TYPE_ROT:
            *data_size_out = g_sotp_rot.data_size_in_bytes >> 3;
            memcpy(data_out, g_sotp_rot.data, g_sotp_rot.data_size_in_bytes);
            break;
        case FCC_SOTP_TYPE_FACTORY_DISABLE:
            *data_size_out = g_sotp_factory_disabled.data_size_in_bytes >> 3;
            memcpy(data_out, g_sotp_factory_disabled.data, g_sotp_factory_disabled.data_size_in_bytes);
            break;
        default:
            return false;
    }

    return true;
}

void SOTP_TestOnly_reset()
{
    memset(&g_sotp_entropy, 0, sizeof(g_sotp_entropy));
    memset(&g_sotp_rot, 0, sizeof(g_sotp_rot));
    memset(&g_sotp_factory_disabled, 0, sizeof(g_sotp_factory_disabled));
}
