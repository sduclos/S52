// S52.h: top-level interface to libS52.so plug-in
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013 Sylvain Duclos sduclos@users.sourceforge.net

    OpENCview is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpENCview is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with OpENCview.  If not, see <http://www.gnu.org/licenses/>.
*/



#ifndef _S52_H_
#define _S52_H_

#ifdef _MINGW
#include <windows.h>
#define DLL __declspec (dllexport)
#define STD __stdcall
#else
#define DLL
#define STD
#endif

#ifdef __cplusplus
// Ruby FFI need to see C when libS52.so is compiled with g++
extern "C" {
#endif

typedef enum S52ObjectType {
    S52__META  = 0,         // meta geo stuff (ex: C_AGGR)
    S52_AREAS  = 1,         // 1
    S52_LINES  = 2,         // 2
    S52_POINT  = 3,         // 3
    S52_N_OBJ  = 4          // number of object type
} S52ObjectType;

// global parameter for mariners' selection
typedef enum S52MarinerParameter {
    S52_MAR_NONE                =  0,   // default
    S52_MAR_SHOW_TEXT           =  1,   // view group 23 (on/off)
    S52_MAR_TWO_SHADES          =  2,   // flag indicating selection of two depth shades (on/off) [default ON]
    S52_MAR_SAFETY_CONTOUR      =  3,   // S52_LINES: selected safety contour (meters) [IMO PS 3.6]
    S52_MAR_SAFETY_DEPTH        =  4,   // S52_POINT: selected safety depth (for sounding color) (meters) [IMO PS 3.7]
    S52_MAR_SHALLOW_CONTOUR     =  5,   // S52_AREAS: selected shallow water contour (meters) (optional) [OFF==S52_MAR_TWO_SHADES]
    S52_MAR_DEEP_CONTOUR        =  6,   // S52_AREAS: selected deepwater contour (meters) (optional)
    S52_MAR_SHALLOW_PATTERN     =  7,   // flag indicating selection of shallow water highlight (on/off)(optional) [default OFF]
    S52_MAR_SHIPS_OUTLINE       =  8,   // flag indicating selection of ship scale symbol (on/off) [IMO PS 8.4]
    S52_MAR_DISTANCE_TAGS       =  9,   // -not implemented- selected spacing of "distance to run" tags at a route (nm) [default 0.0 - OFF]
    S52_MAR_TIME_TAGS           = 10,   // -not implemented- selected spacing of time tags at the past track (min), [ref S52_addPASTRKPosition() bellow]
    S52_MAR_FULL_SECTORS        = 11,   // show full length light sector lines (on/off) [default ON]
    S52_MAR_SYMBOLIZED_BND      = 12,   // symbolized area boundaries (on/off) [default ON]
    S52_MAR_SYMPLIFIED_PNT      = 13,   // simplified point (on/off) [default ON]
    S52_MAR_DISP_CATEGORY       = 14,   // display category (see [1] bellow)
    S52_MAR_COLOR_PALETTE       = 15,   // color palette  (0 - DAY_BRIGHT, 1 - DAY_BLACKBACK, 2 - DAY_WHITEBACK, 3 - DUSK, 4 - NIGHT)

    S52_MAR_VECPER              = 16,   // vecper: Vector-length time-period (min) (normaly 6 or 12)
    S52_MAR_VECMRK              = 17,   // vecmrk: Vector time-mark interval (0 - none, 1 - 1&6 min, 2 - 6 min)
    S52_MAR_VECSTB              = 18,   // vecstb: Vector Stabilization (0 - none, 1 - ground, 2 - water)

    S52_MAR_HEADNG_LINE         = 19,   // all ship (ownshp and AIS) show heading line (on/off)
    S52_MAR_BEAM_BRG_NM         = 20,   // ownshp beam bearing length (nm)



    //---- experimental variables ----

    S52_MAR_FONT_SOUNDG         = 21,   // use font for sounding (on/off)

    S52_MAR_DATUM_OFFSET        = 22,   // value of chart datum offset (S52_MAR_FONT_SOUNDG must be ON)
                                        // PROBLEM: 2 datum: sounding / vertical (ex bridge clearance)
    S52_MAR_SCAMIN              = 23,   // flag for using SCAMIN filter (on/off) (default ON)

    S52_MAR_ANTIALIAS           = 24,   // flag for color blending (anti-aliasing) (on/off)

    S52_MAR_QUAPNT01            = 25,   // display QUAPNT01 (quality of position symbol) (on/off) (default on)

    S52_MAR_DISP_OVERLAP        = 26,   // display overlapping symbol (debug)

    S52_MAR_DISP_LAYER_LAST     = 27,   // enable S52_drawLast (see [1] bellow)

    S52_MAR_ROT_BUOY_LIGHT      = 28,   // rotate buoy light (deg from north)

    S52_MAR_DISP_CRSR_POS       = 29,   // display cursor position (on/off)

    S52_MAR_DISP_GRATICULE      = 30,   // display graticule (on/off)

    S52_MAR_DISP_WHOLIN         = 31,   // wholin auto placement: 0 - off, 1 - wholin, 2 - arc, 3 - wholin + arc  (default off)

    S52_MAR_DISP_LEGEND         = 32,   // display legend (on/off) (default off)

    S52_CMD_WRD_FILTER          = 33,   // toggle command word filter mask for profiling

    S52_MAR_DOTPITCH_MM_X       = 34,   // dotpitch X (mm) - pixel size in X
    S52_MAR_DOTPITCH_MM_Y       = 35,   // dotpitch Y (mm) - pixel size in Y

    S52_MAR_DISP_CALIB          = 36,   // display calibration symbol (on/off) (default off)

    S52_MAR_DISP_DRGARE_PATTERN = 37,   // display DRGARE pattern (on/off) (default on)

    S52_MAR_DISP_NODATA_LAYER   = 38,   // display NODATA layer 0 (on/off) (default on)

    S52_MAR_DEL_VESSEL_DELAY    = 39,   // time delay (sec) defore deleting old AIS (default 600 sec, 0 - OFF)

    S52_MAR_DISP_AFTERGLOW      = 40,   // display synthetic afterglow (in PLAUX_00.DAI) for OWNSHP & VESSEL (on/off)

    S52_MAR_DISP_CENTROIDS      = 41,   // display all centered symb of one area (on/off) (default off)

    S52_MAR_DISP_WORLD          = 42,   // display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)

    S52_MAR_DISP_RND_LN_END     = 43,   // display rounded line segment ending (on/off)

    S52_MAR_DISP_VRMEBL_LABEL   = 44,   // display bearing / range label on VRMEBL (on/off)

    S52_MAR_DISP_RASTER         = 45,   // display Raster:RADAR,Bathy,... (on/off) (default off)

    S52_MAR_NUM                 = 46    // number of parameters
} S52MarinerParameter;

// debug - command word filter for profiling
typedef enum S52_CMD_WRD_FILTER_t {
    S52_CMD_WRD_FILTER_SY = 1 << 0,   // 000001 - SY
    S52_CMD_WRD_FILTER_LS = 1 << 1,   // 000010 - LS
    S52_CMD_WRD_FILTER_LC = 1 << 2,   // 000100 - LC
    S52_CMD_WRD_FILTER_AC = 1 << 3,   // 001000 - AC
    S52_CMD_WRD_FILTER_AP = 1 << 4,   // 010000 - AP
    S52_CMD_WRD_FILTER_TX = 1 << 5    // 100000 - TE & TX
} S52_CMD_WRD_FILTER_t;

// [1] S52_MAR_DISP_CATEGORY / S52_MAR_DISP_LAYER_LAST
// 0x0000000 - DISPLAYBASE: only objects of the DISPLAYBASE category are shown (always ON)
// 0x0000001 - STANDARD:    only objects of the categorys DISPLAYBASE and STANDARD are shown (default)
// 0x0000010 - OTHER:       only objects of the categorys DISPLAYBASE and OTHER are shown
// 0x0000100 - SELECT:      initialy all objects are show (DISPLAYBASE + STANDARD + OHTER.) [2]
// 0x0001000 - MARINERS' NONE:     - when set, a call to S52_drawLast() output nothing
// 0x0010000 - MARINERS' STANDARD: - (default!)
// 0x0100000 - MARINERS' OTHER:    -
// 0x1000000 - MARINERS' SELECT:   - (see [2])
// [2] the display/supression of objects on STANDARD and/or OHTER is set via S52_toggleObjClass/ON/OFF()
typedef enum S52_MAR_DISP_CATEGORY_t {
    S52_MAR_DISP_CATEGORY_BASE     = 0,        // 0000000 - DISPLAY BASE
    S52_MAR_DISP_CATEGORY_STD      = 1 << 0,   // 0000001 - STANDARD
    S52_MAR_DISP_CATEGORY_OTHER    = 1 << 1,   // 0000010 - OTHER
    S52_MAR_DISP_CATEGORY_SELECT   = 1 << 2,   // 0000100 - SELECT

    //S52_MAR_DISP_LAYER_LAST  - MARINERS' CATEGORY (drawn on top - last)
    S52_MAR_DISP_LAYER_LAST_NONE   = 1 << 3,   // 0001000 - MARINERS' NONE
    S52_MAR_DISP_LAYER_LAST_STD    = 1 << 4,   // 0010000 - MARINERS' STANDARD
    S52_MAR_DISP_LAYER_LAST_OTHER  = 1 << 5,   // 0100000 - MARINERS' OTHER
    S52_MAR_DISP_LAYER_LAST_SELECT = 1 << 6    // 1000000 - MARINERS' SELECT
} S52_MAR_DISP_CATEGORY_t;


//-----need a GL context (main loop) ------------------------

#ifdef S52_USE_EGL
/**
 * S52_setEGLcb: register callback use by draw(), drawLast(), drawStr() and drawBlit()
 * @eglBeg: (in): callback to EGL begin (makecurrent)
 * @eglBeg: (in): callback to EGL end   (swap)
 * @EGLctx: (in): EGL context           (user_data)
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef DLL int STD (*EGL_cb)(void *EGLctx, const char *tag);
DLL int    STD S52_setEGLcb(EGL_cb eglBeg, EGL_cb eglEnd, void *EGLctx);
#endif

/**
 * S52_draw:
 *
 * Draw S57 object (cell) on layer 0-8
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_draw(void);

/**
 * S52_drawLast:
 *
 * Draw layer 9 (last) stuff that change all the time (ex AIS)
 * fast update
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawLast(void);


#ifdef S52_USE_GV
DLL int    STD S52_drawLayer(const char *name);
#endif


/**
 * S52_drawStr:
 * @pixels_x:  (in): origin LL corner
 * @pixels_y:  (in): origin LL corner
 * @colorName: (in): S52 UI color name
 * @bsize:     (in): body size (1..)
 * @str:       (in):
 *
 * For reference S52 UI color name:
 * "UINFD", "UINFF", "UIBCK", "UIAFD",
 * "UINFR", "UINFG", "UINFO", "UINFB",
 * "UINFM", "UIBDR", "UIAFF"
 *
 * Note: client must register EGL callback via S52_setEGLcb()
 * to handle the framebuffer (or handle FB by hand)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawStr(double pixels_x, double pixels_y,
                           const char *colorName, unsigned int bsize, const char *str);

/**
 * S52_drawBlit: Blitting
 * @scale_x: (in): -1.0 .. 0.0 .. +1.0
 * @scale_y: (in): -1.0 .. 0.0 .. +1.0
 * @scale_z: (in): -1.0 .. 0.0 .. +1.0
 * @north:   (in): [0.0 .. 360.0[      (<0 or >=360 unchage)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north);

/**
 * S52_setViewPort:
 * @pixels_x:      (in): origine LL corner
 * @pixels_y:      (in): origine LL corner
 * @pixels_width:  (in): viewport width in pixels
 * @pixels_height: (in): viewport height in pixels
 *
 *  Call this if viewPort change (ex: going full screen)
 *  From WebGL (OpenGL ES 2.0) spec:
 *  Rationale: automatically setting the viewport will interfere with applications
 *   that set it manually. Applications are expected to use onresize handlers to
 *   respond to changes in size of the canvas and set the OpenGL viewport in turn.
 * So this call is simply a wrapper on 'void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)'
 *
 * Use this call in conjuction with S52_setView() and S52_draw() to setup a magnifying glass
 * or an overview
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height);

/**
 * S52_pickAt: Cursor pick
 * @pixels_x: (in): origin LL corner
 * @pixels_y: (in): origin LL corner
 *
 *
 * NOTE:
 *  - BUG: Y is Down, but origin is LL !!
 *  - X is Right and Y is Down to match X11 origin
 *  - in the next frame, the object is drawn with the "DNGHL" color (experimental))
 *  - using 'double' instead of 'unsigned int' because X11 handle mouse in 'double'.
 *
 *
 * Return: (transfer none): string '<name>:<S57ID>' or if relationship existe
 * '<name>:<S57ID>:<relationS57IDa>,<S57IDa>,...:<relationS57IDb>,<S57IDb>,...'
 * of the S57 object, else NULL
 */
DLL const char * STD S52_pickAt(double pixels_x, double pixels_y);


// --- Helper ---

/**
 * S52_xy2LL:
 * @pixels_x: (inout):  origin LL corner (return longitude)
 * @pixels_y: (inout):  origin LL corner (return latitude)
 *
 * Convert pixel X/Y to longitude/latitude (deg)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_xy2LL (double *pixels_x,  double *pixels_y);

/**
 * S52_LL2xy:
 * @longitude: (inout): degree (return X)
 * @latitude:  (inout): degree (return Y)
 *
 * Convert longitude/latitude to X/Y (pixel - origin LL corner)
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_LL2xy (double *longitude, double *latitude);


//----- NO GL context (can work outside main loop) ----------

#ifdef S52_USE_DOTPITCH
/**
 * S52_init:
 * @screen_pixels_w: (in):
 * @screen_pixels_h: (in):
 * @screen_mm_w:     (in):
 * @screen_mm_h:     (in):
 * @err_cb:          (scope call) (allow-none): callback
 *
 * Initialize libS52, install SIGINT handler to abort drawing (Ctrl-C)
 * set physical dimension of screen (used in dotpitch)
 * xrandr can be used if framework doesn't do it (ie Clutter)
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef DLL int STD (*S52_error_cb)(const char *err);
DLL int   STD S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h, S52_error_cb err_cb);
#else  // when using GTK1
DLL int   STD S52_init(void);
#endif

/**
 * S52_version:
 *
 * Internal Version
 *
 *
 * Return: (transfer none): String with the version of libS52 and the '#define' used to build it
 */
DLL const char * STD S52_version(void);

/**
 * S52_done:
 *
 * Free up all ressources
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_done(void);


// ---- CHART LOADING (cell) -------------------------------------------
//
// callback for each layer to add app. specific stuff
// callback: S52_loadCell() --> S52_loadLayer() --> S52_loadObject()
// -OR-
// callback: S52_loadCell() --> S52_loadObject()
// Note: objects of a layer have the same name as the layer's name
//

/**
 * S52_loadObject:
 * @objname: (in): name of S57 object (same as layer name)
 * @feature: (in): S57 object feature passed from GDAL/OGR
 *
 * Can be called more than once (in theorie)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int   STD S52_loadObject      (const char *objname, /* OGRFeatureH */ void *feature);

