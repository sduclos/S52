# Makefile: make HTML5/Dart(JS) UI for testing libS52 functionality
#
# 2013FEB25


all:build/web/s52ui.dart.js

DARTSDKPATH = /home/sduclos/dev/prog/dart/dart/dart-sdk/bin

# in DARTSDKPATH/dart2js script:  --heap_growth_rate=512
# also add --no_use_inlining to save 1 sec of compilation time
# --disable-native-live-type-analysis - Disable the optimization that removes unused native types from dart:html 
build/web/s52ui.dart.js: web/s52ui.dart web/s52.dart
	$(DARTSDKPATH)/dart2js --checked web/s52ui.dart -obuild/web/s52ui.dart.js

clean:
	rm -rf build/*

distclean: clean

# copy to Android sdcard (s52ui.java must loadURL html for sdcard - not asset/www)
install: build/web/s52ui.dart.js
	adb push build/web /sdcard/s52ui
