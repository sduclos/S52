part of s52ui;


//////////////////////////////////////////////////////
//
// Base Class that mimic S52 interface (S52.h)
// UI <--> WebSocket (s52ui.html)
//

class Cmd {
  String    _str;
  int       _id;
  Completer _completer;

  Cmd(String str, int id, Completer completer) {
    _str       = str;
    _id        = id;
    _completer = completer;
  }
}

//abstract class S52 {
class S52 {
  //Completer _completer = null;
  Map _data = JSON.decode('{"id":1,"method":"???","params":["???"]}');
  int _id   = 1;

  get id => ++_id;

  WebSocket _ws;

  //Stopwatch _stopwatch = new Stopwatch();

  // call drawLast() at interval
  Timer _timer     = null;
  bool  _skipTimer = false;
  set skipTimer(val) => _skipTimer = val;

  // FIXME: use Queue to stack S52 cmd .. what happend if returning out-of-order!
  //Queue<String> _queue = new Queue<String>();
  Queue<Cmd> _queue = new Queue<Cmd>();

  // S52 color for UI element
  List UIBCK;  // background (same as DEPDW)
  List UINFF;  // text normal
  List UIBDR;  // border

  // UIAFD;    // blue fill UI area
  // UIAFF;    // brown fill UI area

  // UINFD;  // text conspic important

  // UINFB;  // blue
  // UINFG;  // green
  // UINFR;  // red
  // UINFM;  // magenta
  // UINFO;  // orange

  static const int MAR_SHOW_ERROR             =  0;
  static const int MAR_SHOW_TEXT              =  1;

  static const int MAR_SAFETY_CONTOUR         =  3;  // S52_LINES: selected safety contour (meters) [IMO PS 3.6]
  static const int MAR_SAFETY_DEPTH           =  4;  // S52_POINT: selected safety depth (for sounding color) (meters) [IMO PS 3.7]
  static const int MAR_SHALLOW_CONTOUR        =  5;  // S52_AREAS: selected shallow water contour (meters) (optional) [OFF==S52_MAR_TWO_SHADES]
  static const int MAR_DEEP_CONTOUR           =  6;  // S52_AREAS: selected deepwater contour (meters) (optional)

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

  S52() {}

  Future<bool> initWS(var wsUri) {
    Completer completer = new Completer();

    _ws = new WebSocket(wsUri);
    _ws.onOpen.   listen((Event e)        {completer.complete(true);_drawLastTimer();});
    _ws.onMessage.listen((MessageEvent e) {_rcvMsg(e);});
    _ws.onClose.  listen((CloseEvent e)   {throw '_ws CLOSE:$e';});
    _ws.onError.  listen((Event e)        {throw '_ws ERROR:$e';});

    return completer.future;
  }
  _drawLastTimer() {
    // debug
    //print('_drawLastTimer() ..');
    if (null != _timer)
      return;

    // call drawLast every second (1sec)
    _timer = new Timer.periodic(new Duration(milliseconds: 1000), (timer) {
      if (false == _skipTimer) {
        drawLast().then((ret) {});
      }
    });
  }

  _rcvMsg(MessageEvent evt) {

    // debug
    //print('_rcvMsg:${evt.data}');

    var str = evt.data;
    Map data;
    try {
      data = JSON.decode(str);
    } catch (e) {
      print ('rcvMsg(): malformed JSON throw the parser: $str');
      throw ('rcvMsg(): malformed JSON throw the parser: $str');
    }

    if ("no error" != data["error"]) {
      print ("rcvMsg(): S52 call failed [$data]");
      throw ("rcvMsg(): S52 call failed [$data]");
    }

    // debug
    //_stopwatch.stop();
    //print("roundtrip: ${_stopwatch.elapsedMilliseconds}msec");
    //print('rcvMsg():receive JSON str from libS52: $str');

    // Javascript: MAXINT overflow if (x == x + 1)
    // Dart: same; 2^53 = 9007199254740992 = 52124995687 days @ 0.5s

    //_skipTimer = false;

    if (true == _queue.isNotEmpty) {
      Cmd cmd = _queue.removeFirst();
      if (cmd._id != data["id"]) {
        print('rcvMsg(): failed on key: cmd._id=${cmd._id} data_id=${data["id"]} [data:$data]');
        throw "rcvMsg(): ID mismatch";
      }

      // drawLas()
      _skipTimer = false;

      return cmd._completer.complete(data['result']);
    } else {
      throw ("rcvMsg(): queue empty!");
    }
  }
  Future<List> _sndMsg(String str) {
    if (_ws.readyState != WebSocket.OPEN) {
      throw 'WebSocket not connected, message not sent:$str';
    }
    _skipTimer = true;

    //_stopwatch.reset();
    //_stopwatch.start();

    Completer completer = new Completer();
    Cmd cmd = new Cmd(str, _id, completer);
    _queue.add(cmd);

    if (_ws.bufferedAmount == 0) {
      //_ws.send(str);
      _ws.sendString(str);

      // debug
      //print('_sndMsg:$str');
    } else {
      print('_sndMsg: ERROR SOCK FAIL');
    }

    return completer.future;
  }

