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
#define _GNU_SOURCE // This is for ppoll found in poll.h
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <mqueue.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include "pal.h"
#include "pal_plat_rtos.h"



typedef struct palThreadFuncWrapper
{
    palTimerFuncPtr realThreadFunc;
    void* realThreadArgs;
    uint32_t threadIndex;
} palThreadFuncWrapper_t;

//! Thread structure
typedef struct palThread
{
    pthread_t  threadID;
    uint32_t   palThreadID;
    bool initialized;
    palThreadLocalStore_t* threadStore; //! please see pal_rtos.h for documentation
    palThreadFuncWrapper_t threadFuncWrapper;
    palThreadPriority_t priority;
    uint32_t stackSize;
} palThread_t;

/*! Count number of created threads. Initiate to zero.
*/
PAL_PRIVATE uint32_t g_threadCounter = 0;

palThread_t g_palThreads[PAL_MAX_NUMBER_OF_THREADS];

/*
 * The realtime clock is in nano seconds resolution. This is too much for us, so we use "longer" ticks.
 * Below are relevant defines.
 * make sure they all coherent. Can use one at the other, but will add some unneeded calculations.
 */
#define NANOS_PER_TICK 100
#define TICKS_PER_MICRO  10L
#define TICKS_PER_MILLI  TICKS_PER_MICRO * 1000
#define TICKS_PER_SECOND TICKS_PER_MILLI * 1000

// priorities must be positive, so shift all by this margin. we might want to do smarter convert.
#define LINUX_THREAD_PRIORITY_BASE 10

//  message Queues names related staff:
#define MQ_FILENAME_LEN 10

#ifndef CLOCK_MONOTONIC_RAW //a workaround for the operWRT port that missing this include
#define CLOCK_MONOTONIC_RAW 4 //http://elixir.free-electrons.com/linux/latest/source/include/uapi/linux/time.h
#endif

PAL_PRIVATE char g_mqName[MQ_FILENAME_LEN];
PAL_PRIVATE int g_mqNextNameNum = 0;


extern palStatus_t pal_plat_getRandomBufferFromHW(uint8_t *randomBuf, size_t bufSizeBytes);

inline PAL_PRIVATE void nextMessageQName()
{
    g_mqNextNameNum++;
    for (int j = 4, divider = 10000; j < 9; j++, divider /= 10)
    {
        g_mqName[j] = '0' + (g_mqNextNameNum / divider) %10 ; //just to make sure we don't write more then 1 digit.
    }
    g_mqName[9] = '\0';
}


/*! Initiate a system reboot.
 */
void pal_plat_osReboot(void)
{
    struct utsname buf;
    buf.nodename[0] = 0;
    // Get the system names. Ignore error for this function call
    uname(&buf);
    // We assume that it is a desktop if "ubuntu" is returned.
    if(!strcmp(buf.nodename,"ubuntu"))
    {
        // We emulate resetting the device by running the application again.
        // Restart the application
        int status, timeout; // unused ifdef WAIT_FOR_COMPLETION ;
        const char *argv[] = {"0" , 0};

        //printf("program_invocation_name=%s, __progname=%s",program_invocation_name,__progname);
        argv[0] = program_invocation_name;
        pid_t   my_pid;

        if (0 == (my_pid = fork()))
        {
            char *const envp[] = { 0 };
            if (-1 == execve(argv[0], (char **)argv , envp))
            {
                printf("child process execve failed [%s]",argv[0]);
            }
        }
        timeout = 1000;

        while (0 == waitpid(my_pid , &status , WNOHANG))
        {
            if ( --timeout < 0 ) {
                perror("timeout");
                //return -1;
            }
            sleep(1);
        }
    }
    else
    {
        // Reboot the device
        reboot(RB_AUTOBOOT);
    }
    return;
}

/*! Initialize all data structures (semaphores, mutexs, memory pools, message queues) at system initialization.
 *	In case of a failure in any of the initializations, the function returns with an error and stops the rest of the initializations.
 * @param[in] opaqueContext The context passed to the initialization (not required for generic CMSIS, pass NULL in this case).
 * \return PAL_SUCCESS(0) in case of success, PAL_ERR_CREATION_FAILED in case of failure.
 */
palStatus_t pal_plat_RTOSInitialize(void* opaqueContext)
{
    palStatus_t status = PAL_SUCCESS;
    (void) opaqueContext;
    strncpy(g_mqName, "/pal00001", MQ_FILENAME_LEN);
    g_mqNextNameNum = 1;   // used for the next name

    //Clean thread tables
    memset(g_palThreads,0,sizeof(palThread_t) * PAL_MAX_NUMBER_OF_THREADS);

    //Add implicit the running task as PAL main
    g_palThreads[0].initialized = true;
    g_palThreads[0].threadID =  pthread_self();

    pal_osAtomicIncrement((int32_t*)&g_threadCounter,1);
    //palThreadID = 24 bits for thread counter + 8 bits for thread index (= 0).
    g_palThreads[0].palThreadID = (g_threadCounter << 8 );

    return status;
}

/*! De-Initialize thread objects.
 */
palStatus_t pal_plat_RTOSDestroy(void)
{
    return PAL_SUCCESS;
}


/*return The RTOS kernel system timer counter, in microseconds
 */

uint64_t pal_plat_osKernelSysTick(void) // optional API - not part of original CMSIS API.
{
    /*Using clock_gettime is more accurate, but then we have to convert it to ticks. we are using a tick every 100 nanoseconds*/
    struct timespec ts;
    uint64_t ticks;
    //TODO: error handling
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    ticks = (uint64_t) (ts.tv_sec * (uint64_t)TICKS_PER_SECOND
            + (ts.tv_nsec / NANOS_PER_TICK));
    return ticks;
}

/* Convert the value from microseconds to kernel sys ticks.
 * This is the same as CMSIS macro osKernelSysTickMicroSec.
 * since we return microsecods as ticks, just return the value
 */
uint64_t pal_plat_osKernelSysTickMicroSec(uint64_t microseconds)
{

    //convert to nanoseconds
    return microseconds * TICKS_PER_MICRO;
}

/*! Get the system tick frequency.
 * \return The system tick frequency.
 */
inline uint64_t pal_plat_osKernelSysTickFrequency(void)
{
    /* since we use clock_gettime, with resolution of 100 nanosecond per tick*/
    return TICKS_PER_SECOND;
}

