// s52ui.dart: html/websocket test driver for libS52.so

library s52ui;

import 'dart:html';
import 'dart:svg';
import 'dart:json';
import 'dart:async';
import 'package:js/js.dart' as js;

part 'S52.dart';

S52  s52;

var _width;
var _height;

  _handleInput(int param, double value) {
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
      query(".scrollTableL").style.backgroundColor =
          "rgba(${s52.UIBCK[0]},${s52.UIBCK[1]},${s52.UIBCK[2]}, 0.7)";
      // set S52 UI Border Color
      queryAll("hr").forEach((s) => s.style.backgroundColor =
           "rgb(${s52.UIBDR[0]},${s52.UIBDR[1]},${s52.UIBDR[2]})");
      // set S52 UI Text Color
      queryAll("div").forEach((s) => s.style.color =
           "rgb(${s52.UINFF[0]},${s52.UINFF[1]},${s52.UINFF[2]})");

      completer.complete(true);
    });
    
    return completer.future;
  }

  _updateUIcol(int idx, TableCellElement c) {
    s52.setMarinerParam(S52.MAR_COLOR_PALETTE, idx.toDouble()).then((ret) {
      s52.draw().then((ret) {
        //s52.drawLast().then((ret){
          _setUIcolor().then((ret) {});
        //});
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

    TableElement    t = query("#tableR");
    TableRowElement r = t.insertRow(-1);  // add at the end
    r.nodes.add(c);
    
    return c;
  }
  _clearTable(id) {
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
  /////////////// S57ID Button Handling //////////////////////////
  void _listS57IDatt(var S57ID) {
    _clearTable("#tableR");

    s52.getAttList(int.parse(S57ID)).then((ret) {
      print('ret: ${ret[0]}');
      
      var Att = ret[0].split(',');
      for (int i=0; i<Att.length; ++i) {
        _appendCellRTable(Att[i], null, i);
      }
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

//*
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

          //var svg = query("#svg1circle");
          
          int startIdx = 0;
          _initCheckBox(_checkButton, startIdx, "i", completer);
        });
      });
    });
  });
  //onError: (AsyncError e) {
  //  print('call failed');
  //}
  //);

  
  return completer.future;
}
//*/

void _orientationChg(var orientation, var width, var height)
{
  // debug
  print("++++++++++++++++++++++++++++++++++++++orientation:$orientation W:$width x H:$height");
  
  _width  = width;
  _height = height;
  //s52.width  = width;
  //s52.height = height;
  
  s52.setViewPort(0, 0, width, height).then((ret) {
    s52.draw().then((ret) {});
  });

  // rehook callback for next rotation
  //js.scoped(() {
  //  js.context.orientationChg = new js.Callback.once(_orientationChg);
  //});
}

