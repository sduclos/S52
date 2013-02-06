/**
 *   Copyright 2013 Mehran Ziadloo
 *   (https://github.com/ziadloo/PhoneGap-Java-WebSocket)
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 **/

package com.Method.WebSocket;

import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.Map;
import android.webkit.WebView;

public class WebSocketFactory
{
	private WebView mView;
	private Map<String, AndroidWebSocket> collection;
	
	public WebSocketFactory(WebView view)
	{
		mView = view;
		collection = new HashMap<String, AndroidWebSocket>();
	}
	
	public AndroidWebSocket getNew(String url) throws URISyntaxException
	{
		return getNew(url, (String)null);
	}
	
	public AndroidWebSocket getNew(String url, String[] protocols) throws URISyntaxException
	{
		String p = new String();
		if (protocols != null && protocols.length > 0) {
			StringBuilder sb = new StringBuilder(protocols[0]);
			for (int i=1; i<protocols.length; i++) {
				sb.append(", ");
				sb.append(protocols[i]);
			}
			p = sb.toString();
		}
		return getNew(url, p);
	}
	
	public AndroidWebSocket getNew(String url, String protocols) throws URISyntaxException
	{
		Map<String, String> headers = null;
		if (protocols != null) {
			headers = new HashMap<String, String>();
			headers.put("Sec-WebSocket-Protocol", protocols);
		}
		AndroidWebSocket ws = new AndroidWebSocket(mView, url, headers);
		if (!collection.containsKey(ws.getIdentifier())) {
			collection.put(ws.getIdentifier(), ws);
		}
		return ws;
	}
	
	public void removeSocket(String key)
	{
		if (!collection.containsKey(key)) {
			AndroidWebSocket ws = collection.get(key);
			collection.remove(key);
			ws.close();
		}
	}
}