  /*
  // alternate way of calling libS52 - one call for all S52.h calls
  Future<List> send(var cmdName, var params) {
    _data["id"    ] = _id;
    _data["method"] = cmdName;
    _data["params"] = params;
    String jsonCmdstr = JSON.encode(_data);

    return _sendMsg(jsonCmdstr);
  }
  */

  ////////////////////// FIXME: REFACTOR use send() ///////////////////////////
  // pro: less code
  // con: lost of info .. doesn't mirror S52.h well since all call info would be
  // littered across s52ui.dart
  Future<List> draw() {
    _data["id"    ] = id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> drawLast() {
    _data["id"    ] = id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> drawBlit(double scale_x, double scale_y, double scale_z, double north) {
    _data["id"    ] = id;
    _data["method"] = "S52_drawBlit";
    _data["params"] = [scale_x, scale_y, scale_z, north];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getMarinerParam(int param) {
    _data["id"    ] = id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> setMarinerParam(int param, double value) {
    _data["id"    ] = id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getPalettesNameList() {
    _data["id"    ] = id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getRGB(String colorName) {
    _data["id"    ] = id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> newOWNSHP(var label) {
    _data["id"    ] = id;
    _data["method"] = "S52_newOWNSHP";
    _data["params"] = [label];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> pushPosition(int objH, double latitude, double longitude, double z) {
    _data["id"    ] = id;
    _data["method"] = "S52_pushPosition";
    _data["params"] = [objH, latitude, longitude, z];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> setVector(int objH, int vecstb, double course, double speed) {
    _data["id"    ] = id;
    _data["method"] = "S52_setVector";
    _data["params"] = [objH, vecstb, course, speed];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> pickAt(double pixels_x, double pixels_y) {
    _data["id"    ] = id;
    _data["method"] = "S52_pickAt";
    _data["params"] = [pixels_x, pixels_y];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getObjList(var cellName, var className) {
    _data["id"    ] = id;
    _data["method"] = "S52_getObjList";
    _data["params"] = [cellName, className];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getAttList(int S57ID) {
    _data["id"    ] = id;
    _data["method"] = "S52_getAttList";
    _data["params"] = [S57ID];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getMarObjH(int S57ID) {
    _data["id"    ] = id;
    _data["method"] = "S52_getMarObjH";
    _data["params"] = [S57ID];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> setVESSELstate(int objH, int vesselSelect, int vestat, int vesselTurn) {
    _data["id"    ] = id;
    _data["method"] = "S52_setVESSELstate";
    _data["params"] = [objH, vesselSelect, vestat, vesselTurn];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getCellNameList() {
    _data["id"    ] = id;
    _data["method"] = "S52_getCellNameList";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> doneCell(encPath) {
    _data["id"    ] = id;
    _data["method"] = "S52_doneCell";
    _data["params"] = [encPath];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> loadCell(encPath) {
    _data["id"    ] = id;
    _data["method"] = "S52_loadCell";
    _data["params"] = [encPath];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> xy2LL(double pixels_x, double pixels_y) {
    _data["id"    ] = id;
    _data["method"] = "S52_xy2LL";
    _data["params"] = [pixels_x, pixels_y];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> setView(double cLat, double cLon, double rNM, double north) {
    _data["id"    ] = id;
    _data["method"] = "S52_setView";
    _data["params"] = [cLat, cLon, rNM, north];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> getView() {
    _data["id"    ] = id;
    _data["method"] = "S52_getView";
    _data["params"] = [];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
  Future<List> setViewPort(int x, int y, int w, int h) {
    _data["id"    ] = id;
    _data["method"] = "S52_setViewPort";
    _data["params"] = [x, y, w, h];
    String jsonCmdstr = JSON.encode(_data);

    return _sndMsg(jsonCmdstr);
  }
}
////////////////////////////////////////////////////////


