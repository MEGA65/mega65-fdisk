# Operating System detection and conditional compile options

ifeq ($(OS),Windows_NT)
    OSYSTEM := Windows
else
    OSYSTEM := $(shell sh -c 'uname 2>/dev/null || echo Unknown')
endif

ifeq ($(OSYSTEM),Windows) # MS Windows
    GCOPT +=
endif

ifeq ($(OSYSTEM),Darwin) # Apple macOS
    GCOPT += -I/opt/homebrew/include -L/opt/homebrew/lib -Wno-unknown-pragmas
endif

ifeq ($(OSYSTEM),Linux) # Linux
    GCOPT += -I/usr/local/include -L/usr/local/lib
endif

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

GTESTDIR=gtest
GTESTBINDIR=$(GTESTDIR)/bin
# For now, disable g++ compile warnings on tests (there's so many :))
GTESTOPTS=-w -DTESTING

GTESTFILES=$(GTESTBINDIR)/m65fdisk.test
GTESTFILESEXE=$(GTESTBINDIR)/m65fdisk.test.exe

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
	$(CC65) $(COPTS) --add-source -o $@ $<

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
	$(CC) $(GCOPT) -o pngprepare pngprepare.c -lpng

m65fdisk.prg:	$(ASSFILES) $(DATAFILES) $(CC65)
	$(warning ======== Making: $@)
	$(CL65) $(COPTS) $(LOPTS) -vm -m m65fdisk.map --listing m65fdisk.list -Ln m65fdisk.label -o m65fdisk.prg $(ASSFILES)

UNIX_M65FDISK_SRC = fdisk.c \
							 			fdisk_fat32.c \
							 			fdisk_hal_unix.c \
							 			fdisk_memory.c \
							 			fdisk_screen.c

m65fdisk:	$(HEADERS) Makefile $(UNIX_M65FDISK_SRC)
	$(warning ======== Making: $@)
	gcc -Wall -Wno-char-subscripts -g -O0 -o m65fdisk $(UNIX_M65FDISK_SRC)

define LINUX_AND_MINGW_GTEST_TARGETS
$(1): $(2)
	$$(CXX) $$(COPT) $$(GTESTOPTS) -Iinclude -o $$@ $$(filter %.c %.cpp,$$^) -lgtest_main -lgtest -lpthread $(3)

$(1).exe: $(2)
	$$(CXX) $$(WINCOPT) $$(GTESTOPTS) -Iinclude $(LIBUSBINC) -o $$@ $$(filter %.c %.cpp,$$^) -lgtest_main -lgtest -lpthread $(3)
endef

# Gives two targets of:
# - gtest/bin/m65fdisk.test
# - gtest/bin/m65fdisk.test.exe
$(eval $(call LINUX_AND_MINGW_GTEST_TARGETS, $(GTESTBINDIR)/m65fdisk.test, $(GTESTDIR)/m65fdisk_test.cpp $(UNIX_M65FDISK_SRC) Makefile, -fpermissive -std=gnu++14 -g -O0))

# testing
test: $(GTESTFILES)
	@for test in $+; do \
		name=$${test%%.test}; \
		name=$${name##*/}; \
		echo ""; \
		echo "TESTING: $$name..."; \
		echo "======================"; \
		$$test; \
	done

test.exe: $(GTESTFILESEXE)
	@for test in $+; do \
		name=$${test%%.test.exe}; \
		name=$${name##*/}; \
		path=$${test%/*}; \
		echo ""; \
		echo "TESTING: $$name..."; \
		echo "======================"; \
		cd $$path; ./$${test##*/}; \
	done

clean:
	rm -f $(FILES) m65fdisk.map \
	pngprepare \
	*.o \
	fdisk*.s \
	ascii.h asciih \
	ascii8x8.bin \
	*.prg \
	gtest/bin/m65fdisk.test

cleangen:
	rm ascii8x8.bin
