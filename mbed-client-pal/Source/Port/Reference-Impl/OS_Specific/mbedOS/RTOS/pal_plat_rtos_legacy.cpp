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




#include "pal_types.h"
#include "pal_rtos.h"
#include "pal_plat_rtos.h"
#include "pal_errors.h"
#include "stdlib.h"
#include "string.h"

#include "mbed.h"

#include "entropy_poll.h"


/*******************************************************************************
    mbedOS legacy RTOS support
********************************************************************************/
#if !(defined(osRtxVersionAPI) && (osRtxVersionAPI >= 20000000))

#define PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(cmsisCode)\
    ((int32_t)((int32_t)cmsisCode + PAL_ERR_RTOS_ERROR_BASE))

//! the size of the memory to allocate was taken from CMSIS header (cmsis_os.h)
#define PAL_RTOS_MEMORY_POOL_SIZE(blockSize, blockCount)\
    (sizeof(uint32_t)*(3+((blockSize+3)/4)*(blockCount)))

//! the size of the memory to allocate was taken from CMSIS header (cmsis_os.h)
#define PAL_RTOS_MESSAGE_Q_SIZE(messageQSize)\
    (sizeof(uint32_t)*(4 + messageQSize))


#ifdef PAL_RTOS_WAIT_FOREVER
#undef PAL_RTOS_WAIT_FOREVER
#define PAL_RTOS_WAIT_FOREVER osWaitForever
#endif //PAL_RTOS_WAIT_FOREVER

//! This definitions should be under #ifdef for different CORTEX-X processors.
//! The current vaules are for cortex-M these are the sizes of the internal data array in definitions arrays
#define PAL_TIMER_DATA_SIZE 6
#define PAL_MUTEX_DATA_SIZE 4
#define PAL_SEMAPHORE_DATA_SIZE 2
#define PAL_NUM_OF_THREAD_INSTANCES 1

PAL_PRIVATE uint8_t g_randomBuffer[PAL_INITIAL_RANDOM_SIZE] = {0};
PAL_PRIVATE bool g_randInitiated = false;

typedef struct palThreadFuncWrapper{
    palTimerFuncPtr         realThreadFunc;
    void*                   realThreadArgs;
    uint32_t                threadIndex;
}palThreadFuncWrapper_t;

//! Thread structure
typedef struct palThread{
    palThreadID_t           threadID;
    bool                    initialized;
    palThreadLocalStore_t*  threadStore; //! please see pal_rtos.h for documentation
    palThreadFuncWrapper_t  threadFuncWrapper;
    osThreadDef_t           osThread;
    bool                    taskCompleted; //The task has completed and exit
} palThread_t;


palThread_t g_palThreads[PAL_MAX_NUMBER_OF_THREADS] = {0};

//! Timer structure
typedef struct palTimer{
    palTimerID_t            timerID;
    uint32_t                internalTimerData[PAL_TIMER_DATA_SIZE];  ///< pointer to internal data
    osTimerDef_t            osTimer;
} palTimer_t;

//! Mutex structure
typedef struct palMutex{
    palMutexID_t            mutexID;
    uint32_t                internalMutexData[PAL_MUTEX_DATA_SIZE];
    osMutexDef_t            osMutex;
}palMutex_t;

//! Semaphore structure
typedef struct palSemaphore{
    palSemaphoreID_t        semaphoreID;
    uint32_t                internalSemaphoreData[PAL_SEMAPHORE_DATA_SIZE];
    osSemaphoreDef_t        osSemaphore;
}palSemaphore_t;


//! Memoey Pool structure
typedef struct palMemPool{
    palMemoryPoolID_t       memoryPoolID;
    osPoolDef_t             osPool;
}palMemoryPool_t;

//! Message Queue structure
typedef struct palMessageQ{
    palMessageQID_t         messageQID;
    osMessageQDef_t         osMessageQ;
}palMessageQ_t;


