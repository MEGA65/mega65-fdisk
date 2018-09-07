/*
  Extremely simplified FDISK + FORMAT utility for the MEGA65.
  This program is designed to be compilable both for the MEGA65
  using CC65, and also for UNIX-like operating systems for testing.
  All hardware dependent features will be in fdisk_hal_mega65.c and
  fdisk_hal_unix.c, respectively. I.e., this file contains only the
  hardware independent logic.

  This program gets the size of the SD card, and then calculates an
  appropriate MBR, DOS Boot Sector, FS Information Sector, FATs and
  root directory, and puts them in place.

  XXX - We should also create the MEGA65 system partitions for
  installed services, and for task switching.

*/

#include <stdio.h>
#include <string.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "fdisk_fat32.h"
#include "ascii.h"

uint8_t sector_buffer[512];

void clear_sector_buffer(void)
{
#ifndef __CC65__DONTUSE
  int i;
  for(i=0;i<512;i++) sector_buffer[i]=0;
#else
  lfill((uint32_t)sector_buffer,0,512);
#endif
}

/* Build a master boot record that has the single partition we need in
   the correct place, and with the size of the partition set correctly.
*/
void build_mbr(const uint32_t sys_partition_start,
	       const uint32_t sys_partition_sectors,
	       const uint32_t fat_partition_start,
	       const uint32_t fat_partition_sectors
	       )
{
  clear_sector_buffer();

  // Set disk signature (fixed value)
  sector_buffer[0x1b8]=0x83;
  sector_buffer[0x1b9]=0x7d;
  sector_buffer[0x1ba]=0xcb;
  sector_buffer[0x1bb]=0xa6;

  // MEGA65 System Partition entry
  sector_buffer[0x1be]=0x00;  // Not bootable by DOS
  sector_buffer[0x1bf]=0x00;  // 3 bytes CHS starting point
  sector_buffer[0x1c0]=0x00;
  sector_buffer[0x1c1]=0x00;
  sector_buffer[0x1c2]=0x41;  // Partition type (MEGA65 System Partition)
  sector_buffer[0x1c3]=0x00;  // 3 bytes CHS end point - SHOULD CHANGE WITH DISK SIZE
  sector_buffer[0x1c4]=0x00;
  sector_buffer[0x1c5]=0x00;
  // LBA starting sector of partition (usually @ 0x0800 = sector 2,048 = 1MB)
  sector_buffer[0x1c6]=(sys_partition_start>>0)&0xff;  
  sector_buffer[0x1c7]=(sys_partition_start>>8)&0xff;  
  sector_buffer[0x1c8]=(sys_partition_start>>16)&0xff;  
  sector_buffer[0x1c9]=(sys_partition_start>>24)&0xff;  
  // LBA size of partition in sectors
  sector_buffer[0x1ca]=(sys_partition_sectors>>0)&0xff;  
  sector_buffer[0x1cb]=(sys_partition_sectors>>8)&0xff;  
  sector_buffer[0x1cc]=(sys_partition_sectors>>16)&0xff;  
  sector_buffer[0x1cd]=(sys_partition_sectors>>24)&0xff;  
  
  
  // FAT32 Partition entry
  sector_buffer[0x1ce]=0x00;  // Not bootable by DOS
  sector_buffer[0x1cf]=0x00;  // 3 bytes CHS starting point
  sector_buffer[0x1d0]=0x00;
  sector_buffer[0x1d1]=0x00;
  sector_buffer[0x1d2]=0x0c;  // Partition type (VFAT32)
  sector_buffer[0x1d3]=0x00;  // 3 bytes CHS end point - SHOULD CHANGE WITH DISK SIZE
  sector_buffer[0x1d4]=0x00;
  sector_buffer[0x1d5]=0x00;
  // LBA starting sector of FAT32 partition
  sector_buffer[0x1d6]=(fat_partition_start>>0)&0xff;  
  sector_buffer[0x1d7]=(fat_partition_start>>8)&0xff;  
  sector_buffer[0x1d8]=(fat_partition_start>>16)&0xff;  
  sector_buffer[0x1d9]=(fat_partition_start>>24)&0xff;  
  // LBA size of partition in sectors
  sector_buffer[0x1da]=(fat_partition_sectors>>0)&0xff;  
  sector_buffer[0x1db]=(fat_partition_sectors>>8)&0xff;  
  sector_buffer[0x1dc]=(fat_partition_sectors>>16)&0xff;  
  sector_buffer[0x1dd]=(fat_partition_sectors>>24)&0xff;  

  // MBR signature
  sector_buffer[0x1fe]=0x55;
  sector_buffer[0x1ff]=0xaa;
}


