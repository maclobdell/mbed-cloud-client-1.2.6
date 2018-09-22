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

/* PAL-RTOS porting for FreeRTOS-8.1.2
*  This is porting code for PAL RTOS APIS for 
*  FreeRTOS-8.1.2 version.
*/

#include "board.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"


#include "pal_types.h"
#include "pal_rtos.h"
#include "pal_plat_rtos.h"
#include "pal_errors.h"
#include "stdlib.h"


#define PAL_RTOS_TRANSLATE_CMSIS_ERROR_CODE(cmsisCode)\
    ((int32_t)(cmsisCode + PAL_ERR_RTOS_ERROR_BASE))

#define PAL_TICK_TO_MILLI_FACTOR 1000


extern palStatus_t pal_plat_getRandomBufferFromHW(uint8_t *randomBuf, size_t bufSizeBytes);

/////////////////////////STATIC FUNCTION///////////////////////////
/*! Get IPSR Register
*
* @param[in] Void
* \returns uint32 - the content of the IPSR Register.
*
*/
PAL_PRIVATE PAL_INLINE uint32_t pal_plat_GetIPSR(void);
/////////////////////////END STATIC FUNCTION///////////////////////////

typedef struct palThreadFuncWrapper{
	palTimerFuncPtr         realThreadFunc;
	void*                   realThreadArgs;
	uint32_t                threadIndex;
}palThreadFuncWrapper_t;

//! Thread structure
typedef struct palThread{
	bool                    initialized;
	bool                    running;
	palThreadLocalStore_t*  threadStore; //! please see pal_rtos.h for documentation
	palThreadFuncWrapper_t  threadFuncWrapper;
	TaskHandle_t            threadID;
	uint32_t                palThreadID;
	EventGroupHandle_t      eventGroup;
	palThreadPriority_t     priority;
	bool                    taskCompleted; //The task has completed and exit
} palThread_t;

PAL_PRIVATE palThread_t g_palThreads[PAL_MAX_NUMBER_OF_THREADS] = {0};

//! Timer structure
typedef struct palTimer{
	palTimerID_t            timerID;
	//    uint32_t                internalTimerData[PAL_TIMER_DATA_SIZE];  ///< pointer to internal data
	TimerCallbackFunction_t function;
	void*                   functionArgs;
	uint32_t                timerType;
} palTimer_t;

//! Mutex structure
typedef struct palMutex{
	palMutexID_t            mutexID;
}palMutex_t;

//! Semaphore structure
typedef struct palSemaphore{
	palSemaphoreID_t        semaphoreID;
	uint32_t                maxCount;
}palSemaphore_t;

/*! Count number of created threads. Initiate to zero.
*/
PAL_PRIVATE uint32_t g_threadCounter = 0;

//! Message Queue structure
typedef struct palMessageQ{
	palMessageQID_t            messageQID;
}palMessageQ_t;

//! Memory Pool structure
typedef struct palMemoryPool
{
	void*               start;
	uint32_t            blockCount;
	uint32_t            blockSize;
	uint8_t*            allocated;
} palMemoryPool_t;



PAL_PRIVATE PAL_INLINE uint32_t pal_plat_GetIPSR(void)
{
	uint32_t result;

#if defined (__CC_ARM)
	__asm volatile
	{
		MRS result, ipsr
	}
#elif defined (__GNUC__)
	__asm volatile ("MRS %0, ipsr" : "=r" (result) );
#endif

	return(result);
}



