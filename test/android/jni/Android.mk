LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := s52android
#LOCAL_SRC_FILES  := ../../s52egl.c ../../s52gps.c
LOCAL_SRC_FILES  := ../../s52egl.c

# -DS52_USE_AFGLOW   - add afterglow to target (VESSEL/OWNSHP), need symbole in PLAUX_00.DAI
# -DS52_USE_FAKE_AIS - fake AIS (debug)
# -DS52_USE_SL4A     - SL4A is a RPC bridge to the Android framework
# -DS52_USE_AIS      - not use
S52ANDRIODINC    := /home/sduclos/S52/test/android/dist/system/include
LOCAL_CFLAGS     := -Wall -g -DG_LOG_DOMAIN=\"s52android\"                                          \
                    -DS52_USE_DOTPITCH -DS52_USE_ANDROID -DS52_USE_TEGRA2 -DS52_USE_AFGLOW          \
                    -I../..                                                                         \
                    -I$(S52ANDRIODINC)                                                              \
					-I$(S52ANDRIODINC)/glib-2.0                                                     \
                    -I$(S52ANDRIODINC)/glib-android-1.0                                             \
                    -I/home/sduclos/dev/prog/Android/dev/android-9-toolchain/sysroot/usr/include    \
                    $(NULL)

#LOCAL_LDFLAGS    := -Wl,-Map,xxx.map -rdynamic -fexception
LOCAL_LDFLAGS    := -rdynamic -fexception

#TIAMATLIBS       := /home/sduclos/dev/prog/Android/xoom/tiamat-xoom-rom/Xoom.Zone-Tiamat.Rom.2.2.2/system/lib
TIAMATLIBS       := /home/sduclos/dev/prog/Android/xoom/tiamat-xoom-rom/Eos-wingray/system/lib
S52ANDRIODLIBS   := /home/sduclos/S52/test/android/dist/system/lib
ARMTOOLCHAINROOT := /home/sduclos/dev/prog/Android/dev/android-9-toolchain
#ARMTOOLCHAINROOT := /home/sduclos/dev/prog/Android/dev/android-14-toolchain
#ARMLIBS          := $(ARMTOOLCHAINROOT)/sysroot/usr/lib
# -lGLESv1_CM
# $(S52ANDRIODLIBS)/libgio-2.0.a
LOCAL_LDLIBS     := -L$(TIAMATLIBS) -lEGL -lGLESv2                                               \
                    -llog -lcutils -lz -lutils                                                   \
                    -lbinder -lpixelflinger -lhardware -lhardware_legacy -lskia -lui -lgui       \
                    -lsurfaceflinger_client -landroid -lexpat -lnativehelper -lnetutils          \
                    -lcamera_client -lsqlite -ldvm -lETC1 -lsonivox -lcrypto                     \
                    -lssl -licuuc -licui18n -lmedia -lwpa_client -ljpeg -lnfc_ndef -lusbhost     \
                    -lhwui -lbluedroid -ldbus -lemoji -lstlport -lstagefright_foundation         \
                    -landroid_runtime -lc -lm -ldl                                               \
                           $(S52ANDRIODLIBS)/libS52.a                     \
                           $(S52ANDRIODLIBS)/libgps.a                     \
                           $(S52ANDRIODLIBS)/libgthread-2.0.a             \
                           $(S52ANDRIODLIBS)/libproj.a                    \
                           $(S52ANDRIODLIBS)/libgdal.a                    \
                           $(S52ANDRIODLIBS)/liblcms.a                    \
						   $(S52ANDRIODLIBS)/libfreetype.a                \
                           $(S52ANDRIODLIBS)/libgio-2.0.a                 \
						   $(S52ANDRIODLIBS)/libgobject-2.0.a             \
						   $(S52ANDRIODLIBS)/libgmodule-2.0.a             \
						   $(S52ANDRIODLIBS)/libglib-2.0.a                \
						   $(S52ANDRIODLIBS)/libiconv.a                   \
						   $(S52ANDRIODLIBS)/libandroid_native_app_glue.a \
                    $(ARMTOOLCHAINROOT)/arm-linux-androideabi/lib/thumb/libstdc++.a


# /home/sduclos/dev/prog/Android/dev/usr/arm-linux-gnueabi/lib/libSegFault.so
# $(S52ANDRIODLIBS)/libunwind.a                  

LOCAL_ARM_MODE   := arm


include $(BUILD_SHARED_LIBRARY)

#NDK_MODULE_PATH += /home/sduclos/dev/prog/Android/dev
#$(call import-module,0xdroid-external_libunwind)

#$(call import-module,android/native_app_glue)
#$(call import-module,glib)
