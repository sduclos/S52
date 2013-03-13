# Makefile: step to compile source, build package (.apk), install on xoom
#
# SD 2011NOV21


# $adb shell bugreport
# $ndk-build -B APP_ABI=armeabi-v7a
# NDK_LOG=1 APP_ABI=armeabi

APKNAME = s52android-debug.apk

all: build.xml bin/$(APKNAME)
#all: bin/$(APKNAME)

# Based on Android 4.1.2 (API level 16)
build.xml: AndroidManifest.xml
	android update project -p . -t "android-14" -n s52android

bin/$(APKNAME):
	ndk-build V=1 NDK_DEBUG=1 APP_OPTIM=debug NDK_LOG=1
	ant debug install

# make sure there is no loop in Dart packages
clean:
	-rm -f bin/$(APKNAME)               \
	../s52ui/packages/browser/packages  \
	../s52ui/packages/js/packages       \
	../s52ui/packages/meta/packages     \
	../s52ui/packages/unittest

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


# to send STDOUT & STDERR to logd
#$ adb shell stop
#$ adb shell setprop log.redirect-stdio true
#$ adb shell start

# perfhudes
#adb shell setprop debug.perfhudes 1

# wake up GPSD
# telnet 127.0.0.1 2947
# ?WATCH={"enable":true,"json":true}

#ActivityManager(  186): No content provider found for permission revoke: file:///data/local/tmp/s52android-debug.apk
#adb shell rm -r /data/dalvik-cache/*

