#include <stdio.h>
#include <stdlib.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "ascii.h"  

#define POKE(X,Y) (*(unsigned char*)(X))=Y
#define PEEK(X) (*(unsigned char*)(X))

const long sd_sectorbuffer=0xffd6e00L;
const uint16_t sd_ctl=0xd680L;
const uint16_t sd_addr=0xd681L;
const uint16_t sd_errorcode=0xd6daL;

// Tell utilpacker what our display name is
const char *prop_m65u_name="PROP.M65U.NAME=SDCARD FDISK+FORMAT UTILITY";

unsigned char key=0;
unsigned char mega65_getkey(void)
{
  while(!PEEK(0xD610)) continue;
  key=PEEK(0xD610);
  POKE(0xD610,0);
  return key;
}

void sdcard_select(unsigned char n)
{
  POKE(sd_ctl,0xc0+(n&1));
}

void usleep(uint32_t micros)
{
  // Sleep for desired number of micro-seconds.
  // Each VIC-II raster line is ~64 microseconds
  // this is not totally accurate, but is a reasonable approach
  while(micros>64) {    
    uint8_t b=PEEK(0xD012);
    while(PEEK(0xD012)==b) continue;
    micros-=64;
  }
  return;
}

long reset_timeout;

unsigned char sdcard_reset(void)
{
  // Reset and release reset
  //  write_line("Resetting SD card...",0);

  // Clear SDHC flag
  POKE(sd_ctl,0x40);
  
  POKE(sd_ctl,0);
  POKE(sd_ctl,1);

  reset_timeout=100000L;
  
  // Now wait for SD card reset to complete
  while (PEEK(sd_ctl)&3) {
    POKE(0xd020,(PEEK(0xd020)+1)&15);
    reset_timeout--;
    if (!reset_timeout) return 0xff;
  }
  
  // Set SDHC flag, since we don't support SDC cards any more
  POKE(sd_ctl,0x41);

  return 0;
}

void mega65_fast(void)
{
  POKE(0,65);
}

