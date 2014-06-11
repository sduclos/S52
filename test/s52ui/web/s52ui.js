// s52ui.js: helper functional template for
//           DOM loading via tinyjet.js (witch is base on XULjet.js)
//


//////////////////////////////////////////////////////////////////
//
// Main (Template)
//

var Main = function(aWindow)
{
    exports.Component.call(this, aWindow);
}
exports.inherits(Main, exports.Component);

Main.prototype.checkCell = function(html, arg)
{
    html.tr({},
        html.td({},
                html.div({},
                         arg[1],
                         html.input({id:arg[0], type:'checkbox'}),
                         html.hr()
                        )
               )
           )
}

Main.prototype.numCell = function(html, arg)
{
  html.tr({},
      html.td({},
            html.div({},
                     arg[1],
                     html.input({id:arg[0], type:'number'}),
                     html.hr()
                    )
               )
           )
}

Main.prototype.rangeCell = function(html, arg)
{
    html.tr({},
        html.td({},
                html.div({},
                         html.input({id:arg[0], type:'range', min:'0', max:'360', step:'10'}),
                         arg[1],
                         html.hr()
                        )
               )
           )
}

Main.prototype.buttonCell = function(html, arg)
{
    html.tr({},
            html.td({id:arg[0]},
                    html.div({id:'buttonCell'}, arg[1]),
                    html.hr()
                   )
           )
}

Main.prototype.sectionTitle = function(html, txt)
{
    html.tr({},
        html.th({id:'td'},
            html.h3({id:'sectionTitle'},
                    html.div({}, txt),
                    html.hr()
                   )
               )
           )
}

Main.prototype.tables = function(html)
{   // width:"50%"
    html.table({id:'tableL'},
               html.tbody({id:'tbodyL'},

//=========== 'MARINERS OPTIONS' ==========================================
                          html.insert(this.sectionTitle, 'MARINERS OPTIONS'),
                          html.insert(this.buttonCell, ['td_buttonCell0', 'Close s52ui']),
                          html.insert(this.buttonCell, ['td_buttonCell1', 'Color Palettes']),
                          html.insert(this.buttonCell, ['td_buttonCell2', 'AIS']),
                          html.insert(this.buttonCell, ['td_buttonCell3', 'ENC']),

                          // i - Input checkbox
                          html.insert(this.checkCell, ['i1','Show Text']),

                          //  S52_MAR_DISP_CATEGORY(14)
                          // c - Category
                          html.insert(this.checkCell, ['c0','Display Base']),
                          html.insert(this.checkCell, ['c1','Display Standard']),
                          html.insert(this.checkCell, ['c2','Display Other']),
                          html.insert(this.checkCell, ['c4','Display Selection (override)']),

                          // S52_MAR_DISP_LAYER_LAST(27)
                          // l - Last
                          html.insert(this.checkCell, ['l8', 'Display Mariners None']),
                          html.insert(this.checkCell, ['l16','Display Mariners Standard']),
                          html.insert(this.checkCell, ['l32','Display Mariners Other']),
                          html.insert(this.checkCell, ['l64','Display Mariners Selection (override)']),

                          // I - Input Number
                          html.insert(this.numCell,   ['I3', 'SAFETY CONTOUR (line)']),
                          html.insert(this.numCell,   ['I5', 'SHALLOW CONTOUR (area)']),
                          html.insert(this.numCell,   ['I6', 'DEEP CONTOUR (area)']),
                          html.insert(this.numCell,   ['I4', 'SAFETY DEPTH (sndg)']),

//========== 'EXPERIMENTAL / DEBUG' =======================================
                          html.insert(this.sectionTitle, 'EXPERIMENTAL / DEBUG'),

                          // i - Input checkbox
                          html.insert(this.checkCell, ['i23','Scamin']             ),
                          html.insert(this.checkCell, ['i42','Show World Map']     ),
                          html.insert(this.checkCell, ['i24','Anti-Alias']         ),
                          html.insert(this.checkCell, ['i40','Synthetic Afterglow']),
                          html.insert(this.checkCell, ['i36','Calibration Symbols']),
                          html.insert(this.checkCell, ['i32','Legend']             ),
                          html.insert(this.checkCell, ['i41','Centroids']          ),
                          html.insert(this.checkCell, ['i25','Quality Of Position']),
                          html.insert(this.checkCell, ['i37','Dredge Pattern']     ),
                          html.insert(this.checkCell, ['i38','Nodata Layer']       ),

                          // r - input Range
                          html.insert(this.rangeCell, ['r28','Rot. Buoy Light']  ),

                          //  S52_MAR_CMD_WRD_FILTER(33)
                          // f - Filter
                          html.insert(this.checkCell, ['f1', 'CMD_WRD_FILTER_SY']),
                          html.insert(this.checkCell, ['f2', 'CMD_WRD_FILTER_LS']),
                          html.insert(this.checkCell, ['f4', 'CMD_WRD_FILTER_LC']),
                          html.insert(this.checkCell, ['f8', 'CMD_WRD_FILTER_AC']),
                          html.insert(this.checkCell, ['f16','CMD_WRD_FILTER_AP']),
                          html.insert(this.checkCell, ['f32','CMD_WRD_FILTER_TX'])
                        ) // tbody
              ); // table


    // width:"50%"
    html.table({id:'tableR'},
               html.tbody({id:'tbodyR'})
              ); // table


}

