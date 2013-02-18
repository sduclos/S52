// s52ui.dart: html/websocket (web) test driver for libS52.so

import 'dart:html';
import 'dart:json';
import 'dart:async';
import 'package:js/js.dart' as js;


//////////////////////////////////////////////////////
//
// Base Class that mimic S52 interface (S52.h)
// UI <--> WebSocket (s52ui.html)
//

S52  s52;

// S52 color for UI element
List _UIBCK;  // background
List _UINFF;  // text
List _UIBDR;  // border


class S52 {
  Completer _completer;
  Map       _data = parse('{"id":1,"method":"???","params":["???"]}');
  int       _id   = 1;

  static const int MAR_SHOW_TEXT            =  1;
  static const int MAR_COLOR_PALETTE        = 15;
  static const int MAR_SCAMIN               = 23;
  static const int MAR_ANTIALIAS            = 24;
  static const int MAR_QUAPNT01             = 25;
  static const int MAR_DISP_LEGEND          = 32;
  static const int MAR_DISP_CALIB           = 36;
  static const int MAR_DISP_DRGARE_PATTERN  = 37;
  static const int MAR_DISP_NODATA_LAYER    = 38;
  static const int MAR_DISP_AFTERGLOW       = 40;
  static const int MAR_DISP_CENTROIDS       = 41;
  static const int MAR_DISP_WORLD           = 42;

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

  // receive JSON str from libS52
  rcvMsg(str) {
    //print('rcvMsg():$str');
    Map data;
    // malformed JSON throw the parser
    try {
      data = parse(str);
    } catch(e) {
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
      throw "ID mismatch";
    }

    ++_id;
    _completer.complete(data['result']);
  }
  // send JSON str to libS52
  _sendMsg(String str) {
    //print('_sendMsg(): $str');
    js.scoped(() {
      js.context.sndS52Msg(str);
      // FIXME: use many (no new .. so could be more efficient!)
      // but need to be deleted on onClose
      js.context.rcvS52Msg = new js.Callback.once(s52.rcvMsg);
    });
  }
    
