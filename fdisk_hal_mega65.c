#include <stdio.h>
#include <stdlib.h>

#include <memory.h>

#include "fdisk_hal.h"
#include "fdisk_screen.h"
#include "ascii.h"

const long sd_sectorbuffer = 0xffd6e00L;
const long qspi_sectorbuffer = 0xffd6a00L;
const uint16_t sd_ctl = 0xd680L;
const uint16_t sd_addr = 0xd681L;
const uint16_t sd_errorcode = 0xd6daL;

#ifdef SDLOG
// Instrumented build (make m65fdisk-sdlog.prg): every write to $D680 and
// every change of the value read from it goes into a log in attic RAM, with
// a microsecond time, so that what FDISK did to a card before it stopped
// answering can be read back over the serial monitor (sdlog_dump.py).
//
// $8000000: "SDLG", entries written (32 bits), entry size (16), ring size
// in entries; $8000010-: the entries, a ring of SDLOG_RING.  An entry:
//   0 type: 'W' $D680 written (1: the value), 'S' $D680 read changed (1:
//     the new value), 'I' the init record ($D6CE 16-23, in 4-11), 'B' the
//     block record ($D6CE 32-55, three entries: 1 = 0, 1, 2; 4-11 its bytes)
//   2 $D686  3 $D687  4-7 $D681-$D684 (W and S)
//   8-11 time, CIA 2 timers A and B chained (phi2, about 1 MHz) (W and S)
//   12-15 the reads of $D680 since the previous entry
// After an init (the first idle status after a $01) the init record is
// logged; when the error bit (6) sets, the block record.  The card is
// called wedged when an init ends in the SPI fallback (record flags bit 0)
// or with $D687 = $03: the log stops there and the screen says so.
#include <memory.h>
#define SDLOG_BASE 0x8000000L
#define SDLOG_RING 0x40000UL
static uint32_t sdlog_n = 0;
static uint32_t sdlog_polls = 0;
static unsigned char sdlog_last = 0xff;
static unsigned char sdlog_on = 0;
static unsigned char sdlog_initing = 0;
static unsigned char sdlog_reset = 0;
static unsigned char sdlog_stopped = 0;
static unsigned char sdlog_e[16];
static unsigned char sdlog_rec[24];
static const unsigned char sdlog_head[16] = {
  'S', 'D', 'L', 'G', 0, 0, 0, 0, 16, 0, 0, 0,
  (SDLOG_RING >> 0) & 0xff, (SDLOG_RING >> 8) & 0xff, (SDLOG_RING >> 16) & 0xff, 0
};

static void sdlog_start(void)
{
  // CIA 2: timer A free-running from phi2, timer B counting its underflows
  POKE(0xdd0d, 0x7f);
  POKE(0xdd0e, 0x00);
  POKE(0xdd0f, 0x00);
  POKE(0xdd04, 0xff);
  POKE(0xdd05, 0xff);
  POKE(0xdd06, 0xff);
  POKE(0xdd07, 0xff);
  POKE(0xdd0f, 0x51);
  POKE(0xdd0e, 0x11);
  lcopy((long)sdlog_head, SDLOG_BASE, 16);
  sdlog_on = 1;
}

static void sdlog_put(void)
{
  lcopy((long)sdlog_e, SDLOG_BASE + 16 + (long)((sdlog_n & (SDLOG_RING - 1)) << 4), 16);
  sdlog_n++;
  lcopy((long)&sdlog_n, SDLOG_BASE + 4, 4);
  sdlog_polls = 0;
}

static void sdlog_event(unsigned char type, unsigned char value)
{
  if (!sdlog_on)
    sdlog_start();
  if (sdlog_stopped)
    return;
  sdlog_e[0] = type;
  sdlog_e[1] = value;
  sdlog_e[2] = PEEK(0xd686);
  sdlog_e[3] = PEEK(0xd687);
  sdlog_e[4] = PEEK(0xd681);
  sdlog_e[5] = PEEK(0xd682);
  sdlog_e[6] = PEEK(0xd683);
  sdlog_e[7] = PEEK(0xd684);
  // (B high first: a wrap of A between the reads is off by 65 ms, rarely)
  sdlog_e[11] = ~PEEK(0xdd07);
  sdlog_e[10] = ~PEEK(0xdd06);
  sdlog_e[9] = ~PEEK(0xdd05);
  sdlog_e[8] = ~PEEK(0xdd04);
  *(uint32_t *)&sdlog_e[12] = sdlog_polls;
  sdlog_put();
}

