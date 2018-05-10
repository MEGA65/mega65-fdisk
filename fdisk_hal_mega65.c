#include <stdio.h>
#include <stdlib.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "ascii.h"

#define POKE(X,Y) (*(unsigned char*)(X))=Y
#define PEEK(X) (*(unsigned char*)(X))

const long sd_sectorbuffer=0xffd6e00;
const uint16_t sd_ctl=0xd680L;
const uint16_t sd_addr=0xd681L;
const uint16_t sd_errorcode=0xd6daL;

unsigned char sdhc_card=0;

// Tell utilpacker what our display name is
const char *prop_m65u_name="PROP.M65U.NAME=SDCARD FDISK+FORMAT UTILITY";

void usleep(uint32_t micros)
{
  // Sleep for desired number of micro-seconds.
  // Each VIC-II raster line is ~64 microseconds
  // this is not totally accurate, but is a reasonable approach
  while(micros>64) {    
    uint8_t b=PEEK(0xD012);
    while(PEEK(0xD011)==b) continue;
    micros-=64;
  }
  return;
}

void sdcard_reset(void)
{
  // Reset and release reset
  //  write_line("Resetting SD card...",0);

  // Clear SDHC flag
  POKE(sd_ctl,0x40);
  
  POKE(sd_ctl,0);
  usleep(10000L);
  POKE(sd_ctl,1);

  // Now wait for SD card reset to complete
  while (PEEK(sd_ctl)&2) {
    continue;
  }
  

  if (sdhc_card)
    // Set SDHC flag (else writing doesnt work for some reason)
    POKE(sd_ctl,0x41);
    
}

void mega65_fast(void)
{
  POKE(0,65);
}

void show_card_size(uint32_t sector_number)
{
  // Work out size in MB and tell user
  char col=6;
  uint32_t megs=(sector_number+1L)/2048L;
  uint32_t gigs=0;
  if (megs&0xffff0000L) {
    gigs=megs/1024L;
    megs=gigs;
  }
  screen_decimal(screen_line_address,megs);
  if (megs<10000) col=5;
  if (megs<1000) col=4;
  if (megs<100) col=3;
  if (!gigs)
    write_line("MiB SD CARD FOUND.",col);
  else
    write_line("GiB SD CARD FOUND.",col);
}

uint32_t sdcard_getsize(void)
{
  // Work out the largest sector number we can read without an error
  
  uint32_t sector_number=0x00200000U;
  uint32_t step         =0x00200000U;
  
  char result;
  
  // Work out if it is SD or SDHC first of all
  // SD cards can't read at non-sector aligned addresses
  sdcard_reset();
  // Setup non-aligned address
  POKE(0xD681U,1); POKE(0xD682U,0); POKE(0xD683U,0); POKE(0xD684U,0);
  // Trigger read
  POKE(0xD680U,2);
  // Then sleep for plenty of time for the read to complete
  usleep(65535U);  usleep(65535U);  usleep(65535U);  usleep(65535U);
  		
  if (!PEEK(sd_ctl)) {
    write_line("SDHC card detected. Using sector addressing.",0);
    sdhc_card=1;
  } else {
    write_line("SDSC (<4GB) card detected. Using byte addressing.",0);
    sdcard_reset();
    sdhc_card=0;
  }

  if (sdhc_card) {
    // SDHC claims 32GB limit, and reading from beyond that might cause
    // trouble. However, 32bits x 512byte sectors = 16TiB addressable.
    // It thus seems that the top byte of the address may not be safe to use,
    // or at least the top few bits.
    sector_number=0x02000000U;
    step=sector_number;
    write_line("Determining size of SDHC card...",0);
  } else
    write_line("Determining size of SD card...",0);

  // Work out size of SD card in a safe way
  // (binary search of sector numbers is NOT safe for some reason.
  //  It frequently reports bigger than the size of the card)
  sector_number=0;
  step=16*2048; // = 16MiB
  while(sector_number<0x10000000U) {
    sdcard_readsector(sector_number);
    result=PEEK(sd_ctl)&0x63;
    if (result) {
      // Failed to read this, so reduce step size, and then resume.
      sector_number-=step;
      step=step>>2;
      if (!step) break;
    }
    sector_number+=step;

    // show card size as we figure it out,
    // and stay on the same line of output
    show_card_size(sector_number);
    screen_line_address-=80;
  }

  // Report number of sectors
  write_line("Maximum readable sector is $",0);
  screen_hex(screen_line_address-80+28,sector_number);
  screen_decimal(screen_line_address,sector_number/1024L);
  write_line("K Sector SD CARD.",6);  
  
  // Work out size in MB and tell user
  show_card_size(sector_number);
  
  return sector_number;
}

void sdcard_open(void)
{
  sdcard_reset();
}


uint32_t write_count=0;

void sdcard_map_sector_buffer(void)
{
  m65_io_enable();
  
  POKE(sd_ctl,0x81);
}

void sdcard_unmap_sector_buffer(void)
{
  m65_io_enable();
  
  POKE(sd_ctl,0x82);
}