  Future<List> getMarinerParam(int param) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> setMarinerParam(int param, double value) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getPalettesNameList() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getRGB(String colorName) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> draw() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> drawLast() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }

}
////////////////////////////////////////////////////////


  void outputMsg(String msg) {
    //var output = query('#output1');
    //var output = query('#text');
    //output.text = "${output.text}\n${msg}";
    print(msg);
  }

  void _handleInput(int param, double value) {
    bool checked = false;
    switch (param) {
      case S52.MAR_SHOW_TEXT            : //=  1;
      case S52.MAR_SCAMIN               : //= 23;
      case S52.MAR_ANTIALIAS            : //= 24;
      case S52.MAR_QUAPNT01             : //= 25;
      case S52.MAR_DISP_LEGEND          : //= 32;
      case S52.MAR_DISP_CALIB           : //= 36;
      case S52.MAR_DISP_DRGARE_PATTERN  : //= 37;
      case S52.MAR_DISP_NODATA_LAYER    : //= 38;
      case S52.MAR_DISP_AFTERGLOW       : //= 40;
      case S52.MAR_DISP_CENTROIDS       : //= 41;
      case S52.MAR_DISP_WORLD           : //= 42;

        //checked = query("#i$param").checked;

        InputElement i = query("#i$param");
        checked = i.checked;
        break;

      case S52.MAR_DISP_CATEGORY        :  //= 14;
        //S52_MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000000
        //S52_MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0000001
        //S52_MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0000010
        //S52_MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0000100
        //checked = query("#c$subParam").checked;
        //break;

      case S52.MAR_DISP_LAYER_LAST       : //= 27;
        //S52_MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0001000
        //S52_MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010000
        //S52_MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0100000
        //S52_MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 1000000
        //checked = query("#l$subParam").checked;
        //break;

      case S52.CMD_WRD_FILTER            : //= 33;
        //S52_CMD_WRD_FILTER_SY          =        1;  // 1 << 0; 000001 - SY
        //S52_CMD_WRD_FILTER_LS          =        2;  // 1 << 1; 000010 - LS
        //S52_CMD_WRD_FILTER_LC          =        4;  // 1 << 2; 000100 - LC
        //S52_CMD_WRD_FILTER_AC          =        8;  // 1 << 3; 001000 - AC
        //S52_CMD_WRD_FILTER_AP          =       16;  // 1 << 4; 010000 - AP
        //S52_CMD_WRD_FILTER_TX          =       32;  // 1 << 5; 100000 - TE & TX
        //checked = query("#f$subParam").checked;
        s52.setMarinerParam(param, value).then((ret) {
          if (1 == ret[0]) {
            print("OK");
            s52.draw();
          }
        });
        return;

      default:
        throw "_handleInput(): param invalid";
    }

    double val = (true == checked) ? 1.0 : 0.0;
    s52.setMarinerParam(param, val).then((ret) {
      if (1 == ret[0]) {
        print("OK");
        s52.draw();
      }
    });
  }

  Future<bool> _getS52UIcolor() {
    Completer completer = new Completer();

    // get S52 UI background color
    s52.getRGB("UIBCK").then((UIBCK) {
      _UIBCK = UIBCK;
      // get S52 UI Text Color
      s52.getRGB("UINFF").then((UINFF) {
        _UINFF = UINFF;
        // get S52 UI Border Color
        s52.getRGB("UIBDR").then((UIBDR) {
          _UIBDR = UIBDR;
          completer.complete(true);
        });
      });
    });
    return completer.future;
  }

  Future<bool> _setUIcolor() {
    Completer completer = new Completer();
    _getS52UIcolor().then((value) {
      // set S52 UI background color
      query(".scrollTableL").style.backgroundColor =
           "rgba(${_UIBCK[0]},${_UIBCK[1]},${_UIBCK[2]}, 0.6)";
      // set S52 UI Border Color
      queryAll("hr").forEach((s) => s.style.backgroundColor =
           "rgb(${_UIBDR[0]},${_UIBDR[1]},${_UIBDR[2]})");
      // set S52 UI Text Color
      queryAll("span").forEach((s) => s.style.color =
           "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");

      completer.complete(true);
    });
    return completer.future;
  }

  void _updateUIcol(int idx) {
    s52.setMarinerParam(S52.MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
      _setUIcolor();
      s52.draw().then((ret) {
        s52.drawLast();
      });
   });
  }

  void _insertPalR(String txt, int idx) {
    SpanElement      s = new SpanElement();
    s.text = txt;
    TableCellElement c = new TableCellElement();
    c.onClick.listen((ev) => _updateUIcol(idx));
    c.nodes.add(s);
    // add at the end
    TableElement t = query("#tableR");
    TableRowElement r = t.insertRow(-1);
    r.nodes.add(c);
  }

  void _listPal(MouseEvent e) {
    // start color highlight animation
    //query("#td_buttonCell").style.animationIterationCount = '1';

    // clear #tableR(ight)
    TableElement  t = query("#tableR");
    //bool nr = query("#tableR").rows.isEmpty;
    bool nr = t.rows.isEmpty;
    while (!nr) {
      //query("#tableR")
      t.deleteRow(0);
      nr = t.rows.isEmpty;
    }
    //= t.deleteRow(1);
    s52.getPalettesNameList().then((palNmList) {
      for (int i=0; i<palNmList.length; ++i) {
        _insertPalR(palNmList[i], i);
      }

      List l = queryAll("span");
      l.forEach((s) => s.style.textFillColor =
          "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");

      // stop (abruptly) color animation
      //query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }
  //*/



///////////////////////////////////////
//
// Init UI from libS52 state
//

  List _checkButton = [
                       S52.MAR_SHOW_TEXT,S52.MAR_SCAMIN,S52.MAR_ANTIALIAS,
                       S52.MAR_QUAPNT01,S52.MAR_DISP_LEGEND,S52.MAR_DISP_CALIB,
                       S52.MAR_DISP_DRGARE_PATTERN,S52.MAR_DISP_NODATA_LAYER,
                       S52.MAR_DISP_AFTERGLOW,S52.MAR_DISP_CENTROIDS,S52.MAR_DISP_WORLD
                       ];

//*
void _initCheckBox(List lst, int idx, String prefix) {
  if (idx < _checkButton.length) {
    int el = _checkButton[idx];
    s52.getMarinerParam(el).then((ret) {
      InputElement i = query("#$prefix$el");
      i
        ..checked = (1.0 == ret[0])? true : false
        ..onClick.listen((ev) => outputMsg("id:'$prefix$el'"))
        ..onClick.listen((ev) => _handleInput(el, 0.0));

      // recursion
      _initCheckBox(lst, idx+1, prefix);
    });
  }
}

void _initUI() {
  //_setUIcolor();

  //*
  _setUIcolor().then((ret) {
  // S52_MAR_CMD_WRD_FILTER(33)
  s52.getMarinerParam(S52.CMD_WRD_FILTER).then((ret) {
    [S52.CMD_WRD_FILTER_SY,S52.CMD_WRD_FILTER_LS,S52.CMD_WRD_FILTER_LC,
     S52.CMD_WRD_FILTER_AC,S52.CMD_WRD_FILTER_AP,S52.CMD_WRD_FILTER_TX
    ].forEach((el) {

      int filter = ret[0].toInt();
      InputElement i = query("#f$el");
      i
        ..checked = (0 != (filter & el))? true : false
        ..onClick.listen((ev) => outputMsg("id:'f$el'"))
        ..onClick.listen((ev) => _handleInput(S52.CMD_WRD_FILTER, el));
    });

    //S52_MAR_DISP_CATEGORY(14)
    s52.getMarinerParam(S52.MAR_DISP_CATEGORY).then((ret) {
      [S52.MAR_DISP_CATEGORY_BASE,
       S52.MAR_DISP_CATEGORY_STD,
       S52.MAR_DISP_CATEGORY_OTHER,
       S52.MAR_DISP_CATEGORY_SELECT
      ].forEach((el) {

        if (0 == el) { // S52_MAR_DISP_CATEGORY_BASE is alway ON
          InputElement i = query("#c$el");
          i
            ..checked  = true
            ..disabled = true;
        } else {
          int filter = ret[0].toInt();
          InputElement i = query("#c$el");
          i
            ..checked = (0 != (filter & el))? true : false
            ..onClick.listen((ev) => outputMsg("id:'c$el'"))
            ..onClick.listen((ev) => _handleInput(S52.MAR_DISP_CATEGORY, el.toDouble()));
        }
      });

      // S52_MAR_DISP_LAYER_LAST(27)
      s52.getMarinerParam(S52.MAR_DISP_LAYER_LAST).then((ret) {
        [S52.MAR_DISP_LAYER_LAST_NONE,
         S52.MAR_DISP_LAYER_LAST_STD,
         S52.MAR_DISP_LAYER_LAST_OTHER,
         S52.MAR_DISP_LAYER_LAST_SELECT
        ].forEach((el) {

           int filter = ret[0].toInt();
           InputElement i = query("#l$el");
           i
             ..checked = (0 != (filter & el))? true : false
             ..onClick.listen((ev) => outputMsg("id:'l$el'"))
             ..onClick.listen((ev) => _handleInput(S52.MAR_DISP_LAYER_LAST, el.toDouble()));
        });

        query("#td_buttonCell")
          ..onClick.listen((ev) => outputMsg("id:'td_buttonCell'"))
          ..onClick.listen((ev) => _listPal(ev));

        int startIdx = 0;
        _initCheckBox(_checkButton, startIdx, "i");

      });
    });
  });
  });
  // */
}


///////////////////////////////////////
//
// Main
//

void main() {
  //_ownshp = new s52obj("OWNSHP"); // GPS & Gyro
  s52 = new S52();
  
  // FIXME: should not be needed
  // wait for loading .js to settle
  //window.setTimeout(_initUI, 100);
  //window.setTimeout(_initUI, 200);
  window.setTimeout(_initUI, 500);

  // WebSocket reply something (meaningless!) when making initial connection
  // read it! (and maybe do something)
  js.scoped(() {
    js.context.rcvS52Msg = new js.Callback.once(s52.rcvMsg);
  });
}


/*
s52obj _ownshp;

class s52obj {
  String objName;
  Map data = JSON.parse('{"id":0,"method":"???","params":["???"]}');
  int objH = 0;
  int cmd = 0; // FIXME: use const

  s52obj(this.objName) {
    window.on.deviceOrientation.add((eventData) {_devOrient(eventData);});
    window.setInterval(this.setPosition, 500);
    //initDeviceOrientationEvent(String type, bool bubbles, bool cancelable,
    //num alpha, num beta, num gamma, bool absolute)
  }
  _devOrient(DeviceOrientationEvent eventData) {
    if (0 != objH)
      setVector(0, eventData.gamma, 0.0);
  }
  setPosition() {
    if (0 != objH) {
      window.navigator.geolocation.getCurrentPosition((Geoposition position) {
        data["method"] = "S52_pushPosition";
        data["params"] = [objH,position.coords.latitude,position.coords.longitude,0.0];
        String jsonCmdstr = JSON.stringify(data);
        cmd = 2;
        //outputMsg('setPosition():$jsonCmdstr');
        ws.send(jsonCmdstr);
      });
    }
  }
  setVector(int vecstb, double course, double speed) {
    data["method"] = "S52_setVector";
    data["params"] = [objH,vecstb,course,speed];
    String jsonCmdstr = JSON.stringify(data);
    cmd = 3;
    ws.send(jsonCmdstr);
  }
}
*/
