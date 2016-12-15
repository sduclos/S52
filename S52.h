// S52.h: top-level interface to libS52.so
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2016 Sylvain Duclos sduclos@users.sourceforge.net

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


// Summary:
// - def / type / enum
// - call at any time
// - call available fater S52_init()
// - call from main loop / GL context
// -- call for cell un/loaging
// -- call to camera movement
// -- call to PLib state
// -- call for MIO's
// -- call made over DBUS, Socket, WebSocket (_S52.i)
// --- JSON Grammar (see ./test/s52ui)

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
    S52_AREAS  = 1,
    S52_LINES  = 2,
    S52_POINT  = 3,
    S52_N_OBJ  = 4          // number of object type
} S52ObjectType;

// global parameter for mariners' selection
typedef enum S52MarinerParameter {
    S52_MAR_ERROR               =  0,   //

    S52_MAR_SHOW_TEXT           =  1,   // flags to show text (see S52_setTextDisp() for details) (on/off) [default ON]
    S52_MAR_TWO_SHADES          =  2,   // flag indicating selection of two depth shades (on/off) [default ON]
    S52_MAR_SAFETY_CONTOUR      =  3,   // S52_LINES: selected safety contour (meters) [IMO PS 3.6]
    S52_MAR_SAFETY_DEPTH        =  4,   // S52_POINT: selected safety depth (for sounding color) (meters) [IMO PS 3.7]
    S52_MAR_SHALLOW_CONTOUR     =  5,   // S52_AREAS: selected shallow water contour (meters) (optional) [OFF==S52_MAR_TWO_SHADES]
    S52_MAR_DEEP_CONTOUR        =  6,   // S52_AREAS: selected deepwater contour (meters) (optional)
    S52_MAR_SHALLOW_PATTERN     =  7,   // flag indicating selection of shallow water highlight (on/off)(optional) [default OFF]
    S52_MAR_SHIPS_OUTLINE       =  8,   // flag indicating selection of ship scale symbol (on/off) [IMO PS 8.4]
    S52_MAR_DISTANCE_TAGS       =  9,   // NOT IMPLEMENTED: selected spacing of "distance to run" tags at a route (nm) [default 0.0 - OFF]
    S52_MAR_TIME_TAGS           = 10,   // NOT IMPLEMENTED: selected spacing of time tags at the past track (min)
    S52_MAR_FULL_SECTORS        = 11,   // show full length light sector lines (on/off) [default ON]
    S52_MAR_SYMBOLIZED_BND      = 12,   // symbolized area boundaries (on/off) [default ON]
    S52_MAR_SYMPLIFIED_PNT      = 13,   // simplified point (on/off) [default ON]
    S52_MAR_DISP_CATEGORY       = 14,   // display category (see [1] bellow)
    S52_MAR_COLOR_PALETTE       = 15,   // color palette  (0 - DAY_BRIGHT, 1 - DAY_BLACKBACK, 2 - DAY_WHITEBACK, 3 - DUSK, 4 - NIGHT)
                                        // (call S52_getPalettesNameList() to get the current palette list

    S52_MAR_VECPER              = 16,   // vecper: OWNSHP & VESSEL: Vector-length time-period (min) (normaly 6 or 12)
    S52_MAR_VECMRK              = 17,   // vecmrk: OWNSHP & VESSEL: Vector time-mark interval (0 - none, 1 - 1&6 min, 2 - 6 min)
    S52_MAR_VECSTB              = 18,   // vecstb: OWNSHP         : Vector Stabilization (0 - none, 1 - ground, 2 - water)

    S52_MAR_HEADNG_LINE         = 19,   // OWNSHP & VESSEL: show heading line (on/off)
    S52_MAR_BEAM_BRG_NM         = 20,   // OWNSHP         : beam bearing length (nm) (0 - off)



    //---- experimental variables ----

    S52_MAR_FONT_SOUNDG         = 21,   // NOT IMPLEMENTED: use font for sounding (on/off)

    S52_MAR_DATUM_OFFSET        = 22,   // value of chart datum offset. FIXME: 2 datum: sounding / vertical (ex bridge clearance)

    S52_MAR_SCAMIN              = 23,   // flag for using SCAMIN filter (on/off) (default ON)

    S52_MAR_ANTIALIAS           = 24,   // flag for color blending (anti-aliasing) (on/off)

    S52_MAR_QUAPNT01            = 25,   // display QUAPNT01 (quality of position symbol) (on/off) (default on)

    S52_MAR_DISP_OVERLAP        = 26,   // display cells, overlapping layer (debug)

    S52_MAR_DISP_LAYER_LAST     = 27,   // enable S52_drawLast (see [1] bellow)

    S52_MAR_ROT_BUOY_LIGHT      = 28,   // rotate buoy light (deg from north)

    S52_MAR_DISP_CRSR_PICK      = 29,   // 0 - off, 1 - pick/highlight top object, 2 - pick stack/highlight top,
                                        // 3 - pick stack+ASSOC/highlight ASSOC (compiled with -DS52_USE_C_AGGR_C_ASSO)
    // those 3 are in S52 specs
    S52_MAR_DISP_GRATICULE      = 30,   // display graticule (on/off)
    S52_MAR_DISP_WHOLIN         = 31,   // wholin auto placement: 0 - off, 1 - wholin, 2 - arc, 3 - wholin + arc  (default off)
    S52_MAR_DISP_LEGEND         = 32,   // display legend (on/off) (default off)

    S52_CMD_WRD_FILTER          = 33,   // toggle command word filter mask for profiling (see [3] bellow)

    S52_MAR_DOTPITCH_MM_X       = 34,   // dotpitch X (mm) - pixel size in X
    S52_MAR_DOTPITCH_MM_Y       = 35,   // dotpitch Y (mm) - pixel size in Y

    // in S52 specs
    S52_MAR_DISP_CALIB          = 36,   // display calibration symbol (on/off) (default off)

    S52_MAR_DISP_DRGARE_PATTERN = 37,   // display DRGARE pattern (on/off) (default on)

    S52_MAR_DISP_NODATA_LAYER   = 38,   // display NODATA layer 0 (on/off) (default on)

    S52_MAR_DISP_VESSEL_DELAY   = 39,   // time delay (sec) defore deleting old AIS (default 0 - OFF)

    S52_MAR_DISP_AFTERGLOW      = 40,   // display synthetic afterglow (in PLAUX_00.DAI) for OWNSHP & VESSEL (on/off)

    S52_MAR_DISP_CENTROIDS      = 41,   // display all centered symb of one area (on/off) (default off)

    S52_MAR_DISP_WORLD          = 42,   // display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)

    S52_MAR_DISP_RND_LN_END     = 43,   // display rounded line segment ending (on/off)

    S52_MAR_DISP_VRMEBL_LABEL   = 44,   // display bearing / range label on VRMEBL (on/off)

    S52_MAR_DISP_RADAR_LAYER    = 45,   // display Raster: RADAR, Bathy, ... (on/off) (default off)

    // FIXME: DISP TEXT SHADOW - 0-7 bit: N NE E SE S SW W NW, 0 - off, [default to SE for now]

    S52_MAR_GUARDZONE_BEAM      = 46,   // Danger/Indication Highlight used by LEGLIN&Position  (meters) [0.0 - off]
    S52_MAR_GUARDZONE_LENGTH    = 47,   // Danger/Indication Highlight used by Position (meters, user computed from speed/time or distance)
    S52_MAR_GUARDZONE_ALARM     = 48,   // FIXME: 1&2 ON at the same time. 0 - no error, 1 - alarm, 2 - indication
                                        // -1 - display highlight

    S52_MAR_DISP_HODATA         = 49,   // 0 - union HO data limit "m_covr" (default), 1 - all HO data limit "M_COVR+m_covr" (debug)

    S52_MAR_NUM                 = 50    // number of parameters
} S52MarinerParameter;