/**
 * S52_loadObject_cb:
 * @objname: (in): name of S57 object (same as layer name)
 * @feature: (in): S57 object feature passed from GDAL/OGR (OGRFeatureH)
 *
 * This callback provide a way to manipulate each S57 object before
 * they are inserted into the scenegraph (via S52_loadObject())
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef DLL int STD (*S52_loadObject_cb)(const char *objname, /* OGRFeatureH */ void *feature);

/**
 * S52_loadCell:
 * @encPath:       (allow-none):
 * @loadObject_cb: (allow-none) (scope call):
 *
 * if @encPath is NULL look for label 'CHART' in s52.cfg
 * if @encPath is a path load all S57 base cell + update
 * if @loadObject_cb is NULL then S52_loadObject() is executed
 *
 * Note: the first ENCs to load will set the Mercator Projection Latitude
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_loadCell        (const char *encPath,  S52_loadObject_cb loadObject_cb);

/**
 * S52_doneCell:
 * @encPath: (in):
 *
 * Free up all ressources used by a cell
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_doneCell        (const char *encPath);

/**
 * S52_setView:
 * @cLat:  (in): latitude of the center of the view (deg)  [- 90 .. + 90]
 * @cLon:  (in): longitude of the center of the view (deg) [-180 .. +180]
 * @rNM:   (in): range (radius of view (NM), <0 unchange
 * @north: (in): angle from north (deg),     <0 unchange
 *
 * Set center of view / where to place the camera on model
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setView(double  cLat, double  cLon, double  rNM, double  north);
DLL int    STD S52_getView(double *cLat, double *cLon, double *rNM, double *north);

/**
 * S52_getCellExtent:
 * @filename: (in)  (allow-none)   :
 * @S:        (out) (transfer full): latitude  (deg)
 * @W:        (out) (transfer full): longitude (deg)
 * @N:        (out) (transfer full): latitude  (deg)
 * @E:        (out) (transfer full): longitude (deg)
 *
 * Cell extent; South, West, North, East
 * if @filename is NULL then return the extent of all cells loaded
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_getCellExtent(const char *filename, double *S, double *W, double *N, double *E);

/**
 * S52_getMarinerParam:
 * @paramID: (in): ID of Mariners' Object Parameter
 *
 * Get the value of the Mariners' Parameter @paramID (global variables/system wide)
 *
 * Invalid @paramID will return INFINITY, the value of S52_MAR_NONE
 *
 *
 * Return: value
 */
