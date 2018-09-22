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


/*
    mbedOS latest version RTOS support
*/
#if defined(osRtxVersionAPI) && (osRtxVersionAPI >= 20000000)

#include "cmsis_os2.h" // Revision:    V2.1


#define PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(cmsisCode)\
    ((int32_t)((int32_t)cmsisCode + PAL_ERR_RTOS_ERROR_BASE))

typedef struct palThreadFuncWrapper{
    palTimerFuncPtr         realThreadFunc;
    void*                   realThreadArgs;
    uint32_t                threadIndex;
}palThreadFuncWrapper_t;

//! Thread structure
typedef struct palThread{
    palThreadID_t              threadID;
    uint32_t                   palThreadID;
    bool                       initialized;
    palThreadLocalStore_t*     threadStore; //! please see pal_rtos.h for documentation
    palThreadFuncWrapper_t     threadFuncWrapper;
    osThreadAttr_t             osThread;
    mbed_rtos_storage_thread_t osThreadStorage;
} palThread_t;

/*! Count number of created threads. Initiate to zero.
*/
PAL_PRIVATE uint32_t g_threadCounter = 0;
palThread_t g_palThreads[PAL_MAX_NUMBER_OF_THREADS] = {0};

//! Timer structure
typedef struct palTimer{
    palTimerID_t              timerID;
    osTimerAttr_t             osTimer;
    mbed_rtos_storage_timer_t osTimerStorage;
} palTimer_t;

//! Mutex structure
typedef struct palMutex{
    palMutexID_t              mutexID;
    osMutexAttr_t             osMutex;
    mbed_rtos_storage_mutex_t osMutexStorage;
}palMutex_t;

//! Semaphore structure
typedef struct palSemaphore{
    palSemaphoreID_t              semaphoreID;
    osSemaphoreAttr_t             osSemaphore;
    mbed_rtos_storage_semaphore_t osSemaphoreStorage;
}palSemaphore_t;

//! Memoey Pool structure
typedef struct palMemPool{
    palMemoryPoolID_t            memoryPoolID;
    osMemoryPoolAttr_t           osPool;
    mbed_rtos_storage_mem_pool_t osPoolStorage;
    uint32_t                     blockSize;
}palMemoryPool_t;

//! Message Queue structure
typedef struct palMessageQ{
    palMessageQID_t               messageQID;
    osMessageQueueAttr_t          osMessageQ;
    mbed_rtos_storage_msg_queue_t osMessageQStorage;
}palMessageQ_t;

//! thread cleanup timer argument structure
typedef struct palThreadCleanupData {
    palTimerID_t timerID;
    palThreadID_t threadToCleanUp;
    void* threadStackMem;
}palThreadCleanupData_t;



inline PAL_PRIVATE int mapThreadPriorityToPlatSpecific(palThreadPriority_t priority)
{
    int adjustedPriority = -1;

    switch (priority)
    {
        case PAL_osPriorityIdle:
            adjustedPriority = osPriorityIdle;
            break;

        case PAL_osPriorityLow:
            adjustedPriority = osPriorityLow;
            break;

        case PAL_osPriorityBelowNormal:
            adjustedPriority = osPriorityBelowNormal;
            break;

        case PAL_osPriorityNormal:
            adjustedPriority = osPriorityNormal;
            break;

        case PAL_osPriorityAboveNormal:
            adjustedPriority = osPriorityAboveNormal;
            break;

        case PAL_osPriorityHigh:
            adjustedPriority = osPriorityHigh;
            break;

        case PAL_osPriorityRealtime:
            adjustedPriority = osPriorityRealtime;
            break;

        case PAL_osPriorityError:
        	adjustedPriority = osPriorityError;
        	break;

        default:
            adjustedPriority = osPriorityNone;
            break;
    }

    return adjustedPriority;
}


inline PAL_PRIVATE palThreadPriority_t mapThreadPriorityToPalGeneric(int priority)
{
    palThreadPriority_t adjustedPriority = PAL_osPriorityError;

    switch (priority)
    {
        case osPriorityIdle:
            adjustedPriority = PAL_osPriorityIdle;
            break;

        case osPriorityLow :
            adjustedPriority = PAL_osPriorityLow;        
            break;

        case osPriorityBelowNormal :
            adjustedPriority = PAL_osPriorityBelowNormal;        
            break;

        case osPriorityNormal :
            adjustedPriority = PAL_osPriorityNormal;        
            break;

        case osPriorityAboveNormal :
            adjustedPriority = PAL_osPriorityAboveNormal;        
            break;

        case osPriorityHigh :
            adjustedPriority = PAL_osPriorityHigh;        
            break;

        case osPriorityRealtime :
            adjustedPriority = PAL_osPriorityRealtime;        
            break;

        case osPriorityError:
        default:
            adjustedPriority = PAL_osPriorityError;        
            break;
    }

    return adjustedPriority;
}