inline PAL_PRIVATE void setDefaultThreadValues(palThread_t* thread)
{
#if PAL_UNIQUE_THREAD_PRIORITY      
    g_palThreadPriorities[thread->osThread.tpriority+PRIORITY_INDEX_OFFSET] = false;
#endif //PAL_UNIQUE_THREAD_PRIORITY     
    thread->threadStore = NULL;
    thread->threadFuncWrapper.realThreadArgs = NULL;
    thread->threadFuncWrapper.realThreadFunc = NULL;
    thread->threadFuncWrapper.threadIndex = 0;
    thread->osThread.pthread = NULL;
	thread->osThread.tpriority = (osPriority)PAL_osPriorityError;
    thread->osThread.instances = PAL_NUM_OF_THREAD_INSTANCES;
    thread->osThread.stacksize = 0;
#if __MBED_CMSIS_RTOS_CM
    if(thread->osThread.stack_pointer != NULL)
    {
    	free(thread->osThread.stack_pointer);
    }
#else
    thread->osThread.stack_pointer = NULL;
#endif
    thread->threadID = NULLPTR;
    thread->taskCompleted = false;
    //! This line should be last thing to be done in this function.
    //! in order to prevent double accessing the same index between
    //! this function and the threadCreate function.
    thread->initialized = false;
}

/*! Clean thread data from the global thread data base (g_palThreads). Thread Safe API
*
* @param[in] dbPointer: data base pointer.
* @param[in] index: the index in the data base to be cleaned.
*/
PAL_PRIVATE void threadCleanUp(void* dbPointer, uint32_t index)
{
    palThread_t* threadsDB = (palThread_t*)dbPointer;

    if (NULL == dbPointer || index >= PAL_MAX_NUMBER_OF_THREADS)
    {
        return;
    }
    setDefaultThreadValues(&threadsDB[index]);
}

/*! Thread wrapper function, this function will be set as the thread function (for every thread)
*   and it will get as an argument the real data about the thread and call the REAL thread function
*   with the REAL argument.
*
*   @param[in] arg: data structure which contains the real data about the thread.
*/
PAL_PRIVATE void threadFunctionWrapper(void const* arg)
{
    palThreadFuncWrapper_t* threadWrapper = (palThreadFuncWrapper_t*)arg;

    if (NULL != threadWrapper)
    {
        if(g_palThreads[threadWrapper->threadIndex].threadID == NULLPTR)
        {
        	g_palThreads[threadWrapper->threadIndex].threadID = (palThreadID_t)osThreadGetId();
        }
        threadWrapper->realThreadFunc(threadWrapper->realThreadArgs);
        g_palThreads[threadWrapper->threadIndex].taskCompleted = true;
    }
}


void pal_plat_osReboot()
{
    NVIC_SystemReset();
}


palStatus_t pal_plat_RTOSInitialize(void* opaqueContext)
{
	//Clean thread tables
    palStatus_t status = PAL_SUCCESS;
    int32_t platStatus = 0;
    size_t actualOutputLen = 0;

	memset(g_palThreads,0,sizeof(palThread_t) * PAL_MAX_NUMBER_OF_THREADS);

    //Add implicit the running task as PAL main
    g_palThreads[0].initialized = true;
    g_palThreads[0].threadID = (palThreadID_t)osThreadGetId();
    g_palThreads[0].osThread.stack_pointer = NULL;


    platStatus = mbedtls_hardware_poll(NULL /*Not used by the function*/, g_randomBuffer, sizeof(g_randomBuffer), &actualOutputLen);
    if (0 != platStatus || actualOutputLen != sizeof(g_randomBuffer))
    {
        status = PAL_ERR_RTOS_TRNG_FAILED;
    }
    else
    {
    	g_randInitiated = true;
    }

    return status;
}





palStatus_t pal_plat_RTOSDestroy(void)
{
    return PAL_SUCCESS;
}

palStatus_t pal_plat_osDelay(uint32_t milliseconds)
{
    palStatus_t status;
    osStatus platStatus = osDelay(milliseconds);
    if (osEventTimeout == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus); //TODO(nirson01): error propagation MACRO??
    }
    return status;
}

uint64_t pal_plat_osKernelSysTick(void)
{
    uint64_t result;
    result = osKernelSysTick();
    return result;
}

uint64_t pal_plat_osKernelSysTickMicroSec(uint64_t microseconds)
{
    uint64_t result;
    result = osKernelSysTickMicroSec(microseconds);
    return result;
}

uint64_t pal_plat_osKernelSysTickFrequency()
{
    return osKernelSysTickFrequency;
}