DLL double STD S52_getMarinerParam(S52MarinerParameter paramID);

/**
 * S52_setMarinerParam:
 * @paramID: (in): ID of Mariners' Object Parameter
 * @val:     (in): value
 *
 * Set the value of the global variables @paramID
 * used by Mariners' Object
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val);


/**
 * S52_setTextDisp:
 * @dispPrioIdx: (in): display priority index  (0..99)
 * @count:       (in): count
 * @state:       (in): display state (TRUE/FALSE)
 *
 * Set text @state display priority starting from @dispPrioIdx
 * to @count index.
 *
 * 75 - OWNSHP label
 * 76 - VESSEL label
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setTextDisp(int dispPrioIdx, int count, int state);

/**
 * S52_getTextDisp:
 * @dispPrioIdx: (in): display priority index  (0..99)
 *
 * Get the @state of text display priority at @dispPrioIdx
 *
 *
 * Return: state, TRUE / FALSE / -1 (fail)
 */
DLL int    STD S52_getTextDisp(int dispPrioIdx);

// DEPRECATED
/**
 * S52_toggleObjClass:
 * @className: (in): name of the classe of S57 object
 *
 * Toggle display all S57 objects of class @className.
 *
 * NOTE: S52_MAR_DISP_CATEGORY must be set to SELECT (3).
 *
 *
 *
 * Return: TRUE if transition ON to OFF or OFF to ON, else FALSE
 */
