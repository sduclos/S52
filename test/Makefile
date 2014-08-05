# Makefile: test/dev of libS52.so
#
# SD JUN2008
# SD NOV2009 -add s52glx minimal
# SD 2011MAR -add s52clutter.js
# SD 2011NOV -add s52eglx s52eglarm  - EGL/GLES2
# SD 2013SEP -add s52gtk3egl         - EGL/GLES2
# SD 2014APR -add s52gtk2gl2         - GL2

#all: s52glx        # GLX & OGR
#all: s52eglx       # EGL & GLX & OGR
#DEPRECATED: all: s52eglarm     # EGL & GLES2 & ARM/Android & OGR - S52_USE_ADRENO for Nexus / S52_USE_TEGRA2 for Xoom
#all: s52gv         # GV  (OpenEV  / GTK )
#all: s52gv2        # GV2 (OpenEV2 / GTK2)  broken
#all: s52gtk2       # GTK2 & OGR
#all: s52gtk2p      # GTK2 & OGR (profiler)
#all: s52gtk2egl    # GTK2 & EGL
all: s52gtk3egl     # GTK3 & EGL
#all: s52win32      # same as s52gtk2 but run on wine
#all: s52eglw32     # same as s52gtk2egl but run on wine
#all: s52clutter.js # same as s52gtk2 but use Clutter & Javascript (experimental)
                    # also used to build S52/doc/S52-0.1.gir

#CC   = tcc -fPIC
CC   = gcc -O0 -g -Wall -Wextra -pedantic -std=gnu99
#CC   = g++ -fPIC -O0 -g -Wall
CXX   = g++ -O0 -g -Wall

MINGW = /usr/bin/i586-mingw32msvc-
#MINGW = /usr/bin/i686-w64-mingw32-
#s52win32 : CC = $(MINGW)gcc -std=c99 -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : CC = $(MINGW)gcc -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
#s52win32 : CC = $(MINGW)g++ -std=c99 -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : CXX = $(MINGW)g++ -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : GTKPATH   = $(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/bin
s52win32 : GTKGLPATH = $(HOME)/dev/gis/openev-cvs/mingw/gtkglext-win32-1.0.6/bin

