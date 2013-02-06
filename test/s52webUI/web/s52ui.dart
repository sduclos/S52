// s52ui.dart: html/websocket (web) test driver for libS52.so

import 'dart:html';
import 'dart:json';
import 'dart:async';

/////////////////////////////////////////////
//
// WebSocket <--> libS52
//
/*
class s52obj {
  String objName;
  WebSocket ws = new WebSocket('ws://127.0.0.1:2950');
  Map data = JSON.parse('{"id":0,"method":"???","params":["???"]}');
  int objH = 0;
  int cmd = 0; // FIXME: use const

  s52obj(this.objName) {
    ws.on.open.add   ((Event e) {_setNew(objName);});
    ws.on.close.add  ((Event e) {_close(e);});
    ws.on.error.add  ((Event e) {_error(e);});
    ws.on.message.add((MessageEvent e) {_parseMsg(e);});
    window.on.deviceOrientation.add((eventData) {_devOrient(eventData);});
    window.setInterval(this.setPosition, 500);
    //initDeviceOrientationEvent(String type, bool bubbles, bool cancelable,
    //num alpha, num beta, num gamma, bool absolute)
  }
  _error(Event e) {
    outputMsg('s52obj: sock error');
    data["method"] = "S52_delMarObj";
    data["params"] = [objH];
    String jsonCmdstr = JSON.stringify(data);
    cmd = 3;
    ws.send(jsonCmdstr);
  }
  _close(Event e) {
    outputMsg('s52obj:sock close');
  }
  _setNew(String label) {
    data["method"] = "S52_newOWNSHP";
    data["params"] = ["$label"];
    String jsonCmdstr = JSON.stringify(data);
    cmd = 1;
    ws.send(jsonCmdstr);
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
  _devOrient(DeviceOrientationEvent eventData) {
    if (0 != objH)
      setVector(0, eventData.gamma, 0.0);
  }
  _parseMsg(MessageEvent e) {
    Map data = JSON.parse(e.data);
    if ("no error" == data["error"]) {
      if (1 == cmd) {
        objH = data["result"][0];
        outputMsg('1 - setNew() call returned:$objH');
        //setPosition();
      }
      if (2 == cmd) {
        int ret = data["result"][0];
        outputMsg('2 - setPosition() call returned:$ret');
        //setVector(0, 45.0, 12.0);
      }
      if (3 == cmd) {
        int ret = data["result"][0];
        outputMsg('3 - setVector() call returned:$ret');
      }
    } else {
      outputMsg('X - call returned:ERROR');
    }
    cmd = 0;
  }
}
*/

S52  s52;
//s52obj _ownshp;

//String S52_WS_SRVR = 'ws://127.0.0.1:2950';    // localhost
//String S52_WS_SRVR = 'ws://192.168.1.67:2950'; // laptop
String S52_WS_SRVR = 'ws://192.168.1.66:2950'; // Xoom
List _UIBCK;
List _UINFF;
List _UIBDR;


const int S52_MAR_SHOW_TEXT            =  1;
const int S52_MAR_COLOR_PALETTE        = 15;
const int S52_MAR_SCAMIN               = 23;
const int S52_MAR_ANTIALIAS            = 24;
const int S52_MAR_QUAPNT01             = 25;
const int S52_MAR_DISP_LEGEND          = 32;
const int S52_MAR_DISP_CALIB           = 36;
const int S52_MAR_DISP_DRGARE_PATTERN  = 37;
const int S52_MAR_DISP_NODATA_LAYER    = 38;
const int S52_MAR_DISP_AFTERGLOW       = 40;
const int S52_MAR_DISP_CENTROIDS       = 41;
const int S52_MAR_DISP_WORLD           = 42;

const int S52_MAR_DISP_CATEGORY          = 14;
const int S52_MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000000
const int S52_MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0000001
const int S52_MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0000010
const int S52_MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0000100

const int S52_MAR_DISP_LAYER_LAST        = 27;
const int S52_MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0001000
const int S52_MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010000
const int S52_MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0100000
const int S52_MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 1000000

