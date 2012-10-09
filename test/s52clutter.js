//s52clutter.js: drive libS52.so from Gome-shell sandbox
//               Javascript (gjs) and clutter
//
// SD 2011MAR05 - created

const Clutter = imports.gi.Clutter;
const Cogl    = imports.gi.Cogl;
const St      = imports.gi.St;

const Lang    = imports.lang;
//const UI      = imports.testcommon.ui;
//UI.init();


Clutter.init(null,null);


const S52     = imports.gi.S52;
let version   = S52.version();
print(version);

let ret = S52.init(1280, 800, 261, 163, null);

let _vessel_arpa;
let _vessel_ais;

let _view = (function()
{
    this.cLat;
    this.cLon;
    this.rNM;
    this.north;     // center of screen (lat,long), range of view(NM)

    return this;
})();

function _initView() /*FOLD00*/
{
    let [ret, R, G, B] = S52.getRGB("NODTA", R, G, B);
    print("NODTA RGB:", R, G, B);

    let encPath = null;
    let ret, S=0, W=0, N=0, E=0;
    [ret, S, W, N, E] = S52.getCellExtent(encPath, S, W, N, E);
    if (false == ret)
        return false;

    print("extent: S, W, N, E", S,W,N,E);

    _view.cLat  =  (N + S) / 2.0;
    _view.cLon  =  (E + W) / 2.0;
    _view.rNM   = ((N - S) / 2.0) * 60.0;
    _view.north = 0.0;

    //center of BEC (CA579016.000): lat:46.405833, long:-72.375000, range:1.350000 north:0.000000
    //_view.cLat  =  46.405833;
    //_view.cLon  = -72.375000;
    //_view.rNM   =   3.0;
    //_view.north =   0.0;

    return true;
}

function _setVESSEL()
{
    //_computeView(&_view);

    // ARPA
    _vessel_arpa = S52.newVESSEL(1, "ARPA label");
    S52.pushPosition(_vessel_arpa, _view.cLat + 0.01, _view.cLon - 0.02, 045);
    S52.setVector(_vessel_arpa, 2, 060, 3);   // water

    // AIS active
    _vessel_ais = S52.newVESSEL(2, "MV Non Such");
    S52.setDimension(_vessel_ais, 100, 100, 15, 15);
    S52.pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045);
    S52.setVector(_vessel_ais, 1, 060, 3);   // ground

    // (re) set label
    S52.setVESSELlabel(_vessel_ais, "~~MV Non Such~~");
    S52.setVESSELstate(_vessel_ais, 0, 1, 0);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_setPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);

    // VTS (this will not draw anything!)
    //_vessel_vts = S52_newVESSEL(3, dummy);

    return true;
}

 /*fold00*/
let _marfea_area;
let _marfea_line;
let _marfea_point;
function _setMarFeature() /*FOLD00*/
// exemple to display something define in the PLib directly
{
    /*
    // CCW doesn't center text
    double xyz[5*3] = {
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.005, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.005, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.000, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
    };
    */

    //double xyzArea[5*3]  = {
    let xyzArea = [
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.000, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.005, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.005, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0
    ];
    print('xyzArea:', xyzArea);

    //_marfea_line  = S52.newMarObj("marfea", S52.ObjectType.LINES, 2, xyzLine,  attVal);
    //_marfea_point = S52.newMarObj("marfea", S52.ObjectType.POINT, 1, xyzPoint, attVal);

    // LINE
    //double xyzLine[2*3]  = {
    //let xyzLine = [
    //    _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
    //    _view.cLon + 0.02, _view.cLat - 0.005, 0.0
    //];

    // debug
    //double xyzLine[2*3]  = {
    //    -72.3166666, 46.416666, 0.0,
    //    -72.4,       46.4,      0.0
    //};

    // POINT
    //double xyzPoint[1*3] = {
    //let xyzPoint = [
    //    _view.cLon - 0.02, _view.cLat - 0.005, 0.0
    //];

    let objName = "marfea";
    //let objName = "dnghlt";
    let xyznbrmax  = 5;
    let listAttVal = "OBJNAM:6.5_marfea";

    _marfea_area = S52.newMarObj(objName, S52.ObjectType.AREAS, xyznbrmax, null,  listAttVal);

    print('_marfea_area:', _marfea_area);

    // AREA (CW: to center the text)
    S52.pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon + 0.00, 0.0);
    S52.pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon - 0.01, 0.0);
    S52.pushPosition(_marfea_area, _view.cLat + 0.005, _view.cLon - 0.01, 0.0);
    S52.pushPosition(_marfea_area, _view.cLat + 0.005, _view.cLon + 0.00, 0.0);
    S52.pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon + 0.00, 0.0);


    return true;
}

