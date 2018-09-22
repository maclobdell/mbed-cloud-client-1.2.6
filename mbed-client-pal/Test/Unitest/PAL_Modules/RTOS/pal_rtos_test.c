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
#include "unity.h"
#include "unity_fixture.h"
#include "pal_rtos_test_utils.h"
#include <string.h>
#include <stdlib.h>



TEST_GROUP(pal_rtos);

//Sometimes you may want to get local data in a module,
//for example if you need to pass a reference.
//However, you should usually avoid this.
//extern int Counter;
palThreadLocalStore_t g_threadStorage = {NULL};
threadsArgument_t g_threadsArg = {0};
timerArgument_t g_timerArgs = {0};
palMutexID_t mutex1 = NULLPTR;
palMutexID_t mutex2 = NULLPTR;
palSemaphoreID_t semaphore1 = NULLPTR;
palRecursiveMutexParam_t* recursiveMutexData = NULL;
#define PAL_TEST_HIGH_RES_TIMER 100
#define PAL_TEST_HIGH_RES_TIMER2 10
#define PAL_TEST_PERCENTAGE_LOW 95
#define PAL_TEST_PERCENTAGE_HIGH 105
#define PAL_TEST_PERCENTAGE_HUNDRED  100

//Forward declarations
void palRunThreads(void);


TEST_SETUP(pal_rtos)
{
     pal_init();
}

TEST_TEAR_DOWN(pal_rtos)
{
    if (NULL != recursiveMutexData)
    {
        if (recursiveMutexData->higherPriorityThread != NULLPTR)
        {
            pal_osThreadTerminate(&(recursiveMutexData->higherPriorityThread));
        }
        if (recursiveMutexData->lowerPriorityThread != NULLPTR)
        {
            pal_osThreadTerminate(&(recursiveMutexData->lowerPriorityThread));
        }
        if (recursiveMutexData->mtx != NULLPTR)
        {
            pal_osMutexDelete(&(recursiveMutexData->mtx));
        }
        if (recursiveMutexData->sem != NULLPTR)
        {
            pal_osSemaphoreDelete(&recursiveMutexData->sem);
        }
        free(recursiveMutexData);
        recursiveMutexData = NULL;
    }
    pal_destroy();
}

/*! \brief Sanity check of the kernel system tick API.
 * Fails if system tic value is zero (**note:** this can sometimes happen on wrap-around).
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Get current tick count using `pal_osKernelSysTick` and check that it is not 0.  | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osKernelSysTick_Unity)
{
    uint32_t tick1 = 0, tick2 = 0;
    /*#1*/
    tick1 = pal_osKernelSysTick();
    TEST_PRINTF("%" PRIu32 " %" PRIu32 "\n", tick1, tick2);

    TEST_ASSERT_TRUE(tick2 != tick1);
}

/*! \brief Sanity check of the kernel system tick API.
 * Fails if two calls return the same `sysTick` value.
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Get current tick count using `pal_osKernelSysTick`.       | PAL_SUCCESS |
 * | 2 | Get current tick count using `pal_osKernelSysTick`.       | PAL_SUCCESS |
 * | 3 | Check that the two tick count values are not the same. | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osKernelSysTick64_Unity)
{
    uint64_t tick1 = 0, tick2 = 0;
    /*#1*/
    tick1 = pal_osKernelSysTick();
    /*#2*/
    tick2 = pal_osKernelSysTick();
    /*#3*/
    TEST_ASSERT_TRUE(tick2 >= tick1);
}

/*! \brief Check the conversion from a non-zero `sysTick` value to microseconds.
 * Verify that the result is not 0.
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Convert a nubmer in `sysTicks` to microseconds using `pal_osKernelSysTickMicroSec` and check it is not 0. | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osKernelSysTickMicroSec_Unity)
{
    uint64_t tick = 0;
    uint64_t microSec = 2000 * 1000;
    /*#1*/
    tick = pal_osKernelSysTickMicroSec(microSec);
    TEST_ASSERT_TRUE(0 != tick);
}

/*! \brief Sanity check of non-zero values conversion between microseconds to ticks to milliseconds.
 * Verify that the result is correct when converting the input (microseconds) to the test output (milliseconds).
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Convert a nubmer in `sysTicks` to mircorseconds using `pal_osKernelSysTickMicroSec` and check it is not 0. | PAL_SUCCESS |
 * | 2 | Convert a nubmer in `sysTicks` to milliseconds using `pal_osKernelSysMilliSecTick` and check the returned value. | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osKernelSysMilliSecTick_Unity)
{
    uint64_t tick = 0;
    uint64_t microSec = 2000 * 1000;
    uint64_t milliseconds = 0;
    /*#1*/
    tick = pal_osKernelSysTickMicroSec(microSec);
    TEST_ASSERT_TRUE(0 != tick);
    /*#2*/
    milliseconds = pal_osKernelSysMilliSecTick(tick);
    TEST_ASSERT_EQUAL(microSec/1000, milliseconds);
}

/*! \brief Verify that the tick frequency function returns a non-zero value.
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Get the kernel `sysTick` frequency and check that it is positive.     | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osKernelSysTickFrequency_Unity)
{
    uint64_t frequency = 0;
    /*#1*/
    frequency = pal_osKernelSysTickFrequency();

    TEST_ASSERT_TRUE(frequency > 0);
}

/*! \brief Sanity check for the Delay API, verifying that `sysTick` increments after delay.
 * The test reads two system tick values. Between the two calls, it calls the delay function and
 * verifies that the tick values are different.
 *
 * | # |    Step                        |   Expected  |
 * |---|--------------------------------|-------------|
 * | 1 | Get the kernel `sysTick` value.                           | PAL_SUCCESS |
 * | 2 | Sleep for a short period .                              | PAL_SUCCESS |
 * | 3 | Get the kernel `sysTick` value.                          | PAL_SUCCESS |
 * | 4 | Check that second tick value is greater than the first. | PAL_SUCCESS |
 */
