# Makefile: build libS52.so and others for various scenarios (openev plugin)
#
# SD AUG2004
# SD FEB2008   - clean up, fix some deps
# SD JUL2008   - move test stuff
# SD JUL2008   - add pkg-config
# SD JAN2009   - mod clean up
# SD JUN2009   - fix s52gv
# SD FEB2010   - fix s52gv2 (now default)
# SD 2011NOV08 - add s52eglarm (android)
# SD 2012NOV04 - add Parson lib (simple JSON parser) to handle SOCK stream
# SD 2012NOV05 - move sduc-git.txt note inside Makefile (bottom)
# SD 2013AUG31 - add s52gtk3egl
# SD 2014FEB11 - add s52eglw32, s52gtk2egl


##### TARGETS #########
#all: s52glx         # OGR & GLX & GL 1.1 & GLU
#all: s52eglx        # OGR & EGL & X11   (for testing EGL/GLES2 on X)
#all: s52eglarm      # OGR & EGL & ARM   (for testing EGL/GLES2 on ARM/Android)
#all: s52eglw32      # OGR & EGL & Win32 (for testing EGL/GLES2 on Win32)
#all: s52gv          # GV  (GTK)
#all: s52gv2         # GV2 (GTK2)
all: s52gtk2        # OGR & GTK2 & GL 1.5 (VBO)
#all: s52gtk2gl2     # OGR & GTK2 & GL 2.x
#all: s52gtk2p       # profiling
#all: s52gtk2gps     # build s52gtk2 for testing with live data comming from GPSD
#all: s52gtk2egl     # GTK2 & EGL
#all: s52gtk3egl     # GTK3 & EGL
#all: s52qt4         # OGR & Qt4 (build s52gtk2 to run on Qt4)
#all: s52win32       # build libS52.dll on GL1 to run on wine/win32 (MinGW)
#all: s52clutter     # use COGL for rendering text
#all: s52clutter.js  # use COGL for rendering text and Javascript


SHELL = /bin/sh