//let _my_S52_loadObject_cb = function(feature, objname)
function _my_S52_loadObject_cb(objname, feature) /*fold00*/
//function _my_S52_loadObject_cb(feature)
{
    //
    // .. do something cleaver with each object of a layer ..
    //

    // this fill the terminal
    print('s52clutter.js:OJECT: NAME:', objname, 'feature:', feature);
    //print('s52clutter.js:OJECT: feature:', feature);

    //S52.loadObject(objname, user_data);
    //S52.loadObject('test', feature);

    return true;
}

function _setupS52() /*FOLD00*/
{
    let ret = false;

    // load default cell (in s52.cfg)
    //ret = S52.loadCell(null, null);
    ret = S52.loadCell('ENC_ROOT/CA579016.000', null);
    //ret = S52.loadCell('ENC_ROOT/CA579016.000', _my_S52_loadObject_cb);

    _initView();

    ////////////////////////////////////////////////////////////
    //
    // setup supression of chart object (for debugging)
    //
    // supresse display of adminitrative objects when
    // S52_MAR_DISP_CATEGORY is SELECT, to avoir cluttering
    //S52_toggleObjClass("M_NSYS");   // cell limit (line complex --A--B-- ), buoyage (IALA)
    S52.toggleObjClass("M_COVR");   // ??
    S52.toggleObjClass("M_NPUB");   // ??
    S52.toggleObjClass("M_QUAL");   // U pattern

    // debug
    ret = S52.toggleObjClassOFF("M_QUAL");  // OK - ret == TRUE
    ret = S52.toggleObjClassON ("M_QUAL");  // OK - ret == TRUE
    ret = S52.toggleObjClassON ("M_QUAL");  // OK - ret == FALSE

    // test
    //S52_toggleObjClass("DRGARE");   // drege area



    ////////////////////////////////////////////////////////////
    //
    // setup internal variable to decent value for debugging
    //
    /*
    S52_setMarinerParam("S52_MAR_SHOW_TEXT",       1.0);
    S52_setMarinerParam("S52_MAR_TWO_SHADES",      0.0);
    S52_setMarinerParam("S52_MAR_SAFETY_CONTOUR", 10.0);
    S52_setMarinerParam("S52_MAR_SAFETY_DEPTH",   10.0);
    S52_setMarinerParam("S52_MAR_SHALLOW_CONTOUR", 5.0);
    S52_setMarinerParam("S52_MAR_DEEP_CONTOUR",   11.0);

    S52_setMarinerParam("S52_MAR_SHALLOW_PATTERN", 0.0);
    //S52_setMarinerParam("S52_MAR_SHALLOW_PATTERN", 1.0);

    S52_setMarinerParam("S52_MAR_SHIPS_OUTLINE",   1.0);
    S52_setMarinerParam("S52_MAR_DISTANCE_TAGS",   0.0);
    S52_setMarinerParam("S52_MAR_TIME_TAGS",       0.0);
    S52_setMarinerParam("S52_MAR_BEAM_BRG_NM",     1.0);

    S52_setMarinerParam("S52_MAR_FULL_SECTORS",    1.0);
    S52_setMarinerParam("S52_MAR_SYMBOLIZED_BND",  1.0);
    S52_setMarinerParam("S52_MAR_SYMPLIFIED_PNT",  1.0);

    //S52_setMarinerParam("S52_MAR_DISP_CATEGORY",  2.0);  // OTHER
    S52_setMarinerParam("S52_MAR_DISP_CATEGORY",   1.0);  // STANDARD (default)

    S52_setMarinerParam("S52_MAR_COLOR_PALETTE",   0.0);  // first palette

    //S52_setMarinerParam("S52_MAR_FONT_SOUNDG",    1.0);
    S52_setMarinerParam("S52_MAR_FONT_SOUNDG",     0.0);

    S52_setMarinerParam("S52_MAR_DATUM_OFFSET",    0.0);
    //S52_setMarinerParam("S52_MAR_DATUM_OFFSET",    5.0);

    S52_setMarinerParam("S52_MAR_SCAMIN",          1.0);
    //S52_setMarinerParam("S52_MAR_SCAMIN",          0.0);

    // remove clutter
    S52_setMarinerParam("S52_MAR_QUAPNT01",        0.0);
    */

    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       15.0);
    //S52.setMarinerParam(S52._MAR_param_t.MAR_SHOW_TEXT,      60.0);
    S52.setMarinerParam(S52.MarinerParameter.SHOW_TEXT,      60.0);
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       0.0);

    S52.setMarinerParam(S52.MarinerParameter.TWO_SHADES,      0.0);
    S52.setMarinerParam(S52.MarinerParameter.SAFETY_CONTOUR, 10.0);
    S52.setMarinerParam(S52.MarinerParameter.SAFETY_DEPTH,   10.0);
    S52.setMarinerParam(S52.MarinerParameter.SHALLOW_CONTOUR, 5.0);
    S52.setMarinerParam(S52.MarinerParameter.DEEP_CONTOUR,   11.0);

    S52.setMarinerParam(S52.MarinerParameter.SHALLOW_PATTERN, 0.0);
    //S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 1.0);

    S52.setMarinerParam(S52.MarinerParameter.SHIPS_OUTLINE,   1.0);
    //S52_setMarinerParam(S52_MAR_DISTANCE_TAGS,   1.0);
    S52.setMarinerParam(S52.MarinerParameter.DISTANCE_TAGS,   0.0);
    S52.setMarinerParam(S52.MarinerParameter.TIME_TAGS,       0.0);
    S52.setMarinerParam(S52.MarinerParameter.HEADNG_LINE,     1.0);
    //S52_setMarinerParam(S52_MAR_HEADNG_LINE,     0.0);
    S52.setMarinerParam(S52.MarinerParameter.BEAM_BRG_NM,     1.0);

    //S52_setMarinerParam(S52_MAR_FULL_SECTORS,    1.0);
    S52.setMarinerParam(S52.MarinerParameter.FULL_SECTORS,    0.0);
    S52.setMarinerParam(S52.MarinerParameter.SYMBOLIZED_BND,  1.0);
    S52.setMarinerParam(S52.MarinerParameter.SYMPLIFIED_PNT,  1.0);

    //S52.setMarinerParam(S52.MarinerParameter.DISP_CATEGORY,   1.0);  // STANDARD (default)
    //S52.setMarinerParam(S52.MarinerParameter.DISP_CATEGORY,   2.0);  // OTHER
    S52.setMarinerParam(S52.MarinerParameter.DISP_CATEGORY,   3.0);  // SELECT (all)

    S52.setMarinerParam(S52.MarinerParameter.COLOR_PALETTE,   0.0);  // first palette

    //S52_setMarinerParam(S52_MAR_FONT_SOUNDG,    1.0);
    //S52.setMarinerParam(S52.MarinerParameter.FONT_SOUNDG,     0.0);

    S52.setMarinerParam(S52.MarinerParameter.DATUM_OFFSET,    0.0);
    //S52_setMarinerParam(S52_MAR_DATUM_OFFSET,    5.0);

    //S52.setMarinerParam(S52.MarinerParameter.SCAMIN,          1.0);
    S52.setMarinerParam(S52.MarinerParameter.SCAMIN,          0.0);
    // remove QUAPNT01 symbole (black diagonal and a '?')
    S52.setMarinerParam(S52.MarinerParameter.QUAPNT01,        0.0);


    //--------  SETTING FOR CHART NO 1 (PLib C1 3.1) --------
    // Soundings      ON
    // Text           ON
    // Depth Shades    4
    // Safety Contour 10 m
    // Safety Depth    7 m
    // Shallow         5 m
    // Deep           30 m
    /*
    S52_setMarinerParam("S52_MAR_SHOW_TEXT",        1.0);
    S52_setMarinerParam("S52_MAR_DISP_CATEGORY",    3.0); // OTHER_ALL - show all
    S52_setMarinerParam("S52_MAR_TWO_SHADES",       0.0); // Depth Shades
    S52_setMarinerParam("S52_MAR_SAFETY_CONTOUR",  10.0);
    S52_setMarinerParam("S52_MAR_SAFETY_DEPTH",     7.0);
    S52_setMarinerParam("S52_MAR_SHALLOW_CONTOUR",  5.0);
    S52_setMarinerParam("S52_MAR_DEEP_CONTOUR",    30.0);
    */
    //-------------------------------------------------------

    // showing off (OpenGL blending)
    S52.setMarinerParam(S52.MarinerParameter.ANTIALIAS,        1.0);



    ////////////////////////////////////////////////////////////
    //
    // setup mariner object (for debugging)
    //

    // load additional PLib (facultative)
    //S52_loadPLib(NULL);
    //S52_loadPLib("plib_pilote.rle");
    S52.loadPLib("plib-test2.rle");

    // load auxiliary PLib (fix waypnt/WAYPNT01, OWNSHP vector)
    S52.loadPLib("PLAUX_00.DAI");

    // load PLib from s52.cfg indication
    //S52.loadPLib(null);

    // debug - turn off rendering of last layer
    // very slow on some machine
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, 0.0);
    S52.setMarinerParam(S52.MarinerParameter.DISP_LAYER_LAST, 1.0);

    // cell's legend
    S52.setMarinerParam(S52.MarinerParameter.DISP_LEGEND, 1.0);   // show
    //S52.setMarinerParam(S52.MarinerParameter.DISP_LEGEND, 0.0);     // hide

    // init OWNSHP
    //_setOWNSHP();

    // init decoration (scale bar, North arrow, unit)
    S52.newCSYMB();

    //_setVRMEBL();

    _setVESSEL();

    //_setPASTRK();

    //_setRoute();

    //_setCLRLIN();

    //S52_setRADARCallBack(_radar_cb);

    _setMarFeature();

    return true;
}