uint8_t boot_bytes[258]={
  // Jump to boot code, required by most version of DOS
  0xeb, 0x58, 0x90,
  
  // OEM String: MEGA65r1
  0x4d, 0x45, 0x47, 0x41, 0x36, 0x35, 0x72, 0x31,
  
  // BIOS Parameter block.  We patch certain
  // values in here.
  0x00, 0x02,  // Sector size = 512 bytes
  0x08 , // Sectors per cluster
  /* 0x0e */ 0x38, 0x02,  // Number of reserved sectors (0x238 = 568)
  /* 0x10 */ 0x02, // Number of FATs
  0x00, 0x00, // Max directory entries for FAT12/16 (0 for FAT32)
  /* offset 0x13 */ 0x00, 0x00, // Total logical sectors (0 for FAT32)
  0xf8, // Disk type (0xF8 = hard disk)
  0x00, 0x00, // Sectors per FAT for FAT12/16 (0 for FAT32)
  /* offset 0x18 */ 0x00, 0x00, // Sectors per track (0 for LBA only)
  0x00, 0x00, // Number of heads for CHS drives, zero for LBA
  0x00, 0x00, 0x00, 0x00, // 32-bit Number of hidden sectors before partition. Should be 0 if logical sectors == 0
  
  /* 0x20 */ 0x00, 0xe8, 0x0f, 0x00, // 32-bit total logical sectors
  /* 0x24 */ 0xf8, 0x03, 0x00, 0x00, // Sectors per FAT
  /* 0x28 */ 0x00, 0x00, // Drive description
  /* 0x2a */ 0x00, 0x00, // Version 0.0
  /* 0x2c */ 0x02, 0x00 ,0x00, 0x00, // Number of first cluster
  /* 0x30 */ 0x01, 0x00, // Logical sector of FS Information sector
  /* 0x32 */ 0x06, 0x00, // Sector number of backup-copy of boot sector
  /* 0x34 */ 0x00, 0x00, 0x00, 0x00, // Filler bytes
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ,0x00, 0x00, // Filler bytes
  /* 0x40 */ 0x80, // Physical drive number
  /* 0x41 */ 0x00, // FAT12/16 use only
  /* 0x42 */ 0x29, // 0x29 == Extended Boot Signature
  /* 0x43 */ 0x6d, 0x66, 0x62, 0x61, // Volume ID "mfba"
  /* 0x47 */ 0x4d, 0x2e, 0x45, 0x2e, 0x47, // 11 byte volume label
  0x2e, 0x41 ,0x2e, 0x20, 0x36, 0x35,
  /* 0x52 */ 0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20, // "FAT32   "
  // Boot loader code starts here
  0x0e, 0x1f, 0xbe, 0x77 ,0x7c, 0xac,
  0x22, 0xc0, 0x74, 0x0b, 0x56, 0xb4, 0x0e, 0xbb,
  0x07, 0x00, 0xcd, 0x10, 0x5e, 0xeb ,0xf0, 0x32,
  0xe4, 0xcd, 0x16, 0xcd, 0x19, 0xeb, 0xfe,
  // From here on is the non-bootable error message
  // 0x82 - 0x69 = 
  0x4d, 0x45, 0x47, 0x41, 0x36, 0x35, 0x20, 0x4b,
  0x49, 0x43, 0x4b, 0x53, 0x54, 0x41, 0x52, 0x54,
  0x20, 0x56, 0x30, 0x30, 0x2e, 0x31, 0x31,
  0x0d, 0x0a, 0x0d, 0x3f, 0x4e, 0x4f, 0x20, 0x34,
  0x35, 0x47, 0x53, 0x30, 0x32, 0x2c, 0x20, 0x34,
  0x35, 0x31, 0x30, 0x2c, 0x20, 0x36, 0x35, 0x5b,
  0x63, 0x65, 0x5d, 0x30, 0x32, 0x2c, 0x20, 0x36,
  0x35, 0x31, 0x30, 0x20, 0x4f, 0x52, 0x20, 0x38,
  0x35, 0x31, 0x30, 0x20, 0x50, 0x52, 0x4f, 0x43,
  0x45, 0x53, 0x53, 0x4f, 0x52, 0x20, 0x20, 0x45,
  0x52, 0x52, 0x4f, 0x52, 0x0d, 0x0a, 0x49, 0x4e, 0x53,
  0x45, 0x52, 0x54, 0x20, 0x44, 0x49, 0x53, 0x4b,
  0x20, 0x49, 0x4e, 0x20, 0x52, 0x45, 0x41, 0x4c,
  0x20, 0x43, 0x4f, 0x4d, 0x50, 0x55, 0x54, 0x45,
  0x52, 0x20, 0x41, 0x4e, 0x44, 0x20, 0x54, 0x52,
  0x59, 0x20, 0x41, 0x47, 0x41, 0x49, 0x4e, 0x2e,
  0x0a, 0x0a, 0x52, 0x45, 0x41, 0x44, 0x59, 0x2e,
  0x0d, 0x0a
  
};

