// s52ui.dart: html/websocket test driver for libS52.so

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
  _sendMsg(String str) {
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
  Future<List> newOWNSHP(var label) {
    _completer = new Completer();
    
    _data["id"    ] = _id;
    _data["method"] = "S52_newOWNSHP";
    _data["params"] = [label];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> setPosition(int objH, double latitude, double longitude, double z) {
    _completer = new Completer();
    
    _data["id"    ] = _id;
    _data["method"] = "S52_pushPosition";
    _data["params"] = [objH,latitude,longitude,z];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> setVector(int objH, int vecstb, double course, double speed) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_setVector";
    _data["params"] = [objH,vecstb,course,speed];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> pickAt(double pixels_x, double pixels_y) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_pickAt";
    _data["params"] = [pixels_x,pixels_y];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getObjList(var cellName, var className) {
    _completer = new Completer();
    
    _data["id"    ] = _id;
    _data["method"] = "S52_getObjList";
    _data["params"] = [cellName,className];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getAttList(int S57ID) {
    _completer = new Completer();
    
    _data["id"    ] = _id;
    _data["method"] = "S52_getAttList";
    _data["params"] = [S57ID];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> getMarObjH(int S57ID) {
    _completer = new Completer();
    
    _data["id"    ] = _id;
    _data["method"] = "S52_getMarObjH";
    _data["params"] = [S57ID];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }
  Future<List> setVESSELstate(int objH, int vesselSelect, int vestat, int vesselTurn) {
    _completer = new Completer();

    _data["id"    ] = _id;
    _data["method"] = "S52_setVESSELstate";
    _data["params"] = [objH,vesselSelect,vestat,vesselTurn];
    String jsonCmdstr = stringify(_data);
    _sendMsg(jsonCmdstr);

    return _completer.future;
  }

}
////////////////////////////////////////////////////////


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
          //if (1 == ret[0]) 
          //  print("OK");
            s52.draw().then((ret){});
          //}
        });
        return;

      default:
        throw "_handleInput(): param invalid";
    }

    double val = (true == checked) ? 1.0 : 0.0;
    s52.setMarinerParam(param, val).then((ret) {
      //if (1 == ret[0]) {
      //  print("OK");
        s52.draw().then((ret){});
      //}
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
      s52.draw().then((ret) {
        s52.drawLast().then((ret){
          _setUIcolor().then((ret) {});
        });
      });
   });
  }

  void _insertCellRTable(String txt, var cb, var idx) {
    SpanElement      sp = new SpanElement();
    UListElement     ul = new UListElement();
    LIElement        li = new LIElement(); 
    sp.text = txt;
    li.text = txt;
    ul.nodes.add(li);
    TableCellElement c = new TableCellElement();
    c.onClick.listen((ev) => cb(idx));
    c.nodes.add(sp);
    c.nodes.add(ul);
    
    // add at the end
    TableElement    t = query("#tableR");
    TableRowElement r = t.insertRow(-1);
    r.nodes.add(c);
  }
  _appendUList(UListElement UList, var txt) {
    if (null == UList)
      UList = new UListElement();

    LIElement li = new LIElement(); 
    li.text = txt;
    UList.nodes.add(li);

    return UList;
  }
  _appendCellRTable(String txt, var cb, var idx) {
    SpanElement      sp = new SpanElement();
    sp.text = txt;

    TableCellElement c = new TableCellElement();
    c.onClick.listen((ev) => cb(idx, c));
    c.nodes.add(sp);

    TableElement    t = query("#tableR");
    TableRowElement r = t.insertRow(-1);  // add at the end
    r.nodes.add(c);
    
    return c;
  }
  void _clearRTable() {
    TableElement t = query("#tableR");
    bool nr = t.rows.isEmpty;
    while (!nr) {
      t.deleteRow(0);
      nr = t.rows.isEmpty;
    }
  }
  
  /////////////// Color Palette Button Handling //////////////////////////
  void _listPal(MouseEvent e) {
    // start color highlight animation
    //query("#td_buttonCell").style.animationIterationCount = '1';
    
    _clearRTable();
    
    s52.getPalettesNameList().then((palNmList) {
      for (int i=0; i<palNmList.length; ++i) {
        _insertCellRTable(palNmList[i], _updateUIcol, i);
      }

      List l = queryAll("span");
      l.forEach((s) => s.style.textFillColor =
          "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");

      // stop (abruptly) color animation
      //query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }
  
  /////////////// AIS Button Handling //////////////////////////
  void _updateAIS(int idx, TableCellElement c){
    //print('_updateAIS: $idx');
    //print(c.children[1]);
    //print(c.children[1].children[0]);

    c.children[1].style.display = ('block' == c.children[1].style.display) ? 'none' : 'block';
    
    int vesselSelect = ('block' == c.children[1].style.display) ? 1 : 2;
    int S57ID        = int.parse(c.children[1].children[0].text);
    s52.getMarObjH(S57ID).then((ret) {
      // vesselTurn:129 - undefined
      s52.setVESSELstate(ret[0], vesselSelect, 0, 129).then((ret) {});
    });
  }
  void _setAISatt(var vesselList, var idx) {
    if (idx < vesselList.length) {
      var l     = vesselList[idx].split(':');
      int S57ID = int.parse(l[0]);
      
      //print('S57ID: $S57ID');
      
      s52.getAttList(S57ID).then((ret) {
        TableCellElement cell;
        UListElement UList;
        //print('ret: ${ret[0]}');
        var vesselAtt = ret[0].split(',');
        vesselAtt.forEach((att) {
          if (-1 != att.indexOf('_vessel_label')) {
            cell = _appendCellRTable(att, _updateAIS, idx);
          } else {
            UList = _appendUList(UList, att);
          }
        });
        
        cell.nodes.add(UList);
        
        // recursion
        _setAISatt(vesselList, idx+1); 
      });
    }
  }
  void _listAIS(MouseEvent e) {
    // start color highlight animation
    //query("#td_buttonCell").style.animationIterationCount = '1';
    
    _clearRTable();

    s52.getObjList('--6MARIN.000', 'vessel').then((str) {
      var vesselList = str[0].split(',');
      vesselList.removeAt(0); // list[0]: is cellName: '--6MARIN.000'
      vesselList.removeAt(0); // list[1]: is objName: 'vessel'

      _setAISatt(vesselList, 0);

      // stop (abruptly) color animation
      //query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }



///////////////////////////////////////
//
// init  
//

  List _checkButton = [
                       S52.MAR_SHOW_TEXT,S52.MAR_SCAMIN,S52.MAR_ANTIALIAS,
                       S52.MAR_QUAPNT01,S52.MAR_DISP_LEGEND,S52.MAR_DISP_CALIB,
                       S52.MAR_DISP_DRGARE_PATTERN,S52.MAR_DISP_NODATA_LAYER,
                       S52.MAR_DISP_AFTERGLOW,S52.MAR_DISP_CENTROIDS,S52.MAR_DISP_WORLD
                       ];

void _initCheckBox(List lst, int idx, String prefix, Completer completer) {
  // need recursion: wait Future of call before calling libS52 again
  if (idx < _checkButton.length) {
    int el = _checkButton[idx];
    s52.getMarinerParam(el).then((ret) {
      InputElement i = query("#$prefix$el");
      i
        ..checked = (1.0 == ret[0]) ? true : false
        ..onClick.listen((ev) => print("id:'$prefix$el'"))
        ..onClick.listen((ev) => _handleInput(el, 0.0));

      // recursion
      _initCheckBox(lst, idx+1, prefix, completer);
    });
  } else {
    completer.complete(true);  
  }
}

Future<bool> _initUI() {
  Completer completer = new Completer();
  
  print('_initUI(): start');

  _setUIcolor().then((ret) {
    // S52_MAR_CMD_WRD_FILTER(33)
    s52.getMarinerParam(S52.CMD_WRD_FILTER).then((ret) {
      [S52.CMD_WRD_FILTER_SY,S52.CMD_WRD_FILTER_LS,S52.CMD_WRD_FILTER_LC,
       S52.CMD_WRD_FILTER_AC,S52.CMD_WRD_FILTER_AP,S52.CMD_WRD_FILTER_TX
      ].forEach((el) {
        int filter = ret[0].toInt();
        InputElement i = query("#f$el");
        i.checked = (0 == (filter & el)) ? false : true;
        i.onClick.listen((ev) => print("id:'f$el'"));
        i.onClick.listen((ev) => _handleInput(S52.CMD_WRD_FILTER, el));
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
            i.checked  = true;
            i.disabled = true;
          } else {
            int filter = ret[0].toInt();
            InputElement i = query("#c$el");
            i.checked = (0 == (filter & el)) ? false : true;
            i.onClick.listen((ev) => print("id:'c$el'"));
            i.onClick.listen((ev) => _handleInput(S52.MAR_DISP_CATEGORY, el.toDouble()));
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
             i.checked = (0 == (filter & el)) ? false : true;
             i.onClick.listen((ev) => print("id:'l$el'"));
             i.onClick.listen((ev) => _handleInput(S52.MAR_DISP_LAYER_LAST, el.toDouble()));
          });

          query("#td_buttonCell1")
          ..onClick.listen((ev) => print("id:'td_buttonCell1'"))
          ..onClick.listen((ev) => _listPal(ev));

          query("#td_buttonCell2")
          ..onClick.listen((ev) => print("id:'td_buttonCell2'"))
          ..onClick.listen((ev) => _listAIS(ev));

          int startIdx = 0;
          _initCheckBox(_checkButton, startIdx, "i", completer);
        });
      });
    });
  });
  
  return completer.future;
}

void _initTouch() {
  // Handle touch events.
  int touchStartX = null;
  int touchStartY = null;

  var target = query('#mainBody');
  target.onTouchStart.listen((TouchEvent event) {
    event.preventDefault();

    if (event.touches.length > 0) {
      touchStartX = event.touches[0].pageX;
      touchStartY = event.touches[0].pageY;
      
      //print("start X1:$touchStartX");
      //touchStartX = event.touches[1].pageX;
      //print("start X2:$touchStartX");
    }
  });

  target.onTouchMove.listen((TouchEvent event) {
    event.preventDefault();
  });

  target.onTouchEnd.listen((TouchEvent event) {
    event.preventDefault();

    if (touchStartX != null && event.touches.length > 0) {
      int deltaX = event.touches[0].pageX - touchStartX;
      int deltaY = event.touches[0].pageY - touchStartY;
      
      if ((deltaX<5) && (deltaY<5)) {
        s52.pickAt(touchStartX.toDouble(), touchStartY.toDouble()).then((ret) {
          print('pick:$ret');
        });

        
      }
      //print('move X:$newTouchX');
    }
    
    //print('end X: null');
    touchStartX = null;
    touchStartY = null;
  });
}

int _ownshp = 0;
var _watchID;
void _watchPosition() {
  if (0 == _ownshp) { 
    print('s5ui.dart:_setPosition(): failed, no _ownshp handle');
    return;
  }
  //print('s5ui.dart:_setPosition(): start');
  
  /*
  _watchID = window.navigator.geolocation.watchPosition(
      (Geoposition position) {
        s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
      },
      (PositionError error) {
        print('getCurrentPosition(): ${error.message} (${error.code})');
      },
      {'enableHighAccuracy':true, 'maximumAge':30000, 'timeout':27000}
  );  
  */

  // {'enableHighAccuracy':true, 'timeout':27000, 'maximumAge':30000}
  // true, 27000, 30000 !?
  //window.navigator.geolocation.watchPosition().then(

  //* FF choke here
  window.navigator.geolocation.getCurrentPosition().then(
    (Geoposition position) {
      s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
    }
  );
  // */
}

void _init() {
  _initTouch();
  
  //*
  // FIXME: get ownshp
  // use the Future of ownshp to signal to start loading the UI (_initUI)
  s52.newOWNSHP('OWNSHP').then((ret) {
    //print('s5ui.dart:OWNSHP(): $ret');
    _ownshp = ret[0]; 
    _watchPosition();

    _initUI().then((ret) {});
    
    //initDeviceOrientationEvent(String type, bool bubbles, bool cancelable, num alpha, num beta, num gamma, bool absolute)
    //window.onDeviceOrientation.listen((e) {s52.setVector(_ownshp, 1, e.gamma, 0.0).then((ret){});});
  });
  //*/
}

///////////////////////////////////////
//
// Main
//
//void _onData(ProgressEvent e) {
//}
void main() {
  s52 = new S52();
  
  // debug - read file
  //File           f      = new File('file:///data/media/s52android/ENC_ROOT');
  //File           f      = new File();
  //Blob           b      = new Blob('file:///data/media/s52android/ENC_ROOT');
  //FileReaderSync reader = new FileReaderSync();
  //reader.onLoad.listen(onData, onError, onDone, unsubscribeOnError) 
  //reader.onLoadEnd.listen(_onData);
  //Blob
  //reader.readAsText('file:///data/media/s52android/ENC_ROOT');
  //reader.readAsDataUrl();
  //reader.result;
  //print('s5ui.dart:main(): start');

  js.scoped(() {
    js.context.wsReady   = new js.Callback.once(_init);
    // WebSocket reply something (meaningless!) when making initial connection read it! (and maybe do something)
    js.context.rcvS52Msg = new js.Callback.once(s52.rcvMsg);
  });
  
}
