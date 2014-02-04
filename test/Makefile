# Makefile: test/dev of libS52.so
#
# SD JUN2008
# SD NOV2009 -add s52glx minimal
# SD 2011MAR -add s52clutter.js
# SD 2011NOV -add s52eglx s52eglarm
# SD 2013SEP -add s52gtk3egl


#all: s52glx        # GLX & OGR
#all: s52eglx       # EGL & GLX & OGR
#all: s52eglarm     # DEPRECATE - EGL & GLES2 & ARM/Android & OGR  S52_USE_ADRENO for Nexus & S52_USE_TEGRA2 for Xoom
#all: s52gv         # GV  (OpenEV  / GTK )
#all: s52gv2        # GV2 (OpenEV2 / GTK2)  broken
#all: s52gtk2       # GTK2 & OGR
#all: s52gtk2p      # GTK2 & OGR (profiler)
all: s52gtk3egl     # GTK3 & EGL
#all: s52win32      # same as s52gtk2 but run on wine
#all: s52clutter.js # same as s52gtk2 but use Clutter & Javascript (experimental)
					# also used to build S52/doc/S52-0.1.gir

#CC   = tcc -fPIC
CC   = gcc -O0 -g -Wall -Wextra -pedantic -std=gnu99
#CC   = g++ -fPIC -O0 -g -Wall
CXX   = g++ -O0 -g -Wall

#s52win32 : CC = /usr/bin/i586-mingw32msvc-gcc -std=c99 -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : CC = /usr/bin/i586-mingw32msvc-gcc -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
#s52win32 : CC = /usr/bin/i586-mingw32msvc-g++ -std=c99 -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : CXX = /usr/bin/i586-mingw32msvc-g++ -O0 -g -Wall -DG_DISABLE_ASSERT -D_MINGW
s52win32 : GTKPATH   = $(HOME)/dev/gis/openev-cvs/mingw/gtk+-bundle_2.16.6-20100912_win32/bin
s52win32 : GTKGLPATH = $(HOME)/dev/gis/openev-cvs/mingw/gtkglext-win32-1.0.6/bin

TAGS = ctags

LIBS52_HOME = ..

# using Android toolchain from NDK to cross compile for ARM (s52eglarm target)
# xoom info: Processor : ARMv7 Processor rev 0 (v7l)
#            Features  : swp half thumb fastmult vfp edsp vfpv3 vfpv3d16
s52eglarm : DROIDTOOLCHAINROOT = /home/sduclos/dev/prog/Android/dev/android-19-toolchain
s52eglarm : DROIDINCLUDE       = $(DROIDTOOLCHAINROOT)/sysroot/usr/include
s52eglarm : DROIDLIBS          = $(DROIDTOOLCHAINROOT)/sysroot/usr/lib
s52eglarm : CC                 = $(DROIDTOOLCHAINROOT)/bin/arm-linux-androideabi-gcc -g -fPIC -mthumb -std=c99
s52eglarm : CXX                = $(DROIDTOOLCHAINROOT)/bin/arm-linux-androideabi-g++ -g -fPIC -mthumb
s52eglarm : STRIP              = $(DROIDTOOLCHAINROOT)/bin/arm-linux-androideabi-strip -s

s52eglarm : S52DROIDINC        = $(HOME)/dev/gis/openev-cvs/contrib/S52/test/android/dist/system/include
s52eglarm : S52DROIDLIBS       = $(HOME)/dev/gis/openev-cvs/contrib/S52/test/android/dist/system/lib


# NOTE: -malign-double: for 32bits system --useless on 64its
# NOTE: GV_USE_DOUBLE_PRECISION_COORD not needed directly for S52
# but gvtype.h need it and it can be pulled in (somehow.)


############### CFLAGS setup ##############################
#
#
# GTK2 / WIN32
CFLAGS = `pkg-config --cflags gtk+-2.0 gtkglext-1.0` \
         -I..                                        \
         -DS52_USE_GTK2                              \
         -DS52_USE_GLIB2                             \
         -DS52_USE_DOTPITCH

