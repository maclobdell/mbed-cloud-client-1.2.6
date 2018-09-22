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
#include "pal.h"
#include "pal_plat_internalFlash.h"

#define BITS_ALIGNED_TO_32 	0x3
#define PAL_MAX_PAGE_SIZE   16

PAL_PRIVATE palMutexID_t g_flashMutex = NULLPTR;


size_t pal_internalFlashGetPageSize(void)
{
	size_t ret = pal_plat_internalFlashGetPageSize();
	if(ret > PAL_MAX_PAGE_SIZE)
	{
	    ret = PAL_MAX_PAGE_SIZE;
	}
	return ret;
}

size_t pal_internalFlashGetSectorSize(uint32_t address)
{
	size_t ret = pal_plat_internalFlashGetSectorSize(address);
	return ret;
}


palStatus_t pal_internalFlashInit(void)
{
	palStatus_t ret = PAL_SUCCESS;
    if(NULLPTR == g_flashMutex)
    {
        ret = pal_osMutexCreate(&g_flashMutex);
    }

    if(PAL_SUCCESS == ret)
    {
        ret = pal_plat_internalFlashInit();
    }
	return ret;
}

palStatus_t pal_internalFlashDeInit(void)
{
	palStatus_t ret = PAL_SUCCESS;
    if(NULLPTR != g_flashMutex)
    {
        ret = pal_osMutexDelete(&g_flashMutex);
        g_flashMutex = NULLPTR;
    }

    if(PAL_SUCCESS == ret)
    {
        ret = pal_plat_internalFlashDeInit();
    }
	return ret;
}

palStatus_t pal_internalFlashWrite(const size_t size, const uint32_t address, const uint32_t * buffer)
{
	palStatus_t ret = PAL_SUCCESS;
	uint32_t pageSize = 0, sectorSize = 0, alignmentLeft = 0;

	if (buffer == NULL)
	{
		return PAL_ERR_INTERNAL_FLASH_NULL_PTR_RECEIVED;
	}
	else if (address & BITS_ALIGNED_TO_32)
	{
		return PAL_ERR_INTERNAL_FLASH_BUFFER_ADDRESS_NOT_ALIGNED;
	}
	else if (size == 0)
	{
		return PAL_ERR_INTERNAL_FLASH_WRONG_SIZE;
	}

	pageSize = pal_internalFlashGetPageSize();
	sectorSize = pal_internalFlashGetSectorSize(address);
	if ((0 == pageSize) || (sectorSize == 0))
	{
	    return PAL_ERR_INTERNAL_FLASH_FLASH_ZERO_SIZE;
	}

	if (address % pageSize)
	{
		ret =  PAL_ERR_INTERNAL_FLASH_ADDRESS_NOT_ALIGNED;
	}
	else if (((address % sectorSize) + size) > sectorSize)
	{
		ret =  PAL_ERR_INTERNAL_FLASH_CROSSING_SECTORS;
	}
	else
	{
	    palStatus_t mutexRet = PAL_SUCCESS;
	    ret = pal_osMutexWait(g_flashMutex, PAL_RTOS_WAIT_FOREVER);
	    if (ret == PAL_SUCCESS)
	    {
	        alignmentLeft = size % pageSize; //Keep the leftover to be copied separately
	        if (size > pageSize)
	        {
	            ret = pal_plat_internalFlashWrite(size - alignmentLeft, address, buffer);
	        }

	        if ((ret == PAL_SUCCESS) && (alignmentLeft != 0))
	        {
	            uint32_t * pageBuffer = (uint32_t *)malloc(pageSize);
	            if (pageBuffer == NULL)
	            {
	                ret = PAL_ERR_NO_MEMORY;
	            }
	            else
	            {
	                memset(pageBuffer, 0xFF, pageSize);
	                memcpy(pageBuffer, (uint8_t*)buffer + (size - alignmentLeft), alignmentLeft);
	                ret = pal_plat_internalFlashWrite(pageSize, address + (size - alignmentLeft), pageBuffer);
	                free(pageBuffer);
	            }
	        }
	        mutexRet = pal_osMutexRelease(g_flashMutex);
	        if(PAL_SUCCESS != mutexRet)
	        {
	            ret = PAL_ERR_INTERNAL_FLASH_MUTEX_RELEASE_ERROR;
	        }
	    }
	}
	return ret;
}

palStatus_t pal_internalFlashRead(const size_t size, const uint32_t address, uint32_t * buffer)
{
	palStatus_t ret = PAL_SUCCESS;

	if (buffer == NULL)
	{
		return PAL_ERR_INTERNAL_FLASH_NULL_PTR_RECEIVED;
	}

	if (size == 0)
	{
		return PAL_ERR_INTERNAL_FLASH_WRONG_SIZE;
	}

    ret = pal_osMutexWait(g_flashMutex, PAL_RTOS_WAIT_FOREVER);
    if (PAL_SUCCESS == ret)
    {
        palStatus_t mutexRet = PAL_SUCCESS;
        ret = pal_plat_internalFlashRead(size, address, buffer);
        mutexRet = pal_osMutexRelease(g_flashMutex);
        if(PAL_SUCCESS != mutexRet)
        {
            ret = PAL_ERR_INTERNAL_FLASH_MUTEX_RELEASE_ERROR;
        }
    }

	return ret;
}

palStatus_t pal_internalFlashErase(uint32_t address, size_t size)
{
    palStatus_t ret = PAL_SUCCESS;
	if (size == 0)
	{
		return PAL_ERR_INTERNAL_FLASH_WRONG_SIZE;
	}

	if (address & 0x3)
	{
		return PAL_ERR_INTERNAL_FLASH_BUFFER_ADDRESS_NOT_ALIGNED;
	}

    ret = pal_osMutexWait(g_flashMutex, PAL_RTOS_WAIT_FOREVER);
    if (PAL_SUCCESS == ret)
    {
        palStatus_t mutexRet = PAL_SUCCESS;
        ret = pal_plat_internalFlashErase(address, size);
        mutexRet = pal_osMutexRelease(g_flashMutex);
        if(PAL_SUCCESS != mutexRet)
        {
            ret = PAL_ERR_INTERNAL_FLASH_MUTEX_RELEASE_ERROR;
        }
    }
	return ret;
}

palStatus_t pal_internalFlashGetAreaInfo(bool section, palSotpAreaData_t *data)
{
    palStatus_t ret = PAL_SUCCESS;
    const palSotpAreaData_t internalFlashArea[] =
            {
                    {PAL_INTERNAL_FLASH_SECTION_1_ADDRESS, PAL_INTERNAL_FLASH_SECTION_1_SIZE},
                    {PAL_INTERNAL_FLASH_SECTION_2_ADDRESS, PAL_INTERNAL_FLASH_SECTION_2_SIZE}
            };

    if(data == NULL)
    {
        ret = PAL_ERR_INTERNAL_FLASH_NULL_PTR_RECEIVED;
    }
    else
    {
        data->address = internalFlashArea[section].address;
        data->size = internalFlashArea[section].size;
    }

    return ret;
}

