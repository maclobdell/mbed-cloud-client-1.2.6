#include "pal.h"
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>

#ifndef PRIMARY_PARTITION_NAME
#define PRIMARY_PARTITION_NAME "/dev/mmcblk0p3"
#endif

#ifndef SECONDARY_PARTITION_NAME
#define SECONDARY_PARTITION_NAME "/dev/mmcblk0p4"
#endif

#ifndef PAL_PARTITION_FORMAT_TYPE
#define PAL_PARTITION_FORMAT_TYPE "ext4"
#endif

#ifndef PARTITION_FORMAT_ADDITIONAL_PARAMS
#define PARTITION_FORMAT_ADDITIONAL_PARAMS NULL
#endif
// Desktop Linux
// In order for tests to pass for all partition configurations we need to simulate the case of multiple
// partitions using a single folder. We do this by creating one or two different sub-folders, depending on
// the configuration.
palStatus_t fileSystemCreateRootFolders(void)
{
	palStatus_t status = PAL_SUCCESS;
	char folder[PAL_MAX_FILE_AND_FOLDER_LENGTH] = {0};

	// Get default mount point.
	status = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FILE_AND_FOLDER_LENGTH, folder);
	if(status != PAL_SUCCESS)
	{
	    return PAL_ERR_GENERIC_FAILURE;
	}
	printf("Mount point for primary partition: %s\r\n",folder);
	// Make the sub-folder
	int res = mkdir(folder,0744);
    if(res)
    {
        // Ignore error if it exists
        if( errno != EEXIST)
        {
        	printf("mkdir failed errno= %d\r\n",errno);
            return PAL_ERR_GENERIC_FAILURE;
        }
    }

    res = mount(PRIMARY_PARTITION_NAME, folder, PAL_PARTITION_FORMAT_TYPE, 0 ,PARTITION_FORMAT_ADDITIONAL_PARAMS);
    if (res)
    {
        if( errno != EBUSY)
        {
        	printf("mounting failed %d\r\n",errno);
            return PAL_ERR_GENERIC_FAILURE;
        }
    }
    // Get default mount point.
    memset(folder,0,sizeof(folder));
    status = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FILE_AND_FOLDER_LENGTH, folder);
    printf("Mount point for secondary partition: %s\r\n",folder);
    if(status != PAL_SUCCESS)
    {
        return PAL_ERR_GENERIC_FAILURE;
    }

    // Make the sub-folder
    res = mkdir(folder,0744);
    if(res)
    {
        // Ignore error if it exists
        if( errno != EEXIST)
        {
        	printf("mkdir failed errno= %d\r\n",errno);
            return PAL_ERR_GENERIC_FAILURE;
        }
    }

    res = mount(SECONDARY_PARTITION_NAME, folder,PAL_PARTITION_FORMAT_TYPE ,0 ,PARTITION_FORMAT_ADDITIONAL_PARAMS);
    if (res)
    {
        if( errno != EBUSY)
        {
        	printf("mounting failed %d\r\n",errno);
            return PAL_ERR_GENERIC_FAILURE;
        }
    }

    if(status != PAL_SUCCESS)
    {
        return PAL_ERR_GENERIC_FAILURE;
    }

	return PAL_SUCCESS;
}