void build_dosbootsector(const uint8_t volume_name[11],
			 uint32_t data_sectors, uint32_t fs_sectors_per_fat)
{
  uint16_t i;
  
  clear_sector_buffer();
  
  // Start with template, and then modify relevant fields */
  for(i=0;i<sizeof(boot_bytes);i++) sector_buffer[i]=boot_bytes[i];

  // 0x20-0x23 = 32-bit number of data sectors in file system
  for(i=0;i<4;i++) sector_buffer[0x20+i]=((data_sectors)>>(i*8))&0xff;

  // 0x24-0x27 = 32-bit number of sectors per fat
  for(i=0;i<4;i++) sector_buffer[0x24+i]=((fs_sectors_per_fat)>>(i*8))&0xff;

  // 0x43-0x46 = 32-bit volume ID (random bytes)
  // 0x47-0x51 = 11 byte volume string
  
  // Boot sector signature
  sector_buffer[510]=0x55;
  sector_buffer[511]=0xaa;
  
}

void build_fs_information_sector(const uint32_t fs_clusters)
{
  uint8_t i;
  
  clear_sector_buffer();
  
  sector_buffer[0]=0x52;
  sector_buffer[1]=0x52;  
  sector_buffer[2]=0x61;
  sector_buffer[3]=0x41;
  
  sector_buffer[0x1e4]=0x72;
  sector_buffer[0x1e5]=0x72;  
  sector_buffer[0x1e6]=0x41;
  sector_buffer[0x1e7]=0x61;

  // Last free cluster = (cluster count - 1)
#ifndef __CC65__
  fprintf(stderr,"Writing fs_clusters (0x%x) as ",fs_clusters);
#endif
  for(i=0;i<4;i++) {
    // Number of free clusters
    sector_buffer[0x1e8+i]=((fs_clusters-3)>>(i*8))&0xff;
#ifndef __CC65__
    fprintf(stderr,"%02x ",sector_buffer[0x1e8+i]);
#endif
  }
#ifndef __CC65__
  fprintf(stderr,"\n");
#endif
  
  // First free cluster = 2
  sector_buffer[0x1ec]=0x02+1;  // OSX newfs/fsck puts 3 here instead?

  // Boot sector signature
  sector_buffer[510]=0x55;
  sector_buffer[511]=0xaa;
}

