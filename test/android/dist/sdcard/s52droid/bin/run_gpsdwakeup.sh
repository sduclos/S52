# run_wakeupgpsd.sh: wake up GPSD
#
# SD 2013JUN02

echo "?WATCH={\"enable\":true,\"json\":true}" | telnet 127.0.0.1 2947
