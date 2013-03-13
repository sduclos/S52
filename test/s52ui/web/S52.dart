part of s52ui;



//////////////////////////////////////////////////////
//
// Base Class that mimic S52 interface (S52.h)
// UI <--> WebSocket (s52ui.html)
//

class S52 {
  Completer _completer;
  Map       _data = parse('{"id":1,"method":"???","params":["???"]}');
  int       _id   = 1;

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

  // receive S52/JSON msg from WebSocket (Cordova) in s52ui.html
  rcvMsg(str) {
    print('rcvMsg():receive JSON str from libS52: $str');
    Map data;
    try {
      data = parse(str);
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

    ++_id;
    _completer.complete(data['result']);
  }
  // send S52/JSON msg to WebSocket (Cordova) in s52ui.html
  Future<List> _sendMsg(String str) {
    _completer = new Completer();
    // send JSON str to libS52
    //print('_sendMsg(): $str');
    js.scoped(() {
      // first hookup callbaqck
      // FIXME: use many (no new .. so could be more efficient!)
      // but need to be deleted on onClose
      js.context.rcvS52Msg = new js.Callback.once(s52.rcvMsg);

      // then send
      js.context.sndS52Msg(str);
    });
    
    return _completer.future;
  }
    
  Future<List> getMarinerParam(int param) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = stringify(_data);
    
    return _sendMsg(jsonCmdstr);
  }
  Future<List> setMarinerParam(int param, double value) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getPalettesNameList() {
    _data["id"    ] = _id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getRGB(String colorName) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> draw() {
    _data["id"    ] = _id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> drawLast() {
    _data["id"    ] = _id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> newOWNSHP(var label) {
    _data["id"    ] = _id;
    _data["method"] = "S52_newOWNSHP";
    _data["params"] = [label];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setPosition(int objH, double latitude, double longitude, double z) {
    _data["id"    ] = _id;
    _data["method"] = "S52_pushPosition";
    _data["params"] = [objH,latitude,longitude,z];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setVector(int objH, int vecstb, double course, double speed) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setVector";
    _data["params"] = [objH,vecstb,course,speed];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> pickAt(double pixels_x, double pixels_y) {
    _data["id"    ] = _id;
    _data["method"] = "S52_pickAt";
    _data["params"] = [pixels_x,pixels_y];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getObjList(var cellName, var className) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getObjList";
    _data["params"] = [cellName,className];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getAttList(int S57ID) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getAttList";
    _data["params"] = [S57ID];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getMarObjH(int S57ID) {
    _data["id"    ] = _id;
    _data["method"] = "S52_getMarObjH";
    _data["params"] = [S57ID];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> setVESSELstate(int objH, int vesselSelect, int vestat, int vesselTurn) {
    _data["id"    ] = _id;
    _data["method"] = "S52_setVESSELstate";
    _data["params"] = [objH,vesselSelect,vestat,vesselTurn];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> getCellNameList() {
    _data["id"    ] = _id;
    _data["method"] = "S52_getCellNameList";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> doneCell(encPath) {
    _data["id"    ] = _id;
    _data["method"] = "S52_doneCell";
    _data["params"] = [encPath];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }
  Future<List> loadCell(encPath) {
    _data["id"    ] = _id;
    _data["method"] = "S52_loadCell";
    _data["params"] = [encPath];
    String jsonCmdstr = stringify(_data);

    return _sendMsg(jsonCmdstr);
  }

}
////////////////////////////////////////////////////////



