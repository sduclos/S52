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

(function () {
	if (window.WebSocket) {
		return;
	}

	var WebSocket = window.WebSocket = function(url, protocols) {
		// listener to overload
		this.onopen    = null;
		this.onmessage = null;
		this.onerror   = null;
		this.onclose   = null;

		this._handler = WebSocketFactory.getNew(url, protocols);
		WebSocket.collection[this._handler.getIdentifier()] = this;

		return this;
	};

	WebSocket.collection = {};

	WebSocket.triggerEvent = function(evt) {
		if (!WebSocket.collection[evt.socket_id]) {
			WebSocketFactory.removeSocket(evt.socket_id);
			return;
		}
		if (WebSocket.collection[evt.socket_id]['on' + evt.type]) {
			WebSocket.collection[evt.socket_id]['on' + evt.type].apply(window, [evt]);
		}
	}

	WebSocket.prototype.send = function(data)
	{
		data = "" + data;
		this._handler._send(data);
	}

	WebSocket.prototype.close = function()
	{
		this._handler.close();
	}
})();