inline PAL_PRIVATE void setDefaultThreadValues(palThread_t* thread)
{

#if PAL_UNIQUE_THREAD_PRIORITY
    g_palThreadPriorities[thread->priority + PRIORITY_INDEX_OFFSET] = 0;
#endif //PAL_UNIQUE_THREAD_PRIORITY
    thread->threadStore = NULL;
    thread->threadFuncWrapper.realThreadArgs = NULL;
    thread->threadFuncWrapper.realThreadFunc = NULL;
    thread->threadFuncWrapper.threadIndex = 0;
    thread->priority = PAL_osPriorityError;
    thread->stackSize = 0;
    thread->threadID = (palThreadID_t)PAL_INVALID_THREAD;
    thread->palThreadID = 0;
    //! This line should be last thing to be done in this function.
    //! in order to prevent double accessing the same index between
    //! this function and the threadCreate function.
    thread->initialized = false;
}

/*! Clean thread data from the global thread data base (g_palThreads). Thread Safe API
 *
 * @param[in] index:	the index in the data base to be cleaned.
 */
PAL_PRIVATE void threadCleanUp(uint32_t index)
{
    uint32_t status = PAL_SUCCESS;
    uint32_t threadIndex = PAL_GET_THREAD_INDEX(index);

    status = pal_osMutexWait(g_palThreadInitMutex, PAL_RTOS_WAIT_FOREVER);

    if (PAL_SUCCESS != status)
    {
         PAL_LOG(ERR,"thread cleanup: mutex wait failed!\n");
    }
    else{
        if ((threadIndex < PAL_MAX_NUMBER_OF_THREADS) && (g_palThreads[threadIndex].palThreadID == index))
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

/*! Thread wrapper function, this function will be set as the thread function (for every thread)
 *	and it will get as an argument the real data about the thread and call the REAL thread function
 *	with the REAL argument. Once the REAL thread function finished, \ref pal_threadClean() will be called.
 *
 *	@param[in] arg: data structure which contains the real data about the thread.
 */
void* threadFunctionWrapper(void* arg)
{

    palThreadFuncWrapper_t* threadWrapper = (palThreadFuncWrapper_t*) arg;

    if ((NULL != threadWrapper) && (NULL != threadWrapper->realThreadFunc))
    {
        if(g_palThreads[threadWrapper->threadIndex].threadID == NULLPTR)
        {
            g_palThreads[threadWrapper->threadIndex].threadID =  pthread_self();
        }

        threadWrapper->realThreadFunc(threadWrapper->realThreadArgs);
        threadCleanUp(g_palThreads[threadWrapper->threadIndex].palThreadID);

    }
    return NULL;
}

/*! Create and start a thread function.
 *
 * @param[in] function A function pointer to the thread callback function.
 * @param[in] funcArgument An argument for the thread function.
 * @param[in] priority The priority of the thread.
 * @param[in] stackSize The stack size of the thread.
 * @param[in] stackPtr A pointer to the thread's stack.
 * @param[in] store A pointer to thread's local store, can be NULL.
 * @param[out] threadID The created thread ID handle, zero indicates an error.
 *
 * \return The ID of the created thread, in case of error return zero.
 * \note Each thread MUST have a unique priority.
 * \note When the priority of the created thread function is higher than the current running thread, the
 * 		created thread function starts instantly and becomes the new running thread.
 */
palStatus_t pal_plat_osThreadCreate(palThreadFuncPtr function,
        void* funcArgument, palThreadPriority_t priority, uint32_t stackSize,
        uint32_t* stackPtr, palThreadLocalStore_t* store,
        palThreadID_t* threadID)
{
    uint32_t firstAvailableThreadIndex = PAL_MAX_NUMBER_OF_THREADS;
    palStatus_t status = PAL_SUCCESS;
    uint32_t localPalThreadID = 0;


    {
        if ((NULL == threadID) || (NULL == function) || (0 == stackSize) || (priority > PAL_osPriorityRealtime))
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }
        status = pal_osMutexWait(g_palThreadInitMutex, PAL_RTOS_WAIT_FOREVER);
        if (PAL_SUCCESS == status)
        {
            for (uint32_t i = 0; i < PAL_MAX_NUMBER_OF_THREADS; ++i)
            {
                if ((!g_palThreads[i].initialized))
                {
                    g_palThreads[i].initialized = true;
                    firstAvailableThreadIndex = i;
                    break;
                }
            }

            if (firstAvailableThreadIndex >= PAL_MAX_NUMBER_OF_THREADS)
            {
                *threadID = PAL_INVALID_THREAD;
                status = PAL_ERR_RTOS_RESOURCE;
                // release mutex if error.
                pal_osMutexRelease(g_palThreadInitMutex);
                goto finish;
            }

            g_palThreads[firstAvailableThreadIndex].threadStore = store;
            g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadArgs =
                    funcArgument;
            g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.realThreadFunc =
                    function;
            g_palThreads[firstAvailableThreadIndex].threadFuncWrapper.threadIndex =
                    firstAvailableThreadIndex;
            g_palThreads[firstAvailableThreadIndex].priority = priority;
            g_palThreads[firstAvailableThreadIndex].stackSize = stackSize;
            g_palThreads[firstAvailableThreadIndex].palThreadID = ((firstAvailableThreadIndex)+((pal_osAtomicIncrement((int32_t*)&g_threadCounter, 1)) << 8)); //palThreadID = 24 bits for thread counter + lower 8 bits for thread index.
            localPalThreadID = g_palThreads[firstAvailableThreadIndex].palThreadID;

            // release mutex before thread creation .
            status = pal_osMutexRelease(g_palThreadInitMutex);

            if (PAL_SUCCESS == status)
            {
                // prepare thread attributes
                pthread_attr_t attr;
                pthread_attr_init(&attr);

                int err = pthread_attr_setstacksize(&attr, stackSize);// Replace stack pointer with dynamically  allocated from the OS
                if (0 != err)
                {
                    status = PAL_ERR_INVALID_ARGUMENT;
                    goto finish;
                }

                if (0 != pthread_attr_setschedpolicy(&attr, SCHED_RR))
                {
                    status = PAL_ERR_INVALID_ARGUMENT;
                    goto finish;
                }

                //PTHREAD_CREATE_JOINABLE in Linux save the stack TCB until join is call and detach clean every thing upon exit
                //Because PAL is not forcing the user to call thread cancel the threads are detached
                if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                {
                    status = PAL_ERR_INVALID_ARGUMENT;
                    goto finish;
                }

                struct sched_param schedParam;
                schedParam.sched_priority = LINUX_THREAD_PRIORITY_BASE + (int) priority;
                if (0 != pthread_attr_setschedparam(&attr, &schedParam))
                {
                    status = PAL_ERR_INVALID_ARGUMENT;
                    goto finish;
                }

                //create the thread
                pthread_t thread = (pthread_t)NULL;

                int retVal = pthread_create(&thread, &attr, threadFunctionWrapper, (void*) &(g_palThreads[firstAvailableThreadIndex].threadFuncWrapper));
                pthread_attr_destroy(&attr); //Destroy the thread attributes object, since it is no longer needed
                if (0 != retVal)
                {
                    if (EPERM == retVal)
                    {
                        // cannot set the priority
                        status = PAL_ERR_RTOS_PRIORITY;
                    }
                    else
                    {
                        status = PAL_ERR_RTOS_RESOURCE;
                    }
                    goto finish;
                }

                if((thread != (palThreadID_t)PAL_INVALID_THREAD) && (thread != 0))
                {
                    // if we managed to do it, set in the the array, set threadID, and return
                    g_palThreads[firstAvailableThreadIndex].threadID = (palThreadID_t) thread;
                    *threadID = localPalThreadID;
                }
            }

        }
    }
    finish:
    if (PAL_SUCCESS != status)
    {
        if (firstAvailableThreadIndex < PAL_MAX_NUMBER_OF_THREADS)
        {
            threadCleanUp(localPalThreadID);
        }
        *threadID = PAL_INVALID_THREAD;
    }
    return status;

}

/*! Terminate and free allocated data for the thread.
 *
 * @param[in] threadID The ID of the thread to stop and terminate.
 *
 * \return palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osThreadTerminate(palThreadID_t* threadID)
{
    palStatus_t status = PAL_ERR_INVALID_ARGUMENT;
    int statusOS = 0;
    uint32_t threadIndex = PAL_GET_THREAD_INDEX(*threadID);

    if ((PAL_INVALID_THREAD == *threadID) || (threadIndex >= PAL_MAX_NUMBER_OF_THREADS))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    // if thread exited or was terminated already return success.
    if ((g_palThreads[threadIndex].palThreadID == 0) ||  // thread already exited
        (g_palThreads[threadIndex].palThreadID != *threadID) || // thread already exited and a new thread was created at the same index.
        (g_palThreads[threadIndex].threadID == (palThreadID_t)PAL_INVALID_THREAD))  // thread was terminsated.
    {
        return PAL_SUCCESS;
    }


    if((pthread_self() != g_palThreads[threadIndex].threadID))
    {//Kill only if not trying to kill from running task
        status = PAL_SUCCESS;
        if ((g_palThreads[threadIndex].initialized))
        {
            statusOS = pthread_cancel(g_palThreads[threadIndex].threadID);
            if((statusOS != 0) && (statusOS != ESRCH))
            {
                status = PAL_ERR_RTOS_RESOURCE;
            }
        }
    }

    if(status == PAL_SUCCESS)
    {
        threadCleanUp(*threadID);
        *threadID = PAL_INVALID_THREAD;
    }

    return status;
}

/*! Get the ID of the current thread.
 * \return The ID of the current thread, in case of error return PAL_maX_UINT32.
 * \note For a thread with real time priority, the function always returns PAL_maX_UINT32.
 */
palThreadID_t pal_plat_osThreadGetId()
{
    int i = 0;
    pthread_t  osThreadID = pthread_self();
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

/*! Get the storage of the current thread.
 * \return The storage of the current thread.
 */
palThreadLocalStore_t* pal_plat_osThreadGetLocalStore(void)
{
    palThreadLocalStore_t* localStore = NULL;
    palThreadID_t id = pal_osThreadGetId();

    if( g_palThreads[id].initialized)
    {
        localStore = g_palThreads[id].threadStore;
    }
    return localStore;
}

/*! Wait for a specified period of time in milliseconds.
 *
 * @param[in] milliseconds The number of milliseconds to wait before proceeding.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osDelay(uint32_t milliseconds)
{
    struct timespec sTime;
    struct timespec rTime; // this will return how much sleep time still left in case of interrupted sleep
    int stat;
    //init rTime, as we will copy it over to stime inside the do-while loop.
    rTime.tv_sec = milliseconds / 1000;
    rTime.tv_nsec = PAL_MILLI_TO_NANO(milliseconds);

    do
    {
        sTime.tv_sec = rTime.tv_sec;
        sTime.tv_nsec = rTime.tv_nsec;
        stat = nanosleep(&sTime, &rTime);
    } while ((-1 == stat) && (EINTR ==errno)) ;
    return (stat == 0) ? PAL_SUCCESS : PAL_ERR_GENERIC_FAILURE;
}

/*
 * Internal struct to handle timers.
 */

struct palTimerInfo
{
    timer_t handle;
    palTimerFuncPtr function;
    void *funcArgs;
    palTimerType_t timerType;
    bool isHighRes;
};

/*
 * internal function used to handle timers expiration events.
 */
PAL_PRIVATE void palTimerEventHandler(void* args)
{
    struct palTimerInfo* timer = (struct palTimerInfo *) args;

    if (NULL == timer)
    { // no timer anymore, so just return.
        return;
    }

    //call the callback function
    timer->function(timer->funcArgs);
}


/*
* Internal struct to handle timers.
*/

#define PAL_HIGH_RES_TIMER_THRESHOLD_MS 100

typedef struct palHighResTimerThreadContext
{
    palTimerFuncPtr function;
    void *funcArgs;
    uint32_t intervalMS;
} palHighResTimerThreadContext_t;


static pthread_t s_palHighResTimerThreadID = {0};
static bool s_palHighResTimerThreadInUse =  0;
static palHighResTimerThreadContext_t s_palHighResTimerThreadContext = {0};

/*
*  callback for handling high precision timer callbacks (currently only one is supported)
*/

PAL_PRIVATE void* palHighResTimerThread(void* args)
{
    palHighResTimerThreadContext_t* context = (palHighResTimerThreadContext_t*)args;
    uint32_t timer_period_ms = context->intervalMS;
    int err = 0;
    struct timespec next_timeout_ts;
    err = clock_gettime(CLOCK_MONOTONIC, &next_timeout_ts);
    assert(err == 0);

    while(1) {
        // Determine absolute time we want to sleep until
        next_timeout_ts.tv_nsec += PAL_NANO_PER_MILLI * timer_period_ms;
        if (next_timeout_ts.tv_nsec >= PAL_NANO_PER_SECOND) 
        {
            next_timeout_ts.tv_nsec = next_timeout_ts.tv_nsec - PAL_NANO_PER_SECOND;
            next_timeout_ts.tv_sec += 1;
        }

        // Call nanosleep until error or no interrupt, ie. return code is 0
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_timeout_ts, NULL);
            assert(err == 0 || err == EINTR);
        } while(err == EINTR);

        // Done sleeping, call callback
        context->function(context->funcArgs);
    }
    return NULL;
}

