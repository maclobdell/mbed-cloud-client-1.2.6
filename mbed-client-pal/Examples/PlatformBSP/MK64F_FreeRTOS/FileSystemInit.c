#include "pal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_mpu.h"
#include "ff.h"
#include "diskio.h"
#include "sdhc_config.h"
#include "fsl_debug_console.h"


//uncomment this to create the partitions
//#define PAL_EXAMPLE_GENERATE_PARTITION 1

#if (PAL_NUMBER_OF_PARTITIONS > 0)

#ifndef _MULTI_PARTITION
#error "Please Define _MULTI_PARTITION in ffconf.h"
#endif

#if ((PAL_NUMBER_OF_PARTITIONS > 2) || (PAL_NUMBER_OF_PARTITIONS < 0))
#error "Pal partition number is not supported, please set to a number between 0 and 2"
#endif

PARTITION VolToPart[] = {
#if (PAL_NUMBER_OF_PARTITIONS > 0)
		{SDDISK,1}, /* 0: */
#endif
#if (PAL_NUMBER_OF_PARTITIONS > 1)
		{SDDISK,2}  /* 1: */
#endif
};
#endif

bool FileSystemInit = false;
#define MAX_SD_READ_RETRIES	5
#define LABEL_LENGTH	66
/*!
 * @brief Get event instance.
 * @param eventType The event type
 * @return The event instance's pointer.
 */
PAL_PRIVATE volatile uint32_t *EVENT_GetInstance(event_t eventType);

/*! @brief Transfer complete event. */
PAL_PRIVATE volatile uint32_t g_eventTransferComplete;

PAL_PRIVATE volatile uint32_t g_eventSDReady;

/*! @brief Time variable unites as milliseconds. */
PAL_PRIVATE volatile uint32_t g_timeMilliseconds;

/*! @brief Preallocated Work area (file system object) for logical drive, should NOT be free or lost*/
PAL_PRIVATE FATFS fileSystem[2];

/*! \brief CallBack function for SD card initialization
 *		   Set systick reload value to generate 1ms interrupt
 * @param void
 *
 * \return void
 *
 */
void EVENT_InitTimer(void)
{
	/* Set systick reload value to generate 1ms interrupt */
	SysTick_Config(CLOCK_GetFreq(kCLOCK_CoreSysClk) / 1000U);
}


/*! \brief CallBack function for SD card initialization
 *
 * @param void
 *
 * \return pointer to the requested instance
 *
 */
PAL_PRIVATE volatile uint32_t *EVENT_GetInstance(event_t eventType)
{
	volatile uint32_t *event;

	switch (eventType)
	{
	case kEVENT_TransferComplete:
		event = &g_eventTransferComplete;
		break;
	default:
		event = NULL;
		break;
	}

	return event;
}

/*! \brief CallBack function for SD card initialization
 *
 * @param event_t
 *
 * \return TRUE if instance was found
 *
 */
bool EVENT_Create(event_t eventType)
{
	volatile uint32_t *event = EVENT_GetInstance(eventType);

	if (event)
	{
		*event = 0;
		return true;
	}
	else
	{
		return false;
	}
}

/*! \brief blockDelay - Blocks the task and count the number of ticks given
 *
 * @param void
 *
 * \return TRUE - on success
 *
 */
void blockDelay(uint32_t Ticks)
{
	uint32_t tickCounts = 0;
	for(tickCounts = 0; tickCounts < Ticks; tickCounts++){}
}


/*! \brief CallBack function for SD card initialization
 *
 * @param void
 *
 * \return TRUE - on success
 *
 */
bool EVENT_Wait(event_t eventType, uint32_t timeoutMilliseconds)
{
	uint32_t startTime;
	uint32_t elapsedTime;

	volatile uint32_t *event = EVENT_GetInstance(eventType);

	if (timeoutMilliseconds && event)
	{
		startTime = g_timeMilliseconds;
		do
		{
			elapsedTime = (g_timeMilliseconds - startTime);
		} while ((*event == 0U) && (elapsedTime < timeoutMilliseconds));
		*event = 0U;

		return ((elapsedTime < timeoutMilliseconds) ? true : false);
	}
	else
	{
		return false;
	}
}