DLL int    STD S52_toggleObjClass   (const char *className);

/**
 * S52_toggleObjClassON:
 * @className: (in): name of the classe of S57 object
 *
 * supress display of Object class (suppression ON.)
 *
 * NOTE: S52_MAR_DISP_CATEGORY must be set to SELECT (3).
 *
 *
 * Return: TRUE if transition OFF to ON, else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_toggleObjClassON (const char *className);

/**
 * S52_toggleObjClassOFF:
 * @className: (in): name of the classe of S57 object
 *
 * display Object class (suppression OFF.)
 *
 * NOTE: S52_MAR_DISP_CATEGORY must be set to SELECT (3).
 *
 *
 * Return: TRUE if transition ON to OFF, else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_toggleObjClassOFF(const char *className);

/**
 * S52_getS57ObjClassSupp:
 * @className: (in): name of the classe of S57 object
 *
 * get Object class suppression
 *
 *
 * Return: TRUE if suppression is ON else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_getS57ObjClassSupp(const char *className);

/**
 * S52_setS57ObjClassSupp:
 * @className: (in): name of the S57 classe
 * @value: (in): TRUE / FALSE
 *
 * set suppression (TRUE/FALSE) from display of all Objects of the S57 class @className
 *
 *
 * Return: TRUE if call successfull else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_setS57ObjClassSupp(const char *className, int value);

/**
 * S52_loadPLib:
 * @plibName: (allow-none): name or path+name
 *
 * If @plibName is NULL look for label 'PLIB' in s52.cfg
 *
 * WARNING: after loadPLib() all S52ObjectHandle are invalid, so user must
 * reload them to get new S52ObjectHandle.
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_loadPLib(const char *plibName);

/**
 * S52_getPLibNameList:
 *
 * List of PLib name loaded delimited by ','
 * WARNING: the return str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): string
 */