void show_card_size(uint32_t sector_number)
{
  // Work out size in MB and tell user
  char col=8;
  uint32_t megs=(sector_number+1L)/2048L;
  uint32_t gigs=0;
  if (megs&0xffff0000L) {
    gigs=megs/1024L;
    megs=gigs;
  }
  screen_decimal(screen_line_address+2,megs);
  if (megs<10000) col=7;
  if (megs<1000) col=6;
  if (megs<100) col=5;
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
  if (sdcard_reset()) return 0;

  // SDHC claims 32GB limit, and reading from beyond that might cause
  // trouble. However, 32bits x 512byte sectors = 16TiB addressable.
  // It thus seems that the top byte of the address may not be safe to use,
  // or at least the top few bits.
  sector_number=0x02000000U;
  step=sector_number;

  // Work out size of SD card in a safe way
  // (binary search of sector numbers is NOT safe for some reason.
  //  It frequently reports bigger than the size of the card)
  sector_number=0;
  step=256*2048; // = 256MiB
  while(sector_number<0x10000000U) {
    //    write_line("Trying to read sector $",0);
    //    screen_hex(screen_line_address-80+24,sector_number);
    sdcard_readsector(sector_number);
    result=PEEK(sd_ctl)&0x63;
    if (result) {
      //      write_line("Read failed",2);
      // Failed to read this, so reduce step size, and then resume.

      // Reset card ready for next try
      sdcard_reset();
      
      sector_number-=step;
      step=step>>2;
      if (!step) break;
    } // else write_line("Read succeeded",2);
    sector_number+=step;
    //    mega65_getkey();

    // show card size as we figure it out,
    // and stay on the same line of output
    show_card_size(sector_number);
    POKE(0xD020U,PEEK(0xD020U)+1);
    screen_line_address-=80;
  }

  // Report number of sectors
  write_line("Maximum readable sector is $",2);
  screen_hex(screen_line_address-80+30,sector_number);
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

unsigned short timeout;

void do_read_sector(unsigned char cmd,uint32_t sector_number)
{
  char tries=0;  
  uint32_t sector_address=sector_number;

  POKE(sd_addr+0,(sector_address>>0)&0xff);
  POKE(sd_addr+1,(sector_address>>8)&0xff);
  POKE(sd_addr+2,((uint32_t)sector_address>>16)&0xff);
  POKE(sd_addr+3,((uint32_t)sector_address>>24)&0xff);

  //  write_line("Reading sector @ $",0);
  //  screen_hex(screen_line_address-80+18,sector_address);
  
  while(tries<10) {

    // Wait for SD card to be ready
    timeout=50000U;
    while (PEEK(sd_ctl)&0x3)
      {
	timeout--; if (!timeout) return;
	if (PEEK(sd_ctl)&0x40)
	  {
	    return;
	  }
	// Sometimes we see this result, i.e., sdcard.vhdl thinks it is done,
	// but sdcardio.vhdl thinks not. This means a read error
	if (PEEK(sd_ctl)==0x01) return;
      }

    // Command read
    POKE(sd_ctl,cmd);
    
    // Wait for read to complete
    timeout=50000U;
    while (PEEK(sd_ctl)&0x3) {
      timeout--; if (!timeout) return;
	//      write_line("Waiting for read to complete",0);
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

    if (!(PEEK(sd_ctl)&0x67)) {
      // Copy data from hardware sector buffer via DMA
      lcopy(sd_sectorbuffer,(long)sector_buffer,512);
  
      return;
    }
    
    POKE(0xd020,(PEEK(0xd020)+1)&0xf);

    // Reset SD card
    sdcard_open();

    tries++;
  }
  
}

void sdcard_readsector(const uint32_t sector_number)
{

  do_read_sector(0x02,sector_number);
}

void flash_readsector(const uint32_t sector_number)
{
  do_read_sector(0x53,sector_number);
}



uint8_t verify_buffer[512];

void sdcard_writesector(const uint32_t sector_number)
{
  // Copy buffer into the SD card buffer, and then execute the write job
  uint32_t sector_address;
  int i;
  char tries=0,result;
  uint16_t counter=0;

  while (PEEK(sd_ctl)&3) {
    continue;
  }

  // Set address to read/write
  POKE(sd_ctl,1); // end reset
  sector_address=sector_number;
  POKE(sd_addr+0,(sector_address>>0)&0xff);
  POKE(sd_addr+1,(sector_address>>8)&0xff);
  POKE(sd_addr+2,(sector_address>>16)&0xff);
  POKE(sd_addr+3,(sector_address>>24)&0xff);

  // Read the sector and see if it already has the correct contents.
  // If so, nothing to write

  POKE(sd_ctl,2); // read the sector we just wrote

  while (PEEK(sd_ctl)&3) {
    continue;
  }

  // Copy the read data to a buffer for verification
  lcopy(sd_sectorbuffer,(long)verify_buffer,512);
  
  // VErify that it matches the data we wrote
  for(i=0;i<512;i++) {
    if (sector_buffer[i]!=verify_buffer[i]) break;
  }
  if (i==512) {
    return;
  } 
  
  while(tries<10) {

    // Copy data to hardware sector buffer via DMA
    lcopy((long)sector_buffer,sd_sectorbuffer,512);
    
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
	  if (sector_number)
	    POKE(sd_ctl,0x57); // open SD card write gate
	  else
	    POKE(sd_ctl,0x4D); // open SD card write gate for MBR
	  POKE(sd_ctl,3); // retry write

	}
	// Show we are doing something
	//	POKE(0x804f,1+(PEEK(0x804f)&0x7f));
      }

    // Command write
    if (sector_number)
      POKE(sd_ctl,0x57); // open SD card write gate
    else
      POKE(sd_ctl,0x4D); // open SD card write gate for MBR
    POKE(sd_ctl,3);

    while (!(PEEK(sd_ctl)&3)) continue;
    
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
          if (sector_number)
            POKE(sd_ctl,0x57); // open SD card write gate
          else
            POKE(sd_ctl,0x4D); // open SD card write gate for MBR
	  POKE(sd_ctl,3); // retry write

	}
	// Show we are doing something
	//	POKE(0x809f,1+(PEEK(0x809f)&0x7f));
      }

    write_count++;
    POKE(0xD020,write_count&0x0f);

    // Note result
    result=PEEK(sd_ctl);
    
    if (!(PEEK(sd_ctl)&0x67)) {
      write_count++;
      
      POKE(0xD020,write_count&0x0f);

      // There is a bug in the SD controller: You have to read between writes, or it
      // gets really upset.

      // But sometimes even that doesn't work, and we have to reset it.

      // Does it just need some time between accesses?

      while (PEEK(sd_ctl)&3) {
      	continue;
      }

      POKE(sd_ctl,2); // read the sector we just wrote

      while (!(PEEK(sd_ctl)&3)) {
      	continue;
      }

      while (PEEK(sd_ctl)&3) {
      	continue;
      }

      // Copy the read data to a buffer for verification
      lcopy(sd_sectorbuffer,(long)verify_buffer,512);

      // VErify that it matches the data we wrote
      for(i=0;i<512;i++) {
	if (sector_buffer[i]!=verify_buffer[i]) break;
      }
      if (i!=512) {
	// VErify error has occurred
	write_line("Verify error for sector $$$$$$$$",0);
	screen_hex(screen_line_address-80+24,sector_number);
      }
      else {
      //      write_line("Wrote sector $$$$$$$$, result=$$",2);      
      //      screen_hex(screen_line_address-80+2+14,sector_number);
      //      screen_hex(screen_line_address-80+2+30,result);

	return;
      }
    }

    POKE(0xd020,(PEEK(0xd020)+1)&0xf);

  }

  write_line("Write error @ $$$$$$$$$",2);      
  screen_hex(screen_line_address-80+2+16,sector_number);
  
}

