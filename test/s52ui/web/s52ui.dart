// s52ui.dart: html/websocket test driver for libS52.so

import 'dart:html';
import 'dart:json';


/////////////////////////////////////////////
//
// WebSocket <--> libS52
//

class s52obj {
  String objName;
  WebSocket ws;
  Map data = JSON.parse('{"id":1,"method":"???","params":["???"]}');
  int objH = 0;
  int cmd = 0; // FIXME: use const

  s52obj(this.objName, this.ws) {
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

class s52ui {
  Completer _completer;
  WebSocket _ws   = new WebSocket('ws://127.0.0.1:2950');
  Map       _data = JSON.parse('{"id":0,"method":"???","params":["???"]}');
  int       _id   = 1;

  s52ui() {
    _ws.on.open.add   ((Event e) {_open(e);});
    _ws.on.close.add  ((Event e) {_close(e);});
    _ws.on.error.add  ((Event e) {_error(e);});
    _ws.on.message.add((MessageEvent e) {_parseMsg(e);});
  }
  _open(Event e) {
    outputMsg('s52ui:sock open');
  }
  _close(Event e) {
    outputMsg('s52ui:sock close');
  }
  _error(Event e) {
    outputMsg('s52ui:sock error');
  }
  _parseMsg(MessageEvent e) {
    //print('_parseMsg():${e.data}');
    Map data = JSON.parse(e.data);
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
  //Future<double> getMarinerParam(int param) {
  Future<List> getMarinerParam(int param) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getMarinerParam";
    _data["params"] = [param];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  //Future<int> setMarinerParam(int param, double value) {
  Future<List> setMarinerParam(int param, double value) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_setMarinerParam";
    _data["params"] = [param, value];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getPalettesNameList() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getPalettesNameList";
    _data["params"] = [];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getRGB(String colorName) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_getRGB";
    _data["params"] = [colorName];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> draw() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_draw";
    _data["params"] = [];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }
  Future<List> drawLast() {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_drawLast";
    _data["params"] = [];
    String jsonCmdstr = JSON.stringify(_data);
    _ws.send(jsonCmdstr);

    return _completer.future;
  }

}
////////////////////////////////////////////////////////


  void outputMsg(String msg) {
    var output = query('#output1');
    //var output = query('#text');
    output.text = "${output.text}\n${msg}";
  }

  void _handleInput(int param, double subParam) {
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
        checked = query("#i$param").checked;
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
        _s52ui.setMarinerParam(param, subParam).then((ret) {
          if (1 == ret[0]) { print("OK"); }
        });
        return;

      default:
        throw "_handleInput(): param invalid";
    }

    double val = (true == checked) ? 1.0 : 0.0;
    _s52ui.setMarinerParam(param, val).then((ret) {
      if (1 == ret[0]) { print("OK"); }
    });
  }

  void _test1(MouseEvent e) {
    NamedNodeMap nm = e.toElement.$dom_attributes;
    outputMsg(nm[0].text);
  }

  void _list(MouseEvent e) {
    _s52ui.getPalettesNameList().then((palNmList) {
      //outputMsg('ret: $palNmList');
      //palNmList.forEach((palNm) => insertPalR(palNm, i));
      for (int i=0; i<palNmList.length; ++i) {
        _insertPalR(palNmList[i], i);
      }
    });
  }


  Future<bool> _setUIcolor() {
    Completer completer = new Completer();
    // set S52 UI background color
    _s52ui.getRGB("UIBCK").then((rgbL) {
      query(".scrollTableL").style.backgroundColor =
          "rgba(${rgbL[0]},${rgbL[1]},${rgbL[2]}, 0.6)";

          // set S52 UI Text Color
          _s52ui.getRGB("UINFF").then((rgbL) {
            //query("span").style.textFillColor =
            //    "rgb(${rgbL[0]},${rgbL[1]},${rgbL[2]})";
            //query("span").computedStyle.then((s) =>
            //    s.textFillColor = "rgb(${rgbL[0]},${rgbL[1]},${rgbL[2]})");
            List l = queryAll("span");
            l.forEach((s) => s.style.textFillColor =
                    "rgb(${rgbL[0]},${rgbL[1]},${rgbL[2]})");

                // set S52 UI Border Color
                _s52ui.getRGB("UIBDR").then((rgbL) {
                  query("hr").style.color =
                      "rgb(${rgbL[0]},${rgbL[1]},${rgbL[2]})";
                      completer.complete(true);
                });
          });
    });

    return completer.future;
  }
  void _updateUIcol(int idx) {
    _s52ui.setMarinerParam(S52_MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
      _setUIcolor().then((ret) {
        _s52ui.draw().then((ret) {
          _s52ui.drawLast();
        });
      });
    });
  }
  void _insertPalR(String txt, int idx) {
    SpanElement      s = new SpanElement();
    s.text = txt;
    TableCellElement c = new TableCellElement();
    c.on.click.add((ev) => _updateUIcol(idx));
    c.nodes.add(s);
    TableRowElement  l = query("#tableR").insertRow(-1); // add at the end
    l.nodes.add(c);
  }


