#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <strings.h>
#include <termios.h>

#include "fdisk_hal.h"

FILE *sdcard = NULL;
FILE *flash = NULL;

unsigned char sdcard_reset(void)
{
  return 0;
}

void sdcard_select(unsigned char n)
{
  return;
}

void sdcard_readsector(const uint32_t sector_number)
{
  fseek(sdcard, sector_number * 512LL, SEEK_SET);
  fread(sector_buffer, 512, 1, sdcard);
}

void sdcard_readspeed_test(void)
{
}

void mega65_fast(void)
{
}

void sdcard_map_sector_buffer(void)
{
}

uint32_t sdcard_getsize(void)
{
  struct stat s;

  if (!sdcard) {
    fprintf(stderr, "SD card not open.\n");
    exit(-1);
  }

  int r = fstat(fileno(sdcard), &s);

  if (r) {
    perror("stat");
    exit(-1);
  }

  fprintf(stderr, "Size = $%08X sectors.\n", (unsigned int)(16000000000LL / 512LL));
  //  return s.st_size/512;
  return 16000000000LL / 512;
}

void sdcard_open(void)
{
  if (!getenv("SDCARDFILE")) {
    fprintf(stderr, "ERROR: Environment variable 'SDCARDFILE' not found!\n");
    fprintf(stderr, "- Please set it to either:\n"
        "  - a file (e.g., 'sdcard.bin')\n"
        "  - or your sd-card device (e.g., '/dev/sdb')\n"
        "\n"
        "- Also consider setting 'FLASHFILE' env-var to point to a .cor file\n");

    exit(1);
  }

  sdcard = fopen(getenv("SDCARDFILE"), "r+");
  if (!sdcard) {
    fprintf(stderr, "Could not open '%s'...\n", getenv("SDCARDFILE"));
    perror("fopen");
    exit(-1);
  }
}

uint32_t write_count = 0;

void sdcard_writesector(const uint32_t sector_number)
{
  const uint8_t *buffer = sector_buffer;

  fseek(sdcard, sector_number * 512LL, SEEK_SET);
  fwrite(buffer, 512, 1, sdcard);

  write_count++;
}

void sdcard_erase(const uint32_t first_sector, const uint32_t last_sector)
{
  uint32_t n;
  bzero(sector_buffer, 512);

  fprintf(stderr, "Erasing sectors %d..%d\n", first_sector, last_sector);

  for (n = first_sector; n <= last_sector; n++)
    sdcard_writesector(n);
}

static int first_flash_read = 1;

void open_flash_file(void)
{
  if (!getenv("FLASHFILE")) {
    fprintf(stderr, "ERROR: Environment variable 'FLASHFILE' not found!\n");
    exit(1);
  }

  fprintf(stderr, "FLASHFILE=%s\n", getenv("FLASHFILE"));

  flash = fopen(getenv("FLASHFILE"),"rb+");
}

void flash_read512bytes(const uint32_t byte_offset)
{
  if (first_flash_read) {
    first_flash_read = 0;
    open_flash_file();
  }

  fseek(flash, byte_offset, SEEK_SET);
  fread(sector_buffer, 512, 1, flash);
}

unsigned char mega65_getkey(void)
{
  struct termios oldattr, newattr;
  int ch;
  tcgetattr(0, &oldattr);
  newattr=oldattr;
  newattr.c_lflag &= ~( ICANON | ECHO );
  tcsetattr( 0, TCSANOW, &newattr);
  ch=getchar();
  tcsetattr(0, TCSANOW, &oldattr);
  return(ch);
}