static uint16_t i;

void sdcard_readspeed_test(void)
{
  uint32_t n;
  uint32_t total_time=0;
  uint8_t last_raster=0;
  uint16_t speed;

  n=0;
  for(i=0;i<1000;i++) {
    POKE(sd_addr+0,(n>>0)&0xff);
    POKE(sd_addr+1,(n>>8)&0xff);
    POKE(sd_addr+2,(n>>16)&0xff);
    POKE(sd_addr+3,(n>>24)&0xff);
    n+=9873;
    n&=0xfffff;

    while (PEEK(sd_ctl)&3) continue;
    POKE(sd_ctl,0x02);
    while (!(PEEK(sd_ctl)&3)) continue;
    while (PEEK(sd_ctl)&3) {
      if (PEEK(0xD012U)!=last_raster) {
	total_time++;
	last_raster=PEEK(0xD012U);
      }
    }

    POKE(0xD020U,PEEK(0xD020U)+1);
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
  
  write_line("SD Card read speed =       KB/sec",2);
  screen_decimal(screen_line_address-80+23,speed);
}

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
    while (PEEK(sd_ctl)&3) continue;
    
    if (sector_number)
      POKE(sd_ctl,0x57); // open SD card write gate
    else
      POKE(sd_ctl,0x4D); // open SD card write gate for MBR
    if (n==first_sector) {
      // First sector of multi-sector write
      POKE(sd_ctl,0x04);
    } else {
      // Subsequent sector of multi-sector write
      POKE(sd_ctl,0x05);
    }

    // while (!(PEEK(sd_ctl)&3)) continue;
    POKE(0xD020U,1);
    
    while (PEEK(sd_ctl)&3) continue;
    POKE(0xD020U,0);
  }

  // End multi-sector write
  if (sector_number)
    POKE(sd_ctl,0x57); // open SD card write gate
  else
    POKE(sd_ctl,0x4D); // open SD card write gate for MBR
  POKE(sd_ctl,0x06);

  // Wait for SD card to go busy
  while (!(PEEK(sd_ctl)&3)) continue;
  
  // Wait for SD card to go ready
  while (PEEK(sd_ctl)&3) continue;
  
  // Try to flush cache?
  //  POKE(sd_ctl,0x0c);
  
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
    while (PEEK(sd_ctl)&3) continue;

    // Record sector number in start of each sector
    sector_buffer[0]=n>>0;
    sector_buffer[1]=n>>8;
    sector_buffer[2]=n>>16;
    sector_buffer[3]=n>>24;
    lcopy((long)sector_buffer,sd_sectorbuffer,512);
    POKE(SCREEN_ADDRESS+10*80+n-first_sector,lpeek(sd_sectorbuffer));
    
    if (sector_number)
      POKE(sd_ctl,0x57); // open SD card write gate
    else
      POKE(sd_ctl,0x4D); // open SD card write gate for MBR
    if (n==first_sector) {
      // First sector of multi-sector write
      POKE(sd_ctl,0x04);
    } else {
      // Subsequent sector of multi-sector write
      POKE(sd_ctl,0x05);
    }

    // while (!(PEEK(sd_ctl)&3)) continue;
    POKE(0xD020U,1);
    
    while (PEEK(sd_ctl)&3) continue;
    POKE(0xD020U,0);
  }

  // End multi-sector write
  if (sector_number)
    POKE(sd_ctl,0x57); // open SD card write gate
  else
    POKE(sd_ctl,0x4D); // open SD card write gate for MBR
  POKE(sd_ctl,0x06);

  // Wait for SD card to go busy
  while (!(PEEK(sd_ctl)&3)) continue;
  
  // Wait for SD card to go ready
  while (PEEK(sd_ctl)&3) continue;
  
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

