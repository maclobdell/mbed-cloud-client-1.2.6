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
#include "pal_plat_network.h"
#include "pal_plat_TLS.h"
#include "pal_plat_Crypto.h"
#include "pal_macros.h"



//this variable must be a int32_t for using atomic increment
PAL_PRIVATE int32_t g_palIntialized = 0;


PAL_PRIVATE void pal_modulesCleanup(void)
{
    DEBUG_PRINT("Destroying modules\r\n");
    pal_plat_cleanupTLS();
    pal_plat_socketsTerminate(NULL);
    pal_RTOSDestroy();
    pal_plat_cleanupCrypto();
    pal_internalFlashDeInit();
}


palStatus_t pal_init(void)
{

    palStatus_t status = PAL_SUCCESS;
    int32_t currentInitValue;
    //  get the return value of g_palIntialized+1 to save it locally
    currentInitValue = pal_osAtomicIncrement(&g_palIntialized,1);
    // if increased for the 1st time
    if (1 == currentInitValue)
    {
        DEBUG_PRINT("\nInit for the 1st time, initializing the modules\r\n");
        status = pal_RTOSInitialize(NULL);
        if (PAL_SUCCESS == status)
        {
            DEBUG_PRINT("\n1. Network init\r\n");
            status = pal_plat_socketsInit(NULL);
            if (PAL_SUCCESS != status)
            {
                DEBUG_PRINT("init of network module has failed with status %" PRIu32 "\r\n",status);
            }
            else //socket init succeeded
            {
                DEBUG_PRINT("\n2. TLS init\r\n");
                status = pal_plat_initTLSLibrary();
                if (PAL_SUCCESS != status)
                {
                    DEBUG_PRINT("init of tls module has failed with status %" PRIu32 "\r\n",status);
                }
                else
                {
                    DEBUG_PRINT("\n3. Crypto init\r\n");
                    status = pal_plat_initCrypto();
                    if (PAL_SUCCESS != status)
                    {
                        DEBUG_PRINT("init of crypto module has failed with status %" PRIu32 "\r\n",status);
                    }
                    else
                    {
                    	DEBUG_PRINT("\n4. Internal Flash init\r\n");
                    	status = pal_internalFlashInit();
                        if (PAL_SUCCESS != status)
                        {
                            DEBUG_PRINT("init of Internal Flash module has failed with status %" PRIu32 "\r\n",status);
                        }
                    }
                }
            }
        }
        else
        {
            DEBUG_PRINT("init of RTOS module has failed with status %" PRIu32 "\r\n",status);
        }

        // if failed decrease the value of g_palIntialized
        if (PAL_SUCCESS != status)
        {
            pal_modulesCleanup();
            pal_osAtomicIncrement(&g_palIntialized, -1);
            PAL_LOG(ERR,"\nInit failed\r\n");
        }        
    }

 
    return status;
}


int32_t  pal_destroy(void)
{
    int32_t currentInitValue;
    // get the current value of g_palIntialized locally
    currentInitValue = pal_osAtomicIncrement(&g_palIntialized, 0);
    if(currentInitValue != 0)
    {
		currentInitValue = pal_osAtomicIncrement(&g_palIntialized, -1);
		if (0 == currentInitValue)
		{
			pal_modulesCleanup();
		}
    }
    return currentInitValue;
}
