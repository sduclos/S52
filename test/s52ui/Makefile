# Makefile:
#
# 2013FEB25

DARTSDKPATH = /home/sduclos/dev/prog/dart/dart/dart-sdk/bin

web/s52ui.dart.js: web/s52ui.dart
	$(DARTSDKPATH)/dart2js web/s52ui.dart -oweb/s52ui.dart.js

adb push ../s52ui /data/media/dart/s52ui
