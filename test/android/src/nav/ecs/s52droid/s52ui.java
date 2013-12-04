package nav.ecs.s52droid;

import android.util.Log;

import android.graphics.Color;

import android.app.Activity;
import android.os.Bundle;
import android.os.StatFs;
import android.view.KeyEvent;
import android.webkit.WebView;
import android.webkit.WebChromeClient;
import android.webkit.GeolocationPermissions;

public class s52ui extends Activity
{
    private static final String TAG = "s52ui_API19";

    WebView webview;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        webview = new WebView(this);
        //webview = (WebView) findViewById(R.id.webview);

        setContentView(webview);

        webview.getSettings().setJavaScriptEnabled(true);
        webview.getSettings().setGeolocationDatabasePath("/data/data/nav.ecs.s52droid");

        webview.setBackgroundColor(Color.TRANSPARENT);

        try {
            StatFs stat = new StatFs("/sdcard/s52ui/s52ui.html");
            Log.i(TAG, "StatFs: " + stat.getAvailableBlocks());
            // debug - work OK for dev (faster)
            webview.loadUrl("file:///sdcard/s52ui/s52ui.html");
            webview.clearCache(false);                           // page loading always from file (a bit slower)
        } catch (IllegalArgumentException e) {
            webview.loadUrl("file:///android_asset/www/s52ui.html");
        }

        // debug from Chrome
        //webview.setWebContentsDebuggingEnabled(true);

        webview.setWebChromeClient(new WebChromeClient() {
        	public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
                callback.invoke(origin, true, false);
            }
		});

        Log.i(TAG, "Starting WebView ...");


        // shutdown activity
        // $ adb shell "am broadcast -a foo.bar.intent.action.SHUTDOWN"
        //BroadcastReceiver receiver = new BroadcastReceiver() {
        //    @Override
        //        public void onReceive(Context ctx, Intent intent) {
        //            Log.i(TAG, "onReceive():");
        //            finish();
        //        }
        //};
        //registerReceiver(receiver, new IntentFilter("nav.ecs.s52droid.s52ui.SHUTDOWN"));
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        // Check if the key event was the Back button and if there's history
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            webview.loadUrl("javascript:toggleUI()");
            return true;
        }
        // If it wasn't the Back key or there's no web page history, bubble up to the default
        // system behavior (probably exit the activity)
        return super.onKeyDown(keyCode, event);
    }
}

/*
import org.apache.cordova.*;
import com.Method.WebSocket.WebSocketFactory;
public class s52ui_API16 extends DroidGap
{
    private static final String TAG = "s52ui_API16";

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        appView.getSettings().setJavaScriptEnabled(true);
        appView.getSettings().setGeolocationDatabasePath("/data/data/nav.ecs.s52droid");

        appView.setBackgroundColor(Color.TRANSPARENT);

        appView.clearCache(false);                         // page loading always from file (a bit slower)

        // Cordova
        super.setIntegerProperty("backgroundColor", Color.TRANSPARENT);
        appView.addJavascriptInterface(new WebSocketFactory(appView), "WebSocketFactory");

        // debug from Chrome
        //webview.setWebContentsDebuggingEnabled(true);

        webview.setWebChromeClient(new WebChromeClient() {
        	public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
                callback.invoke(origin, true, false);
            }
		});


        try {
            StatFs stat = new StatFs("/sdcard/s52ui/s52ui.html");
            Log.i(TAG, "StatFs: " + stat.getAvailableBlocks());
            // debug - work OK for dev (faster)
            //super.loadUrl("file:///data/media/dart/s52ui/web/s52ui.html");  // android 4.1
            super.loadUrl("file:///sdcard/s52ui/s52ui.html");                 // android 4.2
            appView.clearCache(false);                                        // page loading always from file (a bit slower)
        } catch (IllegalArgumentException e) {
            super.loadUrl("file:///android_asset/www/s52ui.html");
        }

        appView.setBackgroundColor(0);

        // enable built-in zoom
        //appView.getSettings().setBuiltInZoomControls(true);

        // quiet Err msg in logcat
        appView.getSettings().setGeolocationDatabasePath("/data/data/nav.ecs.s52droid");

        appView.addJavascriptInterface(new WebSocketFactory(appView), "WebSocketFactory");

        // FIXME: invisible not working
        //appView.getSettings().setVisibility(appView.INVISIBLE);
        //appView.setVisibility(appView.INVISIBLE);

        Log.i(TAG, "Starting WebView ...");


        // shutdown activity
        // $ adb shell "am broadcast -a foo.bar.intent.action.SHUTDOWN"
        //BroadcastReceiver receiver = new BroadcastReceiver() {
        //    @Override
        //        public void onReceive(Context ctx, Intent intent) {
        //            Log.i(TAG, "onReceive():");
        //            finish();
        //        }
        //};

        //registerReceiver(receiver, new IntentFilter("nav.ecs.s52droid.s52ui.SHUTDOWN"));
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
    // Check if the key event was the Back button and if there's history
    if (keyCode == KeyEvent.KEYCODE_BACK) {
        //myWebView.goBack();
        webview.loadUrl("javascript:toggleUI()");
        return true;
    }
    // If it wasn't the Back key or there's no web page history, bubble up to the default
    // system behavior (probably exit the activity)
    return super.onKeyDown(keyCode, event);
}
}
*/