const int S52_CMD_WRD_FILTER             = 33;
const int S52_CMD_WRD_FILTER_SY          =        1;  // 1 << 0; 000001 - SY
const int S52_CMD_WRD_FILTER_LS          =        2;  // 1 << 1; 000010 - LS
const int S52_CMD_WRD_FILTER_LC          =        4;  // 1 << 2; 000100 - LC
const int S52_CMD_WRD_FILTER_AC          =        8;  // 1 << 3; 001000 - AC
const int S52_CMD_WRD_FILTER_AP          =       16;  // 1 << 4; 010000 - AP
const int S52_CMD_WRD_FILTER_TX          =       32;  // 1 << 5; 100000 - TE & TX

class S52 {
  Completer _completer;
  WebSocket _ws;
  //WebSocket _ws   = new WebSocket('ws://192.168.1.66:2950');
  Map       _data = parse('{"id":0,"method":"???","params":["???"]}');
  int       _id   = 1;

  S52(WebSocket ws) {
    _ws = ws;
    _ws.on.open.add   ((Event e)        {_open(e);    });
    _ws.on.close.add  ((Event e)        {_close(e);   });
    _ws.on.error.add  ((Event e)        {_error(e);   });
    _ws.on.message.add((MessageEvent e) {_parseMsg(e);});
  }

  _open (Event e) {
    outputMsg('s52:sock open');
  }
  _close(Event e) {
    outputMsg('s52:sock close');
  }
  _error(Event e) {
    outputMsg('s52:sock error');
  }
  _parseMsg(MessageEvent e) {
    //print('_parseMsg():${e.data}');
    Map data = parse(e.data);
    if (_id != data["id"]) {
      print('_parseMsg(): failed on key: _id=$_id data_id=${data["id"]}');
      throw "ID mismatch";
    }
    if ("no error" != data["error"]) {
      print('_parseMsg(): failed on key: "error"');
      throw "S52 call failed";
    }
    ++_id;
    _completer.complete(data['result']);
  }

  Future<List> getMarinerParam(int param) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> setMarinerParam(int param, double value) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getPalettesNameList() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getRGB(String colorName) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> draw() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> drawLast() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = stringify(_data);
    _ws.send(jsonCmdstr);

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
      case S52_MAR_SHOW_TEXT            : //=  1;
      case S52_MAR_SCAMIN               : //= 23;
      case S52_MAR_ANTIALIAS            : //= 24;
      case S52_MAR_QUAPNT01             : //= 25;
      case S52_MAR_DISP_LEGEND          : //= 32;
      case S52_MAR_DISP_CALIB           : //= 36;
      case S52_MAR_DISP_DRGARE_PATTERN  : //= 37;
      case S52_MAR_DISP_NODATA_LAYER    : //= 38;
      case S52_MAR_DISP_AFTERGLOW       : //= 40;
      case S52_MAR_DISP_CENTROIDS       : //= 41;
      case S52_MAR_DISP_WORLD           : //= 42;

        //checked = query("#i$param").checked;

        InputElement i = query("#i$param");
        checked = i.checked;
        break;

      case S52_MAR_DISP_CATEGORY        :  //= 14;
        //S52_MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000000
        //S52_MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0000001
        //S52_MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0000010
        //S52_MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0000100
        //checked = query("#c$subParam").checked;
        //break;

      case S52_MAR_DISP_LAYER_LAST       : //= 27;
        //S52_MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0001000
        //S52_MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010000
        //S52_MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0100000
        //S52_MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 1000000
        //checked = query("#l$subParam").checked;
        //break;

      case S52_CMD_WRD_FILTER            : //= 33;
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
    s52.setMarinerParam(S52_MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
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
    c.on.click.add((ev) => _updateUIcol(idx));
    c.nodes.add(s);
    // add at the end
    TableElement t = query("#tableR");
    TableRowElement r = t.insertRow(-1);
    r.nodes.add(c);
  }

