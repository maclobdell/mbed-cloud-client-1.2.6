#ifndef PAL_PLAT_FLASH_H_
#define PAL_PLAT_FLASH_H_

#include "pal_internalFlash.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief This function initialized the flash API module,
 * 			And should be called prior flash APIs calls
 *
 * \return   PAL_SUCCESS upon successful operation. \n
 *           PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
 *
 * \note should be called only once unless \c pal_InternalFlashDeinit function is called
 * \note This function is Blocking till completion!!
 *
 */
palStatus_t pal_plat_internalFlashInit(void);

/*! \brief This function destroy the flash module
 *
 * \return PAL_SUCCESS upon successful operation. \n
 *         PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
 *
 * \note Should be called only after \c pal_InternalFlashinit() is called.
 * \note Flash APIs will not work after calling this function
 * \note This function is Blocking till completion!!
 *
 */
palStatus_t pal_plat_internalFlashDeInit(void);

/*! \brief This function writes to the internal flash
*
* @param[in]	buffer - pointer to the buffer to be written
* @param[in]	size - the size of the buffer in bytes, must be aligned to minimum writing unit (page size).
* @param[in]	address - the address of the internal flash.
*
* \return PAL_SUCCESS upon successful operation. \n
*         PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
*
* \note This function is Blocking till completion!!
* \note This function is Thread Safe!!
*/
palStatus_t pal_plat_internalFlashWrite(const size_t size, const uint32_t address, const uint32_t * buffer);

/*! \brief This function copies the memory data into the user given buffer
*
* @param[in]	size - the size of the buffer in bytes.
* @param[in]	address - the address of the internal flash.
* @param[out]	buffer - pointer to the buffer to write to
*
* \return PAL_SUCCESS upon successful operation. \n
*         PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
* \note This function is Blocking till completion!!
* \note This function is Thread Safe!!
*
*/
palStatus_t pal_plat_internalFlashRead(const size_t size, const uint32_t address, uint32_t * buffer);

/*! \brief This function Erase the sector
*
* @param[in]	size - the size to be erased, must be align to sector.
* @param[in]	address - sector start address to be erased, must be align to sector.
*
* \return PAL_SUCCESS upon successful operation. \n
*         PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
*
* \note ALL sectors can be erased!! No protection to bootloader, program or other...
* \note This function is Blocking till completion!!
* \note Only one sector can be erased in each function call
* \note This function is Thread Safe!!
*/
palStatus_t pal_plat_internalFlashErase(uint32_t address, size_t size);

/*! \brief This function returns the minimum writing unit to the flash
*
* \return size_t the 2, 4, 8....
*/
size_t pal_plat_internalFlashGetPageSize(void);


/*! \brief This function returns the sector size
 *
* @param[in]	address - the starting address of the sector is question
*
* \return size of sector, 0 if error
*/
size_t pal_plat_internalFlashGetSectorSize(uint32_t address);



///////////////////////////////////////////////////////////////
////-------------------SOTP functions------------------------//
///////////////////////////////////////////////////////////////
/*! \brief This function return the SOTP section data
*
* @param[in]	section - the section number (0 or 1)
* @param[out]	data - the information about the section
*
* \return PAL_SUCCESS upon successful operation. \n
*         PAL_ERR_INTERNAL_FLASH_ERROR - see error code \c palError_t.
*
*/
palStatus_t pal_plat_internalFlashGetAreaInfo(bool section, palSotpAreaData_t *data);


#ifdef __cplusplus
}
#endif

#endif /* PAL_PLAT_FLASH_H_ */