static void sdlog_record(unsigned char type, unsigned char first, unsigned char count)
{
  unsigned char i;
  POKE(0xd6ce, 0x80);
  for (i = 0; i < count; i++) {
    POKE(0xd6ce, first + i);
    sdlog_rec[i] = PEEK(0xd6cf);
  }
  for (i = 0; i < count; i += 8) {
    sdlog_e[0] = type;
    sdlog_e[1] = i >> 3;
    sdlog_e[2] = PEEK(0xd686);
    sdlog_e[3] = PEEK(0xd687);
    lcopy((long)&sdlog_rec[i], (long)&sdlog_e[4], 8);
    *(uint32_t *)&sdlog_e[12] = sdlog_polls;
    sdlog_put();
  }
}

static unsigned char sdlog_native(void)
{
  return PEEK(0xd6cc) == 0x53 && PEEK(0xd6cd) == 0x01;
}

void sdlog_wedged(void)
{
  sdlog_stopped = 1;
  write_line("SDLOG: WEDGED, LOG STOPPED. ENTRIES:", 2);
  screen_hex(screen_line_address - 80 + 36, sdlog_n);
  mega65_getkey();
}

unsigned char sd_peek(void)
{
  unsigned char v = PEEK(0xd680);
  sdlog_polls++;
  if (v != sdlog_last && !sdlog_stopped) {
    sdlog_event('S', v);
    if ((v & 0x40) && !(sdlog_last & 0x40) && sdlog_native())
      sdlog_record('B', 32, 24);
    sdlog_last = v;
    if ((v & 0x40) && PEEK(0xd687) == 0x03 && sdlog_native())
      sdlog_wedged();
    else if (sdlog_initing && !(v & 0x03)) {
      sdlog_initing = 0;
      if (sdlog_native()) {
        sdlog_record('I', 16, 8);
        if ((sdlog_rec[2] & 0x01) || PEEK(0xd687) == 0x03)
          sdlog_wedged();
      }
    }
  }
  return v;
}

void sd_poke(unsigned char v)
{
  POKE(0xd680, v);
  sdlog_event('W', v);
  // (an init: $01 ends a reset; FDISK writes $01 alone before each write)
  if (v == 0x00 || v == 0x10)
    sdlog_reset = 1;
  else if ((v == 0x01 || v == 0x11) && sdlog_reset) {
    sdlog_reset = 0;
    sdlog_initing = 1;
  }
}

#define SD_PEEK() sd_peek()
#define SD_POKE(V) sd_poke(V)
#else
#define SD_PEEK() PEEK(sd_ctl)
#define SD_POKE(V) POKE(sd_ctl, V)
#endif

// Tell utilpacker what our display name is
const char *prop_m65u_name = "PROP.M65U.NAME=SDCARD FDISK+FORMAT UTILITY";

unsigned char key = 0;
uint16_t i;

unsigned char get_random_byte(void)
{
  while (PEEK(0xD7FE) & 0x80) continue;
  return PEEK(0xD7EF);
}

unsigned char mega65_getkey(void)
{
  while (!PEEK(0xD610))
    continue;
  key = PEEK(0xD610);
  POKE(0xD610, 0);
  return key;
}

void sdcard_select(unsigned char n)
{
  SD_POKE(0xc0 + (n & 1));
}

void usleep(uint32_t micros)
{
  // Sleep for desired number of micro-seconds.
  // Each VIC-II raster line is ~64 microseconds
  // this is not totally accurate, but is a reasonable approach
  while (micros > 64) {
    uint8_t b = PEEK(0xD012);
    while (PEEK(0xD012) == b)
      continue;
    micros -= 64;
  }
  return;
}

long reset_timeout;