inline PAL_PRIVATE void setDefaultThreadValues(palThread_t* thread)
{
#if PAL_UNIQUE_THREAD_PRIORITY
	g_palThreadPriorities[thread->priority + PRIORITY_INDEX_OFFSET] = 0;
#endif //PAL_UNIQUE_THREAD_PRIORITY
	thread->threadStore = NULL;
	thread->threadFuncWrapper.realThreadArgs = NULL;
	thread->threadFuncWrapper.realThreadFunc = NULL;
	thread->threadFuncWrapper.threadIndex = PAL_MAX_NUMBER_OF_THREADS;
	thread->threadID = NULL;
	thread->taskCompleted = false;
	thread->palThreadID = 0;
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
	uint32_t status = PAL_SUCCESS;
	palThread_t* threadsDB = (palThread_t*)dbPointer;
	uint32_t threadIndex = PAL_GET_THREAD_INDEX(index);

	status = pal_osMutexWait(g_palThreadInitMutex, PAL_RTOS_WAIT_FOREVER);
    if (PAL_SUCCESS != status)
    {
    	 PAL_LOG(ERR,"thread cleanup: mutex wait failed!\n");
    }
    else{
		if ((NULL != dbPointer) && (threadIndex < PAL_MAX_NUMBER_OF_THREADS) && (threadsDB[threadIndex].palThreadID == index))
		{
			setDefaultThreadValues(&threadsDB[threadIndex]);
		}

		status = pal_osMutexRelease(g_palThreadInitMutex);
	    if (PAL_SUCCESS != status)
	    {
	    	 PAL_LOG(ERR,"thread cleanup: mutex release failed!\n");
	    }
    }
    return;
}