// [3] debug - command word filter for profiling
typedef enum S52_CMD_WRD_FILTER_t {
    S52_CMD_WRD_FILTER_OFF = 0,        // not used
    S52_CMD_WRD_FILTER_SY  = 1 << 0,   // 0x000001 - SY
    S52_CMD_WRD_FILTER_LS  = 1 << 1,   // 0x000010 - LS
    S52_CMD_WRD_FILTER_LC  = 1 << 2,   // 0x000100 - LC
    S52_CMD_WRD_FILTER_AC  = 1 << 3,   // 0x001000 - AC
    S52_CMD_WRD_FILTER_AP  = 1 << 4,   // 0x010000 - AP
    S52_CMD_WRD_FILTER_TX  = 1 << 5    // 0x100000 - TE & TX
} S52_CMD_WRD_FILTER_t;

// [1] S52_MAR_DISP_CATEGORY / S52_MAR_DISP_LAYER_LAST
// 0x0000000 - DISPLAYBASE: only objects of the DISPLAYBASE category are shown (always ON)
// 0x0000001 - STANDARD:    only objects of the categorys DISPLAYBASE and STANDARD are shown (default)
// 0x0000010 - OTHER:       only objects of the categorys DISPLAYBASE and OTHER are shown
// 0x0000100 - SELECT:      initialy all objects are show (DISPLAYBASE + STANDARD + OHTER.) (see [2])
// 0x0001000 - MARINERS' NONE:     - when set, a call to S52_drawLast() output nothing
// 0x0010000 - MARINERS' STANDARD: - (default!)
// 0x0100000 - MARINERS' OTHER:    -
// 0x1000000 - MARINERS' SELECT:   - (see [2])
// [2] the display/supression of objects on STANDARD and/or OHTER is set via S52_setS57ObjClassSupp()