palStatus_t pal_plat_osThreadCreate(palThreadFuncPtr function, void* funcArgument, palThreadPriority_t priority, uint32_t stackSize, uint32_t* stackPtr, palThreadLocalStore_t* store, palThreadID_t* threadID)
{
    palStatus_t status = PAL_SUCCESS;
    uint32_t firstAvailableThreadIndex = PAL_MAX_NUMBER_OF_THREADS;
    uint32_t i;
    uint32_t *stackAllocPtr = NULL;
    osThreadId osThreadID = NULL;

    if (NULL == threadID || NULL == function || 0 == stackSize || priority > PAL_osPriorityRealtime)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < PAL_MAX_NUMBER_OF_THREADS; ++i)
    {
    	if (!g_palThreads[i].initialized)
        {
    		g_palThreads[i].initialized = true;
            firstAvailableThreadIndex = i;
            break;
        }
    }

    if (firstAvailableThreadIndex >= PAL_MAX_NUMBER_OF_THREADS)
    {
        status = PAL_ERR_RTOS_RESOURCE;
    }

#if __MBED_CMSIS_RTOS_CM
    if (PAL_SUCCESS == status)
    {
        stackAllocPtr =  (uint32_t*)malloc(stackSize);

        if(NULL == stackAllocPtr)
        {
            status = PAL_ERR_RTOS_RESOURCE;
        }
    }
#endif
    if (PAL_SUCCESS == status)
    {
        g_palThreads[firstAvailableThreadIndex].threadStore = store;
        g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadArgs = funcArgument;
        g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadFunc = function;
        g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.threadIndex = firstAvailableThreadIndex;
        g_palThreads[firstAvailableThreadIndex].osThread.pthread = threadFunctionWrapper;
        g_palThreads[firstAvailableThreadIndex].osThread.tpriority = (osPriority)priority;
        g_palThreads[firstAvailableThreadIndex].osThread.instances = PAL_NUM_OF_THREAD_INSTANCES;
        g_palThreads[firstAvailableThreadIndex].osThread.stacksize = stackSize;
#if __MBED_CMSIS_RTOS_CM
        	g_palThreads[firstAvailableThreadIndex].osThread.stack_pointer = stackAllocPtr;
#else
        	g_palThreads[firstAvailableThreadIndex].osThread.stack_pointer = stackPtr;
#endif

#if PAL_UNIQUE_THREAD_PRIORITY      
        g_palThreadPriorities[priority+PRIORITY_INDEX_OFFSET] = true;
#endif //PAL_UNIQUE_THREAD_PRIORITY     

    
        osThreadID = osThreadCreate(&g_palThreads[firstAvailableThreadIndex].osThread, &g_palThreads[firstAvailableThreadIndex].threadFuncWrapper);
        
        if(NULL == osThreadID)
        {
            //! in case of error in the thread creation, reset the data of the given index in the threads array.
            threadCleanUp(g_palThreads, firstAvailableThreadIndex);
            status = PAL_ERR_GENERIC_FAILURE;
            *threadID = PAL_INVALID_THREAD;
        }
        else
        {
        	*threadID = firstAvailableThreadIndex;
        }

    }
    return status;
}

palThreadID_t pal_plat_osThreadGetId(void)
{
	int i = 0;
	palThreadID_t osThreadID;
	palThreadID_t ret = PAL_INVALID_THREAD;
	osThreadID = (palThreadID_t)osThreadGetId();

	for(i= 0; i < PAL_MAX_NUMBER_OF_THREADS; i++)
	{
		if(osThreadID == g_palThreads[i].threadID)
		{
			ret = i;
			break;
		}
	}
    return ret;
}

