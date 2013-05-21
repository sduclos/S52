// cordova.js: script to load the real cordova lib if run on Android
//    <!-- WebSocket Implementation for Android Browser (via Cordove plugin) -->
//    <!-- BUG: Chrome choke here when run on laptop (dart2js OK - firefox)
//    <script type="text/javascript" src="js/cordova-2.2.0.js"></script>
//    -->
//
// SD 2013FEB26

(function() {
    //var VERSION='1.9.0rc1';
    //var VERSION='2.3.0';
    var VERSION='2.2.0';
    //var VERSION='2.4.0rc2';
    var scripts = document.getElementsByTagName('script');
    var cordovaPath = scripts[scripts.length - 1].src.replace('cordova.js', 'cordova-'+VERSION+'.js');

    console.log('platform: ' + window.navigator.platform);
    
	if ( "Linux x86_64" != window.navigator.platform) {
    	document.write('<script type="text/javascript" charset="utf-8" src="' + cordovaPath + '"></script>');
	}
	
})();