s52gtk2gps: CFLAGS += -DS52GTK2_USE_GPSD -DS52_USE_DBUS `pkg-config --cflags --libs glib-2.0 libgps dbus-1 dbus-glib-1`
s52glx:     CFLAGS = `pkg-config --cflags glib-2.0` -I.. -DS52_USE_GTK2 -DS52_USE_DOTPITCH

# -DUSE_FAKE_AIS      - debug - programmaticaly faked AIS data
# -DUSE_AIS           - call s52ais.c

# -DS52_USE_GLES2
# -DS52_USE_AFGLOW    - need symbole in PLAUX_00.DAI
# -DS52_USE_WORLD     - load world shapefile
# -DS52_USE_SOCK      - in s52ais.c when build with S52AIS_STANDALONE use socket to call libS52
s52eglx:    CFLAGS =  -I.. -I. `pkg-config --cflags egl glib-2.0 gthread-2.0 gobject-2.0 libgps` \
                      -DS52_USE_DOTPITCH -DS52_USE_AFGLOW -DS52_USE_EGL -DS52_USE_GLES2

# same as s52eglx, but "gtk+-3.0" is added
s52gtk3egl: CFLAGS =  -I.. -I. `pkg-config --cflags egl glib-2.0 gthread-2.0 gobject-2.0 libgps gtk+-3.0` \
                      -DS52_USE_DOTPITCH -DS52_USE_SOCK -DS52_USE_AFGLOW -DS52_USE_EGL -DS52_USE_GLES2

# -DS52_USE_TEGRA2 (Xoom)
# -DS52_USE_ADRENO (Nexus 7)
# -DS52_USE_WORLD
s52eglarm:  CFLAGS =  -DS52_USE_DOTPITCH                 \
                      -DS52_USE_EGL                      \
                      -DS52_USE_GLES2                    \
                      -DS52_USE_TEGRA2                   \
                      -DS52_USE_ANDROID                  \
                      -DS52_USE_AFGLOW                   \
                      -DS52_USE_SOCK                     \
                      -I..                               \
                      -I$(DROIDINCLUDE)                  \
                      -I$(S52DROIDINC)                   \
                      -I$(S52DROIDINC)/glib-2.0

s52gv:      CFLAGS = `gtk-config --cflags` `glib-config --cflags`     \
                      -I$(OPENEV_HOME) -DGV_USE_DOUBLE_PRECISION_COORD \
                      -I.. -DS52_USE_GV

s52gv2:     CFLAGS = `pkg-config --cflags gtk+-2.0 gtkglext-1.0`      \
                      -D_REENTRANT  -DGV_USE_DOUBLE_PRECISION_COORD    \
                      -I$(OPENEV2HOME) -I..  -DS52_USE_GV -DS52_USE_GTK2


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
GTK2LIBS    = `pkg-config  --libs gtk+-2.0 gtkglext-1.0 gthread-2.0 ftgl lcms`
GL__LIBS    = -lGL
#GL__LIBS  = /usr/lib/fglrx/libGL.so.1.2

OGR_LIBS  = `gdal-config --libs`

s52eglx   : LIBS    = `pkg-config --libs egl glib-2.0 gio-2.0 gthread-2.0 gobject-2.0 libgps` $(S52_LIBS)

# same as s52eglx, add "gtk+-3.0"
s52gtk3egl: LIBS    = `pkg-config --libs egl glib-2.0 gio-2.0 gthread-2.0 gobject-2.0 libgps gtk+-3.0` $(S52_LIBS)

s52eglarm : LIBS    = $(S52DROIDLIBS)/libgio-2.0.a                 \
                      $(S52DROIDLIBS)/libgobject-2.0.a             \
                      $(S52DROIDLIBS)/libgmodule-2.0.a             \
                      $(S52DROIDLIBS)/libglib-2.0.a                \
                      $(S52DROIDLIBS)/libgthread-2.0.a             \
                      $(S52DROIDLIBS)/libiconv.a                   \
                      $(S52DROIDLIBS)/libgps.a                     \
                       -L$(DROIDLIBS) -llog


############### build ##############################
#
#
s52glx: s52glx.c
	$(CC) $(CFLAGS) s52glx.c $(S52_LIBS) -o $@

