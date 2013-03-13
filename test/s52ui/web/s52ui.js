//////////////////////////////////////////////////////////////////
//
// Main
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
        	html.article({},
        				 html.div({}, 
            	         		  html.input({id:arg[0], type:"checkbox"}),
        				 		  arg[1],
                  		          html.hr()
                  		         )
                        )
               )
           )
}

Main.prototype.rangeCell = function(html, arg)
{
	html.tr({},
    	html.td({},
        	html.article({},
        				 html.div({}, 
            	         		  html.input({id:arg[0], type:"range", min:"0", max:"360", step:"10"}),
        				 		  arg[1],
                  		          html.hr()
                  		         )
                        )
               )
           )
}

Main.prototype.buttonCell = function(html, arg)
{
	html.tr({},
    	html.td({id:arg[0]},
        	    html.div({id:'buttonCell', class:'buttonCell'}, arg[1]),
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
{   //, cellpadding:"0", cellspacing:"0" , width:"50%"
    html.table({id:'tableL', class:"scrollTableL", border:"0", cellpadding:"0", cellspacing:"0"},
               html.tbody({id:"tbodyL", class:"scrollContentL"},

//=========== 'MARINERS OPTIONS' ==========================================
                          html.insert(this.sectionTitle, 'MARINERS OPTIONS'),
//*
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
//*/
                        ) // tbody
              ); // table


    // , cellpadding:"0", cellspacing:"0", width:"50%"
    html.table({id:'tableR', class:"scrollTableR", border:"0", cellpadding:"0", cellspacing:"0"},
               html.tbody({id:"tbodyR", class:"scrollContentR"})
              ); // table


}

Main.prototype.render = function(html)
{
    //*
    var SVG = html.SVG();
    //SVG.svg({id:'svg1', width:'120px', height:'120px', version:'1.1'},
    SVG.svg({id:'svg1', width:'100%', height:'100%', version:'1.1'},
            SVG.circle({r:'50', cx:'60', cy:'60', style:"stroke:red;fill:none;stroke-width:20"},
                       SVG.animate({attributeName:'cx', from:'60', to:'200', dur:'10s'})
                       ),
            SVG.line  ({x1:'33', y1:'93', x2:'93', y2:'23', style:"stroke:blue;stroke-width:20"})
            //SVG.line  ({x1:'33', y1:'93', x2:'93', y2:'23', stroke:'red', stroke-width:'20'})
            //onmouseover:onOver, onmouseout:onOut })
           );

    //html.textarea({id:'output1', rows:10, cols:40});
    //html.hr();
    //*/

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
}