void _initTouch(var orient, var w, var h) {
  // debug
  print("+++++++++++++++++++++++++++_initTouch(): O:$orient W:$w x H:$h");

  // Handle touch events.
  Element target = query('#svg1');

  bool doBlit1  = true;
  bool doBlit2  = true;
  bool modeZoom = false;
  bool newTouch = false;

  int start_x1 =  0;
  int start_y1 =  0;
  int start_x2 =  0;
  int start_y2 =  0;
  int new_x1   = -1;
  int new_y1   = -1;
  int new_x2   =  0;
  int new_y2   =  0;
  int ticks    =  0;
  
  double dx_pc =  0.0; 
  double dy_pc =  0.0; 

  double cLat  =  0.0;
  double cLon  =  0.0;
  double rNM   = -1.0;
  double north = -1.0;
  
  // FIXME: get w/h on orientation change msg
  // Dart broken on android WebView
  //var width   = window.innerWidth;   
  //var height  = window.innerHeight;
  //var width   = 1208;
  //var height  =  752;
  _width  = w;
  _height = h;
  //s52.width  = w;
  //s52.height = h;

  
  target.onTouchStart.listen((TouchEvent event) {
    event.preventDefault();
    
    // FIXME: move this to browser start
    if (false == newTouch) {
      newTouch = true;
      modeZoom = false;
      ticks    = 0;
      
      //* FIXME: cLat/cLon doesn't propagate to touchEnd
      s52.getView().then((ret){
        cLat  = ret[0];
        cLon  = ret[1];
        rNM   = ret[2];
        north = ret[3];
        print("getView(): cLat:$cLat, cLon:$cLon, rNM:$rNM, north:$north");
      });
      //*/
    }

    if (1 == event.touches.length) {
      start_x1 = event.touches[0].page.x;  //pageX;
      start_y1 = event.touches[0].page.y;
      
      print("onTouchStart start_x1:$start_x1, start_y1:$start_y1");

      doBlit1 = true;
      doBlit2 = false;
    }
    
    if (2 == event.touches.length) {
      start_x1 = event.touches[0].page.x;
      start_y1 = event.touches[0].page.y;
      start_x2 = event.touches[1].page.x;
      start_y2 = event.touches[1].page.y;
      
      print("onTouchStart start_x2:$start_x2, start_y2:$start_y2");
      
      modeZoom = true;
      
      doBlit1 = false;
      doBlit2 = true;
    }
  });
  
  target.onTouchMove.listen((TouchEvent event) {
    event.preventDefault();
    
    // trim the number of touchMove event 
    //if (0 != (++ticks % 5))
    //  return;
    ++ticks;    
    
    // scrool
    if ((1==event.touches.length) && (false==modeZoom)) {
      new_x1 = event.touches[0].page.x;
      new_y1 = event.touches[0].page.y;
    
      //print('onTouchMove 1: new_x1:$new_x1, new_x1:$new_y1');
    
      double dx_pc =  (start_x1 - new_x1) / _width;  // %
      double dy_pc = -(start_y1 - new_y1) / _height; // % - Y down
      //double dx_pc =  (start_x1 - new_x1) / s52.width;  // %
      //double dy_pc = -(start_y1 - new_y1) / s52.height; // % - Y down

      if (true == doBlit1) {
        doBlit1 = false;
        s52.drawBlit(dx_pc/2.0, dy_pc, 0.0, 0.0).then((ret) {doBlit1 = true;});
      }
    }
    
    // zoom (in/out)
    if (2==event.touches.length && true==doBlit2) {
      new_x1 = event.touches[0].page.x;
      new_y1 = event.touches[0].page.y;
      new_x2 = event.touches[1].page.x;
      new_y2 = event.touches[1].page.y;
    
      //print('onTouchMove 2: new_x2:$new_x2, new_x2:$new_y2');
    
      double dx1 = (start_x1 - new_x1).toDouble();
      double dy1 = (start_y1 - new_y1).toDouble();
      double dx2 = (start_x2 - new_x2).toDouble();
      double dy2 = (start_y2 - new_y2).toDouble();
      int dx;
      int dy;
      
      // out: |---->    <----|
      // in : <----|    |---->
      //        dx1       dx2
      if (start_x1<start_x2) {
        dx = (new_x2 - new_x1) - (start_x2 - start_x1);
      } else {
        dx = (new_x1 - new_x2) - (start_x1 - start_x2);
      }
      
      //dx_pc = (dx1 + dx2) / width;  // %
      //dy_pc = (dy1 + dy2) / height; // %
      dx_pc = dx1 / _width;  // %
      //dx_pc = dx1 / s52.width;  // %
      //if (true == doBlit2) {
        doBlit2 = false;
        s52.drawBlit(0.0, 0.0, dx_pc, 0.0).then((ret) {doBlit2 = true;});
      //}
    }
  });
  
  target.onTouchEnd.listen((TouchEvent event) {
    event.preventDefault();
    //#define TICKS_PER_TAP  6
    //#define EDGE_X0       50   // 0 at left
    //#define EDGE_Y0       50   // 0 at top
    //#define DELTA          5
    if (ticks < 6) {
      if ('inline-block' == query('#svg1g').style.display)
        return;
      
      s52.pickAt(start_x1.toDouble(), _height - start_y1.toDouble()).then((ret) {
        TextElement svg1txt = query('#svg1text');
        // set S52 UI Text Color
        svg1txt.$dom_setAttribute('style', 'fill:rgba(${s52.UINFF[0]},${s52.UINFF[1]},${s52.UINFF[2]}, 1.0);');
        svg1txt.text = '${ret[0]}';

        var x = start_x1 +  5;
        var y = start_y1 + 55;
        svg1txt.$dom_setAttribute('x', '$x');
        svg1txt.$dom_setAttribute('y', '$y');

        var rec = svg1txt.client;
        var w   = rec.width  + 10;
        var h   = rec.height + 10;

        RectElement svg1rec = query('#svg1rect');
        svg1rec.$dom_setAttribute('width',  '$w');
        svg1rec.$dom_setAttribute('height', '$h');
        svg1rec.$dom_setAttribute('x', '${start_x1}');
        svg1rec.$dom_setAttribute('y', '${start_y1}');
        // set S52 UI background & border color - test:display:inline-block;
        svg1rec.$dom_setAttribute('style', 'fill:rgba(${s52.UIBCK[0]},${s52.UIBCK[1]},${s52.UIBCK[2]}, 0.7);stroke:rgb(${s52.UIBDR[0]},${s52.UIBDR[1]},${s52.UIBDR[2]});display:inline-block;'
                                 );

        query('#svg1g').style.display = 'inline-block';
        
        s52.draw().then((ret) {});
      });

      return;
    }
    

    // wait for last finger on the screen
    if (0 != event.touches.length) {
      print('onTouchEnd: event.len=${event.touches.length} .. ');
      return;
    }
    
    // reset 
    newTouch = false;
    
    // no touch move event - view unchange
    if (-1 == new_x1)
      return;

    // wait for s52.drawBlit() to return
    // 200ms found by trial and error 
    new Timer(new Duration(milliseconds: 300), () {
      
      //print('onTouchEnd zoom:$modeZoom');      
      
      // 2 finger - Zoom
      if (true == modeZoom) {
        new_x1 = -1;
        new_y1 = -1;

        //double rNMnew = rNM - (rNM * dxy_pc * 2.0);  // 
        double rNMnew = rNM - (rNM * dx_pc * 2.0);  // x2: because .. 
        rNMnew = (0 < rNMnew) ? rNMnew : (-rNMnew);  // ABS()
        print("dx_pc:$dx_pc, dy_pc:$dy_pc, rNM:$rNM, rNMnew:$rNMnew, (rNM * dx_pc):${(rNM * dx_pc)}");
        //s52.setView(cLat, cLon, rNMnew, -1.0).then((ret) {
        //  s52.draw().then((ret) {});
        //});

        //*
        s52.getView().then((ret){
          cLat  = ret[0];
          cLon  = ret[1];
          rNM   = ret[2];
          north = ret[3];
          print("getView(): cLat:$cLat, cLon:$cLon, rNM:$rNM, north:$north");
          s52.setView(cLat, cLon, rNMnew, -1.0).then((ret) {
            s52.draw().then((ret) {});
          });
        });
        //*/
      } 
      else  // 1 finger - scroll
      {
        //print('setView start_x1:$start_x1, start_y1:$start_y1, new_x1:$new_x1, new_y1:$new_y1');
        s52.getView().then((ret){
          cLat  = ret[0];
          cLon  = ret[1];
          rNM   = ret[2];
          north = ret[3];
          print("getView(): cLat:$cLat, cLon:$cLon, rNM:$rNM, north:$north");
          s52.xy2LL(new_x1.toDouble(), _height - new_y1.toDouble()).then((ret) {
          //s52.send("S52_xy2LL", [new_x1, new_y1]).then((ret) {
            double x1 = ret[0];  // lon
            double y1 = ret[1];  // lat
            new_x1 = -1;
            new_y1 = -1;
            s52.xy2LL(start_x1.toDouble(), _height - start_y1.toDouble()).then((ret) {
              double x2 = ret[0]; // lon
              double y2 = ret[1]; // lat
              double dx = x1 - x2;
              double dy = y1 - y2;
  
              // FIXME: cLat/cLon doesn't propagate to touchEnd
              // but here it does .. sometime!
              s52.setView(cLat - dy, cLon - dx, -1.0, -1.0).then((ret) {
                s52.draw().then((ret) {});
              });
            });
          });
        });
      }   // if
     });  // timer
  });     // touchEnd
}