PAL_PRIVATE palStatus_t startHighResTimerThread(palTimerFuncPtr function, void *funcArgs , uint32_t intervalMS)
{
    int retVal = 0;
    palStatus_t status = PAL_SUCCESS;
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    s_palHighResTimerThreadContext.function = function;
    s_palHighResTimerThreadContext.funcArgs = funcArgs;
    s_palHighResTimerThreadContext.intervalMS = intervalMS;

    retVal = pthread_attr_setstacksize(&attr, PAL_RTOS_HIGH_RES_TIMER_THREAD_STACK_SIZE);//sets the minimum stack size
    if (0 != retVal)
    {
        status = PAL_ERR_INVALID_ARGUMENT;
        goto finish;
    }

    if (0 != pthread_attr_setschedpolicy(&attr, SCHED_RR))
    {
        status = PAL_ERR_INVALID_ARGUMENT;
        goto finish;
    }

    //PTHREAD_CREATE_JOINABLE in Linux save the stack TCB until join is call and detach clean every thing upon exit
    //Because PAL is not forcing the user to call thread cancel the threads are detached
    if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
    {
        status = PAL_ERR_INVALID_ARGUMENT;
        goto finish;
    }

    struct sched_param schedParam;
    schedParam.sched_priority = LINUX_THREAD_PRIORITY_BASE + (int)PAL_osPriorityRealtime;
    if (0 != pthread_attr_setschedparam(&attr, &schedParam))
    {
        status = PAL_ERR_INVALID_ARGUMENT;
        goto finish;
    }

   retVal = pthread_create(&s_palHighResTimerThreadID, &attr, &palHighResTimerThread, &s_palHighResTimerThreadContext);
    if (0 != retVal)
    {
        if (EPERM == retVal)
        {
            // cannot set the priority
            status = PAL_ERR_RTOS_PRIORITY;
        }
        else
        {
            status = PAL_ERR_RTOS_RESOURCE;
        }
    }

finish:
    pthread_attr_destroy(&attr); //Destroy the thread attributes object, since it is no longer needed

    return status;

}