typedef enum S52_MAR_DISP_CATEGORY_t {
    S52_MAR_DISP_CATEGORY_BASE     = 0,        // 0x0000000 - DISPLAY BASE
    S52_MAR_DISP_CATEGORY_STD      = 1 << 0,   // 0x0000001 - STANDARD
    S52_MAR_DISP_CATEGORY_OTHER    = 1 << 1,   // 0x0000010 - OTHER
    S52_MAR_DISP_CATEGORY_SELECT   = 1 << 2,   // 0x0000100 - SELECT

    //S52_MAR_DISP_LAYER_LAST  - MARINERS' CATEGORY (drawn on top - last)
    S52_MAR_DISP_LAYER_LAST_NONE   = 1 << 3,   // 0x0001000 - MARINERS' NONE
    S52_MAR_DISP_LAYER_LAST_STD    = 1 << 4,   // 0x0010000 - MARINERS' STANDARD
    S52_MAR_DISP_LAYER_LAST_OTHER  = 1 << 5,   // 0x0100000 - MARINERS' OTHER
    S52_MAR_DISP_LAYER_LAST_SELECT = 1 << 6    // 0x1000000 - MARINERS' SELECT
} S52_MAR_DISP_CATEGORY_t;


/**
 * S52_version:
 *
 * Internal Version.
 *
 *
 * Return: (transfer none): String with the version of libS52 and the '#define' used to build it
 */
#define S52_VERSION "libS52-2016DEC15-1.189"
DLL const char * STD S52_version(void);

/**
 * S52_getMarinerParam:
 * @paramID: (in): ID of Mariners' Parameter
 *
 * Get the value of the Mariners' Parameter @paramID (global variables/system wide)
 *
 * Invalid @paramID will return the value of S52_MAR_ERROR
 *
 *
 * Return: value
 */
DLL double STD S52_getMarinerParam(S52MarinerParameter paramID);

