/*
 * 2.c
 *
 *  Created on: 22 Jan 2017
 *      Author: alonof01
 */

#include "pal.h"
#include "pal_fileSystem.h"
#include "pal_plat_fileSystem.h"

PAL_PRIVATE char* g_RootFolder[PAL_FS_PARTITION_LAST] = { NULL , NULL };      //!< global var that holds the  root folder
PAL_PRIVATE bool g_RootFolderIsSet[PAL_FS_PARTITION_LAST] = { false, false };    //!< global var that holds the state root folder


void pal_fsCleanup()
{
    if (NULL != g_RootFolder[PAL_FS_PARTITION_PRIMARY])
    {
        free(g_RootFolder[PAL_FS_PARTITION_PRIMARY]);
        g_RootFolder[PAL_FS_PARTITION_PRIMARY] = NULL;
    }

    if (NULL != g_RootFolder[PAL_FS_PARTITION_SECONDARY])
    {
        free(g_RootFolder[PAL_FS_PARTITION_SECONDARY]);
        g_RootFolder[PAL_FS_PARTITION_SECONDARY] = NULL;
    }

    g_RootFolder[PAL_FS_PARTITION_SECONDARY] = NULL;
    g_RootFolderIsSet[PAL_FS_PARTITION_PRIMARY] = false;
    g_RootFolderIsSet[PAL_FS_PARTITION_SECONDARY] = false;
}



palStatus_t pal_fsMkDir(const char *pathName)
{
    palStatus_t ret = PAL_SUCCESS;
    if (pathName == NULL)
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(pathName) >= PAL_MAX_FOLDER_DEPTH_CHAR)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
        ret = pal_plat_fsMkdir(pathName);
        if ((PAL_SUCCESS != ret) && (PAL_ERR_FS_NAME_ALREADY_EXIST != ret))
        {
            PAL_LOG(ERR, "Failed to create folder, was the storage properly initialized?");
        }
    }
    return ret;
}



palStatus_t pal_fsRmDir(const char *pathName)
{
    palStatus_t ret = PAL_SUCCESS;
    if (pathName == NULL)
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(pathName) >= PAL_MAX_FOLDER_DEPTH_CHAR)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
        ret = pal_plat_fsRmdir(pathName);
    }
    return ret;
}

palStatus_t pal_fsFopen(const char *pathName, pal_fsFileMode_t mode, palFileDescriptor_t *fd)
{
    palStatus_t ret = PAL_SUCCESS;
    if (fd == NULL)
    {
        return PAL_ERR_FS_INVALID_ARGUMENT;
    }
    if (pathName == NULL)
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(pathName) >= PAL_MAX_FULL_FILE_NAME)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else if (!((mode > PAL_FS_FLAG_KEEP_FIRST) && (mode < PAL_FS_FLAG_KEEP_LAST)))
    {
        ret = PAL_ERR_FS_INVALID_OPEN_FLAGS;
    }
    else
    {
        ret = pal_plat_fsFopen(pathName,  mode, fd);
    }
    if (ret != PAL_SUCCESS)
    {
        PAL_LOG(ERR, "Failed to open/create file, was the storage properly initialized?");
        *fd = 0;
    }
    return ret;
}


palStatus_t pal_fsFclose(palFileDescriptor_t *fd)
{
    palStatus_t ret = PAL_SUCCESS;
    if (fd == NULL)
    {
        return PAL_ERR_FS_INVALID_ARGUMENT;
    }
    if (*fd == 0)
    {
        ret = PAL_ERR_FS_BAD_FD;
    }
    else
    {
        ret = pal_plat_fsFclose(fd);
        *fd = 0;
    }
    return ret;
}


palStatus_t pal_fsFread(palFileDescriptor_t *fd, void * buffer, size_t numOfBytes, size_t *numberOfBytesRead)
{
    palStatus_t ret = PAL_SUCCESS;
    *numberOfBytesRead = 0;
    if (*fd == 0)
    {
        ret = PAL_ERR_FS_BAD_FD;
    }

    else if (buffer == NULL)
    {
        ret = PAL_ERR_FS_BUFFER_ERROR;
    }
    else
    {
        ret = pal_plat_fsFread(fd, buffer, numOfBytes, numberOfBytesRead);
    }
    return ret;
}


palStatus_t pal_fsFwrite(palFileDescriptor_t *fd, const void * buffer, size_t numOfBytes, size_t *numberOfBytesWritten)
{
    palStatus_t ret = PAL_SUCCESS;
    *numberOfBytesWritten = 0;
    if (*fd == 0)
    {
        ret = PAL_ERR_FS_BAD_FD;
    }
    else if (numOfBytes == 0)
    {
        ret = PAL_ERR_FS_LENGTH_ERROR;
    }
    else if (buffer == NULL)
    {
        ret = PAL_ERR_FS_BUFFER_ERROR;
    }
    else
    {
        ret = pal_plat_fsFwrite(fd, buffer, numOfBytes, numberOfBytesWritten);
    }
    return ret;
}