/*! Create a timer.
 *
 * @param[in] function A function pointer to the timer callback function.
 * @param[in] funcArgument An argument for the timer callback function.
 * @param[in] timerType The timer type to be created, periodic or oneShot.
 * @param[out] timerID The ID of the created timer, zero value indicates an error.
 *
 * \return PAL_SUCCESS when the timer was created successfully. A specific error in case of failure.
 */
palStatus_t pal_plat_osTimerCreate(palTimerFuncPtr function, void* funcArgument,
        palTimerType_t timerType, palTimerID_t* timerID)
{

    palStatus_t status = PAL_SUCCESS;
    struct palTimerInfo* timerInfo = NULL;
    {
        struct sigevent sig;
        timer_t localTimer;

        if ((NULL == timerID) || (NULL == (void*) function))
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }

        timerInfo = (struct palTimerInfo*) malloc(sizeof(struct palTimerInfo));
        if (NULL == timerInfo)
        {
            status = PAL_ERR_NO_MEMORY;
            goto finish;
        }

        timerInfo->function = function;
        timerInfo->funcArgs = funcArgument;
        timerInfo->timerType = timerType;
        timerInfo->isHighRes = false;

        memset(&sig, 0, sizeof(sig));

        sig.sigev_notify = SIGEV_THREAD;
        sig.sigev_signo = 0;
        sig.sigev_value.sival_ptr = timerInfo;
        sig.sigev_notify_function = (void (*)(union sigval)) palTimerEventHandler;

        int ret = timer_create(CLOCK_MONOTONIC, &sig, &localTimer);
        if (-1 == ret)
        {
            if (EINVAL == errno)
            {
                status = PAL_ERR_INVALID_ARGUMENT;
                goto finish;
            }
            if (ENOMEM == errno)
            {
                status = PAL_ERR_NO_MEMORY;
                goto finish;
            }
            PAL_LOG(ERR, "Rtos timer create error %d", ret);
            status = PAL_ERR_GENERIC_FAILURE;
            goto finish;
        }

        // managed to create the timer - finish up
        timerInfo->handle = localTimer;
        *timerID = (palTimerID_t) timerInfo;
    }
    finish: if (PAL_SUCCESS != status)
    {
        if (NULL != timerInfo)
        {
            free(timerInfo);
            *timerID = (palTimerID_t) NULL;
        }
    }
    return status;
}

/* Convert milliseconds into seconds and nanoseconds inside a timespec struct
 */
PAL_PRIVATE void convertMilli2Timespec(uint32_t millisec, struct timespec* ts)
{
    ts->tv_sec = millisec / 1000;
    ts->tv_nsec = PAL_MILLI_TO_NANO(millisec);
}

/*! Start or restart a timer.
 *
 * @param[in] timerID The handle for the timer to start.
 * @param[in] millisec The time in milliseconds to set the timer to.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osTimerStart(palTimerID_t timerID, uint32_t millisec)
{
    palStatus_t status = PAL_SUCCESS;
    if (NULL == (struct palTimerInfo *) timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    struct palTimerInfo* timerInfo = (struct palTimerInfo *) timerID;
    struct itimerspec its;


    if ((millisec <= PAL_HIGH_RES_TIMER_THRESHOLD_MS) && (palOsTimerPeriodic == timerInfo->timerType )) // periodic high res timer  - we only support 1 (workaround for issue when lots of threads are created in linux)
    {
        if (true == s_palHighResTimerThreadInUse)
        {
            status = PAL_ERR_NO_HIGH_RES_TIMER_LEFT;
        }
        else
        {
            status = startHighResTimerThread(timerInfo->function, timerInfo->funcArgs, millisec);
            if (PAL_SUCCESS == status)
            {
                timerInfo->isHighRes = true;
        s_palHighResTimerThreadInUse = true;
            }

        }

    }
    else // otherwise handle normally
    {
        convertMilli2Timespec(millisec, &(its.it_value));

        if (palOsTimerPeriodic == timerInfo->timerType)
        {
            convertMilli2Timespec(millisec, &(its.it_interval));
        }
        else
        {  // one time timer
            convertMilli2Timespec(0, &(its.it_interval));
        }

        if (-1 == timer_settime(timerInfo->handle, 0, &its, NULL))
        {
            status = PAL_ERR_INVALID_ARGUMENT;
        }
    }

    return status;
}

/*! Stop a timer.
 *
 * @param[in] timerID The handle for the timer to stop.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osTimerStop(palTimerID_t timerID)
{
    palStatus_t status = PAL_SUCCESS;
    if (NULL == (struct palTimerInfo *) timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    struct palTimerInfo* timerInfo = (struct palTimerInfo *) timerID;
    struct itimerspec its;

    if ((true == timerInfo->isHighRes) && (0 != s_palHighResTimerThreadInUse )) // if  high res timer clean up thread.
    {
        int statusOS = pthread_cancel(s_palHighResTimerThreadID);
        if ((statusOS != 0) && (statusOS != ESRCH))
        {
            return PAL_ERR_RTOS_RESOURCE;
        }
        else
        {
            timerInfo->isHighRes = false;
            s_palHighResTimerThreadInUse = false;
            return PAL_SUCCESS;
        }
    }
    else // otherwise process normally
    {
        // set timer to 0 to disarm it.
        convertMilli2Timespec(0, &(its.it_value));

        convertMilli2Timespec(0, &(its.it_interval));

        if (-1 == timer_settime(timerInfo->handle, 0, &its, NULL))
        {
            status = PAL_ERR_INVALID_ARGUMENT;
        }
    }

    return status;
}

/*! Delete the timer object
 *
 * @param[inout] timerID The handle for the timer to delete. In success, *timerID = NULL.
 *
 * \return PAL_SUCCESS when the timer was deleted successfully, PAL_ERR_RTOS_PARAMETER when the timerID is incorrect.
 */
