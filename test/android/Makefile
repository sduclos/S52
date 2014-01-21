# Makefile: step to compile source, build package (.apk), install on xoom
#
# SD 2011NOV21


# $adb shell bugreport
# APP_ABI=armeabi-v7a
# -B, --always-make, Unconditionally make all targets.

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

bin/$(APKNAME): dist/system/lib/libS52.a
	ndk-build -B V=1 NDK_DEBUG=1 APP_OPTIM=debug NDK_LOG=1
	ant debug install

clean:
	-rm -f bin/$(APKNAME)

distclean: clean
	-rm -rf build.xml bin libs/armeabi-v7a obj gen proguard-project.txt project.properties local.properties

.PHONY: all clean distclean


# add route to connect to xoom telnetd
# sudo route add -host 192.168.1.71 gw 192.168.1.254
# OR
# install terminal-IDE on xoom, in a ternimal
# run: ./system/bin/telnetd
# in adb shell run: telnet 127.0.0.1 8080

# adb uninstall $(PACKAGE)
#
# then start gdb
# ndk-gdb --start --force --verbose NDK_LOG=1
# target remote :5039
# setprop debug.db.uid 32767

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

# perfhudes
#adb shell setprop debug.perfhudes 1

# wake up GPSD
# telnet 127.0.0.1 2947
# ?WATCH={"enable":true,"json":true}

#ActivityManager(  186): No content provider found for permission revoke: file:///data/local/tmp/s52android-debug.apk
#adb shell rm -r /data/dalvik-cache/*

# From 'notbrodie' in XDA forum
# Westwood for TCP, ondemand for performance/interactive for battery, readahead 512, add_random 0, zram off, swappiness 0
# I like interactive with hispeed freq set to 1200mhz
# I use trickster donate to set all of these
