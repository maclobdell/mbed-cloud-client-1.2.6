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


#include "pal_rtos.h"
#include "pal_plat_rtos.h"
#include "pal_Crypto.h"


#if PAL_UNIQUE_THREAD_PRIORITY
//! Threads priorities array.
uint32_t g_palThreadPriorities[PAL_NUMBER_OF_THREADS_PRIORITIES] = {0};
#endif //PAL_UNIQUE_THREAD_PRIORITY

palMutexID_t g_palThreadInitMutex = NULLPTR;

//! static variables for Random functionality.
//! CTR-DRBG context to be used for generating random numbers from given seed
static palCtrDrbgCtxHandle_t s_ctrDRBGCtx = NULLPTR;


static uint64_t g_palDeviceBootTimeInSec = 0;

/*
 * Here we define const keys for RoT derivation algorithm.
 * Must be 16 characters or less
 */
#define PAL_STORAGE_SIGNATURE_128_BIT_KEY  "RoTStorageSgn128"
#define PAL_STORAGE_ENCRYPTION_128_BIT_KEY "RoTStorageEnc128"
#define PAL_STORAGE_ENCRYPTION_256_BIT_KEY "StorageEnc256HMACSHA256SIGNATURE"

static bool palRTOSInitialized = false;

palStatus_t pal_RTOSInitialize(void* opaqueContext)
{
    palStatus_t status = PAL_SUCCESS;

    if (palRTOSInitialized)
    {
        return PAL_SUCCESS;
    }
    memset(g_palThreadPriorities, 0, sizeof(g_palThreadPriorities));
    status = pal_osMutexCreate(&g_palThreadInitMutex);
    if(PAL_SUCCESS == status)
    {
        status = pal_plat_RTOSInitialize(opaqueContext);
        if(PAL_SUCCESS == status)
        {
        	palRTOSInitialized = true;
        }
    }

    return status;
}

palStatus_t pal_RTOSDestroy(void)
{
	palStatus_t status = PAL_SUCCESS;
	int i = 0;

	if(palRTOSInitialized == true)
	{
		for (i = 0; i < PAL_MAX_NUMBER_OF_THREADS; ++i)
		{
			palThreadID_t tempID = i;
			pal_osThreadTerminate(&tempID);
		}
		palRTOSInitialized = false;

	    status = pal_osMutexDelete(&g_palThreadInitMutex);
		if ((NULLPTR != s_ctrDRBGCtx) && (PAL_SUCCESS == status))
		{
			status = pal_CtrDRBGFree(&s_ctrDRBGCtx);
		}
	    if (PAL_SUCCESS == status)
		{
			status = pal_plat_RTOSDestroy();
		}

	}
	else
	{
		status = PAL_ERR_NOT_INITIALIZED;
	}
	return status;
}

void pal_osReboot(void)
{
    pal_plat_osReboot();
}

uint64_t pal_osKernelSysTick(void)
{
    static uint64_t lastValue = 0;
    static uint64_t wraparoundsDetected = 0;
    const uint64_t one = 1;
    uint64_t tics = pal_plat_osKernelSysTick();
    uint64_t tmp = tics + (wraparoundsDetected << 32);

    if (tmp < lastValue) //erez's "wraparound algorithm" if we detect a wrap around add 1 to the higher 32 bits
    {
        tmp = tmp + (one << 32);
        wraparoundsDetected++;
    }
    lastValue = tmp;
    return (uint64_t)tmp;
}


uint64_t pal_osKernelSysTickMicroSec(uint64_t microseconds)
{
    uint64_t result;
    result = pal_plat_osKernelSysTickMicroSec(microseconds);
    return result;
}

uint64_t pal_osKernelSysMilliSecTick(uint64_t sysTicks)
{
    uint64_t result = 0;
    uint64_t osTickFreq = pal_plat_osKernelSysTickFrequency();
    if ((sysTicks) && (osTickFreq)) // > 0
    {
    	result = (uint64_t)((sysTicks) / osTickFreq * PAL_TICK_TO_MILLI_FACTOR); //convert ticks per second to milliseconds
    }

    return result;
}

uint64_t pal_osKernelSysTickFrequency(void)
{
    uint64_t result;
    result = pal_plat_osKernelSysTickFrequency();
    return result;
}

palStatus_t pal_osThreadCreate(palThreadFuncPtr function, void* funcArgument, palThreadPriority_t priority, uint32_t stackSize, uint32_t* stackPtr, palThreadLocalStore_t* store, palThreadID_t* threadID)
{
    palStatus_t status = PAL_SUCCESS;
    //! check if the priority have been used by other thread before
    if(PAL_osPriorityError == priority)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }


#if PAL_UNIQUE_THREAD_PRIORITY
    uint32_t incrementedPriorityNum = pal_osAtomicIncrement((int32_t*)&(g_palThreadPriorities[priority+PRIORITY_INDEX_OFFSET]),1);
    // if incrementedPriorityNum != 1 the cell is already occupied by other thread with the same priority.
    if (incrementedPriorityNum != 1)
    {
        *threadID = NULLPTR;
        status = PAL_ERR_RTOS_PRIORITY;
    }
#endif //PAL_IGNORE_UNIQUE_THREAD_PRIORITY

    if (PAL_SUCCESS == status)
    {
        status = pal_plat_osThreadCreate(function, funcArgument, priority, stackSize, NULL, store, threadID);
	#if PAL_UNIQUE_THREAD_PRIORITY
		if (PAL_SUCCESS != status)
		{
			g_palThreadPriorities[priority+PRIORITY_INDEX_OFFSET]= 0 ;
		}
	#endif //PAL_IGNORE_UNIQUE_THREAD_PRIORITY
    }

    return status;
}

palStatus_t pal_osThreadCreateWithAlloc(palThreadFuncPtr function, void* funcArgument, palThreadPriority_t priority, uint32_t stackSize, palThreadLocalStore_t* store, palThreadID_t* threadID)
{
    palStatus_t status = PAL_SUCCESS;

    //! check if the priority have been used by other thread before
    if(PAL_osPriorityError == priority)
    {
    	return PAL_ERR_INVALID_ARGUMENT;
    }

#if PAL_UNIQUE_THREAD_PRIORITY
    uint32_t incrementedPriorityNum = pal_osAtomicIncrement((int32_t*)&(g_palThreadPriorities[priority+PRIORITY_INDEX_OFFSET]),1);
    // if incrementedPriorityNum != 1 the cell is already occupied by other thread with the same priority.
    if (incrementedPriorityNum != 1)
    {
        *threadID = NULLPTR;
        status = PAL_ERR_RTOS_PRIORITY;
    }
#endif //PAL_IGNORE_UNIQUE_THREAD_PRIORITY

    if (PAL_SUCCESS == status)
    {
        status = pal_plat_osThreadCreate(function, funcArgument, priority, stackSize, NULL, store, threadID);
	#if PAL_UNIQUE_THREAD_PRIORITY
		if (PAL_SUCCESS != status)
		{
			g_palThreadPriorities[priority+PRIORITY_INDEX_OFFSET]= 0 ;
		}
	#endif //PAL_IGNORE_UNIQUE_THREAD_PRIORITY
    }

    return status;
}


palStatus_t pal_osThreadTerminate(palThreadID_t* threadID)
{
    palStatus_t status;
    if ((NULL == threadID) || (PAL_INVALID_THREAD == *threadID))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_plat_osThreadTerminate(threadID);
    return status;
}

palThreadID_t pal_osThreadGetId(void)
{
    palThreadID_t result;
    result = pal_plat_osThreadGetId();
    return result;
}

palThreadLocalStore_t*  pal_osThreadGetLocalStore(void)
{
    void* result;
    result = pal_plat_osThreadGetLocalStore();
    return result;
}

palStatus_t pal_osDelay(uint32_t milliseconds)
{
    palStatus_t status;
    status = pal_plat_osDelay(milliseconds);
    return status;
}


palStatus_t pal_osTimerCreate(palTimerFuncPtr function, void* funcArgument, palTimerType_t timerType, palTimerID_t* timerID)
{
    palStatus_t status;
    status = pal_plat_osTimerCreate(function, funcArgument, timerType, timerID);
    return status;
}

palStatus_t pal_osTimerStart(palTimerID_t timerID, uint32_t millisec)
{
    palStatus_t status;
    if (0 == millisec)
    {
        return PAL_ERR_RTOS_VALUE;
    }
    status = pal_plat_osTimerStart(timerID, millisec);
    return status;
}

palStatus_t pal_osTimerStop(palTimerID_t timerID)
{
    palStatus_t status;
    status = pal_plat_osTimerStop(timerID);
    return status;
}

palStatus_t pal_osTimerDelete(palTimerID_t* timerID)
{
    palStatus_t status;
    status = pal_plat_osTimerDelete(timerID);
    return status;
}

palStatus_t pal_osMutexCreate(palMutexID_t* mutexID)
{
    palStatus_t status;
    status = pal_plat_osMutexCreate(mutexID);
    return status;
}