palStatus_t pal_plat_osTimerDelete(palTimerID_t* timerID)
{
    palStatus_t status = PAL_SUCCESS;
    if (NULL == timerID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    struct palTimerInfo* timerInfo = (struct palTimerInfo *) *timerID;
    if (NULL == timerInfo)
    {
        status = PAL_ERR_RTOS_PARAMETER;
    }

    if ((true == timerInfo->isHighRes) && (0 != s_palHighResTimerThreadInUse)) //  if high res timer delted before stopping => clean up thread.
    {
        int statusOS = pthread_cancel(s_palHighResTimerThreadID);
        if ((statusOS != 0) && (statusOS != ESRCH))
        {
            status = PAL_ERR_RTOS_RESOURCE;
        }
        else
        {
            timerInfo->isHighRes = false;
            s_palHighResTimerThreadInUse = false;
        }
    }

    if (PAL_SUCCESS == status)
    {
        timer_t lt = timerInfo->handle;
        if (-1 == timer_delete(lt))
        {
            status = PAL_ERR_RTOS_RESOURCE;
        }

        free(timerInfo);
        *timerID = (palTimerID_t) NULL;
    }
    return status;
}

/*! Create and initialize a mutex object.
 *
 * @param[out] mutexID The created mutex ID handle, zero value indicates an error.
 *
 * \return PAL_SUCCESS when the mutex was created successfully, a specific error in case of failure.
 */
palStatus_t pal_plat_osMutexCreate(palMutexID_t* mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    pthread_mutex_t* mutex = NULL;
    {
        int ret;
        if (NULL == mutexID)
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }

        mutex = malloc(sizeof(pthread_mutex_t));
        if (NULL == mutex)
        {
            status = PAL_ERR_NO_MEMORY;
            goto finish;
        }

        pthread_mutexattr_t mutexAttr;
        pthread_mutexattr_init(&mutexAttr);
        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
        ret = pthread_mutex_init(mutex, &mutexAttr);

        if (0 != ret)
        {
            if (ENOMEM == ret)
            {
                status = PAL_ERR_NO_MEMORY;
            }
            else
            {
                PAL_LOG(ERR, "Rtos mutex create status %d", ret);
                status = PAL_ERR_GENERIC_FAILURE;
            }
            goto finish;
        }
        *mutexID = (palMutexID_t) mutex;
    }
    finish: if (PAL_SUCCESS != status)
    {
        if (NULL != mutex)
        {
            free(mutex);
        }
    }
    return status;
}

/* Wait until a mutex becomes available.
 *
 * @param[in] mutexID The handle for the mutex.
 * @param[in] millisec The timeout for the waiting operation if the timeout expires before the semaphore is released and an error is returned from the function.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, one of the following error codes in case of failure:
 * 		  PAL_ERR_RTOS_RESOURCE - Mutex not available but no timeout set.
 * 		  PAL_ERR_RTOS_TIMEOUT - Mutex was not available until timeout expired.
 * 		  PAL_ERR_RTOS_PARAMETER - Mutex ID is invalid.
 * 		  PAL_ERR_RTOS_ISR - Cannot be called from interrupt service routines.
 */
palStatus_t pal_plat_osMutexWait(palMutexID_t mutexID, uint32_t millisec)
{
    palStatus_t status = PAL_SUCCESS;
    int err;
    if (NULL == ((pthread_mutex_t*) mutexID))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_t* mutex = (pthread_mutex_t*) mutexID;

    if (PAL_RTOS_WAIT_FOREVER != millisec)
    {
        /* calculate the wait absolute time */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        ts.tv_sec += (millisec / PAL_MILLI_PER_SECOND);
        ts.tv_nsec += PAL_MILLI_TO_NANO(millisec);
        ts.tv_sec += ts.tv_nsec / PAL_NANO_PER_SECOND; // if there is some overflow in the addition of nanoseconds.
        ts.tv_nsec = ts.tv_nsec % PAL_NANO_PER_SECOND;

        while ((err = pthread_mutex_timedlock(mutex, &ts)) != 0 && err == EINTR)
        {
            continue; /* Restart if interrupted by handler */
        }
    }
    else
    { // wait for ever
        err = pthread_mutex_lock(mutex);
    }

    if (0 != err)
    {
        if (err == ETIMEDOUT)
        {
            status = PAL_ERR_RTOS_TIMEOUT;
        }
        else
        {
            PAL_LOG(ERR, "Rtos mutex wait status %d", err);
            status = PAL_ERR_GENERIC_FAILURE;
        }
    }

    return status;
}

/* Release a mutex that was obtained by osMutexWait.
 *
 * @param[in] mutexID The handle for the mutex.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osMutexRelease(palMutexID_t mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    int result = 0;

    pthread_mutex_t* mutex = (pthread_mutex_t*) mutexID;
    if (NULL == mutex)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    result = pthread_mutex_unlock(mutex);
    if (0 != result)
    {
        // only reason this might fail - process don't have permission for mutex.
        PAL_LOG(ERR, "Rtos mutex release failure - %d",result);
        status = PAL_ERR_GENERIC_FAILURE;
    }
    return status;
}

/*Delete a mutex object.
 *
 * @param[inout] mutexID The ID of the mutex to delete. In success, *mutexID = NULL.
 *
 * \return PAL_SUCCESS when the mutex was deleted successfully, one of the following error codes in case of failure:
 * 		  PAL_ERR_RTOS_RESOURCE - Mutex already released.
 * 		  PAL_ERR_RTOS_PARAMETER - Mutex ID is invalid.
 * 		  PAL_ERR_RTOS_ISR - Cannot be called from interrupt service routines.
 * \note After this call, mutex_id is no longer valid and cannot be used.
 */
