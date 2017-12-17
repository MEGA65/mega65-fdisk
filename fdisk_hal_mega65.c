#include <stdio.h>
#include <stdlib.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "ascii.h"

#define POKE(X,Y) (*(unsigned char*)(X))=Y
#define PEEK(X) (*(unsigned char*)(X))

long sd_sectorbuffer=0xffd6000;
uint16_t sd_ctl=0xd680L;
uint16_t sd_addr=0xd681L;

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

void mega65_fast(void)
{
  POKE(0,65);
}

uint32_t sdcard_getsize(void)
{
  // Work out the largest sector number we can read without an error
  
  uint32_t sector_address;
  uint32_t sector_number=0x00200000U;
  uint32_t step         =0x00200000U;

  char result;
  
  // Set address to read/write
  while (step) {
    // Try to read sector number with bit set

    // Work out address of sector
    // XXX - Assumes SD, not SDHC card
    sdcard_readsector(sector_number);
    
    // Note result
    result=PEEK(sd_ctl);

    // If we have a read error, then remove this bit from the mask
    if (result&0x60) {
      // Now mask out bit in sector number, and try again
      //      write_line("Error reading sector $",0);
      //      screen_hex(screen_line_address-79+21,sector_number);      
      sector_number-=step;
    } else {
      //      write_line("OK reading sector $",0);
      //      screen_hex(screen_line_address-79+18,sector_number);      
    }
    // Advance half step
    step=step>>1;
    sector_number+=step;
    
  }

  // Report number of sectors
  //  screen_decimal(screen_line_address,sector_number/1024);
  //  write_line("K Sector SD CARD.",8);  
  
  // Work out size in MB and tell user
  {
    char col=6;
    int megs=(sector_number+1)/2048;
    screen_decimal(screen_line_address,megs);
    if (megs<10000) col=5;
    if (megs<1000) col=4;
    if (megs<100) col=3;
    write_line("MiB SD CARD FOUND.",col);
  }
  
  return sector_number;
}

void sdcard_open(void)
{
  // On real MEGA65, there is nothing to do here.
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
  int i;
  
  uint32_t sector_address=sector_number*512;    
  POKE(sd_addr+0,(sector_address>>0)&0xff);
  POKE(sd_addr+1,(sector_address>>8)&0xff);
  POKE(sd_addr+2,((uint32_t)sector_address>>16)&0xff);
  POKE(sd_addr+3,((uint32_t)sector_address>>24)&0xff);

  while(tries<10) {
  
    // Wait for SD card to be ready
    while (PEEK(sd_ctl)&3)
      continue;
    
    // Command read
    POKE(sd_ctl,2);
    
    // Wait for read to complete
    while (PEEK(sd_ctl)&3)
      continue;
    
      // Note result
    // result=PEEK(sd_ctl);

    if (!(PEEK(sd_ctl)&0xe7)) {
      // Copy data from hardware sector buffer via DMA
      lcopy(sd_sectorbuffer,(long)sector_buffer,512);
  
      return;
    }

    POKE(0xd020,(PEEK(0xd020)+1)&0xf);
    // Wait a bit first for SD card to get happy
    for(i=0;i<1000000;i++) continue;
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
  sector_address=sector_number*512;
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
	POKE(0x804f,1+(PEEK(0x804f)&0x7f));
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
	POKE(0x809f,1+(PEEK(0x809f)&0x7f));
      }

    write_count++;
    POKE(0xD020,write_count&0xff);
    
    // Note result
    result=PEEK(sd_ctl);

    if (!(PEEK(sd_ctl)&0xe7)) {
      write_count++;
      
      POKE(0xD020,write_count&0xff);

      // There is a bug in the SD controller: You have to read between writes, or it
      // gets really upset.
      POKE(sd_ctl,2); // read the sector we just wrote
      while (PEEK(sd_ctl)&3) continue;
            
      write_line("Wrote sector $$$$$$$$, result=$$",2);      
      screen_hex(screen_line_address-80+2+14,sector_number);
      screen_hex(screen_line_address-80+2+24,result);

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
  lfill(sd_sectorbuffer,0,512);

  //  fprintf(stderr,"ERASING SECTORS %d..%d\r\n",first_sector,last_sector);

  for(n=first_sector;n<=last_sector;n++) {
    sdcard_writesector(n);
    //    fprintf(stderr,"."); fflush(stderr);
  }
  
}