Main.prototype.render = function(html)
{
    var radius = '50%';
    if (window.innerWidth < window.innerHeight)
        radius = window.innerWidth  / 2.0;
    else
        radius = window.innerHeight / 2.0;

    var SVG = html.SVG();
    SVG.svg({id:'svg1', width:'100%', height:'100%'},
            SVG.g({id:'svg1g'},
                  SVG.rect({id:'svg1rect', x:'100', y:'100', rx:'10', ry:'10', width:'300', height:'64', style:'fill:rgba(0,0,255, 0.5);stroke-width:1;'}),
                  SVG.text({id:'svg1text', x:'105', y:'155', style:'fill:rgb(0,255,255);'},'SVG')
                 ),
            SVG.circle({id:'svg1c', r:radius, cx:'50%', cy:'50%', style:'stroke:red;fill:none;stroke-width:1;'})
           );

    html.insert(this.tables);
}


///////////////////////////////////////
//
// init
//

var _rootComponent;

function _main() {

    this.document.title = "s52ui/tinyjet";

    _rootComponent = new Main(window);
    _rootComponent.beMainWindowComponent();
};



//----------------------- J U N K ----------------------------------------------
/* test
    SVG.svg({id:'svg2', width:'100%', height:'100%', version:'1.1'},
          SVG.circle({id:'svg1circle', r:radius, cx:'50%', cy:'50%', style:'stroke:red;fill:none;stroke-width:1;'})
          );

          SVG.circle({id:'svg1circle1', r:radius-10, cx:'50%', cy:'50%',
           style:'stroke:red;fill:none;stroke-width:10;stroke-dasharray:1,67;'}),
            SVG.line({x1:'50%', y1:'0',   x2:'50%',             y2:window.innerHeight, style:'stroke:blue;stroke-width:1;'}),
            SVG.line({x1:'0',   y1:'50%', x2:window.innerWidth, y2:'50%',              style:'stroke:blue;stroke-width:1;'})

          SVG.circle({id:'svg1circle1', r:radius-5, cx:'50%', cy:'50%',
           style:'stroke:red;fill:none;stroke-width:5;stroke-dasharray:10,10;'}),

    SVG.svg({id:'svg1'},
            SVG.text({id:'svg1text', x:'20%', y:'20%', fill:'red'},
            SVG.line  ({x1:'0', y1:'0', x2:'100', y2:'200', style:"stroke:blue;stroke-width:20"})
            SVG.line  ({x1:'30', y1:'90', x2:'90', y2:'30', stroke:'red', stroke-width:'20'})
            //onmouseover:onOver, onmouseout:onOut })
            //SVG.animate({attributeName:'cx', from:'60', to:'200', dur:'30s'})


    html.textarea({id:'output1', rows:10, cols:40});
    html.hr();
*/