  void _listPal(MouseEvent e) {
    // start color highlight animation
    query("#td_buttonCell").style.animationIterationCount = '1';

    // clear #tableR(ight)
    TableElement  t = query("#tableR");
    //bool nr = query("#tableR").rows.isEmpty;
    bool nr = t.rows.isEmpty;
    while (!nr) {
      //query("#tableR")
      t.deleteRow(0);
      nr = t.rows.isEmpty;
    }
    //while (NULL != t)
    //= t.deleteRow(1);
    s52.getPalettesNameList().then((palNmList) {
      for (int i=0; i<palNmList.length; ++i) {
        _insertPalR(palNmList[i], i);
      }

      List l = queryAll("span");
      l.forEach((s) => s.style.textFillColor =
          "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");

      // stop (abruptly) color animation
      query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }
  //*/


List _checkButton = [
  S52_MAR_SHOW_TEXT,S52_MAR_SCAMIN,S52_MAR_ANTIALIAS,
  S52_MAR_QUAPNT01,S52_MAR_DISP_LEGEND,S52_MAR_DISP_CALIB,
  S52_MAR_DISP_DRGARE_PATTERN,S52_MAR_DISP_NODATA_LAYER,
  S52_MAR_DISP_AFTERGLOW,S52_MAR_DISP_CENTROIDS,S52_MAR_DISP_WORLD
];


///////////////////////////////////////
//
// Init UI from libS52 state
//

//*
void _initCheckBox(List lst, int idx, String prefix) {
  if (idx < _checkButton.length) {
    int el = _checkButton[idx];
    s52.getMarinerParam(el).then((ret) {
      InputElement i = query("#$prefix$el");
      i
        ..checked = (1.0 == ret[0])? true : false
        ..on.click.add((ev) => outputMsg("id:'$prefix$el'"))
        ..on.click.add((ev) => _handleInput(el, 0.0));

      // recursion
      _initCheckBox(lst, idx+1, prefix);
    });
  }
}
//*/

void _init() {
  _setUIcolor().then((ret) {
  //_setUIcolor();

  //*
  // S52_MAR_CMD_WRD_FILTER(33)
  s52.getMarinerParam(S52_CMD_WRD_FILTER).then((ret) {
    [S52_CMD_WRD_FILTER_SY,S52_CMD_WRD_FILTER_LS,S52_CMD_WRD_FILTER_LC,
     S52_CMD_WRD_FILTER_AC,S52_CMD_WRD_FILTER_AP,S52_CMD_WRD_FILTER_TX
    ].forEach((el) {

      int filter = ret[0].toInt();
      InputElement i = query("#f$el");
      i
        ..checked = (0 != (filter & el))? true : false
        ..on.click.add((ev) => outputMsg("id:'f$el'"))
        ..on.click.add((ev) => _handleInput(S52_CMD_WRD_FILTER, el));
    });

    //S52_MAR_DISP_CATEGORY(14)
    s52.getMarinerParam(S52_MAR_DISP_CATEGORY).then((ret) {
      [S52_MAR_DISP_CATEGORY_BASE,
       S52_MAR_DISP_CATEGORY_STD,
       S52_MAR_DISP_CATEGORY_OTHER,
       S52_MAR_DISP_CATEGORY_SELECT
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
            ..on.click.add((ev) => outputMsg("id:'c$el'"))
            ..on.click.add((ev) => _handleInput(S52_MAR_DISP_CATEGORY, el.toDouble()));
        }
      });

      // S52_MAR_DISP_LAYER_LAST(27)
      s52.getMarinerParam(S52_MAR_DISP_LAYER_LAST).then((ret) {
        [S52_MAR_DISP_LAYER_LAST_NONE,
         S52_MAR_DISP_LAYER_LAST_STD,
         S52_MAR_DISP_LAYER_LAST_OTHER,
         S52_MAR_DISP_LAYER_LAST_SELECT
        ].forEach((el) {

           int filter = ret[0].toInt();
           InputElement i = query("#l$el");
           i
             ..checked = (0 != (filter & el))? true : false
             ..on.click.add((ev) => outputMsg("id:'l$el'"))
             ..on.click.add((ev) => _handleInput(S52_MAR_DISP_LAYER_LAST, el.toDouble()));
        });

        query("#td_buttonCell")
          ..on.click.add((ev) => outputMsg("id:'button'"))
          ..on.click.add((ev) => _listPal(ev));

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

  //_ownshp = new s52obj("OWNSHP");
  s52 = new S52(new WebSocket(S52_WS_SRVR));


  //_init();

  // FIXME: should not be needed
  // wait for s52ui.js to finish
  //window.setTimeout(_init, 100);
  window.setTimeout(_init, 200);
  //window.setTimeout(_init, 500);
}
