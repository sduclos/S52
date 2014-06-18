LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := s52droid
# -DUSE_AIS
LOCAL_SRC_FILES  := ../../s52egl.c ../../s52ais.c
# -DS52AIS_STANDALONE
#LOCAL_SRC_FILES  := ../../s52egl.c


# CHECK THIS: include $(BUILD_EXECUTABLE)



# -DUSE_FAKE_AIS     - fake AIS (debug)
# -DUSE_AIS          - get AIS data from gpsd running on a host machine

# -DS52_USE_AFGLOW   - add afterglow to target (VESSEL/OWNSHP), need symbole in PLAUX_00.DAI
# -DS52_USE_SL4A     - SL4A is a RPC bridge to the Android framework
# -DS52_USE_GLES2    - GLES2.x
# -DS52_USE_GLES3    - GLES3.x / GLSL ES 3.0
# -DS52_USE_ANDROID  -
# -DS52_USE_TEGRA2   - need GLES2
# -DS52_USE_ADRENO   - need GLES2
# -DS52_USE_WORLD    - experimental - load world Shapefile
# -DS52_USE_SOCK     - send call to S52_*() via socket
# -DS52_USE_LOG      - use S52_error_cb in init to get log send to STDOUT (usefull on non-rooted device)
# -DS52_USE_RADAR    -

ARMTOOLCHAINPATH := /home/sduclos/dev/prog/Android/dev/android-19-toolchain
S52DROIDINC      := /home/sduclos/S52/test/android/dist/sysroot/include
# TEGRA: tadp: -fno-omit-frame-pointer -mno-thumb
# NDK-BUILD add this: -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16
LOCAL_CFLAGS     := -g -O0 -std=c99 -Wall -DG_LOG_DOMAIN=\"s52droid\"                 \
                    -DS52_USE_ANDROID -DS52_USE_ADRENO                                \
                    -DS52_USE_EGL -DS52_USE_GLES2                                     \
                    -DUSE_AIS -DS52_USE_LOG                                           \
                    -DS52_USE_AFGLOW                                                  \
                    -I../..                                                           \
                    -I$(S52DROIDINC)                                                  \
                    -I$(S52DROIDINC)/glib-2.0                                         \
                    -I$(S52DROIDINC)/glib-android-1.0                                 \
                    -I$(ARMTOOLCHAINPATH)/sysroot/usr/include

#LOCAL_WHOLE_STATIC_LIBRARIES :=
# no need to link gpsd client when S52AIS_STANDALONE
# $(S52DROIDLIBS)/libgps.a

LOCAL_STATIC_LIBRARIES :=                                                             \
                    $(S52DROIDLIBS)/libS52.a                                          \
                    $(S52DROIDLIBS)/libgdal.a                                         \
                    $(S52DROIDLIBS)/libproj.a                                         \
                    $(S52DROIDLIBS)/liblcms.a                                         \
                    $(S52DROIDLIBS)/libfreetype.a                                     \
                    $(S52DROIDLIBS)/libgio-2.0.a                                      \
                    $(S52DROIDLIBS)/libgobject-2.0.a                                  \
                    $(S52DROIDLIBS)/libgmodule-2.0.a                                  \
                    $(S52DROIDLIBS)/libgthread-2.0.a                                  \
                    $(S52DROIDLIBS)/libglib-2.0.a                                     \
                    $(S52DROIDLIBS)/libiconv.a                                        \
                    $(S52DROIDLIBS)/libandroid_native_app_glue.a                      \
                    $(ARMTOOLCHAINPATH)/arm-linux-androideabi/lib/thumb/libstdc++.a

#LOCAL_LDFLAGS    := -Wl,-Map,xxx.map -rdynamic -fexception
# NDK-BUILD add this: -march=armv7-a -Wl,--fix-cortex-a8

#CYANOGENLIBS     := /home/sduclos/dev/prog/Android/xoom/cm/system/lib
OMNILIBS         := /home/sduclos/dev/prog/Android/xoom/omni/system/lib
S52DROIDLIBS     := /home/sduclos/S52/test/android/dist/sysroot/lib
#LOCAL_LDLIBS     := -L$(CYANOGENLIBS)
#LOCAL_LDLIBS     := -L$(OMNILIBS)
#LOCAL_LDFLAGS    := -rdynamic -fexception
LOCAL_LDFLAGS    := -L$(S52DROIDLIBS) -lS52 -lgps -lgdal -lproj -llcms \
-lfreetype -lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lgthread-2.0 -liconv -landroid_native_app_glue \
$(ARMTOOLCHAINPATH)/arm-linux-androideabi/lib/thumb/libstdc++.a



#LOCAL_SHARED_LIBRARIES := -L$(OMNILIBS) -lEGL -lGLESv2 -llog -landroid -landroid_runtime -lz -lc -lm -ldl
#_LOCAL_SHARED_LIBRARIES := -lEGL -lGLESv2 -llog -landroid -lz -lc -lm -ldl
#_LOCAL_SHARED_LIBRARIES := -lEGL -lGLESv2_adreno -lGLESv1_CM_adreno -llog -landroid -lz -lc -lm -ldl
_LOCAL_SHARED_LIBRARIES := -lEGL -lGLESv3 -llog -landroid -lz -lc -lm -ldl

# Android NDK:     This is likely to result in incorrect builds. Try using LOCAL_STATIC_LIBRARIES
# Android NDK:     or LOCAL_SHARED_LIBRARIES instead to list the library dependencies of the
# Android NDK:     current module
# Android NDK: [armeabi-v7a] Modules to build: s52droid
#LOCAL_LDLIBS     := $(LOCAL_STATIC_LIBRARIES) $(LOCAL_SHARED_LIBRARIES)
LOCAL_LDLIBS     := $(_LOCAL_SHARED_LIBRARIES)

# /home/sduclos/dev/prog/Android/dev/usr/arm-linux-gnueabi/lib/libSegFault.so
# $(S52DRIODLIBS)/libunwind.a

LOCAL_ARM_MODE   := arm


include $(BUILD_SHARED_LIBRARY)

#NDK_MODULE_PATH += /home/sduclos/dev/prog/Android/dev
#$(call import-module,0xdroid-external_libunwind)

#$(call import-module,android/native_app_glue)
#$(call import-module,glib)
