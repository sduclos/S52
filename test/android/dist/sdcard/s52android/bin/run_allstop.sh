# run_allstop: stop all process in the group s52android
#
# SD 2012MAY23


# signal s52ais for a clean exit
kill -SIGINT `cat /data/media/s52android/bin/s52ais.pid 2> /dev/null` 2> /dev/null
rm /sdcard/s52android/bin/s52ais.pid

# signal sl4agps for a clean exit
kill -SIGINT `cat /data/media/s52android/bin/sl4agps.pid 2> /dev/null` 2> /dev/null
# debug -
rm /sdcard/s52android/bin/sl4agps.pid

# stop sl4a server
/system/xbin/su -c "/system/bin/am force-stop com.googlecode.android_scripting"
#/system/xbin/su -c "kill -9  `ps | grep com.googlecode.android_scripting | awk '{ print $2 }'`"
