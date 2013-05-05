/*
       Licensed to the Apache Software Foundation (ASF) under one
       or more contributor license agreements.  See the NOTICE file
       distributed with this work for additional information
       regarding copyright ownership.  The ASF licenses this file
       to you under the Apache License, Version 2.0 (the
       "License"); you may not use this file except in compliance
       with the License.  You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

       Unless required by applicable law or agreed to in writing,
       software distributed under the License is distributed on an
       "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
       KIND, either express or implied.  See the License for the
       specific language governing permissions and limitations
       under the License.
*/

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
        super.loadUrl("file:///data/media/dart/s52ui/web/s52ui.html");
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