/*! Thread wrapper function, this function will be set as the thread function (for every thread)
 *   and it will get as an argument the real data about the thread and call the REAL thread function
 *   with the REAL argument. Once the REAL thread function finished, \ref pal_threadClean() will be called.
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
	    	g_palThreads[threadWrapper->threadIndex].threadID =  xTaskGetCurrentTaskHandle();
	    }

		threadWrapper->realThreadFunc(threadWrapper->realThreadArgs);
		g_palThreads[threadWrapper->threadIndex].taskCompleted = true;
		threadCleanUp(g_palThreads, g_palThreads[threadWrapper->threadIndex].palThreadID);
	}

	vTaskDelete( NULL );
}


palStatus_t pal_plat_RTOSInitialize(void* opaqueContext)
{
	palStatus_t status = PAL_SUCCESS;

	//Clean thread tables
	memset(g_palThreads,0,sizeof(palThread_t) * PAL_MAX_NUMBER_OF_THREADS);

	//Add implicit the running task as PAL main
	g_palThreads[0].initialized = true;
	g_palThreads[0].threadID =  xTaskGetCurrentTaskHandle();

	pal_osAtomicIncrement((int32_t*)&g_threadCounter,1);
    //palThreadID = 24 bits for thread counter + lower 8 bits for thread index (= 0).
	g_palThreads[0].palThreadID = (g_threadCounter << 8 );

    return status;
}

palStatus_t pal_plat_RTOSDestroy(void)
{
	return PAL_SUCCESS;
}

palStatus_t pal_plat_osDelay(uint32_t milliseconds)
{
	vTaskDelay(milliseconds / portTICK_PERIOD_MS);
	return PAL_SUCCESS;
}


uint64_t pal_plat_osKernelSysTick()
{

	uint64_t result;
	if (pal_plat_GetIPSR() != 0)
	{
		result = xTaskGetTickCountFromISR();
	}
	else
	{
		result = xTaskGetTickCount();
	}
	return result;
}

uint64_t pal_plat_osKernelSysTickMicroSec(uint64_t microseconds)
{
	uint64_t sysTicks = microseconds * configTICK_RATE_HZ / (PAL_TICK_TO_MILLI_FACTOR * PAL_TICK_TO_MILLI_FACTOR);
	return sysTicks;
}

uint64_t pal_plat_osKernelSysTickFrequency()
{
	return configTICK_RATE_HZ;
}

palStatus_t pal_plat_osThreadCreate(palThreadFuncPtr function, void* funcArgument, palThreadPriority_t priority,
		uint32_t stackSize, uint32_t* stackPtr, palThreadLocalStore_t* store,
		palThreadID_t* threadID)
{
	palStatus_t status = PAL_SUCCESS;
	BaseType_t res;
	uint32_t firstAvailableThreadIndex = PAL_MAX_NUMBER_OF_THREADS;
	uint32_t i;
	TaskHandle_t osThreadID = NULL;
    uint32_t localPalThreadID = 0;

	if((NULL == threadID) || (NULL == function) || (priority > PAL_osPriorityRealtime) || (0 == stackSize))
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
			g_palThreads[firstAvailableThreadIndex].initialized = true;
			g_palThreads[firstAvailableThreadIndex].priority = priority;
			g_palThreads[firstAvailableThreadIndex].palThreadID = ((firstAvailableThreadIndex)+((pal_osAtomicIncrement((int32_t*)&g_threadCounter, 1)) << 8)); //palThreadID = 24 bits for thread counter + lower 8 bits for thread index.
			localPalThreadID = g_palThreads[firstAvailableThreadIndex].palThreadID;

			// release mutex before thread creation .
			status = pal_osMutexRelease(g_palThreadInitMutex);

			if (PAL_SUCCESS == status)
			{
				//Note: the stack in this API handled as an array of "StackType_t" which can be in different sized for different ports.
				//      in this specific port of (8.1.2) the "StackType_t" is defined to 4-bytes this is why we divided the "stackSize" parameter on "sizeof(uint32_t)".
				//      inside freeRTOS code, the size calculated according to this formula: "( size_t ) usStackDepth ) * sizeof( StackType_t )" where "usStackDepth" is
				//      equal to "stackSize  / sizeof(uint32_t)".
				res = xTaskGenericCreate((TaskFunction_t)threadFunctionWrapper,
						"palTask",
						stackSize  / sizeof(uint32_t),
						&g_palThreads[firstAvailableThreadIndex].threadFuncWrapper,
						priority + PRIORITY_INDEX_OFFSET,
						&osThreadID,
						NULL, //if Stack pointer NULL then allocate stack according to stack size
						NULL);


				if(pdPASS == res)
				{
					*threadID = localPalThreadID;
					g_palThreads[firstAvailableThreadIndex].threadID = osThreadID;
				}
				else
				{
					//! in case of error in the thread creation, reset the data of the given index in the threads array.
					threadCleanUp(g_palThreads, localPalThreadID);
					*threadID = PAL_INVALID_THREAD;
					PAL_LOG(ERR, "Rtos thread create failure");
					status = PAL_ERR_GENERIC_FAILURE;
				}
			}
		}
	}
	return status;
}

palThreadID_t pal_plat_osThreadGetId(void)
{
	int i = 0;
	TaskHandle_t osThreadID =xTaskGetCurrentTaskHandle();
	palThreadID_t ret = PAL_INVALID_THREAD;

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
	palStatus_t status = PAL_SUCCESS;
    uint32_t threadIndex = PAL_GET_THREAD_INDEX(*threadID);

    if ((PAL_INVALID_THREAD == *threadID) || (threadIndex >= PAL_MAX_NUMBER_OF_THREADS))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    // if thread exited or was terminated already return success.
    if ((g_palThreads[threadIndex].palThreadID == 0) ||  // thread already exited 
        (g_palThreads[threadIndex].palThreadID != *threadID) || // thread already exited and a new thread was created at the same index.
        (g_palThreads[threadIndex].threadID == (TaskHandle_t)PAL_INVALID_THREAD))  // thread was terminated.
    {
        return status;
    }

	if((xTaskGetCurrentTaskHandle() != g_palThreads[threadIndex].threadID))
	{//Kill only if not trying to kill from running task
		if (g_palThreads[threadIndex].initialized)
		{
			if ((g_palThreads[threadIndex].threadID != NULL) && ( g_palThreads[threadIndex].taskCompleted == false))
			{
				vTaskDelete(g_palThreads[threadIndex].threadID);
			}
			threadCleanUp(g_palThreads, *threadID);
		}
		*threadID = PAL_INVALID_THREAD;
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


PAL_PRIVATE palTimer_t* s_timerArrays[PAL_MAX_NUM_OF_TIMERS] = {0};

PAL_PRIVATE void pal_plat_osTimerWarpperFunction( TimerHandle_t xTimer )
{
	int i;
	palTimer_t* timer = NULL;
	for(i=0 ; i< PAL_MAX_NUM_OF_TIMERS ; i++)
	{
		if (s_timerArrays[i]->timerID == (palTimerID_t)xTimer)
		{
			timer = s_timerArrays[i];
			timer->function(timer->functionArgs);

		}
	}
}

palStatus_t pal_plat_osTimerCreate(palTimerFuncPtr function, void* funcArgument, palTimerType_t timerType, palTimerID_t* timerID)
{
	palStatus_t status = PAL_SUCCESS;
	palTimer_t* timer = NULL;
	int i;
	if(NULL == timerID || NULL == function)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	timer = (palTimer_t*)malloc(sizeof(palTimer_t));

	if (NULL == timer)
	{
		status = PAL_ERR_NO_MEMORY;
	}
	else
	{
		memset(timer,0,sizeof(palTimer_t));
	}

	if (PAL_SUCCESS == status)
	{
		for (i=0; i< PAL_MAX_NUM_OF_TIMERS; i++)
		{
			if (s_timerArrays[i] == NULL)
			{
				s_timerArrays[i] = timer;
				break;
			}
		}
		if (PAL_MAX_NUM_OF_TIMERS == i)
		{
			status = PAL_ERR_NO_MEMORY;
		}
		if (PAL_SUCCESS == status)
		{
			timer->function = (TimerCallbackFunction_t)function;
			timer->functionArgs = funcArgument;
			timer->timerType = timerType;

			timer->timerID = (palTimerID_t)xTimerCreate(
					"timer",
					1, // xTimerPeriod - cannot be '0'
					(const TickType_t)timerType, // 0 = osTimerOnce, 1 = osTimerPeriodic
					NULL,
					(TimerCallbackFunction_t)pal_plat_osTimerWarpperFunction
			);
		}
		if (NULLPTR == timer->timerID)
		{
			free(timer);
			timer = NULLPTR;
			PAL_LOG(ERR, "Rtos timer create failure");
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
	palTimer_t* timer = NULL;

	if (NULLPTR == timerID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	timer = (palTimer_t*)timerID;

	if (pal_plat_GetIPSR() != 0)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		status = xTimerChangePeriodFromISR(
				(TimerHandle_t)(timer->timerID),
				(millisec / portTICK_PERIOD_MS),
				&pxHigherPriorityTaskWoken
		);
	}
	else
	{
		status =  xTimerChangePeriod((TimerHandle_t)(timer->timerID), (millisec / portTICK_PERIOD_MS), 0);
	}

	if (pdPASS != status)
	{
		status =  PAL_ERR_RTOS_PARAMETER;
	}
	if (pdPASS == status)
	{
		if (pal_plat_GetIPSR() != 0)
		{
			BaseType_t pxHigherPriorityTaskWoken;
			status = xTimerStartFromISR((TimerHandle_t)(timer->timerID), &pxHigherPriorityTaskWoken);
		}
		else
		{
			status = xTimerStart((TimerHandle_t)(timer->timerID), 0);
		}

		if (pdPASS != status)
		{
			status =  PAL_ERR_RTOS_PARAMETER;
		}
		else
		{
			status = PAL_SUCCESS;
		}
	}
	return status;
}

palStatus_t pal_plat_osTimerStop(palTimerID_t timerID)
{
	palStatus_t status = PAL_SUCCESS;
	palTimer_t* timer = NULL;

	if(NULLPTR == timerID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	timer = (palTimer_t*)timerID;

	if (pal_plat_GetIPSR() != 0)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		status = xTimerStopFromISR((TimerHandle_t)(timer->timerID), &pxHigherPriorityTaskWoken);
	}
	else
	{
		status = xTimerStop((TimerHandle_t)(timer->timerID), 0);
	}


	if (pdPASS != status)
	{
		status = PAL_ERR_RTOS_PARAMETER;
	}
	else
	{
		status = PAL_SUCCESS;
	}
	return status;
}

palStatus_t pal_plat_osTimerDelete(palTimerID_t* timerID)
{
	palStatus_t status = PAL_ERR_RTOS_PARAMETER;
	palTimer_t* timer = NULL;
	int i;

	if(NULL == timerID || NULLPTR == *timerID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	timer = (palTimer_t*)*timerID;

	if (timer->timerID)
	{
		for(i=0 ; i< PAL_MAX_NUM_OF_TIMERS ; i++)
		{
			if (s_timerArrays[i] == timer)
			{
				status = xTimerDelete((TimerHandle_t)(timer->timerID), 0);
				free(timer);
				s_timerArrays[i] = NULL;
				*timerID = NULLPTR;
				break;
			}
		}

		if (pdPASS == status)
		{
			status = PAL_SUCCESS;
		}
		else
		{
			status = PAL_ERR_RTOS_PARAMETER;
		}
	}
	else
	{
		status = PAL_ERR_RTOS_PARAMETER;
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

		mutex->mutexID = (uintptr_t) xSemaphoreCreateRecursiveMutex();
		if (NULLPTR == mutex->mutexID)
		{
			free(mutex);
			mutex = NULL;
			PAL_LOG(ERR, "Rtos mutex create failure");
			status = PAL_ERR_GENERIC_FAILURE;
		}
		*mutexID = (palMutexID_t)mutex;
	}
	return status;
}


palStatus_t pal_plat_osMutexWait(palMutexID_t mutexID, uint32_t millisec)
{

	palStatus_t status = PAL_SUCCESS;
	palMutex_t* mutex = NULL;
	BaseType_t res = pdTRUE;

	if(NULLPTR == mutexID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mutex = (palMutex_t*)mutexID;
	if (pal_plat_GetIPSR() != 0)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		res = xSemaphoreTakeFromISR(mutex->mutexID, &pxHigherPriorityTaskWoken);
	}
	else
	{
		res = xSemaphoreTakeRecursive((QueueHandle_t)(mutex->mutexID), (millisec / portTICK_PERIOD_MS) );
	}

	if (pdTRUE == res)
	{
		status = PAL_SUCCESS;
	}
	else
	{
		status = PAL_ERR_RTOS_TIMEOUT;
	}

	return status;
}


palStatus_t pal_plat_osMutexRelease(palMutexID_t mutexID)
{
	palStatus_t status = PAL_SUCCESS;
	palMutex_t* mutex = NULL;
	BaseType_t res = pdTRUE;

	if(NULLPTR == mutexID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mutex = (palMutex_t*)mutexID;
	if (pal_plat_GetIPSR() != 0)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		res = xSemaphoreGiveFromISR(mutex->mutexID, &pxHigherPriorityTaskWoken);
	}
	else
	{
		res = xSemaphoreGiveRecursive((QueueHandle_t)(mutex->mutexID));
	}

	if (pdTRUE == res)
	{
		status = PAL_SUCCESS;
	}
	else
	{
		PAL_LOG(ERR, "Rtos mutex release failure %d", res);
		status = PAL_ERR_GENERIC_FAILURE;
	}
	return status;
}

palStatus_t pal_plat_osMutexDelete(palMutexID_t* mutexID)
{
	palStatus_t status = PAL_SUCCESS;
	palMutex_t* mutex = NULL;

	if(NULL == mutexID || NULLPTR == *mutexID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mutex = (palMutex_t*)*mutexID;
	if (NULLPTR != mutex->mutexID)
	{
		vSemaphoreDelete(mutex->mutexID);
		free(mutex);
		*mutexID = NULLPTR;
		status = PAL_SUCCESS;
	}
	else
	{
		PAL_LOG(ERR, "Rtos mutex delete failure");
		status = PAL_ERR_GENERIC_FAILURE;
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
		semaphore->semaphoreID = (uintptr_t)xSemaphoreCreateCounting(PAL_SEMAPHORE_MAX_COUNT, count);
		semaphore->maxCount = PAL_SEMAPHORE_MAX_COUNT;
		if (NULLPTR == semaphore->semaphoreID)
		{
			free(semaphore);
			semaphore = NULLPTR;
			PAL_LOG(ERR, "Rtos semaphore create error");
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
	int32_t tmpCounters = 0;
	BaseType_t res = pdTRUE;

	if(NULLPTR == semaphoreID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	semaphore = (palSemaphore_t*)semaphoreID;
	if (pal_plat_GetIPSR() != 0)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		res = xSemaphoreTakeFromISR(semaphore->semaphoreID, &pxHigherPriorityTaskWoken);
	}
	else
	{
		if (millisec == PAL_RTOS_WAIT_FOREVER)
		{
			res = xSemaphoreTake(semaphore->semaphoreID, portMAX_DELAY);
		}
		else
		{
			res = xSemaphoreTake(semaphore->semaphoreID, millisec / portTICK_PERIOD_MS);
		}
	}

	if (pdTRUE == res)
	{
		
		tmpCounters = uxQueueMessagesWaiting((QueueHandle_t)(semaphore->semaphoreID));
	}
	else
	{
		tmpCounters = 0;
		status = PAL_ERR_RTOS_TIMEOUT;
	}

	if (NULL != countersAvailable)
	{
		//because mbedOS returns the number available BEFORE the current take, we have to add 1 here.
		*countersAvailable = tmpCounters;
	}
	return status;
}

palStatus_t pal_plat_osSemaphoreRelease(palSemaphoreID_t semaphoreID)
{
	palStatus_t status = PAL_SUCCESS;
	palSemaphore_t* semaphore = NULL;
	BaseType_t res = pdTRUE;
	int32_t tmpCounters = 0;

	if(NULLPTR == semaphoreID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	semaphore = (palSemaphore_t*)semaphoreID;

	tmpCounters = uxQueueMessagesWaiting((QueueHandle_t)(semaphore->semaphoreID));

	if(tmpCounters < semaphore->maxCount)
	{
		if (pal_plat_GetIPSR() != 0)
		{
			BaseType_t pxHigherPriorityTaskWoken;
			res = xSemaphoreGiveFromISR(semaphore->semaphoreID, &pxHigherPriorityTaskWoken);
		}
		else
		{
			res = xSemaphoreGive(semaphore->semaphoreID);
		}

		if (pdTRUE != res)
		{	
			status = PAL_ERR_RTOS_PARAMETER;
		}
	}
	else 
	{
		status = PAL_ERR_RTOS_RESOURCE;
	}
	
	return status;
}

palStatus_t pal_plat_osSemaphoreDelete(palSemaphoreID_t* semaphoreID)
{
	palStatus_t status = PAL_SUCCESS;
	palSemaphore_t* semaphore = NULL;

	if(NULL == semaphoreID || NULLPTR == *semaphoreID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	semaphore = (palSemaphore_t*)*semaphoreID;
	if (NULLPTR != semaphore->semaphoreID)
	{
		vSemaphoreDelete(semaphore->semaphoreID);
		free(semaphore);
		*semaphoreID = NULLPTR;
		status = PAL_SUCCESS;
	}
	else
	{
		PAL_LOG(ERR, "Rtos semaphore destroy error");
		status = PAL_ERR_GENERIC_FAILURE;
	}
	return status;
}

palStatus_t pal_plat_osPoolCreate(uint32_t blockSize, uint32_t blockCount, palMemoryPoolID_t* memoryPoolID)
{
	palStatus_t status = PAL_SUCCESS;
	palMemoryPool_t* mp = NULL;

	if (NULL == memoryPoolID || 0 == blockSize || 0 == blockCount) {
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mp = (palMemoryPool_t*)malloc(sizeof(palMemoryPool_t));
	if (NULL == mp)
	{
		status = PAL_ERR_RTOS_NO_MEMORY;
	}

	if(PAL_SUCCESS == status)
	{
		mp->start = malloc(blockCount * blockSize);
		if (NULL == mp->start)
		{
			free(mp);
			status = PAL_ERR_RTOS_NO_MEMORY;
		}

		if (PAL_SUCCESS == status)
		{
			mp->blockCount = blockCount;
			mp->blockSize = blockSize;

			mp->allocated = (uint8_t*) malloc(blockCount * sizeof(uint8_t));
			if (NULL == mp->allocated)
			{
				free(mp->start);
				free(mp);
				status = PAL_ERR_RTOS_NO_MEMORY;
			}
			else
			{
				for (uint32_t i = 0; i < blockCount; i++)
				{
					mp->allocated[i] = false;
				}
				*memoryPoolID = (palMemoryPoolID_t) mp;
			}
		}
	}
	return status;
}

PAL_PRIVATE void* poolAlloc(palMemoryPoolID_t memoryPoolID, uint8_t zero)
{
	palMemoryPool_t* mp = NULL;
	void* result = NULL;
	if (NULLPTR == memoryPoolID)
	{
		return NULL;
	}
	mp = (palMemoryPool_t*) memoryPoolID;

	for (uint32_t i = 0; i < mp->blockCount; i++)
	{
		if (false == mp->allocated[i])
		{
			mp->allocated[i] = true;
			result = (void *)( (uintptr_t)(mp->start) + (uintptr_t)(i * mp->blockSize) );
			if (zero == true)
			{
				memset(result, 0, mp->blockSize);
			}
			break;
		}
	}
	//we didn't find any
	return result;
}

void* pal_plat_osPoolAlloc(palMemoryPoolID_t memoryPoolID)
{
	return poolAlloc(memoryPoolID, false);
}

void* pal_plat_osPoolCAlloc(palMemoryPoolID_t memoryPoolID)
{
	return poolAlloc(memoryPoolID, true);
}

palStatus_t pal_plat_osPoolFree(palMemoryPoolID_t memoryPoolID, void* block)
{
	palMemoryPool_t* mp = (palMemoryPool_t*)memoryPoolID;;

	if ( (NULL == mp) ||
			(NULL == block) ||
			(mp->start > block) ||
			( ( (uintptr_t)(mp->start) + (uintptr_t)(mp->blockCount * mp->blockSize) ) < (uintptr_t)block ) ||
			(0 != ( ( (uintptr_t)block - (uintptr_t)(mp->start) ) % mp->blockSize) ) )
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	int index = (int)( ( (uintptr_t)block - (uintptr_t)(mp->start) ) / mp->blockSize );
	mp->allocated[index] = false;
	return PAL_SUCCESS;
}

palStatus_t pal_plat_osPoolDestroy(palMemoryPoolID_t* memoryPoolID)
{
	if (NULL == memoryPoolID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}
	palMemoryPool_t* mp = (palMemoryPool_t*)*memoryPoolID;
	free(mp->start);
	free(mp->allocated);
	free(mp);
	*memoryPoolID = (palMemoryPoolID_t)NULL;   // don't let anyone use it anymore.
	return PAL_SUCCESS;
}

palStatus_t pal_plat_osMessageQueueCreate(uint32_t messageQSize, palMessageQID_t* messageQID)
{
	palStatus_t status = PAL_SUCCESS;
	palMessageQ_t* messageQ = NULL;
	if(NULL == messageQID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	messageQ = (palMessageQ_t*)malloc(sizeof(palMessageQ_t));
	if (NULL == messageQ)
	{
		status = PAL_ERR_NO_MEMORY;
	}

	if (PAL_SUCCESS == status)
	{

		messageQ->messageQID = (uintptr_t)xQueueCreate(messageQSize, sizeof(uint32_t));
		if (NULLPTR == messageQ->messageQID)
		{
			free(messageQ);
			messageQ = NULLPTR;
			PAL_LOG(ERR, "Rtos message queue create failure");
			status = PAL_ERR_GENERIC_FAILURE;
		}
		else
		{
			*messageQID = (palMessageQID_t)messageQ;
		}
	}
	return status;
}

palStatus_t pal_plat_osMessagePut(palMessageQID_t messageQID, uint32_t info, uint32_t timeout)
{
	palStatus_t status = PAL_SUCCESS;
	palMessageQ_t* messageQ = NULL;
	BaseType_t res = pdTRUE;

	if(NULLPTR == messageQID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	messageQ = (palMessageQ_t*)messageQID;
	res = xQueueSend( (QueueHandle_t)(messageQ->messageQID), &info, timeout );
	if (pdTRUE != res)
	{
		status = PAL_ERR_RTOS_RESOURCE;
	}
	return status;
}

palStatus_t pal_plat_osMessageGet(palMessageQID_t messageQID, uint32_t timeout, uint32_t* messageValue)
{
	palStatus_t status = PAL_SUCCESS;
	palMessageQ_t* messageQ = NULL;
	BaseType_t res = pdTRUE;

	if(NULLPTR == messageQID)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	messageQ = (palMessageQ_t*)messageQID;
	res = xQueueReceive( (QueueHandle_t)(messageQ->messageQID), messageValue, timeout );
	if ((messageValue != NULL) && (pdTRUE == res))
	{
		status = PAL_SUCCESS;
	}
	else
	{
		status = PAL_ERR_RTOS_TIMEOUT;
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
	vQueueDelete( (QueueHandle_t)(messageQ->messageQID) );
	free(messageQ);
	*messageQID = NULLPTR;
	return status;
}


void *pal_plat_malloc(size_t len)
{
	return malloc(len);
}


void pal_plat_free(void * buffer)
{
	free(buffer);
}


palStatus_t pal_plat_osRandomBuffer(uint8_t *randomBuf, size_t bufSizeBytes)
{
    palStatus_t status = PAL_SUCCESS;

	status = pal_plat_getRandomBufferFromHW(randomBuf, sizeof(randomBuf));
    return status;
}
