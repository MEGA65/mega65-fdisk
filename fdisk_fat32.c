#include <stdio.h>
#include <string.h>

#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#ifdef __CC65__
#include "ascii.h"
#endif

extern uint32_t root_dir_sector;
extern uint32_t fat1_sector;
extern uint32_t fat2_sector;
extern uint32_t reserved_sectors;
extern uint8_t sectors_per_cluster;
extern uint32_t fat_sectors;
extern uint32_t fat_partition_start;
#define fat_copies 2
#define sectors_per_fat fat_sectors
#define root_dir_cluster 2

extern unsigned char sector_buffer[512];

void sdcard_readsector(const uint32_t sector_number);

void mega65_serial_monitor_write(char *s)
{
  fprintf(stderr, "SERIALOUT: %s\n", s);
#ifdef __CC65__
  while (*s) {
    // There is almost certainly a better way to do this, but it works.
    POKE(0x380, *s);
    __asm__("lda $0380");

    // Use CLC in the spare instruction slot, in case assembler tries to
    // optimise a NOP away.
    __asm__("sta $d643");
    __asm__("clc");
    s++;
  }
#endif
}

char hexchar2(unsigned char v)
{
  v = v & 0xf;
  if (v < 10)
    return '0' + v;
  return 0x41 + v - 10;
}

void hexout2(char *m, unsigned long v, int n)
{
  if (!n)
    return;
  do {
    m[n - 1] = hexchar2(v);
    v = v >> 4L;

  } while (--n);
}

char shbuf[11];
void serial_hex(unsigned long v)
{
  hexout2(shbuf, v, 8);
  shbuf[8] = 0x0d;
  shbuf[9] = 0x0a;
  shbuf[10] = 0;
  //  mega65_serial_monitor_write(shbuf);
}

#ifdef __CC65__
#define detect_target() (lpeek(0xffd3629))
#endif
#define TARGET_UNKNOWN 0
#define TARGET_MEGA65R1 1
#define TARGET_MEGA65R2 2
#define TARGET_MEGA65R3 3
#define TARGET_MEGAPHONER1 0x21
#define TARGET_NEXYS4 0x40
#define TARGET_NEXYS4DDR 0x41
#define TARGET_NEXYS4DDRWIDGET 0x42
#define TARGET_WUKONG 0xFD
#define TARGET_SIMULATION 0xFE

struct m65_tm {
  unsigned char tm_sec;   /* Seconds (0-60) */
  unsigned char tm_min;   /* Minutes (0-59) */
  unsigned char tm_hour;  /* Hours (0-23) */
  unsigned char tm_mday;  /* Day of the month (1-31) */
  unsigned char tm_mon;   /* Month (0-11) */
  unsigned short tm_year; /* Year - 1900 (in practice, never < 2000) */
  unsigned char tm_wday;  /* Day of the week (0-6, Sunday = 0) */
  int tm_yday;            /* Day in the year (0-365, 1 Jan = 0) */
  unsigned char tm_isdst; /* Daylight saving time */
};

unsigned char db1, db2, db3;

#ifdef __CC65__
unsigned char lpeek_debounced(long address)
{
  db1 = 0;
  db2 = 1;
  while (db1 != db2 || db1 != db3) {
    db1 = lpeek(address);
    db2 = lpeek(address);
    db3 = lpeek(address);
  }
  return db1;
}
#endif

unsigned char bcd_work;

unsigned char unbcd(unsigned char in)
{
  bcd_work = 0;
  while (in & 0xf0) {
    bcd_work += 10;
    in -= 0x10;
  }
  bcd_work += in;
  return bcd_work;
}