void sdcard_readsector(const uint32_t sector_number)
{
  char tries=0;
  
  uint32_t sector_address=sector_number*512;
  if (sdhc_card) sector_address=sector_number;
  else {
    if (sector_number>=0x7fffff) {
      write_line("ERROR: Asking for sector @ >= 4GB on SDSC card.",0);
      while(1) continue;
    }
  }

  POKE(sd_addr+0,(sector_address>>0)&0xff);
  POKE(sd_addr+1,(sector_address>>8)&0xff);
  POKE(sd_addr+2,((uint32_t)sector_address>>16)&0xff);
  POKE(sd_addr+3,((uint32_t)sector_address>>24)&0xff);

  //  write_line("Reading sector @ $",0);
  //  screen_hex(screen_line_address-80+18,sector_address);
  
  while(tries<10) {

    // Wait for SD card to be ready
    while (PEEK(sd_ctl)&0x3)
      {
	if (PEEK(sd_ctl)&0x40)
	  {
	    return;
	  }
	// Sometimes we see this result, i.e., sdcard.vhdl thinks it is done,
	// but sdcardio.vhdl thinks not. This means a read error
	if (PEEK(sd_ctl)==0x01) return;
      }

    // Command read
    POKE(sd_ctl,2);
    
    // Wait for read to complete
    while (PEEK(sd_ctl)&0x3) {
      if (PEEK(sd_ctl)&0x40)
	{
	  return;
	}
      // Sometimes we see this result, i.e., sdcard.vhdl thinks it is done,
      // but sdcardio.vhdl thinks not. This means a read error
      if (PEEK(sd_ctl)==0x01) return;
    }

      // Note result
    // result=PEEK(sd_ctl);

    if (!(PEEK(sd_ctl)&0xe7)) {
      // Copy data from hardware sector buffer via DMA
      lcopy(sd_sectorbuffer,(long)sector_buffer,512);
  
      return;
    }
    
    POKE(0xd020,(PEEK(0xd020)+1)&0xf);

    // Reset SD card
    sdcard_open();
  }
  
}

void sdcard_writesector(const uint32_t sector_number)
{
  // Copy buffer into the SD card buffer, and then execute the write job
  uint32_t sector_address;
  int i;
  char tries=0,result;
  uint16_t counter=0;
  
  // Set address to read/write
  POKE(sd_ctl,1); // end reset
  if (!sdhc_card) sector_address=sector_number*512;
  else sector_address=sector_number;
  POKE(sd_addr+0,(sector_address>>0)&0xff);
  POKE(sd_addr+1,(sector_address>>8)&0xff);
  POKE(sd_addr+2,(sector_address>>16)&0xff);
  POKE(sd_addr+3,(sector_address>>24)&0xff);

  // Copy data to hardware sector buffer via DMA
  lcopy((long)sector_buffer,sd_sectorbuffer,512);
  
  while(tries<10) {

    // Wait for SD card to be ready
    counter=0;
    while (PEEK(sd_ctl)&3)
      {
	counter++;
	if (!counter) {
	  // SD card not becoming ready: try reset
	  POKE(sd_ctl,0); // begin reset
	  usleep(500000);
	  POKE(sd_ctl,1); // end reset
	  POKE(sd_ctl,3); // retry write

	}
	// Show we are doing something
	//	POKE(0x804f,1+(PEEK(0x804f)&0x7f));
      }
    
    // Command write
    POKE(sd_ctl,3);
    
    // Wait for write to complete
    counter=0;
    while (PEEK(sd_ctl)&3)
      {
	counter++;
	if (!counter) {
	  // SD card not becoming ready: try reset
	  POKE(sd_ctl,0); // begin reset
	  usleep(500000);
	  POKE(sd_ctl,1); // end reset
	  POKE(sd_ctl,3); // retry write

	}
	// Show we are doing something
	//	POKE(0x809f,1+(PEEK(0x809f)&0x7f));
      }

    write_count++;
    POKE(0xD020,write_count&0x0f);

    // Note result
    result=PEEK(sd_ctl);
    
    if (!(PEEK(sd_ctl)&0xe7)) {
      write_count++;
      
      POKE(0xD020,write_count&0x0f);

      // There is a bug in the SD controller: You have to read between writes, or it
      // gets really upset.

      // But sometimes even that doesn't work, and we have to reset it.

      // Does it just need some time between accesses?
      
      POKE(sd_ctl,2); // read the sector we just wrote
      while (PEEK(sd_ctl)&3) {
      	continue;
      }

      //      write_line("Wrote sector $$$$$$$$, result=$$",2);      
      //      screen_hex(screen_line_address-80+2+14,sector_number);
      //      screen_hex(screen_line_address-80+2+30,result);

      return;
    }

    POKE(0xd020,(PEEK(0xd020)+1)&0xf);

    // Wait a bit first for SD card to get happy
    for(i=0;i<32000;i++) continue;
  }

  write_line("Write error @ $$$$$$$$$",2);      
  screen_hex(screen_line_address-80+2+16,sector_number);
  {
    long i;
    for(i=0;i<100000;i++) continue;
  }
  
}

void sdcard_erase(const uint32_t first_sector,const uint32_t last_sector)
{
  uint32_t n;
  lfill((uint32_t)sector_buffer,0,512);

  //  fprintf(stderr,"ERASING SECTORS %d..%d\r\n",first_sector,last_sector);

  for(n=first_sector;n<=last_sector;n++) {
    sdcard_writesector(n);
    // Show count-down
    screen_decimal(screen_line_address,last_sector-n);
    //    fprintf(stderr,"."); fflush(stderr);
  }
  
}