inline PAL_PRIVATE void setDefaultThreadValues(palThread_t* thread)
{
#if PAL_UNIQUE_THREAD_PRIORITY
    palThreadPriority_t threadGenericPriority = mapThreadPriorityToPalGeneric(thread->osThread.priority);      
    g_palThreadPriorities[threadGenericPriority+PRIORITY_INDEX_OFFSET] = 0;
#endif //PAL_UNIQUE_THREAD_PRIORITY     
    thread->threadStore = NULL;
    thread->threadFuncWrapper.realThreadArgs = NULL;
    thread->threadFuncWrapper.realThreadFunc = NULL;
    thread->threadFuncWrapper.threadIndex = 0;

    thread->threadID = NULLPTR;
    thread->palThreadID = 0;
    //! This line should be last thing to be done in this function.
    //! in order to prevent double accessing the same index between
    //! this function and the threadCreate function.
    thread->initialized = false;
}

/*! Clean thread data from the global thread data base (g_palThreads). Thread Safe API
*
* @param[in] index: the index in the data base to be cleaned.
*/
PAL_PRIVATE void threadCleanUp( uint32_t threadID)
{
	uint32_t status = PAL_SUCCESS;
    uint32_t threadIndex = PAL_GET_THREAD_INDEX(threadID);

    status = pal_osMutexWait(g_palThreadInitMutex, PAL_RTOS_WAIT_FOREVER);
    if (PAL_SUCCESS != status)
    {
    	 PAL_LOG(ERR,"thread cleanup: mutex wait failed!\n");
    }
    else{
		if ((NULL != g_palThreads) && (threadIndex < PAL_MAX_NUMBER_OF_THREADS) && (g_palThreads[threadIndex].palThreadID == threadID))
		{
			setDefaultThreadValues(&g_palThreads[threadIndex]);
		}

		status = pal_osMutexRelease(g_palThreadInitMutex);
	    if (PAL_SUCCESS != status)
	    {
	    	 PAL_LOG(ERR,"thread cleanup: mutex release failed!\n");
	    }
    }
    return;
}



/*! Thread Cleanup timer. This is a timer funciton dedicated to deallocating the thread stack in case if it exits naturally (not via thread Terminate).
*
*   @param[in] arg: data structure which contains the data about the thread to clean up.
*/
PAL_PRIVATE void threadCleanupTimer(const void* arg)
{
    osThreadState_t threadState = osThreadError;
    palThreadCleanupData_t* threadCleanupData = (palThreadCleanupData_t*) arg;
    palTimerID_t localtimerID = threadCleanupData->timerID;

    threadState = osThreadGetState((osThreadId_t)(threadCleanupData->threadToCleanUp));
    if ((threadState == osThreadTerminated) || (threadState == osThreadInactive)) // thread has ended, can clean up.
    {
        free(threadCleanupData->threadStackMem); // free the thread stack memory.
        free(threadCleanupData); // free the thread cleanup data.
        pal_osTimerDelete(&localtimerID);
    }
    else // Thread not ended yet, wait another PAL_RTOS_THREAD_CLEANUP_TIMER_MILISEC ms.
    {
        if (osThreadError == threadState)
        {
            PAL_LOG(DBG,"thread Cleanup Timer: error getting thread status\n");
        }
        else
        {
            palStatus_t status = pal_osTimerStart(threadCleanupData->timerID, PAL_RTOS_THREAD_CLEANUP_TIMER_MILISEC);
            if (PAL_SUCCESS != status)
            {
                PAL_LOG(ERR,"thread Cleanup Timer: timer start failed -  thread stack memory leak likely!\n");
            }
        }
    }  

}



