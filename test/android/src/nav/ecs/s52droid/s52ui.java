package nav.ecs.s52droid;

import	android.util.Log;

//import android.content.BroadcastReceiver;
//import android.content.Context;
//import android.content.Intent;
//import android.content.IntentFilter;

import android.graphics.Color;

import android.app.Activity;
import android.os.Bundle;
import org.apache.cordova.*;

import com.Method.WebSocket.WebSocketFactory;

import android.os.StatFs;

public class s52ui extends DroidGap
{
    private static final String TAG = "s52ui";

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        super.setIntegerProperty("backgroundColor", Color.TRANSPARENT);

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


        /*
        // shutdown activity
        // $ adb shell "am broadcast -a foo.bar.intent.action.SHUTDOWN"
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
                public void onReceive(Context ctx, Intent intent) {
                    Log.i(TAG, "onReceive():");
                    finish();
                }
        };

        registerReceiver(receiver, new IntentFilter("nav.ecs.s52droid.s52ui.SHUTDOWN"));
        */
    }
}