//void GPSpos(Geoposition position) {
//  print('GPS new pos');
//  s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
//}
int _ownshp = 0;
void _watchPosition() {
  if (0 == _ownshp) { 
    print('s5ui.dart:_setPosition(): failed, no _ownshp handle');
    return;
  }
  print('s5ui.dart:_watchPosition(): start');
  
  // {'enableHighAccuracy':true, 'timeout':27000, 'maximumAge':30000}
  //window.navigator.geolocation.watchPosition().listen(onData, onError, onDone, unsubscribeOnError)
  
  //try {
  //  window.navigator.geolocation.watchPosition().listen(GPSpos);
  //} catch (e,s) {
  //  print(s);
  //}
  
  
  //subscribe(onData: (List<int> data) { print(data.length); });
  /* FF choke here
  window.navigator.geolocation.getCurrentPosition().then(
    (Geoposition position) {
      s52.setPosition(_ownshp, position.coords.latitude, position.coords.longitude, 0.0).then((ret){});
    }
  );
  // */
}

_toggleUIEvent(evt) {
  
  if ('none' == query('#tbodyL').style.display) {
    query('#tbodyL').style.display = 'table';
    query('#tbodyR').style.display = 'table';
    query('#svg1'  ).style.display = 'none';
  } else {
    query('#tbodyL').style.display = 'none';
    query('#tbodyR').style.display = 'none';
    query('#svg1'  ).style.display = 'inline-block';
    query('#svg1g' ).style.display = 'none';
  }

  //js.scoped(() {
  //  js.context.toggleUIEvent = new js.Callback.once(_toggleUIEvent);
  //});
}

_fullList(ev) {
  ev.preventDefault();
  
  
  _toggleUIEvent(ev);
  
  var txtL = query('#svg1text').text.split(':');
  _listS57IDatt(txtL[1]); 
}

///////////////////////////////////////
//
// Main
//

void _initMain() {
  //print('s52ui.dart:_init()');
  s52 = new S52();

  s52.newOWNSHP('OWNSHP').then((ret) {
    //print('s5ui.dart:OWNSHP(): $ret');
    _ownshp = ret[0]; 
    
    // can't read GPS
    //_watchPosition();
    
    query('#svg1g').onTouchStart.listen((ev) { _fullList(ev); });

    _initUI().then((ret) {});
  });
}

void main() {
  print('s5ui.dart:main(): start');

  js.context.wsReady        = new js.Callback.once(_initMain);
  js.context.setTouchScrnSz = new js.Callback.once(_initTouch);
  js.context.orientationChg = new js.Callback.many(_orientationChg);
  js.context.toggleUIEvent  = new js.Callback.many(_toggleUIEvent);
}
