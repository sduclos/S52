# Makefile: make HTML5/Dart(JS) UI for testing libS52 functionality
#
# SD 2013FEB25
# SD 2014APR25 - update Dart build file layout


all:build/web/s52ui.dart.js

DARTSDKPATH = /home/sduclos/dev/prog/dart/dart/dart-sdk/bin

# in DARTSDKPATH/dart2js script:  --heap_growth_rate=512
# also add --no_use_inlining to save 1 sec of compilation time
# --disable-native-live-type-analysis - Disable the optimization that removes unused native types from dart:html
# --checked 

# need (re)build if html change to (re)populate build/web/
build/web/s52ui.dart.js: web/s52.dart web/s52ui.dart web/s52ui.css web/s52ui.html web/s52ui.js
	$(DARTSDKPATH)/pub -v build

clean:
	rm -rf build/web/*

distclean: clean

# copy to Android sdcard (s52ui.java loadURL html from sdcard if present else asset/www)
install: build/web/s52ui.dart.js
	adb push build/web /sdcard/s52droid/s52ui/
