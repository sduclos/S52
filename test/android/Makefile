# Makefile: step to compile source, build package (.apk), install on Android
#
# SD 2011NOV21

# Note: root make target: ~/S52/Makefile s52eglarm
# set PATH to dev setup: android-4.0-sdk-linux / android-ndk-r9d)
# export PATH=$PATH:/home/sduclos/mnt/prog/Android/android-sdk/tools:/home/sduclos/mnt/prog/Android/android-ndk
# and to Android BriDge (adb)
#~/dev/prog/Android/android-4.0-sdk-linux/platform-tools$ sudo ./adb start-server

# Note: to start s52ui/s52droid in android by hand
# adb shell am start -a android.intent.action.MAIN -n nav.ecs.s52droid/.s52ui
# adb shell am start -a android.intent.action.MAIN -n nav.ecs.s52droid/android.app.NativeActivity
# bump log: ./adb logcat *:S s52droid

# 07da448e               device usb:2-2 product:razor model:Nexus_7 device:flo
#/etc/udev/rules.d/51-android.rules
#SUBSYSTEM =="usb", ATTR{idVendor}=="18d1", MODE ="0666", GROUP ="plugdev"
# restart udev
# 027831c7438020d7       device usb:2-1 product:omni_wingray model:Xoom_Wifi device:wingray


APKNAME = s52droid-debug.apk

all: build.xml bin/$(APKNAME)

# API level 14 - no MENU button !!
# Android 4.1.2 (API level 16) - OK - JELLY_BEAN
# Android 4.2.2 (API level 17) - NO - cordova need upgrade
# Android 4.3   (API level 18) - NO - cordova need upgrade
# Android 4.4   (API level 19) - NO - cordova need upgrade
# NOTE: JELLY_BEAN_MR1 & JELLY_BEAN_MR2, add anotation @JavascriptInterface in WebSocketFactory.java

build.xml: AndroidManifest.xml
	android update project -p . -t "android-19" -n s52droid


# FIXME: use zipalign for certificate
# ant release install
# pkg: /data/local/tmp/s52droid-release-unsigned.apk
# Failure [INSTALL_PARSE_FAILED_NO_CERTIFICATES]
bin/$(APKNAME): dist/sysroot/lib/libS52.a
	ndk-build -B V=1 NDK_DEBUG=1
	ant debug install

clean:
	-rm -f bin/$(APKNAME)

distclean: clean
	-rm -rf build.xml bin libs/armeabi-v7a obj gen proguard-project.txt project.properties local.properties

.PHONY: all clean distclean


# $adb shell bugreport
# APP_ABI=armeabi-v7a
# -B, --always-make, Unconditionally make all targets.

# add route to connect to xoom telnetd
# sudo route add -host 192.168.1.71 gw 192.168.1.254
# OR
# install terminal-IDE on xoom, in a ternimal
# run: ./system/bin/telnetd
# in adb shell run: telnet 127.0.0.1 8080

# adb uninstall $(PACKAGE)
#

# <start ndk-gdb>
# NOTE: AndroidManifest.xml must not have SPACE around '=' for ndk-gbd to work!
# ndk-gdb --start --force --verbose NDK_LOG=1
# target remote :5039
# setprop debug.db.uid 32767

#<launch gdb application manually>
#
#In a first terminal - device/target:
# adb shell ps  --> look at the PID of your application
# adb shell /data/data/nav.ecs.s52droid/lib/gdbserver :5039 --attach <pid>

#In a second terminal - host:
# cd ~/S52/test/android/
# adb forward tcp:5039 tcp:5039
# arm-linux-androideabi-gdb -e obj/local/armeabi-v7a/app_process  app_process -x obj/local/armeabi-v7a/gdb.setup


# show mem - check every 10 min
# watch -n 600 ps|grep nav.ecs.s52droid

#
# Note:
# mount -o remount,rw -t rootfs /
#
# core dump - it doesn't work !
# ulimit -c unlimited
# echo "/data/core/%e.%p" > /proc/sys/kernel/core_pattern

# Start private sl4a server that listen to port 45001
#   $ am start -a com.googlecode.android_scripting.action.LAUNCH_SERVER                    \
#              -n com.googlecode.android_scripting/.activity.ScriptingLayerServiceLauncher \
#              --ei com.googlecode.android_scripting.extra.USE_SERVICE_PORT 45001

#Aborting or crashing in dlmalloc() usually indicates that the native heap has become corrupted.
#This is usually caused by native code in an application doing something bad.
#
#Sometimes the malloc-debug code will help find problems (like buffer overruns) closer
#to the point where they occur.  On a rooted device:
#
#% adb shell stop
#% adb shell setprop libc.debug.malloc 10
#% adb shell start
#
#You will start to see some additional messages in the log file.
#If a problem is encountered the process will abort.
#
#You can also try turning on CheckJNI:
#
#% adb shell setprop dalvik.vm.checkjni true
#
#and perhaps give CheckJNI "forcecopy" mode a try:
#
#% adb shell setprop dalvik.vm.jniopts forcecopy
#
#These also require stop/start of the app framework.
#
#All of these come with a performance penalty.


# to send STDOUT & STDERR to logcat
#$ adb shell su -c stop
#$ adb shell su -c "setprop log.redirect-stdio true"
#$ adb shell su -c start

# Tracer for OpenGL ES
# par of android-sdk/tools/monitor (Android Device Monitor)
# ==> trace1.gltrace

# perfhudes - can't make it work
#adb shell setprop debug.perfhudes 1

# wake up GPSD
# telnet 127.0.0.1 2947
# ?WATCH={"enable":true,"json":true}

#ActivityManager(  186): No content provider found for permission revoke: file:///data/local/tmp/s52android-debug.apk
#adb shell rm -r /data/dalvik-cache/*

# From 'notbrodie' in XDA forum
# Westwood for TCP, ondemand for performance/interactive for battery, readahead 512, add_random 0, zram off, swappiness 0
# I like interactive with hispeed freq set to 1200mhz

#sh-4.1# ./ld-linux.so.3 --list ./gpsd
#        libc.so => /system/lib/libc.so (0x40181000)
#        libm.so => /system/lib/libm.so (0x40015000)
#        libdl.so => /system/lib/libdl.so (0x400e5000)

# mksh
# export COLUMNS=140
# export LINES=60

# to drive s52droid from PC via s52ui/html
#./adb forward tcp:2950 tcp:2950
#firefox file:///home/sduclos/S52/test/s52ui/build/web/s52ui.html?ws://localhost:2950
