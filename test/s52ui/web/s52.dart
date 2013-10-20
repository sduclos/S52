part of s52ui;

//////////////////////////////////////////////////////
//
// Base Class that mimic S52 interface (S52.h)
// UI <--> WebSocket (s52ui.html)
//

//abstract class S52 {
class S52 {
  Completer _completer = null;
  //Map       _data      = parse('{"id":1,"method":"???","params":["???"]}');
  Map       _data      = JSON.decode('{"id":1,"method":"???","params":["???"]}');
  int       _id        = 1;
  //WebSocket _ws;

  Stopwatch _stopwatch = new Stopwatch();

  // call drawLast() at interval
  Timer _timer        = null;
  bool  _cancelTimer  = false;

  //var width;
  //var height;

  // S52 color for UI element
  List UIBCK;  // background
  List UINFF;  // text
  List UIBDR;  // border


  static const int MAR_SHOW_TEXT              =  1;
  static const int MAR_COLOR_PALETTE          = 15;
  static const int MAR_SCAMIN                 = 23;
  static const int MAR_ANTIALIAS              = 24;
  static const int MAR_QUAPNT01               = 25;
  static const int MAR_ROT_BUOY_LIGHT         = 28;
  static const int MAR_DISP_LEGEND            = 32;
  static const int MAR_DISP_CALIB             = 36;
  static const int MAR_DISP_DRGARE_PATTERN    = 37;
  static const int MAR_DISP_NODATA_LAYER      = 38;
  static const int MAR_DISP_AFTERGLOW         = 40;
  static const int MAR_DISP_CENTROIDS         = 41;
  static const int MAR_DISP_WORLD             = 42;

  static const int MAR_DISP_CATEGORY          = 14;
  static const int MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000000
  static const int MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0000001
  static const int MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0000010
  static const int MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0000100

  static const int MAR_DISP_LAYER_LAST        = 27;
  static const int MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0001000
  static const int MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010000
  static const int MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0100000
  static const int MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 1000000

  static const int CMD_WRD_FILTER             = 33;
  static const int CMD_WRD_FILTER_SY          =        1;  // 1 << 0; 000001 - SY
  static const int CMD_WRD_FILTER_LS          =        2;  // 1 << 1; 000010 - LS
  static const int CMD_WRD_FILTER_LC          =        4;  // 1 << 2; 000100 - LC
  static const int CMD_WRD_FILTER_AC          =        8;  // 1 << 3; 001000 - AC
  static const int CMD_WRD_FILTER_AP          =       16;  // 1 << 4; 010000 - AP
  static const int CMD_WRD_FILTER_TX          =       32;  // 1 << 5; 100000 - TE & TX

  S52() {
    js.context['websocket'].onmessage = new js.Callback.many(rcvMsg);

    _drawLastTimer();
  }
  _drawLastTimer() {
    if (null != _timer)
      return;

    // call drawLast every second (2sec)
    _timer = new Timer.periodic(new Duration(milliseconds: 2000), (timer) {
      drawLast().then((ret) {});

      //drawLast()
      //  .then(      (ret) {})
      //  .catchError((err) {print('_drawLastTimer(): catchError ... %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%');} );

      // FIXME: can't make onError / catchError work
      //drawLast().then      (    (ret)          {print('_drawLastTimer(): then       ... &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&');},
      //                 onError: (AsyncError e) {print('_drawLastTimer(): onError:   ... ##############################');})
      //          .catchError(     (e)           {print('_drawLastTimer(): catchError ... %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%');});
    });
  }

