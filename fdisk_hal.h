#include <sys/types.h>
#include <ctype.h>
#include <stdint.h>

extern uint8_t sector_buffer[512];
extern unsigned char sdhc_card;

uint32_t sdcard_getsize(void);
void sdcard_open(void);
void sdcard_writesector(const uint32_t sector_number);
void sdcard_readsector(const uint32_t sector_number);
void flash_readsector(const uint32_t sector_number);
void sdcard_erase(const uint32_t first_sector, const uint32_t last_sector);
void mega65_fast(void);
void sdcard_map_sector_buffer(void);
void multisector_write_test(void);
void sdcard_readspeed_test(void);
void sdcard_select(unsigned char n);
unsigned char mega65_getkey(void);
unsigned char sdcard_reset(void);