/*! \brief CallBack function for SD card initialization
 *
 * @param eventType
 *
 * \return TRUE if instance was found
 *
 */
bool EVENT_Notify(event_t eventType)
{
	volatile uint32_t *event = EVENT_GetInstance(eventType);

	if (event)
	{
		*event = 1U;
		return true;
	}
	else
	{
		return false;
	}
}

/*! \brief CallBack function for SD card initialization
 *
 * @param eventType
 *
 * \return void
 *
 */
void EVENT_Delete(event_t eventType)
{
	volatile uint32_t *event = EVENT_GetInstance(eventType);

	if (event)
	{
		*event = 0U;
	}
}


/*! \brief This function mount the fatfs on and SD card
 *
 * @param void
 *
 * \return palStatus_t - PAL_SUCCESS when mount point succeeded
 *
 */

void fileSystemMountDrive(void)
{
	char folder1[PAL_MAX_FILE_AND_FOLDER_LENGTH] = {0};
	char folder2[PAL_MAX_FILE_AND_FOLDER_LENGTH] = {0};
	PRINTF("%s : Creating FileSystem SetUp thread!\r\n",__FUNCTION__);
	FRESULT fatResult;
	int count = 0;
	palStatus_t status = PAL_SUCCESS;

	if (FileSystemInit == false)
	{
		//Detected SD card inserted
		while (!(GPIO_ReadPinInput(BOARD_SDHC_CD_GPIO_BASE, BOARD_SDHC_CD_GPIO_PIN)))
		{
			blockDelay(1000U);
			if (count++ > MAX_SD_READ_RETRIES)
			{
				break;
			}
		}

		if(count < MAX_SD_READ_RETRIES)
		{
			/* Delay some time to make card stable. */
			blockDelay(10000000U);
#ifdef PAL_EXAMPLE_GENERATE_PARTITION
#if (PAL_NUMBER_OF_PARTITIONS == 1)
			DWORD plist[] = {100,0,0,0};
#elif	(PAL_NUMBER_OF_PARTITIONS == 2) //else of (PAL_NUMBER_OF_PARTITIONS == 1)
			DWORD plist[] = {50,50,0,0};
#endif //(PAL_NUMBER_OF_PARTITIONS == 1)
			BYTE work[_MAX_SS];

			fatResult= f_fdisk(SDDISK,plist, work);
			PRINTF("f_fdisk fatResult=%d\r\n",fatResult);
			if (FR_OK != fatResult)
			{
				PRINTF("Failed to create partitions in disk\r\n");
			}
#endif //PAL_EXAMPLE_GENERATE_PARTITION


			status = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY,PAL_MAX_FILE_AND_FOLDER_LENGTH,folder1);
			if (PAL_SUCCESS == status)
			{
                fatResult = f_mount(&fileSystem[0], folder1, 1U);
                if (FR_OK != fatResult)
                {
                    PRINTF("Failed to mount partition %s in disk\r\n",folder1);
                }
			}
			else
			{
			    PRINTF("Failed to get mount point for primary partition\r\n");
			}

			status = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY,PAL_MAX_FILE_AND_FOLDER_LENGTH,folder2);
            if (PAL_SUCCESS == status)
            {
                //if there is a different root folder for partition 1 and 2, mount the 2nd partition
                if (strncmp(folder1,folder2,PAL_MAX_FILE_AND_FOLDER_LENGTH))
                {
                    fatResult = f_mount(&fileSystem[1], folder2, 1U);
                    if (FR_OK != fatResult)
                    {
                        PRINTF("Failed to mount partition %s in disk\r\n",folder2);
                    }
                }
            }
            else
            {
                PRINTF("Failed to get mount point for secondary partition\r\n");
            }

			if (fatResult == FR_OK)
			{
				FileSystemInit = true;
				PRINTF("%s : Exit FileSystem SetUp thread!\r\n",__FUNCTION__);
			}
		}
	}
	vTaskDelete( NULL );
}
