// s52ui.dart: html/websocket/async test driver for libS52.so

library s52ui;

import 'dart:html';
import 'dart:svg';
import 'dart:async';
import 'dart:convert';
import 'dart:collection'; // Queue in s52.dart, maybe await/async!!

part 's52.dart';


// Note: wsUri is replace, in shell,
// by calling $firefox build/web/s52ui.html?ws://192.168.1.66:2950
//var wsUri = 'ws://192.168.1.66:2950';
//var wsUri = 'ws://192.168.1.70:2950';
//var wsUri = 'ws://192.168.1.67:2950'; // xoom
//var wsUri = 'ws://192.168.1.71:2950'; // xoom
//var wsUri = 'ws://192.168.1.69:2950'; // Nexus
//var wsUri = 'ws://127.0.0.1:2950';    // localhost
var wsUri = 'ws://localhost:2950';    // localhost
// remote s52droid via adb (USB)
// adb forward tcp:2950 tcp:2950
//var wsUri = 'ws://127.0.0.1:2950'; // Nexus

S52 s52; // instance of S52 interface (using WebSocket)

void _handleInput(int param, double value) {
  switch (param) {
    case S52.MAR_SHOW_TEXT:           //=  1;
    case S52.MAR_SCAMIN:              //= 23;
    case S52.MAR_ANTIALIAS:           //= 24;
    case S52.MAR_QUAPNT01:            //= 25;
    case S52.MAR_DISP_LEGEND:         //= 32;
    case S52.MAR_DISP_CALIB:          //= 36;
    case S52.MAR_DISP_DRGARE_PATTERN: //= 37;
    case S52.MAR_DISP_NODATA_LAYER:   //= 38;
    case S52.MAR_DISP_AFTERGLOW:      //= 40;
    case S52.MAR_DISP_CENTROIDS:      //= 41;
    case S52.MAR_DISP_WORLD:          //= 42;

      InputElement i = querySelector("#i$param");
      double val = (true == i.checked) ? 1.0 : 0.0;
      s52.setMarinerParam(param, val).then((ret) {
        s52.draw();
      });
      break;

    case S52.MAR_ROT_BUOY_LIGHT:      //=28
      RangeInputElement r = querySelector("#r$param");
      var val = r.valueAsNumber;
      s52.setMarinerParam(param, val).then((ret) {
        s52.draw();
      });
      return;

    case S52.MAR_SAFETY_CONTOUR:
    case S52.MAR_SAFETY_DEPTH:
    case S52.MAR_SHALLOW_CONTOUR:
    case S52.MAR_DEEP_CONTOUR:
      InputElement i = querySelector("#I$param");
      var val = i.valueAsNumber;
      s52.setMarinerParam(param, val).then((ret) {
        s52.draw();
      });
      return;

    case S52.MAR_DISP_CATEGORY:      //= 14;
      //S52_MAR_DISP_CATEGORY_BASE     =        0;  //      0; 0000
      //S52_MAR_DISP_CATEGORY_STD      =        1;  // 1 << 0; 0001
      //S52_MAR_DISP_CATEGORY_OTHER    =        2;  // 1 << 1; 0002
      //S52_MAR_DISP_CATEGORY_SELECT   =        4;  // 1 << 2; 0004

    case S52.MAR_DISP_LAYER_LAST:    //= 27;
      //S52_MAR_DISP_LAYER_LAST_NONE   =        8;  // 1 << 3; 0008
      //S52_MAR_DISP_LAYER_LAST_STD    =       16;  // 1 << 4; 0010
      //S52_MAR_DISP_LAYER_LAST_OTHER  =       32;  // 1 << 5; 0020
      //S52_MAR_DISP_LAYER_LAST_SELECT =       64;  // 1 << 5; 0040

    case S52.CMD_WRD_FILTER:         //= 33;
      //S52_CMD_WRD_FILTER_SY          =        1;  // 1 << 0; 0001 - SY
      //S52_CMD_WRD_FILTER_LS          =        2;  // 1 << 1; 0002 - LS
      //S52_CMD_WRD_FILTER_LC          =        4;  // 1 << 2; 0004 - LC
      //S52_CMD_WRD_FILTER_AC          =        8;  // 1 << 3; 0008 - AC
      //S52_CMD_WRD_FILTER_AP          =       16;  // 1 << 4; 0010 - AP
      //S52_CMD_WRD_FILTER_TX          =       32;  // 1 << 5; 0020 - TE & TX

      s52.setMarinerParam(param, value).then((ret) {
        s52.draw();
      });
      return;

    default:
      throw "_handleInput(): param invalid";
  }
}