/*! Thread wrapper function, this function will be set as the thread function (for every thread)
*   and it will get as an argument the real data about the thread and call the REAL thread function
*   with the REAL argument.
*
*   @param[in] arg: data structure which contains the real data about the thread.
*/
PAL_PRIVATE void threadFunctionWrapper(void* arg)
{
    palThreadFuncWrapper_t* threadWrapper = (palThreadFuncWrapper_t*)arg;
    palThreadCleanupData_t* threadCleanupData = NULL;
    palTimerID_t localTimerID = 0;

    if (NULL != threadWrapper)
    {
        if(g_palThreads[threadWrapper->threadIndex].threadID == NULLPTR)
        {
        	g_palThreads[threadWrapper->threadIndex].threadID = (palThreadID_t)osThreadGetId();
        }
        threadWrapper->realThreadFunc(threadWrapper->realThreadArgs);

        threadCleanupData = (palThreadCleanupData_t*)malloc(sizeof(palThreadCleanupData_t));
        if (NULL == threadCleanupData)
        {
            PAL_LOG(ERR,"thread cleanup: timer data allocation failed -  thread stack memory leak likely!\n");
        }
        else
        {
            palStatus_t status = pal_osTimerCreate(threadCleanupTimer, threadCleanupData, palOsTimerOnce, &localTimerID);
            if (PAL_SUCCESS != status)
            {
                PAL_LOG(ERR,"thread cleanup: timer create failed -  thread stack memory leak likely!\n");
            }
            else
            {
                threadCleanupData->timerID = localTimerID;
                threadCleanupData->threadToCleanUp = g_palThreads[threadWrapper->threadIndex].threadID;
                threadCleanupData->threadStackMem = g_palThreads[threadWrapper->threadIndex].osThread.stack_mem;
                status = pal_osTimerStart(localTimerID, PAL_RTOS_THREAD_CLEANUP_TIMER_MILISEC);
                if (PAL_SUCCESS != status)
                {
                    PAL_LOG(ERR,"thread cleanup: timer start failed -  thread stack memory leak likely!\n");
                }
            }
        }
        
        threadCleanUp(g_palThreads[threadWrapper->threadIndex].palThreadID); // clean up everything except deallocating stack
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

	 memset(g_palThreads,0,sizeof(palThread_t) * PAL_MAX_NUMBER_OF_THREADS);

    //Add implicit the running task as PAL main
    g_palThreads[0].initialized = true;
    g_palThreads[0].threadID = (palThreadID_t)osThreadGetId();
    g_palThreads[0].osThread.stack_mem = NULL;

    pal_osAtomicIncrement((int32_t*)&g_threadCounter,1);
    //palThreadID = 24 bits for thread counter  + lower 8 bits for thread index (= 0).
    g_palThreads[0].palThreadID = (g_threadCounter << 8 );

    return status;
}



palStatus_t pal_plat_RTOSDestroy(void)
{
    return PAL_SUCCESS;
}


palStatus_t pal_plat_osDelay(uint32_t milliseconds)
{
    palStatus_t status;
    osStatus_t platStatus = osDelay(milliseconds);
    if (osOK == platStatus)
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
    result = osKernelGetTickCount();
    return result;
}

uint64_t pal_plat_osKernelSysTickMicroSec(uint64_t microseconds)
{
    uint64_t result;
    result =  (((uint64_t)microseconds * (osKernelGetTickFreq())) / 1000000);

    return result;
}

uint64_t pal_plat_osKernelSysTickFrequency()
{
    return osKernelGetTickFreq();
}

palStatus_t pal_plat_osThreadCreate(palThreadFuncPtr function, void* funcArgument, palThreadPriority_t priority, uint32_t stackSize, uint32_t* stackPtr, palThreadLocalStore_t* store, palThreadID_t* threadID)
{
    palStatus_t status = PAL_SUCCESS;
    uint32_t firstAvailableThreadIndex = PAL_MAX_NUMBER_OF_THREADS;
    uint32_t i;
    uint32_t *stackAllocPtr = NULL;
    osThreadId_t osThreadID = NULL;
    uint32_t localPalThreadID = 0;


    if (NULL == threadID || NULL == function || 0 == stackSize || priority > PAL_osPriorityRealtime)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

	status = pal_osMutexWait(g_palThreadInitMutex, PAL_RTOS_WAIT_FOREVER);
	if (PAL_SUCCESS == status)
	{
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

		if (PAL_SUCCESS == status)
		{
			stackAllocPtr =  (uint32_t*)malloc(stackSize);
			if(NULL == stackAllocPtr)
			{
				status = PAL_ERR_RTOS_RESOURCE;
			}
		}

		if (PAL_SUCCESS != status)
		{
			// release mutex if error.
			status = pal_osMutexRelease(g_palThreadInitMutex);
		}
		else
		{
			g_palThreads[firstAvailableThreadIndex].threadStore = store;
			g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadArgs = funcArgument;
			g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadFunc = function;
			g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.threadIndex = firstAvailableThreadIndex;
			g_palThreads[firstAvailableThreadIndex].osThread.priority = (osPriority_t)mapThreadPriorityToPlatSpecific(priority);
			g_palThreads[firstAvailableThreadIndex].osThread.stack_size = stackSize;
			g_palThreads[firstAvailableThreadIndex].osThread.stack_mem = stackAllocPtr;
			g_palThreads[firstAvailableThreadIndex].osThread.cb_mem =  &(g_palThreads[firstAvailableThreadIndex].osThreadStorage);
			g_palThreads[firstAvailableThreadIndex].osThread.cb_size = sizeof(g_palThreads[firstAvailableThreadIndex].osThreadStorage);
			g_palThreads[firstAvailableThreadIndex].palThreadID = ((firstAvailableThreadIndex) + ((pal_osAtomicIncrement((int32_t*)&g_threadCounter, 1)) << 8)); //palThreadID = 24 bits for thread counter + lower 8 bits for thread index.
			memset(&(g_palThreads[firstAvailableThreadIndex].osThreadStorage), 0, sizeof(g_palThreads[firstAvailableThreadIndex].osThreadStorage));

			localPalThreadID = g_palThreads[firstAvailableThreadIndex].palThreadID; // save Thread ID value localy in case thread exists (and table is cleared) before funciton completes.

			// release mutex before thread creation .
			status = pal_osMutexRelease(g_palThreadInitMutex);

			if (PAL_SUCCESS == status)
			{
				osThreadID = osThreadNew(threadFunctionWrapper, &g_palThreads[firstAvailableThreadIndex].threadFuncWrapper, &g_palThreads[firstAvailableThreadIndex].osThread);
				g_palThreads[firstAvailableThreadIndex].threadID = (palThreadID_t)osThreadID;
				if(NULL == osThreadID)
				{
					//! in case of error in the thread creation, reset the data of the given index in the threads array.
					threadCleanUp(g_palThreads[firstAvailableThreadIndex].palThreadID);

					if (NULL != g_palThreads[firstAvailableThreadIndex].osThread.stack_mem)
					{
						free(g_palThreads[firstAvailableThreadIndex].osThread.stack_mem);
						g_palThreads[firstAvailableThreadIndex].osThread.stack_mem = NULL;
					}
					status = PAL_ERR_GENERIC_FAILURE;
					*threadID = PAL_INVALID_THREAD;
				}
				else
				{
					*threadID = localPalThreadID; // here we use the thread may have already exited and cleared the table so local copy of ID is used.
				}
			}
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
    osStatus_t platStatus = osOK;
    osThreadState_t threadState = osThreadError;
    uint32_t threadIndex = PAL_GET_THREAD_INDEX(*threadID);

    if ((PAL_INVALID_THREAD == *threadID) || (threadIndex >= PAL_MAX_NUMBER_OF_THREADS))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    // if thread exited or was terminated already return success.
    if ((g_palThreads[threadIndex].palThreadID == 0 ) ||  // thread already exited 
        (g_palThreads[threadIndex].palThreadID != *threadID)|| // thread already exited and a new thread was created at the same index.
        (g_palThreads[threadIndex].threadID == (palThreadID_t)PAL_INVALID_THREAD))  // thread was terminsated.
    {
        return PAL_SUCCESS;
    }

    if ((palThreadID_t)osThreadGetId() != g_palThreads[threadIndex].threadID)
    {//Kill only if not trying to kill from running task
    	if (g_palThreads[threadIndex].initialized)
    	{
            if (g_palThreads[threadIndex].threadID != NULLPTR)
    		{
                threadState = osThreadGetState((osThreadId_t)(g_palThreads[threadIndex].threadID));
                if ((threadState != osThreadTerminated) && (threadState != osThreadError) && (threadState != osThreadInactive))
                {
                    platStatus = osThreadTerminate((osThreadId_t)(g_palThreads[threadIndex].threadID));
                }
            }

            if (platStatus != osErrorISR) // osErrorISR: osThreadTerminate cannot be called from interrupt service routines.
            {
                threadCleanUp( *threadID);
                if (NULL != g_palThreads[threadIndex].osThread.stack_mem)
                {
                    free(g_palThreads[threadIndex].osThread.stack_mem);
                    g_palThreads[threadIndex].osThread.stack_mem = NULL;
                }
                *threadID = PAL_INVALID_THREAD;
                status = PAL_SUCCESS;
            }
            else
            {
                status = PAL_ERR_RTOS_ISR;
            }
        }
        else
        {
            // thread already tminated and cleaned up
            status = PAL_SUCCESS;
        }
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
        timer->osTimer.name = NULL;
        timer->osTimer.attr_bits = 0;
        timer->osTimer.cb_mem = &timer->osTimerStorage;
        timer->osTimer.cb_size = sizeof(timer->osTimerStorage);
        memset(&timer->osTimerStorage, 0, sizeof(timer->osTimerStorage));
    
        timer->timerID = (uintptr_t)osTimerNew((osTimerFunc_t)function, (osTimerType_t)timerType, funcArgument, &timer->osTimer);
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
    osStatus_t platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if (NULLPTR == timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)timerID;
    platStatus = osTimerStart((osTimerId_t)timer->timerID, millisec);
    if (osOK == (osStatus_t)platStatus)
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
    osStatus_t platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if(NULLPTR == timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)timerID;
    platStatus = osTimerStop((osTimerId_t)timer->timerID);
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
    osStatus_t platStatus = osOK;
    palTimer_t* timer = NULL;
    
    if(NULL == timerID || NULLPTR == *timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    timer = (palTimer_t*)*timerID;
    platStatus = osTimerDelete((osTimerId_t)timer->timerID);
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
        mutex->osMutex.name = NULL;
        mutex->osMutex.attr_bits = osMutexRecursive | osMutexRobust;
        mutex->osMutex.cb_mem = &mutex->osMutexStorage;
        mutex->osMutex.cb_size = sizeof(mutex->osMutexStorage);
        memset(&mutex->osMutexStorage, 0, sizeof(mutex->osMutexStorage));

        mutex->mutexID = (uintptr_t)osMutexNew(&mutex->osMutex);
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
    osStatus_t platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULLPTR == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)mutexID;
    platStatus = osMutexAcquire((osMutexId_t)mutex->mutexID, millisec);
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
    osStatus_t platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULLPTR == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)mutexID;
    platStatus = osMutexRelease((osMutexId_t)mutex->mutexID);
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
    osStatus_t platStatus = osOK;
    palMutex_t* mutex = NULL;
    
    if(NULL == mutexID || NULLPTR == *mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    mutex = (palMutex_t*)*mutexID;
    platStatus = osMutexDelete((osMutexId_t)mutex->mutexID);
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
        semaphore->osSemaphore.cb_mem = &semaphore->osSemaphoreStorage;
        semaphore->osSemaphore.cb_size = sizeof(semaphore->osSemaphoreStorage);
        memset(&semaphore->osSemaphoreStorage, 0, sizeof(semaphore->osSemaphoreStorage));

        semaphore->semaphoreID = (uintptr_t)osSemaphoreNew(PAL_MAX_SEMAPHORE_COUNT, count, &semaphore->osSemaphore);
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
    osStatus_t platStatus;
    if(NULLPTR == semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }   

    semaphore = (palSemaphore_t*)semaphoreID;
    platStatus = osSemaphoreAcquire((osSemaphoreId_t)semaphore->semaphoreID, millisec);

    if (osErrorTimeout == platStatus)
    {
        status = PAL_ERR_RTOS_TIMEOUT;
    }
    else if (platStatus != osOK)
    {
        status = PAL_ERR_RTOS_PARAMETER;
    }

    if (NULL != countersAvailable)
    {
        *countersAvailable = osSemaphoreGetCount((osSemaphoreId_t)semaphore->semaphoreID);
    }
    return status;
}

palStatus_t pal_plat_osSemaphoreRelease(palSemaphoreID_t semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus_t platStatus = osOK;
    palSemaphore_t* semaphore = NULL;

    if(NULLPTR == semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    semaphore = (palSemaphore_t*)semaphoreID;
    platStatus = osSemaphoreRelease((osSemaphoreId_t)semaphore->semaphoreID);
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
    osStatus_t platStatus = osOK;
    palSemaphore_t* semaphore = NULL;
    
    if(NULL == semaphoreID || NULLPTR == *semaphoreID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    semaphore = (palSemaphore_t*)*semaphoreID;
    platStatus = osSemaphoreDelete((osSemaphoreId_t)semaphore->semaphoreID);
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
        memoryPool->blockSize = blockSize;
        memoryPool->osPool.name = NULL;
        memoryPool->osPool.attr_bits = 0;
        memoryPool->osPool.cb_mem = &memoryPool->osPoolStorage;
        memoryPool->osPool.cb_size = sizeof(memoryPool->osPoolStorage);
        memset(&memoryPool->osPoolStorage, 0, sizeof(memoryPool->osPoolStorage));
        memoryPool->osPool.mp_size = blockSize * blockCount;
        memoryPool->osPool.mp_mem = (uint32_t*)malloc(memoryPool->osPool.mp_size);
        if (NULL == memoryPool->osPool.mp_mem)
        {
            free(memoryPool);
            *memoryPoolID = NULLPTR;
            status = PAL_ERR_NO_MEMORY;
        }
        else
        {
            memset(memoryPool->osPool.mp_mem, 0, memoryPool->osPool.mp_size);

            memoryPool->memoryPoolID = (uintptr_t)osMemoryPoolNew(blockCount, blockSize, &memoryPool->osPool);
            if (NULLPTR == memoryPool->memoryPoolID)
            {
                free(memoryPool->osPool.mp_mem);
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
    result = osMemoryPoolAlloc((osMemoryPoolId_t)memoryPool->memoryPoolID, 0);

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
    result = osMemoryPoolAlloc((osMemoryPoolId_t)memoryPool->memoryPoolID, 0);
    if (NULLPTR != result)
    {
        memset(result, 0, memoryPool->blockSize);
    }

    return result;  
}

palStatus_t pal_plat_osPoolFree(palMemoryPoolID_t memoryPoolID, void* block)
{
    palStatus_t status = PAL_SUCCESS;
    osStatus_t platStatus = osOK;
    palMemoryPool_t* memoryPool = NULL;
    
    if(NULLPTR == memoryPoolID || NULL == block)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    memoryPool = (palMemoryPool_t*)memoryPoolID;
    platStatus = osMemoryPoolFree((osMemoryPoolId_t)memoryPool->memoryPoolID, block);
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
    free(memoryPool->osPool.mp_mem);
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
        messageQ->osMessageQ.name = NULL;
        messageQ->osMessageQ.attr_bits = 0;
        messageQ->osMessageQ.cb_size = sizeof(messageQ->osMessageQStorage);
        messageQ->osMessageQ.cb_mem = &messageQ->osMessageQStorage;
        memset(&messageQ->osMessageQStorage, 0, sizeof(messageQ->osMessageQStorage));
        messageQ->osMessageQ.mq_size = (sizeof(uint32_t) + sizeof(mbed_rtos_storage_message_t)) * messageQCount ;
        messageQ->osMessageQ.mq_mem = (uint32_t*)malloc(messageQ->osMessageQ.mq_size);
        if (NULL == messageQ->osMessageQ.mq_mem)
        {
            free(messageQ);
            messageQ = NULL;
            status = PAL_ERR_NO_MEMORY;
        }
        else
        {
            memset(messageQ->osMessageQ.mq_mem, 0, messageQ->osMessageQ.mq_size);

            messageQ->messageQID = (uintptr_t)osMessageQueueNew(messageQCount, sizeof(uint32_t), &messageQ->osMessageQ);
            if (NULLPTR == messageQ->messageQID)
            {
                free(messageQ->osMessageQ.mq_mem);
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
    osStatus_t platStatus = osOK;
    palMessageQ_t* messageQ = NULL;
    
    if(NULLPTR == messageQID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    messageQ = (palMessageQ_t*)messageQID;
    platStatus = osMessageQueuePut((osMessageQueueId_t)messageQ->messageQID, (void *)&info, 0, timeout);
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
    osStatus_t platStatus;
    palMessageQ_t* messageQ = NULL;

    if (NULLPTR == messageQID || NULLPTR == messageValue)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    messageQ = (palMessageQ_t*)messageQID;
    platStatus = osMessageQueueGet((osMessageQueueId_t)messageQ->messageQID, messageValue, NULL, timeout);
    if (osOK == platStatus)
    {
        status = PAL_SUCCESS;
    }
    else if (osErrorTimeout == platStatus)
    {
        status = PAL_ERR_RTOS_TIMEOUT;
    }
    else if (osOK != platStatus)
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
    free(messageQ->osMessageQ.mq_mem);
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
    platStatus = mbedtls_hardware_poll(NULL /*Not used by the function*/, randomBuf, bufSizeBytes, &actualOutputLen);
    if ((0 != platStatus) || (actualOutputLen != bufSizeBytes))
    {
        status = PAL_ERR_RTOS_TRNG_FAILED;
    }
    return status;
}

#endif