palStatus_t pal_plat_osMutexDelete(palMutexID_t* mutexID)
{
    palStatus_t status = PAL_SUCCESS;
    uint32_t ret;
    if (NULL == mutexID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_t* mutex = (pthread_mutex_t*) *mutexID;

    if (NULL == mutex)
    {
        status = PAL_ERR_RTOS_RESOURCE;
    }
    ret = pthread_mutex_destroy(mutex);
    if ((PAL_SUCCESS == status) && (0 != ret))
    {
        PAL_LOG(ERR,"pal_plat_osMutexDelete 0x%x",ret);
        status = PAL_ERR_RTOS_RESOURCE;
    }
    if (NULL != mutex)
    {
        free(mutex);
    }

    *mutexID = (palMutexID_t) NULL;
    return status;
}

/* Create and initialize a semaphore object.
 *
 * Semaphore is shared between threads, but not process.
 *
 * @param[in] count The number of available resources.
 * @param[out] semaphoreID The ID of the created semaphore, zero value indicates an error.
 *
 * \return PAL_SUCCESS when the semaphore was created successfully, a specific error in case of failure.
 */
palStatus_t pal_plat_osSemaphoreCreate(uint32_t count,
        palSemaphoreID_t* semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    sem_t* semaphore = NULL;

    {
        if (NULL == semaphoreID)
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }
        semaphore = malloc(sizeof(sem_t));
        if (NULL == semaphore)
        {
            status = PAL_ERR_NO_MEMORY;
            goto finish;
        }
        /* create the semaphore as shared between threads */
        int ret = sem_init(semaphore, 0, count);
        if (-1 == ret)
        {
            if (EINVAL == errno)
            {
                /* count is too big */
                status = PAL_ERR_INVALID_ARGUMENT;
            }
            else
            {
                PAL_LOG(ERR, "Rtos semaphore init error %d", ret);
                status = PAL_ERR_GENERIC_FAILURE;
            }
            goto finish;
        }

        *semaphoreID = (palSemaphoreID_t) semaphore;
    }
    finish: if (PAL_SUCCESS != status)
    {
        if (NULL != semaphore)
        {
            free(semaphore);
        }
        *semaphoreID = (palSemaphoreID_t) NULL;
    }
    return status;
}

/* Wait until a semaphore token becomes available.
 *
 * @param[in] semaphoreID The handle for the semaphore.
 * @param[in] millisec The timeout for the waiting operation if the timeout expires before the semaphore is released and an error is returned from the function.
 * @param[out] countersAvailable The number of semaphores available (before the wait), if semaphores are not available (timeout/error) zero is returned.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, one of the following error codes in case of failure:
 * 		PAL_ERR_RTOS_TIMEOUT - Semaphore was not available until timeout expired.
 *	    PAL_ERR_RTOS_PARAMETER - Semaphore ID is invalid.
 *	    PAL_ERR_INVALID_ARGUMENT - countersAvailable is NULL
 *
 *	    NOTES: 1. counterAvailable returns 0 in case there are no semaphores available or there are other threads waiting on it.
 *	              Value is not thread safe - it might be changed by the time it is read/returned.
 *	           2. timed wait is using absolute time.
 */
palStatus_t pal_plat_osSemaphoreWait(palSemaphoreID_t semaphoreID,
        uint32_t millisec, int32_t* countersAvailable)
{
    palStatus_t status = PAL_SUCCESS;
    int tmpCounters = 0;
    {
        int err;
        sem_t* sem = (sem_t*) semaphoreID;
        if ((NULL == sem))
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }

        if (PAL_RTOS_WAIT_FOREVER != millisec)
        {
            /* calculate the wait absolute time */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += millisec / PAL_MILLI_PER_SECOND;
            ts.tv_nsec += PAL_MILLI_TO_NANO(millisec);
            ts.tv_sec += ts.tv_nsec / PAL_NANO_PER_SECOND; // in case there is overflow in the nanoseconds.
            ts.tv_nsec = ts.tv_nsec % PAL_NANO_PER_SECOND;

            while ((err = sem_timedwait(sem, &ts)) == -1 && errno == EINTR)
                continue; /* Restart if interrupted by handler */
        }
        else
        { // wait for ever
            do
            {
                err = sem_wait(sem);

                /* loop again if the wait was interrupted by a signal */
            } while ((err == -1) && (errno == EINTR));
        }

        if (-1 == err)
        {
            tmpCounters = 0;
            if (errno == ETIMEDOUT)
            {
                status = PAL_ERR_RTOS_TIMEOUT;
            }
            else
            { /* seems this is not a valid semaphore */
                status = PAL_ERR_RTOS_PARAMETER;
            }
            goto finish;
        }
        /* get the counter number, shouldn't fail, as we already know this is valid semaphore */
        sem_getvalue(sem, &tmpCounters);
    }
    finish:
    if (NULL != countersAvailable)
    {
        *countersAvailable = tmpCounters;
    }
    return status;
}

/*! Release a semaphore token.
 *
 * @param[in] semaphoreID The handle for the semaphore.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osSemaphoreRelease(palSemaphoreID_t semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    sem_t* sem = (sem_t*) semaphoreID;

    if (NULL == sem)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    if (-1 == sem_post(sem))
    {
        if (EINVAL == errno)
        {
            status = PAL_ERR_RTOS_PARAMETER;
        }
        else
        { /* max value of semaphore exeeded */
            PAL_LOG(ERR, "Rtos semaphore release error %d", errno);
            status = PAL_ERR_GENERIC_FAILURE;
        }
    }

    return status;
}

/*! Delete a semaphore object.
 *
 * @param[inout] semaphoreID: The ID of the semaphore to delete. In success, *semaphoreID = NULL.
 *
 * \return PAL_SUCCESS when the semaphore was deleted successfully, one of the following error codes in case of failure:
 * 		  PAL_ERR_RTOS_RESOURCE - Semaphore already released.
 * 		  PAL_ERR_RTOS_PARAMETER - Semaphore ID is invalid.
 * \note After this call, the semaphore_id is no longer valid and cannot be used.
 */