  //rcvMsg(MessageEvent evt) {
  rcvMsg(var evt) {
    // receive S52/JSON msg from WebSocket (Cordova) in s52ui.html
    //print('............................str:${evt.data}');
    var str = evt.data;
    Map data;
    try {
      data = JSON.decode(str);
    } catch(e) {
      print('rcvMsg(): malformed JSON throw the parser: $str');
      return;
    }

    if (null == data["error"]) {
      print('rcvMsg(): failed NO key: "error" [${str}]');
      return;
    }
    if ("no error" != data["error"]) {
      print("rcvMsg(): S52 call failed");
      return;
    }

    if (_id != data["id"]) {
      print('rcvMsg(): failed on key: _id=$_id data_id=${data["id"]}');
      throw "rcvMsg(): ID mismatch";
    }

    _stopwatch.stop();
    print("roundtrip: ${_stopwatch.elapsedMilliseconds}msec");
    //print('rcvMsg():receive JSON str from libS52: $str');


    ++_id;
    _completer.complete(data['result']);

    // restart timer if need be
    _drawLastTimer();
  }
  Future<List> _sendMsg(String str) {
    // send S52/JSON msg to WebSocket (Cordova) in s52ui.html
    _stopwatch.reset();
    _stopwatch.start();

    _completer = new Completer();

    //js.context.sndS52Msg(str);
    //js.context.websocket.send(str);
    js.context['websocket'].send(str);

    //_ws.send(str);
    //if (_ws.readyState == WebSocket.OPEN) {
    //  _ws.send(str);
    //} else {
    //  print('WebSocket not connected, message not sent:$str');
    //}

    return _completer.future;
  }

  // alternate way of calling libS52 - one call for all S52.h calls
  Future<List> send(var cmdName, var params) {
    _data["id"    ] = _id;
    _data["method"] = cmdName;
    _data["params"] = params;
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }


  ////////////////////// FIXME: REFACTOR use send() ///////////////////////////
  // pro: less code
  // con: lost of info .. doesn't mirror S52.h well since all call info would be
  // littered across s52ui.dart
  Future<List> drawLast() {
    if (null!=_completer && false==_completer.isCompleted) {
      print("drawLast(): _completer NOT completed XXXXXXXXX");
      _timer.cancel();
      _timer = null;
      throw "drawLast(): _completer is busy";
      //throw new Exception('drawLast(): _completer is busy');
      //throw new AsyncError('error');
      //return _completer.future;
    }

    _data["id"    ] = _id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getMarinerParam(int param) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setMarinerParam(int param, double value) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getPalettesNameList() {
    _data["id"    ] = _id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getRGB(String colorName) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  // FIXME: mistery .. no call to EGL and this call still work it seem
  // Now call to EGL is done trough callback from libS52
  Future<List> draw() {
    _data["id"    ] = _id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> newOWNSHP(var label) {
    _data["id"    ] = _id;
    _data["method"] = "S52_newOWNSHP";
    _data["params"] = [label];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> pushPosition(int objH, double latitude, double longitude, double z) {
    _data["id"    ] = _id;
    _data["method"] = "S52_pushPosition";
    _data["params"] = [objH,latitude,longitude,z];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setVector(int objH, int vecstb, double course, double speed) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setVector";
    _data["params"] = [objH,vecstb,course,speed];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> pickAt(double pixels_x, double pixels_y) {
    _data["id"    ] = _id;
    _data["method"] = "S52_pickAt";
    _data["params"] = [pixels_x,pixels_y];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getObjList(var cellName, var className) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getObjList";
    _data["params"] = [cellName,className];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getAttList(int S57ID) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getAttList";
    _data["params"] = [S57ID];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getMarObjH(int S57ID) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getMarObjH";
    _data["params"] = [S57ID];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setVESSELstate(int objH, int vesselSelect, int vestat, int vesselTurn) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setVESSELstate";
    _data["params"] = [objH,vesselSelect,vestat,vesselTurn];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getCellNameList() {
    _data["id"    ] = _id;
    _data["method"] = "S52_getCellNameList";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> doneCell(encPath) {
    _data["id"    ] = _id;
    _data["method"] = "S52_doneCell";
    _data["params"] = [encPath];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> loadCell(encPath) {
    _data["id"    ] = _id;
    _data["method"] = "S52_loadCell";
    _data["params"] = [encPath];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> drawBlit(double scale_x, double scale_y, double scale_z, double north) {
    _data["id"    ] = _id;
    _data["method"] = "S52_drawBlit";
    _data["params"] = [scale_x,scale_y,scale_z,north];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> xy2LL(double pixels_x, double pixels_y) {
    _data["id"    ] = _id;
    _data["method"] = "S52_xy2LL";
    _data["params"] = [pixels_x,pixels_y];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setView(double cLat, double cLon, double rNM, double north) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setView";
    _data["params"] = [cLat,cLon,rNM,north];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getView() {
    _data["id"    ] = _id;
    _data["method"] = "S52_getView";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setViewPort(int x, int y, int w, int h) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setViewPort";
    _data["params"] = [x,y,w,h];
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
}
////////////////////////////////////////////////////////