s52ui  _s52ui;
s52obj _ownshp;

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

List _checkButton = [
  S52_MAR_SHOW_TEXT,S52_MAR_SCAMIN,S52_MAR_ANTIALIAS,
  S52_MAR_QUAPNT01,S52_MAR_DISP_LEGEND,S52_MAR_DISP_CALIB,
  S52_MAR_DISP_DRGARE_PATTERN,S52_MAR_DISP_NODATA_LAYER,
  S52_MAR_DISP_AFTERGLOW,S52_MAR_DISP_CENTROIDS,S52_MAR_DISP_WORLD
];


///////////////////////////////////////
//
// Init
//

void _initCheck(List lst, int idx, String prefix) {
  if (idx < _checkButton.length) {
    int el = _checkButton[idx];
    _s52ui.getMarinerParam(el).then((ret) {
      query("#$prefix$el")
        ..checked = (1.0 == ret[0])? true : false
        ..on.click.add((ev) => outputMsg("id:'$prefix$el'"))
        ..on.click.add((ev) => _handleInput(el, 0.0));

      _initCheck(lst, idx+1, prefix);
    });
  }
}


void _init() {
  _setUIcolor().then((ret) {

  // S52_MAR_CMD_WRD_FILTER(33)
  _s52ui.getMarinerParam(S52_CMD_WRD_FILTER).then((ret) {
    [S52_CMD_WRD_FILTER_SY,S52_CMD_WRD_FILTER_LS,S52_CMD_WRD_FILTER_LC,
     S52_CMD_WRD_FILTER_AC,S52_CMD_WRD_FILTER_AP,S52_CMD_WRD_FILTER_TX
    ].forEach((el) {

      int filter = ret[0].toInt();
      query("#f$el")
        ..checked = (0 != (filter & el))? true : false
        ..on.click.add((ev) => outputMsg("id:'f$el'"))
        ..on.click.add((ev) => _handleInput(S52_CMD_WRD_FILTER, el));
    });

    //S52_MAR_DISP_CATEGORY(14)
    _s52ui.getMarinerParam(S52_MAR_DISP_CATEGORY).then((ret) {
      [S52_MAR_DISP_CATEGORY_BASE,
       S52_MAR_DISP_CATEGORY_STD,
       S52_MAR_DISP_CATEGORY_OTHER,
       S52_MAR_DISP_CATEGORY_SELECT
      ].forEach((el) {

        if (0 == el) { // S52_MAR_DISP_CATEGORY_BASE is alway ON
          query("#c$el")
            ..checked  = true
            ..disabled = true;
        } else {
          int filter = ret[0].toInt();
          query("#c$el")
            ..checked = (0 != (filter & el))? true : false
            ..on.click.add((ev) => outputMsg("id:'c$el'"))
            ..on.click.add((ev) => _handleInput(S52_MAR_DISP_CATEGORY, el));
        }
      });

      // S52_MAR_DISP_LAYER_LAST(27)
      _s52ui.getMarinerParam(S52_MAR_DISP_LAYER_LAST).then((ret) {
        [S52_MAR_DISP_LAYER_LAST_NONE,
         S52_MAR_DISP_LAYER_LAST_STD,
         S52_MAR_DISP_LAYER_LAST_OTHER,
         S52_MAR_DISP_LAYER_LAST_SELECT
        ].forEach((el) {

           int filter = ret[0].toInt();
           query("#l$el")
             ..checked = (0 != (filter & el))? true : false
             ..on.click.add((ev) => outputMsg("id:'l$el'"))
             ..on.click.add((ev) => _handleInput(S52_MAR_DISP_LAYER_LAST, el));
        });

        //query("#button")
        query("#td")
          ..on.click.add((ev) => outputMsg("id:'button'"))
          ..on.click.add((ev) => _list(ev));

        int startIdx = 0;
        _initCheck(_checkButton, startIdx, "i");

      });
    });
  });
  });
}


///////////////////////////////////////
//
// Main
//

void main() {

  //_ownshp = new s52obj("OWNSHP", new WebSocket('ws://127.0.0.1:2950'));
  _s52ui = new s52ui();


  // FIXME: should not be needed
  //_init();
  // wait for s52ui.js to finish
  window.setTimeout(_init, 100);
  //window.setTimeout(_init, 200);
  //window.setTimeout(_init, 500);
}


