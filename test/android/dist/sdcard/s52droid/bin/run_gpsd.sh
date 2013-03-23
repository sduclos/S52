# run_gpsd.sh: script to start gpsd
#
# SD 2011DEC14 - created
# SD 2012MAR09 - move to sdcard 

# need this in AndroidManifest.xml to allows s52android
# to write gpsd.sock and gpsd.pid to sdcard -->
# <uses-permission android:name = "android.permission.WRITE_EXTERNAL_STORAGE" />

# chmod 755 /data/media/s52droid/bin/run_gpsd.sh
# not /sdcard/... (!)


/sdcard/s52droid/bin/gpsd              \
    -F /sdcard/s52droid/bin/gpsd.sock  \
    -P /sdcard/s52droid/bin/gpsd.pid   \
    tcp://example.com:8000 > /dev/null   &