unsigned char sdcard_reset(void)
{
  // Reset and release reset
  //  write_line("Resetting SD card...",0);

  // Clear SDHC flag
  SD_POKE(0x40);

  SD_POKE(0);
  SD_POKE(1);

  reset_timeout = 100000L;

  // Now wait for SD card reset to complete
  while (SD_PEEK() & 3) {
    POKE(0xd020, (PEEK(0xd020) + 1) & 15);
    reset_timeout--;
    if (!reset_timeout)
      return 0xff;
  }

  // Set SDHC flag, since we don't support SDC cards any more
  SD_POKE(0x41);

  return 0;
}

void mega65_fast(void)
{
  POKE(0, 65);
}

void show_card_size(uint32_t sector_number)
{
  // Work out size in MB and tell user
  char col = 8;
  uint32_t megs = (sector_number + 1L) / 2048L;
  uint32_t gigs = 0;
  if (megs & 0xffff0000UL) {
    gigs = megs / 1024L;
    megs = gigs;
  }
  screen_decimal(screen_line_address + 2, megs);
  if (megs < 10000)
    col = 7;
  if (megs < 1000)
    col = 6;
  if (megs < 100)
    col = 5;
  if (!gigs)
    write_line("MiB SD CARD FOUND.", col);
  else
    write_line("GiB SD CARD FOUND.", col);
}

uint32_t sdcard_getsize(void)
{
  // Work out the largest sector number we can read without an error

  uint32_t sector_number = 0x00200000U;
  uint32_t step = 0x00200000U;

  char result;

  // Work out if it is SD or SDHC first of all
  // SD cards can't read at non-sector aligned addresses
  if (sdcard_reset())
    return 0;

  // SDHC claims 32GB limit, and reading from beyond that might cause
  // trouble. However, 32bits x 512byte sectors = 16TiB addressable.
  // It thus seems that the top byte of the address may not be safe to use,
  // or at least the top few bits.
  sector_number = 0x02000000U;
  step = sector_number;

  // Work out size of SD card in a safe way
  // (binary search of sector numbers is NOT safe for some reason.
  //  It frequently reports bigger than the size of the card)
  sector_number = 0;
  step = 256UL * 2048UL; // = 256MiB
  while (sector_number < 0x10000000U) {
    //    write_line("Trying to read sector $",0);
    //    screen_hex(screen_line_address-80+24,sector_number);
    sdcard_readsector(sector_number);
    result = SD_PEEK() & 0x63;
    if (result) {
      //      write_line("Read failed",2);
      // Failed to read this, so reduce step size, and then resume.

      // Reset card ready for next try
      sdcard_reset();

      sector_number -= step;
      step = step >> 2;
      if (!step)
        break;
    } // else write_line("Read succeeded",2);
    sector_number += step;
    //    mega65_getkey();

    // show card size as we figure it out,
    // and stay on the same line of output
    show_card_size(sector_number);
    POKE(0xD020U, PEEK(0xD020U) + 1);
    screen_line_address -= 80;
  }

  // Report number of sectors
  write_line("Maximum readable sector is $", 2);
  screen_hex(screen_line_address - 80 + 30, sector_number);
  //  screen_decimal(screen_line_address,sector_number/1024L);
  //  write_line("K Sector SD CARD.",6);

  // Work out size in MB and tell user
  show_card_size(sector_number);

  return sector_number;
}

void sdcard_open(void)
{
  sdcard_reset();
}

uint32_t write_count = 0;

void sdcard_map_sector_buffer(void)
{
  SD_POKE(0x81);
}

void sdcard_unmap_sector_buffer(void)
{
  SD_POKE(0x82);
}

unsigned short timeout;

