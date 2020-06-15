# mega65-fdisk
FDISK+Format Utility for MEGA65 (http://github.com/MEGA65)

## Prerequisites
* need something for ```pngprepare```, -> ```sudo apt install libpng12-dev```
* the 'cc65' toolchain is required, and by default this is taken care of in the Makefile.  
Alternatively, you can supply your pre-built ```cc65``` binaries (see below at "CI").

## Building
* ``make`` will build (including init/update/build of submodule ``./cc65``)

## Continuous Integration (CI)
For CI building, you may not want to build the cc65-submodule,
but rather use a locally installed binary instead.

In that case, build with:
```make USE_LOCAL_CC65=1```