/**
 * S52_setMarinerParam:
 * @paramID: (in): ID of Mariners' Parameter
 * @val:     (in): value
 *
 * Note: S52_MAR_DISP_CATEGORY, S52_MAR_DISP_LAYER_LAST, S52_CMD_WRD_FILTER,
 * XOR the value of the global variables @paramID
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
DLL int    STD S52_setTextDisp(unsigned int dispPrioIdx, unsigned int count, unsigned int state);

/**
 * S52_getTextDisp:
 * @dispPrioIdx: (in): display priority index  (0..99)
 *
 * Get the @state of text display priority at @dispPrioIdx
 *
 *
 * Return: state, TRUE / FALSE / -1 (fail)
 */
DLL int    STD S52_getTextDisp(unsigned int dispPrioIdx);

/**
 * S52_setEGLcb: register callback use by draw(), drawLast(), drawStr() and drawBlit()
 * @eglBeg: (in): callback to EGL begin (makecurrent)
 * @eglBeg: (in): callback to EGL end   (swap)
 * @EGLctx: (in): EGL context           (user_data)
 *
 * usefull for debuging - send tag string to EGL/glInsertEventMarkerEXT to tell witch draw is starting
 *
 * Return: TRUE on success, else FALSE
 */
typedef int (*S52_EGL_cb)(void *EGLctx, const char *tag);
DLL int    STD S52_setEGLCallBack(S52_EGL_cb eglBeg, S52_EGL_cb eglEnd, void *EGLctx);

/**
 * S52_setRADARCallBack:
 * @cb: (scope call) (allow-none):
 * @texRadius: (in): texture radius (pixels), if 0 free cb
 *
 * S52_RADAR_cb: return alpha texture data
 * @cLat: (in): center latitude  (deg)
 * @cLng: (in): center longitude (deg)
 * @rNM : (in): radar range      (NM)
 *
 * Signal that libS52 is at RADAR layer in the layer's sequence in S52_draw()
 * (compile with S52_USE_RADAR)
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef unsigned char * (*S52_RADAR_cb)(double *cLat, double *cLng, double *rNM);
DLL int    STD S52_setRADARCallBack(S52_RADAR_cb cb, unsigned int textureRadiusPX);


//
//----- All call bellow need S52_init() first ----------------
//

//----- need a GL context / config event (main loop) --------

/**
 * S52_draw:
 *
 * Draw S57 object (cell) on layer 0-8
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 * Note: Interrupt 2 (ANSI) - user press Ctrl-C to stop long running process
 *
 * WARNING: At startup, this call must be the very first draw call to set projection
 *
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
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 * Note: Interrupt 2 (ANSI) - user press Ctrl-C to stop long running process
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawLast(void);


// deprecated
DLL int    STD S52_drawLayer(const char *name);


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
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawStr(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str);

/**
 * S52_drawBlit: Blitting (for touch screen)
 * @scale_x: (in): -1.0 .. 0.0 .. +1.0
 * @scale_y: (in): -1.0 .. 0.0 .. +1.0
 * @scale_z: (in): -1.0 .. 0.0 .. +1.0
 * @north:   (in):  [0.0 .. 360.0[  (<0 or >=360 unchage) [not impl. yet]
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north);

/**
 * S52_pickAt: Cursor pick
 * @pixels_x: (in): origin LL corner
 * @pixels_y: (in): origin LL corner
 *
 *
 * NOTE:
 *  - in the next frame, the object is drawn with the "DNGHL" color (experimental))
 *  - using 'double' instead of 'unsigned int' because X11 handle mouse in 'double'.
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: (transfer none): string '<name>:<S57ID>' or if relationship existe
 * '<name>:<S57ID>:<relationS57IDa>,<S57IDa>,...:<relationS57IDb>,<S57IDb>,...'
 * of the S57 object, else NULL
 */
DLL const char * STD S52_pickAt(double pixels_x, double pixels_y);