void getrtc(struct m65_tm *tm)
{
#ifdef __CC65__
  if (!tm)
    return;

  tm->tm_sec = 0;
  tm->tm_min = 0;
  tm->tm_hour = 0;
  tm->tm_mday = 0;
  tm->tm_mon = 0;
  tm->tm_year = 0;
  tm->tm_wday = 0;
  tm->tm_isdst = 0;

  switch (detect_target()) {
  case TARGET_MEGA65R2:
  case TARGET_MEGA65R3:
    tm->tm_sec = unbcd(lpeek_debounced(0xffd7110));
    tm->tm_min = unbcd(lpeek_debounced(0xffd7111));
    tm->tm_hour = lpeek_debounced(0xffd7112);
    if (tm->tm_hour & 0x80) {
      tm->tm_hour = unbcd(tm->tm_hour & 0x3f);
    }
    else {
      if (tm->tm_hour & 0x20) {
        tm->tm_hour = unbcd(tm->tm_hour & 0x1f) + 12;
      }
      else {
        tm->tm_hour = unbcd(tm->tm_hour & 0x1f);
      }
    }
    tm->tm_mday = unbcd(lpeek_debounced(0xffd7113)) - 1;
    tm->tm_mon = unbcd(lpeek_debounced(0xffd7114));
    // RTC is based on 2000, not 1900
    tm->tm_year = unbcd(lpeek_debounced(0xffd7115)) + 100;
    tm->tm_wday = unbcd(lpeek_debounced(0xffd7116));
    tm->tm_isdst = lpeek_debounced(0xffd7117) & 0x20;
    break;
  case TARGET_MEGAPHONER1:
    break;
  default:
    return;
  }
#endif
}

unsigned long fat32_follow_cluster(unsigned long cluster)
{
  unsigned long r;
  // Read out the cluster number from the FAT
  sdcard_readsector(fat1_sector + (cluster / 128));
  r = *((unsigned long *)&sector_buffer[(cluster & 127) << 2]);
  return r;
}

unsigned long fat32_allocate_cluster(unsigned long cluster)
{
  unsigned long r;
  unsigned long fat_sector_num;
  unsigned short i;

  // Find free cluster
  for (fat_sector_num = 0; fat_sector_num <= (fat2_sector - fat1_sector); fat_sector_num++) {
    sdcard_readsector(fat1_sector + fat_sector_num);
    for (i = 0; i < 512; i += 4) {
      if (sector_buffer[i])
        continue;
      if (sector_buffer[i + 3])
        continue;
      if (sector_buffer[i + 1])
        continue;
      if (sector_buffer[i + 2])
        continue;
    }
    if (i < 512) {
      // Found one
      r = fat_sector_num * 128 + (i >> 2);
      *((unsigned long *)&sector_buffer[i]) = cluster;
      sdcard_writesector(fat1_sector + fat_sector_num);
      sdcard_writesector(fat2_sector + fat_sector_num);
      return r;
    }
  }

  return 0;
}

#ifndef __CC65__
int are_there_gaps_between_files(void)
{
  unsigned long fat_sector_num = 0;
  int found_unallocated_cluster = 0;

  for (fat_sector_num = 0; fat_sector_num < (fat2_sector - fat1_sector); fat_sector_num++) {
    sdcard_readsector(fat_partition_start + fat1_sector + fat_sector_num);
    for (int j = 0; j < 512; j+=4) {
      int cval = sector_buffer[j] +
        (sector_buffer[j+1] << 8) +
        (sector_buffer[j+2] << 16) +
        (sector_buffer[j+3] << 24);
      if (!found_unallocated_cluster && cval == 0) {
        found_unallocated_cluster = 1;
        continue;
      }

      if (found_unallocated_cluster && cval != 0) {
        return 1;
      }
    }
  }
  return 0;
}
#endif

unsigned long find_free_cluster(unsigned long first_cluster)
{
  unsigned long cluster = 0;

  int retVal = 0;

  do {
    unsigned long i, o;

    i = first_cluster / (512 / 4);
    o = (first_cluster % (512 / 4)) * 4;

    for (; i < sectors_per_fat; i++) {
      // Read FAT sector
      //      printf("Checking FAT sector $%x for free clusters.\n",i);
      sdcard_readsector(fat_partition_start + fat1_sector + i);

      // Search for free sectors
      for (; o < 512; o += 4) {
        if (!(sector_buffer[o] | sector_buffer[o + 1] | sector_buffer[o + 2] | sector_buffer[o + 3])) {
          // Found a free cluster.
          cluster = i * (512 / 4) + (o / 4);
          // printf("cluster sector %d, offset %d yields cluster %d\n",i,o,cluster);
          break;
        }
      }
      o = 0;

      if (cluster || retVal)
        break;
    }

    // printf("I believe cluster $%x is free.\n",cluster);

    retVal = cluster;
  } while (0);

  return retVal;
}

int is_free_cluster(unsigned int cluster)
{
  int i, o;
  i = cluster / (512 / 4);
  o = cluster % (512 / 4) * 4;

  sdcard_readsector(fat_partition_start + fat1_sector + i);

  if (!(sector_buffer[o] | sector_buffer[o + 1] | sector_buffer[o + 2] | sector_buffer[o + 3])) {
    return 1;
  }

  return 0;
}

