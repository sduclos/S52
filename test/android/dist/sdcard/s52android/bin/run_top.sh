# run_top.sh: run top to 
#
# SD 2012MAY12 

echo "  PID PR CPU% S  #THR     VSS     RSS PCY UID      Name"
top -d 600 | grep nav.ecs.s52android
