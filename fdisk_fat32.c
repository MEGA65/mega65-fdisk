#include <stdio.h>
#include <string.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "ascii.h"

/*
  Create a file in the root directory of the new FAT32 filesystem
  with the indicated name and size.

  The file will be created contiguous on disk, and the first
  sector of the created file returned.

  The root directory is the start of cluster 2, and clusters are
  assumed to be 4KB in size, to keep things simple.
*/
long fat32_create_contiguous_file(char *name, long size,
				  long root_dir_sector,
				  long fat1_sector,
				  long fat2_sector)
{
  unsigned char i;
  unsigned short offset;
  unsigned short clusters;
  unsigned long start_cluster=0;
  unsigned long next_cluster;
  unsigned long contiguous_clusters=0;
  unsigned int fat_offset=0;
  char message[40]="Found file: ????????.???";

  clusters=size/4096;
  if (size&4095) clusters++;

  for(fat_offset=0;fat_offset <= (fat2_sector-fat1_sector); fat_offset++) {
    sdcard_readsector(fat1_sector+fat_offset);
    contiguous_clusters=0;
    start_cluster=0;

    // Skip any FAT sectors with allocated clusters
    int j;
    for(j=0;j<512;j++) if (sector_buffer[j]) break;
    if (j!=512) {
      continue;
    }
    
    for(offset=0;offset<512;offset+=4)
      {
	next_cluster=sector_buffer[offset];
	next_cluster|=((long)sector_buffer[offset+1]<<8L);
	next_cluster|=((long)sector_buffer[offset+2]<<16L);
	next_cluster|=((long)sector_buffer[offset+3]<<24L);
#ifdef __CC65__
	screen_decimal(screen_line_address-80+8,offset/4);
	screen_hex(screen_line_address-80+32,next_cluster);
#endif
	if (!next_cluster) {
	  if (!start_cluster) {
	    start_cluster=(offset/4)+fat_offset*(512/4);
	  }
	  contiguous_clusters++;
	  if (contiguous_clusters==clusters) {
	    // End of chain marker
	    sector_buffer[offset+0]=0xff; sector_buffer[offset+1]=0xff;
	    sector_buffer[offset+2]=0xff; sector_buffer[offset+3]=0x0f;
	    printf("Found enough contiguous clusters\n");
	    break;
	  } else {
	    // Point to next cluster
	    sector_buffer[offset+0]=(offset/4)+1;
	    sector_buffer[offset+1]=0; sector_buffer[offset+2]=0; sector_buffer[offset+3]=0;	  
	  }
	} else {
	  if (start_cluster) {
	    // write_line("ERROR: Disk space is fragmented. File not created.",0);
	    // 	    return 0;
	    // Not enough contiguous space in this FAT sector, so try the next
	    break;
	  }
	}
      }

    if (start_cluster&&(contiguous_clusters==clusters)) break;
    else {
      printf("FAT sector #%d : start_cluster=%d, contigous_clusters=%d\n",
	     fat_offset,start_cluster,contiguous_clusters);
    }
  }
  if ((!start_cluster)||(contiguous_clusters!=clusters)) {
    write_line("ERROR: Could not find enough free clusters in file system",0);
    return 0;
  }

  
#ifdef __CC65__
  write_line("First free cluster is ",0);
  screen_decimal(screen_line_address-80+22,start_cluster);
#else
  fprintf(stdout,"First free cluster is #%ld\n",start_cluster);
#endif

  // Commit sector to disk (in both copies of FAT)
  sdcard_writesector(fat1_sector);
  sdcard_writesector(fat2_sector);
  
  sdcard_readsector(root_dir_sector);
  
  for(offset=0;offset<512;offset+=32)
    {
      for(i=0;i<8;i++) message[12+i]=sector_buffer[offset + i];
      for(i=0;i<3;i++) message[21+i]=sector_buffer[offset + 8 + i];
      if (message[12]>' ') write_line(message,0);
      else break;
    }
  if (offset==512) {
    write_line("ERROR: First sector of root directory already full.",0);
    return 0;
  }

  // Build directory entry
  for(i=0;i<32;i++) sector_buffer[offset+i]=0x00;
  for(i=0;i<12;i++) sector_buffer[offset+i]=name[i];
  sector_buffer[offset+0x0b]=0x20; // Archive bit set
  sector_buffer[offset+0x1A]=start_cluster; 
  sector_buffer[offset+0x1B]=start_cluster>>8; 
  sector_buffer[offset+0x14]=start_cluster>>16; 
  sector_buffer[offset+0x15]=start_cluster>>24; 
  sector_buffer[offset+0x1C]=(size>>0)&0xff; 
  sector_buffer[offset+0x1D]=(size>>8L)&0xff; 
  sector_buffer[offset+0x1E]=(size>>16L)&0xff; 
  sector_buffer[offset+0x1F]=(size>>24l)&0xff;

  sdcard_writesector(root_dir_sector);

  printf("Found space starting at cluster %d\n",start_cluster);
  return root_dir_sector+(start_cluster-2)*8;
}