TEST(pal_rtos, pal_osDelay_Unity)
{
    palStatus_t status = PAL_SUCCESS;
    uint32_t tick1 , tick2;
    /*#1*/
    tick1 = pal_osKernelSysTick();
    /*#2*/
    status = pal_osDelay(200);
    /*#3*/
    tick2 = pal_osKernelSysTick();
    /*#4*/
    TEST_ASSERT_TRUE(tick2 > tick1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}

/*! \brief Test for basic timing scenarios based on calls for the ticks and delay
* functionality while verifying that results meet the defined deltas.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Get the kernel `sysTick` value.                                      | PAL_SUCCESS |
* | 2 | Sleep for a very short period.                                     | PAL_SUCCESS |
* | 3 | Get the kernel `sysTick` value.                                      | PAL_SUCCESS |
* | 4 | Check that second tick value is greater than the first.            | PAL_SUCCESS |
* | 5 | Get the kernel `sysTick` value.                                      | PAL_SUCCESS |
* | 6 | Sleep for a longer period.                                         | PAL_SUCCESS |
* | 7 | Get the kernel `sysTick` value.                                      | PAL_SUCCESS |
* | 8 | Check that second tick value is greated than the first.           | PAL_SUCCESS |
* | 9 | Calculate the difference between the ticks.                                | PAL_SUCCESS |
* | 10 | Convert last sleep period to ticks.                              | PAL_SUCCESS |
* | 11 | Check that the tick period is correct (same as sleep period +/-delta). | PAL_SUCCESS |
*/
TEST(pal_rtos, BasicTimeScenario)
{
    palStatus_t status = PAL_SUCCESS;
    uint64_t tick, tick1 , tick2 , tickDiff, tickDelta;

    /*#1*/
    tick1 = pal_osKernelSysTick();
    /*#2*/
    status = pal_osDelay(1);
    /*#3*/
    tick2 = pal_osKernelSysTick();

    /*#4*/
    TEST_ASSERT_TRUE(tick1 != tick2);
    TEST_ASSERT_TRUE(tick2 > tick1);  // To check that the tick counts are incremental - be aware of wrap-arounds
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /****************************************/
    /*#5*/
    tick1 = pal_osKernelSysTick();
    /*#6*/
    status = pal_osDelay(2000);
    /*#7*/
    tick2 = pal_osKernelSysTick();

    /*#8*/
    TEST_ASSERT_TRUE(tick1 != tick2);
    TEST_ASSERT_TRUE(tick2 > tick1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#9*/
    tickDiff = tick2 - tick1;
    /*#10*/
    tick = pal_osKernelSysTickMicroSec(2000 * 1000);
    // 10 milliseconds delta
    /*#11*/
    tickDelta = pal_osKernelSysTickMicroSec(10 * 1000);
    TEST_ASSERT_TRUE((tick - tickDelta < tickDiff) && (tickDiff < tick + tickDelta));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}

/*! \brief Create two timers: periodic and one-shot. Starts both timers,
* then causes a delay to allow output from the timer functions to be printed on the console.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a one-shot timer, which calls `palTimerFunc1` when triggered, using `pal_osTimerCreate`. | PAL_SUCCESS |
* | 2 | Create a periodic timer, which calls `palTimerFunc2` when triggered, using `pal_osTimerCreate`. | PAL_SUCCESS |
* | 3 | Get the kernel `sysTick` value.                                                              | PAL_SUCCESS |
* | 4 | Start the first timer using `pal_osTimerStart`.                                              | PAL_SUCCESS |
* | 5 | Get the kernel `sysTick` value.                                                              | PAL_SUCCESS |
* | 6 | Start the first timer using `pal_osTimerStart`.                                              | PAL_SUCCESS |
* | 7 | Sleep for a period.                                                                          | PAL_SUCCESS |
* | 8 | Stop the second timer using `pal_osTimerStop`.                                               | PAL_SUCCESS |
* | 9 | Delete the first timer using `pal_osTimerDelete`.                                            | PAL_SUCCESS |
* | 10 | Delete the second timer using `pal_osTimerDelete`.                                            | PAL_SUCCESS |
* | 11 | Create a periodic timer, which calls `palTimerFunc3` when triggered, using `pal_osTimerCreate`. | PAL_SUCCESS |
* | 12 | Create a periodic timer, which calls `palTimerFunc4` when triggered, using `pal_osTimerCreate`. | PAL_ERR_NO_HIGH_RES_TIMER_LEFT |
* | 13 | Start the first timer using `pal_osTimerStart` as high res timer.                           | PAL_SUCCESS |
* | 14 | Start the second timer using `pal_osTimerStart` as high res timer.                          | PAL_ERR_NO_HIGH_RES_TIMER_LEFT |
* | 15 | Sleep for a period.                                                                         | PAL_SUCCESS |
* | 16 | Stop the second timer using `pal_osTimerStop`.                                              | PAL_SUCCESS |
* | 17 | Start the second timer using `pal_osTimerStart` as high res timer                           | PAL_SUCCESS |
* | 18 | Sleep for a period.                                                                         | PAL_SUCCESS |
* | 19 | Delete the first timer using `pal_osTimerDelete`.                                           | PAL_SUCCESS |
* | 20 | Delete the second timer using `pal_osTimerDelete`.                                          | PAL_SUCCESS |
* | 21 | Create a periodic timer, which calls `palTimerFunc5` when triggered, using `pal_osTimerCreate`.  | PAL_SUCCESS |
* | 22 | Sleep for a period.                                                                         | PAL_SUCCESS |
* | 23 | Delete the first timer using `pal_osTimerDelete`.                                           | PAL_SUCCESS |
* | 24 | Stop the timer using `pal_osTimerStop`.  and check the number of callbacks is correct       | PAL_SUCCESS |
* | 25 | Delete the timer using `pal_osTimerDelete`.                                                 | PAL_SUCCESS |
*/
TEST(pal_rtos, TimerUnityTest)
{
    palStatus_t status = PAL_SUCCESS;
    palTimerID_t timerID1 = NULLPTR;
    palTimerID_t timerID2 = NULLPTR;
    /*#1*/
    status = pal_osTimerCreate(palTimerFunc1, NULL, palOsTimerOnce, &timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_osTimerCreate(palTimerFunc2, NULL, palOsTimerPeriodic, &timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    g_timerArgs.ticksBeforeTimer = pal_osKernelSysTick();
    /*#4*/
    status = pal_osTimerStart(timerID1, 1000);
    TEST_PRINTF("ticks before Timer: 0 - %" PRIu32 "\n", g_timerArgs.ticksBeforeTimer);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    g_timerArgs.ticksBeforeTimer = pal_osKernelSysTick();
    /*#6*/
    status = pal_osTimerStart(timerID2, 1000);
    TEST_PRINTF("ticks before Timer: 1 - %" PRIu32 "\n", g_timerArgs.ticksBeforeTimer);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    status = pal_osDelay(1500);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#8*/
    status = pal_osTimerStop(timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#9*/
    status = pal_osTimerDelete(&timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, timerID1);
    /*#10*/
    status = pal_osTimerDelete(&timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, timerID2);

	g_timerArgs.ticksBeforeTimer = 0;
    g_timerArgs.ticksInFunc1 = 0;
    g_timerArgs.ticksInFunc2 = 0;
	
    /*#11*/
    status = pal_osTimerCreate(palTimerFunc3, NULL, palOsTimerPeriodic, &timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    
    /*#12*/
    status = pal_osTimerCreate(palTimerFunc4, NULL, palOsTimerPeriodic, &timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#13*/
    status = pal_osTimerStart(timerID1, PAL_TEST_HIGH_RES_TIMER);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#14*/
    status = pal_osTimerStart(timerID2, PAL_TEST_HIGH_RES_TIMER);
    if (PAL_SUCCESS == status) // behavior is slightly different for Linux due to high res timer limitation there (only one at a time supported there)
	{
		status = pal_osTimerStop(timerID2);
		TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
	}
	else  
	{
		TEST_ASSERT_EQUAL_HEX(PAL_ERR_NO_HIGH_RES_TIMER_LEFT, status);
	}
    /*#15*/
    status = pal_osDelay(500);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#16*/
    status = pal_osTimerStop(timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#17*/
    status = pal_osTimerStart(timerID2, PAL_TEST_HIGH_RES_TIMER2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#18*/
    status = pal_osDelay(PAL_TIME_TO_WAIT_SHORT_MS);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osTimerStop(timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

	TEST_ASSERT_TRUE(g_timerArgs.ticksInFunc1 >= ((PAL_TIME_TO_WAIT_SHORT_MS / PAL_TEST_HIGH_RES_TIMER2)*PAL_TEST_PERCENTAGE_LOW)/ PAL_TEST_PERCENTAGE_HUNDRED); // check there is at least more than 95% of expected timer callbacks.
	
    /*#19*/
    status = pal_osTimerDelete(&timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, timerID1);
    
    /*#20*/
    status = pal_osTimerDelete(&timerID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, timerID2);

    /*#21*/
    g_timerArgs.ticksBeforeTimer = 0;
    g_timerArgs.ticksInFunc1 = 0;
    g_timerArgs.ticksInFunc2 = 0;

    status = pal_osTimerCreate(palTimerFunc5, NULL, palOsTimerPeriodic, &timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#22*/
    status = pal_osTimerStart(timerID1, PAL_TEST_HIGH_RES_TIMER);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    
    /*#23*/
    status = pal_osDelay(PAL_TIME_TO_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#24*/
    status = pal_osTimerStop(timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    TEST_ASSERT_TRUE(g_timerArgs.ticksInFunc1 >= ((PAL_TIME_TO_WAIT_MS / PAL_TEST_HIGH_RES_TIMER) * PAL_TEST_PERCENTAGE_LOW) / PAL_TEST_PERCENTAGE_HUNDRED); // check there is at least more than 95% of expected timer callbacks.
    TEST_ASSERT_TRUE(g_timerArgs.ticksInFunc1 <= ((PAL_TIME_TO_WAIT_MS / PAL_TEST_HIGH_RES_TIMER) * PAL_TEST_PERCENTAGE_HIGH) / PAL_TEST_PERCENTAGE_HUNDRED); // check there is at most less than 105% of expected timer callbacks.

    /*#25*/
    status = pal_osTimerDelete(&timerID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, timerID1);
}

/*! \brief Creates mutexes and semaphores and uses them to communicate between
* the different threads it creates (as defined in `pal_rtos_test_utils.c`).
* In this test, we check that thread communication is working as expected between the threads and in the designed order.
* In one case, we expect the thread to fail to lock a mutex – (thread1).
* Threads are created with different priorities (PAL enforces this attribute).
* For each case, the thread function prints the expected result. The test code verifies this result as well.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a mutex using `pal_osMutexCreate`.                           | PAL_SUCCESS |
* | 2 | Create a mutex using `pal_osMutexCreate`.                           | PAL_SUCCESS |
* | 3 | Create a semaphore with count 1.                                  | PAL_SUCCESS |
* | 4 | Run the PAL test threads using the `palRunThreads` test function.  | PAL_SUCCESS |
* | 5 | Delete the semaphore using `pal_osSemaphoreDelete`.                     | PAL_SUCCESS |
* | 6 | Delete the first mutex using `pal_osMutexDelete`.                       | PAL_SUCCESS |
* | 7 | Delete the second mutex using `pal_osMutexDelete`.                       | PAL_SUCCESS |
*/
TEST(pal_rtos, PrimitivesUnityTest1)
{
    palStatus_t status = PAL_SUCCESS;
    /*#1*/
    status = pal_osMutexCreate(&mutex1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_osMutexCreate(&mutex2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_osSemaphoreCreate(1 ,&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    palRunThreads();
    /*#5*/
    status = pal_osSemaphoreDelete(&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, semaphore1);
    /*#6*/
    status = pal_osMutexDelete(&mutex1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, mutex1);
    /*#7*/
    status = pal_osMutexDelete(&mutex2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, mutex2);

}

/*! \brief Verifies that several RTOS primitives APIs can handle invalid
* arguments. The test calls each API with invalid arguments and verifies the result.
* It also verifies that the semaphore wait API can accept NULL as the third parameter.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Test thread creation with invalid arguments (`pal_osThreadCreateWithAlloc`). | PAL_ERR_INVALID_ARGUMENT |
* | 2 | Test thread creation with invalid arguments (`pal_osThreadCreateWithAlloc`). | PAL_ERR_INVALID_ARGUMENT |
* | 3 | Test thread creation with invalid arguments (`pal_osThreadCreateWithAlloc`). | PAL_ERR_INVALID_ARGUMENT |
* | 4 | Test semaphore creation with invalid arguments (`pal_osSemaphoreCreate`).    | PAL_ERR_INVALID_ARGUMENT |
* | 5 | Test semaphore creation with invalid arguments (`pal_osSemaphoreCreate`).   | PAL_ERR_INVALID_ARGUMENT |
* | 6 | Test semaphore creation with invalid arguments (`pal_osSemaphoreCreate`).    | PAL_ERR_INVALID_ARGUMENT |
* | 7 | Test semaphore creation with invalid arguments (`pal_osSemaphoreCreate`).   | PAL_ERR_INVALID_ARGUMENT |
*/
TEST(pal_rtos, PrimitivesUnityTest2)
{
    palStatus_t status = PAL_SUCCESS;
    int32_t tmp = 0;
    palThreadID_t threadID = NULLPTR;

    /*#1*/
    //Check thread parameter validation
    status = pal_osThreadCreateWithAlloc(palThreadFunc1, NULL, PAL_osPriorityError, 1024, NULL, &threadID);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#2*/
    status = pal_osThreadCreateWithAlloc(palThreadFunc1, NULL, PAL_osPriorityIdle, 0, NULL, &threadID);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#3*/
    status = pal_osThreadCreateWithAlloc(palThreadFunc1, NULL, PAL_osPriorityIdle, 1024, NULL, NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);

    /*#4*/
    //Check semaphore parameter validation
    status = pal_osSemaphoreCreate(1 ,NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#5*/
    status = pal_osSemaphoreDelete(NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#6*/
    status = pal_osSemaphoreWait(NULLPTR, 1000, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#7*/
    status = pal_osSemaphoreRelease(NULLPTR);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
}

/*! \brief Creates a semaphore with count=1 and a thread to
* test that it waits forever (the test waits 5 seconds). Then deletes the semaphore
* and terminates the thread.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a semaphore with count = 1 using `pal_osSemaphoreCreate`.                          | PAL_SUCCESS |
* | 2 | Wait for the semaphore using `pal_osSemaphoreWait` (should not block).                    | PAL_SUCCESS |
* | 3 | Create a thread running `palThreadFuncWaitForEverTestusing` and `pal_osThreadCreateWithAlloc`. | PAL_SUCCESS |
* | 4 | Set time using `pal_osSetTime`.                                                           | PAL_SUCCESS |
* | 5 | Wait for the semaphore using `pal_osSemaphoreWait` (should block; released by thread).        | PAL_SUCCESS |
* | 6 | Delete the semaphore using `pal_osSemaphoreDelete`.                                           | PAL_SUCCESS |
* | 7 | Terminate the thread using `pal_osThreadTerminate`.                                           | PAL_SUCCESS |
*/
TEST(pal_rtos, SemaphoreWaitForever)
{
    int32_t count = 0;
    uint64_t timeElapsed = PAL_MIN_SEC_FROM_EPOCH;
    uint64_t timePassedInSec;
    palStatus_t status = PAL_SUCCESS;
    palThreadID_t threadID1 = PAL_INVALID_THREAD;

    /*#1*/
    status = pal_osSemaphoreCreate(1 ,&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_osSemaphoreWait(semaphore1, PAL_RTOS_WAIT_FOREVER, &count);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_osSetTime(timeElapsed);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status); // More than current epoch time -> success    
    status = pal_osThreadCreateWithAlloc(palThreadFuncWaitForEverTest, (void *)&semaphore1, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    status = pal_osSemaphoreWait(semaphore1, PAL_RTOS_WAIT_FOREVER, &count);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    timePassedInSec = pal_osGetTime();
    TEST_ASSERT_EQUAL_HEX(0, (timePassedInSec - timeElapsed) >= PAL_TIME_TO_WAIT_MS/2);
    /*#6*/
    status = pal_osSemaphoreDelete(&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(0, semaphore1);
    /*#7*/
    status = pal_osThreadTerminate(&threadID1);
    TEST_ASSERT_EQUAL(PAL_SUCCESS, status);
}

/*! \brief Creates a semaphore and waits on it to verify the
* available count for it. Also verifies that the semaphore release API works correctly.
* In addition, it checks the semaphore parameter validation scenarios.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a semaphore with count = 2 using `pal_osSemaphoreCreate`.                          | PAL_SUCCESS |
* | 2 | Wait for the semaphore using `pal_osSemaphoreWait` (should not block), and check count.   | PAL_SUCCESS |
* | 3 | Increase semaphore count by ten using `pal_osSemaphoreRelease` in a loop.             | PAL_SUCCESS |
* | 4 | Delete semaphore using `pal_osSemaphoreDelete`.                                           | PAL_SUCCESS |
* | 5 | Test semaphore creation with invalid arguments (`pal_osSemaphoreCreate`).                 | PAL_ERR_INVALID_ARGUMENT |
* | 6 | Test semaphore deletion with invalid arguments (`pal_osSemaphoreDelete`).                 | PAL_ERR_INVALID_ARGUMENT |
* | 7 | Test semaphore waiting with invalid arguments (`pal_osSemaphoreWait`).                    | PAL_ERR_INVALID_ARGUMENT |
* | 8 | Test semaphore release with invalid arguments (`pal_osSemaphoreRelease`).                 | PAL_ERR_INVALID_ARGUMENT |
*/
TEST(pal_rtos, SemaphoreBasicTest)
{

    palStatus_t status = PAL_SUCCESS;
    int counter = 0;
    /*#1*/
    status = pal_osSemaphoreCreate(2 ,&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    int32_t count = -1;
    status = pal_osSemaphoreWait(semaphore1, 1000, &count);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(1, count);

    /*#3*/
    for(counter = 0; counter < 10; counter++)
    {
        status=pal_osSemaphoreRelease(semaphore1);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#4*/
    status=pal_osSemaphoreDelete(&semaphore1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(0, semaphore1);

    //Check semaphore parameter validation
    int32_t tmp;
    /*#5*/
    status = pal_osSemaphoreCreate(1 ,NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#6*/
    status = pal_osSemaphoreDelete(NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#7*/
    status = pal_osSemaphoreWait(NULLPTR, 1000, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#8*/
    status = pal_osSemaphoreRelease(NULLPTR);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
}

/*! \brief Creates two memory pools.
* Allocates blocks from each pool using the APIs `pal_osPoolAlloc` and `pal_osPoolCAlloc`.
* Verifies that none of the allocated blocks are NULL.
* Deallocates the blocks.
* Destroys the pools.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a memory pool using `pal_osPoolCreate`.                               | PAL_SUCCESS |
* | 2 | Create a memory pool using `pal_osPoolCreate`.                               | PAL_SUCCESS |
* | 3 | Allocate blocks from the first pool in a loop using `pal_osPoolAlloc`.       | PAL_SUCCESS |
* | 4 | Allocate blocks from the second pool in a loop using `pal_osPoolAlloc`.      | PAL_SUCCESS |
* | 5 | Free blocks from the first pool in a loop using `pal_osPoolAlloc`.           | PAL_SUCCESS |
* | 6 | Free blocks from the second pool in a loop using `pal_osPoolAlloc`.          | PAL_SUCCESS |
* | 7 | Delete first memory pool.                                                  | PAL_SUCCESS |
* | 7 | Delete second memory pool.                                                | PAL_SUCCESS |
*/
TEST(pal_rtos, MemoryPoolUnityTest)
{
    palStatus_t status = PAL_SUCCESS;
    palMemoryPoolID_t poolID1 = NULLPTR;
    palMemoryPoolID_t poolID2 = NULLPTR;
    uint8_t* ptr1[MEMORY_POOL1_BLOCK_COUNT] = {0};
    uint8_t* ptr2[MEMORY_POOL2_BLOCK_COUNT] = {0};

    /*#1*/
    status = pal_osPoolCreate(MEMORY_POOL1_BLOCK_SIZE, MEMORY_POOL1_BLOCK_COUNT, &poolID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_osPoolCreate(MEMORY_POOL2_BLOCK_SIZE, MEMORY_POOL2_BLOCK_COUNT, &poolID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    for(uint8_t block1 = 0 ; block1 < MEMORY_POOL1_BLOCK_COUNT; ++block1)
    {
        ptr1[block1] = pal_osPoolAlloc(poolID1);
        TEST_ASSERT_NOT_EQUAL(ptr1[block1], NULL);
    }
    /*#4*/
    for(uint8_t block2 = 0 ; block2 < MEMORY_POOL2_BLOCK_COUNT; ++block2)
    {
        ptr2[block2] = pal_osPoolCAlloc(poolID2);
        TEST_ASSERT_NOT_EQUAL(ptr2[block2], NULL);
    }
    /*#5*/
    for(uint8_t freeblock1 = 0; freeblock1 < MEMORY_POOL1_BLOCK_COUNT; ++freeblock1)
    {
        status = pal_osPoolFree(poolID1, ptr1[freeblock1]);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#6*/
    for(uint8_t freeblock2 = 0; freeblock2 < MEMORY_POOL2_BLOCK_COUNT; ++freeblock2)
    {
        status = pal_osPoolFree(poolID2, ptr2[freeblock2]);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#7*/
    status = pal_osPoolDestroy(&poolID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(poolID1, NULL);
    /*#8*/
    status = pal_osPoolDestroy(&poolID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(poolID2, NULL);
}

/*! \brief Creates a message queue.
* Puts a message in the queue, and reads the message from the queue.
* Verifies that the message has the expected value.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a MessageQueue using `pal_osMessageQueueCreate`.                      | PAL_SUCCESS |
* | 2 | Put a message in the queue using `pal_osMessagePut`.                         | PAL_SUCCESS |
* | 3 | Get a message from the queue using `pal_osMessageGet`.                      | PAL_SUCCESS |
* | 4 | Delete the MessageQueue using `pal_osMessageQueueDestroy`.                       | PAL_SUCCESS |
*/
TEST(pal_rtos, MessageUnityTest)
{
    palStatus_t status = PAL_SUCCESS;
    palMessageQID_t messageQID = NULLPTR;
    uint32_t infoToSend = 3215;
    uint32_t infoToGet = 0;

    /*#1*/
    status = pal_osMessageQueueCreate(10, &messageQID);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#2*/
    status = pal_osMessagePut(messageQID, infoToSend, 1500);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#3*/
    status = pal_osMessageGet(messageQID, 1500, &infoToGet);

    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL_UINT32(infoToSend, infoToGet);

    /*#4*/
    status = pal_osMessageQueueDestroy(&messageQID);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(messageQID, NULL);
}

/*! \brief Performs a single atomic increment call
* to an integer value and verifies that the result is as expected.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Call atomic increment using `pal_osAtomicIncrement` and check that the value was incremented. | PAL_SUCCESS |
*/
TEST(pal_rtos, AtomicIncrementUnityTest)
{
    int32_t num1 = 0;
    int32_t increment = 10;
    int32_t tmp = 0;
    int32_t original = num1;
    /*#1*/
    tmp = pal_osAtomicIncrement(&num1, increment);


    TEST_ASSERT_EQUAL(original + increment, tmp);

}

struct randBuf
{
    uint8_t rand[6];
};

/*! \brief Check the random APIs. For each API, the test calls the random API in a loop
* and stores the result. When the loop finishes, we verify that the count of the
* duplication in the stored values is less than the defined random margin value for each API.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Fill array with random 32bit values using `pal_osRandom32bit` in a loop.    | PAL_SUCCESS |
* | 2 | Check array for matching values and make sure there are not too many.         | PAL_SUCCESS |
* | 3 | Fill array with random values using `pal_osRandomUniform` in a loop.        | PAL_SUCCESS |
* | 4 | Check array for matching values and make sure there are not too many.         | PAL_SUCCESS |
* | 5 | Fill array with random byte sequences using `pal_osRandomBuffer` in a loop.  | PAL_SUCCESS |
* | 6 | Check array for matching values and make sure there are not too many.         | PAL_SUCCESS |
*/
TEST(pal_rtos, RandomUnityTest)
{
    palStatus_t status = PAL_SUCCESS;
    uint32_t randomArray[PAL_RANDOM_ARRAY_TEST_SIZE];
    struct randBuf randomBufArray[PAL_RANDOM_BUFFER_ARRAY_TEST_SIZE];
    uint32_t randomMargin = 0;
    uint32_t upperBound = PAL_MAX_UINT32; //This value need to be changed once PAL implements `pal_osRandomUniform` correctly

    memset(randomArray, 0x0, sizeof(randomArray));
    memset(randomBufArray, 0x0, sizeof(randomBufArray));
    /*#1*/
    for(int i = 0; i < PAL_RANDOM_ARRAY_TEST_SIZE ; ++i)
    {
        status = pal_osRandom32bit(&randomArray[i]);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#2*/
    for(int k = 0; k < PAL_RANDOM_ARRAY_TEST_SIZE ; ++k)
    {
        for (int j = k+1 ; j < PAL_RANDOM_ARRAY_TEST_SIZE ; ++j)
        {
            if (randomArray[k] == randomArray[j])
            {
                ++randomMargin;
            }
        }
        randomArray[k] = 0;
    }
    TEST_ASSERT_TRUE(20 >= randomMargin);
    randomMargin = 0;
    /*#3*/
    for(int i = 0; i < PAL_RANDOM_ARRAY_TEST_SIZE ; ++i)
    {
        status = pal_osRandomUniform(upperBound , &randomArray[i]);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#4*/
    for(int k = 0; k < PAL_RANDOM_ARRAY_TEST_SIZE ; ++k)
    {
        if (randomArray[k] > upperBound)
        {
            ++randomMargin;
        }
        randomArray[k] = 0;
    }

    TEST_ASSERT_TRUE(1 >= randomMargin);
    randomMargin = 0;
    /*#5*/
    for (int i = 0; i < PAL_RANDOM_BUFFER_ARRAY_TEST_SIZE ; ++i)
    {
        status = pal_osRandomBuffer(randomBufArray[i].rand, sizeof(randomBufArray[i].rand));
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#6*/
    for(int k = 0; k < PAL_RANDOM_BUFFER_ARRAY_TEST_SIZE ; ++k)
    {
        for (int j = k+1 ; j < PAL_RANDOM_BUFFER_ARRAY_TEST_SIZE ; ++j)
        {
            if(0 == memcmp(randomBufArray[k].rand, randomBufArray[j].rand, sizeof(uint8_t)*6))
            {
                ++randomMargin;
            }
        }
    }

    TEST_ASSERT_TRUE(10 >= randomMargin);
}


/*! \brief call the random API in a PAL_RANDOM_TEST_LOOP loop.
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Call `pal_osRandomBuffer` in a PAL_RANDOM_TEST_LOOP loop .         PAL_SUCCESS |
*/
TEST(pal_rtos, loopRandomBigNumber)
{
	palStatus_t status = PAL_SUCCESS;
	uint8_t loopRandomArray[PAL_RANDOM_ARRAY_TEST_SIZE];

	for (int i = 0; i < PAL_RANDOM_TEST_LOOP; ++i)
	{
		status = pal_osRandomBuffer(loopRandomArray, sizeof(loopRandomArray));
		TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
	}
}

/*! \brief Verify that PAL can handle multiple calls for `pal_init()` and `pal_destroy()`.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Call `pal_init`.                                   | PAL_SUCCESS |
* | 2 | Call `pal_init`.                                 | PAL_SUCCESS |
* | 3 | Call `pal_init`.                                 | PAL_SUCCESS |
* | 4 | Call `pal_destroy` in a loop untill init count == 0. | PAL_SUCCESS |
* | 5 | Call `pal_init`.                                   | PAL_SUCCESS |
* | 6 | Call `pal_destroy`.                                 | PAL_SUCCESS |
*/
TEST(pal_rtos, pal_init_test)
{
    palStatus_t status = PAL_SUCCESS;
    int32_t initCounter = 0;
    /*#1*/
    status = pal_init();
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_init();
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_init();
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#4*/
    do
    {
        initCounter = pal_destroy();
        //TEST_ASSERT_EQUAL_HEX(0, initCounter);

    }while(initCounter != 0);

    /*#5*/
    status = pal_init();
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#6*/
    initCounter = pal_destroy();
    TEST_ASSERT_EQUAL_HEX(0, initCounter);
}

/*! \brief This test does not run by default in the PAL Unity tets.
* It's called "customized" because the purpose of it is to provide a test structure
* for a developer who wants to check a specific API.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a thread that runs `palThreadFuncCustom1` using `pal_osThreadCreateWithAlloc`.  | PAL_SUCCESS |
* | 2 | Create a thread that runs `palThreadFuncCustom2` using `pal_osThreadCreateWithAlloc`.  | PAL_SUCCESS |
* | 3 | Sleep.                                                                                 | PAL_SUCCESS |
* | 4 | Terminate the first thread.                                                            | PAL_SUCCESS |
* | 5 | Terminate the second thread.                                                           | PAL_SUCCESS |
* | 6 | Create a thread that runs `palThreadFuncCustom1` using `pal_osThreadCreateWithAlloc`.  | PAL_SUCCESS |
* | 7 | Create a thread that runs `palThreadFuncCustom2` using `pal_osThreadCreateWithAlloc`.  | PAL_SUCCESS |
* | 8 | compare threads index 						                                           | PAL_SUCCESS |
* | 9 | check threadIDs are not equal.		                                                   | PAL_SUCCESS |
* | 10 | Sleep.                                                                                 | PAL_SUCCESS |
* | 11 | Terminate the first thread.                                                            | PAL_SUCCESS |
* | 12 | Terminate again the first thread.                                                      | PAL_SUCCESS |
* | 13 | Terminate the second thread.                                                          | PAL_SUCCESS |
*
*/


TEST(pal_rtos, ThreadReCreateSamePriority)
{
    palStatus_t status = PAL_SUCCESS;

    palThreadID_t threadID1 = NULLPTR;
    palThreadID_t threadID2 = NULLPTR;
    palThreadID_t threadIndex = NULLPTR;

    /*#1*/
    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom1, NULL, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    threadIndex =  threadID1;
    /*#2*/
    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom2, NULL, PAL_osPriorityHigh, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    pal_osDelay(3000);
    /*#4*/
    // We deliberately dont terminate threadID1, it should end by itself
    /*#5*/
    // We deliberately dont terminate threadID2, it should end by itself
    /*#6*/
    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom1, NULL, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom2, NULL, PAL_osPriorityHigh, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#8*/
    TEST_ASSERT_EQUAL_UINT32(PAL_GET_THREAD_INDEX(threadIndex),PAL_GET_THREAD_INDEX(threadID1));
    /*#9*/
    TEST_ASSERT_NOT_EQUAL(threadIndex,threadID1);
    /*#10*/
    pal_osDelay(3000);
    /*#11*/
    status = pal_osThreadTerminate(&threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#12*/
    status = pal_osThreadTerminate(&threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#13*/
    status = pal_osThreadTerminate(&threadID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    pal_osDelay(500);

    mutex1 = NULLPTR;
    status = pal_osMutexCreate(&mutex1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osMutexWait(mutex1, PAL_RTOS_WAIT_FOREVER);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom3, NULL, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osThreadCreateWithAlloc(palThreadFuncCustom4, NULL, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID2);
#if PAL_UNIQUE_THREAD_PRIORITY
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_RTOS_PRIORITY, status);
#else
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    status = pal_osThreadTerminate(&threadID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
#endif

    status = pal_osMutexRelease(mutex1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osThreadTerminate(&threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osMutexDelete(&mutex1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(NULL, mutex1);

}

/*! \brief Check derivation of keys from the platform's Root of Trust using the KDF algorithm.
 *
 * 
 *
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Start a loop to perform the following steps.                                               |  |
* | 2 | Derive a device key for encryption using `pal_osGetDeviceKey`.                   | PAL_SUCCESS |
* | 3 | Derive a device key for signing using `pal_osGetDeviceKey`.                      | PAL_SUCCESS |
* | 4 | Call `pal_osGetDeviceKey` with invalid arguments.                              | PAL_FAILURE |
* | 5 | Call `pal_osGetDeviceKey` with invalid arguments.                              | PAL_FAILURE |
* | 6 | Check that the derived signing and encryption keys are different.                     | PAL_SUCCESS |
* | 7 | Check that all integrations of each type of derivation return the same value.       | PAL_SUCCESS |
 */
TEST(pal_rtos, GetDeviceKeyTest_CMAC)
{
    palStatus_t status = PAL_SUCCESS;
    size_t keyLenBytes = 16;
    uint8_t timesToDerive = 4;
    unsigned char encKeyDerive[timesToDerive][keyLenBytes]; //16 bytes=128bit
    unsigned char signKeyDerive[timesToDerive][keyLenBytes]; //16 bytes=128bit
    /*#1*/
    for (int i=0; i < timesToDerive; i++)
    {
        /*#2*/
        status = pal_osGetDeviceKey(palOsStorageEncryptionKey128Bit, encKeyDerive[i], keyLenBytes);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
        /*#3*/
        status = pal_osGetDeviceKey(palOsStorageSignatureKey128Bit,  signKeyDerive[i], keyLenBytes);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
        /*#4*/
        status = pal_osGetDeviceKey(palOsStorageSignatureKey128Bit,  signKeyDerive[i], keyLenBytes-1);
        TEST_ASSERT_NOT_EQUAL(PAL_SUCCESS, status);
        /*#5*/
        status = pal_osGetDeviceKey(palOsStorageSignatureKey128Bit,  NULL, keyLenBytes);
        TEST_ASSERT_NOT_EQUAL(PAL_SUCCESS, status);
        /*#6*/
        status = memcmp(encKeyDerive[i], signKeyDerive[i], keyLenBytes);
        TEST_ASSERT_NOT_EQUAL(status,0); //The keys MUST be different!
        /*#7*/
        if (i > 0) //Make sure key derivation is persistent every time
        {
            TEST_ASSERT_EQUAL_MEMORY(encKeyDerive[i-1], encKeyDerive[i], keyLenBytes);
            TEST_ASSERT_EQUAL_MEMORY(signKeyDerive[i-1], signKeyDerive[i], keyLenBytes);

        } //if

    } //for

}

/*! \brief Check derivation of keys from the platform's Root of Trust using the KDF algorithm.
 *
 * 
 *
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Start a loop to perform the following steps.                                   |  |
* | 2 | Derive a device key for encryption using `pal_osGetDeviceKey`.                 | PAL_SUCCESS |
* | 3 | Call `pal_osGetDeviceKey` with invalid arguments.                              | PAL_FAILURE |
* | 4 | Call `pal_osGetDeviceKey` with invalid arguments.                              | PAL_FAILURE |
* | 5 | Check that all integrations of each type of derivation return the same value.  | PAL_SUCCESS |
 */
TEST(pal_rtos, GetDeviceKeyTest_HMAC_SHA256)
{
    palStatus_t status = PAL_SUCCESS;
    size_t keyLenBytes = 32;
    uint8_t timesToDerive = 4;
    unsigned char encKeyDerive[timesToDerive][keyLenBytes]; //32 bytes=256bit
    /*#1*/
    for (int i=0; i < timesToDerive; i++)
    {
        /*#2*/
        status = pal_osGetDeviceKey(palOsStorageHmacSha256, encKeyDerive[i], keyLenBytes);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
        /*#4*/
        status = pal_osGetDeviceKey(palOsStorageHmacSha256,  encKeyDerive[i], keyLenBytes-1);
        TEST_ASSERT_NOT_EQUAL(PAL_SUCCESS, status);
        /*#5*/
        status = pal_osGetDeviceKey(palOsStorageHmacSha256,  NULL, keyLenBytes);
        TEST_ASSERT_NOT_EQUAL(PAL_SUCCESS, status);
        /*#7*/
        if (i > 0) //Make sure key derivation is persistent every time
        {
            TEST_ASSERT_EQUAL_MEMORY(encKeyDerive[i-1], encKeyDerive[i], keyLenBytes);
        } //if

    } //for

}

/*! \brief Check the APIs `pal_osSetTime()` and `pal_osGetTime()` with different scenarios
* for valid and non-valid scenarios and epoch values.
* The test also checks that the time increases.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Get time using `pal_osGetTime`.                                                      | PAL_SUCCESS |
* | 2 | Set time to invalid value using `pal_osSetTime`.                                     | PAL_ERR_INVALID_TIME |
* | 3 | Get time using `pal_osGetTime`.                                                      | PAL_SUCCESS |
* | 4 | Start a loop for the following steps.                                                | PAL_SUCCESS |
* | 5 | Set time to invalid value using `pal_osSetTime`.                                 | PAL_ERR_INVALID_TIME |
* | 6 | Get time using `pal_osGetTime`.                                                  | PAL_SUCCESS |
* | 7 | Set time to valid value using `pal_osSetTime`.                                   | PAL_SUCCESS |
* | 8 | Sleep.                                                                         | PAL_SUCCESS |
* | 9 | Get time using `pal_osGetTime` and check that it equals set time + sleep time.   | PAL_SUCCESS |
*/
TEST(pal_rtos, RealTimeClockTest1)
{
    palStatus_t status;
    uint64_t curTime;
    uint64_t lastTimeSeen = 0;
    const uint64_t minSecSinceEpoch = PAL_MIN_SEC_FROM_EPOCH + 1; //At least 47 years passed from 1.1.1970 in seconds

    /*#1*/
    curTime = pal_osGetTime();
    TEST_ASSERT_EQUAL(0, curTime); //Time was not previously set; 0 is acceptable
    /*#2*/
    status = pal_osSetTime(3);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_TIME, status); // Less than current epoch time -> error
    /*#3*/
    curTime = pal_osGetTime();
    TEST_ASSERT_EQUAL(lastTimeSeen, curTime); //Time was not previously set; 0 is acceptable

    /*#4*/
    for (int i=0; i < 2; i++)
    {
    /*#5*/
        status = pal_osSetTime(3);
        TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_TIME, status); // Less than current epoch time -> error

    /*#6*/
        curTime = pal_osGetTime();
        TEST_ASSERT_TRUE(lastTimeSeen <= curTime); //Time was not previously set; 0 is acceptable
    /*#7*/
        status = pal_osSetTime(minSecSinceEpoch);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status); // More than current epoch time -> success
    /*#8*/
        int milliDelay = 1500;
        pal_osDelay(milliDelay); //500 milliseconds
    /*#9*/
        curTime = pal_osGetTime();
        TEST_ASSERT_TRUE(curTime > minSecSinceEpoch);
        TEST_PRINTF("Current sys time in sec:%lld after delay:%lld\n", curTime, minSecSinceEpoch+(int)ceil((float)milliDelay/1000));
        TEST_ASSERT_TRUE(curTime <= minSecSinceEpoch+(int)ceil((float)milliDelay/1000));
        lastTimeSeen = curTime;
    }
}


/*! \brief Check recursive mutex behavior.
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a mutex using `pal_osMutexCreate`.                                             | PAL_SUCCESS |
* | 2 | Create a semaphore using `pal_osSemaphoreCreate`.                                     | PAL_SUCCESS |
* | 3 | Create a thread running `RecursiveLockThread` using `pal_osThreadCreateWithAlloc`.       | PAL_SUCCESS |
* | 4 | Create a thread running `RecursiveLockThread` using `pal_osThreadCreateWithAlloc`.       | PAL_SUCCESS |
* | 5 | Release the semaphore using `pal_osSemaphoreRelease`.                                    | PAL_SUCCESS |
* | 6 | Release the semaphore using `pal_osSemaphoreRelease`.                                    | PAL_SUCCESS |
* | 7 | Sleep for a short interval.                                                          | PAL_SUCCESS |
* | 8 | Wait for the semaphore using `pal_osSemaphoreWait`.                                      | PAL_SUCCESS |
* | 9 | Wait for the semaphore using `pal_osSemaphoreWait`.                                      | PAL_SUCCESS |
* | 10 | Terminate the first thread using `pal_osThreadTerminate`.                               | PAL_SUCCESS |
* | 11 | Terminate the second thread using `pal_osThreadTerminate`.                              | PAL_SUCCESS |
* | 12 | Delete the mutex using `pal_osMutexDelete`.                                           | PAL_SUCCESS |
* | 13 | Delete the semaphore using `pal_osSemaphoreDelete`.                                   | PAL_SUCCESS |
*/
TEST(pal_rtos, Recursive_Mutex_Test)
{
    palStatus_t status;
    int32_t val = 0;

    recursiveMutexData = malloc(sizeof(palRecursiveMutexParam_t));
    TEST_ASSERT_NOT_NULL(recursiveMutexData);
    memset(recursiveMutexData, 0, sizeof(palRecursiveMutexParam_t));
    /*#1*/
    status = pal_osMutexCreate(&(recursiveMutexData->mtx));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_osSemaphoreCreate(0, &(recursiveMutexData->sem));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_osThreadCreateWithAlloc(RecursiveLockThread, (void*)recursiveMutexData, PAL_osPriorityHigh, PAL_TEST_THREAD_STACK_SIZE, NULL, &(recursiveMutexData->higherPriorityThread));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
     status = pal_osThreadCreateWithAlloc(RecursiveLockThread, (void*)recursiveMutexData, PAL_osPriorityAboveNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &(recursiveMutexData->lowerPriorityThread));
     TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_osSemaphoreRelease(recursiveMutexData->sem);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#6*/
    status = pal_osSemaphoreRelease(recursiveMutexData->sem);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    pal_osDelay(1000);
    /*#8*/
    status = pal_osSemaphoreWait(recursiveMutexData->sem, PAL_RTOS_WAIT_FOREVER, &val);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#9*/
    status = pal_osSemaphoreWait(recursiveMutexData->sem, PAL_RTOS_WAIT_FOREVER, &val);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_EQUAL(0, val);
    TEST_ASSERT_EQUAL_HEX(NULLPTR, recursiveMutexData->activeThread);
    /*#10*/
    status = pal_osThreadTerminate(&(recursiveMutexData->higherPriorityThread));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#11*/
    status = pal_osThreadTerminate(&(recursiveMutexData->lowerPriorityThread));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#12*/
    status = pal_osMutexDelete(&(recursiveMutexData->mtx));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#13*/
    status = pal_osSemaphoreDelete(&recursiveMutexData->sem);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    TEST_ASSERT_EQUAL(400, recursiveMutexData->count);

    free(recursiveMutexData);
    recursiveMutexData = NULL;
}
