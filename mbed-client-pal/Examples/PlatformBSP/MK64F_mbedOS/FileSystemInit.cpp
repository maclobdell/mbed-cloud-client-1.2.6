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
#include "mbed.h"
#include "FATFileSystem.h"
#include "SDBlockDevice.h"

#include "MBRBlockDevice.h"

#ifndef PRIMARY_PARTITION_NUMBER
#define PRIMARY_PARTITION_NUMBER 1
#endif

#ifndef PRIMARY_PARTITION_START
#define PRIMARY_PARTITION_START 0
#endif

#ifndef PRIMARY_PARTITION_SIZE
#define PRIMARY_PARTITION_SIZE 512*1024
#endif

#ifndef SECONDARY_PARTITION_NUMBER
#define SECONDARY_PARTITION_NUMBER 2
#endif

#ifndef SECONDARY_PARTITION_START
#define SECONDARY_PARTITION_START PRIMARY_PARTITION_SIZE
#endif

#ifndef SECONDARY_PARTITION_SIZE
#define SECONDARY_PARTITION_SIZE PRIMARY_PARTITION_SIZE
#endif

/*MBED_LIBRARY_VERSION 129 stands for mbed-os 5.2.2*/
#ifdef MBED_LIBRARY_VERSION
#if (MBED_LIBRARY_VERSION < 129)
#endif
#endif //MBED_LIBRARY_VERSION


//uncomment this to create the partitions
#define PAL_EXAMPLE_GENERATE_PARTITION 1

//
// See the mbed_lib.json in the sd-driver library for the definitions.
// See the sd-driver library README.md for details with CI-shield etc.
// Add also new boards/exceptions there rather than in code directly
// OR
// alternatively overload via your mbed_app.json (MBED_CONF_APP...)
//

#if defined (MBED_CONF_APP_SPI_MOSI) && defined (MBED_CONF_APP_SPI_MISO) && defined (MBED_CONF_APP_SPI_CLK) && defined (MBED_CONF_APP_SPI_CS)
    SDBlockDevice sd(MBED_CONF_APP_SPI_MOSI, MBED_CONF_APP_SPI_MISO, MBED_CONF_APP_SPI_CLK, MBED_CONF_APP_SPI_CS);
#else
    SDBlockDevice sd(MBED_CONF_SD_SPI_MOSI, MBED_CONF_SD_SPI_MISO, MBED_CONF_SD_SPI_CLK, MBED_CONF_SD_SPI_CS);
#endif

//This trick (the adding of 1) is to skip the '/' that is needed for FS but not needed to init
FATFileSystem fat1(((char*)PAL_FS_MOUNT_POINT_PRIMARY+1));

#if (PAL_NUMBER_OF_PARTITIONS > 0)
MBRBlockDevice part1(&sd,1);
#if (PAL_NUMBER_OF_PARTITIONS == 2)
MBRBlockDevice part2(&sd,2);
//This trick (the adding of 1) is to skip the '/' that is needed for FS but not needed to init
FATFileSystem fat2(((char*)PAL_FS_MOUNT_POINT_SECONDARY+1));
#endif
#endif


static int initPartition(uint8_t partitionNumber, BlockDevice* bd,FATFileSystem* fs)
{
	int err = bd->init();
	if (err < 0)
	{
		printf("Failed to initialize 1st partition cause %d\r\n",err);
#ifdef PAL_EXAMPLE_GENERATE_PARTITION
		printf("Trying to create the partition\r\n");
		if (PRIMARY_PARTITION_NUMBER == partitionNumber)
		{
			err = MBRBlockDevice::partition(&sd, PRIMARY_PARTITION_NUMBER, 0x83, PRIMARY_PARTITION_START, PRIMARY_PARTITION_START + PRIMARY_PARTITION_SIZE);
		}
		else if (SECONDARY_PARTITION_NUMBER == partitionNumber)
		{
			err = MBRBlockDevice::partition(&sd, SECONDARY_PARTITION_NUMBER, 0x83, SECONDARY_PARTITION_START, SECONDARY_PARTITION_START + SECONDARY_PARTITION_SIZE);
		}
		else
		{
			printf("Wrong partition number %d\r\n",partitionNumber);
			err = -1;
		}
		if (err < 0)
		{
			printf("Failed to create the partition cause %d\r\n",err);
		}
		else
		{
			err = bd->init();
		}
#endif
	}
	if (!err)
	{
		err = fs->mount(bd);
		if (err < 0)
		{
			err = FATFileSystem::format(bd);
			if (err < 0)
			{
				printf("failed to format part cause %d\r\n",err);
			}
			else
			{
				err = fs->mount(bd);
				if (err < 0)
				{
					printf("failed to mount cause %d\r\n",err);
				}
			}
		}
	}
	return err;
}

int initSDcardAndFileSystem(void)
{
	printf("Initializing the file system\r\n");
#if (PAL_NUMBER_OF_PARTITIONS == 0 )
	int err = initPartition(0, &sd, &fat1);
	if (err < 0)
	{
		printf("Failed to initialize primary partition\r\n");
	}
#elif (PAL_NUMBER_OF_PARTITIONS > 0) // create and mount an additional partition
	int err = initPartition(PRIMARY_PARTITION_NUMBER, &part1,&fat1);
	if (err < 0)
	{
		printf("Failed to initialize primary partition\r\n");
	}
#if (PAL_NUMBER_OF_PARTITIONS == 2)
	else
	{
		err = initPartition(SECONDARY_PARTITION_NUMBER, &part2,&fat2);
		if (err < 0)
		{
			printf("Failed to initialize secondary partition\r\n");
		}
	}
#endif
#endif
	return err;
}

