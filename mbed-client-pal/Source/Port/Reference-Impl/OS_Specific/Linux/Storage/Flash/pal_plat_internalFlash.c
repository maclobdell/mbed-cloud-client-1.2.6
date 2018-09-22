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


////////////////////////////PRIVATE///////////////////////////////////
////////////////////////////END PRIVATE////////////////////////////////

palStatus_t pal_plat_internalFlashInit(void)
{
	return PAL_SUCCESS;
}


palStatus_t pal_plat_internalFlashDeInit(void)
{
	return PAL_SUCCESS;
}


palStatus_t pal_plat_internalFlashWrite(const size_t size, const uint32_t address, const uint32_t * buffer)
{
	return PAL_ERR_NOT_SUPPORTED;
}


palStatus_t pal_plat_internalFlashRead(const size_t size, const uint32_t address, uint32_t * buffer)
{
	return PAL_ERR_NOT_SUPPORTED;
}


palStatus_t pal_plat_internalFlashErase(uint32_t address, size_t size)
{
	return PAL_ERR_NOT_SUPPORTED;
}


size_t pal_plat_internalFlashGetPageSize(void)
{
	return 0;
}


size_t pal_plat_internalFlashGetSectorSize(uint32_t address)
{
	return 0;
}
