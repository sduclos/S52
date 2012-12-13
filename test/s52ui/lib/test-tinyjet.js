//////////////////////////////////////////////////////////////////
//
// Main
//


var Main = function(aWindow)
{
    exports.Component.call(this, aWindow);
}
exports.inherits(Main, exports.Component);

Main.prototype.footer = function(html)
{
    html.footer(html.h1(html.p("<footer> test h1")),
                html.h2(html.p("<footer> test h2")),
                html.h3(html.p("<footer> test h3")),
                html.h4(html.p("<footer> test h4")),
                html.h5(html.p("<footer> test h5")),
                html.h6(html.p("<footer> test h6"))
               );

    html.time({datetime:"2008-02-14"}, "Valentines day");

    html.p(html.time({datetime:"2008-02-14"}, "Valentines day"));

    html.input({type:"number",   name:"quantity", min:"1", max:"5"});
    html.input({type:"submit",   value: "test value"});
    html.input({type:"checkbox", checked:"checked", disabled:"disabled"});
    html.input({type:"checkbox", checked:"checked"});

    html.input({list:"browsers", name:"browser"});
    html.datalist({id:"browsers"},
                  html.option({value:"Internet Explorer"}),
                  html.option({value:"Firefox"          })
                 );

    html.a({href:"http://www.w3schools.com"}, "This is a link");

    html.div({id:"content"},
             html.section({id:"left"},
                          html.ul({},
                                  html.li("LCoffee"),
                                  html.li("LTea"),
                                  html.li("LMilk")
                                 )
                         ),
             html.section({id:"rigth"},
                          html.ul({},
                                  html.li("RCoffee"),
                                  html.li("RTea"),
                                  html.li("RMilk")
                                 )
                         )
             );


    html.a({href:"http://www.w3schools.com"}, "This is a link");


    // table
    html.div({id:"tableContainer", class:"tableContainer"},
             html.table({border:"0", cellpadding:"0", cellspacing:"0", width:"100%", class:"scrollTable"},
                        html.thead({class:"fixedHeader"},
                                   html.tr({},
                                           html.th(html.a({href:"#"}, "Header A")),
                                           html.th(html.a({href:"#"}, "Header B")),
                                           html.th(html.a({href:"#"}, "Header C"))
                                          )
                                  ),

                        html.tbody({class:"scrollContent"},
                                   html.tr({},
                                           html.td("ln:1 Cell Content a"),
                                           html.td("ln:1 Cell Content b"),
                                           html.td("ln:1 Cell Content c")
                                          ),
                                   html.tr({},
                                           html.td("ln:2 More Cell Content a"),
                                           html.td("ln:2 More Cell Content b"),
                                           html.td(html.input({type:"checkbox", checked:"checked"}))
                                          ),
                                   html.tr({},
                                           html.td("ln:3 Even More Cell Content a"),
                                           html.td("ln:3 Even More Cell Content b"),
                                           html.td("ln:3 Even More Cell Content c")
                                          ),
                                   html.tr({},
                                           html.td("ln:4 Even More Cell Content a"),
                                           html.td("ln:4 Even More Cell Content b"),
                                           html.td("ln:4 Even More Cell Content c")
                                          ),
                                   html.tr({},
                                           html.td("ln:5 Even More Cell Content a"),
                                           html.td("ln:5 Even More Cell Content b"),
                                           html.td("ln:5 Even More Cell Content c")
                                          ),
                                   html.tr({},
                                           html.td("ln:6 Even More Cell Content a"),
                                           html.td("ln:6 Even More Cell Content b"),
                                           html.td("ln:6 Even More Cell Content c")
                                          ),
                                   html.tr({},
                                           html.td("ln:7 Even More Cell Content a"),
                                           html.td("ln:7 Even More Cell Content b"),
                                           html.td("ln:7 Even More Cell Content c")
                                          )
                                  )
                       ) // table
             );   // div
}


//html.p({bind:"XXXdesc1", title:"Free The Web"}, "Press the button");
//html.button({type    : "button"},
//            "OK"
//           );


//Main.prototype.renderSVG = function(html) {
Main.prototype.render = function(html) {
    var SVG = html.SVG();

    // animate work with xulrunner 2.0, html5 (Chrome)
    SVG.svg({width: '120px', height: '120px', version: "1.1"},
             SVG.circle({r: 50, cx: 60, cy: 60, style: 'stroke: red; fill: none; stroke-width: 20'},
                         SVG.animate({attributeName:'cx', to:200, dur:'10s'})
                       ),
             //onmouseover: onOver, onmouseout: onOut }),
             SVG.line  ({x1: 33, y1: 93, x2: 93, y2: 23, style: 'stroke: red; stroke-width: 20'})
             //onmouseover: onOver, onmouseout: onOut })
           );

    //fillStyle:"#FF0000", fillRect:"0,0,50,75;"
    html.canvas({id:'canvas4', width:200, height:100, style:"border:1px solid #c3c3c3;"});
    html.textarea({id:'output', rows:10, cols:40});
    
    
    html.insert(this.footer);
}


///////////////////////////////////////
//
// init
//

var _rootComponent;

function _main() {

    // both work so what the use for '_setTitle()' !
    //window._setTitle("htmljetZZZ");
    this.document.title = "htmljetXXX";

    _rootComponent = new Main(window);
    _rootComponent.beMainWindowComponent();

}
