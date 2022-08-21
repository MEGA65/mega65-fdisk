
ifdef USE_LOCAL_CC65
	# use locally installed binary (requires cc65 to be in the $PATH)
	CC65=cc65
	CL65=cl65
else
	# use the binary built from the submodule
	CC65=cc65/bin/cc65
	CL65=cc65/bin/cl65
endif

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
	$(warning ======== Making: $@)
	$(CC65) $(COPTS) -o $@ $<

all:	$(FILES)

format:
	submodules=""; for sm in `git submodule | awk '{ print "./" $$2 }'`; do \
		submodules="$$submodules -o -path $$sm"; \
	done; \
	find . -type d \( $${submodules:3} \) -prune -false -o \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) -print0 | xargs -0 clang-format --style=file -i --verbose

.PHONY: format

$(CL65):
$(CC65):
	$(warning ======== Making: $@)
ifdef $(USE_LOCAL_CC65)
	@echo "Using local installed CC65."
else
	git submodule init
	git submodule update
	(cd cc65 && make -j 8 )
endif

ascii8x8.bin: ascii00-ff.png pngprepare
	$(warning ======== Making: $@)
	./pngprepare charrom ascii00-ff.png ascii8x8.bin

asciih:	asciih.c
	$(warning ======== Making: $@)
	$(CC) -o asciih asciih.c
ascii.h:	asciih
	$(warning ======== Making: $@)
	./asciih

pngprepare:	pngprepare.c
	$(warning ======== Making: $@)
	$(CC) -I/usr/local/include -L/usr/local/lib -o pngprepare pngprepare.c -lpng

m65fdisk.prg:	$(ASSFILES) $(DATAFILES) $(CC65)
	$(warning ======== Making: $@)
	$(CL65) $(COPTS) $(LOPTS) -vm -m m65fdisk.map -o m65fdisk.prg $(ASSFILES)

m65fdisk:	$(HEADERS) Makefile fdisk.c fdisk_fat32.c fdisk_hal_unix.c fdisk_memory.c fdisk_screen.c
	$(warning ======== Making: $@)
	gcc -Wall -Wno-char-subscripts -o m65fdisk fdisk.c fdisk_fat32.c fdisk_hal_unix.c fdisk_memory.c fdisk_screen.c

clean:
	rm -f $(FILES) m65fdisk.map \
	pngprepare \
	*.o \
	fdisk*.s \
	ascii.h asciih \
	ascii8x8.bin

cleangen:
	rm ascii8x8.bin
