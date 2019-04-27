
CC65=	cc65/bin/cc65
CL65=	cc65/bin/cl65
COPTS=	-t c64 -O -Or -Oi -Os --cpu 65c02 -Icc65/include
LOPTS=	--asm-include-dir cc65/asminc --cfg-path cc65/cfg --lib-path cc65/lib

FILES=		m65fdisk.prg  m65fdisk

M65IDESOURCES=	fdisk.c \
		fdisk_memory.c \
		fdisk_screen.c \
		fdisk_fat32.c \
		fdisk_hal_mega65.c

ASSFILES=	fdisk.s \
		fdisk_memory.s \
		fdisk_screen.s \
		fdisk_fat32.s \
		fdisk_hal_mega65.s \
		charset.s

HEADERS=	Makefile \
		fdisk_memory.h \
		fdisk_screen.h \
		fdisk_fat32.h \
		fdisk_hal.h \
		ascii.h

DATAFILES=	ascii8x8.bin

%.s:	%.c $(HEADERS) $(DATAFILES) $(CC65)
	$(CC65) $(COPTS) -o $@ $<

all:	$(FILES)

$(CC65):
	git submodule init
	git submodule update
	(cd cc65 && make -j 8)

ascii8x8.bin: ascii00-7f.png pngprepare
	./pngprepare charrom ascii00-7f.png ascii8x8.bin

asciih:	asciih.c
	$(CC) -o asciih asciih.c
ascii.h:	asciih
	./asciih

pngprepare:	pngprepare.c
	$(CC) -I/usr/local/include -L/usr/local/lib -o pngprepare pngprepare.c -lpng

m65fdisk.prg:	$(ASSFILES) $(DATAFILES) $(CL65)
	$(CL65) $(COPTS) $(LOPTS) -vm -m m65fdisk.map -o m65fdisk.prg $(ASSFILES)

m65fdisk:	$(HEADERS) Makefile fdisk.c fdisk_fat32.c fdisk_hal_unix.c fdisk_memory.c fdisk_screen.c
	gcc -Wall -Wno-char-subscripts -o m65fdisk fdisk.c fdisk_fat32.c fdisk_hal_unix.c fdisk_memory.c fdisk_screen.c

clean:
	rm -f $(FILES)

cleangen:
	rm ascii8x8.bin
