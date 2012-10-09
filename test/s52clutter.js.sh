# run_s52clutter.js.sh: start S52 via clutter via Javascript
#
# SD 2010APR14	- created


#jhbuild run ~/gnome-shell/source/gnome-shell/tests/run-test.sh -g AIS-monitor.js
#jhbuild run ~/gnome-shell/source/gnome-shell/tests/run-test.sh -v AIS-monitor.js
#jhbuild run ~/gnome-shell/source/gnome-shell/tests/run-test.sh s52clutter.js

GI_TYPELIB_PATH=/usr/lib/gnome-shell
GJS_PATH=/usr/share/gnome-shell/js/
#GJS_DEBUG_OUTPUT
GJS_DEBUG_TOPICS="JS ERROR;JS LOG"
#GNOME_SHELL_JS
GNOME_SHELL_TESTSDIR=
#LD_PRELOAD


export GI_TYPELIB_PATH GJS_PATH GJS_DEBUG_TOPICS

#gjs AIS-monitor.js
gjs s52clutter.js