void do_read_sector(unsigned char cmd, uint32_t sector_number)
{
  char tries = 0;
  uint32_t sector_address = sector_number;

  POKE(sd_addr + 0, (sector_address >> 0) & 0xff);
  POKE(sd_addr + 1, (sector_address >> 8) & 0xff);
  POKE(sd_addr + 2, ((uint32_t)sector_address >> 16) & 0xff);
  POKE(sd_addr + 3, ((uint32_t)sector_address >> 24) & 0xff);

  //  write_line("Reading sector @ $",0);
  //  screen_hex(screen_line_address-80+18,sector_address);

  while (tries < 10) {

    // Wait for SD card to be ready
    timeout = 50000U;
    while (SD_PEEK() & 0x3) {
      timeout--;
      if (!timeout)
        return;
      if (SD_PEEK() & 0x40) {
        return;
      }
      // Sometimes we see this result, i.e., sdcard.vhdl thinks it is done,
      // but sdcardio.vhdl thinks not. This means a read error
      if (SD_PEEK() == 0x01)
        return;
    }

    // Command read
    SD_POKE(cmd);

    // Wait for read to complete
    timeout = 50000U;
    while (SD_PEEK() & 0x3) {
      timeout--;
      if (!timeout)
        return;
      //      write_line("Waiting for read to complete",0);
      if (SD_PEEK() & 0x40) {
        return;
      }
      // Sometimes we see this result, i.e., sdcard.vhdl thinks it is done,
      // but sdcardio.vhdl thinks not. This means a read error
      if (SD_PEEK() == 0x01)
        return;
    }

    // Note result
    // result=SD_PEEK();

    if (!(SD_PEEK() & 0x67)) {
      // Copy data from hardware sector buffer via DMA
      lcopy(sd_sectorbuffer, (long)sector_buffer, 512);

      return;
    }

    POKE(0xd020, (PEEK(0xd020) + 1) & 0xf);

    // Reset SD card
    sdcard_open();

    tries++;
  }
}

void sdcard_readsector(const uint32_t sector_number)
{

  do_read_sector(0x02, sector_number);
}

void flash_read512bytes(const uint32_t byte_offset)
{
  uint32_t sector_address = byte_offset;

  POKE(sd_addr + 0, (sector_address >> 0) & 0xff);
  POKE(sd_addr + 1, (sector_address >> 8) & 0xff);
  POKE(sd_addr + 2, ((uint32_t)sector_address >> 16) & 0xff);
  POKE(sd_addr + 3, ((uint32_t)sector_address >> 24) & 0xff);

  while (SD_PEEK() & 0x01)
    continue;
  SD_POKE(0x61);

  while (SD_PEEK() & 0x01)
    continue;

  lcopy(qspi_sectorbuffer, (long)sector_buffer, 512);
}

uint8_t verify_buffer[512];

// Wait for the SD controller to be ready, for several seconds: a slow card
// can be busy for over 0.2 s after a write, and a reset before it is done
// can leave it busy for seconds more at its next init.  1 if still busy.
unsigned char sd_wait_ready(void)
{
  uint32_t n = 0x400000UL;
  while (SD_PEEK() & 3) {
    if (!--n)
      return 1;
  }
  return 0;
}

// Reset the card, then wait for its init to finish before using it.  A
// native SD controller ($D6CC/$D6CD = $53 $01) reports a card that is in
// but answers nothing with $D687 = $03: only re-inserting it helps
void sd_reset_and_wait(void)
{
  while (1) {
    SD_POKE(0); // begin reset
    usleep(500000);
    SD_POKE(1); // end reset
    sd_wait_ready();
    if (PEEK(0xd6cc) != 0x53 || PEEK(0xd6cd) != 0x01 || PEEK(0xd687) != 0x03)
      return;
    write_line("SD CARD STOPPED RESPONDING. REINSERT IT, THEN PRESS A KEY.", 2);
    mega65_getkey();
  }
}