//----- NO GL context (can work outside main loop) ----------

// --- Helper ---

/**
 * S52_xy2LL:
 * @pixels_x: (inout):  origin LL corner (return longitude)
 * @pixels_y: (inout):  origin LL corner (return latitude)
 *
 * Convert pixel X/Y to longitude/latitude (deg)
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_xy2LL(double *pixels_x,  double *pixels_y);

/**
 * S52_LL2xy:
 * @longitude: (inout): degree (return X)
 * @latitude:  (inout): degree (return Y)
 *
 * Convert longitude/latitude to X/Y (pixel - origin LL corner)
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_LL2xy(double *longitude, double *latitude);
// --------------


/**
 * S52_init:
 * @screen_pixels_w: (in): use to compute DOTPITCH_X (px)
 * @screen_pixels_h: (in): use to compute DOTPITCH_Y (px)
 * @screen_mm_w:     (in): use to compute DOTPITCH_X (mm)
 * @screen_mm_h:     (in): use to compute DOTPITCH_Y (mm)
 * @log_cb:          (scope call) (allow-none): log callback
 *
 * Initialize libS52, install SIGINT handler to abort drawing (Ctrl-C)
 * xrandr can be used if framework doesn't do it (ie Clutter)
 *
 * Note: the ratio screen mmw/w and screen mm_h/h is used to compute initial DOTPITCH,
 *       overide with S52_MAR_DOTPITCH_MM_X/Y after init().
 *
 * Note: screen_pixels_w, int screen_pixels_h are used to setViewPort to full-screen
 *
 *
 * Return: TRUE on success, else FALSE
 */
typedef int (*S52_log_cb)(const char *str);
DLL int    STD S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h, S52_log_cb log_cb);

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
int            S52_loadObject(const char *objname, /* OGRFeatureH */ void *feature);

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
typedef int (*S52_loadObject_cb)(const char *objname, /* OGRFeatureH */ void *feature);

/**
 * S52_loadCell:
 * @encPath:       (allow-none):
 * @loadObject_cb: (allow-none) (scope call):
 *
 * if @encPath is NULL look for label 'CHART' in s52.cfg
 * if @encPath is a path load all S57 base cell + update
 * if @loadObject_cb is NULL then S52_loadObject() is executed
 *
 * Note: the first call to S52_loadCell() will set the Mercator Projection Latitude
 *       and Longitude of any futher S52_loadCell() call(s)
 * Note: Interrupt 2 (ANSI) - user press Ctrl-C to stop long running process
 *       (if compiled with S52_USE_SUPP_LINE_OVERLAP and/or S52_USE_C_AGGR_C_ASSO,
 *        analysis can be expensive in large file)
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_loadCell(const char *encPath,  S52_loadObject_cb loadObject_cb);

/**
 * S52_doneCell:
 * @encPath: (in):
 *
 * Free up all ressources used by @encPath
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_doneCell        (const char *encPath);
// ---- CHART LOADING (cell) -------------------------------------------


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
 *
 * Use this call in conjuction with S52_setView() and S52_draw() to setup a magnifying glass
 * or an overview
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height);

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

/**
 * S52_getView:
 * @cLat:  (out) (transfer full): latitude of the center of the view (deg)  [- 90 .. + 90]
 * @cLon:  (out) (transfer full): longitude of the center of the view (deg) [-180 .. +180]
 * @rNM:   (out) (transfer full): range (radius of view (NM), <0 unchange
 * @north: (out) (transfer full): angle from north (deg),     <0 unchange
 *
 * Get center of view / where is the camera on model
 *
 *
 * Return: TRUE on success, else FALSE
 */
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
 * S52_getS57ObjClassSupp:
 * @className: (in): name of the classe of S57 object
 *
 * get Object class suppression state
 *
 *
 * Return: TRUE if suppression is ON else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_getS57ObjClassSupp(const char *className);