palStatus_t pal_plat_osSemaphoreDelete(palSemaphoreID_t* semaphoreID)
{
    palStatus_t status = PAL_SUCCESS;
    {
        if (NULL == semaphoreID)
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }

        sem_t* sem = (sem_t*) (*semaphoreID);
        if (NULL == sem)
        {
            status = PAL_ERR_RTOS_RESOURCE;
            goto finish;
        }
        if (-1 == sem_destroy(sem))
        {
            status = PAL_ERR_RTOS_PARAMETER;
            goto finish;
        }

        if (NULL != sem)
        {
            free(sem);
        }
        *semaphoreID = (palSemaphoreID_t) NULL;
    }
    finish: return status;
}

typedef struct palMemoryPool
{
    void *start;
    uint32_t blockCount;
    uint32_t blockSize;
    bool* allocated;
} palMemoryPool_t;

/*! Create and initialize a memory pool.
 *
 * @param[in] blockSize The size of a single block in bytes.
 * @param[in] blockCount The maximum number of blocks in the memory pool.
 * @param[out] memoryPoolID The ID of the created memory pool, zero value indicates an error.
 *
 * \return PAL_SUCCESS when the memory pool was created successfully, a specific error in case of failure.
 */
palStatus_t pal_plat_osPoolCreate(uint32_t blockSize, uint32_t blockCount, palMemoryPoolID_t* memoryPoolID)
{
    palStatus_t status = PAL_SUCCESS;
    palMemoryPool_t* mp = NULL;
    {

        if ((NULL == memoryPoolID) || (0 == blockSize) || (0 == blockCount))
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }

        mp = (palMemoryPool_t*) malloc(sizeof(palMemoryPool_t));
        if (NULL == mp)
        {
            status = PAL_ERR_RTOS_NO_MEMORY;
            goto finish;
        }

        mp->blockCount = blockCount;
        mp->blockSize = blockSize;
        mp->allocated = NULL;
        mp->start = malloc((size_t)blockCount * blockSize);
        if (NULL == mp->start)
        {
            status = PAL_ERR_RTOS_NO_MEMORY;
            goto finish;
        }

        mp->allocated = (bool*) malloc(blockCount * sizeof(bool));
        if (NULL == mp->allocated)
        {
            status = PAL_ERR_RTOS_NO_MEMORY;
            goto finish;
        }
        for (uint32_t i = 0; i < blockCount; i++)
        {
            mp->allocated[i] = false;
        }

        *memoryPoolID = (palMemoryPoolID_t) mp;
    }

    finish:

    if (PAL_SUCCESS != status)
    {
        if (NULL != mp)
        {
            if (NULL != mp->start)
            {
                free(mp->start);
            }
            if (NULL != mp->allocated) // in current code - we are not supposed to be here.
            {
                free(mp->allocated);
            }
            free(mp);
        }
    }

    return status;
}

PAL_PRIVATE inline void* poolAlloc(palMemoryPoolID_t memoryPoolID, bool zero)
{
    if (NULL == (palMemoryPool_t*) memoryPoolID)
    {
        return NULL;
    }

    palMemoryPool_t* mp = (palMemoryPool_t*) memoryPoolID;

    for (uint32_t i = 0; i < mp->blockCount; i++)
    {
        if (mp->allocated[i] == false)
        {
            mp->allocated[i] = true;
            void* block = (mp->start + i * mp->blockSize);
            if (zero == true)
            {
                memset(block, 0, mp->blockSize);
            }
            return block;
        }
    }
    //we didn't find any
    return NULL;
}
/*! Allocate a single memory block from a memory pool.
 *
 * @param[in] memoryPoolID The handle for the memory pool.
 *
 * \return A pointer to a single allocated memory from the pool, NULL in case of failure.
 */
void* pal_plat_osPoolAlloc(palMemoryPoolID_t memoryPoolID)
{
    return poolAlloc(memoryPoolID, false);
}

/*! Allocate a single memory block from a memory pool and set memory block to zero.
 *
 * @param[in] memoryPoolID The handle for the memory pool.
 *
 * \return A pointer to a single allocated memory from the pool, NULL in case of failure.
 */
void* pal_plat_osPoolCAlloc(palMemoryPoolID_t memoryPoolID)
{
    return poolAlloc(memoryPoolID, true);
}

/*! Return the memoryPoolID of the memory block back to a specific memory pool.
 *
 * @param[in] memoryPoolID The handle for the memory pool.
 * @param[in] block The block to be freed.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osPoolFree(palMemoryPoolID_t memoryPoolID, void* block)
{
    palMemoryPool_t* mp = (palMemoryPool_t*) memoryPoolID;
    if ((NULL == (palMemoryPool_t*) memoryPoolID) || (NULL == block)
            || (mp->start > block)
            || ((mp->start + mp->blockCount * mp->blockSize) < block)
            || (0 != ((block - mp->start) % mp->blockSize)))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    int index = (block - mp->start) / mp->blockSize;
    mp->allocated[index] = false;
    return PAL_SUCCESS;
}

/*! Delete a memory pool object.
 *
 * @param[inout] memoryPoolID The handle for the memory pool. In success, *memoryPoolID = NULL.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osPoolDestroy(palMemoryPoolID_t* memoryPoolID)
{
    if (NULL == memoryPoolID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    palMemoryPool_t* mp = (palMemoryPool_t*) *memoryPoolID;
    *memoryPoolID = (palMemoryPoolID_t) NULL; // don't let anyone use it anymore.
    free(mp->start);
    free(mp->allocated);
    free(mp);
    return PAL_SUCCESS;
}

typedef struct palMessageQ
{
    mqd_t handle;
    char name[MQ_FILENAME_LEN];
} palMessageQ_t;

/*! Create and initialize a message queue.
 *
 * @param[in] messageQSize The size of the message queue.
 * @param[out] messageQID The ID of the created message queue, zero value indicates an error.
 *
 * \return PAL_SUCCESS when the message queue was created successfully, a specific error in case of failure.
 */