void sdcard_writesector(const uint32_t sector_number)
{
  // Copy buffer into the SD card buffer, and then execute the write job
  uint32_t sector_address;
  int i;
  char tries = 0, result;

  while (SD_PEEK() & 3) {
    continue;
  }

  // Set address to read/write
  SD_POKE(1); // end reset
  sector_address = sector_number;
  POKE(sd_addr + 0, (sector_address >> 0) & 0xff);
  POKE(sd_addr + 1, (sector_address >> 8) & 0xff);
  POKE(sd_addr + 2, (sector_address >> 16) & 0xff);
  POKE(sd_addr + 3, (sector_address >> 24) & 0xff);

  // Read the sector and see if it already has the correct contents.
  // If so, nothing to write

  SD_POKE(2); // read the sector we just wrote

  while (SD_PEEK() & 3) {
    continue;
  }

  // Copy the read data to a buffer for verification
  lcopy(sd_sectorbuffer, (long)verify_buffer, 512);

  // Verify that it matches the data we wrote
  for (i = 0; i < 512; i++) {
    if (sector_buffer[i] != verify_buffer[i])
      break;
  }
  if (i == 512) {
    return;
  }

  while (tries < 10) {

    // Copy data to hardware sector buffer via DMA
    lcopy((long)sector_buffer, sd_sectorbuffer, 512);

    // Wait for SD card to be ready
    if (sd_wait_ready())
      sd_reset_and_wait();

    // Command write
    if (sector_number)
      SD_POKE(0x57); // open SD card write gate
    else
      SD_POKE(0x4D); // open SD card write gate for MBR
    SD_POKE(3);

    while (!(SD_PEEK() & 3))
      continue;

    // Wait for write to complete
    while (sd_wait_ready()) {
      // SD card not becoming ready: try reset
      sd_reset_and_wait();
      if (sector_number)
        SD_POKE(0x57); // open SD card write gate
      else
        SD_POKE(0x4D); // open SD card write gate for MBR
      SD_POKE(3);      // retry write
    }

    write_count++;
    POKE(0xD020, write_count & 0x0f);

    // Note result
    result = SD_PEEK();

    if (!(SD_PEEK() & 0x67)) {
      write_count++;

      POKE(0xD020, write_count & 0x0f);

      // There is a bug in the SD controller: You have to read between writes, or it
      // gets really upset.

      // But sometimes even that doesn't work, and we have to reset it.

      // Does it just need some time between accesses?

      while (SD_PEEK() & 3) {
        continue;
      }

      SD_POKE(2); // read the sector we just wrote

      while (!(SD_PEEK() & 3)) {
        continue;
      }

      while (SD_PEEK() & 3) {
        continue;
      }

      // Copy the read data to a buffer for verification
      lcopy(sd_sectorbuffer, (long)verify_buffer, 512);

      // VErify that it matches the data we wrote
      for (i = 0; i < 512; i++) {
        if (sector_buffer[i] != verify_buffer[i])
          break;
      }
      if (i != 512) {
        // VErify error has occurred
        write_line("Verify error for sector $$$$$$$$", 0);
        screen_hex(screen_line_address - 80 + 24, sector_number);
      }
      else {
        //      write_line("Wrote sector $$$$$$$$, result=$$",2);
        //      screen_hex(screen_line_address-80+2+14,sector_number);
        //      screen_hex(screen_line_address-80+2+30,result);

        return;
      }
    }

    POKE(0xd020, (PEEK(0xd020) + 1) & 0xf);
  }

  write_line("Write error @ $$$$$$$$$", 2);
  screen_hex(screen_line_address - 80 + 2 + 16, sector_number);
}

#ifdef SDLOG
// (no room for it in the instrumented build)
void sdcard_readspeed_test(void)
{
}
#else
void sdcard_readspeed_test(void)
{
  uint32_t n;
  uint32_t total_time = 0;
  uint8_t last_raster = 0;
  uint16_t speed;

  n = 0;
  for (i = 0; i < 1000; i++) {
    POKE(sd_addr + 0, (n >> 0) & 0xff);
    POKE(sd_addr + 1, (n >> 8) & 0xff);
    POKE(sd_addr + 2, (n >> 16) & 0xff);
    POKE(sd_addr + 3, (n >> 24) & 0xff);
    n += 9873;
    n &= 0xfffff;

    while (SD_PEEK() & 3)
      continue;
    SD_POKE(0x02);
    while (!(SD_PEEK() & 3))
      continue;
    while (SD_PEEK() & 3) {
      if (PEEK(0xD012U) != last_raster) {
        total_time++;
        last_raster = PEEK(0xD012U);
      }
    }

    POKE(0xD020U, PEEK(0xD020U) + 1);
  }

  // Bus interface makes for an upper limit of about 3MB/sec
  // Divide total time by 1000 to get # rasters per sector.
  // Then convert rasters to miliseconds.
  // 60Hz 800x600 uses ~26 usec per raster.
  // But our rasters are 2 physical rasters, so ~52 usec per line
  // Round it to 50 usec for ease of calculation.
  // A count of 1000 = 50 usec per sector = 10MB/sec
  // A count of 20000 = 1 msec per sector = 512KB/sec
  // If it takes 1 raster on average, then the speed is (1sec/50usec) sectors/sec
  // = 20000 sectors / second = 10MB /sec.
  // Thus we can call the speed 10000*1000 / rasters*1000
  // = 10000000 / total_time

  speed = 10000000L / total_time;

  write_line("SD Card read speed =       KB/sec", 2);
  screen_decimal(screen_line_address - 80 + 23, speed);
}
#endif