palStatus_t pal_plat_osThreadTerminate(palThreadID_t* threadID)
{
    palStatus_t status = PAL_ERR_INVALID_ARGUMENT;
    osStatus platStatus = osOK;

    if (*threadID >= PAL_MAX_NUMBER_OF_THREADS)
    {
        return status;
    }

    if ((palThreadID_t)osThreadGetId() != g_palThreads[*threadID].threadID)
    {//Kill only if not trying to kill from running task
    	if (g_palThreads[*threadID].initialized)
    	{
    		if ((g_palThreads[*threadID].threadID != NULL) && ( g_palThreads[*threadID].taskCompleted == false))
    		{
                platStatus = osThreadTerminate((osThreadId)(g_palThreads[*threadID].threadID));
            }
            if (platStatus != osErrorISR) // osErrorISR: osThreadTerminate cannot be called from interrupt service routines.
    		{
                threadCleanUp(g_palThreads, *threadID);
            }
            else 
            {
                status = PAL_ERR_RTOS_ISR;
            }
    	}
    	*threadID = PAL_INVALID_THREAD;
    	status = PAL_SUCCESS;
    }
    else
    {
    	status = PAL_ERR_RTOS_TASK;
    }

    return status;
}

palThreadLocalStore_t* pal_plat_osThreadGetLocalStore(void)
{
	palThreadLocalStore_t* localStore = NULL;
	palThreadID_t id = (uintptr_t)pal_osThreadGetId();

	if( g_palThreads[id].initialized)
	{
		localStore = g_palThreads[id].threadStore;
	}
	return localStore;
}


palStatus_t pal_plat_osTimerCreate(palTimerFuncPtr function, void* funcArgument, palTimerType_t timerType, palTimerID_t* timerID)
{
    palStatus_t status = PAL_SUCCESS;
    palTimer_t* timer = NULL;

    if(NULL == timerID || NULL == function)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)malloc(sizeof(palTimer_t));
    if (NULL == timer)
    {
        status = PAL_ERR_NO_MEMORY;
    }

    if (PAL_SUCCESS == status)
    {
        timer->osTimer.ptimer = function;
        timer->osTimer.timer = timer->internalTimerData;
        memset(timer->osTimer.timer, 0, sizeof(uint32_t)*PAL_TIMER_DATA_SIZE);
    
        timer->timerID = (uintptr_t)osTimerCreate(&timer->osTimer, (os_timer_type)timerType, funcArgument);
        if (NULLPTR == timer->timerID)
        {
            free(timer);
            timer = NULL;
            status = PAL_ERR_GENERIC_FAILURE;
        }
        else
        {
            *timerID = (palTimerID_t)timer;
        }
    }
    return status;
}

palStatus_t pal_plat_osTimerStart(palTimerID_t timerID, uint32_t millisec)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if (NULLPTR == timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)timerID;
    platStatus = osTimerStart((osTimerId)timer->timerID, millisec);
    if (osOK == (osStatus)platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}

palStatus_t pal_plat_osTimerStop(palTimerID_t timerID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if(NULLPTR == timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)timerID;
    platStatus = osTimerStop((osTimerId)timer->timerID);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;  
}

palStatus_t pal_plat_osTimerDelete(palTimerID_t* timerID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if(NULL == timerID || NULLPTR == *timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)*timerID;
    platStatus = osTimerDelete((osTimerId)timer->timerID);
    if (osOK == platStatus)
    {
        free(timer);
        *timerID = NULLPTR;
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}


palStatus_t pal_plat_osMutexCreate(palMutexID_t* mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    palMutex_t* mutex = NULL;
    if(NULL == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)malloc(sizeof(palMutex_t));
    if (NULL == mutex)
    {
        status = PAL_ERR_NO_MEMORY;
    }

    if (PAL_SUCCESS == status)
    {
        mutex->osMutex.mutex = mutex->internalMutexData;
        memset(mutex->osMutex.mutex, 0, sizeof(uint32_t)*PAL_MUTEX_DATA_SIZE);
    
        mutex->mutexID = (uintptr_t)osMutexCreate(&mutex->osMutex);
        if (NULLPTR == mutex->mutexID)
        {
            free(mutex);
            mutex = NULL;
            status = PAL_ERR_GENERIC_FAILURE;
        }
        else
        {
            *mutexID = (palMutexID_t)mutex;
        }
    }
    return status;
}


palStatus_t pal_plat_osMutexWait(palMutexID_t mutexID, uint32_t millisec)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULLPTR == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)mutexID;
    platStatus = osMutexWait((osMutexId)mutex->mutexID, millisec);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}


palStatus_t pal_plat_osMutexRelease(palMutexID_t mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULLPTR == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)mutexID;
    platStatus = osMutexRelease((osMutexId)mutex->mutexID);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}