s52eglw32 : CC  = $(MINGW)gcc -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW -m32
s52eglw32 : CXX = $(MINGW)g++ -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW -m32
s52eglw32 : GTKPATH = $(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32

# NOTE: -malign-double: for 32bits system --useless on 64its
# NOTE: GV_USE_DOUBLE_PRECISION_COORD not needed directly for S52
# but gvtype.h need it and it can be pulled in (somehow.)


############### CFLAGS setup ##############################
#
#

# -DUSE_FAKE_AIS      - debug - programmaticaly faked AIS data
# -DUSE_AIS           - call s52ais.c
# -DSET_SCREEN_SIZE   - set screen size be hand (hard coded as dual-screen seem to confused the setup)

# -DS52_USE_GLIB2     -
# -DS52_USE_LOG       - log every S52_* in tmp file
# -DS52_USE_MESA3D    - set ENV for Mesa3D MSAA and vblank_mode
# -DS52_USE_GLES2     - GLES2
# -DS52_USE_GLES3     - GLES3.x / GLSL ES 3.0
# -DS52_USE_ANDROID   - then set TREGRA2 or ADRENO
# -DS52_USE_TEGRA2    - Xoom
# -DS52_USE_ADRENO    - Nexus 7 (2013)
# -DS52_USE_AFGLOW    - need symbole in PLAUX_00.DAI
# -DS52_USE_WORLD     - load world shapefile
# -DS52_USE_SOCK      - in s52ais.c when build with S52AIS_STANDALONE use socket to call libS52
# -DS52_USE_RADAR     - when in RADAR mode

CFLAGS = -I.. `pkg-config --cflags gtk+-2.0 gtkglext-1.0 gl` -DS52_USE_GLIB2

s52gtk2gl2 : CFLAGS = -I.. `pkg-config --cflags gtk+-2.0 gtkglext-1.0` \
                      -DS52_USE_GLIB2                                  \
                      -DS52_USE_GL2                                    \
                      -DS52_USE_MESA3D

s52gtk2gps: CFLAGS += -DS52GTK2_USE_GPSD -DS52_USE_DBUS `pkg-config --cflags --libs glib-2.0 libgps dbus-1 dbus-glib-1`

s52glx:     CFLAGS = -I.. `pkg-config --cflags gl`

s52eglx:    CFLAGS = -I.. -I. `pkg-config --cflags egl glib-2.0 gthread-2.0 gobject-2.0 libgps`   \
                     -DS52_USE_GLES2 -DS52_USE_EGL -DS52_USE_AFGLOW -DUSE_AIS -DS52_USE_MESA3D -DSET_SCREEN_SIZE

# same as s52eglx, but "gtk+-2.0" is added - no AIS
s52gtk2egl: CFLAGS = -I.. -I. `pkg-config --cflags egl glib-2.0 gthread-2.0 gobject-2.0 gtk+-2.0` \
                     -DS52_USE_EGL -DS52_USE_GLES2
# same as s52eglx, but "gtk+-3.0" is added
s52gtk3egl: CFLAGS = -I.. -I. `pkg-config --cflags egl glib-2.0 gthread-2.0 gobject-2.0 gtk+-3.0` \
                     -DS52_USE_SOCK -DS52_USE_AFGLOW -DS52_USE_EGL -DS52_USE_GLES2

s52gv:      CFLAGS = `gtk-config --cflags` `glib-config --cflags`     \
                      -I$(OPENEV_HOME) -DGV_USE_DOUBLE_PRECISION_COORD \
                      -I.. -DS52_USE_GV

s52gv2:     CFLAGS = `pkg-config --cflags gtk+-2.0 gtkglext-1.0`       \
                      -D_REENTRANT  -DGV_USE_DOUBLE_PRECISION_COORD    \
                      -I$(OPENEV2HOME) -I..  -DS52_USE_GV


############### LIBS setup ##############################
#
# search for libS52.so (-rpath)
S52_LIBS  = -Wl,-rpath .. -L.. -lS52

# NOTE: need full path in order to find libgv.so at run-time (-rpath)
OPENEV_HOME = `pwd -P`/../../../openev-cvs
OPENEV2HOME = `pwd -P`/../../../../openev2/trunk/src/lib/gv

GV_LIBS     = -Wl,-rpath $(OPENEV_HOME) -L$(OPENEV_HOME) -lgv
GV2LIBS     = -Wl,-rpath $(OPENEV2LIBS) $(OPENEV2LIBS) -lpython2.6

GLIB_LIBS   = `glib-config --libs`
GLIB2LIBS   = `pkg-config  --libs glib-2.0`

GTK_LIBS    = `gtk-config  --libs` -lgtkgl
GTK2LIBS    = `pkg-config  --libs gtk+-2.0 gtkglext-1.0 gthread-2.0 gl`

# GL2 can't use GL1 stuff like glBegin/glEnd
# FIXME: use VBO instead
s52gtk2gl2 : GTK2LIBS = `pkg-config --libs gtk+-2.0 gtkglext-1.0 gthread-2.0`

OGR_LIBS  = `gdal-config --libs`

# Ubuntu 13+ need -lX11
s52eglx   : LIBS    = `pkg-config --libs egl x11 glib-2.0 gio-2.0 gthread-2.0 gobject-2.0 libgps` $(S52_LIBS) -lm

# same as s52eglx, add "gtk+-2.0"
s52gtk2egl: LIBS    = `pkg-config --libs egl glib-2.0 gio-2.0 gthread-2.0 gobject-2.0 gtk+-2.0` $(S52_LIBS)

# same as s52eglx, add "gtk+-3.0"
s52gtk3egl: LIBS    = `pkg-config --libs egl glib-2.0 gio-2.0 gthread-2.0 gobject-2.0 gtk+-3.0` $(S52_LIBS)


############### build ##############################
#
#
s52glx: s52glx.c
	$(CC) $(CFLAGS) s52glx.c $(S52_LIBS) -lGL -lX11 -o $@

#s52eglx: s52egl.c
#	$(CC) $(CFLAGS) s52egl.c $(LIBS) -o $@
#	$(CC) $(CFLAGS) -DS52AIS_STANDALONE s52ais.c $(LIBS) -o s52ais

#FIXME: with -DUSE_FAKE_AIS no need to build AIS
s52eglx: s52egl.c s52ais.c
	$(CC) $(CFLAGS) s52egl.c s52ais.c $(LIBS) -o $@

s52gtk2egl: s52gtk2egl.c
	$(CC) $(CFLAGS) s52gtk2egl.c $(LIBS) -o $@

s52gtk3egl: s52gtk3egl.c s52ais.c
	$(CC) $(CFLAGS) s52gtk3egl.c $(LIBS) -o $@
	$(CC) $(CFLAGS) -DS52AIS_STANDALONE s52ais.c $(LIBS) `pkg-config --cflags --libs libgps` -o s52ais

s52gv:  LIBS = $(S52_LIBS) $(GTK_LIBS) $(GV_LIBS) $(OGR_LIBS)
s52gv:  s52gv.c
	(cd $(OPENEV_HOME); make gvtest; $(CC) -shared *.o -o libgv.so)
	$(CC) $(CFLAGS) s52gv.c $(LIBS) -o $@

s52gv2: LIBS = $(S52_LIBS) $(GTK2LIBS) $(GV2LIBS) $(OGR_LIBS)
s52gv2: s52gv.c
	$(CC) $(CFLAGS) s52gv.c $(LIBS) -o $@

testmain:
	$(CC) $(LDFLAGS) testmain.o $(LIBS)  -o $@

s52gtk2 s52gtk2gl2: s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) s52gtk2.c $(S52_LIBS) $(GTK2LIBS) -o $@