palStatus_t pal_fsFseek(palFileDescriptor_t *fd, int32_t offset, pal_fsOffset_t whence)
{
    palStatus_t ret = PAL_SUCCESS;
    if (*fd == 0)
    {
        ret = PAL_ERR_FS_BAD_FD;
    }
    else if (!((whence < PAL_FS_OFFSET_KEEP_LAST) && (whence > PAL_FS_OFFSET_KEEP_FIRST)))
    {
        ret = PAL_ERR_FS_OFFSET_ERROR;
    }
    else
    {
        ret = pal_plat_fsFseek(fd, offset, whence);
    }
    return ret;
}


palStatus_t pal_fsFtell(palFileDescriptor_t *fd, int32_t *pos)
{
    palStatus_t ret = PAL_SUCCESS;
    if (*fd == 0)
    {
        ret = PAL_ERR_FS_BAD_FD;
    }
    else
    {
        ret = pal_plat_fsFtell(fd, pos);
    }
    return ret;
}

palStatus_t pal_fsUnlink(const char *pathName)
{
    palStatus_t ret = PAL_SUCCESS;
    if (pathName == NULL)
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(pathName) >= PAL_MAX_FULL_FILE_NAME)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
        ret = pal_plat_fsUnlink(pathName);
    }
    return ret;
}



palStatus_t pal_fsRmFiles(const char *pathName)
{
    palStatus_t ret = PAL_SUCCESS;
    if (pathName == NULL)
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(pathName) >= PAL_MAX_FOLDER_DEPTH_CHAR)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
        ret = pal_plat_fsRmFiles(pathName);
    }
    return ret;
}


palStatus_t pal_fsCpFolder(const char *pathNameSrc,  char *pathNameDest)
{
    palStatus_t ret = PAL_SUCCESS;
    if ((pathNameSrc == NULL) || ((pathNameDest == NULL)))
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if ((pal_plat_fsSizeCheck(pathNameSrc) >= PAL_MAX_FOLDER_DEPTH_CHAR) || (pal_plat_fsSizeCheck(pathNameDest) >= PAL_MAX_FOLDER_DEPTH_CHAR))
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
        ret = pal_plat_fsCpFolder(pathNameSrc, pathNameDest);
    }
    return ret;
}



palStatus_t pal_fsSetMountPoint(pal_fsStorageID_t dataID, const char *path)
{
    palStatus_t ret = PAL_SUCCESS;
    if ((dataID >= PAL_FS_PARTITION_LAST) || (NULL == path))
    {
        ret = PAL_ERR_FS_INVALID_FILE_NAME;
    }
    else if (pal_plat_fsSizeCheck(path) >= PAL_MAX_FOLDER_DEPTH_CHAR)
    {
        ret = PAL_ERR_FS_FILENAME_LENGTH;
    }
    else
    {
            if (g_RootFolderIsSet[dataID])
            {
                ret = PAL_ERR_FS_ERROR;
            }
            else
            {
                if (NULL == g_RootFolder[dataID])
                {
                    g_RootFolder[dataID] = (char*)malloc(PAL_MAX_FOLDER_DEPTH_CHAR);
                    if (NULL == g_RootFolder[dataID])
                    {
                        return PAL_ERR_NO_MEMORY;
                    }
                    g_RootFolder[dataID][0] = NULLPTR;
                }
                strncat( g_RootFolder[dataID], path, PAL_MAX_FOLDER_DEPTH_CHAR - pal_plat_fsSizeCheck(g_RootFolder[dataID]));// same buffer is used for active backup root dirs using indexing
                g_RootFolderIsSet[dataID] = true;
            }
    }
    return ret;
}

palStatus_t pal_fsGetMountPoint(pal_fsStorageID_t dataID, size_t length, char *path)
{
    palStatus_t ret = PAL_SUCCESS;

    if (dataID >= PAL_FS_PARTITION_LAST)
    {
        return  PAL_ERR_INVALID_ARGUMENT;
    }
    if (length < PAL_MAX_FOLDER_DEPTH_CHAR)
    {
        return PAL_ERR_FS_LENGTH_ERROR;
    }

    if (path)
    {
        if (false == g_RootFolderIsSet[dataID])
        {
            strncpy(path, pal_plat_fsGetDefaultRootFolder(dataID), length);
        }
        else 
        {
            strncpy(path, g_RootFolder[dataID], length); // same buffer is used for active backup root dirs using indexing
        }
        
    }
    else
    {
        ret = PAL_ERR_FS_BUFFER_ERROR;
    }
    return ret;
}


palStatus_t pal_fsFormat(pal_fsStorageID_t dataID)
{
    palStatus_t ret = PAL_SUCCESS;
    int32_t opCode = (int32_t)dataID;
    if ((opCode < PAL_FS_PARTITION_PRIMARY) || (opCode >= PAL_FS_PARTITION_LAST))
    {
        ret = PAL_ERR_INVALID_ARGUMENT;
    }
    else
    {
        ret = pal_plat_fsFormat(dataID);
    }
    return ret;
}




bool pal_fsIsPrivatePartition(pal_fsStorageID_t dataID)
{
    bool isPrivate;
    if (PAL_FS_PARTITION_PRIMARY == dataID)
    {
        isPrivate = PAL_PRIMARY_PARTITION_PRIVATE;
    }
    else
    {
        isPrivate = PAL_SECONDARY_PARTITION_PRIVATE;
    }
    return isPrivate;
}