#if 0
void multisector_write_test(void)
{
  uint32_t n;

  // Write 17 sectors 
  uint32_t first_sector=2;
  uint32_t last_sector=2+64;
  uint32_t verify_errors=0;
  
  // Read sectors and see what is there already
  verify_errors=0;
  for(n=first_sector;n<=last_sector;n++) {
    POKE(0xD020U,1);
    sdcard_readsector(n);
    POKE(0xD020U,0);
    for(i=0;i<512;i++)
      if (sector_buffer[i]) {
	verify_errors++;
	break;
      }
  }

  // Set address of first sector
  POKE(sd_addr+0,(first_sector>>0)&0xff);
  POKE(sd_addr+1,(first_sector>>8)&0xff);
  POKE(sd_addr+2,(first_sector>>16)&0xff);
  POKE(sd_addr+3,(first_sector>>24)&0xff);

  // First, erase all sectors to all zeroes
  lfill((uint32_t)sector_buffer,0,512);
  lcopy((long)sector_buffer,sd_sectorbuffer,512);

  for(n=first_sector;n<=last_sector;n++) {

    // Wait for SD card to go ready
    while (SD_PEEK()&3) continue;
    
    if (sector_number)
      SD_POKE(0x57); // open SD card write gate
    else
      SD_POKE(0x4D); // open SD card write gate for MBR
    if (n==first_sector) {
      // First sector of multi-sector write
      SD_POKE(0x04);
    } else {
      // Subsequent sector of multi-sector write
      SD_POKE(0x05);
    }

    // while (!(SD_PEEK()&3)) continue;
    POKE(0xD020U,1);
    
    while (SD_PEEK()&3) continue;
    POKE(0xD020U,0);
  }

  // End multi-sector write
  if (sector_number)
    SD_POKE(0x57); // open SD card write gate
  else
    SD_POKE(0x4D); // open SD card write gate for MBR
  SD_POKE(0x06);

  // Wait for SD card to go busy
  while (!(SD_PEEK()&3)) continue;
  
  // Wait for SD card to go ready
  while (SD_PEEK()&3) continue;
  
  // Try to flush cache?
  //  SD_POKE(0x0c);
  
  // Read sectors and see what is there already
  verify_errors=0;
  for(n=first_sector;n<=last_sector;n++) {
    POKE(0xD020U,1);
    sdcard_readsector(n);
    POKE(0xD020U,0);
    for(i=0;i<512;i++)
      if (sector_buffer[i]) {
	verify_errors++;
	break;
      }
    if (i==512) POKE(SCREEN_ADDRESS+80+n-first_sector,0);
  }

  // Set address of first sector
  POKE(sd_addr+0,(first_sector>>0)&0xff);
  POKE(sd_addr+1,(first_sector>>8)&0xff);
  POKE(sd_addr+2,(first_sector>>16)&0xff);
  POKE(sd_addr+3,(first_sector>>24)&0xff);
  
  // Now re-write sectors with sector number marker
  lfill((uint32_t)sector_buffer,0x55,512);
  lcopy((long)sector_buffer,sd_sectorbuffer,512);
  for(n=first_sector;n<=last_sector;n++) {

    // Wait for SD card to go ready
    while (SD_PEEK()&3) continue;

    // Record sector number in start of each sector
    sector_buffer[0]=n>>0;
    sector_buffer[1]=n>>8;
    sector_buffer[2]=n>>16;
    sector_buffer[3]=n>>24;
    lcopy((long)sector_buffer,sd_sectorbuffer,512);
    POKE(SCREEN_ADDRESS+10*80+n-first_sector,lpeek(sd_sectorbuffer));
    
    if (sector_number)
      SD_POKE(0x57); // open SD card write gate
    else
      SD_POKE(0x4D); // open SD card write gate for MBR
    if (n==first_sector) {
      // First sector of multi-sector write
      SD_POKE(0x04);
    } else {
      // Subsequent sector of multi-sector write
      SD_POKE(0x05);
    }

    // while (!(SD_PEEK()&3)) continue;
    POKE(0xD020U,1);
    
    while (SD_PEEK()&3) continue;
    POKE(0xD020U,0);
  }

  // End multi-sector write
  if (sector_number)
    SD_POKE(0x57); // open SD card write gate
  else
    SD_POKE(0x4D); // open SD card write gate for MBR
  SD_POKE(0x06);

  // Wait for SD card to go busy
  while (!(SD_PEEK()&3)) continue;
  
  // Wait for SD card to go ready
  while (SD_PEEK()&3) continue;
  
  // Read sectors and see what is there already, checking for the markers we wrote
  if (!verify_errors) {
    for(n=first_sector;n<=last_sector;n++) {
      sdcard_readsector(n);
      if ((sector_buffer[0]!=((n>>0)&0xff))
	  ||(sector_buffer[1]!=((n>>8)&0xff))
	  ||(sector_buffer[2]!=((n>>16)&0xff))
	  ||(sector_buffer[3]!=((n>>24)&0xff)))
	{
	  POKE(SCREEN_ADDRESS+3*80+n-first_sector,0x2e);
	  POKE(SCREEN_ADDRESS+4*80+n-first_sector,i&0xff);
	  verify_errors++;
	}
      else {
	POKE(SCREEN_ADDRESS+3*80+n-first_sector,2);
      }
    }    
  } else POKE(0xD021U,0);

  write_line("##### Errors during bulk-write",0);
  screen_decimal(screen_line_address-80,verify_errors);
  
  while(1) {
    POKE(0xD020U,PEEK(0xD020U)+1);
  }
  
}
#endif