Future<bool> _getS52UIcolor() {
  Completer completer = new Completer();

  // get S52 UI background color
  s52.getRGB("UIBCK").then((UIBCK) {
    s52.UIBCK = UIBCK;
    // get S52 UI Text Color
    s52.getRGB("UINFF").then((UINFF) {
      s52.UINFF = UINFF;
      // get S52 UI Border Color
      s52.getRGB("UIBDR").then((UIBDR) {
        s52.UIBDR = UIBDR;
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
    querySelector("#tableL").style.backgroundColor =
        "rgba(${s52.UIBCK[0]},${s52.UIBCK[1]},${s52.UIBCK[2]}, 0.7)";
    // set S52 UI Border Color
    querySelectorAll("hr").forEach((s) => s.style.backgroundColor =
        "rgb(${s52.UIBDR[0]},${s52.UIBDR[1]},${s52.UIBDR[2]})");
    // set S52 UI Text Color
    querySelectorAll("div").forEach((s) =>
        s.style.color = "rgb(${s52.UINFF[0]},${s52.UINFF[1]},${s52.UINFF[2]})");

    completer.complete(true);
  });

  return completer.future;
}

void _updateUIcol(int idx, TableCellElement c) {
  s52.setMarinerParam(S52.MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
    s52.draw().then((ret) {
      _setUIcolor();
    });
  });
}

UListElement _appendUList(UListElement UList, var txt) {
  if (null == UList)
    UList = new UListElement();

  LIElement li = new LIElement();
  li.text = txt;
  UList.nodes.add(li);

  return UList;
}

TableCellElement _appendCellRTable(String txt, var cb, var idx) {
  ParagraphElement p = new ParagraphElement();
  p.text = txt;

  TableCellElement c = new TableCellElement();
  c.onClick.listen((ev) => cb(idx, c));
  c.nodes.add(p);

  TableElement t = querySelector("#tableR");
  TableRowElement r = t.insertRow(-1); // add at the end
  r.nodes.add(c);

  return c;
}

void _clearTable(id) {
  TableElement t = querySelector(id);
  bool nr = t.rows.isEmpty;
  while (!nr) {
    t.deleteRow(0);
    nr = t.rows.isEmpty;
  }
}

/////////////// version //////////////////////////
void _version(MouseEvent e) {
  _clearTable("#tableR");

  s52.version().then((ret) {
    //print('ret: ${ret[0]}');
    print('ret: ${ret}');

    var Att = ret[0].split(',');
    for (int i=0; i<Att.length; ++i) {
      _appendCellRTable(Att[i], null, i);
    }
  });
}

/////////////// Color Palette Button Handling //////////////////////////
void _listPal(MouseEvent e) {
  // start color highlight animation
  //query("#td_buttonCell").style.animationIterationCount = '1';

  _clearTable("#tableR");

  s52.getPalettesNameList().then((palNmList) {
    var i = 0;
    var nmList = palNmList[0].split(',');
    nmList.forEach((nm) {
      _appendCellRTable(nm, _updateUIcol, i++);
    });

    // stop (abruptly) color animation
    //query("#td_buttonCell").style.animationIterationCount = '0';
  });
}

/////////////// AIS Button Handling //////////////////////////
void _updateAIS(int idx, TableCellElement c) {
  c.children[1].style.display =
      ('block' == c.children[1].style.display) ? 'none' : 'block';

  int vesselSelect = ('block' == c.children[1].style.display) ? 1 : 2;
  // S57ID allway the first
  int S57ID = int.parse(c.children[1].children[0].text);
  s52.getMarObj(S57ID).then((ret) {
    // vesselTurn:129 - undefined
    s52.setVESSELstate(ret[0], vesselSelect, 0, 129);
  });
}

void _setAISatt(var vesselList, var idx) {
  if (idx < vesselList.length) {
    var l = vesselList[idx].split(':');
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
      _setAISatt(vesselList, idx + 1);
    });
  }
}

void _listAIS(MouseEvent e) {
  // start color highlight animation
  //query("#td_buttonCell").style.animationIterationCount = '1';

  _clearTable("#tableR");

  s52.getObjList('--6MARIN.000', 'vessel').then((ret) {
    List<String> vesselList = ret[0].split(',');
    if (1 < vesselList.length) {
      vesselList.removeAt(0); // list[0]: is cellName: '--6MARIN.000'
      vesselList.removeAt(0); // list[1]: is objName: 'vessel'

      _setAISatt(vesselList, 0);
    }

    // stop (abruptly) color animation
    //query("#td_buttonCell").style.animationIterationCount = '0';
  });
}

/////////////// ENC Button Handling //////////////////////////
void _loadENC(int idx, TableCellElement c) {
  var encPath = c.children[0].text;
  c.children[0].style.backgroundColor = 'yellow';
  if ('*' == encPath[0]) {
    s52.doneCell(encPath.substring(1)).then((ret) {
      s52.draw().then((ret) {
        _listENC(null);
        c.children[0].style.backgroundColor = 'transparent';
      });
    });
  } else {
    s52.loadCell(encPath).then((ret) {
      s52.draw().then((ret) {
        _listENC(null);
        c.children[0].style.backgroundColor = 'transparent';
      });
    });
  }
}

void _listENC(e) {
  _clearTable("#tableR");

  //print('_listENC(): ..');
  s52.getCellNameList().then((cellList) {
    var idx = 0;
    var encList = cellList[0].split(',');
    //print('_listENC(): .. $encList');
    encList.forEach((enc) {
      _appendCellRTable(enc, _loadENC, idx++);
    });
  });
}

/////////////// S57ID Button Handling //////////////////////////
void _listS57IDatt(var S57ID) {
  _clearTable("#tableR");

  s52.getAttList(int.parse(S57ID)).then((ret) {
    print('ret: ${ret[0]}');

    var Att = ret[0].split(',');
    for (int i = 0; i < Att.length; ++i) {
      _appendCellRTable(Att[i], null, i);
    }
  });
}

///////////////////////////////////////
//
// init UI (fill state)
//

List _checkButton = [
  S52.MAR_SHOW_TEXT,
  S52.MAR_SCAMIN,
  S52.MAR_ANTIALIAS,
  S52.MAR_QUAPNT01,
  S52.MAR_DISP_LEGEND,
  S52.MAR_DISP_CALIB,
  S52.MAR_DISP_DRGARE_PATTERN,
  S52.MAR_DISP_NODATA_LAYER,
  S52.MAR_DISP_AFTERGLOW,
  S52.MAR_DISP_CENTROIDS,
  S52.MAR_DISP_WORLD
];
List _numButton = [
  S52.MAR_SAFETY_CONTOUR,
  S52.MAR_SAFETY_DEPTH,
  S52.MAR_SHALLOW_CONTOUR,
  S52.MAR_DEEP_CONTOUR
];

// FIXME: recursion not needed with Cmd queue - simpler logic
Future<bool> _initCheckBox(List lst, int idx, String prefix, Completer completer) {
  // need recursion: wait Future of call before calling libS52 again
  // FIXME: MAR_SCAMIN fail on key
  if (idx < lst.length) {
    int el = lst[idx];
    s52.getMarinerParam(el).then((ret) {
      InputElement i = querySelector("#$prefix$el");
      i.checked = (1.0 == ret[0]) ? true : false;
      i.onClick.listen((ev) => print("id:'$prefix$el'"));
      i.onClick.listen((ev) => _handleInput(el, 0.0));

      // recursion
      _initCheckBox(lst, idx + 1, prefix, completer);
    });
  } else {
    completer.complete(true);
  }

  return completer.future;
}

// FIXME: recursion not needed with Cmd queue - simpler logic
Future<bool> _initNumBox(List lst, int idx, String prefix, Completer completer) {
  // need recursion: wait Future of call before calling libS52 again
  // FIXME: MAR_SCAMIN fail on key
  if (idx < lst.length) {
    int el = lst[idx];
    s52.getMarinerParam(el).then((ret) {
      InputElement i = querySelector("#$prefix$el");
      i.defaultValue = ret[0].toString();
      print("id:'I$el'");

      //i.onClick.listen((ev) => print("id:'$prefix$el'"));
      //i.onClick.listen((ev) => _handleInput(el, 0.0));
      i.onInput.listen((ev) => _handleInput(el, 0.0));

      // recursion
      _initNumBox(lst, idx + 1, prefix, completer);
    });
  } else {
    completer.complete(true);
  }

  return completer.future;
}

Future<bool> _initUI() {

  print('_initUI(): - start -');

  _setUIcolor();

  // S52_MAR_CMD_WRD_FILTER(33)
  s52.getMarinerParam(S52.CMD_WRD_FILTER).then((ret) {
    [
      S52.CMD_WRD_FILTER_SY,
      S52.CMD_WRD_FILTER_LS,
      S52.CMD_WRD_FILTER_LC,
      S52.CMD_WRD_FILTER_AC,
      S52.CMD_WRD_FILTER_AP,
      S52.CMD_WRD_FILTER_TX
    ].forEach((el) {
      int filter = ret[0].toInt();
      InputElement i = querySelector("#f$el");
      i.checked = (0 == (filter & el)) ? false : true;
      i.onClick.listen((ev) => print("id:'f$el'"));
      i.onClick.listen((ev) => _handleInput(S52.CMD_WRD_FILTER, el.toDouble()));
    });
  });

  //S52_MAR_DISP_CATEGORY(14)
  s52.getMarinerParam(S52.MAR_DISP_CATEGORY).then((ret) {
    [
      S52.MAR_DISP_CATEGORY_BASE,
      S52.MAR_DISP_CATEGORY_STD,
      S52.MAR_DISP_CATEGORY_OTHER,
      S52.MAR_DISP_CATEGORY_SELECT
    ].forEach((el) {
      if (0 == el) {
        // S52_MAR_DISP_CATEGORY_BASE is alway ON
        InputElement i = querySelector("#c$el");
        i.checked = true;
        i.disabled = true;
      } else {
        int filter = ret[0].toInt();
        InputElement i = querySelector("#c$el");
        i.checked = (0 == (filter & el)) ? false : true;
        i.onClick.listen((ev) => print("id:'c$el'"));
        i.onClick.listen((ev) => _handleInput(S52.MAR_DISP_CATEGORY, el.toDouble()));
      }
    });
  });

  // S52_MAR_DISP_LAYER_LAST(27)
  s52.getMarinerParam(S52.MAR_DISP_LAYER_LAST).then((ret) {
    [
      S52.MAR_DISP_LAYER_LAST_NONE,
      S52.MAR_DISP_LAYER_LAST_STD,
      S52.MAR_DISP_LAYER_LAST_OTHER,
      S52.MAR_DISP_LAYER_LAST_SELECT
    ].forEach((el) {
      int filter = ret[0].toInt();
      InputElement i = querySelector("#l$el");
      i.checked = (0 == (filter & el)) ? false : true;
      i.onClick.listen((ev) => print("id:'l$el'"));
      i.onClick.listen((ev) => _handleInput(S52.MAR_DISP_LAYER_LAST, el.toDouble()));
    });
  });

  querySelector("#td_buttonCell0")
    ..onClick.listen((ev) => print("id:'td_buttonCell0'"))
    ..onClick.listen((ev) => _version(ev));

  querySelector("#td_buttonCell1")
    ..onClick.listen((ev) => print("id:'td_buttonCell1'"))
    ..onClick.listen((ev) => _listPal(ev));

  querySelector("#td_buttonCell2")
    ..onClick.listen((ev) => print("id:'td_buttonCell2'"))
    ..onClick.listen((ev) => _listAIS(ev));

  querySelector("#td_buttonCell3")
    ..onClick.listen((ev) => print("id:'td_buttonCell3'"))
    ..onClick.listen((ev) => _listENC(ev));

  // 'Switch to Touch'
  querySelector("#td_buttonCell4")
    ..onClick.listen((ev) => print("id:'td_buttonCell4'"))
    ..onClick.listen((ev) => _toggleUIEvent());

  querySelector("#r28")
    ..onClick.listen((ev) => print("id:'r28'"))
    ..onClick.listen((ev) => _handleInput(S52.MAR_ROT_BUOY_LIGHT, 0.0));

  // 'Switch To Menu'
  querySelector("#svg1menu")
    ..onClick.listen((ev) => print("id:'svg1menu'"))
    ..onClick.listen((ev) => _toggleUIEvent());

  print('s52ui.dart:_checkButton() - start - ');

  int startIdx = 0;
  Completer completer = new Completer();
  _initCheckBox(_checkButton, startIdx, "i", completer).then((ret) {
    completer = new Completer();
    startIdx = 0;
    _initNumBox(_numButton, startIdx, "I", completer);
  });

  return completer.future;
}

void _initTouch() {
  // Handle touch events.
  Element target = querySelector('#svg1');

  //bool doBlit1  = true;
  //bool doBlit2  = true;
  bool modeZoom = false;
  bool newTouch = false;

  int start_x1 = 0;
  int start_y1 = 0;
  int start_x2 = 0;
  //int start_y2 =  0;

  int new_x1 = -1;
  int new_y1 = -1;
  int new_x2 =  0;
  //int new_y2   =  0;

  int    ticks    = 0;
  double zoom_fac = 0.0;

  querySelector('#svg1g').onTouchStart.listen((ev) {
    _fullList(ev);
  });

  //print('s52ui.dart:_initTouch():target');
  print('s52ui.dart:_initTouch():target=$target');

  target.onTouchStart.listen((TouchEvent event) {
    event.preventDefault();
    s52.skipTimer = true;

    // Start of new Touch event cycle
    if (false == newTouch) {
      newTouch = true;
      modeZoom = false;
      zoom_fac = 0.0;
      ticks    = 0;
    }

    if (1 == event.touches.length) {
      start_x1 = event.touches[0].page.x;
      start_y1 = event.touches[0].page.y;

      //doBlit1  = true;
      //doBlit2  = false;

      // debug
      //print("onTouchStart start_x1:$start_x1, start_y1:$start_y1");
    }

    if (2 == event.touches.length) {
      start_x1 = event.touches[0].page.x;
      start_y1 = event.touches[0].page.y;
      start_x2 = event.touches[1].page.x;
      //start_y2 = event.touches[1].page.y;

      //doBlit1  = false;
      //doBlit2  = true;

      modeZoom = true;

      // debug
      //print("onTouchStart start_x2:$start_x2, start_y2:$start_y2");
    }
  });

  target.onTouchMove.listen((TouchEvent event) {
    event.preventDefault();

    ++ticks;

    // scrool
    if ((1 == event.touches.length) && (false == modeZoom)) {
      new_x1 = event.touches[0].page.x;
      new_y1 = event.touches[0].page.y;
      //print('onTouchMove 1: new_x1:$new_x1, new_x1:$new_y1');

      double dx_pc =  (start_x1 - new_x1) / window.innerWidth; // %
      double dy_pc = -(start_y1 - new_y1) / window.innerHeight; // %, Y down
      //print('onTouchMove 1: dx_pc:$dx_pc, dy_pc:$dy_pc, w:${window.innerWidth}, h:${window.innerHeight}');

      //if (true == doBlit1) {
      //doBlit1 = false;
      s52.drawBlit(dx_pc, dy_pc, 0.0, 0.0).then((ret) {
        //doBlit1 = true;
      });
      //}

      return;
    }

    // zoom (in/out)
    //if (2==event.touches.length && true==doBlit2) {
    if (2 == event.touches.length) {
      new_x1 = event.touches[0].page.x;
      new_y1 = event.touches[0].page.y;
      new_x2 = event.touches[1].page.x;
      //new_y2 = event.touches[1].page.y;

      //print('onTouchMove 2: new_x2:$new_x2, new_x2:$new_y2');

      //double dx1 = (start_x1 - new_x1).toDouble();
      //double dy1 = (start_y1 - new_y1).toDouble();
      //double dx2 = (start_x2 - new_x2).toDouble();
      //double dy2 = (start_y2 - new_y2).toDouble();
      int dx;
      //int dy;

      // out: |---->    <----|
      // in : <----|    |---->
      //        dx1       dx2
      if (start_x1 < start_x2) {
        dx = (new_x2 - new_x1) - (start_x2 - start_x1);
      } else {
        dx = (new_x1 - new_x2) - (start_x1 - start_x2);
      }
      /*
      if (start_y1 < start_y2) {
        dy = (new_y2 - new_y1) - (start_y2 - start_y1);
      } else {
        dy = (new_y1 - new_y2) - (start_y1 - start_y2);
      }
      */
      double dx_pc = dx / window.innerWidth;
      //double dy_pc = dy / window.innerHeight; // not used

      //if (true == doBlit2) {
      //doBlit2 = false;
      s52.drawBlit(0.0, 0.0, dx_pc / window.devicePixelRatio, 0.0).then((ret) {
        //doBlit2 = true;
      });
      zoom_fac = dx_pc;

      //}
      return;
    }
  });

  target.onTouchEnd.listen((TouchEvent event) {
    event.preventDefault();
    //#define TICKS_PER_TAP  6
    //#define EDGE_X0       50   // 0 at left
    //#define EDGE_Y0       50   // 0 at top
    //#define DELTA          5

    // short tap
    if (ticks < 3) {
      // do nothing if object allready displayed
      if ('inline-block' == querySelector('#svg1g').style.display)
        return;

      double x = start_x1 * window.devicePixelRatio;
      double y = (window.innerHeight - start_y1) * window.devicePixelRatio;
      s52.pickAt(x, y).then((ret) {
        TextElement svg1txt = querySelector('#svg1text');
        // set S52 UI Text Color
        svg1txt.setAttribute('style', 'fill:rgba(${s52.UINFF[0]},${s52.UINFF[1]},${s52.UINFF[2]}, 1.0);');
        svg1txt.text = '${ret[0]}';

        var x = start_x1 + 5;
        var y = start_y1 + 55;
        svg1txt.setAttribute('x', '$x');
        svg1txt.setAttribute('y', '$y');

        var rec = svg1txt.client;
        var w = rec.width  + 10;
        var h = rec.height + 10;

        RectElement svg1rec = querySelector('#svg1rect');
        svg1rec.setAttribute('width', '$w');
        svg1rec.setAttribute('height', '$h');
        svg1rec.setAttribute('x', '${start_x1}');
        svg1rec.setAttribute('y', '${start_y1}');
        // set S52 UI background & border color - test:display:inline-block;
        svg1rec.setAttribute('style', 'fill:rgba(${s52.UIBCK[0]},${s52.UIBCK[1]},${s52.UIBCK[2]}, 0.7);stroke:rgb(${s52.UIBDR[0]},${s52.UIBDR[1]},${s52.UIBDR[2]});display:inline-block;');

        querySelector('#svg1g').style.display = 'inline-block';

        s52.draw().then((ret) {
          s52.skipTimer = false;
        });
      });

      return;
    }

    // wait for last finger on the screen
    if (0 != event.touches.length) {
      //print('onTouchEnd: event.len=${event.touches.length} .. ');
      return;
    }

    // reset
    newTouch = false;

    // no touch move event - view unchange
    if (-1 == new_x1)
      return;

    // wait for s52.drawBlit() to return
    // 200ms found by trial and error
    new Timer(new Duration(milliseconds: 200), () {
      //double w = window.innerWidth  * window.devicePixelRatio; // not used
      double h = window.innerHeight * window.devicePixelRatio;

      // 2 fingers - Zoom
      if (true == modeZoom) {
        new_x1 = -1;
        new_y1 = -1;

        s52.getView().then((ret) {
          double cLat  = ret[0];
          double cLon  = ret[1];
          double rNM   = ret[2];
          double north = ret[3];
          //print("getView(): cLat:$cLat, cLon:$cLon, rNM:$rNM, north:$north");

          double rNMnew = rNM - (rNM * zoom_fac);
          rNMnew = (rNMnew < 0.0) ? -rNMnew : rNMnew; // ABS()
          s52.setView(cLat, cLon, rNMnew, north).then((ret) {
            s52.draw().then((ret) {
              s52.skipTimer = false;
            });
          });
        });
      }
      else // 1 finger - scroll
      {
        //print('scroll: w:$w, h:$h');
        s52.getView().then((ret) {
          double cLat  = ret[0];
          double cLon  = ret[1];
          double rNM   = ret[2];
          double north = ret[3];
          double x     = new_x1     * window.devicePixelRatio;
          double y     = h - new_y1 * window.devicePixelRatio;

          new_x1 = -1;
          new_y1 = -1;
          s52.xy2LL(x, y).then((ret) {
            double x1 = ret[0];  // lon
            double y1 = ret[1];  // lat
            double x  = start_x1     * window.devicePixelRatio;
            double y  = h - start_y1 * window.devicePixelRatio;
            s52.xy2LL(x, y).then((ret) {
              double x2 = ret[0];  // lon
              double y2 = ret[1];  // lat
              double dx = x2 - x1;
              double dy = y2 - y1;
              s52.setView(cLat + dy, cLon + dx, rNM, north).then((ret) {
                s52.draw().then((ret) {
                  s52.skipTimer = false;
                });
              });
            });
          });
        });
      } // if

      //s52.skipTimer = false;

    }); // timer
  }); // touchEnd

  target.onTouchCancel.listen((TouchEvent event) {
    event.preventDefault();
    print("onTouchCancel(): ...");
  });
}

//void _toggleUIEvent(evt) {
void _toggleUIEvent() {
  //evt.preventDefault();
  //var tbodyL = querySelector('#tbodyL').style;
  //print('s52ui.dart:_toggleUIEvent(): tbodyL=$tbodyL');

  // FIXME: style.invisible: "visible|hidden|collapse|inherit"
  if ('table' == querySelector('#tbodyL').style.display) {
    querySelector('#tbodyL').style.display = 'none';
    querySelector('#tbodyR').style.display = 'none';
    querySelector('#svg1'  ).style.display = 'inline-block';
    querySelector('#svg1c' ).style.display = 'inline-block';
    querySelector('#svg1g' ).style.display = 'inline-block';
      } else {
    querySelector('#tbodyL').style.display = 'table';
    querySelector('#tbodyR').style.display = 'table';
    querySelector('#svg1'  ).style.display = 'none';
    querySelector('#svg1c' ).style.display = 'none';
    querySelector('#svg1g' ).style.display = 'none'; // text group OFF
  }
}

void _fullList(evt) {
  evt.preventDefault();

  //_toggleUIEvent(evt);
  _toggleUIEvent();

  var txtL = querySelector('#svg1text').text.split(':');
  _listS57IDatt(txtL[1]);
}

//////////////// GPS & GYRO ////////////////////////////////////////////////////

int    _ownshpID  = 0;
double _devOrient = 0.0;

void _posError(PositionError error) {
  print("s52ui.dart:posError():Error occurred. Error code: ${error.code}");
}

void _GPSpos(Geoposition position) {
  //print('GPS new pos: ...');

  s52.pushPosition(_ownshpID, position.coords.latitude, position.coords.longitude, _devOrient).then((ret) {
    s52.setVector(_ownshpID, 1, _devOrient, 16.0);  // 1 - ground
  });
}

void _hdg(DeviceOrientationEvent o) {
  if (null == o.alpha)
    return;

  //_devOrient = 360.0 - 90.0 - o.alpha ;  // landscape
  _devOrient = 90.0 - o.alpha; // reverse landscape
  if (_devOrient < 0.0)
    _devOrient += 360.0;

  // debug
  //print('s52ui.dart:_hdg(): orient:$_devOrient, a:${o.alpha}, b:${o.beta}, g:${o.gamma}');
}

void _watchPosition(int ownshpID) {
  print('s5ui.dart:_watchPosition(): - beg -');

  _ownshpID = ownshpID;
  if (0 == _ownshpID) {
    print('s5ui.dart:_watchPosition(): failed, no _ownshpID handle');
    return;
  }

  // GYRO - TODO: test ship's head up - set north in setView
  window.onDeviceOrientation.listen(_hdg);

  // GPS
  window.navigator.geolocation.getCurrentPosition().then(_GPSpos, onError: (error) => _posError(error));

  // {'enableHighAccuracy':true, 'timeout':27000, 'maximumAge':30000}
  window.navigator.geolocation.watchPosition().listen(_GPSpos, onError: (error) => _posError(error));

  print('s5ui.dart:_watchPosition(): - end -');
}


///////////////////////////////////////
//
// Main
//

void main() {
  print('s5ui.dart:main(): start');

  var urlparam = window.location.search.toString();
  print('s52ui.dart:_initMain(): URL:>$urlparam<');
  if ("" != urlparam)
    wsUri = urlparam.substring(1, urlparam.length);  // trim '?'

  try {
    s52 = new S52();
    s52.initWS(wsUri).then((ret) {
      s52.newOWNSHP('OWNSHP').then((ret) {
        _watchPosition(ret[0]);

        // here the WebSocket init is completed - all JS loaded
        _initTouch();
        _toggleUIEvent();
        _initUI();
      }).catchError((e) {
        print(e);
      });
    });
  } catch (e, s) {
    print('s52ui.dart:ERROR: $e');
    print('s52ui.dart:STACK: $s');
    window.close();
  }
}