unsigned long find_contiguous_clusters(unsigned long total_clusters) {
  unsigned long start_cluster = 0;

  while (1) {
    int is_contiguous = 1;
    unsigned long cnt;
    start_cluster = find_free_cluster(start_cluster);

    for (cnt = 1; cnt < total_clusters; cnt++) {
      if (!is_free_cluster(start_cluster + cnt)) {
        is_contiguous = 0;
        break;
      }
    }

    if (is_contiguous)
      break;

    start_cluster += cnt;
  }

  return start_cluster;
}

/*
  Create a file in the root directory of the new FAT32 filesystem
  with the indicated name and size.

  The file will be created contiguous on disk, and the first
  sector of the created file returned.

  The root directory is the start of cluster 2, and clusters are
  assumed to be 4KB in size, to keep things simple.

  XXX -- Should allow creation of files in sub-directories

*/
long fat32_create_contiguous_file(char *name, long size, long root_dir_sector, long fat1_sector, long fat2_sector)
{
  unsigned char i = 0, sn = 0, len = 0;
  unsigned short offset = 0, j = 0;
  unsigned short clusters = 0;
  unsigned long k, start_cluster = 0;
  unsigned long dir_cluster = 2;
  unsigned long last_dir_cluster = 2;
  //  unsigned long next_cluster;
  unsigned long contiguous_clusters = 0;
  unsigned long fat_sector_num = 0;
  unsigned long fat_sector_count = 0;

  unsigned char have_dir_slot = 0;
  unsigned long free_dir_sector_num = 0;
  unsigned short free_dir_sector_ofs = 0;
  struct m65_tm tm;

  char message[40] = "Found file: ????????.???";

  clusters = size / (512 * sectors_per_cluster);
  if (size % (512 * sectors_per_cluster))
    clusters++;

  // Look for a free directory slot.
  // Also complain if the file already exists
  //  mega65_serial_monitor_write("Search for free directory slot\n");

  while (dir_cluster >= 2 && dir_cluster < 0xf0000000) {
    for (sn = 0; sn < sectors_per_cluster; sn++) {

      sdcard_readsector(root_dir_sector + ((dir_cluster - 2) * sectors_per_cluster) + sn);

      for (offset = 0; offset < 512; offset += 32) {
        for (i = 0; i < 8; i++)
          message[i] = sector_buffer[offset + i];
        len = 8;
        while (len && (message[len] == ' ' || message[len] == 0))
          len--;
        message[len++] = '.';
        for (i = 0; i < 3; i++)
          message[len + i] = sector_buffer[offset + 8 + i];
        len += 3;
        while (len && (message[len] == ' ' || message[len] == 0))
          len--;
        if (!strcmp(message, name)) {
          // ERROR: Name already exists
          //	  mega65_serial_monitor_write("File already exists\n");
          return 0;
        }

        // Is the slot free?
        if (sector_buffer[offset] == 0) {
          free_dir_sector_num = root_dir_sector + ((dir_cluster - 2) * sectors_per_cluster) + sn;
          free_dir_sector_ofs = offset;
          have_dir_slot = 1;
          //	  mega65_serial_monitor_write("Found free directory slot:\n");
          serial_hex(dir_cluster);
          serial_hex(sn);
          serial_hex(offset);

          break;
        }
      }
      if (have_dir_slot)
        break;
    }
    // Stop once we have found a free directory slot
    if (have_dir_slot)
      break;

    // Chain to next directory cluster, and extend directory
    // if required.
    last_dir_cluster = dir_cluster;
    dir_cluster = fat32_follow_cluster(dir_cluster);
    if ((!dir_cluster) || (dir_cluster >= 0xf0000000)) {
      // End of directory --
      dir_cluster = fat32_allocate_cluster(last_dir_cluster);

      //      mega65_serial_monitor_write("Allocating new directory cluster");
      serial_hex(dir_cluster);

      if ((!dir_cluster) || (dir_cluster >= 0xf0000000)) {
        // Disk full
        return 0;
      }
      else {

        // Zero out new directory cluster
        //	mega65_serial_monitor_write("Zeroing out new directory cluster\n");
        serial_hex(dir_cluster);
        lfill((unsigned long)sector_buffer, 0, 512);
        for (sn = 0; sn < sectors_per_cluster; sn++) {
          sdcard_readsector(root_dir_sector + ((dir_cluster - 2) * sectors_per_cluster) + sn);
        }
      }
    }
  }

  start_cluster = find_contiguous_clusters(clusters);

  //  mega65_serial_monitor_write("Found contiguous space beginning at cluster $");
  serial_hex(start_cluster);

  // Write cluster chain into both FATs
  //  mega65_serial_monitor_write("Writing FAT sectors for file\r\n");
  fat_sector_num = start_cluster / 128;
  //  next_cluster=start_cluster+1;
  fat_sector_count = clusters / 128;
  if (clusters & 127)
    fat_sector_count++;
  int start_offset = (start_cluster%128)*4;
  int cluster_tally = 0;
  for (k = 0; k < fat_sector_count; k++) {
    // Fill FAT sector with chain
    for (offset = start_offset; offset < 512; offset += 4) {
      if (cluster_tally < clusters) {
        // Write chain
        *(unsigned long *)&sector_buffer[offset] = (k << 7) + (offset >> 2) + 1;
      }
      if (cluster_tally == (clusters - 1)) {
        // Mark end of chain
        *(unsigned long *)&sector_buffer[offset] = 0x0FFFFFF8;
      }
      cluster_tally++;
    }
    start_offset = 0;
    // Write FAT sector to both FATs
    sdcard_writesector(fat1_sector + fat_sector_num + k);
    sdcard_writesector(fat2_sector + fat_sector_num + k);
  }

  // Build directory entry
  //  mega65_serial_monitor_write("Building directory entry\r\n");
  sdcard_readsector(free_dir_sector_num);
  // Clear entry
  for (i = 0; i < 32; i++)
    sector_buffer[free_dir_sector_ofs + i] = 0x00;
  // Write name
  // Fill name with space
  for (i = 0; i < 11; i++)
    sector_buffer[free_dir_sector_ofs + i] = 0x20;
  // Then overwrite with actual name
  for (i = 0, j = 0; i < 11; i++, j++) {
    if (name[j] == '.')
      i = 7;
    else
      sector_buffer[free_dir_sector_ofs + i] = name[j];
  }
  sector_buffer[free_dir_sector_ofs + 0x0b] = 0x20; // Archive bit set

  //  mega65_serial_monitor_write("Getting RTC timestamp\r\n");
  getrtc(&tm);
  //  mega65_serial_monitor_write("Got RTC timestamp\r\n");

  j = (tm.tm_hour << 11);
  j |= (tm.tm_min << 5);
  j |= (tm.tm_sec >> 1);
  // Create time 0x0e -- 0x0f
  *(unsigned short *)&sector_buffer[free_dir_sector_ofs + 0x0e] = j;
  // Modify time 0x16 -- 0x17
  //  *(unsigned short *)&sector_buffer[free_dir_sector_ofs + 0x16]=j;
  j = ((tm.tm_year - 80) << 9); // DOS is based on 1980, tm struct on 1900
  j |= (tm.tm_mon << 5);
  j |= tm.tm_mday;
  // Create date 0x10 -- 0x11
  *(unsigned short *)&sector_buffer[free_dir_sector_ofs + 0x10] = j;
  // Modify date 0x18 -- 0x19
  // *(unsigned short *)&sector_buffer[free_dir_sector_ofs + 0x18]=j;
  // Start cluster
  sector_buffer[free_dir_sector_ofs + 0x1A] = start_cluster;
  sector_buffer[free_dir_sector_ofs + 0x1B] = start_cluster >> 8;
  sector_buffer[free_dir_sector_ofs + 0x14] = start_cluster >> 16;
  sector_buffer[free_dir_sector_ofs + 0x15] = start_cluster >> 24;
  // File length
  sector_buffer[free_dir_sector_ofs + 0x1C] = (size >> 0) & 0xff;
  sector_buffer[free_dir_sector_ofs + 0x1D] = (size >> 8L) & 0xff;
  sector_buffer[free_dir_sector_ofs + 0x1E] = (size >> 16L) & 0xff;
  sector_buffer[free_dir_sector_ofs + 0x1F] = (size >> 24l) & 0xff;

  sdcard_writesector(free_dir_sector_num);
  //  mega65_serial_monitor_write("Wrote DIR sector $");
  serial_hex(free_dir_sector_num);
  //  mega65_serial_monitor_write("@ offset $");
  serial_hex(free_dir_sector_ofs);

  return root_dir_sector + (start_cluster - 2) * 8;
}