void sdcard_erase(const uint32_t first_sector,const uint32_t last_sector)
{
  uint32_t n;
  lfill((uint32_t)sector_buffer,0,512);
  lcopy((long)sector_buffer,sd_sectorbuffer,512);

  //  fprintf(stderr,"ERASING SECTORS %d..%d\r\n",first_sector,last_sector);

#ifndef NOFAST_ERASE
  POKE(sd_addr+0,(first_sector>>0)&0xff);
  POKE(sd_addr+1,(first_sector>>8)&0xff);
  POKE(sd_addr+2,(first_sector>>16)&0xff);
  POKE(sd_addr+3,(first_sector>>24)&0xff);
#endif   
  
  for(n=first_sector;n<=last_sector;n++) {

#ifndef NOFAST_ERASE
    // Wait for SD card to go ready
    while (PEEK(sd_ctl)&3) continue;

    if (n)
      POKE(sd_ctl,0x57); // open SD card write gate
    else
      POKE(sd_ctl,0x4D); // open SD card write gate for MBR
    if (n==first_sector) {
      // First sector of multi-sector write
      POKE(sd_ctl,0x04);
    } else
      // All other sectors
      POKE(sd_ctl,0x05);

    // Wait for SD card to go busy
    while (!(PEEK(sd_ctl)&3)) continue;

    // Wait for SD card to go ready
    while (PEEK(sd_ctl)&3) continue;
       
#else
    sdcard_writesector(n);
#endif
    
    // Show count-down
    screen_decimal(screen_line_address+1,last_sector-n);
    //    fprintf(stderr,"."); fflush(stderr);
  }

#ifndef NOFAST_ERASE
  // Then say when we are done
  POKE(sd_ctl,0x57); // open SD card write gate
  POKE(sd_ctl,0x06);
  
  // Wait for SD card to go busy
  while (!(PEEK(sd_ctl)&3)) continue;
  
  // Wait for SD card to go ready
  while (PEEK(sd_ctl)&3) continue;
#endif    
  
}