/**
 * S52_setS57ObjClassSupp:
 * @className: (in): name of the S57 classe
 * @value:     (in): TRUE / FALSE
 *
 * set suppression (TRUE/FALSE) from display of all Objects of the S57 class @className
 *
 * NOTE: S52_MAR_DISP_CATEGORY must be set to SELECT.
 *
 *
 * Return: TRUE if call successfull to set new value else FALSE, error -1 (DISPLAYBASE or invalid className)
 */
DLL int    STD S52_setS57ObjClassSupp(const char *className, int value);

/**
 * S52_loadPLib:
 * @plibName: (allow-none): name or path+name
 *
 * If @plibName is NULL look for label 'PLIB' in s52.cfg
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_loadPLib(const char *plibName);

/**
 * S52_getPLibNameList:
 *
 * List of PLib name loaded delimited by ','
 *
 *
 * Return: (transfer none): string
 */
DLL const char * STD S52_getPLibNameList(void);

/**
 * S52_getPalettesNameList:
 *
 * List of palettes name loaded separated by ','.
 * Note: use S52_MAR_COLOR_PALETTE, as an index, to select one of them.
 *
 *
 * Return: (transfer none): NULL if call fail
 */
DLL const char * STD S52_getPalettesNameList(void);

/**
 * S52_getCellNameList:
 *
 * List of cells name loaded
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
 *
 * Return: (transfer none): string of all element separeted by ',', NULL if call fail
 */
DLL const char * STD S52_getObjList(const char *cellName, const char *className);

/**
 * S52_getAttList: get Attributes of a S52 object (S57ID)
 * @S57ID:  (in) : a S52 object has a unique S57ID
 *
 * Where the first element is the '<ObjName>:<S57ID>' folowed by list of '<key>:<value>' pair.
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
 * @R:         (out) (transfer full): red,   [0..255]
 * @G:         (out) (transfer full): green, [0..255]
 * @B:         (out) (transfer full): blue,  [0..255]
 *
 *
 * Return: TRUE on success, else FALSE
 */
DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);

/**
 * S52_dumpS57IDPixels: DEBUG dump region of framebuffer to .PNG
 * @toFilename: (in): PNG file (with full path) to dump pixels to
 * @S57ID:      (in): internal ID of object (return via S52_getObjList() or S52_pickAt())
 * @width:      (in): width of dump in pixels (max viewport width)
 * @height:     (in): height of dump in pixels (max viewport height)
 *
 * Dump (@width x @height) pixels to PNG @toFilename centered on @S57ID of the current framebuffer
 * Note: changing the size of the viewport require a call to draw() before this call
 * (ie size of framebuffer must be in sync with the size of the viewport).
 *
 * If @S57ID is zero (0) then the whole framebuffer is dumped (ie @width and @height are ignore).
 *
 * Note: use glReadPixels() instead to get raw pixles.
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
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

/**
 * S52ObjectHandle:
 *
 * Type used for storing references to S52 objects, the S52ObjectHandle
 * is a fully opaque type without any public data members.
 */
typedef unsigned int S52ObjectHandle;  // guint S75ID

// ---- Basic Call (all other S52_new*() call are a specialisation of this one) ----

/**
 * S52_newMarObj:
 * @plibObjName: (in) (type gchar*):
 * @objType:     (in): S52ObjectType
 * @xyznbrmax:   (in): maximum number of xyz (point)(see S52_pushPosition()) (one or more)
 * @xyz:         (in) (type gpointer):
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
 * are reserve for internal libS52 use.
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
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
 * Return: NULL if S52_obj was deleted successfully, if call fail return @S52ObjectHandle
 */
DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);