#s52eglx: s52egl.c
#	$(CC) $(CFLAGS) s52egl.c $(LIBS) -o $@
#	$(CC) $(CFLAGS) -DS52AIS_STANDALONE s52ais.c $(LIBS) -o s52ais
s52eglx: s52egl.c s52ais.c
	$(CC) $(CFLAGS) s52egl.c s52ais.c $(LIBS) -o $@

s52gtk3egl: s52gtk3egl.c
	$(CC) $(CFLAGS) s52gtk3egl.c $(LIBS) -o $@
	$(CC) $(CFLAGS) -DS52AIS_STANDALONE s52ais.c $(LIBS) -o s52ais


############### DEPRECATED build: Android (Android.mk do the build) ##############################

#s52eglarm: libs52gps.so s52egl.c ../S52.h -Wl,-allow-shlib-undefin
# -Wl,--entry=_start -Wl,-rpath=/system/lib -nostdlib
# -Wl,-rpath=$(ARMLIBS) -nostdlib
# -Wl,-rpath=$(TIAMATLIBS)
# -Wl,--dynamic-linker=/data/local/bin/ld-linux.so.3
#s52eglarm: s52egl.c ../S52.h s52gps.c
#	$(CXX) $(CFLAGS) -shared s52egl.c $(LIBS)          \
#	-Wl,-rpath=/system/lib -Wl,-rpath-link=/system/lib \
#	-o $@
# $(CC) $(CFLAGS) $(S52DROIDLIBS)/libS52.a $(LIBS) s52egl.c  -o s52droid

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

# android 4.1
# adb shell chmod 755 /data/media/s52droid/bin/sl4agps
sl4agps: sl4agps.c sl4agps.cfg android/dist/sdcard/s52droid/UI.xml
	$(CC)  $(CFLAGS)    sl4agps.c $(LIBS) -o sl4agps
	$(STRIP)  sl4agps
	cp        sl4agps   android/dist/sdcard/s52droid/bin
	adb push  sl4agps   /sdcard/s52droid/bin
	adb shell su -c 'chmod 755 /data/media/0/s52droid/bin/sl4agps'

# android 4.1
# adb shell chmod 755 /data/media/s52droid/bin/s52ais
s52ais: s52ais.c
	$(CXX) $(CFLAGS) -DS52AIS_STANDALONE s52ais.c  $(LIBS) -o s52ais
	$(STRIP)  s52ais
	cp        s52ais    android/dist/sdcard/s52droid/bin
	adb push  s52ais    /sdcard/s52droid/bin
	adb shell su -c 'chmod 755 /data/media/0/s52droid/bin/s52ais'

s52eglarm: s52ais
#s52eglarm: s52ais sl4agps
#	adb shell su -c 'chmod 755 /data/media/0/s52droid/bin/*.sh'
#	adb shell su -c 'chmod 755 /data/media/0/s52droid/bin/gpsd'

# Note: to start s52ui (the HTML UI that replace sl4agps) in android by hand
# adb shell am start -a android.intent.action.MAIN -n nav.ecs.s52droid/.s52ui
# adb shell am start -a android.intent.action.MAIN -n nav.ecs.s52droid/android.app.NativeActivity
############### build: Android ##############################




s52gv:  LIBS = $(S52_LIBS) $(GTK_LIBS) $(GV_LIBS) $(OGR_LIBS)
s52gv:  s52gv.c
	(cd $(OPENEV_HOME); make gvtest; $(CC) -shared *.o -o libgv.so)
	$(CC) $(CFLAGS) s52gv.c $(LIBS) -o $@

s52gv2: LIBS = $(S52_LIBS) $(GTK2LIBS) $(GV2LIBS) $(OGR_LIBS)
s52gv2: s52gv.c
	$(CC) $(CFLAGS) s52gv.c $(LIBS) -o $@

testmain:
	$(CC) $(LDFLAGS) testmain.o $(LIBS)  -o $@

#s52gtk2: LIBS = $(S52_LIBS) $(GTK2LIBS) $(GL__LIBS) -ldl
s52gtk2: LIBS = $(S52_LIBS) $(GTK2LIBS)  -ldl
s52gtk2: s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) $(GL__LIBS) s52gtk2.c $(LIBS) -o $@

