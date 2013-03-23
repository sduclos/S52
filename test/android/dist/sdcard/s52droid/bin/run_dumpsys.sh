# run_dumpsys.sh: check mem usage
#
# SD 2012MAR25


#watch -t -n 300 'dumpsys meminfo nav.ecs.s52android|grep TOTAL'

ps u | grep USER

while true 
do
   #date +%T
   #dumpsys meminfo nav.ecs.s52android|grep TOTAL
   ps u|grep 21536|grep s52eglx
   sleep 300 
done