/**
 * S52_getMarObj : get Mariners' Object handle
 * @S57ID: (in)  : a S52 object internal S57ID
 *
 * get the handle of a Mariners' Object from is internal S57ID
 * (return via S52_pickAt())
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_getMarObj(unsigned int S57ID);

/**
 * S52_toggleDispMarObj:
 * @objH: (in) (transfer none): addressed S52ObjectHandle
 *
 * Initially Mariners' Object are ON (ie display of object NOT suppressed)
 * FIXME: maybe add toggleDispMarObj ON / OFF for clarity as toggleObjClass..
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
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
 * @latBegin: (in): lat of LEGLIN beginning (degdecimal)
 * @lonBegin: (in): lon of LEGLIN beginning (degdecimal)
 * @latEnd:   (in): lat of LEGLIN ending    (degdecimal)
 * @lonEnd:   (in): lon of LEGLIN ending    (degdecimal)
 *
 * new S52_obj "Clearing Line"
 * 'clrlin': CS(CLRLIN--)
 *
 *
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_newCLRLIN(int catclr, double latBegin, double lonBegin, double latEnd, double lonEnd);

/**
 * S52_newLEGLIN:
 * @select:     (in): Selection: 1 - planned, 2 - alternate
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
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
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
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_newOWNSHP(const char *label);


// --- Vector, Dimension, ... of OWNSHP and VESSEL object -------------------

/**
 * S52_setDimension:
 * @objH: (in) (transfer none): addressed S52ObjectHandle
 * @a:    (in): dist form foward
 * @b:    (in): dist from aft       (a + b = length)
 * @c:    (in): dist from port
 * @d:    (in): dist from starboard (c + d = beam)
 *
 * conning position - for AIS this is the antenna position
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);

/**
 * S52_setVector: OWNSHP & VESSEL
 * @objH:   (in) (transfer none): addressed S52ObjectHandle
 * @vecstb: (in): 0 - none, 1 - ground, 2 - water
 * @course: (in): (deg)
 * @speed:  (in): (kt)
 *
 * Note: @vecstb apply to VESSEL only, use S52_MAR_VECSTB for OWNSHP
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);

/**
 * S52_newPASTRK:
 * @catpst:    (in): Category of past track: 0 - undefined, 1 - primary, 2 - secondary
 * @xyznbrmax: (in): maximum number of PASTRK positon (point) (one or more)
 *
 * 'pastrk': CS(PASTRK--)
 *
 *
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
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
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
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
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);

/**
 * S52_setVESSELlabel:
 * @objH:     (in) (transfer none): addressed S52ObjectHandle
 * @newLabel: (in) (allow-none)   : NULL or a string
 *
 * (re) set label
 * Note: text priority of @newLabel is 76
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
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
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
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
 * Return: @S52ObjectHandle of the new S52_obj or FALSE if call fail
 */
DLL S52ObjectHandle STD S52_newVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin);

/**
 * S52_setVRMEBL:
 * @objH:     (in) (transfer none): addressed S52ObjectHandle
 * @pixels_x: (in)                : origin LL corner
 * @pixels_y: (in)                : origin LL corner
 * @brg:      (out) (allow-none)  : NULL or bearing from origine (FIXME: no offset from S52_setDimension())
 * @rge:      (out) (allow-none)  : NULL or range   from origine (FIXME: no offset from S52_setDimension())
 *
 * The first (@pixels_x,@pixels_y) will set the origine in the case that this object was
 * created (new) with the parameter @setOrigin set to TRUE
 *
 * Note: call will fail if no ENC loaded (via S52_loadCell)
 *
 *
 * Return: @S52ObjectHandle of the adressed S52_obj or FALSE if call fail
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
// Note: DBus has to handle system-wide msg that slow the bus
#ifdef  S52_USE_DBUS
//* commented to hide it from 'make doc' g-ir-scanner
#define S52_DBUS_OBJ_NAME  "nav.ecs.dbus"
#define S52_DBUS_OBJ_PATH  "/nav/ecs/dbus"
#define S52_DBUS_OBJ_IFACE "nav.ecs.dbus"
//*
#endif


#ifdef __cplusplus
}
#endif


#endif  // _S52_H_