DLL const char * STD S52_getPLibNameList(void);

/**
 * S52_getPalettesNameList:
 *
 * List of palettes name loaded separated by ','.
 * WARNING: *BUG*, str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): NULL if call fail
 */
DLL const char * STD S52_getPalettesNameList(void);

/**
 * S52_getCellNameList:
 *
 * List of cells name loaded
 * WARNING: *BUG*, str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): NULL if call fail
 */
DLL const char * STD S52_getCellNameList(void);

/**
 * S52_getS57ClassList: get list of all S57 class in a cell
 * @cellName: (in) (allow-none): cell name
 *
 * if @cellName is not NULL then return a list of all S57 class
 * in the cell @cellName. The first element of the list is the cell's name.
 * If @cellName is NULL then all S57 class is return.
 * WARNING: the return str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): List of all class name separeted by ',', NULL if call fail
 */
DLL const char * STD S52_getS57ClassList(const char *cellName);

/**
 * S52_getObjList: get list of S52 objets of @className in @cellName
 * @cellName:  (in): cell name   (not NULL)
 * @className: (in): class name  (not NULL)
 *
 * Return a string list of element separated by ','
 * Where the first elementy is the cell name, the second element is the class name
 * and the following elements are quadruplet, one for each S52 object
 * A quadruplet is made of ::= <S57ID>:<geoType>:<disp cat>:<disp prio>
 * <S57ID>     ::= number
 * <geoType>   ::= P|L|A                (see S57data.h:S52_Obj_t)
 * <disp cat>  ::= D|S|O|A|T|P|-        (see S52PL.h:S52_DisCat)
 * <disp prio> ::= 0|1|2|3|4|5|6|7|8|9
 *
 * WARNING: the return str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): string of all element separeted by ',', NULL if call fail
 */
DLL const char * STD S52_getObjList(const char *cellName, const char *className);

/**
 * S52_getAttList: get Attributes of a S52 object (S57ID)
`* @S57ID:  (in) : a S52 object has a unique S57ID
 *
 * Where the first elementy is the ID, folowed by <key>:<value> pair
 * WARNING: the return str can be dandling, so raw C call must save
 * the string before calling libS52 again
 *
 *
 * Return: (transfer none): string of all element separeted by ',', NULL if call fail
 */
DLL const char * STD S52_getAttList(unsigned int S57ID);