palStatus_t pal_plat_osMutexDelete(palMutexID_t* mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULL == mutexID || NULLPTR == *mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)*mutexID;
    platStatus = osMutexDelete((osMutexId)mutex->mutexID);
    if (osOK == platStatus)
    {
        free(mutex);
        *mutexID = NULLPTR;
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}

palStatus_t pal_plat_osSemaphoreCreate(uint32_t count, palSemaphoreID_t* semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    palSemaphore_t* semaphore = NULL;
    if(NULL == semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    semaphore = (palSemaphore_t*)malloc(sizeof(palSemaphore_t));
    if (NULL == semaphore)
    {
        status = PAL_ERR_NO_MEMORY;
    }

    if(PAL_SUCCESS == status)
    {
        semaphore->osSemaphore.semaphore = semaphore->internalSemaphoreData;
        memset(semaphore->osSemaphore.semaphore, 0, sizeof(uint32_t)*PAL_SEMAPHORE_DATA_SIZE);
    
        semaphore->semaphoreID = (uintptr_t)osSemaphoreCreate(&semaphore->osSemaphore, count);
        if (NULLPTR == semaphore->semaphoreID)
        {
            free(semaphore);
            semaphore = NULL;
            status = PAL_ERR_GENERIC_FAILURE;
        }
        else
        {
            *semaphoreID = (palSemaphoreID_t)semaphore;
        }
    }
    return status;  
}

palStatus_t pal_plat_osSemaphoreWait(palSemaphoreID_t semaphoreID, uint32_t millisec, int32_t* countersAvailable)
{
    palStatus_t status = PAL_SUCCESS;
    palSemaphore_t* semaphore = NULL;
    int32_t tmpCountersAvailable = 0;
    if(NULLPTR == semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }   

    semaphore = (palSemaphore_t*)semaphoreID;
    tmpCountersAvailable = osSemaphoreWait((osSemaphoreId)semaphore->semaphoreID, millisec);

    if (0 == tmpCountersAvailable)
    {
        status = PAL_ERR_RTOS_TIMEOUT;
    }
    else if (tmpCountersAvailable < 0)
    {
        tmpCountersAvailable = 0;
        status = PAL_ERR_RTOS_PARAMETER;
    }

    if (NULL != countersAvailable)
    {
	//osSemaphoreWait return the number of available counter + "1" 
	//The "1" is added because return value "0" is timeout so mbedOS return 1 and this is false
    	tmpCountersAvailable--;    
        *countersAvailable = tmpCountersAvailable;
    }
    return status;
}

palStatus_t pal_plat_osSemaphoreRelease(palSemaphoreID_t semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palSemaphore_t* semaphore = NULL;
    
    if(NULLPTR == semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    semaphore = (palSemaphore_t*)semaphoreID;
    platStatus = osSemaphoreRelease((osSemaphoreId)semaphore->semaphoreID);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;
}

palStatus_t pal_plat_osSemaphoreDelete(palSemaphoreID_t* semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palSemaphore_t* semaphore = NULL;
    
    if(NULL == semaphoreID || NULLPTR == *semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    semaphore = (palSemaphore_t*)*semaphoreID;
    platStatus = osSemaphoreDelete((osSemaphoreId)semaphore->semaphoreID);
    if (osOK == platStatus)
    {
        free(semaphore);
        *semaphoreID = NULLPTR;
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;  
}

palStatus_t pal_plat_osPoolCreate(uint32_t blockSize, uint32_t blockCount, palMemoryPoolID_t* memoryPoolID)
{
    palStatus_t status = PAL_SUCCESS;
    palMemoryPool_t* memoryPool = NULL;
    if(NULL == memoryPoolID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    //! allocate the memory pool structure
    memoryPool = (palMemoryPool_t*)malloc(sizeof(palMemoryPool_t));
    if (NULL == memoryPool)
    {
        status = PAL_ERR_NO_MEMORY;
    }

    if(PAL_SUCCESS == status)
    {
        //! allocate the actual memory allocation for the memory pool blocks, the size of the memory 
        //! to allocate was taken from CMSIS header (cmsis_os.h)
        memoryPool->osPool.pool = (uint32_t*)malloc(PAL_RTOS_MEMORY_POOL_SIZE(blockSize, blockCount));
        if (NULL == memoryPool->osPool.pool)
        {
            free(memoryPool);
            *memoryPoolID = NULLPTR;
            status = PAL_ERR_NO_MEMORY;
        }
        else
        {
            memset(memoryPool->osPool.pool, 0, PAL_RTOS_MEMORY_POOL_SIZE(blockSize, blockCount));
            memoryPool->osPool.pool_sz = blockCount;    ///< number of items (elements) in the pool
            memoryPool->osPool.item_sz = blockSize;     ///< size of an item
        
            memoryPool->memoryPoolID = (uintptr_t)osPoolCreate(&memoryPool->osPool);
            if (NULLPTR == memoryPool->memoryPoolID)
            {
                free(memoryPool->osPool.pool);
                free(memoryPool);
                memoryPool = NULL;
                status = PAL_ERR_GENERIC_FAILURE;
            }
            else
            {
                *memoryPoolID = (palMemoryPoolID_t)memoryPool;
            }
        }
    }
    return status;      
}

void* pal_plat_osPoolAlloc(palMemoryPoolID_t memoryPoolID)
{
    void* result = NULL;
    palMemoryPool_t* memoryPool = NULL;
    
    if(NULLPTR == memoryPoolID)
    {
        return NULL;
    }

    memoryPool = (palMemoryPool_t*)memoryPoolID;
    result = osPoolAlloc((osPoolId)memoryPool->memoryPoolID);

    return result;
}

void* pal_plat_osPoolCAlloc(palMemoryPoolID_t memoryPoolID)
{
    void* result = NULL;
    palMemoryPool_t* memoryPool = NULL;
    
    if(NULLPTR == memoryPoolID)
    {
        return NULL;
    }

    memoryPool = (palMemoryPool_t*)memoryPoolID;
    result = osPoolCAlloc((osPoolId)memoryPool->memoryPoolID);

    return result;  
}

palStatus_t pal_plat_osPoolFree(palMemoryPoolID_t memoryPoolID, void* block)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palMemoryPool_t* memoryPool = NULL;
    
    if(NULLPTR == memoryPoolID || NULL == block)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    memoryPool = (palMemoryPool_t*)memoryPoolID;
    platStatus = osPoolFree((osPoolId)memoryPool->memoryPoolID, block);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;  
}

palStatus_t pal_plat_osPoolDestroy(palMemoryPoolID_t* memoryPoolID)
{
    palStatus_t status = PAL_SUCCESS;
    palMemoryPool_t* memoryPool = NULL;
    
    if(NULL == memoryPoolID || NULLPTR == *memoryPoolID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }   

    memoryPool = (palMemoryPool_t*)*memoryPoolID;
    free(memoryPool->osPool.pool);
    free(memoryPool);
    *memoryPoolID = NULLPTR;
    return status;
}

palStatus_t pal_plat_osMessageQueueCreate(uint32_t messageQCount, palMessageQID_t* messageQID)
{
    palStatus_t status = PAL_SUCCESS;
    palMessageQ_t* messageQ = NULL;
    if(NULL == messageQID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    //! allocate the message queue structure
    messageQ = (palMessageQ_t*)malloc(sizeof(palMessageQ_t));
    if (NULL == messageQ)
    {
        status = PAL_ERR_NO_MEMORY;
    }

    if (PAL_SUCCESS == status)
    {
        //! allocate the actual memory allocation for the message queue blocks, the size of the memory 
        //! to allocate was taken from CMSIS header (cmsis_os.h)
        messageQ->osMessageQ.pool = (uint32_t*)malloc(PAL_RTOS_MESSAGE_Q_SIZE(messageQCount));
        if (NULL == messageQ->osMessageQ.pool)
        {
            free(messageQ);
            messageQ = NULL;
            status = PAL_ERR_NO_MEMORY;
        }
        else
        {
            memset(messageQ->osMessageQ.pool, 0, PAL_RTOS_MESSAGE_Q_SIZE(messageQCount));
            messageQ->osMessageQ.queue_sz = messageQCount;   ///< number of items (elements) in the queue
        
            messageQ->messageQID = (uintptr_t)osMessageCreate(&(messageQ->osMessageQ), NULL);
            if (NULLPTR == messageQ->messageQID)
            {
                free(messageQ->osMessageQ.pool);
                free(messageQ);
                messageQ = NULL;
                status = PAL_ERR_GENERIC_FAILURE;
            }
            else
            {
                *messageQID = (palMessageQID_t)messageQ;
            }
        }
    }
    return status;      
}

palStatus_t pal_plat_osMessagePut(palMessageQID_t messageQID, uint32_t info, uint32_t timeout)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus platStatus = osOK;
    palMessageQ_t* messageQ = NULL;
    
    if(NULLPTR == messageQID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    messageQ = (palMessageQ_t*)messageQID;
    platStatus = osMessagePut((osMessageQId)messageQ->messageQID, info, timeout);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else
    {
        status = PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(platStatus);
    }

    return status;  
}

palStatus_t pal_plat_osMessageGet(palMessageQID_t messageQID, uint32_t timeout, uint32_t* messageValue)
{
    palStatus_t status = PAL_SUCCESS;
    osEvent event;
    palMessageQ_t* messageQ = NULL;

    if (NULLPTR == messageQID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    messageQ = (palMessageQ_t*)messageQID;
    event = osMessageGet((osMessageQId)messageQ->messageQID, timeout);
    
    if ((messageValue != NULL) && (osEventMessage == event.status))
    {
        *messageValue = event.value.v;
        status = PAL_SUCCESS;
    }
    else if ((osEventTimeout == event.status) || (osOK == event.status))
    {
        status = PAL_ERR_RTOS_TIMEOUT;
    }
    else if (osErrorParameter == event.status)
    {
        status = PAL_ERR_RTOS_PARAMETER;
    }

    return status;
}


palStatus_t pal_plat_osMessageQueueDestroy(palMessageQID_t* messageQID)
{
    palStatus_t status = PAL_SUCCESS;
    palMessageQ_t* messageQ = NULL;
    
    if(NULL == messageQID || NULLPTR == *messageQID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }   

    messageQ = (palMessageQ_t*)*messageQID;
    free(messageQ->osMessageQ.pool);
    free(messageQ);
    *messageQID = NULLPTR;
    return status;
}


int32_t pal_plat_osAtomicIncrement(int32_t* valuePtr, int32_t increment)
{
    if (increment >= 0)
    {
        return core_util_atomic_incr_u32((uint32_t*)valuePtr, increment);
    }
    else
    {
        return core_util_atomic_decr_u32((uint32_t*)valuePtr, 0 - increment);
    }
}


 void *pal_plat_malloc(size_t len)
{
	return malloc(len);
}


 void pal_plat_free(void * buffer)
{
	return free(buffer);
}

palStatus_t pal_plat_osRandomBuffer(uint8_t *randomBuf, size_t bufSizeBytes)
{
    palStatus_t status = PAL_SUCCESS;
    int32_t platStatus = 0;
    size_t actualOutputLen = 0;

    if (g_randInitiated)
    {
        if (bufSizeBytes < sizeof(g_randomBuffer))
        {
            memcpy(randomBuf, g_randomBuffer, bufSizeBytes);
        }
        else
        {
            memcpy(randomBuf, g_randomBuffer, sizeof(g_randomBuffer));
        }
    }
    else
    {
        if (bufSizeBytes <= sizeof(g_randomBuffer))
        {
            platStatus = mbedtls_hardware_poll(NULL /*Not used by the function*/, g_randomBuffer, sizeof(g_randomBuffer), &actualOutputLen);
            if (0 != platStatus || actualOutputLen != sizeof(g_randomBuffer))
            {
                status = PAL_ERR_RTOS_TRNG_FAILED;
            }
            else
            {
                memcpy(randomBuf, g_randomBuffer, bufSizeBytes);
                g_randInitiated = true;
            }
        }
        else
        {
            platStatus = mbedtls_hardware_poll(NULL /*Not used by the function*/, randomBuf, bufSizeBytes, &actualOutputLen);
            if (0 != platStatus || actualOutputLen != bufSizeBytes)
            {
                status = PAL_ERR_RTOS_TRNG_FAILED;
            }
            else
            {
                memcpy(g_randomBuffer, randomBuf, sizeof(g_randomBuffer));
                g_randInitiated = true;
            }
        }     
    }
    return status;
}

#endif //mbedOS version