palStatus_t pal_osMutexWait(palMutexID_t mutexID, uint32_t millisec)
{
    palStatus_t status;
    status = pal_plat_osMutexWait(mutexID, millisec);
    return status;
}

palStatus_t pal_osMutexRelease(palMutexID_t mutexID)
{
    palStatus_t status;
    status = pal_plat_osMutexRelease(mutexID);
    return status;
}

palStatus_t pal_osMutexDelete(palMutexID_t* mutexID)
{
    palStatus_t status;
    status = pal_plat_osMutexDelete(mutexID);
    return status;
}
palStatus_t pal_osSemaphoreCreate(uint32_t count, palSemaphoreID_t* semaphoreID)
{
    palStatus_t status;
    status = pal_plat_osSemaphoreCreate(count, semaphoreID);
    return status;
}

palStatus_t pal_osSemaphoreWait(palSemaphoreID_t semaphoreID, uint32_t millisec,  int32_t* countersAvailable)
{
    palStatus_t status;
    status = pal_plat_osSemaphoreWait(semaphoreID, millisec, countersAvailable);
    return status;
}

palStatus_t pal_osSemaphoreRelease(palSemaphoreID_t semaphoreID)
{
    palStatus_t status;
    status = pal_plat_osSemaphoreRelease(semaphoreID);
    return status;
}

palStatus_t pal_osSemaphoreDelete(palSemaphoreID_t* semaphoreID)
{
    palStatus_t status;
    status = pal_plat_osSemaphoreDelete(semaphoreID);
    return status;
}

palStatus_t pal_osPoolCreate(uint32_t blockSize, uint32_t blockCount, palMemoryPoolID_t* memoryPoolID)
{
    palStatus_t status;
    status = pal_plat_osPoolCreate(blockSize, blockCount, memoryPoolID);
    return status;
}

void* pal_osPoolAlloc(palMemoryPoolID_t memoryPoolID)
{
    void* result;
    result = pal_plat_osPoolAlloc(memoryPoolID);
    return result;
}

void* pal_osPoolCAlloc(palMemoryPoolID_t memoryPoolID)
{
    void* result;
    //TODO(nirson01): debug print in case of failed alloc?
    result = pal_plat_osPoolCAlloc(memoryPoolID);
    return result;
}

palStatus_t pal_osPoolFree(palMemoryPoolID_t memoryPoolID, void* block)
{
    palStatus_t status;
    //TODO(nirson01): debug print in case of failed alloc?
    status = pal_plat_osPoolFree(memoryPoolID, block);
    return status;
}

palStatus_t pal_osPoolDestroy(palMemoryPoolID_t* memoryPoolID)
{
    palStatus_t status;
    status = pal_plat_osPoolDestroy(memoryPoolID);  
    return status;
}

palStatus_t pal_osMessageQueueCreate(uint32_t messageQCount, palMessageQID_t* messageQID)
{
    palStatus_t status;
    status = pal_plat_osMessageQueueCreate(messageQCount, messageQID);
    return status;
}

palStatus_t pal_osMessagePut(palMessageQID_t messageQID, uint32_t info, uint32_t timeout)
{
    palStatus_t status;
    status = pal_plat_osMessagePut(messageQID, info, timeout);
    return status;
}

palStatus_t pal_osMessageGet(palMessageQID_t messageQID, uint32_t timeout, uint32_t* messageValue)
{
    palStatus_t status;
    status = pal_plat_osMessageGet(messageQID, timeout, messageValue);
    return status;
}

palStatus_t pal_osMessageQueueDestroy(palMessageQID_t* messageQID)
{
    palStatus_t status;
    status = pal_plat_osMessageQueueDestroy(messageQID);
    return status;
}

int32_t pal_osAtomicIncrement(int32_t* valuePtr, int32_t increment)
{
    int32_t result;
    result = pal_plat_osAtomicIncrement(valuePtr, increment);
    return result;
}

inline PAL_PRIVATE uint64_t pal_sysTickTimeToSec()
{
	uint64_t sysTicksFromBoot = pal_osKernelSysTick();
	uint64_t secFromBoot = pal_osKernelSysMilliSecTick(sysTicksFromBoot) / PAL_MILLI_PER_SECOND;

	return secFromBoot;
}

uint64_t pal_osGetTime(void)
{
	uint64_t curSysTimeInSec = 0;
	if (0 < g_palDeviceBootTimeInSec) //time was previously set
	{
		uint64_t secFromBoot = pal_sysTickTimeToSec();
		curSysTimeInSec = g_palDeviceBootTimeInSec + secFromBoot; //boot time in sec + sec passed since boot
	}

	return curSysTimeInSec;
}