#libs52gps.so: s52gps.h s52gps.c
#	$(CC) $(CFLAG) -fPIC -shared -I.. s52gps.c `pkg-config --cflags --libs glib-2.0 libgps dbus-1 dbus-glib-1` -o $@
libs52gps.so: s52gps.h s52gps.c
	$(CC) $(CFLAGS) -fPIC -shared -I.. s52gps.c -o $@

s52gtk2gps: LIBS = $(S52_LIBS) $(GTK2LIBS) $(OGR_LIBS) $(GL__LIBS) -Wl,-rpath . -L. -ls52gps
s52gtk2gps: libs52gps.so s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) s52gtk2.c $(LIBS) -o $@

# -unix: deprecated
s52qt4: s52qt4.pro s52qt4.cpp ../S52.h
	qmake -o Makefile.qt4 s52qt4.pro
	make -f Makefile.qt4


s52gtk2p: LIBS = $(GTK2LIBS) $(OGR_LIBS) $(GL__LIBS)
s52gtk2p: s52gtk2.c ../S52.h
	$(CC) $(CFLAGS) -pg s52gtk2.c ../*.o $(LIBS) -lproj -llcms -o $@

#$(CC) $(CFLAGS) -nodefaultlibs -nostartfiles s52gtk2.c ../*.o $(LIBS) -lproj -llcms -o $@




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

#watch ps -C s52gtk2 -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd
#watch ps -C s52gps  -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd
# watch -d ps -C s52eglx  -o pid,ppid,rss,vsize,pcpu,pmem,time,cmd


########### WIN32 STUFF ####################################
#
#
s52win32: LIBS = $(S52_LIBS) -L$(GTKPATH) -L$(GTKGLPATH)         \
                 -lgtk-win32-2.0-0 -lgdk-win32-2.0-0             \
                 -lgtkglext-win32-1.0-0 -lgdkglext-win32-1.0-0   \
                 -lglib-2.0-0 -lgobject-2.0-0 -lglu32 -lopengl32 \
                 -lgthread-2.0-0

#../../../mingw/libgps.a


s52win32: CFLAGS = -DS52_USE_GTK2                                                                            \
                   -DS52_USE_GLIB2                                                                           \
                   -DS52_USE_DOTPITCH                                                                        \
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

#s52win32: s52gtk2.c  ../S52.h
#	$(CXX) $(CFLAGS) -I.. s52gtk2.c $(LIBS) -o s52gtk2.exe

s52win32: s52gtk2.c  ../S52.h
	$(CC) $(CFLAGS) -I.. s52gtk2.c $(LIBS) -o s52gtk2.exe

s52win32gps: libs52gps.dll s52gtk2.c  ../S52.h
	$(CXX) $(CFLAGS) -I.. s52gtk2.c $(LIBS) -L. -llibs52gps -o s52gtk2gps.exe

libs52gps.dll: s52gps.h s52gps.c
	$(CC) -Wall -I.. -I/home/sduclos/dev/gis/gps/gpsd-2.90-mingw `pkg-config --cflags glib-2.0` -c s52gpsd.c
	$(CC) -shared -Wl,-rpath .. -L.. -lS52  \
	 s52gps.o libgps.a -L$(WIN32PATH) -lglib-2.0-0  -L/usr/lib32/wine -lws2_32 -o $@

#	$(CXX) -shared -Wl,--add-stdcall-alias -Wl,--subsystem,windows -Wl,-rpath .. -L.. -lS52





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
        -DS52_USE_DOTPITCH                     \
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
	rm -f s52glx s52eglx s52gv s52gv2 s52gtk2 s52gtk2p s52gtk2.exe s52qt4 *.o *.so \
    s52gtk2gps S52-1.0.* s52eglx s52ais sl4agps s52gtk3egl

distclean: clean
	rm -f sl4agps.cfg android/dist/sdcard/s52droid/bin/s52ais android/dist/sdcard/s52droid/bin/sl4agps
	make -C android distclean
