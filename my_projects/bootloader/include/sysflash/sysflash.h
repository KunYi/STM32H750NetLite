#ifndef SYSFLASH_SYSFLASH_H
#define SYSFLASH_SYSFLASH_H

#define PRIMARY_ID      0
#define SECONDARY_ID    1
#define SCRATCH_ID      2

#define FLASH_AREA_IMAGE_PRIMARY(image_index)    (((image_index) == 0) ? PRIMARY_ID : PRIMARY_ID)
#define FLASH_AREA_IMAGE_SECONDARY(image_index)  (((image_index) == 0) ? SECONDARY_ID : SECONDARY_ID)
#define FLASH_AREA_IMAGE_SCRATCH                 SCRATCH_ID

#endif /* SYSFLASH_SYSFLASH_H */