palStatus_t pal_osSetTime(uint64_t seconds)
{
	palStatus_t status = PAL_SUCCESS;
	if (seconds < (uint64_t)PAL_MIN_SEC_FROM_EPOCH)
	{
		status = PAL_ERR_INVALID_TIME;
	}
	else
	{
		uint64_t secFromBoot = pal_sysTickTimeToSec();
		g_palDeviceBootTimeInSec = seconds - secFromBoot; //update device boot time
	}

	return status;
}

palStatus_t pal_osRandom32bit(uint32_t *random)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == random)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
        
    status = pal_osRandomBuffer((uint8_t*)random, sizeof(uint32_t));
    return status;
}

palStatus_t pal_osRandomBuffer(uint8_t *randomBuf, size_t bufSizeBytes)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == randomBuf)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    if (NULLPTR == s_ctrDRBGCtx)
    {
        uint8_t seed[PAL_INITIAL_RANDOM_SIZE] = {0}; //in order to get 128-bits initial seed
        status = pal_plat_osRandomBuffer(seed, sizeof(seed));
        if (PAL_SUCCESS != status)
        {
            goto finish;
        }
        status = pal_CtrDRBGInit(&s_ctrDRBGCtx, (void*)seed, sizeof(seed));
        if (PAL_SUCCESS != status)
        {
            goto finish;
        }
    }

    status = pal_CtrDRBGGenerate(s_ctrDRBGCtx, (unsigned char*)randomBuf, bufSizeBytes);

finish:
    return status;
}

//As mentioned in the header file, this function ignores the upperBound parameter
//in this stage, in the future it will be supported. (Erez)
palStatus_t pal_osRandomUniform(uint32_t upperBound, uint32_t *random)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == random)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_osRandomBuffer((uint8_t*)random, sizeof(uint32_t));
    //*random = random % upperBound;
    return status;
}

palStatus_t pal_osGetDeviceKey(palDevKeyType_t keyType, uint8_t *key, size_t keyLenBytes)
{
	palStatus_t status = PAL_SUCCESS;
	unsigned char rot[PAL_DEVICE_KEY_SIZE_IN_BYTES] = {0};
    
	if ((keyLenBytes < PAL_DEVICE_KEY_SIZE_IN_BYTES) || ((palOsStorageHmacSha256 == keyType) && (keyLenBytes < PAL_SHA256_DEVICE_KEY_SIZE_IN_BYTES)))
	{
		return PAL_ERR_BUFFER_TOO_SMALL;
	}
	if (NULL == key)
	{
		return PAL_ERR_NULL_POINTER;
	}

    status = pal_plat_osGetRoT128Bit(rot, PAL_DEVICE_KEY_SIZE_IN_BYTES);
	if (PAL_SUCCESS == status)
	{   // Logic of RoT according to key type using 128 bit strong Key Derivation Algorithm
        switch(keyType)
        {
            case palOsStorageEncryptionKey128Bit:
            {
                //USE strong KDF here!
                status = pal_cipherCMAC((const unsigned char*)PAL_STORAGE_ENCRYPTION_128_BIT_KEY, PAL_DEVICE_KEY_SIZE_IN_BITS, (const unsigned char *)rot, PAL_DEVICE_KEY_SIZE_IN_BYTES, key);
                break;
            }
            case palOsStorageSignatureKey128Bit:
            {
                //USE strong KDF here!
                status = pal_cipherCMAC((const unsigned char*)PAL_STORAGE_SIGNATURE_128_BIT_KEY, PAL_DEVICE_KEY_SIZE_IN_BITS, (const unsigned char *)rot, PAL_DEVICE_KEY_SIZE_IN_BYTES, key);
                break;
            }
            case palOsStorageHmacSha256:
            {
                size_t outputLenInBytes = 0;
                status = pal_mdHmacSha256((const unsigned char *)PAL_STORAGE_ENCRYPTION_256_BIT_KEY, PAL_SHA256_DEVICE_KEY_SIZE_IN_BYTES, (const unsigned char*)rot, PAL_DEVICE_KEY_SIZE_IN_BYTES, key, &outputLenInBytes);
                break;
            }
            default:
                status = PAL_ERR_GET_DEV_KEY;

        }

	} // outer if
    else
    {
        status = PAL_ERR_GET_DEV_KEY;
    }
    memset(rot, 0, PAL_DEVICE_KEY_SIZE_IN_BYTES);

	return status;

}