uint8_t fat_bytes[12]={0xf8,0xff,0xff,0x0f,0xff,0xff,0xff,0x0f,0xf8,0xff,0xff,0x0f};

void build_empty_fat()
{
  int i;
  clear_sector_buffer();
  for(i=0;i<12;i++) sector_buffer[i]=fat_bytes[i];  
}

uint8_t dir_bytes[15]={8,0,0,0x53,0xae,0x93,0x4a,0x93,0x4a,0,0,0x53,0xae,0x93,0x4a};

void build_root_dir(const uint8_t volume_name[11])
{
  int i;
  clear_sector_buffer();
  for(i=0;i<11;i++) sector_buffer[i]=volume_name[i];
  for(i=0;i<15;i++) sector_buffer[11+i]=dir_bytes[i];
}

uint32_t sdcard_sectors;

uint32_t sys_partition_start,sys_partition_sectors;
uint32_t fat_partition_start,fat_partition_sectors;

uint32_t sys_partition_freeze_dir;
uint16_t freeze_dir_sectors;
uint32_t sys_partition_service_dir;
uint16_t service_dir_sectors;

// Calculate clusters for file system, and FAT size
uint32_t fs_clusters=0;
uint32_t reserved_sectors=568; // not sure why we use this value
uint32_t rootdir_sector=0;
uint32_t fat_sectors=0;
uint32_t fat1_sector=0;
uint32_t fat2_sector=0;
uint32_t fs_data_sectors=0;
uint8_t sectors_per_cluster=8;  // 4KB clusters
uint8_t volume_name[11]="M.E.G.A.65!";

// Work out maximum number of clusters we can accommodate
uint32_t sectors_required;
uint32_t fat_available_sectors;

void sector_buffer_write_uint16(const uint16_t offset,
				 const uint32_t value)
{
  sector_buffer[offset+0]=(value>>0)&0xff;
  sector_buffer[offset+1]=(value>>8)&0xff;
}

void sector_buffer_write_uint32(const uint16_t offset,
				const uint32_t value)
{
  sector_buffer[offset+0]=(value>>0)&0xff;
  sector_buffer[offset+1]=(value>>8)&0xff;
  sector_buffer[offset+2]=(value>>16)&0xff;
  sector_buffer[offset+3]=(value>>24)&0xff;
}

uint8_t sys_part_magic[]={'M','E','G','A','6','5','S','Y','S','0','0'};

