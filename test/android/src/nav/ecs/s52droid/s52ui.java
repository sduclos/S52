package nav.ecs.s52droid;

import	android.util.Log;

//import android.app.Activity;
//import android.content.BroadcastReceiver;
//import android.content.Context;
//import android.content.Intent;
//import android.content.IntentFilter;

import android.os.Bundle;
import android.graphics.Color;

import org.apache.cordova.*;

import com.Method.WebSocket.WebSocketFactory;


public class s52ui extends DroidGap
{
    private static final String TAG = "s52ui";

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        super.setIntegerProperty("backgroundColor", Color.TRANSPARENT);
        //super.loadUrl("file:///android_asset/www/index.html");
        //super.loadUrl("file:///android_asset/www/s52ui.html");

        // debug - work OK for dev (faster)
        //super.loadUrl("file:///data/media/dart/s52ui/web/s52ui.html");  // android 4.1
        super.loadUrl("file:///sdcard/dart/s52ui/web/s52ui.html");        // android 4.2

        // debug - page loading always from file (a bit slower)
        appView.clearCache(false);

        appView.setBackgroundColor(0);

        appView.addJavascriptInterface(new WebSocketFactory(appView), "WebSocketFactory");

        Log.i(TAG, "starting WebView ...");

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