/**
 * S52_setRGB:
 * @colorName: (in): S52 color name
 * @R:         (in): red,   [0..255]
 * @G:         (in): green, [0..255]
 * @B:         (in): blue,  [0..255]
 *
 * Overright the current RGB for @colorName of current palette
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setRGB(const char *colorName, unsigned char  R, unsigned char  G, unsigned char  B);

/**
 * S52_getRGB:
 * @colorName: (in) : S52 color name
 * @R:         (out): red,   [0..255]
 * @G:         (out): green, [0..255]
 * @B:         (out): blue,  [0..255]
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);

/**
 * S52_setRADARCallBack:
 * @cb: (scope call) (allow-none):
 *
 * Signal that libS52 is at RADAR layer in the layer's sequence in S52_draw()
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef DLL int STD (*S52_RADAR_cb)(void);
DLL int    STD S52_setRADARCallBack(S52_RADAR_cb cb);

/**
 * S52_dumpS57IDPixels:
 * @toFilename: (in): PNG file (with full path) to dump pixels to
 * @S57ID:  (in): internal ID of object (return via S52_getObjList() or S52_pickAt())
 * @width:  (in): width of dump in pixels (max viewport width)
 * @height: (in): height of dump in pixels (max viewport height)
 *
 * Dump (@width x @height) pixels to PNG @toFilename centered on @S57ID of the current framebuffer
 * Note: changing the size of the viewport require a call to draw() before this call
 * (ie size of framebuffer must be in sync with the size of the viewport).
 *
 * If @S57ID is zero (0) then the whole framebuffer is dumped (ie @width and @height are ignore)
 *
 *
 * Return: TRUE on success, else FALSE
 */

DLL int    STD S52_dumpS57IDPixels(const char *toFilename, unsigned int S57ID, unsigned int width, unsigned int height);


///////////////////////////////////////////////////////////////
//
// Mariners' Object (ownshp, vessel, legline & stuff like that)
//
//
// --- all the Mariners' Objects that are 'known' in S52raz-3.2.rle ----
//
// Note: number in () indicate the S52 layer
//
// AREA : dnghlt (8), marfea (8), mnufea (5)
// LINE : clrlin (9), ebline (9), leglin (8), marfea (8), mnufea (5),
//        pastrk (3), poslin (3), rngrng (9), vrmark (9), wholin (8)
// POINT: cursor (8), dnghlt (8), events (8), marfea (8), marnot (8),
//        mnufea (5), ownshp (9), plnpos (5), positn (5), refpnt (7),
//        tidcur (7), vessel (9), waypnt (8)

// NOTE: Mariners' Object *not* on layer 9
// need a call to S52_draw() to be drawn


#ifdef S52_USE_GOBJECT
// Not really using GObect for now but this emphasize that the
// opaque pointer is typedef'ed to something that 'gjs' can understand

#include <glib.h>   // guint64

/**
 * S52ObjectHandle:
 *
 * Type used for storing references to S52 objects, the S52ObjectHandle
 * is a fully opaque type without any public data members.
 */
typedef guint64 S52ObjectHandle;
//typedef gdouble S52ObjectHandle;

// NOTE: gjs doesn't seem to understand 'gpointer', so send the
// handle as a 64 bits unsigned integer (guint64)
// (trying gdouble that is also 64bits on 32 and 64 bits machine!)

#else

// in real life S52ObjectHandle is juste a ordinary opaque pointer
typedef void*   S52ObjectHandle;

#endif /* S52_USE_GOBJECT */


// ---- Basic Call (all other call are a specialisation of these) ----

/**
 * S52_newMarObj:
 * @plibObjName: (in) (type gchar*):
 * @objType:     (in): S52ObjectType
 * @xyznbrmax:   (in): maximum number of xyz (point)(see S52_pushPosition())
 * @xyz:         (in) (type gpointer) (allow-none):
 * @listAttVal:  (in) (type gchar*): format:  "att1:val1,att2:val2,..."
 *                                        OR "[att1,val1,att2,al2,...]" (JSON array)
 *
 * Create new S52_obj - Basic Call.
 * All other call of the form S52_new*() are a specialisation of this one.
 *
 * NOTE: LIFO stack so 'cursor' should be created first to be drawn on top.
 *
 * In 'listAttVal', in S52 attribute name (ex: att1) of 6 lower case letters are reserve
 * for Mariners' Object. Lower case attribute name starting with an unserscore ('_')
 * are reserve for internal libS52 needs.
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
                                      unsigned int xyznbrmax, double *xyz, const char *listAttVal);

/**
 * S52_delMarObj:
 * @objH: (in) (transfer none): addressed S52ObjectHandle
 *
 * Delete ressources in libS52 for this S52_obj.
 *
 *
 * Return: (transfer none): NULL if S52_obj was deleted successfully, if call fail return the handle
 */
DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);

/**
 * S52_getMarObjH: get Mariners' Object handle
`* @S57ID: (in)  : a S52 object internal S57ID
 *
 * get the handle of a Mariners' Object from is internal S57ID
 * (return via S52_pickAt())
 *
 *
 * Return: (transfer none): the S52_obj handle or NULL if call fail
 */
DLL S52ObjectHandle STD S52_getMarObjH(unsigned int S57ID);

/**
 * S52_toggleDispMarObj:
 * @objH: (in) (transfer none): addressed S52ObjectHandle
 *
 * Initially Mariners' Object are ON (ie display of object NOT suppressed)
 * FIXME: maybe add toggleDispMarObj ON / OFF for clarity as toggleObjClass..
 *
 *
 * Return: (transfer none): the S52_obj handle or NULL if call fail
 */
DLL S52ObjectHandle STD S52_toggleDispMarObj(S52ObjectHandle objH);


// ---- Mariners' Objects that call the Conditional Symbology (CS) code ----
// CLRLIN:
// LEGLIN:
// OWNSHP:
// PASTRK:
// VESSEL:
// VRMEBL:

/**
 * S52_newCLRLIN:
 * @catclr:   (in): 0 - undefined, 1 - NMT (not more than), 2 - NLT (not less than)
 * @latBegin: (in):
 * @lonBegin: (in):
 * @latEnd:   (in):
 * @lonEnd:   (in):
 *
 * new S52_obj "Clearing Line"
 * 'clrlin': CS(CLRLIN--)
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newCLRLIN(int catclr, double latBegin, double lonBegin, double latEnd, double lonEnd);

/**
 * S52_newLEGLIN:
 * @select:     (in): Selection: 0 - undefined, 1 - planned, 2 - alternate
 * @plnspd:     (in): planned speed (0.0 for no speed label)
 * @wholinDist: (in): distance of the 'wholin' (wheel-over-line) from End in NM
 * @latBegin:   (in): lat of LEGLIN beginning (degdecimal)
 * @lonBegin:   (in): lon of LEGLIN beginning (degdecimal)
 * @latEnd:     (in): lat of LEGLIN ending    (degdecimal)
 * @lonEnd:     (in): lon of LEGLIN ending    (degdecimal)
 * @previousLEGLIN: (allow-none): handle to the previous LEGLIN, used to draw 'wholin' and/or curve
 *
 * new S52_obj "Leg Line" segment
 * 'leglin': CS(LEGLIN--)
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newLEGLIN(int select, double plnspd, double wholinDist,
                                      double latBegin, double lonBegin, double latEnd, double lonEnd,
                                      S52ObjectHandle previousLEGLIN);

/**
 * S52_newOWNSHP:
 * @label: (in) (allow-none): for example Ship's name or MMSI or NULL
 *
 * new S52_obj "Own Ship"
 * 'ownshp': CS(OWNSHP--)
 * Note: if OWNSHP has allready been created then an other call will
 * return the handle of the first OWNSHIP call.
 * Note: text priority of @label is 75
 *
 *
 * Return: (transfer none): an handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newOWNSHP(const char *label);


// --- Vector, Dimension, ... of OWNSHP and VESSEL object -------------------

/**
 * S52_setDimension:
 * @objH: (in) (transfer none): addressed S52ObjectHandle
 * @a:    (in): dist form foward
 * @b:    (in): dist from aft       (a + b = lenght)
 * @c:    (in): dist from port
 * @d:    (in): dist from starboard (c + d = beam)
 *
 * conning position - for AIS this is the antenna position
 *
 *
 * Return: (transfer none): the handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);

/**
 * S52_setVector:
 * @objH:   (in) (transfer none): addressed S52ObjectHandle
 * @vecstb: (in): 0 - none, 1 - ground, 2 - water
 * @course: (in):
 * @speed:  (in):
 *
 *
 * Return: (transfer none): the handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);

/**
 * S52_newPASTRK:
 * @catpst:    (in): Category of past track: 0 - undefined, 1 - primary, 2 - secondary
 * @xyznbrmax: (in): maximum number of PASTRK positon (point)
 *
 * 'pastrk': CS(PASTRK--)
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newPASTRK(int catpst, unsigned int xyznbrmax);

/**
 * S52_pushPosition:
 * @objH:      (in) (transfer none): addressed S52ObjectHandle
 * @latitude:  (in):
 * @longitude: (in):
 * @data:      (in):
 *
 * Push a position on a FIFO stack. The size of the stack is one for object of type S52_POINT.
 * For object of type S52_LINES and S52_AREAS the size of the stack is set via 'xyznbrmax'.
 * S52_AREAS are expected to have the same first and last point (as any S57 area).
 *
 * 'data' is used to display time (hh.mm) if the object is PASTRK.
 * If the object is VESSEL or OWNSHP then 'data' is the heading.
 *
 *
 * Return: (transfer none): the handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data);

/**
 * S52_newVESSEL:
 * @vesrce: (in): vessel report source: 1 - ARPA target, 2 - AIS vessel report, 3 - VTS report
 * @label:  (in) (allow-none): NULL or a string
 *
 *  'vessel': CS(VESSEL--) ARPA & AIS
 * Note: text priority of @label is 76
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);

/**
 * S52_setVESSELlabel:
 * @objH:     (in) (transfer none): addressed S52ObjectHandle
 * @newLabel: (in) (allow-none): NULL or a string
 *
 * (re) set label
 * Note: text priority of @newLabel is 76
 *
 *
 * Return: (transfer none): the handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel);

/**
 * S52_setVESSELstate:
 * @objH:         (in) (transfer none): addressed S52ObjectHandle
 * @vesselSelect: (in): 0 - undefined, 1 - selected (ON) and follow, 2 - de-seltected (OFF), (ie bracket symbol on vessel),
 * @vestat:       (in): 0 - undefined, 1 - AIS active, 2 - AIS sleeping, 3 - AIS active, close quarter (red)
 * @vesselTurn:   (in): Turn rate is encoded as follows: [from gpsd doc]
 *         0       - not turning
 *         1..126  - turning right at up to 708 degrees per minute or higher
 *        -1..-126 - turning left  at up to 708 degrees per minute or higher
 *         127     - turning right at more than 5deg/30s (No TI available)
 *        -127     - turning left  at more than 5deg/30s (No TI available)
 *         128     - (80 hex) indicates no turn information available (default)
 *         129     - undefined
 *
 * "undefined" mean that the current value of the variable of this objH is unafected
 *
 * Note: experimental @vestat = 3, compile with S52_USE_SYM_VESSEL_DNGHL, symb in PLAUX_00
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn);


// --- VRM & EBL -------------------

// FIXME: use an alternate S52_newVRMEBL() that accept flags instead
//typedef enum S52_VRMEBL_t {
//    S52_VRMEBL_vrm = 1 << 0, //0x000001 - vrm
//    S52_VRMEBL_ebl = 1 << 1, //0x000010 - ebl
//    S52_VRMEBL_sty = 1 << 2, //0x000100 - normalLineStyle
//    S52_VRMEBL_ori = 1 << 3, //0x001000 - setOrigin
//} S52_VRMEBL_t;

/**
 * S52_newVRMEBL:
 * @vrm:             (in): Variable Range Marker TRUE/FALSE
 * @ebl:             (in): Electronic Bearing Line TRUE/FALSE
 * @normalLineStyle: (in): TRUE  - normal line style, FALSE - alternate line style
 * @setOrigin:       (in): TRUE  - will setup a freely movable VRMEBL origin,
 *                         FALSE - centered on ownshp or screen center if no ownshp
 *
 * 'vrmebl' CS(VRMEBL--)
 * Note: if @ebl is TRUE then an "ebline" is created else "vrmark"
 *
 *
 * Return: (transfer none): an handle to a new S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_newVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin);

/**
 * S52_setVRMEBL:
 * @objH:     (in) (transfer none): addressed S52ObjectHandle
 * @pixels_x: (in): origin LL corner
 * @pixels_y: (in): origin LL corner
 * @brg:      (in): bearing from origine (FIXME: no offset from S52_setDimension())
 * @rge:      (in): range   from origine (FIXME: no offset from S52_setDimension())
 *
 * The first (x,y) will set the origine in the case that this object was
 * created (new) with the parameter @setOrigin set to TRUE
 *
 *
 * Return: (transfer none): the handle to S52_obj or NULL if call fail
 */
DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y, double *brg, double *rge);


// --- CS that need to be called by the system --------------

// CS(DATCVR--)


// --- other symbol that need to be called by the system --

/**
 * S52_newCSYMB:
 *
 * Create SCALEB10, SCALEB11, NORTHAR1, UNITMTR1, CHKSYM01, Calibration Symb.
 * note that the S52ObjectHandle of these S52 object are kept inside libS52.
 * Calibration Symb. are turn ON / OFF via S52_MAR_DISP_CALIB
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_newCSYMB(void);


// FIXME: use GDBus instead
// or socket as DBus as to handle system-wide msg that slow bus
#ifdef  S52_USE_DBUS
#define S52_DBUS_OBJ_NAME  "nav.ecs.dbus"
#define S52_DBUS_OBJ_PATH  "/nav/ecs/dbus"
#define S52_DBUS_OBJ_IFACE "nav.ecs.dbus"
#endif


#ifdef __cplusplus
}
#endif


#endif //_S52_H_