void build_mega65_sys_sector(const uint32_t sys_partition_sectors)
{
  /*
    System partition has frozen program and system service areas, including 
    directories for each.

    We work out how many of each we can have (equal number of each), based
    on the size we require them to be.

    The size of each is subject to change, so is left flexible.  One thing
    that is not resolved, is whether to allow including a D81 image in either.

    For now, we will allow 128K RAM + 128KB "ROM" + 32KB colour RAM 
    + 32KB IO regs (including 4KB thumbnail).
    That all up means 320KB per frozen program slot.
    
    For some services at least, we intend to allow for a substantial part of
    memory to be preserved, so we need to have a mechanism that indicates what
    parts of which memory areas require preservation, and whether IO should be
    preserved or not.

    Simple apporach is KB start and end ranges for the four regions = 8 bytes.
    This can go in the directory entries, which have 64 bytes for information.
    We will also likely put the hardware/access permission flags in there
    somewhere, too.

    Anyway, so we need to divide the available space by (320KB + 128 bytes).
    Two types of area means simplest approach with equal slots for both means
    dividing space by (320KB + 128 bytes)*2= ~641KB.    
  */
  uint16_t i;
  uint32_t slot_size=320*1024/512;
  // Take 1MB from partition size, for reserved space when
  // calculating what can fit.
  uint32_t reserved_sectors=1024*1024/512;
  uint32_t slot_count=(sys_partition_sectors-reserved_sectors)/(641*1024/512);
  uint16_t dir_size;

  // Limit number of freeze slots to 16 bit counters
  if (slot_count>=0xffff) slot_count=0xffff;
  
  dir_size=1+(slot_count/4);

  freeze_dir_sectors=dir_size;
  service_dir_sectors=dir_size;

  // Freeze directory begins at 1MB
  sys_partition_freeze_dir=reserved_sectors;
  // System service directory begins after that
  sys_partition_service_dir=
    sys_partition_freeze_dir+slot_size*slot_count;
  
  write_line("      Freeze and OS Service slots.",0);
  screen_decimal(screen_line_address-80,slot_count);
  
  
  // Clear sector
  clear_sector_buffer();

  // Write magic bytes
  for(i=0;i<11;i++) sector_buffer[i]=sys_part_magic[i];

  // $010-$013 = Start of freeze program area
  sector_buffer_write_uint32(0x10,0);
  // $014-$017 = Size of freeze program area
  sector_buffer_write_uint32(0x14,slot_size*slot_count+dir_size);
  // $018-$01b = Size of each freeze program slot
  sector_buffer_write_uint32(0x18,slot_size);
  // $01c-$01d = Number of freeze slots
  sector_buffer_write_uint16(0x1c,slot_count);
  // $01e-$01f = Number of sectors in freeze slot directory
  sector_buffer_write_uint16(0x1e,dir_size);

  // $020-$023 = Start of freeze program area
  sector_buffer_write_uint32(0x20,slot_size*slot_count+dir_size);
  // $024-$027 = Size of service program area
  sector_buffer_write_uint32(0x24,slot_size*slot_count+dir_size);
  // $028-$02b = Size of each service slot
  sector_buffer_write_uint32(0x28,slot_size);
  // $02c-$02d = Number of service slots
  sector_buffer_write_uint16(0x2c,slot_count);
  // $02e-$02f = Number of sectors in service slot directory
  sector_buffer_write_uint16(0x2e,dir_size);

  // Now make sector numbers relative to start of disk for later use
  sys_partition_freeze_dir+=sys_partition_start;
  sys_partition_service_dir+=sys_partition_start;
  
  return;
}

void show_partition_entry(const char i)
{
  char j;
  char report[80]="$$* : Start=%%%/%%/%%%% or $$$$$$$$ / End=%%%/%%/%%%% or $$$$$$$$";
  
  int offset=0x1be+(i<<4);

  char active=sector_buffer[offset+0];
  char shead=sector_buffer[offset+1];
  char ssector=sector_buffer[offset+2]&0x1f;
  int scylinder=((sector_buffer[offset+2]<<2)&0x300)+sector_buffer[offset+3];
  char id=sector_buffer[offset+4];
  char ehead=sector_buffer[offset+5];
  char esector=sector_buffer[offset+6]&0x1f;
  int ecylinder=((sector_buffer[offset+6]<<2)&0x300)+sector_buffer[offset+7];
  uint32_t lba_start,lba_end;

  for(j=0;j<4;j++) ((char *)&lba_start)[j]=sector_buffer[offset+8+j];
  for(j=0;j<4;j++) ((char *)&lba_end)[j]=sector_buffer[offset+12+j];
  
  format_hex((int)report+0,id,2);
  if (!(active&0x80)) report[2]=' '; // not active

  format_decimal((int)report+12,shead,3);
  format_decimal((int)report+16,ssector,2);
  format_decimal((int)report+19,scylinder,4);
  format_hex((int)report+27,lba_start,8);

  format_decimal((int)report+42,ehead,3);
  format_decimal((int)report+46,esector,2);
  format_decimal((int)report+49,ecylinder,4);
  format_hex((int)report+57,lba_end,8);

  write_line(report,0);
  
}