.PHONY: test/* clean distclean LIBS52VERS


DBG0   = -O0 -g
DBG1   = -O0 -g1 -Wall -Wpedantic -Wextra
DBG2   = -O0 -g2 -Wall -Wpedantic -Wextra
DBG3   = -O0 -g3 -Wall -Wpedantic -Wextra -ggdb3 -fstack-protector-all -Wstrict-aliasing -Wstrict-overflow -Wno-uninitialized
DBGOFF = -DG_DISABLE_ASSERT
DBG    = $(DBG3)


# GCC 4.9: -fstack-protector-strong

# CLang:
#    no -rdynamic
#    -fsanitize=safestack

# from clutter
# Compiler flags: -Werror -Wall -Wshadow -Wcast-align -Wno-uninitialized -Wempty-body -Wformat-security -Winit-self

# from /.
#MALLOC_CHECK_=3, gcc's _FORTIFY_SOURCE=2 define, gcc's -fmudflap flag, gcc's -Wall -Wextra and -pedantic flags;

# GO -fdump-go-spec=S52.go to generate symbol for GO

# -Weffc++
# Causes GCC to check for the 50 specific C++ suggestions in Scott Meyers famous book 'Effective C++'

# Qt blog
# CFLAGS = -fsanitize=address -fno-omit-frame-pointer
# LFLAGS = -fsanitize=address

# TCC
#CC   = tcc -fPIC
#CXX  = tcc -fPIC -fmudflap

# GCC
#CC   = gcc -std=c99 -fPIC -D_POSIX_C_SOURCE=199309L # to get siginfo_t
CC   = gcc -std=c99 -fPIC -D_POSIX_C_SOURCE=200809L # 199309L to get siginfo_t

#CC   = gcc -std=c99 -fPIC -D_POSIX_C_SOURCE=200112L
#CC   = gcc -std=c99 -fPIC -DMALLOC_CHECK_=3 -D_FORTIFY_SOURCE=2
#CC   = gcc -std=gnu99 -fPIC -DMALLOC_CHECK_=3 -D_FORTIFY_SOURCE=2 # need gnu99 to get M_PI and sigtrap()
# test - compile C code as C++
#CC   = g++ -fPIC -O0 -g -Wall
CXX  = g++ -fPIC

# CLANG
#CC   = clang   -fPIC -O0 -g -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=199309L
#CXX  = clang++ -fPIC -O0 -g -Wall -Wextra -pedantic

# FIXME: check this
# LLVM-AddressSanitizer: http://clang.llvm.org/docs/AddressSanitizer.html
# Compile
# clang -O1 -g -fsanitize=address -fno-omit-frame-pointer -c example_UseAfterFree.cc
# Link
# clang -g -fsanitize=address example_UseAfterFree.o


MINGW = /usr/bin/i586-mingw32msvc-
#MINGW = /usr/bin/i686-w64-mingw32-
s52win32 : CC    = $(MINGW)gcc -Wall -m32 -std=c99
s52win32 : CXX   = $(MINGW)g++ -Wall -m32
s52win32 : AR    = $(MINGW)ar
s52win32 : RANLIB= $(MINGW)ranlib
s52eglw32: CC    = $(MINGW)gcc -Wall -m32 -std=gnu99
s52eglw32: CXX   = $(MINGW)g++ -Wall -m32

#s52win32 : CXX  = $(CC)  # hack
s52gv    : CXX  = $(CC)  # hack
s52gv2   : CXX  = $(CC)  # hack

TAGS     = ctags


############### OBJS setup ##############################
#
#

SRCS_S52 = S52GL.c S52PL.c S52CS.c S57ogr.c S57data.c S52MP.c S52utils.c S52.c
OBJS_S52 = $(SRCS_S52:.c=.o) S52raz-3.2.rle.o

OBJS_GV  = gvS57layer.o S57gv.o

OBJS_FREETYPE_GL = ./lib/freetype-gl/vector.o ./lib/freetype-gl/texture-atlas.o ./lib/freetype-gl/texture-font.o

# Note: there is no GLU for ARM, so the tess code is pulled from COGL.
# (Quadric are also in GLU, but it is done by hand in S52GL.c to output float
# for VBO in GLES2 for circle / disk / arc.)
OBJS_TESS = ./lib/libtess/dict.o      ./lib/libtess/geom.o     \
            ./lib/libtess/mesh.o      ./lib/libtess/normal.o   \
            ./lib/libtess/priorityq.o ./lib/libtess/render.o   \
            ./lib/libtess/sweep.o     ./lib/libtess/tessmono.o \
            ./lib/libtess/tess.o

# handle JSON in WebSocket
OBJ_PARSON = ./lib/parson/parson.o

OPENEV_HOME  = `pwd -P`/../../openev-cvs
OPENEV2_HOME = `pwd -P`/../../../openev2/trunk/src/lib/gv
#GV2LIBS      = /usr/local/lib/python2.6/dist-packages/openev/_gv.so


############### CFLAGS setup ##############################
#
#

# NOTE: -malign-double: for 32bits system (useless on 64bits)

# NOTE: GV_USE_DOUBLE_PRECISION_COORD not needed directly for S52
#       but gvtype.h need it and it can be pulled in (somehow.)
# NOTE: 'gdal-config --cflags': need with mingw
# NOTE: -D_REENTRANT add this if threading
# NOTE: signal is handled by glib-2.0 as of gtk+-2.0
#
#
# *** experimental ***
#
# Text:
# -DS52_USE_COGL         - used to test text rendering from COGL
# -DS52_USE_FTGL         - text rendering
# -DS52_USE_GLC          - text rendering
# -DS52_USE_FREETYPE_GL  - text rendering need GL2 or GLES2
# -DS52_USE_TXT_SHADOW   - add 'shadow' to Text (work only with S52_USE_FREETYPE_GL)
#
# S52/S57:
# -DS52_USE_OGR_FILECOLLECTOR
#                        - compile with g++ to use gdal/ogr s57filecollector()
#                        - add 'extern "C"' to ogr/ogrsf_frmts/s57.h:40 S57FileCollector()  -or- compile S52 with g++
#                        - for Windows file path in CATALOG to work on unix apply patch in doc/s57filecollector.cpp.diff
# -DS52_USE_SUPP_LINE_OVERLAP
#                        - supress display of overlapping line (need OGR patch in doc/ogrfeature.cpp.diff)
#                        - work for LC() only (not LS())
#                        - see S52 manual p. 45 doc/pslb03_2.pdf
# -DS52_USE_C_AGGR_C_ASSO- return info C_AGGR C_ASSO on cursor pick (need OGR patch in doc/ogrfeature.cpp.diff)
# -DS52_USE_SYM_AISSEL01 - need symbol in test/plib-test-priv.rle
# -DS52_USE_WORLD        - need shapefile WORLD_SHP in S52.c:201 ("--0WORLD.shp")
# -DS52_USE_RADAR        - GL2 / radar mode: skip swapbuffer between DRAW & LAST cycle, skip read/write FB - set S52_MAR_DISP_RADAR_LAYER
# -DS52_USE_RASTER       - GL2 - bathy raster (GeoTIFF) - set S52_MAR_DISP_RADAR_LAYER
# -DS52_USE_AFGLOW       - experimental synthetic after glow
# -DS52_USE_SYM_VESSEL_DNGHL
#                        - vestat = 3, close quarter, show AIS in red (DNGHL)
#
# Debug:
# -DS52_DEBUG            - add more info for debugging libS52 (ex _checkError() in S52GL.c)
# -DS52_USE_LOGFILE      - log every S52_* in tmp file
# -DS52_USE_BACKTRACE    - debug
# -DG_DISABLE_ASSERT     - glib - disable g_assert()
#
# Network:
# -DS52_USE_DBUS         - mimic S52.h
# -DS52_USE_SOCK         - same as DBus - socket & WebSocket - need ./lib/parson
# -DS52_USE_PIPE         - same as DBus, in a day
# -DS52_USE_GOBJECT      - make S52objH an int64 for gjs (Javascript compiler)
#
# OpenGL:
# -DS52_USE_MESA3D       - Mesa drive specific code
# -DS52_USE_EGL          - EGL callback from libS52
# GL FIX FUNC:
# -DS52_USE_GL1          - GL1.x
# -DS52_USE_GLSC1        - GLSC1.x - Safety Critical 1.0 (subset of GL1.3)
# -DS52_USE_OPENGL_VBO   - GL1.5 or greater. Vertex Buffer Object used also in GL2+
# GL GLSL:
# -DS52_USE_GL2          - GL2.x
# -DS52_USE_GLES2        - GLES2.x
# -DS52_USE_GLSC2        - GLSC2.x - Safety Critical 2.0 (subset of GLES2)
# -DS52_USE_GL3          - GL3.x
# -DS52_USE_GLES3        - GLES3.x / GLSL ES 3.0

#
# ARM:
# -DS52_USE_ANDROID      - build for Android/ARM
# -DS52_USE_TEGRA2       - must be in sync with Android.mk (Xoom)
# -DS52_USE_ADRENO       - must be in sync with Android.mk (Nexus 7 (2013))
#
# MinGW:
# -D_MINGW
#
# PROJ4
# -DS52_USE_PROJ         - Mercator Projection, used by all but s52gv and s52gv2


# default CFLAGS for default target (s52gtk2)
CFLAGS = `pkg-config  --cflags glib-2.0 lcms gl ftgl`  \
         `gdal-config --cflags`                        \
         -I./lib/parson                                \
         -DS52_USE_FTGL                                \
         -DS52_USE_GL1                                 \
         -DS52_USE_OPENGL_VBO                          \
         -DS52_USE_SOCK                                \
         -DS52_USE_PROJ                                \
         -DS52_DEBUG $(DBG)

s52gtk2gl2 : CFLAGS =                                  \
         `pkg-config  --cflags glib-2.0 lcms gl freetype2`  \
         `gdal-config --cflags`                        \
         -I./lib/freetype-gl                           \
         -I./lib/libtess                               \
         -I./lib/parson                                \
         -DS52_USE_PROJ                                \
         -DS52_USE_OPENGL_VBO                          \
         -DS52_USE_GL2                                 \
         -DS52_USE_FREETYPE_GL                         \
         -DS52_USE_SUPP_LINE_OVERLAP                   \
         -DS52_USE_BACKTRACE                           \
         -DS52_USE_TXT_SHADOW                          \
         -DS52_DEBUG $(DBG)

# FIXME: clutter use cogl not ftgl
s52clutter s52clutter.js : CFLAGS =                         \
         `pkg-config  --cflags glib-2.0 lcms glu gl ftgl`   \
         `gdal-config --cflags`                             \
         -I/home/sduclos/dev/gis/gdal/gdal/frmts/iso8211/   \
         -DS52_USE_PROJ                                     \
         -DS52_USE_OGR_FILECOLLECTOR                        \
         -DS52_USE_BACKTRACE                                \
         -DS52_USE_GOBJECT                                  \
         -DS52_USE_GL1                                      \
         -DS52_USE_OPENGL_VBO                               \
         -DS52_USE_FTGL                                     \
         -DS52_DEBUG $(DBG)

s52gtk2p : CFLAGS += -pg

s52glx : CFLAGS = `pkg-config  --cflags glib-2.0 lcms glu gl ftgl` \
                  `gdal-config --cflags`          \
                  -DS52_USE_PROJ                  \
                  -DS52_USE_GL1                   \
                  -DS52_USE_FTGL                  \
                  -DS52_DEBUG $(DBG)

#                  -DS52_USE_SUPP_LINE_OVERLAP
s52eglx s52gtk2egl s52gtk3egl : CFLAGS =         \
                  `pkg-config  --cflags glib-2.0 gio-2.0 lcms glesv2 freetype2` \
                  `gdal-config --cflags`         \
                  -I./lib/freetype-gl            \
                  -I./lib/libtess                \
                  -I./lib/parson                 \
                  -DS52_USE_PROJ                 \
                  -DS52_USE_BACKTRACE            \
                  -DS52_USE_EGL                  \
                  -DS52_USE_OPENGL_VBO           \
                  -DS52_USE_GLES2                \
                  -DS52_USE_GLSC2                \
                  -DS52_USE_MESA3D               \
                  -DS52_USE_FREETYPE_GL          \
                  -DS52_USE_SOCK                 \
                  -DS52_USE_TXT_SHADOW           \
                  -DS52_USE_AFGLOW               \
                  -DS52_USE_BACKTRACE            \
                  -DS52_USE_SYM_VESSEL_DNGHL     \
                  -DS52_USE_RASTER               \
                  -DS52_DEBUG $(DBG)

# CFLAGS="-mthumb" CXXFLAGS="-mthumb" LIBS="-lstdc++" ./configure --host=arm-eabi \
# --without-grib --prefix=/home/sduclos/dev/prog/Android/dev/ --enable-shared=no --without-ld-shared
# using Android toolchain from NDK to cross compile for ARM (s52eglarm target)
s52eglarm : ARMTOOLCHAINROOT = /home/sduclos/dev/prog/Android/dev/android-19-toolchain
s52eglarm : ARMINCLUDE       = $(ARMTOOLCHAINROOT)/sysroot/usr/include
s52eglarm : ARMLIBS          = $(ARMTOOLCHAINROOT)/sysroot/usr/lib

# Android 4.4.2: -O2 -O1 crash activity android:name = ".s52ui"
# TEGRA: tadp: -fno-omit-frame-pointer -mno-thumb
s52eglarm : CC     = $(ARMTOOLCHAINROOT)/bin/arm-linux-androideabi-gcc -fPIC -mthumb -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -std=c99
s52eglarm : CXX    = $(ARMTOOLCHAINROOT)/bin/arm-linux-androideabi-g++ -fPIC -mthumb -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16
s52eglarm : AR     = $(ARMTOOLCHAINROOT)/bin/arm-linux-androideabi-ar
s52eglarm : RANLIB = $(ARMTOOLCHAINROOT)/bin/arm-linux-androideabi-ranlib

#                     -DS52_DEBUG $(DBG)
#                     -DG_DISABLE_ASSERT
#                     -DS52_USE_TEGRA2
s52eglarm : S52DROIDINC = /home/sduclos/S52/test/android/dist/sysroot/include
s52eglarm : S52DROIDLIB = /home/sduclos/S52/test/android/dist/sysroot/lib

              DEFS = -DS52_USE_PROJ                        \
                     -DS52_USE_EGL                         \
                     -DS52_USE_GLES2                       \
                     -DS52_USE_OPENGL_VBO                  \
                     -DS52_USE_FREETYPE_GL                 \
                     -DS52_USE_ANDROID                     \
                     -DS52_USE_ADRENO                      \
                     -DS52_USE_OGR_FILECOLLECTOR           \
                     -DS52_USE_SUPP_LINE_OVERLAP           \
                     -DS52_USE_SOCK                        \
                     -DS52_USE_TXT_SHADOW                  \
                     -DS52_USE_AFGLOW                      \
                     -DS52_USE_LOGFILE                     \
                     -DS52_DEBUG


s52eglarm : CFLAGS = -I$(S52DROIDINC)                      \
                     -I$(S52DROIDINC)/glib-2.0             \
                     -I$(S52DROIDINC)/glib-2.0/include     \
                     -I./lib/freetype-gl                   \
                     -I./lib/libtess                       \
                     -I./lib/parson                        \
                     $(DEFS)

# check this; gv use glib-1 S52 use glib-2
s52gv  : CFLAGS = `glib-config --cflags`                \
                  `gdal-config --cflags`                \
                  -DS52_USE_GV $(DBG)                   \
                  -DGV_USE_DOUBLE_PRECISION_COORD       \
                  `gtk-config --cflags` -I$(OPENEV_HOME)

s52gv2 : CFLAGS = `pkg-config  --cflags glib-2.0 lcms`  \
                  `gdal-config --cflags`                \
                  -DS52_USE_GV                          \
                  -DGV_USE_DOUBLE_PRECISION_COORD       \
                  -I$(OPENEV2_HOME)

s52gtk2gps:  CFLAGS = `pkg-config  --cflags glib-2.0 lcms ftgl dbus-1 dbus-glib-1`   \
                      `gdal-config --cflags`            \
                      -DS52_USE_FTGL                    \
                      -DS52_USE_PROJ                    \
                      -DS52_USE_OGR_FILECOLLECTOR       \
                      -DS52_USE_SUPP_LINE_OVERLAP       \
                      -DS52_USE_DBUS                    \
                      -DS52_USE_GOBJECT                 \
                      -DS52_USE_BACKTRACE               \
                      -DS52_DEBUG $(DBG)

s52win32 : GDALPATH = ../../../gdal/gdal-1.7.2-mingw/

#                      -DS52_DEBUG $(DBG2)                    \

s52win32 : CFLAGS   = -mms-bitfields                         \
                      -I../../mingw/gtk+-2.16/gtk+-bundle_2.16.6-20100912_win32/include/glib-2.0     \
                      -I../../mingw/gtk+-2.16/gtk+-bundle_2.16.6-20100912_win32/lib/glib-2.0/include \
                      -I../../mingw/include                  \
                      -I$(GDALPATH)ogr                       \
                      -I$(GDALPATH)port                      \
                      -I$(GDALPATH)gcore                     \
                      -I$(GDALPATH)frmts/iso8211/            \
                      -DS52_USE_FTGL                         \
                      -DS52_USE_GL1                          \
                      -DS52_USE_PROJ                         \
                      -DS52_USE_OGR_FILECOLLECTOR            \
                      -DS52_USE_LOGFILE                      \
                      -DG_DISABLE_ASSERT                     \
                      -D_MINGW

s52eglw32 : GDALPATH = ../../../gdal/gdal-1.7.2-mingw
s52eglw32 : CFLAGS   = -mms-bitfields                         \
                      -I../../mingw/gtk+-2.16/gtk+-bundle_2.16.6-20100912_win32/include/glib-2.0     \
                      -I../../mingw/gtk+-2.16/gtk+-bundle_2.16.6-20100912_win32/lib/glib-2.0/include \
                      -I../../mingw/include                   \
                      -I$(GDALPATH)/ogr                       \
                      -I$(GDALPATH)/port                      \
                      -I$(GDALPATH)/gcore                     \
                      -I$(GDALPATH)/frmts/iso8211/            \
                      -I$(GDALPATH)/alg                       \
                      -I./lib/freetype-gl            \
                      -I./lib/libtess                \
                      -I./lib/parson                 \
                      -DS52_USE_PROJ                 \
                      -DS52_USE_OPENGL_VBO           \
                      -DS52_USE_EGL                  \
                      -DS52_USE_GL2                  \
                      -DS52_USE_GLES2                \
                      -DS52_USE_FREETYPE_GL          \
                      -DS52_USE_OGR_FILECOLLECTOR    \
                      -DS52_USE_SYM_VESSEL_DNGHL     \
                      -DS52_USE_LOGFILE              \
                      -DG_DISABLE_ASSERT             \
                      -D_MINGW                       \
                      -DS52_DEBUG $(DBG2)


############### LIBS setup ##############################
#
#

# default LIBS for default target
LIBS = `pkg-config  --libs glib-2.0 lcms gl ftgl` \
       `gdal-config --libs` -lproj

s52gtk2gl2 : LIBS = `pkg-config  --libs glib-2.0 lcms gl freetype2` \
                    `gdal-config --libs` -lproj

s52clutter, s52clutter.js : LIBS = `pkg-config  --libs glib-2.0 lcms glu gl` \
                                   `gdal-config --libs` -lproj

s52glx : LIBS = `pkg-config  --libs glib-2.0 lcms glu gl ftgl` \
                `gdal-config --libs` -lproj

s52eglx s52gtk2egl s52gtk3egl: LIBS = `pkg-config  --libs glib-2.0 gio-2.0 lcms glesv2 freetype2` \
                                      `gdal-config --libs` -lproj



# check this; gv use glib-1 S52 use glib-2
s52gv  : LIBS = `glib-config --libs`               \
                `gdal-config --libs`               \
                `gtk-config  --libs`               \
                -llcms

s52gv2 : LIBS = `pkg-config  --libs glib-2.0 lcms` \
                `gdal-config --libs`               \
                 -lGL -lGLU  $(GV2LIBS)


############### BUILD ##############################
#
#

s52glx        : libS52.so    test/s52glx
s52eglx       : libS52egl.so test/s52eglx
s52gtk2egl    : libS52egl.so test/s52gtk2egl
s52gtk3egl    : libS52egl.so test/s52gtk3egl
s52eglarm     : $(S52DROIDLIB)/libS52.a     test/s52eglarm
s52gv         : libS52gv.so  test/s52gv
s52gv2        : libS52gv.so  test/s52gv2
s52gtk2       : libS52.so    test/s52gtk2
s52gtk2gl2    : libS52gl2.so test/s52gtk2gl2
s52gtk2p      : $(OBJS_S52)  test/s52gtk2p  # static link
s52clutter    : libS52.so    test/s52clutter
s52clutter.js : libS52.so    test/s52clutter.js
s52qt4        : libS52.so    test/s52qt4
s52win32      : libS52.dll   test/s52win32 s52win32fini
#s52win32      : libS52.dll.a   test/s52win32
#s52win32gps  : libS52.dll   test/s52win32gps s52win32fini
s52eglw32     : libS52.dll   test/s52eglw32
s52gtk2gps    : libS52.so    test/s52gtk2gps


S52raz-3.2.rle.o: S52raz.s
	$(CC) -c S52raz.s -o $@

#%.o: %.c %.h S52.h
%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

#S52GL.o: S52GL.c _GL1.i _GL2.i _GLU.i S52.h
S52GL.o: S52GL.c _GL1.i _GL2.i _GLU.i *.h
	$(CC) $(CFLAGS) -c $< -o $@

#S52.o: S52.c _S52.i S52.h
S52.o: S52.c _S52.i *.h
	$(CC) $(CFLAGS) -c $< -o $@

./lib/libtess/%.o: ./lib/libtess/%.c
	$(CC) $(CFLAGS) -c $< -o $@

./lib/freetype-gl/%.o: ./lib/freetype-gl/%.c
	$(CC) $(CFLAGS) -c $< -o $@

./lib/parson/%.o: ./lib/parson/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(S52DROIDLIB)/libS52.a: $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON) tags
	$(AR) rc   $(S52DROIDLIB)/libS52.a $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON)
	$(RANLIB)  $(S52DROIDLIB)/libS52.a

libS52.so: $(OBJS_S52) $(OBJ_PARSON) tags
	$(CC) -shared $(OBJS_S52) $(OBJ_PARSON) $(LIBS) -o $@

libS52gl2.so:  $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON) tags
	$(CC) -shared  $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON) $(LIBS) -o $@
	-ln -sf libS52gl2.so libS52.so

libS52egl.so: $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON) tags
	$(CC) -shared $(OBJS_S52) $(OBJS_TESS) $(OBJS_FREETYPE_GL) $(OBJ_PARSON) $(LIBS) -o $@
	-ln -sf libS52egl.so libS52.so

libS52gv.so: $(OBJS_S52) $(OBJS_GV)
	$(CC) -shared $(OBJS_S52) $(OBJS_GV) $(LIBS) -o libS52.so

s52win32 s52eglw32: LIBWIN32PATH = ../../mingw
s52win32 s52eglw32: GTKPATH = $(HOME)/dev/gis/openev-cvs/mingw/gtk+-2.16/gtk+-bundle_2.16.6-20100912_win32/bin
#s52win32 s52eglw32: GTKPATH = $(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/lib
s52win32  : LIBS = -L$(GTKPATH) -lglib-2.0-0 -lfreetype6  \
                   $(LIBWIN32PATH)/libproj.a              \
                   $(LIBWIN32PATH)/libftgl.a              \
                   $(LIBWIN32PATH)/libgdal-1.dll          \
                   $(LIBWIN32PATH)/liblcms-1.dll          \
                   -lopengl32 -lglu32                     \

s52eglw32 : LIBS = $(LIBWIN32PATH)/libproj.a              \
                   $(LIBWIN32PATH)/libgdal-1.dll          \
                   $(LIBWIN32PATH)/liblcms-1.dll          \
                   $(LIBWIN32PATH)/libEGL.lib             \
                   $(LIBWIN32PATH)/libGLESv2.lib          \
                   -L$(GTKPATH) -lglib-2.0-0 -lfreetype6

libS52.dll: $(OBJS_S52)
	$(MINGW)objcopy --redefine-sym S52raz=_S52raz --redefine-sym S52razLen=_S52razLen S52raz-3.2.rle.o S52raz-3.2.rle.o
	$(CXX) -O0 -g -Wall -mms-bitfields -shared -Wl,--add-stdcall-alias $(OBJS_S52) $(LIBS) -o $@

libS52.dll.a: $(OBJS_S52)
	$(AR) rc   libS52.dll.a $(OBJS_S52)
	$(RANLIB)  libS52.dll.a

libS52egl.dll: $(OBJS_S52) $(OBJS_FREETYPE_GL) $(OBJS_TESS) $(OBJ_PARSON)
	$(MINGW)objcopy --redefine-sym S52raz=_S52raz                         \
	--redefine-sym S52razLen=_S52razLen S52raz-3.2.rle.o S52raz-3.2.rle.o
	 $(MINGW)g++ -g -mms-bitfields -O0 -Wall  -shared -Wl,--add-stdcall-alias \
	 $(OBJS_S52) $(OBJS_FREETYPE_GL) $(OBJS_TESS) $(OBJ_PARSON) $(LIBS) -o $@
	-rm -f libS52.dll
	-ln -s libS52egl.dll libS52.dll



############### Test ##############################
#
#

test/s52glx:
	(cd test; make s52glx)

test/s52eglx:
	(cd test; make s52eglx)

test/s52gtk2egl:
	(cd test; make s52gtk2egl)

test/s52gtk3egl:
	(cd test; make s52gtk3egl)

# FIXME: remove DEPRECATED step to cd in test
#	(cd test; make s52eglarm; cd android; make)

test/s52eglarm:
	(cd test/android; make)

test/s52gv:
	(cd test; make s52gv)

test/s52gv2:
	(cd test; make s52gv2)

test/s52gtk2:
	(cd test; make s52gtk2)

test/s52gtk2gl2:
	(cd test; make s52gtk2gl2)

test/s52gtk2gps:
	(cd test; make s52gtk2gps)

test/s52clutter:
	 (cd test; make s52clutter)

test/s52clutter.js:
	 (cd test; make s52clutter.js)

test/s52gtk2p:
	(cd test; make s52gtk2p)

test/s52qt4:
	(cd test; make s52qt4)

test/s52win32:
	(cd test; make s52win32)

test/s52win32gps:
	(cd test; make s52win32gps)

test/s52eglw32:
	(cd test; make s52eglw32)


s52win32fini:
	$(MINGW)strip libS52.dll

# NOTE: libS52 need to know where is _gv python wrapper is
# when using openev.py because we have du jump back to it
# to get to the handle of gvProperty of layers
#../../openev/pymod/_gvmodule.so


############### Utils ##############################
#
#

clean:
	rm -f *.o tags *~ *.so *.dll err.txt ./lib/libtess/*.o ./lib/freetype-gl/*.o ./lib/parson/*.o
	(cd test; make clean)

distclean: clean
	rm -f test/android/dist/sysroot/lib/libS52.a
	(cd test; make distclean)

install:
	install libS52.so `gdal-config --prefix`/lib

uninstall:
	rm -f `gdal-config --prefix`/lib/libS52.so

tar: backup
backup: distclean
	(cd ..; tar zcvf S52.tgz S52)

tags:
	-$(TAGS) *.c *.h *.i

err.txt: *.c *.h
	cppcheck --enable=all $(DEFS) *.c 2> err.txt

# get version - "libS52-2014DEC27-1.157" --> 2014DEC27-1.145
LIBS52VERS = $(shell grep libS52- S52utils.c | sed 's/.*"libS52-\(.*\)"/\1/' )

S52-$(LIBS52VERS).gir: S52.h
	g-ir-scanner --verbose --namespace=S52 --nsversion=$(LIBS52VERS) --library=S52 --no-libtool S52.h -o $@
	sudo cp $@ /usr/share/gir-1.0/

S52-$(LIBS52VERS).typelib: S52-$(LIBS52VERS).gir
	g-ir-compiler S52-$(LIBS52VERS).gir -o $@
	sudo cp $@ /usr/lib/girepository-1.0/

# https://git.gnome.org/browse/introspection-doc-generator
doc: S52-$(LIBS52VERS).typelib
	(cd /home/sduclos/dev/prog/doc-generator/introspection-doc-generator/; seed docs.js ../tmp S52;)
	cp /home/sduclos/dev/prog/doc-generator/tmp/seed/* doc/tmp



############### Notes ##############################
#
#

# git:
#   git init (one time)
#   git add <file>  (ex README)
#   git commit -m "new"
#   git remote add origin https://github.com/sduclos/S52.git (one time !)
#   git push -u origin master (sync local .git with github !)

# --- do once ---
# 0 - init
# Assigns the original repo to a 'remote' called "upstream"
# $ git remote add upstream https://github.com/rikulo/rikulo

# --- normal flow ---
# 1 - sync .git with official git
# Fetches any new changes from the original repo
# Pulls in changes not present in your local repository,
# without modifying your files
# SD this will sync LOCAL .git with official Rikulo
# $ git fetch upstream

# 2 - sync LOCAL files with .git
# Merges any changes fetched into your working files
#
# $ git merge upstream/master


# 3 - sync .git with sduclos git on GitHub
# Pushes commits (.git) to your remote repo stored on GitHub
# $ git push origin master