function paint_cb(self) /*FOLD00*/
{
    //Cogl.push_matrix ();

    // debug: call to create_program to see
    // a CoglHandle .. this fail create_program introspectable="0" in .gir!
    //_marfea_area = Cogl.create_grogram();
    //print('_marfea_area:', _marfea_area);

    // FIXME: texte is gone!
    // FIXME: draw() or drawLast() work but not both!!

    Cogl.begin_gl();
    S52.draw();
    Cogl.end_gl();

    //Cogl.begin_gl();
    //S52.drawLast();
    //Cogl.end_gl();

  //Cogl.pop_matrix();
}


_setupS52();

let _stage = Clutter.Stage.get_default();

let ret,x,y;
//_stage.get_scale(x,y);

[x,y]= _stage.get_size ();
print("XY:",x,y);

// array of float not supported in gjs
//let a = [1,2,3,4,5,6];
//let a = new Float64Array(6);
//Cogl.path_polyline(a, 3);

let _vbox = new St.BoxLayout({ vertical: true,
                              width : _stage.width,
                              height: _stage.height,
                              style : 'padding: 10px;'
                                    + 'spacing: 10px;' });

_vbox.connect("paint",  Lang.bind(this, this.paint_cb));

_stage.add_actor(_vbox);
//_stage.connect("activate", _setupS52);

_stage.show();
Clutter.main();
S52.done();