void sdcard_erase(const uint32_t first_sector, const uint32_t last_sector)
{
  uint32_t n;
  lfill((uint32_t)sector_buffer, 0, 512);
  lcopy((long)sector_buffer, sd_sectorbuffer, 512);

  //  fprintf(stderr,"ERASING SECTORS %d..%d\r\n",first_sector,last_sector);

#ifndef NOFAST_ERASE
  POKE(sd_addr + 0, (first_sector >> 0) & 0xff);
  POKE(sd_addr + 1, (first_sector >> 8) & 0xff);
  POKE(sd_addr + 2, (first_sector >> 16) & 0xff);
  POKE(sd_addr + 3, (first_sector >> 24) & 0xff);
#endif

  for (n = first_sector; n <= last_sector; n++) {

#ifndef NOFAST_ERASE
    // Wait for SD card to go ready
    while (SD_PEEK() & 3)
      continue;

    if (n)
      SD_POKE(0x57); // open SD card write gate
    else
      SD_POKE(0x4D); // open SD card write gate for MBR
    if (n == first_sector) {
      // First sector of multi-sector write
      SD_POKE(0x04);
    }
    else if (n == last_sector) {
      // Last sector of multi-sector write
      SD_POKE(0x06);
    }
    else
      // All other sectors
      SD_POKE(0x05);

    // Wait for SD card to go busy
    while (!(SD_PEEK() & 3))
      continue;

    // Wait for SD card to go ready
    while (SD_PEEK() & 3)
      continue;

#else
    sdcard_writesector(n);
#endif

    // Show count-down
    screen_decimal(screen_line_address + 1, last_sector - n);
    //    fprintf(stderr,"."); fflush(stderr);
  }
}