void show_mbr(void)
{
  char i;
  
  sdcard_readsector(0);  
  
  write_line(" ",0);

  if ((sector_buffer[0x1fe]!=0x55)||(sector_buffer[0x1ff]!=0xAA))
    write_line("Current partition table is invalid.",0);
  else {  
    write_line("Current partition table:",0);
    for(i=0;i<4;i++) {
      show_partition_entry(i);
    }
  }
}

#ifdef __CC65__
void main(void)
#else
int main(int argc,char **argv)
#endif
{
#ifdef __CC65__
  mega65_fast();
  setup_screen();
#endif  
  
  sdcard_open();

  // Memory map the SD card sector buffer on MEGA65
  sdcard_map_sector_buffer();

  write_line("Detecting SD card type and size (can take a while)",0);
  
  sdcard_sectors = sdcard_getsize();

  // Show summary of current MBR
  show_mbr();
  
  // Calculate sectors for the system and FAT32 partitions.
  // This is the size of the card, minus 2,048 (=0x0800) sectors.
  // The system partition should be sized to be not more than 50% of
  // the SD card, and probably doesn't need to be bigger than 2GB, which would
  // allow 1GB for 1,024 1MB freeze images and 1,024 1MB service images.
  // (note that freeze images might end up being a funny size to allow for all
  // mem plus a D81 image to be saved. This is all to be determined.)
  // Simple solution for now: Use 1/2 disk for system partition, or 2GiB, whichever
  // is smaller.
  sys_partition_sectors=(sdcard_sectors-0x0800)>>1;
  if (sys_partition_sectors>(2*1024*1024*1024/512))
    sys_partition_sectors=(2*1024*1024*1024/512);
  sys_partition_sectors&=0xfffff800; // round down to nearest 1MB boundary
  fat_partition_sectors=sdcard_sectors-0x800-sys_partition_sectors;

  fat_available_sectors=fat_partition_sectors-reserved_sectors;

  fs_clusters=fat_available_sectors/(sectors_per_cluster);
  fat_sectors=fs_clusters/(512/4); if (fs_clusters%(512/4)) fat_sectors++;
  sectors_required=2*fat_sectors+((fs_clusters-2)*sectors_per_cluster);
  while(sectors_required>fat_available_sectors) {
    uint32_t excess_sectors=sectors_required-fat_available_sectors;
    uint32_t delta=(excess_sectors/(1+sectors_per_cluster));
    if (delta<1) delta=1;
#ifndef __CC65__
    fprintf(stderr,"%d clusters would take %d too many sectors.\r\n",
	    fs_clusters,sectors_required-fat_available_sectors);
#endif
    fs_clusters-=delta;
    fat_sectors=fs_clusters/(512/4); if (fs_clusters%(512/4)) fat_sectors++;
    sectors_required=2*fat_sectors+((fs_clusters-2)*sectors_per_cluster);
  }
#ifndef __CC65__
  fprintf(stderr,"VFAT32 PARTITION HAS $%x SECTORS ($%x AVAILABLE)\r\n",
	  fat_partition_sectors,fat_available_sectors);
#else
  // Tell use how many sectors available for partition
  write_line(" ",0);
  write_line("$         Sectors available for MEGA65 System partition.",0);
  screen_hex(screen_line_address-79,sys_partition_sectors);
  build_mega65_sys_sector(sys_partition_sectors);

  write_line("$         Sectors available for VFAT32 partition.",0);
  screen_hex(screen_line_address-79,fat_partition_sectors);
#endif
  
  fat_partition_start=0x00000800;
  sys_partition_start=fat_partition_start+fat_partition_sectors;
  
  fat1_sector=reserved_sectors;
  fat2_sector=fat1_sector+fat_sectors;
  rootdir_sector=fat2_sector+fat_sectors;
  fs_data_sectors=fs_clusters*sectors_per_cluster;

#ifndef __CC65__
  fprintf(stderr,"Creating File System with %u (0x%x) CLUSTERS, %d SECTORS PER FAT, %d RESERVED SECTORS.\r\n",
	  fs_clusters,fs_clusters,fat_sectors,reserved_sectors);
#else
  write_line(" ",0);
  write_line("Format SD Card with new partition table and FAT32 file fystem?",0);
  recolour_last_line(7);
  {
    char col=6;
    int megs=(fat_partition_sectors+1)/2048;
    screen_decimal(screen_line_address+2,megs);
    if (megs<10000) col=5;
    if (megs<1000) col=4;
    if (megs<100) col=3;
    if (megs<10) col=2;
    write_line("MiB VFAT32 Data Partition @ $$$$$$$$:",2+col);
    screen_hex(screen_line_address-80+28+2+col,fat_partition_start);
  }
  write_line("  $         Clusters,       Sectors/FAT,       Reserved Sectors.",0);
  screen_hex(screen_line_address-80+3,fs_clusters);
  screen_decimal(screen_line_address-80+22,fat_sectors);
  screen_decimal(screen_line_address-80+41,reserved_sectors);

  {
    char col=6;
    int megs=(sys_partition_sectors+1)/2048;
    screen_decimal(2+screen_line_address,megs);
    if (megs<10000) col=5;
    if (megs<1000) col=4;
    if (megs<100) col=3;
    if (megs<10) col=2;
    write_line("MiB MEGA65 System Partition @ $$$$$$$$:",2+col);
    screen_hex(screen_line_address-80+30+2+col,sys_partition_start);
  }

  while(1)
  {
    char line_of_input[80];
    unsigned char len;
    write_line(" ",0);
    write_line("Type DELETE EVERYTHING to continue:",0);
    recolour_last_line(2);
    len=read_line(line_of_input,80);
    if (len) {
      write_line(line_of_input,0);
      recolour_last_line(7);
    }
    if (strcmp("DELETE EVERYTHING",line_of_input)) {
      write_line("Entered text does not match. Try again.",0);
      recolour_last_line(8);
    } else
      // String matches -- so proceed
      break;
  }
#endif
  
  // MBR is always the first sector of a disk
#ifdef __CC65__
  write_line(" ",0);
  write_line("Writing Partition Table / Master Boot Record...",0);
#endif
  build_mbr(sys_partition_start,
	    sys_partition_sectors,
	    fat_partition_start,
	    fat_partition_sectors);
  sdcard_writesector(0);
  show_mbr();

  while(0) {
  build_mbr(sys_partition_start,
	    sys_partition_sectors,
	    fat_partition_start,
	    fat_partition_sectors);
  sdcard_writesector(0);
  //  show_mbr();

  }


#ifdef __CC65__
  // write_line("Erasing reserved sectors before first partition...",0);
#endif
  // Blank intervening sectors
  //  sdcard_erase(0+1,sys_partition_start-1);

  if (1) {
  // Write MEGA65 System partition header sector
#ifdef __CC65__
  write_line("Writing MEGA65 System Partition header sector...",0);
#endif
  build_mega65_sys_sector(sys_partition_sectors);
  sdcard_writesector(sys_partition_start);

#ifdef __CC65__
  write_line("Freeze  dir @ $        ",0);
  screen_hex(screen_line_address-79+14,sys_partition_freeze_dir);
  write_line("Service dir @ $        ",0);
  screen_hex(screen_line_address-79+14,sys_partition_service_dir);
  
#endif

  // Erase 1MB reserved area
  write_line("Erasing configuration area",0);
  sdcard_erase(sys_partition_start+1,sys_partition_start+1023);
  
  // erase frozen program directory  
  write_line("Erasing frozen program and system service directories",0);
  sdcard_erase(sys_partition_freeze_dir,
	       sys_partition_freeze_dir+freeze_dir_sectors-1);
  
  // erase system service image directory
  sdcard_erase(sys_partition_service_dir,
	       sys_partition_service_dir+service_dir_sectors-1);

  }
    
#ifdef __CC65__
  write_line("Writing FAT Boot Sector...",0);
#endif
  // Partition starts at fixed position of sector 2048, i.e., 1MB
  build_dosbootsector(volume_name,
		      fat_partition_sectors,
		      fat_sectors);
  sdcard_writesector(fat_partition_start);
  sdcard_writesector(fat_partition_start+6); // Backup boot sector at partition + 6

#ifdef __CC65__
  write_line("Writing FAT Information Block (and backup copy)...",0);
#endif
  // FAT32 FS Information block (and backup)
  build_fs_information_sector(fs_clusters);
  sdcard_writesector(fat_partition_start+1);
  sdcard_writesector(fat_partition_start+7);

  // FATs
#ifndef __CC65__
  fprintf(stderr,"Writing FATs at offsets 0x%x AND 0x%x\r\n",
	  fat1_sector*512,fat2_sector*512);
#else
  write_line("Writing FATs at $         and $         ...",0);
  screen_hex(screen_line_address-80+17,fat1_sector*512);
  screen_hex(screen_line_address-80+31,fat2_sector*512);
#endif
  build_empty_fat(); 
  sdcard_writesector(fat_partition_start+fat1_sector);
  sdcard_writesector(fat_partition_start+fat2_sector);

#ifdef __CC65__
  write_line("Writing Root Directory...",0);
#endif
  // Root directory
  build_root_dir(volume_name);
  sdcard_writesector(fat_partition_start+rootdir_sector);

#ifdef __CC65__
  write_line(" ",0);
  write_line("Clearing file system data structures...",0);
#endif
  // Make sure all other sectors are empty
#if 1
    sdcard_erase(fat_partition_start+1+1,fat_partition_start+6-1);
    sdcard_erase(fat_partition_start+6+1,fat_partition_start+fat1_sector-1);
    sdcard_erase(fat_partition_start+fat1_sector+1,fat_partition_start+fat2_sector-1);
    sdcard_erase(fat_partition_start+fat2_sector+1,fat_partition_start+rootdir_sector-1);
    sdcard_erase(fat_partition_start+rootdir_sector+1,fat_partition_start+rootdir_sector+1+sectors_per_cluster-1);
#endif

  // Write current rom to MEGA65.ROM on the FAT32 file system
  // (This is as a convenience during development, where I end up formatting the SD card a lot, and it is a pain
  // to have to remove the SD card to put the ROM back on each time)
  if ((lpeek(0x2A004L)=='C')&&(lpeek(0x2A005L)=='B')&&(lpeek(0x2A006L)=='M'))
    {
      unsigned long first_sector;
      write_line("Writing current loaded ROM to FAT32 file system",0);

      // Check if kickstart has patched $FFD2 (which it does for utility menu jobs,
      // when it thinks there is no ROM loaded, which currently is always).
      if (lpeek(0x2ffd2L)==0x60) lpoke(0x2ffd2L,0x6c);
      
      first_sector=fat32_create_contiguous_file("MEGA65  ROM",0x20000L,
						fat_partition_start+rootdir_sector,
						fat_partition_start+fat1_sector,
						fat_partition_start+fat2_sector);
      if (first_sector) {
	// Write out ROM sectors
	unsigned long addr;
	for(addr=0x20000;addr<=0x40000;addr+=512)
	  {
	    lcopy(addr,sector_buffer,512);
	    sdcard_writesector(first_sector+(addr-0x20000)/512);
	  }
	write_line("Completed writing ROM",0);
      }
    } else {
    write_line("No ROM currently loaded (SIG=$$$$$$$).",0);
    screen_hex_byte(screen_line_address-80+30,lpeek(0x2a004));
    screen_hex_byte(screen_line_address-80+32,lpeek(0x2a005));
    screen_hex_byte(screen_line_address-80+34,lpeek(0x2a006));
  }
  
  
#ifdef __CC65__
  POKE(0xd021U,6);
  write_line(" ",0);
  write_line("SD Card has been formatted.  Remove, Copy MEGA65.ROM, Reinsert AND Reboot.",0);
  while(1) continue;
#else
  return 0;
#endif
  
}
