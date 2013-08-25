# Makefile:
#
# 2013FEB25


all:web/s52ui.dart.js

DARTSDKPATH = /home/sduclos/dev/prog/dart/dart/dart-sdk/bin

# in DARTSDKPATH/dart2js script:  --heap_growth_rate=512 
# also add --no_use_inlining to save 1 sec of compilation time
web/s52ui.dart.js: web/s52ui.dart
	$(DARTSDKPATH)/dart2js web/s52ui.dart -oweb/s52ui.dart.js

clean:
	rm web/s52ui.dart.js

install:
	adb push ../s52ui /sdcard/dart/s52ui