#libs52gps.so: s52gps.h s52gps.c
#	$(CC) $(CFLAG) -fPIC -shared -I.. s52gps.c `pkg-config --cflags --libs glib-2.0 libgps dbus-1 dbus-glib-1` -o $@
libs52gps.so: s52gps.h s52gps.c
	$(CC) $(CFLAGS) -fPIC -shared -I.. s52gps.c -o $@

s52gtk2gps: LIBS = $(S52_LIBS) $(GTK2LIBS) $(OGR_LIBS) -Wl,-rpath . -L. -ls52gps
s52gtk2gps: libs52gps.so s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) s52gtk2.c $(LIBS) -o $@

# -unix: deprecated
s52qt4: s52qt4.pro s52qt4.cpp ../S52.h
	qmake -o Makefile.qt4 s52qt4.pro
	make -f Makefile.qt4

s52gtk2p: LIBS = $(GTK2LIBS) $(OGR_LIBS)
s52gtk2p: s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) -pg s52gtk2.c ../*.o $(LIBS) -lproj -llcms -o $@

#$(CC) $(CFLAGS) -nodefaultlibs -nostartfiles s52gtk2.c ../*.o $(LIBS) -lproj -llcms -o $@


############### DEPRECATED build: Android (Android.mk do the build now) ##############################

android/dist/sdcard/s52droid/UI.xml: android/dist/sdcard/s52droid/UI.tmpl.xml
	awk 'BEGIN { count_text    = 0;                                             \
                 count_border  = 0;                                             \
                 start_comment = 0;                                             \
               }                                                                \
               /^$$/  { getline;           }                                    \
               /<!--/ { start_comment = 1; }                                    \
               {                                                                \
                if (start_comment == 1) {                                       \
                  if ($$0 ~ /-->/) { start_comment = 0; }                       \
                } else {                                                        \
                  if ($$0 ~ /@\+id\/s52_text_/) {                               \
                    str = "s52_text_" count_text++;                             \
                    sub(/s52_text_/, str, $$0);                                 \
                  }                                                             \
                  if ($$0 ~ /@id\/s52_text_/) {                                 \
                    str = "s52_text_" count_text-2;                             \
                    sub(/s52_text_/, str, $$0);                                 \
                  }                                                             \
                  if ($$0 ~ /s52_border_/) {                                    \
                    sub(/s52_border_/, "s52_border_" count_border++, $$0);      \
                  }                                                             \
                  print $$0;                                                    \
                }                                                               \
               }                                                                \
    END { print("#define NBR_TEXT   ", count_text,   "// grep s52_text_   UI.xml" ) >  "sl4agps.cfg";   \
          print("#define NBR_BORDER ", count_border, "// grep s52_border_ UI.xml" ) >> "sl4agps.cfg";   \
        }'                                                                                              \
    android/dist/sdcard/s52droid/UI.tmpl.xml > android/dist/sdcard/s52droid/UI.xml
	adb push  android/dist/sdcard/s52droid/UI.xml /sdcard/s52droid/UI.xml

sl4agps.cfg:
	-rm android/dist/sdcard/s52droid/UI.xml


############### profiling ##############################
#
#
runprofiler:
	gprof ./s52gtk2p

run: s52gtk2
	G_SLICE=always-malloc G_DEBUG=gc-friendly                               \
   libtool --mode=execute valgrind --tool=memcheck --leak-check=full        \
   --leak-resolution=high --num-callers=20 --log-file=s52-valgrind.dump-10  \
   --suppressions=gtk.suppression  ./s52gtk2

# watch ps -C s52gtk2 -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd
# watch ps -C s52gps  -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd
# watch ps -C s52eglx  -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd


########### WIN32 STUFF ####################################
#
#
s52win32: LIBS = $(S52_LIBS) -L$(GTKPATH) -L$(GTKGLPATH)         \
                 -lgtk-win32-2.0-0 -lgdk-win32-2.0-0             \
                 -lgtkglext-win32-1.0-0 -lgdkglext-win32-1.0-0   \
                 -lglib-2.0-0 -lgobject-2.0-0 -lglu32 -lopengl32 \
                 -lgthread-2.0-0

s52eglw32: LIBS = $(S52_LIBS) -L$(GTKPATH)/bin                \
                 -lgtk-win32-2.0-0 -lgdk-win32-2.0-0          \
                 -lglib-2.0-0 -lgobject-2.0-0 -lgthread-2.0-0 \
				 $(GTKPATH)/../libEGL.lib

#../../../mingw/libgps.a


s52win32: CFLAGS = -DS52_USE_GTK2                                                                            \
                   -DS52_USE_GLIB2                                                                           \
                   -mms-bitfields                                                                            \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/include/gtk-2.0      \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/include/glib-2.0     \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/include/cairo        \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/include/pango-1.0    \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/include/atk-1.0      \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/lib/glib-2.0/include \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/lib/gtk-2.0/include  \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtkglext-win32-1.0.6/include/gtkglext-1.0              \
                   -I$(HOME)/dev/gis/openev-cvs/mingw/gtkglext-win32-1.0.6/lib/gtkglext-1.0/include

s52eglw32:CFLAGS = -DS52_USE_GTK2                    \
                   -DS52_USE_GLIB2                   \
                   -mms-bitfields                    \
                   -I$(GTKPATH)/../include           \
                   -I$(GTKPATH)/include/gtk-2.0      \
                   -I$(GTKPATH)/include/glib-2.0     \
				   -I$(GTKPATH)/include/cairo        \
				   -I$(GTKPATH)/include/pango-1.0    \
				   -I$(GTKPATH)/include/atk-1.0      \
                   -I$(GTKPATH)/lib/glib-2.0/include \
                   -I$(GTKPATH)/lib/gtk-2.0/include

s52win32: s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) -I.. s52gtk2.c $(LIBS) -o s52gtk2.exe

s52win32gps: libs52gps.dll s52gtk2.c  ../S52.h
	$(CXX) $(CFLAGS) -I.. s52gtk2.c $(LIBS) -L. -llibs52gps -o s52gtk2gps.exe

libs52gps.dll: s52gps.h s52gps.c
	$(CC) -Wall -I.. -I/home/sduclos/dev/gis/gps/gpsd-2.90-mingw `pkg-config --cflags glib-2.0` -c s52gpsd.c
	$(CC) -shared -Wl,-rpath .. -L.. -lS52  \
	 s52gps.o libgps.a -L$(WIN32PATH) -lglib-2.0-0  -L/usr/lib32/wine -lws2_32 -o $@

#	$(CXX) -shared -Wl,--add-stdcall-alias -Wl,--subsystem,windows -Wl,-rpath .. -L.. -lS52

s52eglw32: s52gtk2egl.c ../S52.h
	$(CC) $(CFLAGS) -I.. s52gtk2egl.c $(LIBS) -o s52eglw32.exe





############ experimantal: Clutter/Javascript ################
#
#
#GNOME3_SHELL_LIBS = /home/sduclos/gnome-shell/install/lib64
GNOME3_SHELL_LIBS = /usr/lib
#GNOME3_SHELL_RUN  = jhbuild run

s52clutter.js: S52-1.0.typelib

S52-1.0.h: ../S52.h libS52-1.0.so
	cp ../S52.h $@

S52-1.0.gir: S52-1.0.h libS52-1.0.so
	$(GNOME3_SHELL_RUN) g-ir-scanner           \
        -DS52_USE_FTGL                         \
        -DS52_USE_GLIB2                        \
        -DS52_USE_PROJ                         \
        -DS52_USE_OGR_FILECOLLECTOR            \
        -DS52_USE_SUPP_LINE_OVERLAP            \
        -DS52_DEBUG                            \
        -DS52_USE_GOBJECT                      \
        -DS52_USE_DBUS                         \
        --verbose --warn-all --namespace=S52   \
        --nsversion=1.0 --pkg=clutter-1.0      \
        --pkg=gobject-2.0 --library=S52-1.0    \
    S52-1.0.h -o $@
	sudo cp $@ $(GNOME3_SHELL_LIBS)/../share/gir-1.0/
	cp $@ ../doc

S52-1.0.typelib: S52-1.0.gir
	$(GNOME3_SHELL_RUN) g-ir-compiler S52-1.0.gir -o $@
	sudo cp S52-1.0.typelib $(GNOME3_SHELL_LIBS)/girepository-1.0/

libS52-1.0.so: ../libS52.so ../S52.h
	cp  ../libS52.so libS52-1.0.so
	sudo cp  libS52-1.0.so $(GNOME3_SHELL_LIBS)


############### clean up ##############################
#
#

clean:
	rm -f s52glx s52eglx s52gv s52gv2 s52gtk2 s52gtk2gl2 s52gtk2p s52gtk2.exe s52qt4 *.o *.so \
    s52gtk2gps S52-1.0.* s52eglx s52ais sl4agps s52gtk2egl s52gtk3egl s52eglw32.exe

distclean: clean
	rm -f sl4agps.cfg android/dist/sdcard/s52droid/bin/s52ais android/dist/sdcard/s52droid/bin/sl4agps
	make -C s52ui   distclean
	make -C android distclean
