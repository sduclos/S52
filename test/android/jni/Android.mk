LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := s52droid
LOCAL_SRC_FILES  := ../../s52egl.c ../../s52ais.c

# -DUSE_FAKE_AIS     - fake AIS (debug)
# -DUSE_AIS          - get AIS data from gpsd running on a host machine

# -DS52_USE_AFGLOW   - add afterglow to target (VESSEL/OWNSHP), need symbole in PLAUX_00.DAI
# -DS52_USE_SL4A     - SL4A is a RPC bridge to the Android framework
# -DS52_USE_GLES2    -
# -DS52_USE_TEGRA2   - GLES2
# -DS52_USE_ADRENO   - GLES2
# -DS52_USE_WORLD    - experimental - load world Shapefile
# -DS52_USE_SOCK     - send call to S52_*() via socket
# -DS52_USE_LOG      - use S52_error_cb in init to get log isend to STDOUT (usefull on non-rooted device)
S52ANDRIODINC    := /home/sduclos/S52/test/android/dist/system/include
LOCAL_CFLAGS     := -std=c99 -Wall -g -DG_LOG_DOMAIN=\"s52droid\"                                \
                    -DS52_USE_DOTPITCH -DS52_USE_ANDROID -DS52_USE_ADRENO                        \
					-DS52_USE_AFGLOW -DS52_USE_EGL -DS52_USE_GLES2 -DUSE_AIS                     \
                    -I../..                                                                      \
                    -I$(S52ANDRIODINC)                                                           \
					-I$(S52ANDRIODINC)/glib-2.0                                                  \
                    -I$(S52ANDRIODINC)/glib-android-1.0                                          \
                    -I/home/sduclos/dev/prog/Android/dev/android-9-toolchain/sysroot/usr/include \
                    $(NULL)

#LOCAL_LDFLAGS    := -Wl,-Map,xxx.map -rdynamic -fexception
LOCAL_LDFLAGS    := -rdynamic -fexception

CYANOGENLIBS     := /home/sduclos/dev/prog/Android/xoom/cm/system/lib
S52ANDRIODLIBS   := /home/sduclos/S52/test/android/dist/system/lib
ARMTOOLCHAINROOT := /home/sduclos/dev/prog/Android/dev/android-9-toolchain
LOCAL_LDLIBS     := -L$(CYANOGENLIBS) -lEGL -lGLESv2                                  \
					-llog -landroid -lz -landroid_runtime -lc -lm -ldl                \
                    $(S52ANDRIODLIBS)/libS52.a                                        \
                    $(S52ANDRIODLIBS)/libgps.a                                        \
                    $(S52ANDRIODLIBS)/libgthread-2.0.a                                \
                    $(S52ANDRIODLIBS)/libproj.a                                       \
                    $(S52ANDRIODLIBS)/libgdal.a                                       \
                    $(S52ANDRIODLIBS)/liblcms.a                                       \
					$(S52ANDRIODLIBS)/libfreetype.a                                   \
                    $(S52ANDRIODLIBS)/libgio-2.0.a                                    \
					$(S52ANDRIODLIBS)/libgobject-2.0.a                                \
					$(S52ANDRIODLIBS)/libgmodule-2.0.a                                \
					$(S52ANDRIODLIBS)/libglib-2.0.a                                   \
					$(S52ANDRIODLIBS)/libiconv.a                                      \
					$(S52ANDRIODLIBS)/libandroid_native_app_glue.a                    \
                    $(ARMTOOLCHAINROOT)/arm-linux-androideabi/lib/thumb/libstdc++.a


# /home/sduclos/dev/prog/Android/dev/usr/arm-linux-gnueabi/lib/libSegFault.so
# $(S52ANDRIODLIBS)/libunwind.a

LOCAL_ARM_MODE   := arm


include $(BUILD_SHARED_LIBRARY)

#NDK_MODULE_PATH += /home/sduclos/dev/prog/Android/dev
#$(call import-module,0xdroid-external_libunwind)

#$(call import-module,android/native_app_glue)
#$(call import-module,glib)
