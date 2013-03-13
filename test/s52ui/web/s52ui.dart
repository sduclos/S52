// s52ui.dart: html/websocket test driver for libS52.so

library s52ui;

import 'dart:html';
import 'dart:json';
import 'dart:async';
import 'package:js/js.dart' as js;

part 'S52.dart';

S52  s52;

// S52 color for UI element
List _UIBCK;  // background
List _UINFF;  // text
List _UIBDR;  // border



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

        InputElement i = query("#i$param");
        checked = i.checked;
        break;

      case S52.MAR_ROT_BUOY_LIGHT       :  //=28
        RangeInputElement r = query("#r$param");
        var val = r.valueAsNumber;
        s52.setMarinerParam(param, val).then((ret) {
            s52.draw().then((ret) {});
        });
        return;
      
      case S52.MAR_DISP_CATEGORY        :  //= 14;
        //S52_MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000000
        //S52_MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0000001
        //S52_MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0000010
        //S52_MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0000100

      case S52.MAR_DISP_LAYER_LAST       : //= 27;
        //S52_MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0001000
        //S52_MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010000
        //S52_MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0100000
        //S52_MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 1000000

      case S52.CMD_WRD_FILTER            : //= 33;
        //S52_CMD_WRD_FILTER_SY          =        1;  // 1 << 0; 000001 - SY
        //S52_CMD_WRD_FILTER_LS          =        2;  // 1 << 1; 000010 - LS
        //S52_CMD_WRD_FILTER_LC          =        4;  // 1 << 2; 000100 - LC
        //S52_CMD_WRD_FILTER_AC          =        8;  // 1 << 3; 001000 - AC
        //S52_CMD_WRD_FILTER_AP          =       16;  // 1 << 4; 010000 - AP
        //S52_CMD_WRD_FILTER_TX          =       32;  // 1 << 5; 100000 - TE & TX

        s52.setMarinerParam(param, value).then((ret) {
            s52.draw().then((ret) {});
        });
        return;

      default:
        throw "_handleInput(): param invalid";
    }

    double val = (true == checked) ? 1.0 : 0.0;
    s52.setMarinerParam(param, val).then((ret) {
        s52.draw().then((ret){});
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
          //"rgba(${_UIBCK[0]},${_UIBCK[1]},${_UIBCK[2]}, 0.6)";
          "rgba(${_UIBCK[0]},${_UIBCK[1]},${_UIBCK[2]}, 0.7)";
      // set S52 UI Border Color
      queryAll("hr").forEach((s) => s.style.backgroundColor =
           "rgb(${_UIBDR[0]},${_UIBDR[1]},${_UIBDR[2]})");
      // set S52 UI Text Color
      queryAll("div").forEach((s) => s.style.color =
           "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");

      completer.complete(true);
    });
    return completer.future;
  }

  void _updateUIcol(int idx, TableCellElement c) {
    s52.setMarinerParam(S52.MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
      s52.draw().then((ret) {
        s52.drawLast().then((ret){
          _setUIcolor().then((ret) {});
        });
      });
   });
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
    ParagraphElement p = new ParagraphElement();
    p.text = txt;

    TableCellElement c = new TableCellElement();
    c.onClick.listen((ev) => cb(idx, c));
    c.nodes.add(p);

    TableElement    t = query("#tableR");
    TableRowElement r = t.insertRow(-1);  // add at the end
    r.nodes.add(c);
    
    return c;
  }
  void _clearTable(id) {
    TableElement t = query(id);
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
    
    _clearTable("#tableR");
    
    s52.getPalettesNameList().then((palNmList) {
      for (int i=0; i<palNmList.length; ++i) {
        _appendCellRTable(palNmList[i], _updateUIcol, i);
      }

      /*
      //List l = queryAll("span");
      List l = queryAll("p");
      l.forEach((s) => s.style.textFillColor =
          "rgb(${_UINFF[0]},${_UINFF[1]},${_UINFF[2]})");
      */
      // stop (abruptly) color animation
      //query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }
  
  /////////////// AIS Button Handling //////////////////////////
  void _updateAIS(int idx, TableCellElement c){

    c.children[1].style.display = ('block' == c.children[1].style.display) ? 'none' : 'block';
    
    int vesselSelect = ('block' == c.children[1].style.display) ? 1 : 2;
    // S57ID allway the first
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
        UListElement UList = new UListElement();
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
    
    _clearTable("#tableR");

    s52.getObjList('--6MARIN.000', 'vessel').then((str) {
      var vesselList = str[0].split(',');
      vesselList.removeAt(0); // list[0]: is cellName: '--6MARIN.000'
      vesselList.removeAt(0); // list[1]: is objName: 'vessel'

      _setAISatt(vesselList, 0);

      // stop (abruptly) color animation
      //query("#td_buttonCell").style.animationIterationCount = '0';
    });
  }
  /////////////// ENC Button Handling //////////////////////////
  _loadENC(int idx, TableCellElement c) {
    var encPath = c.children[0].text;
    if ('*' == encPath[0]) {
      s52.doneCell(encPath.substring(1)).then((ret) {
        s52.draw().then((ret) {_listENC(null);});
      });
    } else {
      s52.loadCell(encPath).then((ret) {
        s52.draw().then((ret) {_listENC(null);});
      });
    }
  }
  void _listENC(e) {
    _clearTable("#tableR");
    
    s52.getCellNameList().then((str) {
      var idx = 0;
      //print(str[0]);
      str.forEach((enc) {
        _appendCellRTable(enc, _loadENC, idx++);
      });
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
      i.checked = (1.0 == ret[0]) ? true : false;
      i.onClick.listen((ev) => print("id:'$prefix$el'"));
      i.onClick.listen((ev) => _handleInput(el, 0.0));

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
        i.onClick.listen((ev) => _handleInput(S52.CMD_WRD_FILTER, el.toDouble()));
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

          query("#td_buttonCell3")
          ..onClick.listen((ev) => print("id:'td_buttonCell3'"))
          ..onClick.listen((ev) => _listENC(ev));

          query("#r28")
          ..onClick.listen((ev) => print("id:'r28'"))
          ..onClick.listen((ev) => _handleInput(S52.MAR_ROT_BUOY_LIGHT, 0.0));

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

void GPSpos(Geoposition position) {
  print('GPS new pos');
  s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
}
int _ownshp = 0;
void _watchPosition() {
  if (0 == _ownshp) { 
    print('s5ui.dart:_setPosition(): failed, no _ownshp handle');
    return;
  }
  print('s5ui.dart:_watchPosition(): start');
  
  // {'enableHighAccuracy':true, 'timeout':27000, 'maximumAge':30000}
  //window.navigator.geolocation.watchPosition().listen(onData, onError, onDone, unsubscribeOnError)
  try {
    window.navigator.geolocation.watchPosition().listen(GPSpos);
  } catch (e,s) {
    print(s);
  }
  //subscribe(onData: (List<int> data) { print(data.length); });
  /* FF choke here
  window.navigator.geolocation.getCurrentPosition().then(
    (Geoposition position) {
      s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
    }
  );
  // */
}

void _init() {
  //_initTouch();
  
  //*
  // FIXME: get ownshp
  // use the Future of ownshp to signal to start loading the UI (_initUI)
  s52.newOWNSHP('OWNSHP').then((ret) {
    //print('s5ui.dart:OWNSHP(): $ret');
    _ownshp = ret[0]; 
    
    // Dart BUG: can't read GPS
    //_watchPosition();

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
  //print('s5ui.dart:main(): start');
  
  s52 = new S52();
  
  js.scoped(() {
    js.context.wsReady   = new js.Callback.once(_init);
    // WebSocket reply something (meaningless!) when making initial connection read it! (and maybe do something)
    js.context.rcvS52Msg = new js.Callback.once(s52.rcvMsg);
  });
  
}