palStatus_t pal_plat_osMessageQueueCreate(uint32_t messageQSize, palMessageQID_t* messageQID)
{
    palStatus_t status = PAL_SUCCESS;
    palMessageQ_t *mq_h = NULL;
    {
        if (NULL == messageQID)
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }
        mq_h = malloc(sizeof(palMessageQ_t));
        if (NULL == mq_h)
        {
            status = PAL_ERR_NO_MEMORY;
            goto finish;
        }
        // copy the name to be used, and advance it.
        // Note - not thread safe!!
        strncpy(mq_h->name, g_mqName, MQ_FILENAME_LEN-1);
        mq_h->name[MQ_FILENAME_LEN-1] = '\0';
        nextMessageQName();

        // set the attributes for the queue:
        struct mq_attr mqAttr;
        mqAttr.mq_flags = O_RDWR | O_CREAT | O_EXCL; // if the file for the messageQueue exists - we will fail.
        mqAttr.mq_maxmsg = messageQSize;
        mqAttr.mq_msgsize = sizeof(uint32_t);
        mqAttr.mq_curmsgs = 0;
        // create the message Queue. make sure no such filename exists. open with read/write/execute
        // for user & group.

        mq_h->handle = mq_open(mq_h->name, O_RDWR | O_CREAT | O_EXCL,
        S_IRWXU | S_IRWXG, &mqAttr);
        if (-1 == mq_h->handle)
        {
            status = PAL_ERR_CREATION_FAILED;
            goto finish;
        }

        *messageQID = (palMessageQID_t) mq_h;
    }
    finish:
    if (PAL_SUCCESS != status)
    {
        if (NULL != mq_h)
        {
            free(mq_h);
        }
        *messageQID = 0;
    }
    return status;
}

/*! Put a message to a queue.
 *
 * @param[in] messageQID The handle for the message queue.
 * @param[in] info The data to send.
 * @param[in] timeout The timeout in milliseconds.
 *
 * All messages has the same priority (set as 0).
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osMessagePut(palMessageQID_t messageQID, uint32_t info,
        uint32_t timeout)
{
    palStatus_t status = PAL_SUCCESS;
    int stat;

    palMessageQ_t* mq = (palMessageQ_t*) messageQID;
    if (NULL == mq)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    if (PAL_RTOS_WAIT_FOREVER != timeout)
    {
        /* calculate the wait absolute time */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        ts.tv_sec += timeout / PAL_MILLI_PER_SECOND;
        ts.tv_nsec += PAL_MILLI_TO_NANO(timeout);
        //in case there is overflow in the nanoseconds.
        ts.tv_sec += ts.tv_nsec / PAL_NANO_PER_SECOND;
        ts.tv_nsec = ts.tv_nsec % PAL_NANO_PER_SECOND;

        while ((-1
                == (stat = mq_timedsend(mq->handle, (const char*) &info,
                        sizeof(uint32_t), 0, &ts))) && (EINTR == errno))
        {
            continue; /* Restart if interrupted by handler */
        }
    }
    else
    { // wait for ever
        stat = mq_send(mq->handle, (const char*) &info, sizeof(uint32_t), 0);
    }

    if (-1 == stat)
    {
        if (EBADF == errno)
        {
            status = PAL_ERR_INVALID_ARGUMENT;
        }
        else if (ETIMEDOUT == errno)
        {
            status = PAL_ERR_RTOS_TIMEOUT;
        }
        else
        {
            // all other cases - return generic error.
            PAL_LOG(ERR, "Rtos put message status %d", stat);
            status = PAL_ERR_GENERIC_FAILURE;
        }
    }
    return status;
}

/*! Get a message or wait for a message from a queue.
 *
 * @param[in] messageQID The handle for the message queue.
 * @param[in] timeout The timeout in milliseconds.
 * @param[out] messageValue The data to send.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, one of the following error codes in case of failure:
 * PAL_ERR_RTOS_TIMEOUT -  No message arrived during the timeout period.
 * PAL_ERR_RTOS_RESOURCE -  No message received and there was no timeout.
 */
palStatus_t pal_plat_osMessageGet(palMessageQID_t messageQID, uint32_t timeout,
        uint32_t* messageValue)
{
    palStatus_t status = PAL_SUCCESS;
    int stat;

    palMessageQ_t* mq = (palMessageQ_t*) messageQID;
    if ((NULL == mq) || (NULL == messageValue))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    if (PAL_RTOS_WAIT_FOREVER != timeout)
    {
        /* calculate the wait absolute time */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        ts.tv_sec += timeout / PAL_MILLI_PER_SECOND;
        ts.tv_nsec += PAL_MILLI_TO_NANO(timeout);
        // in case there is an overflow in the nanoseconds
        ts.tv_sec += ts.tv_nsec / PAL_NANO_PER_SECOND;
        ts.tv_nsec = ts.tv_nsec % PAL_NANO_PER_SECOND;

        while ((-1
                == (stat = mq_timedreceive(mq->handle, (char*) messageValue,
                        sizeof(uint32_t), 0, &ts))) && (EINTR == errno))
        {
            continue; /* Restart if interrupted by handler */
        }
    }
    else
    { // wait for ever
        stat = mq_receive(mq->handle, (char*) messageValue, sizeof(uint32_t), 0);
    }

    if (-1 == stat)
    {
        if (EBADF == errno)
        {
            status = PAL_ERR_INVALID_ARGUMENT;
        }
        else if (ETIMEDOUT == errno)
        {
            status = PAL_ERR_RTOS_TIMEOUT;
        }
        else
        {
            // all other cases - return resource error.
            status = PAL_ERR_RTOS_RESOURCE;
        }
    }

    return status;
}

/*! Delete a message queue object.
 *
 * @param[inout] messageQID The handle for the message queue. In success, *messageQID = NULL.
 *
 * \return The status in the form of palStatus_t; PAL_SUCCESS(0) in case of success, a negative value indicating a specific error code in case of failure.
 */
palStatus_t pal_plat_osMessageQueueDestroy(palMessageQID_t* messageQID)
{
    palStatus_t status = PAL_SUCCESS;
    palMessageQ_t *mq = NULL;
    {
        if (NULL == messageQID)
        {
            return PAL_ERR_INVALID_ARGUMENT;
        }
        //close the queue
        mq = (palMessageQ_t *) *messageQID;
        if (-1 == mq_close(mq->handle))
        {
            status = PAL_ERR_INVALID_ARGUMENT;
            goto finish;
        }

        //unlink the file:
        if (-1 == mq_unlink(mq->name))
        {
            status = PAL_ERR_RTOS_RESOURCE;
            goto finish;
        }
    }
    finish:
    free(mq);
    *messageQID = (palMessageQID_t) NULL;
    return status;
}

/*! Perform an atomic increment for a signed32 bit value.
 *
 * @param[in,out] valuePtr The address of the value to increment.
 * @param[in] increment The number by which to increment.
 *
 * \returns The value of the valuePtr after the increment operation.
 */
int32_t pal_plat_osAtomicIncrement(int32_t* valuePtr, int32_t increment)
{
    int32_t res = __sync_add_and_fetch(valuePtr, increment);
    return res;
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

    status = pal_plat_getRandomBufferFromHW(randomBuf, bufSizeBytes);
    return status;
}

