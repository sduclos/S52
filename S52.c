// S52.c: top-level interface to libS52.so plug-in
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2012  Sylvain Duclos sduclos@users.sourceforgue.net

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

// FIXME: split this file - 10 KLOC !


#include "S52.h"        // S52_view,
#include "S52utils.h"   // PRINTF(), CONF*, S52_getConfig(), S52_strstr()
#include "S52PL.h"      // S52_PRIO_NUM
#include "S52MP.h"      // S52MarinerParameter
#include "S57data.h"    // S57_prj2geo(), projXY, S57_geo
#include "S52CS.h"      // ObjClass
#include "S52GL.h"      // S52_GL_draw()

#ifdef S52_USE_GV
#include "S57gv.h"      // S57_gvLoadCell()
#else
#include "S57ogr.h"     // S57_ogrLoadCell()
#endif

#include <string.h>     // memmove()
#include <glib.h>       // GString, GArray, GPtrArray, g_strncasecmp(), g_ascii_strncasecmp()
#include <math.h>       // INFINITY

// mkfifo
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>    // unlink()
#define PIPENAME "/tmp/S52_pipe_01"



#ifndef S52_USE_PROJ
typedef struct { double u, v; } projUV;
#define projXY projUV
#define RAD_TO_DEG    57.29577951308232
#define DEG_TO_RAD     0.0174532925199432958
#endif

#define ATAN2TODEG(xyz)   (90.0 - atan2(xyz[4]-xyz[1], xyz[3]-xyz[0]) * RAD_TO_DEG)


#ifdef S52_USE_GLIB2
#include <glib/gprintf.h> //
#include <glib/gstdio.h>  // FILE
#else
#include <stdio.h>        // FILE, fopen(), ...
#include <stdlib.h>       // setenv(), putenv()
#endif

#ifdef S52_USE_PROJ
#include <proj_api.h>   // projUV, projXY, projPJ
static int _mercPrjSet = FALSE;
#endif

//#ifdef S52_USE_SOCK
//static GSocketConnection *_connection = NULL;
//#endif

#ifdef S52_USE_DBUS
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
static DBusConnection *_dbus    = NULL;
static DBusError       _dbusError;
#endif


static GTimer            *_timer      = NULL;  // lap timer

// trap signal (ESC abort rendering)
// must be compiled with -std=gnu99
#include <sys/types.h>
#include <signal.h>
static volatile gint G_GNUC_MAY_ALIAS _atomicAbort;
// 1) SIGHUP	 2) SIGINT	 3) SIGQUIT	 4) SIGILL	 5) SIGTRAP
// 6) SIGABRT	 7) SIGBUS	 8) SIGFPE	 9) SIGKILL	10) SIGUSR1
//11) SIGSEGV	12) SIGUSR2	13) SIGPIPE	14) SIGALRM	15) SIGTERM
static struct   sigaction             _old_signal_handler_SIGINT;
static struct   sigaction             _old_signal_handler_SIGQUIT;
static struct   sigaction             _old_signal_handler_SIGABRT;
static struct   sigaction             _old_signal_handler_SIGKILL;
static struct   sigaction             _old_signal_handler_SIGSEGV;
static struct   sigaction             _old_signal_handler_SIGTERM;

// not available on win32
#ifdef S52_USE_BACKTRACE
#ifndef S52_USE_ANDROID
#include <execinfo.h>
#endif
#endif


// IMO Radar: 0.25, 0.5, 0.75, 1.5, 3, 6, 12 and 24nm
//#define MIN_RANGE  0.25 // minimum range (NM)
#define MIN_RANGE  0.01 // minimum range (NM)
#define MAX_RANGE  45.0 * 60.0 // maximum range (NM) [45deg]

#define SCROLL_FAC 0.1
#define ZOOM_FAC   0.1
#define ZOOM_INI   1.0

#define S57_CELL_NAME_MAX_LEN 8 // cell name maximum lenght

typedef struct _extent {
    double S,W,N,E;
} _extent;

// FIXME: this is error prone!!
// WARNING: S52_loadPLib must be in sync with struct _cell
typedef struct _cell {
    //S52_extent ext;
    _extent    ext;
    GString   *filename;
    GPtrArray *renderBin[S52_PRIO_NUM][N_OBJ_T];//[RAD_NUM];

    GPtrArray *lights_sector;   // see _doCullLights

    localObj  *local;         // reference to object locality for CS

    // legend
    GString   *scale;         // compilation scale DSID:DSPM_CSCL or M_CSCL

    // legend from DSID
    GString   *dsid_dunistr;       // units for depth
    GString   *dsid_hunistr;       // units for height
    GString   *dsid_csclstr;       // scale  of display
    GString   *dsid_sdatstr;       // sounding datum
    GString   *dsid_vdatstr;       // vertical datum
    GString   *dsid_hdatstr;       // horizontal datum
    GString   *dsid_isdtstr;       // date of latest update
    GString   *dsid_updnstr;       // number of latest update
    GString   *dsid_edtnstr;       // edition number
    GString   *dsid_uadtstr;       // edition date
    double     dsid_heightOffset;  // bring height datum to depth datum

    // legend from M_CSCL
    GString   *cscalestr;     // compilation scale

    // legend from M_QUAL
    GString   *catzocstr;     // data quality indicator

    // legend from M_ACCY
    GString   *posaccstr;     // data accuracy indicator

    // legend from M_SDAT
    GString   *sverdatstr;    // sounding datum
    // legend from M_VDAT
    GString   *vverdatstr;    // vertical datum

    // legend from MAGVAR
    GString   *valmagstr;     // magnetic
    GString   *ryrmgvstr;
    GString   *valacmstr;

#ifdef S52_USE_SUPP_LINE_OVERLAP
    guint      baseEdgeRCID;
    GPtrArray *ConnectedNodes;  // doesn't work without sort, ConnectedNodes rcid random in some case (CA4579016)
    GPtrArray *Edges;
#endif

    // place holder for object to be drawn (after culling)
    GPtrArray *objList_supp;   // list of object on the "Supress by Radar" layer
    GPtrArray *objList_over;   // list of object on the "Over Radar" layer  (ie on top)
    GPtrArray *textList;       // hold ref to text object during rendering of a frame

    GString   *objClassList;   // hold the name of S57 object of this cell

#ifdef S52_USE_PROJ
    int        projDone;       // TRUE this cell has been projected
#endif

} _cell;

// FIXME: mutex this
static GPtrArray *_cellList    = NULL;    // list of loaded cells - sorted, big to small scale (small to large region)
static _cell     *_crntCell    = NULL;    // current cell (passed around when loading --FIXME:global (dumb)
static _cell     *_marinerCell = NULL;    // palce holder MIO's, and other (fake S57) object
#define MARINER_CELL "--6MARIN.000"

static GString   *_plibNameList = NULL;    // string that gather plibName
static GString   *_paltNameList = NULL;    // string that gather palette name
static GString   *_objClassList = NULL;    // string that gather cell object name
static GString   *_cellNameList = NULL;    // string that gather cell name

//static GPtrArray *_textList     = NULL;  // hold ref to text object during rendering of a frame
//static GPtrArray *_objList_supp = NULL;  // list of object on the "Supress by Radar" layer
//static GPtrArray *_objList_over = NULL;  // list of object on the "Over Radar" layer  (ie on top)
static GPtrArray *_objToDelList  = NULL;   // list of obj to delete in next APP cycle


static int        _doInit   = TRUE;    // init the lib
//static int        _doPick   = FALSE;   // TRUE cursor pick 'mode'
// FIXME: reparse CS of the affected MP only (ex: ship outline MP need only to reparse OWNSHP CS)
static int        _doCS     = FALSE;   // TRUE will recreate *all* CS at next draw() or drawLast()
                                       // (not only those affected by Marine's Parameter)

static int        _doCullLights = FALSE; // TRUE will do lights_sector culling when _cellList change

#include <locale.h>                    // setlocal()
static char      *_intl     = NULL;

// statistic
static int        _nCull    = 0;
static int        _nTotal   = 0;

//typedef struct _vp {
//    int x;
//    int y;
//    int width;
//    int height;
//} _vp;
//static _vp _viewPort;

//static S52_view _view = {INFINITY, INFINITY, INFINITY, INFINITY};
//typedef
static struct {
    double cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} _view;

// Note: prenvent two threads from writing into the 'scene graph' at once
// ex data comming from gpsd, so this is mostly Mariners' Object.
// Could be extended to all calls but since the main_loop already serialize
// event there is no point in doing that ATM.
// Note that DBus run from the main loop.
static  GStaticMutex _mp_mutex = G_STATIC_MUTEX_INIT;

//typedef void *_S52_objHandle;

// VRM/EBL
//static S52_objHandle *_vrmebl = NULL;
//static S52_objHandle *_vrmark = NULL;
//static double _beginLat = 0.0;
//static double _beginLon = 0.0;
//static double _endLat = 0.0;
//static double _endLon = 0.0;
// atlternate VRM/EBL
//static S52_objHandle *_vrmebl_alt = NULL;
//static S52_objHandle *_vrmark_alt = NULL;
//static double _beginLat_alt = 0.0;
//static double _beginLon_alt = 0.0;
//static double _endLat_alt = 0.0;
//static double _endLon_alt = 0.0;

// ownship
#ifdef S52_USE_GOBJECT
//static S52ObjectHandle _ownshp   = 0;
static S52ObjectHandle _SCALEB10 = 0;
static S52ObjectHandle _SCALEB11 = 0;
static S52ObjectHandle _NORTHAR1 = 0;
static S52ObjectHandle _UNITMTR1 = 0;
static S52ObjectHandle _CHKSYM01 = 0;
static S52ObjectHandle _BLKADJ01 = 0;
#else
//static S52ObjectHandle _ownshp   = NULL;
static S52ObjectHandle _SCALEB10 = NULL;
static S52ObjectHandle _SCALEB11 = NULL;
static S52ObjectHandle _NORTHAR1 = NULL;
static S52ObjectHandle _UNITMTR1 = NULL;
static S52ObjectHandle _CHKSYM01 = NULL;
static S52ObjectHandle _BLKADJ01 = NULL;
#endif

static double          _ownshp_lat = INFINITY;
static double          _ownshp_lon = INFINITY;

// VRMEBL freely movable origine
//static int           _origineIsSet = FALSE;
//static double        _origine_lat  = 0.0;
//static double        _origine_lon  = 0.0;

// CSYMB init scale bar, north arrow, unit, CHKSYM
static int _iniCSYMB  = TRUE;
//static S52ObjectHandle _SCALEB10 = NULL;
//static S52ObjectHandle _SCALEB11 = NULL;
//static S52ObjectHandle _NORTHAR1 = NULL;
//static S52ObjectHandle _UNITMTR1 = NULL;
//static S52ObjectHandle _CHKSYM01 = NULL;
//static S52ObjectHandle _BLKADJ01 = NULL;

static S52_RADAR_cb  _RADAR_cb = NULL;
//static int          _doRADAR  = TRUE;

// to write cursor lat/long on screen
static double _cursor_lat = 0.0;
static double _cursor_lon = 0.0;

// routes
//static GPtrArray *_route = NULL;    // list of legs to form a route
//static GPtrArray *_wholin= NULL;    // list of legs to form a route

//static GArray  *_arrTmp = NULL;

static char _version[] = "$Revision: 1.103 $\n"
      "libS52 0.78\n"
#ifdef S52_USE_GV
      "S52_USE_GV\n"
#endif
#ifdef GV_USE_DOUBLE_PRECISION_COORD
      "GV_USE_DOUBLE_PRECISION_COORD\n"
#endif
#ifdef S52_USE_GLIB2
      "S52_USE_GLIB2\n"
#endif
#ifdef S52_USE_DOTPITCH
      "S52_USE_DOTPITCH\n"
#endif
#ifdef S52_USE_OGR_FILECOLLECTOR
      "S52_USE_OGR_FILECOLLECTOR\n"
#endif
#ifdef S52_USE_PROJ
      "S52_USE_PROJ\n"
#endif
#ifdef S52_USE_SUPP_LINE_OVERLAP
      "S52_USE_SUPP_LINE_OVERLAP\n"
#endif
#ifdef S52_DEBUG
      "S52_DEBUG\n"
#endif
#ifdef _MINGW
      "_MINGW\n"
#endif
#ifdef S52_USE_LOG
      "S52_USE_LOG\n"
#endif
#ifdef S52_USE_DBUS
      "S52_USE_DBUS\n"
#endif
#ifdef S52_USE_SOCK
      "S52_USE_SOCK\n"
#endif
#ifdef S52_USE_GOBJECT
      "S52_USE_GOBJECT\n"
#endif
#ifdef S52_USE_BACKTRACE
      "S52_USE_BACKTRACE\n"
#endif
#ifdef S52_USE_OPENGL_VBO
      "S52_USE_OPENGL_VBO\n"
#endif
#ifdef S52_USE_ANDROID
      "S52_USE_ANDROID\n"
#endif
#ifdef S52_USE_TEGRA2
      "S52_USE_TEGRA2\n"
#endif
#ifdef S52_USE_COGL
      "S52_USE_COGL\n"
#endif
#ifdef S52_USE_A3D
      "S52_USE_A3D\n"
#endif
#ifdef S52_USE_FREETYPE_GL
      "S52_USE_FREETYPE_GL\n"
#endif
#ifdef S52_USE_SYM_AISSEL01
      "S52_USE_SYM_AISSEL01\n"
#endif
#ifdef S52_USE_WORLD
      "S52_USE_WORLD\n"
#endif
;




// check basic init
#define S52_CHECK_INIT  if (TRUE == _doInit) {                                                 \
                           PRINTF("WARNING: libS52 not initialized --try S52_init() first\n"); \
                           return FALSE;                                                       \
                        }
/*
// check if chart is loaded
static int    _mercPrjSet = FALSE;
static double _mercLat    = 0.0;
#define S52_CHECK_MERC  if (FALSE == _mercPrjSet) {                                                    \
                           PRINTF("WARNING: Mercator Projetion not set --use S52_loadCell() first\n"); \
                           return FALSE;                                                               \
                        }
*/

// CHECK THIS: check if we are shuting down also
#define S52_CHECK_MUTX  g_static_mutex_lock(&_mp_mutex);       \
                        if (NULL == _marinerCell) {            \
                           g_static_mutex_unlock(&_mp_mutex);  \
                           PRINTF("ERROR: mutex lock\n");      \
                           g_assert(0);                        \
                           exit(0);                            \
                           return FALSE;                       \
                        }


static double     _validate_bool(double val)
{
    val = (val==0.0)? 0.0 : 1.0;

    PRINTF("toggle to: %s\n", (val==0.0)? "OFF" : "ON");

    return val;
}

static double     _validate_meter(double val)
{
    PRINTF("Meter: %f\n", val);

    return val;
}

static double     _validate_nm(double val)
{
    if (val < 0.0) val = -val;

    PRINTF("Nautical Mile: %f\n", val);

    return val;
}

static double     _validate_min(double val)
{

    if (val < 0.0) {
        PRINTF("WARNING: time negatif, reset to 0.0: %f\n", val);
        val = 0.0;
    }

    //PRINTF("Minute: %f\n", val);

    return val;
}

static double     _validate_int(double val)
{
    int val_int = (int) val;

    return (double)val_int;
}

static double     _validate_disp(double val)
{
    int crntMask = (int) S52_getMarinerParam(S52_MAR_DISP_CATEGORY);
    int newMask  = (int) val;
    int maxMask  =
        S52_MAR_DISP_CATEGORY_BASE   +
        S52_MAR_DISP_CATEGORY_STD    +
        S52_MAR_DISP_CATEGORY_OTHER  +
        S52_MAR_DISP_CATEGORY_SELECT;

    if (newMask < S52_MAR_DISP_CATEGORY_BASE || newMask > maxMask) {
        PRINTF("WARNING: ignoring category value (%i)\n", newMask);
        return crntMask;
    }


    if (!(0 == newMask)                           &&
        !(S52_MAR_DISP_CATEGORY_BASE   & newMask) &&
        !(S52_MAR_DISP_CATEGORY_STD    & newMask) &&
        !(S52_MAR_DISP_CATEGORY_OTHER  & newMask) &&
        !(S52_MAR_DISP_CATEGORY_SELECT & newMask) ) {

        PRINTF("WARNING: ignoring category value (%i)\n", newMask);

        return crntMask;
    }

    PRINTF("Display Priority: current mask:0x%x (mask to apply:0x%x)\n", crntMask, newMask);

    if (crntMask &  newMask)
        crntMask &= ~newMask;
    else
        crntMask |= newMask;

    PRINTF("Display Priority: new current mask is:0x%x\n", crntMask);

    return (double)crntMask;
}

static double     _validate_mar(double val)
// S52_MAR_DISP_LAYER_LAST  - MARINERS' CATEGORY (drawn on top - last)
{
    int crntMask = (int) S52_getMarinerParam(S52_MAR_DISP_LAYER_LAST);
    int newMask  = (int) val;
    int maxMask  =
        S52_MAR_DISP_LAYER_LAST_NONE  +
        S52_MAR_DISP_LAYER_LAST_STD   +
        S52_MAR_DISP_LAYER_LAST_OTHER +
        S52_MAR_DISP_LAYER_LAST_SELECT;

    if (newMask < S52_MAR_DISP_LAYER_LAST_NONE || newMask > maxMask) {
        PRINTF("WARNING: ignoring mariners' value (%i)\n", newMask);
        return crntMask;
    }

    if (!(S52_MAR_DISP_LAYER_LAST_NONE   & newMask) &&
        !(S52_MAR_DISP_LAYER_LAST_STD    & newMask) &&
        !(S52_MAR_DISP_LAYER_LAST_OTHER  & newMask) &&
        !(S52_MAR_DISP_LAYER_LAST_SELECT & newMask) ) {

        PRINTF("WARNING: ignoring mariners' value (%i)\n", newMask);

        return crntMask;
    }

    if (newMask & crntMask)
        crntMask &= ~newMask;
    else
        crntMask += newMask;


    return (double)crntMask;
}

static double     _validate_pal(double val)
{
    int palTblsz = S52_PL_getPalTableSz();

    if (val >= palTblsz) val = 0.0;
    if (val <  0.0     ) val = palTblsz-1;

    PRINTF("Color Palette set to: %s (%f)\n", S52_PL_getPalTableNm((int)val), val);

    return val;
}

static double     _validate_deg(double val)
{
    // AIS a value of 360 mean heading course unknown
    //if (val < 0.0 || 360.0 <= val) {
    if (val < 0.0 || 360.0 < val) {
        PRINTF("WARNING: degree out of bound [0.0 .. 360.0], reset to 0.0: %f\n", val);
        val = 0.0;
    }

    //PRINTF("set degree to: %f\n", val);

    return val;
}

static double     _validate_lat(double lat)
{
    //double latMin = _mercLat - 90.0;
    //double latMax = _mercLat + 90.0;

    if (lat < -90.0 || 90.0 < lat) {
    //if (lat < latMin || latMax < lat) {
        //PRINTF("WARNING: latitude out of bound [-90.0 .. +90.0] 0f %f, reset to 0.0: %f\n", _mercLat, lat);
        PRINTF("WARNING: latitude out of bound [-90.0 .. +90.0], reset to 0.0: %f\n", lat);
        lat = 0.0;
    }

    //PRINTF("set degree to: %f\n", val);

    return lat;
}

static double     _validate_lon(double val)
{
    if (val < -180.0 || 180.0 < val) {
        PRINTF("WARNING: longitude out of bound [-180.0 .. +180.0], reset to 0.0: %f\n", val);
        val = 0.0;
    }

    //PRINTF("set degree to: %f\n", val);

    return val;
}

static int        _validate_screenPos(double *xx, double *yy)
{
    /*
    // failsafe, init viewPort
    if (0==_viewPort.width || 0==_viewPort.height) {
        S52_GL_getViewPort(&_viewPort.x, &_viewPort.y, &_viewPort.width, &_viewPort.height);
        //PRINTF("ERROR: no viewPort .. use S52_setViewPort() first\n");
        //return FALSE;
    }

    if (*x < 0.0) *x = 0.0;
    if (*x > _viewPort.x+_viewPort.width)  *x = _viewPort.x + _viewPort.width;

    if (*y < 0.0) *y = 0.0;
    if (*y > _viewPort.y+_viewPort.height) *y = _viewPort.y + _viewPort.height;
    */

    int x;
    int y;
    int width;
    int height;

    S52_GL_getViewPort(&x, &y, &width, &height);

    if (*xx < 0.0)      *xx = 0.0;
    if (*xx > x+width)  *xx = x + width;

    if (*yy < 0.0)      *yy = 0.0;
    if (*yy > y+height) *yy = y + height;

    return TRUE;
}

static double     _validate_filter(double mask)
{
    int crntMask = (int) S52_getMarinerParam(S52_MAR_CMD_WRD_FILTER);
    int newMask  = (int) mask;
    int maxMask  =
        S52_CMD_WRD_FILTER_SY +
        S52_CMD_WRD_FILTER_LS +
        S52_CMD_WRD_FILTER_LC +
        S52_CMD_WRD_FILTER_AC +
        S52_CMD_WRD_FILTER_AP +
        S52_CMD_WRD_FILTER_TX;

    if (newMask < S52_CMD_WRD_FILTER_SY || newMask > maxMask) {
        PRINTF("WARNING: ignoring filter mask (%i)\n", newMask);
        return crntMask;
        //return S52_getMarinerParam(S52_MAR_CMD_WRD_FILTER);
    }

    if (newMask & crntMask)
        crntMask -= newMask;
    else
        crntMask += newMask;


    //PRINTF("Filter State:\n");
    //PRINTF("S52_CMD_WRD_FILTER_SY:%s\n",(S52_CMD_WRD_FILTER_SY & crntVal) ? "TRUE" : "FALSE");
    //PRINTF("S52_CMD_WRD_FILTER_LS:%s\n",(S52_CMD_WRD_FILTER_LS & crntVal) ? "TRUE" : "FALSE");
    //PRINTF("S52_CMD_WRD_FILTER_LC:%s\n",(S52_CMD_WRD_FILTER_LC & crntVal) ? "TRUE" : "FALSE");
    //PRINTF("S52_CMD_WRD_FILTER_AC:%s\n",(S52_CMD_WRD_FILTER_AC & crntVal) ? "TRUE" : "FALSE");
    //PRINTF("S52_CMD_WRD_FILTER_AP:%s\n",(S52_CMD_WRD_FILTER_AP & crntVal) ? "TRUE" : "FALSE");

    return (double)crntMask;
}

static double     _validate_floatPositive(double val)
{
    //return fabs(val);
    return ABS(val);
}

static int        _fixme(S52MarinerParameter paramName)
{
    PRINTF("FIXME: S52MarinerParameter %i not implemented\n", paramName);

    return TRUE;
}

//DLL double STD S52_getMarinerParam(const char *paramName)
DLL double STD S52_getMarinerParam(S52MarinerParameter paramID)
// return Mariner parameter or S52_MAR_NONE if fail
// FIXME: check mariner param against groups selection
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    double val = S52_MP_get(paramID);

    PRINTF("paramID:%i, val:%f\n", paramID, val);

    g_static_mutex_unlock(&_mp_mutex);

    return val;
}

DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val)
// validate and set Mariner Parameter
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    PRINTF("paramID:%i, val:%f\n", paramID, val);

    switch (paramID) {
        case S52_MAR_NONE                : break;
        // _SEABED01->DEPARE01;
        case S52_MAR_TWO_SHADES          : val = _validate_bool(val);  _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_PATTERN     : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_SHIPS_OUTLINE       : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_FULL_SECTORS        : val = _validate_bool(val);                   break;
        // RESARE02;
        case S52_MAR_SYMBOLIZED_BND      : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_SYMPLIFIED_PNT      : val = _validate_bool(val);                   break;

        // DEPCNT02; LIGHTS05;
        //case S52_MAR_SHOW_TEXT           : val = _validate_meter(val); _doCS = TRUE;    break;
        case S52_MAR_SHOW_TEXT           : val = _validate_bool(val);  _doCS = TRUE;    break;
        // DEPCNT02; _SEABED01->DEPARE01; _UDWHAZ03->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_CONTOUR      : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SNDFRM02->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_DEPTH        : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_CONTOUR     : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_DEEP_CONTOUR        : val = _validate_meter(val); _doCS = TRUE;    break;
        case S52_MAR_DISTANCE_TAGS       : val = _validate_nm(val);    _fixme(paramID); break;
        case S52_MAR_TIME_TAGS           : val = _validate_min(val);   _fixme(paramID); break;
        case S52_MAR_DISP_CATEGORY       : g_static_mutex_unlock(&_mp_mutex);
                                           val = _validate_disp(val);
                                           S52_CHECK_MUTX;
                                           break;
        case S52_MAR_COLOR_PALETTE       : val = _validate_pal(val);                    break;

        case S52_MAR_VECPER              : val = _validate_min(val);   _doCS = TRUE;    break;
        case S52_MAR_VECMRK              : val = _validate_int(val);   _doCS = TRUE;    break;
        case S52_MAR_VECSTB              : val = _validate_int(val);   _doCS = TRUE;    break;

        case S52_MAR_HEADNG_LINE         : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_BEAM_BRG_NM         : val = _validate_nm(val);    _doCS = TRUE;    break;

        case S52_MAR_FONT_SOUNDG         : val = _validate_bool(val);                   break;
        // DEPARE01; DEPCNT02; _DEPVAL01; SLCONS03; _UDWHAZ03;
        case S52_MAR_DATUM_OFFSET        : val = _validate_meter(val); _doCS = TRUE;    break;
        case S52_MAR_SCAMIN              : val = _validate_bool(val);                   break;
        case S52_MAR_ANTIALIAS           : val = _validate_bool(val);                   break;
        case S52_MAR_QUAPNT01            : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_OVERLAP        : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_LAYER_LAST     : g_static_mutex_unlock(&_mp_mutex);
                                           val = _validate_mar (val);
                                           S52_CHECK_MUTX;
                                           break;

        case S52_MAR_ROT_BUOY_LIGHT      : val = _validate_deg(val);                    break;

        case S52_MAR_DISP_CRSR_POS       : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_GRATICULE      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_WHOLIN         : val = _validate_int(val);                    break;

        case S52_MAR_DISP_LEGEND         : val = _validate_bool(val);                   break;

        case S52_MAR_DOTPITCH_MM_X       : val = _validate_floatPositive(val);          break;
        case S52_MAR_DOTPITCH_MM_Y       : val = _validate_floatPositive(val);          break;

        case S52_MAR_DISP_CALIB          : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_DRGARE_PATTERN : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_NODATA_LAYER   : val = _validate_bool(val);                   break;
        case S52_MAR_DEL_VESSEL_DELAY    : val = _validate_int(val);                    break;
        case S52_MAR_DISP_AFTERGLOW      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_CENTROIDS      : val = _validate_bool(val);                   break;

        case S52_MAR_CMD_WRD_FILTER      : g_static_mutex_unlock(&_mp_mutex);
                                           val = _validate_filter(val);
                                           S52_CHECK_MUTX;
                                           break;

        default:
            //PRINTF("WARNING: unknown Mariner Paramater type (%)\n", paramID);
            PRINTF("WARNING: unknown Mariner's Parameter type (%i)\n", paramID);

            g_static_mutex_unlock(&_mp_mutex);

            return FALSE;
    }

    int ret = S52_MP_set(paramID, val);

    g_static_mutex_unlock(&_mp_mutex);

    //return S52_MP_set(paramID, val);
    return ret;
}

DLL int    STD S52_setTextDisp(int prioIdx, int count, int state)
{

    if (prioIdx<0 || 99<prioIdx) {
        PRINTF("WARNING: prioIdx out of bound (%i)\n", prioIdx);
        return FALSE;
    }

    if (count<0 || 100<count) {
        PRINTF("WARNING: count out of bound (%i)\n", count);
        return FALSE;
    }

    if (100 < (prioIdx + count)) {
        PRINTF("WARNING: prioIdx + count out of bound (%i)\n", prioIdx + count);
        return FALSE;
    }

    state = _validate_bool(state);

    S52_MP_setTextDisp(prioIdx, count, state);

    return TRUE;
}

DLL int    STD S52_getTextDisp(int prioIdx)
{
    if (prioIdx<0 || 99<prioIdx) {
        PRINTF("WARNING: prioIdx out of bound (%i)\n", prioIdx);
        return FALSE;
    }

    return S52_MP_getTextDisp(prioIdx);
}


#if 0
static gint       _cmpLine(gconstpointer pointA, gconstpointer pointB, gpointer user_data)
// compare line
{
    guint   nptA;
    double *pptA = (double *)pointA;
    guint   nptB;
    double *pptB = (double *)pointB;

    S57_getGeoData((S57_geo *)lineA, 1, &nptA, &pptA);
    S57_getGeoData((S57_geo *)lineB, 1, &nptB, &pptB);

    // check long
    if (*pptA > *pptB) return  1;
    if (*pptA < *pptB) return -1;

    // at this point longA == longB

    // check lat
    if (*(pptA+1) > *(pptB+1)) return  1;
    if (*(pptA+1) < *(pptB+1)) return -1;

    // same point --this mean that a new line object start at the
    // same geo point

    // check end point
    PRINTF("ERROR: _cmpLine()\n");
    g_assert(0);

    return 0;



    //return strncmp((char*)nameA, (char*)nameB, S52_LUP_NMLN);
}
#endif

static gint       _cmpCell(gconstpointer a, gconstpointer b)
{
    //gconstpointer A = *a;
    //_cell *B = (_cell*) b;
    _cell *A = *(_cell**) a;
    _cell *B = *(_cell**) b;

    if (A->filename->str[2] ==  B->filename->str[2])
        return 0;

    if (A->filename->str[2] >  B->filename->str[2])
        return -1;
    else
        return  1;
}

static _cell*     _addCell(const char *filename)
// add this cell else NULL (if allready loaded)
// assume filename is not NULL
{
    //guint        idx   = 0;
    gchar *fname = NULL;

    // strip path
    //WARNING: g_basename() deprecated use g_path_get_basename()
    //gchar* g_path_get_basename(const gchar *file_name) G_GNUC_MALLOC;
    //fname = g_basename(filename);
    fname = g_path_get_basename(filename);

    if (NULL == _cellList)
        _cellList = g_ptr_array_new();

    //if (NULL == _textList)
    //    _textList = g_ptr_array_new();

    //if (NULL == _objList_supp)
    //    _objList_supp = g_ptr_array_new();

    //if (NULL == _objList_over)
    //    _objList_over = g_ptr_array_new();

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        // check if allready loaded
        if (0 == S52_strncmp(fname, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
            PRINTF("WARNING: cell (%s) already loaded\n", fname);
            g_free(fname);

            return NULL;
        }

        // sort: bigger region last (eg 553311)
        //if (fname[2] > c->filename->str[2])
        //    break;
    }

    {   // init cell
        _cell *cell = g_new0(_cell, 1);
        int i,j;
        for (i=0; i<S52_PRIO_NUM; ++i) {
            for (j=0; j<S52_N_OBJ; ++j)
                cell->renderBin[i][j] = g_ptr_array_new();
        }

        cell->filename = g_string_new(fname);
        g_free(fname);


        cell->ext.S =  INFINITY;
        cell->ext.W =  INFINITY;
        cell->ext.N = -INFINITY;
        cell->ext.E = -INFINITY;

        // moved to _cellAddGeo
        //cell->lights_sector = g_ptr_array_new();

        cell->local = S52_CS_init();

        cell->textList = g_ptr_array_new();

        cell->objList_supp = g_ptr_array_new();
        cell->objList_over = g_ptr_array_new();

        cell->objClassList = g_string_new("");


        cell->projDone = FALSE;

        //_cellList =  g_ptr_array_insert_val(_cellList, idx, cell);
        g_ptr_array_add(_cellList, cell);
        // sort cell: bigger region (small scale) last (eg 553311)
        g_ptr_array_sort(_cellList, _cmpCell);



        //_crntCell = &g_array_index(_cellList, _cell, idx);
        _crntCell = cell;
    }


//#ifdef S52_USE_SUPP_LINE_OVERLAP
//    if (NULL == _lines)
//        _lines = g_ptr_array_new();
//#endif

    return _crntCell;
}

#if 0
static _cell*     _removeCell(_cell *ch)
// remove a cell from the set, else NULL
{
    for (guint i=0; i<_cellList->len; ++i) {
        //_cell *c = &g_array_index(_cellList, _cell, i);
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        if (c == ch) {
            //g_array_remove_index(_cellList, i);
            g_ptr_array_remove_index(_cellList, i);
            return c;
        }
    }

    return NULL;
}
#endif
//#include <stdio.h>
//#include "valgrind.h"
//#include "memcheck.h"
/*
__attribute__((noinline))
int _app(void);
//int I_WRAP_SONAME_FNNAME_ZU(NONE, _app)(void)
//int I_WRAP_SONAME_FNNAME_ZU(libS52Zdso, _app)(void)
{
   int    result;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   printf("XXXX _app() wrapper start\n");
   CALL_FN_W_v(result, fn);
   //CALL_FN_v_v(result, fn);
   printf("XXXX _app() wrapper end: result %d\n", result);
   return result;
}
*/

#ifdef S52_USE_DBUS
    static int _initDBus();
#endif




//#include "unwind-minimal.h"
#ifdef S52_USE_BACKTRACE
#ifdef S52_USE_ANDROID

#define ANDROID
#define UNW_LOCAL_ONLY
#include <unwind.h>          // _Unwind_*()
#include <libunwind-ptrace.h>
//#include <libunwind-arm.h>
//#include <libunwind.h>
//#include <libunwind-common.h>


#define MAX_FRAME 128
struct callStackSaver {
   unsigned short crntFrame;
   unsigned int   ptrArr[MAX_FRAME];
   unsigned int   libAdjustment;
};


void              _dump_crash_report(unsigned pid)
// shortened code from android's debuggerd
// to get a backtrace on ARM
{
    unw_addr_space_t as;
    struct UPT_info *ui;
    unw_cursor_t     cursor;

    as = unw_create_addr_space(&_UPT_accessors, 0);
    ui = _UPT_create(pid);

    int ret = unw_init_remote(&cursor, as, ui);
    if (ret < 0) {
        PRINTF("unw_init_remote() failed [pid %i]\n", pid);
        _UPT_destroy(ui);
        return;
    }

    PRINTF("backtrace of the remote process (pid %d) using libunwind-ptrace:\n", pid);

    do {
        unw_word_t ip, sp, offp;
        char buf[512];

        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        unw_get_proc_name(&cursor, buf, sizeof (buf), &offp);

        PRINTF("  ip: %10p, sp: %10p   %s\n", (void*) ip, (void*) sp, buf);

    } while ((ret = unw_step (&cursor)) > 0);

    _UPT_destroy (ui);
}

static            _Unwind_Reason_Code trace_func(struct _Unwind_Context *ctx, void *user_data)
{
    //unsigned int rawAddr = __gnu_Unwind_Find_exidx(ctx); //  _Unwind_GetIP(ctx);
    unsigned int rawAddr = _Unwind_GetIP(ctx);
    //unsigned int rawAddr = (unsigned int) __builtin_frame_address(0);
    //unsigned int rawAddr = __builtin_return_address(0);

    struct callStackSaver* state = (struct callStackSaver*) user_data;

    if (state->crntFrame < MAX_FRAME) {
        state->ptrArr[state->crntFrame] = rawAddr - state->libAdjustment;
        ++state->crntFrame;
    }

    return _URC_CONTINUE_UNWIND;
    //return _URC_OK;
}
//*/

#if 0
/*
static unsigned int _GetLibraryAddress(const char* libraryName)
{
    FILE* file = fopen("/proc/self/maps", "rt");
    if (file==NULL) {
        return 0;
    }

    unsigned int addr = 0;
    //const char* libraryName = "libMyLibraryName.so";
    int len_libname = strlen(libraryName);

    char buff[256];
    while( fgets(buff, sizeof(buff), file) != NULL ) {
        int len = strlen(buff);
        if( len > 0 && buff[len-1] == '\n' ) {
            buff[--len] = '\0';
        }
        if (len <= len_libname || memcmp(buff + len - len_libname, libraryName, len_libname)) {
            continue;
        }

        unsigned int start, end, offset;
        char flags[4];
        if ( sscanf( buff, "%zx-%zx %c%c%c%c %zx", &start, &end, &flags[0], &flags[1], &flags[2], &flags[3], &offset ) != 7 ) {
            continue;
        }

        if ( flags[0]=='r' && flags[1]=='-' && flags[2]=='x' ) {
            addr = start - offset;
            break;
        }
    } // while

    fclose(file);

    return addr;
}
*/
#endif

#if 0
/*
static int        _get_backtrace (void** buffer, int n)
{
    unw_cursor_t  cursor;
    unw_context_t uc;
    unw_word_t    ip;
    unw_word_t    sp;

    //unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);

    int i = 0;
    while (unw_step(&cursor) > 0 && i < n) {
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        buffer[i] = (void*)ip;
        i++;
        //printf ("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
    }

    return i;
}
//*/
#endif

static int        _unwind(void)
{
// ============ test using Unwind ====================================
/*
_URC_OK                       = 0,  // operation completed successfully
_URC_FOREIGN_EXCEPTION_CAUGHT = 1,
_URC_END_OF_STACK             = 5,
_URC_HANDLER_FOUND            = 6,
_URC_INSTALL_CONTEXT          = 7,
_URC_CONTINUE_UNWIND          = 8,
_URC_FAILURE                  = 9   // unspecified failure of some kind
*/
        /*
        struct callStackSaver state;
        //state.libAdjustment = _GetLibraryAddress("libs52android.so");
        state.libAdjustment = 0;
        state.crntFrame     = 0;
        state.ptrArr[0]     = 0;


        _Unwind_Reason_Code code = _Unwind_Backtrace(trace_func, (void*)&state);
        PRINTF("After _Unwind_Backtrace() .. code=%i, nFrame=%i, frame=%x\n", code, state.crntFrame, state.ptrArr[0]);
        //*/

       // int nptrs = _get_backtrace((void**)&buffer, 128);

}

#endif // S52_USE_ANDROID

static int        _backtrace(void)
{
    void  *buffer[128];
    char **strings;
    int    nptrs = 0;

    nptrs = backtrace(buffer, 128);

    PRINTF("==== backtrace() returned %d addresses ====\n", nptrs);

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        PRINTF("ERROR: backtrace_symbols() .. no symbols");
    }

    int i;
    for (i=0; i<nptrs; ++i)
        PRINTF("==== %s\n", strings[i]);

    free(strings);

    return TRUE;
}

#endif // S52_USE_BACKTRACE


// signal
static void       _trapSIG(int sig, siginfo_t *info, void *secret)
{
    // 2 -
    if (SIGINT == sig) {
        PRINTF("Signal SIGINT(%i) cought .. setting up atomic to abort draw()\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);

        // continue with normal sig handling
        _old_signal_handler_SIGINT.sa_sigaction(sig, info, secret);
        //return;
    }

    //  3  - Quit (POSIX)
    //sigaction(SIGQUIT, &sa, &_old_signal_handler_SIGQUIT);
    if (SIGQUIT == sig) {
        PRINTF("Signal SIGQUIT(%i) cought .. Quit\n", sig);

        // continue with normal sig handling
        _old_signal_handler_SIGQUIT.sa_sigaction(sig, info, secret);
        //return;
    }

    //  6  - Abort (ANSI)
    //sigaction(SIGABRT, &sa, &_old_signal_handler_SIGABRT);
    if (SIGABRT == sig) {
        PRINTF("Signal SIGABRT(%i) cought .. Abort\n", sig);

        // continue with normal sig handling
        _old_signal_handler_SIGABRT.sa_sigaction(sig, info, secret);
        //return;
    }

    //  9  - Kill, unblock-able (POSIX)
    //sigaction(SIGKILL, &sa, &_old_signal_handler_SIGKILL);
    if (SIGKILL == sig) {
        PRINTF("Signal SIGKILL(%i) cought .. Kill\n", sig);

        // continue with normal sig handling
        _old_signal_handler_SIGKILL.sa_sigaction(sig, info, secret);
        //return;
    }

    // 11 - Segmentation violation
    if (SIGSEGV == sig) {
        PRINTF("Segmentation violation cought (%i) ..\n", sig);

#ifdef S52_USE_BACKTRACE
#ifdef S52_USE_ANDROID
        _unwind();

        // break loop - android debuggerd rethrow SIGSEGV
        exit(0);

#endif  // S52_USE_ANDROID


        _backtrace();

        // continue with normal sig handling
        _old_signal_handler_SIGSEGV.sa_sigaction(sig, info, secret);

#endif  //S52_USE_BACKTRACE
    }

    // 15 - Termination (ANSI)
    //sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);
    if (SIGTERM == sig) {
        PRINTF("Signal SIGTERM(%i) cought .. Termination\n", sig);

        // continue with normal sig handling
        _old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);
        //return;
    }


#ifdef S52_USE_ANDROID
        // break loop - android debuggerd rethrow SIGSEGV
        //exit(0);
#endif


    // shouldn't reach this !?
    g_assert_not_reached();  // turn off via G_DISABLE_ASSERT

/*
#ifdef S52_USE_BREAKPAD
    // experimental
    MinidumpFileWriter writer;
    writer.Open("/tmp/minidump.dmp");
    TypedMDRVA<MDRawHeader> header(&writer_);
    header.Allocate();
    header->get()->signature = MD_HEADER_SIGNATURE;
    writer.Close();
#endif
*/

}

#ifdef S52_USE_PIPE
static gboolean   _pipeReadWrite(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    GError    *error = NULL;
    GString   *str  = g_string_new("");
    GIOStatus  stat = g_io_channel_read_line_string(source, str, NULL, &error);
    PRINTF("GIOStatus: %i\n", stat);

    if (NULL != error) {
        PRINTF("ERROR: %s\n", error->message);
        g_error_free(error);
    }

    PRINTF("PIPE: |%s|\n", str->str);


    gchar** palNmL = g_strsplit(str->str, ",", 0);
    gchar** palNm  = palNmL;

    //const char * STD S52_getPLibsIDList(void);
    if (0 == ("S52_getPLibsIDList", *palNm)) {
        GError   *error = NULL;
        gsize     bout  = 0;
        PRINTF("PIPE: %s\n", *palNm);
        GIOStatus stat  = g_io_channel_write_chars(source, S52_getPLibsIDList(), -1, &bout, &error);

        PRINTF("GIOStatus: %i\n", stat);
    }


    //while (NULL != *palNm) {
    //    switch(type): {
    //        case 's':
    //    }
    //    palNm++;
    //}
    g_strfreev(palNmL);

    g_string_free(str, TRUE);

    return TRUE;
}

static int        _pipeWatch(gpointer dummy)
// add watch to pipe
{
    // use FIFO pipe instead of DBug
    // less overhead - bad on ARM

    unlink(PIPENAME);

    int fdpipe = mkfifo(PIPENAME, S_IFIFO | S_IRUSR | S_IWUSR);

    int fd     = open(PIPENAME, O_RDWR);

    GIOChannel   *pipe      = g_io_channel_unix_new(fd);
    guint         watchID   = g_io_add_watch(pipe, G_IO_IN, _pipeReadWrite, NULL);

    // FIXME: case of no main loop
    //GMainContext *_pipeCtx  = g_main_context_new();
    //GMainLoop    *_pipeLoop = g_main_loop_new(_pipeCtx, TRUE);
    //g_main_loop_run(_pipeLoop);

    return TRUE;
}
#endif

static int        _getCellsExt(_extent* ext);
static int        _initPROJ(void)
{
    double clat = 0.0;
    _extent ext;
    _getCellsExt(&ext);
    clat = (ext.N + ext.S) / 2.0;

    S57_setMercPrj(clat);
    //_mercPrjSet = TRUE;

    // while here, set default view center
    _view.cLat  =  (ext.N + ext.S) / 2.0;
    _view.cLon  =  (ext.E + ext.W) / 2.0;
    _view.rNM   = ((ext.N - ext.S) / 2.0) * 60.0;
    _view.north = 0.0;

    {// debug
        double xyz[3] = {_view.cLat, _view.cLon, 0.0};
        S57_geo2prj3dv(1, xyz);
        PRINTF("PROJ CENTER: lat:%f, lon:%f, rNM:%f\n", xyz[0], xyz[1], _view.rNM);
    }

    return TRUE;
}

static int        _projectCells(void)
{
    for (guint k=0; k<_cellList->len; ++k) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, k);
        if (FALSE == c->projDone) {
            for (guint i=0; i<S52_PRIO_NUM; ++i) {
                for (guint j=0; j<N_OBJ_T; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj  = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo  = S52_PL_getGeo(obj);
                        S57_geo2prj(geo);
                    }
                }
            }

            // then project lights of this cell, if any
            if (NULL != c->lights_sector) {
                for (guint i=0; i<c->lights_sector->len; ++i) {
                    S52_obj *obj  = (S52_obj *)g_ptr_array_index(c->lights_sector, i);
                    S57_geo *geo  = S52_PL_getGeo(obj);
                    S57_geo2prj(geo);
                }
            }
            c->projDone = TRUE;
        }
    }

    return TRUE;
}

DLL int    STD S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb);
DLL int    STD S52_loadObject(const char *objname, void *shape);
static _cell*     _loadBaseCell(char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb);

#ifdef S52_USE_DOTPITCH
//DLL int    STD S52_init(int pixels_w, int pixels_h, int pixels_wmm, int pixels_hmm)
//DLL int    STD S52_init(unsigned int screen_pixels_w, unsigned int screen_pixels_h, unsigned int screen_mm_w, unsigned int screen_mm_h, S52_error_cb err_cb)
//DLL int   STD  S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h)
DLL int    STD S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h, S52_error_cb err_cb)
#else
DLL int    STD S52_init(void)
#endif
// init basic stuff (outside of the main loop)
{
    //libS52Zdso();

    // check if run as root
    if (0 == getuid()) {
        PRINTF("ERROR: do NOT run as SUPERUSER (root) .. exiting\n");
        exit(0);
    }

    // check if init already done
    if (!_doInit) {
        PRINTF("WARNING: libS52 already initalized\n");
        return FALSE;
    }

    ///////////////////////////////////////////////////////////
    //
    // init mem stat stuff
    //
    //extern GMemVTable *glib_mem_profiler_table;
    //g_mem_set_vtable(glib_mem_profiler_table);
    //g_mem_profile();


#ifdef S52_USE_LOG
    S52_initLog(err_cb);
    //S52_initLog(NULL);
    //S52_LOG("starting log");
#else
    if (NULL != err_cb)
        PRINTF("INFO: compiler flags 'S52_USE_LOG' not set, 'S52_error_cb' will not be used\n");
#endif


    PRINTF("screen_pixels_w: %i, screen_pixels_h: %i, screen_mm_w: %i, screen_mm_h: %i\n",
            screen_pixels_w,     screen_pixels_h,     screen_mm_w,     screen_mm_h);


    g_atomic_int_set(&_atomicAbort, FALSE);

    //////////////////////////////////////////////////////////
    // init signal handler
    {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = _trapSIG;
        sigemptyset(&sa.sa_mask);
        //sa.sa_flags = SA_RESTART | SA_SIGINFO;
        sa.sa_flags = SA_SIGINFO|SA_RESETHAND;

        //  2 - Interrupt (ANSI) - user press ESC to stop rendering
        sigaction(SIGINT,  &sa, &_old_signal_handler_SIGINT);
        //  3  - Quit (POSIX)
        sigaction(SIGQUIT, &sa, &_old_signal_handler_SIGQUIT);
        //  6  - Abort (ANSI)
        sigaction(SIGABRT, &sa, &_old_signal_handler_SIGABRT);
        //  9  - Kill, unblock-able (POSIX)
        sigaction(SIGKILL, &sa, &_old_signal_handler_SIGKILL);
        // 11 - Segmentation violation (ANSI).
        sigaction(SIGSEGV, &sa, &_old_signal_handler_SIGSEGV);   // loop in android
        // 15 - Termination (ANSI)
        //sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);

        // debug - will trigger SIGSEGV for testing
        //_cell *c = 0x0;
        //c->ext.S = INFINITY;
    }

    ///////////////////////////////////////////////////////////
    // init global info
    //
    if (NULL == _plibNameList)
        _plibNameList = g_string_new("S52raz-3.2.rle (Internal Chart No 1)");
    if (NULL == _paltNameList)
        _paltNameList = g_string_new("");
    if (NULL == _cellNameList)
        _cellNameList = g_string_new("");
    if (NULL == _objClassList)
        _objClassList = g_string_new("");
    if (NULL == _objToDelList)
        _objToDelList = g_ptr_array_new();


    ///////////////////////////////////////////////////////////
    // init global info
    //
#ifdef S52_USE_GV
    // HACK: fake dot pitch
    w   = 10;
    h   = 10;
    wmm = 3;
    hmm = 3;
#endif


#ifdef S52_USE_DOTPITCH
    // FIXME: validate
    if (0==screen_pixels_w || 0==screen_pixels_h || 0==screen_mm_w || 0==screen_mm_h) {
        PRINTF("ERROR: invalid screen size\n");
        return FALSE;
    }

    S52_GL_setDotPitch(screen_pixels_w, screen_pixels_h, screen_mm_w, screen_mm_h);
#endif



    ///////////////////////////////////////////////////////////
    // init env stuff for GDAL/OGR/S57
    //

    // GDAL/OGR/S57 options (1: overwrite env)

    // GDAL debug info ON
    //g_setenv("CPL_DEBUG", "ON", 1);

#ifdef S52_USE_GLIB2
#ifdef S52_USE_SUPP_LINE_OVERLAP
    // make OGR return primitive and linkage
    g_setenv("OGR_S57_OPTIONS",
             "UPDATES=APPLY,SPLIT_MULTIPOINT=ON,PRESERVE_EMPTY_NUMBERS=ON,RETURN_PRIMITIVES=ON,RETURN_LINKAGES=ON,LNAM_REFS=ON",
             1);
#else
    g_setenv("OGR_S57_OPTIONS",
             "LNAM_REFS:ON,UPDATES:APPLY,SPLIT_MULTIPOINT:ON,PRESERVE_EMPTY_NUMBERS:ON",
             1);


#endif // S52_USE_SUPP_LINE_OVERLAP

#else
    const char *name = "OGR_S57_OPTIONS";
    //const char *value= "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON,PRESERVE_EMPTY_NUMBERS:ON,RETURN_LINKAGES:ON";
    const char *value= "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON,PRESERVE_EMPTY_NUMBERS:ON";

    //setenv("OGR_S57_OPTIONS", "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON", 1);
#include <stdlib.h>
    //extern int setenv(const char *name, const char *value, int overwrite);
    setenv(name, value, 1);
    env = g_getenv("OGR_S57_OPTIONS");
    PRINTF("%s\n", env);
#endif

#ifdef S52_USE_ANDROID
    g_setenv("S57_CSV", "/sdcard/s52android/gdal_data", 1);
#else
    //if (NULL == g_getenv("S57_CSV")) {
    //    PRINTF("env. var. 'S57_CSV' not found .. aborting\n");
    //    exit(0);
    //}
#endif

    _intl = setlocale(LC_ALL, "C");
    //_intl = setlocale(LC_ALL, "");
    //_intl = setlocale(LC_ALL, "fr_CA.utf8");
    //_intl = setlocale(LC_ALL, "POSIX");



    ///////////////////////////////////////////////////////////
    // init S52 stuff
    //
    // load basic def (ex color, CS, ...)
    S52_PL_init();

    if (FALSE == S52_GL_init()) {
        PRINTF("S52_GL_init() failed\n");
        //return FALSE;
    }

    // put an error No in S52_MAR_NONE
    S52_MP_set(S52_MAR_NONE, INFINITY);

    // setup the virtual cell that will hold mariner's objects
    // NOTE: there is no IHO cell at scale '6', this garanty that
    // objects of this 'cell' will be drawn last (ie on top)
    // NOTE: most Mariners' Object land on the "fast" layer 9
    // But 'pastrk' (and other) are drawn on layer < 9.
    // So MARINER_CELL must be checked for all chart for
    // object on layer 0-8 during draw()
    _marinerCell = _addCell(MARINER_CELL);
    // set extent to max
    _marinerCell->ext.S = -INFINITY;
    _marinerCell->ext.W = -INFINITY;
    _marinerCell->ext.N =  INFINITY;
    _marinerCell->ext.E =  INFINITY;




    ///////////////////////////////////////////////////////////
    // init experimental stuff
    //

#ifdef S52_USE_DBUS
    int _initDBus();
    _initDBus();
#endif

#ifdef S52_USE_SOCK
    int _initSock();
    _initSock();
#endif

#ifdef S52_USE_PIPE
    _pipeWatch(NULL);
#endif

    _timer =  g_timer_new();

    _doInit = FALSE;


#ifdef S52_USE_WORLD
    { // load world shapefile
        valueBuf chartPath = {'\0'};
        if (0 == S52_getConfig(CONF_WORLD, &chartPath)) {
            PRINTF("WORLD file not found!\n");
            return FALSE;
        }
        S52_loadLayer_cb  loadLayer_cb  = S52_loadLayer;
        S52_loadObject_cb loadObject_cb = S52_loadObject;
        _loadBaseCell((char *)chartPath, loadLayer_cb, loadObject_cb);
        _initPROJ();
        _projectCells();
    }
#endif

    PRINTF("S52_INIT() .. DONE\n");

    return TRUE;
}

DLL
const char* STD S52_version(void)
{
    PRINTF("%s", _version);

    return _version;
}

static S52_obj   *_delObj(S52_obj *obj); // foward ref
DLL int    STD    _freeCell(_cell *c)
{
    unsigned int j,k;

    S52_CHECK_INIT;

    if (NULL == _cellList) {
        PRINTF("WARNING: no cell\n");
        return TRUE;
    }

    // useless
    //if (NULL == _removeCell(c)) {
    //    PRINTF("ERROR: cell to remove not found!\n");
    //    return FALSE;
    //}

    if (NULL != c->filename)
        g_string_free(c->filename, TRUE);

    for (j=0; j<S52_PRIO_NUM; ++j) {
        for (k=0; k<N_OBJ_T; ++k) {
            //unsigned int idx;
            GPtrArray *rbin = c->renderBin[j][k];

            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                obj = _delObj(obj);
            }
            g_ptr_array_free(rbin, TRUE);
            //g_ptr_array_unref(rbin);
        }
    }

    S52_CS_done(c->local);

    if (NULL != c->lights_sector) {
        //unsigned int idx;

        for (guint idx=0; idx<c->lights_sector->len; ++idx) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, idx);

            obj = _delObj(obj);
        }
        g_ptr_array_free(c->lights_sector, TRUE);
        //g_ptr_array_unref(c->lights_sector);
    }

    if (NULL != c->textList) {
        g_ptr_array_free(c->textList, TRUE);
        //g_ptr_array_unref(_textList);
        c->textList = NULL;
    }

    if (NULL != c->objList_supp) {
        g_ptr_array_free(c->objList_supp, TRUE);
        //g_ptr_array_unref(_objList_supp);
        c->objList_supp = NULL;
    }

    if (NULL != c->objList_over) {
        g_ptr_array_free(c->objList_over, TRUE);
        //g_ptr_array_unref(_objList_over);
        c->objList_over = NULL;
    }

    if (NULL != c->objClassList)
        g_string_free(c->objClassList, TRUE);

    g_free(c);

    //if (NULL == c->lines)
    //    g_tree_destroy(c->lines);

    return TRUE;
}

DLL int    STD S52_done(void)
// clear all --shutdown
{
    //unsigned int i;

    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    if (NULL != _cellList) {
        for (guint i=0; i<_cellList->len; ++i) {
            //_cell *c = &g_array_index(_cellList, _cell, i);
            _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
            _freeCell(c);
        }
        g_ptr_array_free(_cellList, TRUE);
        //g_ptr_array_unref(_cellList);
        _cellList = NULL;
    }
    _marinerCell = NULL;

    /*
    if (NULL != _textList) {
        g_ptr_array_free(_textList, TRUE);
        //g_ptr_array_unref(_textList);
        _textList = NULL;
    }

    if (NULL != _objList_supp) {
        g_ptr_array_free(_objList_supp, TRUE);
        //g_ptr_array_unref(_objList_supp);
        _objList_supp = NULL;
    }

    if (NULL != _objList_over) {
        g_ptr_array_free(_objList_over, TRUE);
        //g_ptr_array_unref(_objList_over);
        _objList_over = NULL;
    }
    */

//#ifdef S52_USE_SUPP_LINE_OVERLAP
//    if (NULL != _lines) {
//        //g_ptr_array_free(_lines, TRUE);
//        g_ptr_array_unref(_lines);
//        _lines = NULL;
//    }
//#endif

    S52_GL_done();
    S52_PL_done();

    S57_donePROJ();

    _intl   = NULL;

    //g_mem_profile();


    S52_doneLog();

    g_timer_destroy(_timer);
    _timer = NULL;

    //g_ptr_array_free(_route,  FALSE);
    //_route  = NULL;
    //g_ptr_array_free(_wholin, FALSE);
    //_wholin = NULL;

    _doInit = FALSE;

#ifdef S52_USE_DBUS
    dbus_connection_unref(_dbus);
    _dbus = NULL;
#endif

    g_string_free(_plibNameList, TRUE); _plibNameList = NULL;
    g_string_free(_paltNameList, TRUE); _paltNameList = NULL;
    g_string_free(_cellNameList, TRUE); _cellNameList = NULL;
    g_string_free(_objClassList, TRUE); _objClassList = NULL;

    g_ptr_array_free(_objToDelList, TRUE); _objToDelList = NULL;

    g_static_mutex_unlock(&_mp_mutex);

    PRINTF("libS52 done\n");

    return TRUE;
}

#if 0
/*  DEPRECATED
DLL int    STD S52_setFont(int font)
{
    S52_CHECK_INIT;

    S52_GL_setFontDL(font);

    return font;
}
*/
#endif

//#ifdef S52_USE_SUPP_LINE_OVERLAP
//static gint       _cmpLINES(gconstpointer a, gconstpointer b)
//{
//    S52_diPrio prioA = S52_PL_getDPRI((S52_obj*)a);
//    S52_diPrio prioB = S52_PL_getDPRI((S52_obj*)b);
//
//    if (prioA < prioB) return -1;
//    if (prioA > prioB) return  1;
//
//    return 0;
//}
//#endif

//static gint       _cmpEdgeID(gconstpointer a, gconstpointer b)
//{
//    GString *olnam = S57_getAttVal((S57_geo *)a, "LNAM");
//
//    if (prioA < prioB) return -1;
//    if (prioA > prioB) return  1;
//
//    return 0;
//}

//DLL int    STD S52_newMercPrj(double latitude)
#ifdef S52_USE_SUPP_LINE_OVERLAP

#if 0
static GArray    *_parseIntList(GString *intstr)
{
    GArray *garray = g_array_new(FALSE, FALSE, sizeof(gint));
    gchar **split  = g_strsplit_set(intstr->str+1, "():,", 0);
    gchar **head   = split;

    //printf("XXX %s\n", intstr->str);

    int i = 0;
    int n = atoi(*split++);
    for (i=0; i<n; ++i) {
        const char *str = *split++;
        if (NULL != str) {
            gint        gi  =  atoi(str);
            g_array_append_val(garray, gi);
        } else {
            // FIXME: for some reason some string end with ",...)" !!
            PRINTF("buffer overflow in GDAL increase TEMP_BUFFER_SIZE (currently 1024) in ogr/ogrfeature.cpp:994\n");
            g_assert(0);
            break;
        }
    }
    g_strfreev(head);

    return garray;
}
#endif

static int        _suppLineOverlap()
// no SUPP in case manual chart corection (LC(CHCRIDnn) and LC(CHCRDELn))
{
    int prio;
    int obj_t;

    return_if_null(_crntCell->Edges);
    return_if_null(_crntCell->ConnectedNodes);

    // assume that there is nothing on layer S52_PRIO_NODATA
    for (prio=S52_PRIO_MARINR; prio>S52_PRIO_NODATA; --prio) {
        for (obj_t=S52_LINES; obj_t>S52__META; --obj_t) {
            //GPtrArray *rbin = _crntCell->renderBin[prio][S52_AREAS_T];
            GPtrArray *rbin = _crntCell->renderBin[prio][obj_t];
            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                // one object
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                // get edge ID list
                S57_geo *geo = S52_PL_getGeo(obj);

                GString *name_rcnmstr = S57_getAttVal(geo, "NAME_RCNM");
                GString *name_rcidstr = S57_getAttVal(geo, "NAME_RCID");

                if ((NULL==name_rcnmstr) || (NULL==name_rcidstr))
                    break;

                {   // check for substring ",...)" if ti can be found at the end
                    // this mean that TEMP_BUFFER_SIZE in OGR is not large anought.
                    const gchar *substr = ",...)";
                    gchar       *found1 = g_strrstr(name_rcnmstr->str, substr);
                    gchar       *found2 = g_strrstr(name_rcidstr->str, substr);
                    if (NULL!=found1 || NULL!=found2) {
                        PRINTF("ERROR: OGR buffer TEMP_BUFFER_SIZE in ogr/ogrfeature.cpp:994 overflow\n");
                        g_assert(0);
                    }
                }


                // FIXME: warn if a line need masking
                // FIXME: add info to find out what this mask mask
                //MASK (IntegerList) = (7:255,255,255,255,255,255,255)
                //GString *maskstr = S57_getAttVal(geo, "MASK");
                //if (NULL != maskstr) {
                //    PRINTF("INFO: MASK found [%s]\n", maskstr->str);
                //}



                // take only Edge (ie rcnm == 130 (Edge type))
                //GArray *intLrcnm = _parseIntList(name_rcnmstr);
                //GArray *intLrcid = _parseIntList(name_rcidstr);
                gchar **splitrcnm  = g_strsplit_set(name_rcnmstr->str+1, "():,", 0);
                gchar **splitrcid  = g_strsplit_set(name_rcidstr->str+1, "():,", 0);
                gchar **toprcnm    = splitrcnm;
                gchar **toprcid    = splitrcid;


                // the first string is the lenght
                guint n = atoi(*splitrcnm);
                guint i = 0;
                for (i=0; i<n; ++splitrcnm, ++i) {
                    if (NULL == *splitrcnm) {
                        PRINTF("ERROR: *splitrcnm\n");
                        g_assert(0);
                    }
                }
                n = atoi(*splitrcid);
                for (i=0; i<n; ++splitrcid, ++i) {
                    if (NULL == *splitrcid) {
                        PRINTF("ERROR: *splitrcid\n");
                        g_assert(0);
                    }
                }

                splitrcnm = toprcnm;
                splitrcid = toprcid;
                splitrcnm++;
                splitrcid++;

                // FIXME: because some name_rcidstr are imcomplet (ending with ",...)" when OGR
                // temp buffer is not large anough)
                // see ogr/ogrfeature.cpp:994
                // #define TEMP_BUFFER_SIZE 1024
                // some intLrcid are shorter than intLrcnm
                //if (intLrcnm->len != intLrcid->len)
                //    g_assert(0);

                //NAME_RCNM (IntegerList) = (1:130)
                //NAME_RCID (IntegerList) = (1:72)
                //for (i=0; NULL!=str
                // for all rcnm == 130
                //   take rcid
                //   get Edge with rcid
                //   make Edge to point to geo if null
                //   else make geo coord z==-1 for all vertex in Egde that are found in geo

                //gint *rcnm;
                //unsigned int i;
                //for (i=0; i<intLrcnm->len; ++i) {
                for (i=0; NULL!=*splitrcnm; splitrcnm++) {
                    //for (i=0; i<intLrcid->len; ++i) {
                    //rcnm = &g_array_index(intLrcnm, gint, i);

                    // the S57 name for Edge (130)
                    //if (0 == g_strcmp0(*splitrcnm, "130")) {
                    if (0 == S52_strncmp(*splitrcnm, "130", 3)) {
                        //guint j    = 0;
                        //gint *rcid = &g_array_index(intLrcid, gint, i);
                        for (guint j=0; j<_crntCell->Edges->len; ++j) {
                            S57_geo *geoEdge     = (S57_geo *)g_ptr_array_index(_crntCell->Edges, j);

                            // FIXME: optimisation: save in rcid in geoEdge
                            //GString *rcidEdgestr = S57_getAttVal(geoEdge, "RCID");
                            //gint     rcidEdge    = (NULL == rcidEdgestr) ? 0 : atoi(rcidEdgestr->str);
                            GString *rcidstr = S57_getRCIDstr(geoEdge);

                            if (0 == S52_strncmp(rcidstr->str, *splitrcid, MAX(strlen(rcidstr->str), strlen(*splitrcid)))) {
                                // debug
                                //if (100 == rcidEdge) {
                                //    PRINTF(" edge id 100\n");
                                //}

                                if (NULL == S57_getGeoLink(geoEdge)) {
                                    //geoEdge->next = geo;
                                    //S57_setGeoNext(geoEdge, geo);
                                    S57_setGeoLink(geoEdge, geo);
                                } else {
                                    //PRINTF("DEBUG: edge overlap found on %s ID:%i\n", S57_getName(geo), S57_getGeoID(geo));
                                    S57_markOverlapGeo(geo, geoEdge);
                                }
                                // edge found, in S57 a geometry can't have the
                                // same edge twice, hence bailout because this edge will not
                                // apear again
                                break;
                            }
                        }
                    }
                    splitrcid++;
                }
                //g_array_free(intLrcnm, TRUE);
                //g_array_free(intLrcid, TRUE);
                g_strfreev(toprcnm);
                g_strfreev(toprcid);

            }
        }
    }

    {
        int quiet = TRUE;
        // flush OGR primitive geo
        //g_ptr_array_foreach(_crntCell->Edges,          (GFunc)S57_doneData, NULL);
        //g_ptr_array_foreach(_crntCell->ConnectedNodes, (GFunc)S57_doneData, NULL);
        g_ptr_array_foreach(_crntCell->Edges,          (GFunc)S57_doneData, &quiet);
        g_ptr_array_foreach(_crntCell->ConnectedNodes, (GFunc)S57_doneData, &quiet);
    }

    g_ptr_array_free(_crntCell->Edges,          TRUE);
    g_ptr_array_free(_crntCell->ConnectedNodes, TRUE);
    //g_array_free(_crntCell->ConnectedNodes, TRUE);

    _crntCell->Edges          = NULL;
    _crntCell->ConnectedNodes = NULL;
    _crntCell->baseEdgeRCID   = 0;

    //PRINTF("done!\n");

    return TRUE;
}
#endif


static _cell*     _loadBaseCell(char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb)
// FIXME: MUTEX
{
    _cell   *ch = NULL;
    FILE    *fd = NULL;

    // OGR doesn't strip blank but S57 filename can't have any
    // maybe this is to allow POSIX naming (!!)
    filename = g_strstrip(filename);

    // skip file not terminated by .000
    //const char *base = g_basename(filename);
    const char *base = g_path_get_basename(filename);
    if (0 != S52_strncmp(base+8, ".000", 4)) {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated]\n", filename);
        //return NULL;
    }



    // G_OS_WIN32, G_OS_UNIX
    // convert paths to unix style
    //for (int i = 0; i < strlen(line); i++) {
    //    // replace dos dir separator character
    //    if (line[i] == '\\')
    //        line[i]='/'; // G_DIR_SEPARATOR
    //}


    if (NULL == (fd = S52_fopen(filename, "r"))) {
        PRINTF("WARNING: cell not found (%s)\n", filename);

        return NULL;
    }

    ch = _addCell(filename);
    if (NULL == ch) {
        PRINTF("WARNING: _addCell() failed\n");

        //g_assert(0);
        return NULL;
    }

    //if (NULL == cb) {
    //    PRINTF("NOTE: using default S52_loadLayer() callback\n");
    //    cb = S52_loadLayer;
    //}

#ifdef S52_USE_GV
    //S57_gvLoadCell (filename, (S52_loadLayer_cb) cb);
    S57_gvLoadCell (filename, layer_cb);
#else
    S57_ogrLoadCell(filename, loadLayer_cb, loadObject_cb);
    //S57_ogrLoadCell(filename, NULL);
#endif

    // FIXME: resolve heightdatum correction here!
    // FIX: go trouht all layer that have to look for
    // VERCSA, VERCLR, VERCCL, VERCOP
    // ...

#ifdef S52_USE_SUPP_LINE_OVERLAP
    PRINTF("DEBUG: resolving line overlap for cell: %s ...\n", filename);

    _suppLineOverlap();
#endif

    S52_fclose(fd);

    return ch;
}

#ifdef S52_USE_OGR_FILECOLLECTOR
// in libgdal.so
// Note: must add 'extern "C"' to GDAL/OGR at S57.h:40
extern char   **S57FileCollector( const char *pszDataset );

//#include "iso8211.h"

static int        _loadCATALOG(char *filename)
{
    FILE *fd = NULL;
    filename = g_strstrip(filename);

    if (NULL == (fd = S52_fopen(filename, "r"))) {
        PRINTF("ERROR: CATALOG not found (%s)\n", filename);

        return FALSE;
    }

    /*
    DDFModule  oModule;
    DDFRecord *poRecord;
    if ( !oModule.Open(filename)) {
        return NULL;
    }

    poRecord = oModule.ReadRecord();
    if (NULL == poRecord)
        return NULL;
    */

    /*
     Field CATD: Catalog Directory field
        RCNM = `CD'
        RCID = 8
        FILE = `GBCHAINS.TXT'
        LFIL = `'
        VOLM = `V01X01'
        IMPL = `BIN'
        SLAT = -32.566667
        WLON = 60.866667
        NLAT = -32.500000
        ELON = 60.966667
        CRCS = `F25B3353'
        COMT = `'
    */

    /*
    for ( ; poRecord != NULL; poRecord = oModule.ReadRecord()) {
        if (NULL != poRecord->FindField("CATD")) {

            const char *impl = poRecord->GetStringSubfield("CATD", 0, "IMPL", 0);
            //SLAT = -32.566667
            //WLON =  60.866667
            //NLAT = -32.500000
            //ELON =  60.966667

            const char *file = poRecord->GetStringSubfield("CATD",0,"FILE",0);

            // mnufea
        }
    }
    */

    //S57_ogrLoadCell(filename, _catalogLayer);

    return TRUE;
}
#endif

DLL int    STD S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb);

//DLL int    STD S52_loadCell(const char *encPath, S52_loadLayer_cb layer_cb)
DLL int    STD S52_loadCell(const char *encPath, S52_loadObject_cb loadObject_cb)
{
    valueBuf chartPath = {'\0'};
    char    *fname     = NULL;
    _cell   *ch        = NULL;
    static int  silent = FALSE;

    S52_CHECK_INIT;
    S52_CHECK_MUTX;   // can't load 2 sets of charte at once

    S52_loadLayer_cb loadLayer_cb = S52_loadLayer;
    //if (NULL == layer_cb) {
    //    if (FALSE == silent) {
    //        PRINTF("NOTE: using default S52_loadLayer() callback\n");
    //        PRINTF("       (this msg will not repeat)\n");
    //        silent = TRUE;
    //    }
    //    layer_cb = S52_loadLayer;
    //}

    if (NULL == loadObject_cb) {
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("       (this msg will not repeat)\n");
            silent = TRUE;
        }
        loadObject_cb = S52_loadObject;
    }

#ifdef _MINGW
    // on Windows 32 the callback is broken
    loadObject_cb = S52_loadObject;
#endif

    if (NULL == encPath) {
        if (0 == S52_getConfig(CONF_CHART, &chartPath)) {
            PRINTF("S57 file not found!\n");
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }
        fname = g_strdup(chartPath);
    } else
        fname = g_strdup(encPath);

    fname = g_strstrip(fname);

    if (TRUE != g_file_test(fname, (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
        PRINTF("S57 file or DIR not found (%s)\n", fname);
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }

#ifdef S52_USE_OGR_FILECOLLECTOR
    {

        //const char *base = g_basename(fname);
        //if (0 != g_strcmp0(base+8, "CATALOG.03")) {
        //    // cell extend - build cell index
        //    _loadCATALOG(fname);
        //}
        //g_chdir("ENC_ROOT");
        //char **encList = S57FileCollector("CATALOG.031");


        char **encList = S57FileCollector(fname);
        if (NULL == encList) {
            PRINTF("WARNING: S57FileCollector(%s) return NULL\n", fname);
            g_free(fname);
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        } else {
            guint i = 0;

            for (i=0; NULL!=encList[i]; ++i) {
                char *encName = encList[i];

                // HACK: g_mem_profile() break the call to S57FileCollector()
                // it return 0x1 instead of 0x0 at the end of encList
                if (1 == GPOINTER_TO_INT(encName))
                    break;

                //ch = _loadBaseCell(encName, layer_cb);
                ch = _loadBaseCell(encName, loadLayer_cb, loadObject_cb);
                g_free(encName);
            }
            g_free(encList);
        }
    }
    g_free(fname);

    if (NULL == ch) {
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }

#else
    //ch = _loadBaseCell(fname, layer_cb);
    ch = _loadBaseCell(fname, loadLayer_cb, loadObject_cb);
    g_free(fname);

    if (NULL == ch) {
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }
#endif

#ifdef S52_USE_PROJ
    {
        _mercPrjSet = _initPROJ();
        _projectCells();

        //g_static_mutex_unlock(&_mp_mutex);
        //S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);
        //S52_CHECK_MUTX;   // can't load 2 sets of charte at once
    }
#endif


    // setup object used by CS
    {
        //unsigned int i,j,k;
        for (guint k=0; k<_cellList->len; ++k) {
            //_cell *c = &g_array_index(_cellList, _cell, k);
            _cell *c = (_cell*) g_ptr_array_index(_cellList, k);
            for (guint i=0; i<S52_PRIO_NUM; ++i) {
                for (guint j=0; j<N_OBJ_T; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        // debug
                        //if (== S57_getGeoID(geo)) {
                        //    PRINTF("found\n");
                        //    //S57_dumpData(geo);
                        //}

                        S52_CS_touch(c->local, geo);

                        // prepare DEPARE/DRGARE
                        //if ( (0==S52_strncmp(name, "DEPARE", 6)) &&
                        //    AREAS_T == S57_getObjtype(geo)) {
                        //
                        //    S52_GL_tess(geo);
                        //}
                        //if (2186==S57_getGeoID(geo)) {
                        //    S57_dumpData(geo);
                        //}

                    }
                }
            }

            // then process lights_sector
            if (NULL != c->lights_sector) {
                //unsigned int i = 0;
                for (guint i=0; i<c->lights_sector->len; ++i) {
                    S52_obj *obj  = (S52_obj *)g_ptr_array_index(c->lights_sector, i);
                    S57_geo *geo  = S52_PL_getGeo(obj);

                    // debug
                    //if (1555 == S57_getGeoID(geo)) {
                    //    PRINTF("lights found\n");
                    //}

                    S52_CS_touch(c->local, geo);
                }
            }
        }
    }


    // need to do a _resolveCS() at the next _app()
    _doCS = TRUE;

    // _app() specific to sector light
    _doCullLights = TRUE;

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_doneCell(const char *encPath)
{
    //valueBuf chartPath = {'\0'};
    //char    *fname     = NULL;
    //_cell   *ch        = NULL;
    //static int  silent = FALSE;

    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // FIXME: the (futur) chart manager (CM) should to this by itself
    // so loadCell would load a CATALOG then CM would load individual cell
    // to fill the view (and unload cell outside the view)

    gchar *base = g_path_get_basename(encPath);
    base = g_strstrip(base);

    // skip file not terminated by .000
    //const char *base = g_basename(filename);
    //const char *base = g_path_get_basename(fname);
    if (0 != g_strcmp0(base+8, ".000")) {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated]\n", encPath);
        g_free(base);
        g_static_mutex_unlock(&_mp_mutex);

        return FALSE;
    }

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        // check if allready loaded
        if (0 == S52_strncmp(base, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
            _freeCell(c);
            g_ptr_array_remove_index(_cellList, idx);
            break;
        }
    }

    g_free(base);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
static int        _loadEdge(const char *name, void *Edge)
{
    if ( (NULL==name) || (NULL==Edge)) {
        PRINTF("ERROR: objname / shape  --> NULL\n");
        return FALSE;
    }

    S57_geo *geoData = S57_ogrLoadObject(name, (void*)Edge);
    if (NULL == geoData)
        return FALSE;

    // add to this cell (crntCell)
    if (NULL == _crntCell->Edges)
        _crntCell->Edges = g_ptr_array_new();

    {
        guint   npt    = 0;
        double *ppt    = NULL;
        double *ppttmp = NULL;

        guint   npt_new   = 0;
        double *ppt_new   = NULL;
        double *ppt_tmp   = NULL;

        S57_getGeoData(geoData, 0, &npt, &ppt);

        npt_new = 2 + npt;
        ppt_tmp = ppt_new = g_new(double, npt_new*3);

        GString *name_rcid_0str = S57_getAttVal(geoData, "NAME_RCID_0");
        guint    name_rcid_0    = (NULL == name_rcid_0str) ? 1 : atoi(name_rcid_0str->str);
        GString *name_rcid_1str = S57_getAttVal(geoData, "NAME_RCID_1");
        guint    name_rcid_1    = (NULL == name_rcid_1str) ? 1 : atoi(name_rcid_1str->str);

        // debug
        if (NULL==name_rcid_0str || NULL==name_rcid_1str) {
            PRINTF("ERROR: Edge with no end point\n");
            g_assert(0);
        }

        guint   npt_0 = 0;
        double *ppt_0 = NULL;

        if ((name_rcid_0 - _crntCell->baseEdgeRCID) > _crntCell->ConnectedNodes->len) {
            PRINTF("ERROR: Edge end point 0 (%s) and ConnectedNodes array lenght mismatch\n",
                   name_rcid_0str->str);
            g_assert(0);
        }

        //S57_geo *node_0 = g_array_index(_crntCell->ConnectedNodes, S57_geo*, name_rcid_0);
        //S57_geo *node_0 =  (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, name_rcid_0 - 1);
        S57_geo *node_0 =  (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, (name_rcid_0 - _crntCell->baseEdgeRCID));
        if (NULL == node_0) {
            PRINTF("ERROR: got empty node_0 at name_rcid_0 = %i\n", name_rcid_0);
            g_assert(0);
        }

        S57_getGeoData(node_0, 0, &npt_0, &ppt_0);

        guint   npt_1   = 0;
        double *ppt_1   = NULL;

        //if ((name_rcid_1-1)>_crntCell->ConnectedNodes->len) {
        if ((name_rcid_1 - _crntCell->baseEdgeRCID) > _crntCell->ConnectedNodes->len) {
            PRINTF("ERROR: Edge end point 1 (%s) and ConnectedNodes array lenght mismatch\n",
                   name_rcid_1str->str);
            g_assert(0);
        }

        //S57_geo *node_1 = g_array_index(_crntCell->ConnectedNodes, S57_geo*, name_rcid_1);
        //S57_geo *node_1 = (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, name_rcid_1 - 1);
        S57_geo *node_1 =  (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, (name_rcid_1 - _crntCell->baseEdgeRCID));
        if (NULL == node_1) {
            // if we land here it meen that there no ConnectedNodes at this index
            // ptr_array has hold (NULL) because of S57 update
            PRINTF("ERROR: got empty node_1 at name_rcid_1 = %i\n", name_rcid_1);
            g_assert(0);
        }

        S57_getGeoData(node_1, 0, &npt_1, &ppt_1);

        {   // check that index are in sync with rcid
            // check assumion that ConnectedNodes are continuous
            // FIXME: when applying update this is no longer true (ie continuous)
            GString *rcid_0str = S57_getAttVal(node_0, "RCID");
            guint    rcid_0    = (NULL == rcid_0str) ? 1 : atoi(rcid_0str->str);
            GString *rcid_1str = S57_getAttVal(node_1, "RCID");
            guint    rcid_1    = (NULL == rcid_1str) ? 1 : atoi(rcid_1str->str);

            if (name_rcid_0 != rcid_0) {
                PRINTF("ERROR: name_rcid_0 mismatch\n");
                g_assert(0);
            }
            if (name_rcid_1 != rcid_1) {
                PRINTF("ERROR: name_rcid_1 mismatch\n");
                g_assert(0);
            }
        }

        ppt_new[0] = ppt_0[0];
        ppt_new[1] = ppt_0[1];
        ppt_new[(npt_new-1)*3 + 0] = ppt_1[0];
        ppt_new[(npt_new-1)*3 + 1] = ppt_1[1];

        ppttmp = ppt;
        ppt_tmp += 3;
        guint i = 0;
        for (i=0; i<npt*3; ++i)
            *ppt_tmp++ = *ppttmp++;

        if (NULL != ppt)
            g_free(ppt);


        S57_setGeoLine(geoData, npt_new, ppt_new);
    }

    g_ptr_array_add(_crntCell->Edges, geoData);

    // debug
    //PRINTF("%X len:%i\n", _crntCell->Edges->pdata, _crntCell->Edges->len);
    //PRINTF("XXX %s\n", S57_getName(geoData));

    return TRUE;
}

static int        _loadConnectedNode(const char *name, void *ConnectedNode)
{
    if ( (NULL==name) || (NULL==ConnectedNode)) {
        PRINTF("ERROR: objname / shape  --> NULL\n");
        return FALSE;
    }

    S57_geo *geoData = S57_ogrLoadObject(name, (void*)ConnectedNode);
    if (NULL == geoData) {
        PRINTF("OGR fail to load object: %s\n", name);
        g_assert(0);
        return FALSE;
    }

    // add to this cell (crntCell)
    if (NULL == _crntCell->ConnectedNodes) {
        _crntCell->ConnectedNodes = g_ptr_array_new();
    }

    {
        GString *rcidstr = S57_getAttVal(geoData, "RCID");
        guint    rcid    = (NULL == rcidstr) ? 0 : atoi(rcidstr->str);


        // FIXME: what if baseEdgeRCID is not 0!
        // set_size is over grown
        if (rcid > _crntCell->ConnectedNodes->len) {
            g_ptr_array_set_size(_crntCell->ConnectedNodes, rcid);
            //g_assert(0);
        }

        if (0 == _crntCell->baseEdgeRCID) {
            _crntCell->baseEdgeRCID = rcid;
        }

        //S57_geo *geoTmp = (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, rcid-1);
        //geoTmp = geoData;
        //_crntCell->ConnectedNodes->pdata[rcid-1] = geoData;


        // FIXME: check bound
        _crntCell->ConnectedNodes->pdata[rcid - _crntCell->baseEdgeRCID] = geoData;
    }

    //g_ptr_array_add(_crntCell->ConnectedNodes, geoData);

    // if this is the first connectednodes get is offset
    // the offset is 1, but in certain case (like update) it can move
    // assume connectednodes continuous
    /*
    if (0 == _crntCell->baseEdgeRCID) {
        GString *rcidstr = S57_getAttVal(geoData, "RCID");
        _crntCell->baseEdgeRCID = (NULL == rcidstr) ? 0 : atoi(rcidstr->str);
    }
    */
    return TRUE;
}
#endif

DLL int    STD S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb)
{
    S52_CHECK_INIT;

    static int  silent  = FALSE;

#ifdef S52_USE_GV
    // init w/ dummy cell name --we get here from OpenEV now (!?)
    if (NULL == _crntCell)
        _addCell("dummy");
#endif

    if ( (NULL==layername) || (NULL==layer) || (NULL==_cellList)) {
        PRINTF("ERROR: layername / ogrlayer / _cellList --> NULL\n");
        return FALSE;
    }

    PRINTF("LOADING LAYER NAME: %s\n", layername);

#ifdef S52_USE_SUPP_LINE_OVERLAP
    // --- trap primitive ---
    // regeject unused low level primitive
    //if (0==g_strcmp0(layername, "IsolatedNode"))
    if (0==S52_strncmp(layername, "IsolatedNode", 12))
        return TRUE;

    //if (0==g_strcmp0(layername, "ConnectedNode"))
    //    return TRUE;

    //if (0==g_strcmp0(layername, "Face"))
    if (0==S52_strncmp(layername, "Face", 4))
        return TRUE;

    // Edge is use to resolve overlapping line
    //if (0==g_strcmp0(layername, "Edge")) {
    if (0==S52_strncmp(layername, "Edge", 4)) {
        S57_ogrLoadLayer(layername, layer, _loadEdge);
        return TRUE;
    }
    // Edge is use to resolve overlapping line
    //if (0==g_strcmp0(layername, "ConnectedNode")) {
    if (0==S52_strncmp(layername, "ConnectedNode", 13)) {
        S57_ogrLoadLayer(layername, layer, _loadConnectedNode);
        return TRUE;
    }
#endif

    // debug: too slow for lake superior
    // FIXME
    //if (0== g_strcmp0(layername, "OBSTRN", 6))
    //    return 1;
    //if (0==g_strcmp0(layername, "UWTROC"))
    //    return 1;

    // GDAL load a DSID layer
    //if (0== g_strcmp0(layername, "DSID", 4)) {
    //    PRINTF("skipping layer name: %s\n", layername);
    //    return 1;
    //}

    if (NULL == loadObject_cb) {
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("       (this msg will not repeat)\n");
            silent = TRUE;
        }
        loadObject_cb = S52_loadObject;
    }

    // save S57 object name
    if (0 != _crntCell->objClassList->len)
        g_string_append(_crntCell->objClassList, ",");

    g_string_append(_crntCell->objClassList, layername);


#ifdef S52_USE_GV
    //S57_gvLoadLayer (layername, layer, S52_loadObject);
    S57_gvLoadLayer (layername, layer, loadObject_cb);
#else
    //S57_ogrLoadLayer(layername, layer, S52_loadObject);
    S57_ogrLoadLayer(layername, layer, loadObject_cb);
#endif

    return TRUE;
}

static int        _insertLightSec(_cell *c, S52_obj *obj)
// return TRUE if this obj is a light sector
// as this object need special prossesing
{
    // BUG: if lights sector is on 2 chart of different scale
    // POSSIBLE FIX: check serial number

    // keep a reference to lights sector apart from other object
    // because it need different culling rules
    if (0==g_strcmp0(S52_PL_getOBCL(obj), "LIGHTS")) {
        S57_geo *geo       = S52_PL_getGeo(obj);
        GString *sectr1str = S57_getAttVal(geo, "SECTR1");
        GString *sectr2str = S57_getAttVal(geo, "SECTR2");

        if (NULL!=sectr1str || NULL!=sectr2str) {
            // moved from _addCell() - create array, only if needed
            if (NULL == c->lights_sector)
                c->lights_sector = g_ptr_array_new();

            g_ptr_array_add(c->lights_sector, obj);

            // go it - bailout
            return TRUE;
        }
    }

    return FALSE;
}

//static S52_obj   *_cellAddGeo(_cell *c, S57_geo *geoData)
static S52_obj   *_insertS57Obj(_cell *c, S57_geo *geoData)
// insert a S52_obj in a cell from a S57_obj
// return the new S52_obj
{
    int         obj_t;
    S52_Obj_t   ot         = S57_getObjtype(geoData);
    S52_obj    *obj        = S52_PL_newObj(geoData);
    S52_disPrio disPrioIdx = S52_PL_getDPRI(obj);

    if (NULL == obj) {
        PRINTF("WARNING: S52 object build failed\n");
        return FALSE;
    }
    if (NULL == c) {
        PRINTF("WARNING: no cell to add to\n");
        return FALSE;
    }


    // connect S52ObjectType (public enum) to S57 object type (private)
    switch (ot) {
        case _META_T: obj_t = S52__META; break; // meta geo stuff (ex: C_AGGR)
        case AREAS_T: obj_t = S52_AREAS; break;
        case LINES_T: obj_t = S52_LINES; break;
        case POINT_T: obj_t = S52_POINT; break;
        default:
            PRINTF("WARNING: unknown index of addressed object type\n");
            g_assert(0);
    }

    // special prossesing for light sector
    if (FALSE == _insertLightSec(c, obj)) {
        // insert normal object (ie not a light with sector)
        g_ptr_array_add(c->renderBin[disPrioIdx][obj_t], obj);
    }

#ifdef S52_USE_WORLD
    {
        S57_geo *geoDataNext = NULL;
        if (NULL != (geoDataNext = S57_getNextPoly(geoData))) {
            // recurssion
            _insertS57Obj(c, geoDataNext);
        }
    }
#endif

    return obj;
}

static S52_obj   *_insertS52Obj(_cell *c, S52_obj *obj)
// inster 'obj' in cell 'c'
{
    S57_geo    *geo        = S52_PL_getGeo(obj);
    S52_disPrio disPrioIdx = S52_PL_getDPRI(obj);
    //S57_Obj_t   ot         = S57_getObjtype(geo);
    S52_Obj_t   ot         = S57_getObjtype(geo);
    int         obj_t      = N_OBJ_T;

    // connect S52ObjectType (public enum) to S57 object type (private)
    switch (ot) {
        case _META_T: obj_t = S52__META; break; // meta geo stuff (ex: C_AGGR)
        case AREAS_T: obj_t = S52_AREAS; break;
        case LINES_T: obj_t = S52_LINES; break;
        case POINT_T: obj_t = S52_POINT; break;
        default:
            PRINTF("ERROR: unknown index of addressed object type\n");
            g_assert(0);
    }

    // special prossesing for light sector
    if (FALSE == _insertLightSec(c, obj)) {
        // insert normal object (ie not a light with sector)
        g_ptr_array_add(c->renderBin[disPrioIdx][obj_t], obj);
    }

    return obj;
}

static S52_obj   *_isObjValid(_cell *c, S52_obj *obj)
// return  obj if the oject is in cell else NULL
// Used to validate User Mariners' Object
{
    /*

     // NOP, this doesn't work as the obj could be dandling
     // if an AIS as expired (and deleted) the client obj handle
     // is now invalid

    return_if_null(c);
    return_if_null(obj);

    S57_geo    *geo        = S52_PL_getGeo(obj);
    S52_disPrio disPrioIdx = S52_PL_getDPRI(obj);
    S52_Obj_t   ot         = S57_getObjtype(geo);
    int         obj_t      = N_OBJ_T;

    // connect S52ObjectType (public enum) to S57 object type (private)
    switch (ot) {
        case _META_T: obj_t = S52__META; break; // meta geo stuff (ex: C_AGGR)
        case AREAS_T: obj_t = S52_AREAS; break;
        case LINES_T: obj_t = S52_LINES; break;
        case POINT_T: obj_t = S52_POINT; break;
        default:
            PRINTF("ERROR: unknown index of addressed object type\n");
            g_assert(0);
    }

    GPtrArray *rbin = c->renderBin[disPrioIdx][obj_t];
    unsigned int idx;
    for (guint idx=0; idx<rbin->len; ++idx) {
        S52_obj *o = (S52_obj *)g_ptr_array_index(rbin, idx);

        if (obj == o) {
            return obj;
        }
    }
    */

    //*
    int i;
    for (i=0; i<S52_PRIO_NUM; ++i) {
        int j;
        for (j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = c->renderBin[i][j];
            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *o = (S52_obj *)g_ptr_array_index(rbin, idx);

                if (obj == o) {
                    return obj;
                }
            }
        }
    }
    //*/

    PRINTF("WARNING: object handle not found\n");

    return NULL;
}

static S52_obj   *_removeObj(_cell *c, S52_obj *obj)
// remove the S52 object from the cell (not the object itself)
// return the oject removed, else return NULL if object not found
{
    // NOTE:cannot find the right renderBin from 'obj'
    // because 'obj' could be dandling

    //*
    int i;
    for (i=0; i<S52_PRIO_NUM; ++i) {
        int j;
        for (j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = c->renderBin[i][j];
            unsigned int idx;
            for (idx=0; idx<rbin->len; ++idx) {
                S52_obj *o = (S52_obj *)g_ptr_array_index(rbin, idx);

                // debug
                //PRINTF("o:%p obj:%p\n", o, obj);

                if (obj == o) {
                    g_ptr_array_remove_index_fast(rbin, idx);
                    return o;
                }
            }
        }
    }
    //*/

    PRINTF("WARNING: object handle not found\n");

    return NULL;
}


DLL int    STD S52_loadObject(const char *objname, void *shape)
{
    S57_geo *geoData = NULL;

    S52_CHECK_INIT;

    if ( (NULL==objname) || (NULL==shape) || (NULL==_cellList)) {
        PRINTF("ERROR: objname / shape / _cellList --> NULL\n");
        return FALSE;
    }

    // debug
    //PRINTF("XXXXX DEBUG: starting to load object (%s:%X)\n", objname, shape);


#ifdef S52_USE_GV
    // debug: filter out GDAL/OGR metadata
    //if (0==g_strcmp0("DSID", objname, 4))
    if (0==g_strcmp0("DSID", objname, 4))
        return FALSE;

    geoData = S57_gvLoadObject (objname, (void*)shape);
#else
    geoData = S57_ogrLoadObject(objname, (void*)shape);
#endif

    if (NULL == geoData)
        return FALSE;


    // set cell extent from each object
    // NOTE:should be the same as CATALOG.03?
    if (_META_T != S57_getObjtype(geoData)) {
    //if ((_META_T!=S57_getObjtype(geoData)) && (0!=strcmp(objname, "XX0WORLD"))) {
        _extent ext;

        S57_getExt(geoData, &ext.W, &ext.S, &ext.E, &ext.N);
        //PRINTF("%s :: MIN: %f %f  MAX: %f %f\n", objname,  ext.w, ext.s, ext.e, ext.n);
        if (_crntCell->ext.S > ext.S) _crntCell->ext.S = ext.S;
        if (_crntCell->ext.W > ext.W) _crntCell->ext.W = ext.W;
        if (_crntCell->ext.N < ext.N) _crntCell->ext.N = ext.N;
        if (_crntCell->ext.E < ext.E) _crntCell->ext.E = ext.E;
        //PRINTF("%s :: MIN: %f %f  MAX: %f %f\n", objname, _crntCell->ext.w, _crntCell->ext.s, _crntCell->ext.e, _crntCell->ext.n);

        // check M_QUAL:CATZOC
        if (0== g_strcmp0(objname, "M_QUAL"))
            _crntCell->catzocstr = S57_getAttVal(geoData, "CATZOC");  // data quality indicator

        // check M_ACCY:POSACC
        if (0== g_strcmp0(objname, "M_ACCY"))
            _crntCell->posaccstr = S57_getAttVal(geoData, "POSACC");  // data quality indicator

        // check MAGVAR
        if (0== g_strcmp0(objname, "MAGVAR")) {
            // MAGVAR:VALMAG and
            _crntCell->valmagstr = S57_getAttVal(geoData, "VALMAG");  //
            // MAGVAR:RYRMGV and
            _crntCell->ryrmgvstr = S57_getAttVal(geoData, "RYRMGV");  //
            // MAGVAR:VALACM
            _crntCell->valacmstr = S57_getAttVal(geoData, "VALACM");  //
         }

        // check M_CSCL compilation scale
        if (0== g_strcmp0(objname, "M_CSCL")) {
            _crntCell->cscalestr = S57_getAttVal(geoData, "CSCALE");
        }

        // check M_SDAT:VERDAT
        if (0== g_strcmp0(objname, "M_SDAT")) {
            _crntCell->sverdatstr = S57_getAttVal(geoData, "VERDAT");
        }
        // check M_VDAT:VERDAT
        if (0== g_strcmp0(objname, "M_VDAT")) {
            _crntCell->vverdatstr = S57_getAttVal(geoData, "VERDAT");
        }

    } else {
        // check DSID  (GDAL metadata)
        if (0== g_strcmp0(objname, "DSID")) {
            GString *dsid_sdatstr = S57_getAttVal(geoData, "DSID_SDAT");
            GString *dsid_vdatstr = S57_getAttVal(geoData, "DSID_VDAT");
            double   dsid_sdat    = (NULL == dsid_sdatstr) ? 0.0 : S52_atof(dsid_sdatstr->str);
            double   dsid_vdat    = (NULL == dsid_vdatstr) ? 0.0 : S52_atof(dsid_vdatstr->str);

            // legend
            _crntCell->dsid_dunistr = S57_getAttVal(geoData, "DSPM_DUNI");  // units for depth
            _crntCell->dsid_hunistr = S57_getAttVal(geoData, "DSPM_HUNI");  // units for height
            _crntCell->dsid_csclstr = S57_getAttVal(geoData, "DSPM_CSCL");  // scale  of display
            _crntCell->dsid_sdatstr = S57_getAttVal(geoData, "DSPM_SDAT");  // sounding datum
            _crntCell->dsid_vdatstr = S57_getAttVal(geoData, "DSPM_VDAT");  // vertical datum
            _crntCell->dsid_hdatstr = S57_getAttVal(geoData, "DSPM_HDAT");  // horizontal datum
            _crntCell->dsid_isdtstr = S57_getAttVal(geoData, "DSID_ISDT");  // date of latest update
            _crntCell->dsid_updnstr = S57_getAttVal(geoData, "DSID_UPDN");  // number of latest update
            _crntCell->dsid_edtnstr = S57_getAttVal(geoData, "DSID_EDTN");  // edition number
            _crntCell->dsid_uadtstr = S57_getAttVal(geoData, "DSID_UADT");  // edition date

            _crntCell->dsid_heightOffset = dsid_vdat - dsid_sdat;

            // debug
            //S57_dumpData(geoData, FALSE);
        }


    }

    _insertS57Obj(_crntCell, geoData);

    S52_CS_add(_crntCell->local, geoData);

    return TRUE;
}

//---------------------------------------------------
//
// CULL (work in progress)
//
//---------------------------------------------------

#if 0
static S52_extent _clip(S52_extent A, S52_extent B)
// assume A, B intersect or inside
{
    S52_extent clip;

    // experimantal
    clip.s = (A.s > B.s)? A.s : B.s;
    clip.w = (A.w > B.w)? A.w : B.w;
    clip.n = (A.n > B.n)? B.n : A.n;
    clip.e = (A.e > B.e)? B.e : A.e;

    return clip;
}
#endif

static int        _intersec(_extent A, _extent B)
// TRUE if intersec, FALSE if outside
{
    if (B.N < A.S) return FALSE;
    if (B.E < A.W) return FALSE;
    if (B.S > A.N) return FALSE;
    if (B.W > A.E) return FALSE;

    return TRUE;
}

static int        _moveObj(_cell *cell, GPtrArray *oldBin, unsigned int idx, int oldPrio, int obj_t)
// TRUE if an 'obj' switched layer (priority), else FALSE
// this is to solve the problem of moving an object from one 'set' to an other
// it shuffle the array that act as a 'set'
{
    S52_obj *obj = NULL;

    if (idx<oldBin->len)
        obj = (S52_obj *)g_ptr_array_index(oldBin, idx);
    else {
        //PRINTF("ERROR: render bin index out of bound: %i max: %i\n", idx, oldBin->len);
        //g_assert(0);
        return FALSE;
    }

    int newPrio = S52_PL_getDPRI(obj);

    // if the newPrio is greater than oldPrio then the obj
    // will have is CS redone!
    if (oldPrio != newPrio) {
        GPtrArray *newBin = cell->renderBin[newPrio][obj_t];

        g_ptr_array_add(newBin, obj);
        if (NULL == g_ptr_array_remove_index_fast(oldBin, idx)) {
                PRINTF("ERROR: no object to remove\n");
                g_assert(0);
        }

        return TRUE;
    }

    return FALSE;
}

static S52_obj   *_delObj(S52_obj *obj)
{
        S57_geo *geo = S52_PL_getGeo(obj);

    // debug
    //PRINTF("objH:%#lX, ID:%i\n", (long unsigned int)obj, S57_getGeoID(geo));

    S52_GL_del(obj);

    S57_doneData(geo, NULL);
    S52_PL_setGeo(obj, NULL);

    // NULL
    obj = S52_PL_delObj(obj);

    return obj; // NULL
}

static int        _app()
// WARNING: not reentrant
{
    //PRINTF("_app(): -.0-\n");

    // first delete pending mariner
    //unsigned int i;
    for (guint i=0; i<_objToDelList->len; ++i) {
        S52_obj *obj = (S52_obj *)g_ptr_array_index(_objToDelList, i);

        // delete ref to ownshp
        //S57_geo *geo = S52_PL_getGeo(obj);
        //if (0==g_strcmp0(S57_getName(geo), "ownshp", 6)) {
        //    _ownshp = FALSE;  // NULL but S52ObjectHandle can be an gint in some config
        //}

        _delObj(obj);
    }
    g_ptr_array_set_size(_objToDelList, 0);

    //PRINTF("_app(): -.1-\n");

    // on Android, VBO need rebulding when the focus is back to libS52
    // as graphic HW need re-init
    // FIXME: some S52_obj are corrupt
    /*
    if (TRUE == S52_GL_resetVBOID()) {
        guint i;
        for (guint i=0; i<_cellList->len; ++i) {
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            // one cell

            int prio;
            for (prio=S52_PRIO_NODATA; prio<S52_PRIO_NUM; ++prio) {
                // one layer

                int obj_t;
                for (obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                    // one object type (render bin)
                    GPtrArray *rbin = ci->renderBin[prio][obj_t];

                    guint idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, i);
                        //S52_GL_del(obj);
                        S57_geo  *geoData = S52_PL_getGeo(obj);
                        S57_prim *prim    = S57_getPrimGeo(geoData);
                        if (NULL != prim) {
                            guint vboID = 0;
                            S57_setPrimDList(prim, vboID);
                        }
                    }
                }
            }
        }
    }
    //*/



    if (TRUE == _doCS) {
        // 1 - reparse CS
        //unsigned int i = 0;
        for (guint i=0; i<_cellList->len; ++i) {
            //_cell *ci = &g_array_index(_cellList, _cell, i);
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            // one cell

            int prio;
            for (prio=S52_PRIO_NODATA; prio<S52_PRIO_NUM; ++prio) {
                // one layer

                int obj_t;
                for (obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                    // one object type (render bin)
                    GPtrArray *rbin = ci->renderBin[prio][obj_t];

                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        // one object
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                        S52_PL_resloveCS(obj);
                    }
                }
            }
        }

        //PRINTF("_app(): -0-\n");

        // 2 - move obj
        for (guint i=0; i<_cellList->len; ++i) {
            //_cell *ci = &g_array_index(_cellList, _cell, i);
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            // one cell

            int prio;
            for (prio=S52_PRIO_NODATA; prio<S52_PRIO_NUM; ++prio) {
                // one layer

                int obj_t;
                for (obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                    // one object type (render bin)
                    GPtrArray *rbin = ci->renderBin[prio][obj_t];

                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        // one object
                        int check = TRUE;
                        while (TRUE == check)
                            check = _moveObj(ci, rbin, idx, prio, obj_t);
                    }
                }
            }
        }
    }

    //PRINTF("_app(): -1-\n");

    // reset journal
    for (guint i=0; i<_cellList->len; ++i) {
        //_cell *ci = &g_array_index(_cellList, _cell, i);
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        g_ptr_array_set_size(c->objList_supp, 0);
        g_ptr_array_set_size(c->objList_over, 0);
    }

    /*
    {   // debug --check for object that land on the NODATA layer
        //unsigned int i = 0;
        for (guint i=0; i<_cellList->len; ++i) {
            _cell *ci = &g_array_index(_cellList, _cell, i);
            // one cell


            int obj_t;
            for (obj_t=S52__META_T; obj_t<S52_N_OBJ_T; ++obj_t) {
                // one object type (render bin)
                GPtrArray *rbin = ci->renderBin[S52_PRIO_NODATA][obj_t];

                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    // one object
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    PRINTF("WARNING: object (%s:%i) is on NODATA layer\n", S57_getName(geo), obj_t);
                    S57_dumpData(geo, FALSE);

                }
            }
        }
    }
    */


    // done rebuilding CS
    _doCS = FALSE;

    //PRINTF("_app(): -2-\n");

    return TRUE;
    //return;
}

static int        _cullObj(_cell *c)
// one cell; cull object out side the view and object supressed
// object culled are not inserted in the list of object to draw
{
    int j;
    //for (j=0; j<S52_PRIO_NUM; ++j) {
    for (j=0; j<S52_PRIO_MARINR; ++j) {

        // one layer
        int k;
        for (k=0; k<N_OBJ_T; ++k) {
            GPtrArray *rbin = c->renderBin[j][k];

            // one object
            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geo = S52_PL_getGeo(obj);


                // make certain that the display is no suppressed
                // it is redondant since all object are initialy not suppressed
                //S57_setSupp(geo, FALSE);

                ++_nTotal;

                // SCAMIN & PLib (disp cat)
                if (TRUE == S52_GL_isSupp(obj)) {
                    //S57_setSupp(geo, TRUE);
                    ++_nCull;
                    continue;
                }

                // outside view
                // NOTE: object can be inside 'ext' but outside the 'view' (cursor pick)
                if (TRUE == S52_GL_isOFFscreen(obj)) {
                    //S57_setSupp(geo, TRUE);
                    ++_nCull;
                    continue;
                }

                // ------------------------------------------
                //*
                // is this object supress by user
                if (TRUE == S57_getSup(geo)) {
                    ++_nCull;
                    continue;
                }

                // sort object according to radar flags
                // note: default to 'over' if something else than 'supp'
                if (S52_RAD_SUPP == S52_PL_getRPRI(obj)) {
                    //g_ptr_array_add(_objList_supp, obj);
                    g_ptr_array_add(c->objList_supp, obj);
                } else {
                    //g_ptr_array_add(_objList_over, obj);
                    g_ptr_array_add(c->objList_over, obj);
                }
                //*/
                // ------------------------------------------

                // if this object has TX or TE, draw text last
                if (TRUE == S52_PL_hasText(obj)) {
                    //g_ptr_array_add(_textList, obj);
                    g_ptr_array_add(c->textList, obj);
                }
            }

            // traverse and draw all mariner object for each layer (of each chart)
            // that are bellow S52_PRIO_MARINR
            // BUG: this over draw mariner object (ex: all pastck is drawn on each chart)
            // FIX: use chart extent to clip
            //    for (k=0; k<N_OBJ_T; ++k) {
            {
                    GPtrArray *rbin = _marinerCell->renderBin[j][k];

                    // one object
                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        if (TRUE != S57_getSup(geo) && FALSE == S52_GL_isSupp(obj)) {
                            //S52_GL_draw(obj, NULL);
                            if (S52_RAD_SUPP == S52_PL_getRPRI(obj)) {
                                g_ptr_array_add(c->objList_supp, obj);
                            } else {
                                g_ptr_array_add(c->objList_over, obj);
                            }

                            if (TRUE == S52_PL_hasText(obj))
                                g_ptr_array_add(c->textList, obj);
                        }
                    }
            //    }
            }

        }
    }

    return TRUE;
}

static int        _cull(_extent ext)
// - viewport
// - small cell region on top
// - later:line removal
{

    // light are allway drawn - event if outside view (sector might be inside)

    // assume: cellList is sorted, big ti small scale (small to large region)

    // CULL: filter out cells that are outside the view

    // BUG: traversing order differ from drawing this cause to cull
    // object that should be drawn
    // this can happen where 2 cells of same scale overlap
    /*
    {
        //int i = 0;
        //int j = 0;
        for (guint i=0; i<_cellList->len; ++i) {
        //for (i=_cellList->len-1; i>=0 ; --i) {
            _cell *ci = &g_array_index(_cellList, _cell, i);
            PRINTF("cell name i(%i): %s\n", i, ci->filename->str);
            if (TRUE == _intersec(ci->ext, ext)) {
                //int j = 0;
                for (guint j=i+1; j<_cellList->len; ++j) {
                    _cell *cj = &g_array_index(_cellList, _cell, j);
                    PRINTF("\tcell name j(%i): %s\n", j, cj->filename->str);
                    if (TRUE == _intersec(ci->ext, cj->ext)) {
                        // supress display of object that are 'under' a cell
                        _suppObject(ci->ext, cj);
                    }
                }
            } else {
                // cell outside view
            }
        }
    }
    */

    /*
    // mark object for drawing
    // A - initialy all object of every cell are not culled
    // B - current  cell is 'this' cell
    // C - following cell is 'other' cells
    // for all cell do
    //   1 - show all object in 'this' cell that are not culled allready
    //   2 - cull all object in 'other' cells within 'this' cell
    {
        int i = 0;
        int j = 0;
        while (i < cellList->len) {
            // show object in the current cell
            // that have not been remove allready
            // SCANMIN fit here
            _showObject(cellList[i]);


            j = i;
            while (j < cellList->len) {
                // remove object in rectangle of current cell
                // for all the following cells
                _cullObject(cellList[j], cellList[i].rect);
                ++j;
            }
        }
    }
    */

    //unsigned int i = 0;

    // all cells - larger region first (small scale)
    //for (guint i=0; i<_cellList->len; ++i) {
    for (guint i=_cellList->len; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);

        // is this chart visible
        if (TRUE == _intersec(c->ext, ext)) {
            _cullObj(c);
        }
    }

    /*  // BUG: sector outside view must show sector leg inside view
    {   // cull lights outside view
        // light outside the view are shown
        //unsigned int i = 0;
        // --6MARIN.000 doesn't have lights so no need to look at it
        //for (i=0; i<_cellList->len; ++i) {
        for (guint i=_cellList->len-1; i>0 ; --i) {
            //_cell *c = &g_array_index(_cellList, _cell, i);
            _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
            //if (FALSE == _intersec(c->ext, ext)) {
                //unsigned int j = 0;
                for (guint j=0; j<c->lights_sector->len; ++j) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // here we hit lights in view that we're allready
                    // processed in the first part of the cull() .. not optimal
                    S57_setSupp(geo, FALSE);

                    // SCAMIN & PLib
                    if (TRUE == S52_GL_isSupp(obj)) {
                        S57_geo *geo = S52_PL_getGeo(obj);
                        S57_setSupp(geo, TRUE);
                        ++_nCull;
                    }
                }
            //}
        }
    }
    */

    return TRUE;
}

DLL    int STD S52_drawText()
// deprecated
{
    /*
    //unsigned int i = 0;
    for (guint i=0; i<_textList->len; ++i) {
        S52_obj *obj = (S52_obj *)g_ptr_array_index(_textList, i);
        // FIXME: view group for Text (Important/Other)
        S52_GL_drawText(obj);
    }
    */


    //g_ptr_array_foreach(_textList, (GFunc)S52_GL_drawText, NULL);

    // empty array but keep mem alloc'ed
    //g_ptr_array_set_size(_textList, 0);

    return TRUE;
}

static int        _drawRADAR()
{
    if (NULL != _RADAR_cb) {
        _RADAR_cb();
        //_doRADAR = FALSE;
    }

    return TRUE;
}

//static int        _draw(S52_extent ext, S52_RadPrio radPrio)
//static int        _draw(S52_extent ext)
static int        _draw()
// draw object inside view
// also collect object that have text
{
    unsigned int i = 0;
    unsigned int n = _cellList->len-1;
    for (i=n; i>0; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        g_atomic_int_get(&_atomicAbort);
        if (TRUE == _atomicAbort) {
            PRINTF("abort drawing .. \n");
            return TRUE;
        }

        // draw under radar
        g_ptr_array_foreach (c->objList_supp, (GFunc)S52_GL_draw, NULL);
        g_ptr_array_set_size(c->objList_supp, 0);

        _drawRADAR();

        // draw over radar
        g_ptr_array_foreach (c->objList_over, (GFunc)S52_GL_draw, NULL);
        g_ptr_array_set_size(c->objList_over, 0);

        // draw arc (experimental)
        // FIXME: bug this draw on top of everything
        /*
        if (2.0 == S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0 == S52_MP_get(S52_MAR_DISP_WHOLIN)) {
            //unsigned int i = 0;
            for (guint i=1; i<_route->len; ++i) {
                S52_obj *objA = (S52_obj *)g_ptr_array_index(_route, i-1);
                S52_obj *objB = (S52_obj *)g_ptr_array_index(_route, i+0);

                S52_GL_drawArc(objA, objB);
            }
        }
        */

        // draw text
        g_ptr_array_foreach (c->textList,     (GFunc)S52_GL_drawText, NULL);
        g_ptr_array_set_size(c->textList,     0);
    }



    /*
    // draw under radar
    g_ptr_array_foreach (_objList_supp, (GFunc)S52_GL_draw, NULL);
    g_ptr_array_set_size(_objList_supp, 0);

    _drawRADAR();

    // draw over radar
    g_ptr_array_foreach (_objList_over, (GFunc)S52_GL_draw, NULL);
    g_ptr_array_set_size(_objList_over, 0);

    // draw arc (experimental)
    if (2.0 == S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0 == S52_MP_get(S52_MAR_DISP_WHOLIN)) {
        //unsigned int i = 0;
        for (guint i=1; i<_route->len; ++i) {
            S52_obj *objA = (S52_obj *)g_ptr_array_index(_route, i-1);
            S52_obj *objB = (S52_obj *)g_ptr_array_index(_route, i+0);

            S52_GL_drawArc(objA, objB);
        }
    }

    // draw text
    g_ptr_array_foreach (_textList,     (GFunc)S52_GL_drawText, NULL);
    g_ptr_array_set_size(_textList,     0);
    //*/


    /*
    // all cells --larger region first
    // do not draw last cell "--6MARIN.000"
    unsigned int i = 0;
    unsigned int n = _cellList->len-1;
    for (i=n; i>0; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // dont traverse cells that are outside view
        if (TRUE == _intersec(c->ext, ext)) {


            // one cell
            int j;
            //for (j=0; j<S52_PRIO_NUM; ++j) {
            for (j=0; j<S52_PRIO_MARINR; ++j) {
            //for (j=S52_PRIO_GROUP1; j<S52_PRIO_NUM; ++j) {

                // one layer
                int k;
                for (k=0; k<N_OBJ_T; ++k) {
                    GPtrArray *rbin = c->renderBin[j][k];

                    // one object
                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        // debug
                        //if (573 == S57_getGeoID(geo)) {
                        //    PRINTF("%s\n", S57_getName(geo));
                        //}

                        //if (radPri == S52_PL_getRPRI(obj)) {
                            // if display of object is not suppressed
                            if (TRUE != S57_getSupp(geo)) {
                                //PRINTF("%s\n", S57_getName(geo));

                                //if (573 == S57_getGeoID(geo)) {
                                //    PRINTF("%s\n", S57_getName(geo));
                                //}

                                S52_GL_draw(obj, NULL);

                                // doing this after the draw because draw() will
                                // parse the text
                                //if (TRUE == S52_PL_hasText(obj))
                                //    g_ptr_array_add(_textList, obj);

                            } //else
                                // unsuppress object (for next frame)
                            //  S57_setSupp(geo, FALSE);
                        //}
                    }
                }

                // traverse and draw all mariner object for each layer (of each chart)
                // that are bellow S52_PRIO_MARINR
                // BUG: this over draw mariner object (ex: all pastck is drawn on each chart)
                // FIX: use chart extent to clip
                for (k=0; k<N_OBJ_T; ++k) {
                    GPtrArray *rbin = _marinerCell->renderBin[j][k];

                    // one object
                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        if (TRUE != S57_getSupp(geo)) {
                            S52_GL_draw(obj, NULL);

                            if (TRUE == S52_PL_hasText(obj))
                                g_ptr_array_add(_textList, obj);
                        }
                    }
                }

                // add curve to route here
                if (j==S52_PRIO_HAZRDS && NULL!=_route) {
                    // if mariner want arc drawn
                    if (2.0 == S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0 == S52_MP_get(S52_MAR_DISP_WHOLIN)) {
                        //unsigned int i = 0;
                        for (guint i=1; i<_route->len; ++i) {
                            S52_obj *objA = (S52_obj *)g_ptr_array_index(_route, i-1);
                            S52_obj *objB = (S52_obj *)g_ptr_array_index(_route, i+0);

                            S52_GL_drawArc(objA, objB);
                        }
                    }

                    // wholin
                    //
                    //if (1.0 == S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0 == S52_MP_get(S52_MAR_DISP_WHOLIN)) {
                    //    //unsigned int i = 0;
                    //    for (guint i=0; i<_wholin->len; ++i) {
                    //        S52_obj *objA = (S52_obj *)g_ptr_array_index(_wholin, i);
                    //        S52_obj *objB = objA;
                    //
                    //        S52_GL_drawArc(objA, objB);
                    //    }
                    //}
                }

            }
            // for each cell, not after all cell,
            // because city name appear twice
            // FIXME: cull object of overlapping region of cell of DIFFERENT nav pourpose
            // NOTE: no culling of object of overlapping region of cell of SAME nav pourpose
            //display priority 8
            S52_drawText();

        }
    }
    //*/

    return TRUE;
}

//static int        _drawLayer(S52_extent ext, int layer)
static int        _drawLayer(_extent ext, int layer)
{
    //unsigned int i = 0;

    // all cells --larger region first
    for (guint i=_cellList->len; i>0 ; --i) {
        //_cell *c = &g_array_index(_cellList, _cell, i-1);
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
        if (TRUE == _intersec(c->ext, ext)) {

            // one layer
            int k;
            for (k=0; k<N_OBJ_T; ++k) {
                GPtrArray *rbin = c->renderBin[layer][k];

                // one object
                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // debug
                    //PRINTF("%s\n", S57_getName(geo));

                    // if display of object is not suppressed
                    if (TRUE != S57_getSup(geo)) {
                            //PRINTF("%s\n", S57_getName(geo));

                        //S52_GL_draw(obj);
                        S52_GL_draw(obj, NULL);

                        // doing this after the draw because draw() will
                        // parse the text
                        if (TRUE == S52_PL_hasText(obj))
                            //g_ptr_array_add(_textList, obj);
                            g_ptr_array_add(c->textList, obj); // not tested

                    } //else
                        // unsuppress object (for next frame)
                      //      S57_setSupp(geo, FALSE);
                }
            }
        }
    }

    return TRUE;
}

#if 0
static int        _appLights(void)
// mark all cells sector lights of the need to be re-culled
{
    //unsigned int i = 0;

    // APP  (when a cell is added or deleted to the set, flags cull)
    //for (guint i=0; i<_cellList->len; ++i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
    //    c->cullLights = TRUE;
    //}


    return TRUE;
}
#endif

static int        _cullLights(void)
// CULL (first draw() after APP, on all cells)
{
    //unsigned int i = 0;

    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        //unsigned int j = 0;

        // a cell can have no lights sector
        if (NULL  == c->lights_sector) {
            continue;
        }

        //if (FALSE == c->cullLightSec)
        //    continue;

        // FIXME: use for_each()
        //if (FALSE == _intersec(c->ext, ext)) {
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            S57_geo *geo = S52_PL_getGeo(obj);
            _extent oext;
            S57_getExt(geo,  &oext.W, &oext.S, &oext.E, &oext.N);

            // do CS wild traversing all lights sectors
            S52_PL_resloveCS(obj);

            unsigned int k = 0;
            // traverse the cell 'above' to check if extent overlap this light
            for (k=i-1; k>0 ; --k) {
                _cell *cellAbove = (_cell*) g_ptr_array_index(_cellList, k);
                // skip if same scale
                if (cellAbove->filename->str[2] > c->filename->str[2]) {
                    if (TRUE == _intersec(cellAbove->ext, oext)) {
                        // check this: a chart above this light sector
                        // does not have the same lights (this would be a bug in S57)
                        S57_setSup(geo, TRUE);
                    }

                }


            }
        }
        //c->cullLightSec = FALSE;
    }
    return TRUE;
}


static int        _drawLights(void)
// draw all lights of all cells outside view extend
// so that sector and legs show up on screen event if
// the light itself is outside
// BUG: all are S52_RAD_OVER by default, check if UNDER RADAR make sens
{
    //unsigned int i = 0;
    // FIXME: do not draw light that are under a cell

    // APP  (when a cell is added or deleted to the set, flags cull)
    //for (guint i=0; i<_cellList->len; ++i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
    //    c->cullLightSec = TRUE;
    //}
    /*
    // CULL (first draw() after APP, on all cells)
    for (i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        unsigned int j = 0;

        // a cell can have no lights sector
        if (NULL  == c->lights_sector) {
            continue;
        }

        if (FALSE == c->cullLightSec)
            continue;

        // FIXME: use for_each()
        //if (FALSE == _intersec(c->ext, ext)) {
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            S57_geo *geo = S52_PL_getGeo(obj);
            _extent oext;

            S57_getExt(geo,  &oext.W, &oext.S, &oext.E, &oext.N);

            // do CS wild traversing all lights sectors
            S52_PL_resloveCS(obj);

            unsigned int k = 0;
            // traverse the cell 'above' to check if extent overlap this light
            for (k=i-1; k>0 ; --k) {
                _cell *cellAbove = (_cell*) g_ptr_array_index(_cellList, k);
                // skip if same scale
                if (cellAbove->filename->str[2] > c->filename->str[2]) {
                    if (TRUE == _intersec(cellAbove->ext, oext)) {
                        // check this: a chart above this light sector
                        // does not have the same lights (this would be a bug in S57)
                        //S57_setSup(geo, TRUE);
                    }

                }


            }
        }
        c->cullLightSec = FALSE;
    }
    */

    if (TRUE == _doCullLights)
        _cullLights();
    _doCullLights = FALSE;

    // DRAW (use normal filter (SCAMIN,Supp,..) on all object of all cells)

    // this way all the lights sector are drawn
    // light outside but with part of sector visible are drawn
    // but light bellow a cell is not drawn
    // also light are drawn last (ie after all cells)
    // so a sector is not shoped by an other cell next to it

    //unsigned int i = 0;

    //for (guint i=0; i<_cellList->len-1; ++i) {
    //for (guint i=0; i<_cellList->len; ++i) {
    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        //unsigned int j = 0;

        // a cell can have no lights sector
        if (NULL == c->lights_sector)
            continue;

        // FIXME: use for_each()
        //if (FALSE == _intersec(c->ext, ext)) {
            for (guint j=0; j<c->lights_sector->len; ++j) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
                //S57_geo *geo = S52_PL_getGeo(obj);
                //_extent oext;

                //S57_getExt(geo,  &oext.W, &oext.S, &oext.E, &oext.N);

                // draw if light outside view
                //if (TRUE != _intersec(ext, oext)) {
                    // FIXME: do not draw light that are under a cell
                    // if this light intersect a cell

                //S52_GL_draw(obj, NULL);

                    // SCAMIN & PLib (disp prio)
                    if (TRUE != S52_GL_isSupp(obj)) {
                        S57_geo *geo = S52_PL_getGeo(obj);
                        if (TRUE != S57_getSup(geo))
                            S52_GL_draw(obj, NULL);
                    }
                //}
            }
            //}
    }

    return TRUE;
}

static int        _drawLegend()
// draw legend of each cell
// Starting at page I-50.
{
        //1.       units for depth                 DUNI subfield of the DSPM field
        // DSID:DSPM_DUNI

        //2.       units for height                HUNI subfield of the DSPM field
        // DSID:DSPM_HUNI

        //3.       scale of display                Selected by user. (The default display scale is defined by the CSCL
        //                                         subfield of the DSPM field or CSCALE attribute value of the M_CSCL object.)
        // DSID:DSPM_CSCL or
        // M_CSCL:CSCALE

        //4. data quality indicator            a. CATZOC attribute of the M_QUAL object for bathymetric data
        //                                     b. POSACC attribute of the M_ACCY object (if available) for non-bathymetric data.
        //Due to the way quality is encoded in the ENC, both values (a and b) must be used.
        // M_QUAL:CATZOC and
        // M_ACCY:POSACC

        //5. sounding/vertical datum SDAT and VDAT subfields of the DSPM field or the VERDAT attribute
        //                           of the M_SDAT object and M_VDAT object.
        //                           (VERDAT attributes of individual objects must not be used for the legend.)
        // DSID:DSPM_SDAT and
        // DSID:DSPM_VDAT
        // or
        // M_SDAT:VERDAT  and
        // M_VDAT:VERDAT

        //6. horizontal datum            HDAT subfield of the DSPM field
        // DSID:DSPM_HDAT

        //7. value of safety depth       Selected by user. Default is 30 metres.
        //8. value of safety contour     Selected by user. Default is 30 metres.

        //9. magnetic variation          VALMAG, RYRMGV and VALACM of the MAGVAR object. Item
        //                               must be displayed as VALMAG RYRMGV (VALACM) e.g., 4°15W 1990(8'E)
        // MAGVAR:VALMAG and
        // MAGVAR:RYRMGV and
        // MAGVAR:VALACM

        //10. date and number of latest  ISDT and UPDN subfields of the DSID field of the last update cell
        //                               update file (ER data set) applied.
        // DSID:DSID_ISDT and
        // DSID:DSID_UPDN

        //11. edition number and date of EDTN and UADT subfields of the DSID field of the last EN data issue
        //                               of current ENC issue of the ENC set.
        // DSID:DSID_EDTN and
        // DSID:DSID_UADT

        //12. chart projection           Projection used for the ECDIS display (e.g., oblique azimuthal).
        // Mercator

    // ---------

    for (guint i=1; i<_cellList->len; ++i) {
        _cell *c        = (_cell*) g_ptr_array_index(_cellList, i);
        double xyz[3]   = {c->ext.W, c->ext.N, 0.0};

        char   str[80]  = {'\0'};
        //double offset_x = 1.0;
        //double offset_y = 1.0;
        double offset_x = 8.0;
        double offset_y = 8.0;

        if (INFINITY==c->ext.W || -INFINITY==c->ext.W)
            return FALSE;

        if (FALSE == S57_geo2prj3dv(1, xyz))
            return FALSE;

        S52_GL_getStrOffset(&offset_x, &offset_y, str);

        // ENC Name
        if (NULL == c->filename)
            SPRINTF(str, "ENC NAME: Unknown");
        else
            SPRINTF(str, "%.8s", c->filename->str);
        S52_GL_drawStr(xyz[0], xyz[1], str, 1, 3);

        // DSID:DSPM_DUNI: units for depth
        if (NULL == c->dsid_dunistr)
            SPRINTF(str, "dsid_dspm_duni: NULL");
        else {
            if ('1' == *c->dsid_dunistr->str)
                SPRINTF(str, "DEPTH IN METER");
            else
                SPRINTF(str, "DEPTH IN :%s", (NULL==c->dsid_dunistr) ? "NULL" : c->dsid_dunistr->str);
        }
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // DSID:DSPM_HUNI: units for height
        if (NULL==c->dsid_hunistr)
            SPRINTF(str, "dsid_dpsm_huni: NULL");
        else {
            if ('1' == *c->dsid_hunistr->str)
                SPRINTF(str, "HEIGHT IN METER");
            else
                SPRINTF(str, "HEIGHT IN :%s", c->dsid_hunistr->str);
        }
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 0);

        //7. value of safety depth       Selected by user. Default is 30 metres.
        SPRINTF(str, "Safety Depth: %.1f", S52_MP_get(S52_MAR_SAFETY_DEPTH));
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 0);

        //8. value of safety contour     Selected by user. Default is 30 metres.
        SPRINTF(str, "Safety Contour: %.1f", S52_MP_get(S52_MAR_SAFETY_CONTOUR));
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 0);

        // scale of display
        xyz[1] -= offset_y; // add some room
        if (NULL==c->dsid_csclstr)
            SPRINTF(str, "dsid_cscl:%s", (NULL==c->dsid_csclstr) ? "NULL" : c->dsid_csclstr->str);
        else
            SPRINTF(str, "Scale 1:%s", (NULL==c->dsid_csclstr) ? "NULL" : c->dsid_csclstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 2);


        // ----------- DATUM ----------------------------------------------------------

        // DSID:DSPM_SDAT: sounding datum
        SPRINTF(str, "dsid_sdat:%s", (NULL==c->dsid_sdatstr) ? "NULL" : c->dsid_sdatstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // vertical datum
        SPRINTF(str, "dsid_vdat:%s", (NULL==c->dsid_vdatstr) ? "NULL" : c->dsid_vdatstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // horizontal datum
        SPRINTF(str, "dsid_hdat:%s", (NULL==c->dsid_hdatstr) ? "NULL" : c->dsid_hdatstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // legend from M_SDAT
        // sounding datum
        if (NULL != c->sverdatstr) {
            SPRINTF(str, "sverdat:%s", c->sverdatstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }
        // legend from M_VDAT
        // vertical datum
        if (NULL != c->vverdatstr) {
            SPRINTF(str, "vverdat:%s", c->vverdatstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }


        // ---------------------------------------------------------------------

        // date of latest update
        SPRINTF(str, "dsid_isdt:%s", (NULL==c->dsid_isdtstr) ? "NULL" : c->dsid_isdtstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        // number of latest update
        SPRINTF(str, "dsid_updn:%s", (NULL==c->dsid_updnstr) ? "NULL" : c->dsid_updnstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        // edition number
        SPRINTF(str, "dsid_edtn:%s", (NULL==c->dsid_edtnstr) ? "NULL" : c->dsid_edtnstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        // edition date
        SPRINTF(str, "dsid_uadt:%s", (NULL==c->dsid_uadtstr) ? "NULL" : c->dsid_uadtstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // legend from M_CSCL
        // scale
        if (NULL != c->cscalestr) {
            SPRINTF(str, "cscale:%s", c->cscalestr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }

        // legend from M_QUAL
        // data quality indicator
        SPRINTF(str, "catzoc:%s", (NULL==c->catzocstr) ? "NULL" : c->catzocstr->str);
        S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);

        // legend from M_ACCY POSACC
        // data quality indicator
        if (NULL != c->posaccstr) {
            SPRINTF(str, "posacc:%s", c->posaccstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }

        // legend from MAGVAR
        // magnetic
        if (NULL != c->valmagstr) {
            SPRINTF(str, "valmag:%s", c->valmagstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }

        if (NULL != c->ryrmgvstr) {
            SPRINTF(str, "ryrmgv:%s", c->ryrmgvstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }

        if (NULL != c->valacmstr) {
            SPRINTF(str, "valacm:%s", c->valacmstr->str);
            S52_GL_drawStr(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        }
    }

    return TRUE;
}

DLL int    STD S52_draw(void)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    // do not wait if an other thread is allready drawing
    //g_static_mutex_lock(&_mp_mutex);
    if (FALSE == g_static_mutex_trylock(&_mp_mutex))
        return FALSE;
    //
    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    //  check if we are shutting down
    if (NULL == _marinerCell) {
        PRINTF("shutting down\n");
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    // debug
    //PRINTF("DRAW: start ..\n");

    g_timer_reset(_timer);

    if (TRUE == S52_GL_begin(FALSE, FALSE)) {

        //PRINTF("S52_draw() .. -1.2-\n");

        //////////////////////////////////////////////
        // APP  .. update object
        _app();

        //////////////////////////////////////////////
        // CULL .. supress display of object (eg outside view)
        _extent ext;
#ifdef S52_USE_PROJ
        projUV uv1, uv2;
        S52_GL_getPRJView(&uv1.v, &uv1.u, &uv2.v, &uv2.u);

        // convert extent to deg
        uv1 = S57_prj2geo(uv1);
        uv2 = S57_prj2geo(uv2);
        ext.S = uv1.v;
        ext.W = uv1.u;
        ext.N = uv2.v;
        ext.E = uv2.u;

        _cull(ext);
#endif

        //PRINTF("S52_draw() .. -1.3-\n");

        //////////////////////////////////////////////
        // DRAW .. render

        if (TRUE == S52_MP_get(S52_MAR_DISP_OVERLAP)) {
            int layer;

            for (layer=0; layer<S52_PRIO_NUM; ++layer) {
                _drawLayer(ext, layer);

                // draw all lights (of all cells) outside ext
                if (S52_PRIO_HAZRDS == layer)
                    _drawLights();
            }
            //_drawText();
        } else {
            _draw();

            // complete leg extend from lights outside view
            _drawLights();
        }

        //PRINTF("S52_draw() .. -1.4-\n");

        // draw graticule
        if (TRUE == S52_MP_get(S52_MAR_DISP_GRATICULE))
            S52_GL_drawGraticule();

        // draw legend
        if (TRUE == S52_MP_get(S52_MAR_DISP_LEGEND))
            _drawLegend();

        // debug UTF - this string is rendered just above Becancour
        //S52_GL_drawStr(-5567198.0, 4019200.0,"Japanese Hiragana: (Iroha)"
	    //		     "いろはにほへとちりぬるを");
        //S52_GL_drawStr(-5567198.0, 4019200.0,"Thai:"
		//"๏ เป็นมนุษย์สุดประเสริฐเลิศคุณค่า  กว่าบรรดาฝูงสัตว์เดรัจฉาน");

        S52_GL_end(FALSE);

        // for each cell, not after all cell,
        // because city name appear twice
        // FIXME: cull object of overlapping region of cell of DIFFERENT nav pourpose
        // NOTE: no culling of object of overlapping region of cell of SAME nav pourpose
        // display priority 8

        // for when using COGL - need to be outside begin/end
        //S52_drawText();

        //PRINTF("S52_draw() .. -2-\n");

    }

    gdouble sec = g_timer_elapsed(_timer, NULL);
    //PRINTF("%.0f msec (%i obj / %i cmd)\n", sec * 1000, _nobj, _ncmd);
    //g_print("%.0f msec (%i obj / %i cmd) renew = %i / %iB\n", sec * 1000, _nobj, _ncmd, _nrealloc, fobj.sz);
    //g_print("DRAW: %.0f msec\n", sec * 1000);
    PRINTF("DRAW: %.0f msec\n", sec * 1000);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

static void       _delOldVessel(gpointer data, gpointer user_data)
// FIXME: should this go to the
{
    S52_obj *obj = (S52_obj *)data;
    S57_geo *geo = S52_PL_getGeo(obj);
    if (0 != g_strcmp0("vessel", S57_getName(geo)))
        return;

    GTimeVal now;
    g_get_current_time(&now);
    long old = S52_PL_getTimeSec(obj);
    if (now.tv_sec - old > S52_MP_get(S52_MAR_DEL_VESSEL_DELAY)) {
        GPtrArray *rbin = (GPtrArray *) user_data;
        // queue obj for deletion in next APP() cycle
        //g_ptr_array_add(_objToDelList, obj);
        // remove obj from 'cell'
        g_ptr_array_remove(rbin, obj);

        _delObj(obj);
    }
}

DLL int    STD S52_drawLast(void)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    // debug
    //PRINTF("DRAWLAST: .. start -0- XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");

    if (S52_MAR_DISP_LAYER_LAST_NONE == S52_MP_get(S52_MAR_DISP_LAYER_LAST))
        return TRUE;

    g_atomic_int_set(&_atomicAbort, FALSE);

    // do not wait if an other thread is allready drawing
    //g_static_mutex_lock(&_mp_mutex);
    if (FALSE == g_static_mutex_trylock(&_mp_mutex))
        return FALSE;

    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    g_timer_reset(_timer);

    ////////////////////////////////////////////////////////////////////
    // rebuilding CS if need be
    // APP: init the journal flush previous journal
    _app();

    // check stray vessel (occur when s52ais re-start)
    if (0.0 != S52_MP_get(S52_MAR_DEL_VESSEL_DELAY)) {
        GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][S52_POINT];
        g_ptr_array_foreach(rbin, _delOldVessel, rbin);
    }

    // debug
    //PRINTF("DRAWLAST: .. -1-\n");

    ////////////////////////////////////////////////////////////////////
    // no CULL (so no journal)
    // cull()


    ////////////////////////////////////////////////////////////////////
    // DRAW
    //
    if (TRUE == S52_GL_begin(FALSE, TRUE)) {
    //*
        // debug
        //PRINTF("DRAWLAST: ..  -2-\n");

        int i = 0;
        // then draw the Mariners' Object on top of it
        for (i=0; i<N_OBJ_T; ++i) {
            GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][i];

            //unsigned int idx;
            // FIFO
            //for (guint idx=0; idx<rbin->len; ++idx) {
            // LIFO: so that 'cursor' is drawn last (on top)
            for (guint idx=rbin->len; idx>0; --idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx-1);

                g_atomic_int_get(&_atomicAbort);
                if (TRUE == _atomicAbort) {
                    PRINTF("abort drawing .. \n");
                    //S52_GL_end(FALSE);
                    S52_GL_end(TRUE);
                    g_static_mutex_unlock(&_mp_mutex);
                    return TRUE;
                }

                // FIXME: debug if Mariner's Object outside 'view'
                // are effectively culled via _cull()

                // in some graphic driver this is expensive
                if (FALSE == S52_GL_isSupp(obj)) {
                    S52_GL_draw(obj, NULL);

                    // debug - commented
                    S52_GL_drawText(obj, NULL);
                }
            }
        }

        // debug
        //PRINTF("DRAWLAST: ..  -3-\n");

        // deprecated
        //S52_drawText();

        if (TRUE == S52_MP_get(S52_MAR_DISP_CRSR_POS)) {
            char str[80];
            projXY uv = {_cursor_lon, _cursor_lat};
            uv = S57_prj2geo(uv);
            SPRINTF(str, "%f %f", uv.v, uv.u);
            S52_GL_drawStr(_cursor_lon, _cursor_lat, str, 1, 1);
        }
    //*/

        S52_GL_end(TRUE);
    }

    g_static_mutex_unlock(&_mp_mutex);

    // debug
    //S52_dumpS57IDPixels("/home/sduclos/dart/helloWeb/tmp.png", 0, 0, 0);

    // debug
    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("DRAWLAST: %.0f msec\n", sec * 1000);

    return TRUE;
}

#ifdef S52_USE_GV
static int        _drawObj(const char *name)
{
    //unsigned int i = 0;
    //unsigned int n = _cellList->len;
    guint n = _cellList->len;

    // all cells --larger region first
    for (guint i=0; i<n; ++i) {
        //_cell *c = &g_array_index(_cellList, _cell, n-i-1);
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // one cell
        int j;
        for (j=0; j<S52_PRIO_NUM; ++j) {
            //for (j=S52_PRIO_GROUP1; j<S52_PRIO_NUM; ++j) {

            // one layer
            int k;
            for (k=0; k<N_OBJ_T; ++k) {
                GPtrArray *rbin = c->renderBin[j][k];

                // one object
                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // debug
                    //PRINTF("%s\n", S57_getName(geo));

                    if (0==S52_strncmp(name, S57_getName(geo), 6))
                        S52_GL_draw(obj);

                    //if (TRUE == S52_PL_hasText(obj))
                    //    g_ptr_array_add(_textList, obj);

                }
            }
        }
    }

    return TRUE;
}

DLL int    STD S52_drawLayer(const char *name)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    PRINTF("name: %s\n", name);

    // debug filter out some layer comming from OpenEV
    /*
    if (0!=g_strcmp0(name, "DEPARE"))
        return FALSE;
    if (0==g_strcmp0(name, "DSID"))
        return FALSE;
    if (0==g_strcmp0(name, "M_QUAL"))
        return FALSE;
    if (0==g_strcmp0(name, "M_COVR"))
        return FALSE;
    if (0==g_strcmp0(name, "M_NPUB"))
        return FALSE;
    */


    _doPick = FALSE;
    //if (TRUE == S52_GL_begin(_doPick, FALSE)) {
        //g_timer_reset(_timer);


        //////////////////////////////////////////////
        // APP  .. update object (eg moving AIS, ..)
        //_app();


        //////////////////////////////////////////////
        // CULL .. remove object (eg outside view)
#ifdef S52_USE_PROJ
        projUV uv1, uv2;
        S52_GL_getPRJView(&uv1.v, &uv1.u, &uv2.v, &uv2.u);

        // convert extent to deg
        uv1 = S57_prj2geo(uv1);
        uv2 = S57_prj2geo(uv2);
        ext.s = uv1.v;
        ext.w = uv1.u;
        ext.n = uv2.v;
        ext.e = uv2.u;
#endif
        //_cull(ext);


        //////////////////////////////////////////////
        // DRAW .. render

        _drawObj(name);
        _drawText();


        // done rebuilding CS
        _doCS = FALSE;

        //S52_GL_end(_doPick, TRUE);
        S52_GL_end(TRUE);

        //gdouble sec = g_timer_elapsed(_timer, NULL);
        //PRINTF("%.0f msec (%i obj / %i cmd)\n", sec * 1000, _nobj, _ncmd);
        //g_print("%.0f msec (%i obj / %i cmd) renew = %i / %iB\n", sec * 1000, _nobj, _ncmd, _nrealloc, fobj.sz);
        //g_print("DRAW: %.0f msec\n", sec * 1000);

    }

g_static_mutex_unlock(&_mp_mutex);
return TRUE;
}

#endif

DLL int    STD S52_drawStr(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    // FIXME: check x,y

    return_if_null(colorName);
    return_if_null(str);

    S52_CHECK_MUTX;

    S52_GL_drawStrWin(pixels_x, pixels_y, colorName, bsize, str);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    // FIXME: check x,y,north

    if (0.5 < ABS(scale_z)) {
        PRINTF("zoom factor overflow (<0.5) [%f]\n", scale_z);
        g_static_mutex_unlock(&_mp_mutex);

        return FALSE;
    }

    // debug
    PRINTF("scale_x:%f, scale_y:%f, scale_z:%f, north:%f\n", scale_x, scale_y, scale_z, north);


    g_timer_reset(_timer);
    // FIXME: handle return FALSE case
    if (TRUE == S52_GL_begin(FALSE, FALSE)) {
        S52_GL_drawBlit(scale_x, scale_y, scale_z, north);
        S52_GL_end(FALSE);
    }
    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("DRAWBLIT: %.0f msec\n", sec * 1000);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL const char* STD S52_pickAt(double pixels_x, double pixels_y)
{
    // viewport
    int x;
    int y;
    int width;
    int height;

    //S52_extent ext;         // pick extent
    _extent ext;         // pick extent
    double s,w,n,e;         // used to save old view
    char     *name  = NULL;  // object's name at XY
    double    oldAA = 0.0;

    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;


    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    // debug
    PRINTF("pixels_x:%f, pixels_y:%f\n", pixels_x, pixels_y);
    // check bound
    if (FALSE == _validate_screenPos(&pixels_x, &pixels_y)) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    // if blending is ON the cursor pick will not work
    oldAA = S52_MP_get(S52_MAR_ANTIALIAS);
    S52_MP_set(S52_MAR_ANTIALIAS, FALSE);

    {   // compute pick view parameter

        // mouse has 'Y' down, opengl is up
        //y = _viewPort.y + _viewPort.height - y + 1;
        //pixels_y = _viewPort.y + _viewPort.height - pixels_y;
        {
            S52_GL_getViewPort(&x, &y, &width, &height);
            pixels_y = y + height - pixels_y;
        }

        // FIXME: check bound
        ext.N = pixels_y + 4;
        ext.S = pixels_y - 4;
        ext.E = pixels_x + 4;
        ext.W = pixels_x - 4;
        S52_GL_win2prj(&ext.W, &ext.S);
        S52_GL_win2prj(&ext.E, &ext.N);

        // save current view
        S52_GL_getPRJView(&s, &w, &n, &e);

        // set view of the pick (PRJ)
        // FIXME: setPRJview is snapped to the current ViewPort aspect ratio
        // so ViewPort value as to be set before PRJView
        S52_GL_setViewPort(pixels_x-4, pixels_y-4, 9, 9);
        S52_GL_setPRJView(ext.S, ext.W, ext.N, ext.E);
        //PRINTF("PICK PRJ EXTENT (swne): %f, %f  %f, %f \n", ext.s, ext.w, ext.n, ext.e);


#ifdef S52_USE_PROJ
        {   // extent prj --> geo
            // compute geographic filter extent
            projUV uv;
            uv.u = ext.W;
            uv.v = ext.S;
            uv = S57_prj2geo(uv);
            ext.W = uv.u;
            ext.S = uv.v;

            uv.u  = ext.E;
            uv.v  = ext.N;
            uv = S57_prj2geo(uv);
            ext.E = uv.u;
            ext.N = uv.v;

            //PRINTF("PICK LL EXTENT (swne): %f, %f  %f, %f \n", ext.s, ext.w, ext.n, ext.e);
        }
#endif

    }

    if (TRUE == S52_GL_begin(TRUE, FALSE)) {
        _nTotal = 0;
        _nCull  = 0;

        // filter out objects that don't intersec the pick view
        _cull(ext);
        //PRINTF("nbr of object culled: %i (%i)\n", _nCull, _nTotal);

        // render object that fall in the pick view
        //_draw(ext);
        //_draw(ext, S52_RAD_SUPP);
        //_draw(ext, S52_RAD_OVER);
        _draw();


        //*
        {   // FIXME: move this to _drawLast() some day
            int i = 0;

            // then draw the Mariners' Object on top of it
            for (i=0; i<N_OBJ_T; ++i) {
                GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][i];

                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                    // FIXME: debug if Mariner's Object outside 'view'
                    // are effectively culled via _cull()

                    if (FALSE == S52_GL_isSupp(obj)) {
                        //S52_GL_draw(obj);
                        S52_GL_draw(obj, NULL);

                        //if (TRUE == S52_PL_hasText(obj))
                        //    g_ptr_array_add(_textList, obj);
                        S52_GL_drawText(obj, NULL);

                    }
                }
            }
        }
        //*/


        S52_GL_end(FALSE);
    }

    // restore view
    // FIXME: setPRJview is snapped to the current ViewPort aspect ratio
    // so ViewPort value as to be set before PRJView
    S52_GL_setViewPort(x, y, width, height);
    S52_GL_setPRJView(s, w, n, e);

    name = S52_GL_getNameObjPick();
    PRINTF("OBJECT PICKED: %s\n", name);

    // replace original blending state
    S52_MP_set(S52_MAR_ANTIALIAS, oldAA);

    g_static_mutex_unlock(&_mp_mutex);

    return name;
}

static int        _win2prj(double *pixels_x, double *pixels_y)
{
    // check bound
    //if (FALSE == _validate_screenPos(x, y))
    //    return FALSE;


    //PRINTF("mouse x:%f y:%f\n", *x, *y);

    // debug timming
    //return TRUE;

    //if (_doInit) {
    //    PRINTF("ERROR: libS52 not initialized --try S52_init() first\n");
    //    return FALSE;
    //}

    // FIXME: coordinate correction varie from cursor to cursor!
    int x;
    int y;
    int width;
    int height;

    S52_GL_getViewPort(&x, &y, &width, &height);

    *pixels_y  = height - *pixels_y - 1;
    *pixels_x += 1.35;

    //if (FALSE == S52_GL_win2prj(pixels_x, pixels_y, &dummy))
    if (FALSE == S52_GL_win2prj(pixels_x, pixels_y))
        return FALSE;

    return TRUE;
}

DLL int    STD S52_xy2LL(double *pixels_x, double *pixels_y)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    // check bound
    if (FALSE == _validate_screenPos(pixels_x, pixels_y)) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    if (FALSE == _win2prj(pixels_x, pixels_y)) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    projXY uv = {*pixels_x, *pixels_y};
    uv = S57_prj2geo(uv);
    *pixels_x = uv.u;
    *pixels_y = uv.v;

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_LL2xy(double *longitude, double *latitude)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    double xyz[3] = {*longitude, *latitude, 0.0};

    if (FALSE == S57_geo2prj3dv(1, xyz))
        return FALSE;

    S52_GL_prj2win(&xyz[0], &xyz[1]);

    *longitude = xyz[0];
    *latitude  = xyz[1];

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setView(double cLat, double cLon, double rNM, double north)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    // debug
    PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);


    //if ((view->cLat-view->rNM/60.0 <  -90.0)  ||
    //    (view->cLat+view->rNM/60.0 >   90.0)  ||
    //    (view->cLon-view->rNM/60.0 < -180.0)  ||
    //    (view->cLon+view->rNM/60.0 >  180.0)) {

    //if (((view->cLat <  -90.0) && (view->cLat >  90.0))  ||
    //    ((view->cLon < -180.0) && (view->cLon > 180.0))) {
    if (((cLat <  -90.0) && (cLat >  90.0))  ||
        ((cLon < -180.0) && (cLon > 180.0))) {

        PRINTF("WARNING: call fail - view out of bound\n");
        return FALSE;
    }

    // FIXME: overscale
    //if (rNM<MIN_RANGE || rNM>MAX_RANGE) {
    if (rNM < MIN_RANGE) {
        PRINTF("WARNING: OVERSCALE (%f)\n", rNM);
        rNM = MIN_RANGE;
        //return FALSE;
    }
    if (rNM > MAX_RANGE) {
        PRINTF("WARNING: OVERSCALE (%f)\n", rNM);
        rNM = MAX_RANGE;
        //return FALSE;
    }

    // FIXME: PROJ4 will explode here (INFINITY) for mercator
    //if (view->rNM > (90.0*60)) {
    if ((ABS(cLat)*60.0 + rNM) > (90.0*60)) {
    //if (rNM > (90.0*60)) {
        PRINTF("WARNING: rangeNM reset to 90*60 NM (%f)\n", rNM);
        //view->rNM = 90.0 * 60.0;
        return FALSE;
    }

    if ((north>=360.0) || (north<0.0)) {
        //PRINTF("WARNING: north clamped to [0..360[ (%f->%f)\n", view->rNM,  360.0 * ((int)view->north % 360));
        //north = 360.0 * ((int)view->north % 360);
        PRINTF("WARNING: reset north 0.0 (%f)\n", north);
        north = 0.0;
        //return FALSE;
    }

    S52_CHECK_MUTX;

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: S52_setView() fail, no cell loaded to project view .. use S52_loadCell() first\n");
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }


    // debug
    //PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);

    S52_GL_setView(cLat, cLon, rNM, north);

    // update local var view
    _view.cLat  = cLat;
    _view.cLon  = cLon;
    _view.rNM   = rNM;
    _view.north = north;

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    PRINTF("viewport: %i %i %i %i\n", pixels_x, pixels_y, pixels_width, pixels_height);

    //_validate_screenPos(&x, &y);

    // save this viewport
    //_viewPort.x      = pixels_x;
    //_viewPort.y      = pixels_y;
    //_viewPort.width  = pixels_width;
    //_viewPort.height = pixels_height;

    S52_GL_setViewPort(pixels_x, pixels_y, pixels_width, pixels_height);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

static int        _getCellsExt(_extent* ext)
{
    //unsigned int i;

    ext->S =  INFINITY;
    ext->W =  INFINITY;
    ext->N = -INFINITY;
    ext->E = -INFINITY;

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        return FALSE;
    }

    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // for now just skip this pseudo cell
        if (0 == S52_strncmp(MARINER_CELL, c->filename->str, S57_CELL_NAME_MAX_LEN))
            continue;
        if (0 == S52_strncmp(MARINER_CELL, c->filename->str, S57_CELL_NAME_MAX_LEN))
            continue;

        ext->S = (c->ext.S < ext->S) ? c->ext.S : ext->S;
        ext->W = (c->ext.W < ext->W) ? c->ext.W : ext->W;
        ext->N = (c->ext.N > ext->N) ? c->ext.N : ext->N;
        ext->E = (c->ext.E > ext->E) ? c->ext.E : ext->E;
    }

    return TRUE;
}

//DLL int    STD S52_getCellExtent(_cell *c, S52_extent *ext)
//DLL int    STD S52_getCellExtent(const char *filename, S52_extent *ext)
DLL int    STD S52_getCellExtent(const char *filename, double *S, double *W, double *N, double *E)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    if (NULL==S || NULL==W || NULL==N || NULL==E) {
        PRINTF("WARNING: NULL extent S,W,N,E\n");
        return FALSE;
    }
    S52_CHECK_MUTX;


    _extent ext;
    if (NULL == filename) {
        _getCellsExt(&ext);
        *S = ext.S;
        *W = ext.W;
        *N = ext.N;
        *E = ext.E;

        PRINTF("ALL EXT(lat/lon): %f, %f -- %f, %f\n", *S, *W, *N, *E);
    } else {
        //unsigned int idx = 0;
        gchar *fnm   = g_strdup(filename);
        gchar *fname = g_strstrip(fnm);

        // strip path
        //WARNING: g_basename() deprecated use g_path_get_basename()
        //gchar* g_path_get_basename(const gchar *file_name) G_GNUC_MALLOC;
        //const gchar *name = g_basename(fname);
        const gchar *name = g_path_get_basename(fname);

        for (guint idx=0; idx<_cellList->len; ++idx) {
            _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

            // check if allready loaded
            if (0 == S52_strncmp(name, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
                *S = c->ext.S;
                *W = c->ext.W;
                *N = c->ext.N;
                *E = c->ext.E;
                //PRINTF("%s: %f, %f, %f, %f\n", filename, ext->s, ext->w, ext->n, ext->e);

                // cell found
                break;
            }
        }

        g_free(fnm);
    }

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}



// -----------------------------------------------------
//
// Toggle Obj Class (some should be deprecated)
// Selecte object (other then DISPLAYBASE) to display
// when in User Selected mode
//
DLL int    STD S52_toggleObjClass(const char *className)
{
    S52_CHECK_INIT;

    return_if_null(className);

    S52_CHECK_MUTX;

    S52_objSup ret = S52_PL_toggleObjClass(className);
    if (S52_SUP_ERR == ret) {
        PRINTF("WARNING: can't toggle object: %s\n", className);

        g_static_mutex_unlock(&_mp_mutex);

        return FALSE;
    }

    g_static_mutex_unlock(&_mp_mutex);

    if (S52_SUP_ON  == ret)
        PRINTF("Supressing display of object: %s\n", className);
    else
        PRINTF("NOT supressing display of object: %s\n", className);


    return TRUE;
}

DLL int    STD S52_toggleObjClassON (const char *className)
{
    S52_CHECK_INIT;

    return_if_null(className);

    S52_CHECK_MUTX;

    S52_objSup supState = S52_PL_getObjClassState(className);
    if (S52_SUP_ERR == supState) {
        PRINTF("WARNING: can't toggle %s\n", className);

        g_static_mutex_unlock(&_mp_mutex);

        return -1;
    }

    g_static_mutex_unlock(&_mp_mutex);

    if (S52_SUP_OFF == supState) {
        S52_toggleObjClass(className);
    } else {

        return FALSE;
    }


    return TRUE;
}

DLL int    STD S52_toggleObjClassOFF(const char *className)
{
    S52_CHECK_INIT;

    return_if_null(className);

    S52_CHECK_MUTX;

    S52_objSup supState = S52_PL_getObjClassState(className);
    if (S52_SUP_ERR == supState) {
        PRINTF("WARNING: can't toggle %s\n", className);

        g_static_mutex_unlock(&_mp_mutex);

        return -1;
    }

    g_static_mutex_unlock(&_mp_mutex);

    if (S52_SUP_ON == supState) {
        S52_toggleObjClass(className);
    } else {

        return FALSE;
    }


    return TRUE;
}

DLL int    STD S52_getS57ObjClassSupp(const char *className)
{
    S52_CHECK_INIT;

    return_if_null(className);

    S52_CHECK_MUTX;

    S52_objSup supState = S52_PL_getObjClassState(className);
    if (S52_SUP_ERR == supState) {
        PRINTF("WARNING: can't toggle %s\n", className);
        return -1;
    }

    g_static_mutex_unlock(&_mp_mutex);

    if (S52_SUP_ON == supState)
        return TRUE;
    else
        return FALSE;
}

DLL int    STD S52_setS57ObjClassSupp(const char *className, int value)
{
    S52_CHECK_INIT;

    return_if_null(className);

    S52_CHECK_MUTX;

    S52_objSup supState = S52_PL_getObjClassState(className);
    if (S52_SUP_ERR == supState) {
        PRINTF("WARNING: can't toggle %s\n", className);
        g_static_mutex_unlock(&_mp_mutex);
        return -1;
    }

    if (TRUE==value  && S52_SUP_ON  == supState) {
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }
    if (FALSE==value && S52_SUP_OFF == supState) {
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }

    g_static_mutex_unlock(&_mp_mutex);

    S52_toggleObjClass(className);

    return TRUE;
}
// -----------------------------------------------------


//static GPtrArray *_cloneCellList(const char *plibName)
DLL int    STD S52_loadPLib(const char *plibName)
{
    S52_CHECK_INIT;

    // can't draw while new PLib is loading
    S52_CHECK_MUTX;

    // 1 - load / parse new PLb
    valueBuf PLibPath = {'\0'};
    if (NULL == plibName) {
        if (0 == S52_getConfig(CONF_PLIB, &PLibPath)) {
            PRINTF("default PLIB not found in .cfg (%s)\n", CONF_PLIB);
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        } else {
            if (TRUE == S52_PL_load(PLibPath)) {
                //g_string_append(_plibNameList, PLibPath);
                g_string_append_printf(_plibNameList, ",%s", PLibPath);
            } else {
                g_static_mutex_unlock(&_mp_mutex);
                return FALSE;
            }
        }
    } else {
        if (TRUE == S52_PL_load(plibName)) {
            //g_string_append(_plibNameList, plibName);
            g_string_append_printf(_plibNameList, ",%s", plibName);
        } else {
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }
    }

    // FIXME: this is error prone!!
    // WARNING: must be in sync with struct _cell
    //
    // 2 - relink S57 objects to the new rendering rules (S52)
    // S52_linkS57(oldS52cellList) ==> newS52cellList
    {
        //unsigned int i,j,k;
        // clone cell: new rendering rule could place objects
        // on a different layer
        GPtrArray *newCellList = g_ptr_array_new();

        for (guint k=0; k<_cellList->len; ++k) {
            _cell *n = g_new0(_cell, 1);  // new
            _cell *c = (_cell*) g_ptr_array_index(_cellList, k);

            // clone this cell
            n->ext          = c->ext;
            n->filename     = c->filename;

            // no need to cull again
            //n->cullLightSec = c->cullLightSec;
            n->lights_sector= c->lights_sector;

            n->local        = c->local;


            // legend
            n->scale        = c->scale;         // compilation scale DSID:DSPM_CSCL or M_CSCL:CSCALE

            // DSID
            n->dsid_dunistr		 = c->dsid_dunistr;       // units for depth
            n->dsid_hunistr		 = c->dsid_hunistr;       // units for height
            n->dsid_csclstr		 = c->dsid_csclstr;       // scale  of display
            n->dsid_sdatstr		 = c->dsid_sdatstr;       // sounding datum
            n->dsid_vdatstr		 = c->dsid_vdatstr;       // vertical datum
            n->dsid_hdatstr		 = c->dsid_hdatstr;       // horizontal datum
            n->dsid_isdtstr		 = c->dsid_isdtstr;       // date of latest update
            n->dsid_updnstr		 = c->dsid_updnstr;       // number of latest update
            n->dsid_edtnstr		 = c->dsid_edtnstr;       // edition number
            n->dsid_uadtstr		 = c->dsid_uadtstr;       // edition date
            n->dsid_heightOffset = c->dsid_heightOffset;



            n->cscalestr	= c->cscalestr;     // scale
            //n->m_qualstr	= c->m_qualstr;     // data quality indicator
            n->catzocstr    = c->catzocstr;
            //n->m_accystr	= c->m_accystr;     // data quality indicator
            n->posaccstr    = c->posaccstr;
            n->sverdatstr	= c->sverdatstr;    // sounding datum
            n->vverdatstr	= c->vverdatstr;    // vertical datum
            n->valmagstr	= c->valmagstr;     // magnetic
            n->ryrmgvstr	= c->ryrmgvstr;
            n->valacmstr	= c->valacmstr;

            n->textList     = c->textList;
            g_ptr_array_set_size(n->textList, 0);
            n->objList_supp = c->objList_supp;
            g_ptr_array_set_size(n->objList_supp, 0);
            n->objList_over = c->objList_over;
            g_ptr_array_set_size(n->objList_over, 0);

            n->objClassList = c->objClassList;
            n->projDone     = c->projDone;
            //n->pjdst        = c->pjdst;


            {   // init render bin
                int i,j;
                for (i=0; i<S52_PRIO_NUM; ++i) {
                    for (j=0; j<N_OBJ_T; ++j)
                        n->renderBin[i][j] = g_ptr_array_new();
                }
            }


            // move geo from old cell to cloned cell
            // and relink S52 to S57 via new local PLib (oldPLib+newPLib)
            int i,j;
            for (i=0; i<S52_PRIO_NUM; ++i) {
                for (j=0; j<N_OBJ_T; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    unsigned int idx;
                    for (idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        _insertS57Obj(n, geo);

                        S52_CS_touch(n->local, geo);

                        // free old PLib stuff
                        // but NOT the geo and
                        // do not delete geo Display List - GL stuff
                        obj = S52_PL_delObj(obj);
                    }
                    g_ptr_array_free(rbin, TRUE);
                    //g_ptr_array_unref(rbin);
                }
            }

            // update _marinerCell
            if (0 == S52_strncmp(MARINER_CELL, c->filename->str, S57_CELL_NAME_MAX_LEN))
                _marinerCell = n;

            g_ptr_array_add(newCellList, n);

            // not a ref holder now
            // free old Array if this chart has light sector
            //if (NULL != c->lights_sector) {
            //    g_ptr_array_free(c->lights_sector, TRUE);
            //    //g_ptr_array_unref(c->lights_sector);
            //}

            g_free(c);
        }

        // reset the order of new cells
        g_ptr_array_sort(newCellList, _cmpCell);

        // purge old cell List
        g_ptr_array_free(_cellList, TRUE);
        //g_ptr_array_unref(_cellList);

        // reset global
        _cellList = newCellList;

    }

    // stuff that point to old cell are no longer valid
    _crntCell = NULL;

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

//-------------------------------------------------------
//
// FEEDBACK TO HIGHER UP MODULE OF INTERNAL STATE
//
DLL const char * STD S52_getPLibsIDList(void)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char * str = _plibNameList->str;

    g_static_mutex_unlock(&_mp_mutex);

    return str;
}

DLL const char * STD S52_getPalettesNameList(void)
// return a JSON array
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char *str = NULL;
    int palTblsz = S52_PL_getPalTableSz();
    //int i = 0;

    g_string_set_size(_paltNameList, 0);

    g_string_append_printf(_paltNameList, "[");

    for (int i=0; i<palTblsz; ++i) {
        //if (0 != _paltNameList->len)
        //    g_string_append_printf(_paltNameList, ",%s", (char*)S52_PL_getPalTableNm(i));
        //else
        //    g_string_append_printf(_paltNameList, "%s",  (char*)S52_PL_getPalTableNm(i));

        if (0 == i)
            g_string_append_printf(_paltNameList, "'%s'",  (char*)S52_PL_getPalTableNm(i));
        else
            g_string_append_printf(_paltNameList, ",'%s'", (char*)S52_PL_getPalTableNm(i));
    }

    g_string_append_printf(_paltNameList, "]");

    if (0 != _paltNameList->len)
        str = _paltNameList->str;

    g_static_mutex_unlock(&_mp_mutex);

    return str;
}

static GString *        _getMARINClassList()
{
    GString *classList = g_string_new(MARINER_CELL);

    unsigned i,j;
    for (i=0; i<S52_PRIO_NUM; ++i) {
        for (j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = _marinerCell->renderBin[i][j];
            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                const char *oname = S52_PL_getOBCL(obj);
                //if (NULL == S52_strstr(_marinerCell->objClassList->str, oname)) {
                if (NULL == S52_strstr(classList->str, oname)) {
                    g_string_append_printf(classList, ",%s", oname);
                }
            }
        }
    }

    return classList;
}

DLL const char * STD S52_getS57ObjClassList(const char *cellName)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char * str = NULL;
    //unsigned int idx;

    g_string_set_size(_objClassList, 0);

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        if (NULL == cellName) {
            if (0 == _objClassList->len)
                g_string_append_printf(_objClassList, "%s",  c->objClassList->str);
            else
                g_string_append_printf(_objClassList, ",%s", c->objClassList->str);
        } else {
            // check if filename is loaded
            if (0 == S52_strncmp(cellName, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
                // special case
                if (0 == S52_strncmp(MARINER_CELL, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
                    GString *classList = _getMARINClassList();

                    g_string_printf(_objClassList, "%s", classList->str);

                    g_string_free(classList, TRUE);

                    g_static_mutex_unlock(&_mp_mutex);

                    return _objClassList->str;
                }
                //if ((NULL!=c->objClassList) && (0!=c->objClassList->len)) {
                if (NULL != c->objClassList) {
                    g_string_printf(_objClassList, "%s,%s", c->filename->str, c->objClassList->str);

                    g_static_mutex_unlock(&_mp_mutex);

                    //return c->objClassList->str;
                    return _objClassList->str;
                }
            }
        }
    }

    if (0 != _objClassList->len)
        str = _objClassList->str;

    g_static_mutex_unlock(&_mp_mutex);

    return str;
}

DLL const char * STD S52_getObjList(const char *cellName, const char *className)
{
    S52_CHECK_INIT;

    return_if_null(cellName);
    return_if_null(className);

    S52_CHECK_MUTX;

    const char * str  = NULL;
    //unsigned int cidx = 0;     // cell index
    int header        = TRUE;

    g_string_set_size(_objClassList, 0);

    for (guint cidx=0; cidx<_cellList->len; ++cidx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, cidx);

        if (0 == S52_strncmp(cellName, c->filename->str, S57_CELL_NAME_MAX_LEN)) {
            unsigned i,j;
            for (i=0; i<S52_PRIO_NUM; ++i) {
                for (j=0; j<N_OBJ_T; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    //unsigned int idx;
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                        const char *oname = S52_PL_getOBCL(obj);
                        if (0 == S52_strncmp(className, oname, S52_LUP_NMLN)) {
                            if (header) {
                                g_string_printf(_objClassList, "%s,%s", cellName, className);
                                header = FALSE;
                            }
                            S57_geo *geo = S52_PL_getGeo(obj);
                            //  S57ID / geo / disp cat / disp prio
                            g_string_append_printf(_objClassList, ",%i:%c:%c:%i",
                                                   S57_getGeoID(geo),
                                                   S52_PL_getFTYP(obj),    // same as 'j', but in text equivalent
                                                   S52_PL_getDISC(obj),    //
                                                   S52_PL_getDPRI(obj));   // same as 'i'
                        }
                    }
                }
            }
            PRINTF("%s\n", _objClassList->str);
            str = _objClassList->str;
            g_static_mutex_unlock(&_mp_mutex);

            return str;
        }
    }

    g_static_mutex_unlock(&_mp_mutex);

    return NULL;
}

DLL const char * STD S52_getAttList(unsigned int S57ID)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // FIXME: put in array of s57id  (what to do if ENC is unloaded)
    // FIXME: find a more elegant way to traverse all object
    //        may be something like _travers(f())
    const char * str  = NULL;
    //unsigned int cidx = 0;     // cell index

    //g_string_set_size(_objClassList, 0);

    for (guint cidx=0; cidx<_cellList->len; ++cidx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, cidx);

        unsigned i,j;
        for (i=0; i<S52_PRIO_NUM; ++i) {
            for (j=0; j<N_OBJ_T; ++j) {
                GPtrArray *rbin = c->renderBin[i][j];
                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);
                    if (S57ID == S57_getGeoID(geo)) {
                        g_static_mutex_unlock(&_mp_mutex);
                        //str = S57_getAtt(geo);
                        return S57_getAtt(geo);
                    }
                }
            }
        }
    }

    g_static_mutex_unlock(&_mp_mutex);

    return str;
}

DLL const char * STD S52_getCellNameList(void)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char * str = NULL;
    //unsigned int idx;

    g_string_set_size(_cellNameList, 0);

    g_string_append_printf(_cellNameList, "[");

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        //if (0 == _cellNameList->len)
        //    g_string_append_printf(_cellNameList, "%s",  c->filename->str);
        //else
        //    g_string_append_printf(_cellNameList, ",%s", c->filename->str);

        if (0 == _cellNameList->len)
            g_string_append_printf(_cellNameList, "'%s'",  c->filename->str);
        else
            g_string_append_printf(_cellNameList, ",'%s'", c->filename->str);
    }

    if (0 != _cellNameList->len)
        str = _cellNameList->str;

    g_static_mutex_unlock(&_mp_mutex);

    return str;
}
//
//-------------------------------------------------------
//

DLL int    STD S52_setRGB(const char *colorName, unsigned char  R, unsigned char  G, unsigned char  B)
{
    S52_CHECK_INIT;

    return_if_null(colorName);

    S52_CHECK_MUTX;

    // debug
    PRINTF("colorName:%s, R:%c, G:%c, B:%c\n", colorName, R, G, B);

    S52_PL_setRGB(colorName, R, G, B);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B)
{
    S52_CHECK_INIT;

    return_if_null(colorName);
    return_if_null(R);
    return_if_null(G);
    return_if_null(B);

    S52_CHECK_MUTX;

    // debug
    PRINTF("colorName:%s, R:%#lX, G:%#lX, B:%#lX\n", colorName, (long unsigned int)R, (long unsigned int)G, (long unsigned int)B);

    S52_PL_getRGB(colorName, R, G, B);

    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setRADARCallBack(S52_RADAR_cb cb)
{
    // debug
    PRINTF("cb%#lX\n", (long unsigned int)cb);

    _RADAR_cb = cb;

    return TRUE;
}

static int        _setAtt(S57_geo *geo, const char *listAttVal)
// must be in name/value pair,
// FIXME: is this OK .. use '---' for a NULL value for any attribute name
{
    return_if_null(geo);
    return_if_null(listAttVal);

    gchar delim[4] = {":,\0"};

#ifdef S52_USE_GLIB2
    // will split "" into NULL == attvalL[0]
    // DEVHELP: As a special case, the result of splitting the empty string "" is an empty vector,
    // not a vector containing a single string. The reason for this special case is that being able
    // to represent a empty vector is typically more useful than consistent handling of empty elements.
    // If you do need to represent empty elements, you'll need to check for the empty string before calling
    gchar** attvalL = NULL;
    if (listAttVal[0] == '[')
        attvalL = g_strsplit_set(listAttVal, ",[]", 0);  // can't handle UTF-8, check g_strsplit() if needed
    else
        attvalL = g_strsplit_set(listAttVal, delim, 0);  // can't handle UTF-8, check g_strsplit() if needed
#else
    // FIXME: this is broken
    gchar** attvalL = g_strsplit(listAttVal, delim, 0);
#endif

    gchar** freeL   = attvalL;

    {   // check that strings comme in pair
        // the other case (ie '"","bla","bla",""') will be catched
        // in the next while
        int i = 0;
        while (NULL != attvalL[i]) {
            //if ('\0' == *attvalL[i]) {
            ++i;
        }

        // AIS messup ship's name some time
        if (i%2 != 0) {
            PRINTF("WARNING: unbalanced att/val pair, _seAtt() fail [%s]\n", listAttVal);
            //g_assert(0);

            g_strfreev(freeL);
            return FALSE;
        }

        // clear the trailling ']'
        /*
        if ((i>0) && (listAttVal[0]=='[')) {
            char *str = attvalL[i-1];
            int   n   = strlen(str);

            if (str[n-1] == ']') {
                str[n-1] = '\n';
            } else {
                PRINTF("WARNING: unbalanced JSON att/val pair, _seAtt() fail [%s]\n", listAttVal);
                g_strfreev(freeL);
                return FALSE;
            }
        }
        */
    }

    attvalL = freeL;
    while (NULL != attvalL[0]) {
        // delim at the beggining
        if ('\0' == *attvalL[0]) {
            attvalL += 1;
            continue;
        }

        // delim at the end
        // what case does that solve! ("bla:,bla:bla")
        // maybe case of unbalanced att/val pair
        // but that case is trapped by the code above
        if ('\0' == *attvalL[1]) {
            PRINTF("WARNING: mixed up '\0' val, the rest of list fail [%s]\n", listAttVal);
            g_strfreev(freeL);
            return FALSE;
            //attvalL += 1;
            //continue;
        }


        if (NULL != attvalL[1]) {
            const char *propName  = attvalL[0];
            const char *propValue = attvalL[1];

            S57_setAtt(geo, propName, propValue);

            attvalL += 2;

        } else {
            PRINTF("WARNING: unbalanced att/val pair (%s, %s, %s)\n", listAttVal, attvalL[0], attvalL[1]);
            g_assert(0);

            g_strfreev(freeL);
            return FALSE;
        }
    }

    //FIXME: is this glib-1 compat!
    g_strfreev(freeL);

    return TRUE;
}

static int        _setExt(S57_geo *geo, unsigned int xyznbr, double *xyz)
{
    //S52_extent ext = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    _extent ext = {INFINITY, INFINITY, -INFINITY, -INFINITY};

    guint   i   = 0;
    //guint   npt = 0;
    //double *ppt = NULL;

    //if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
    //    return FALSE;

    for (i=0; i<xyznbr; ++i) {
        // longitude
        ext.W = (ext.W < *xyz) ? ext.W : *xyz;
        ext.E = (ext.E > *xyz) ? ext.E : *xyz;

        // latitude
        ++xyz;
        ext.S = (ext.S < *xyz) ? ext.S : *xyz;
        ext.N = (ext.N > *xyz) ? ext.N : *xyz;

        // Z
        ++xyz;

        // next pt
        ++xyz;
    }

    S57_setExt(geo, ext.W, ext.S, ext.E, ext.N);

    return TRUE;
}

static int        _isObjNameValid(S52ObjectHandle obj, const char *objName)
// return TRUE if obj is valid else FALSE
{
    // FIXME: what if objName is anonymous! .. any object

    //S52_obj *obj = _isObj(_marinerCell, (S52_obj *)objH);

    //return_if_null(obj);
    //if (NULL == obj)
    //    return FALSE;

    S57_geo *geo = S52_PL_getGeo(obj);
    if (0 != S52_strncmp(objName, S57_getName(geo), 6))
        return FALSE;

    return TRUE;
}

struct _user_data {
    //GPtrArray   *objList_supp;
    //GPtrArray   *objList_over;
    unsigned int S57ID;
    S52_obj     *obj;
};
struct _user_data udata;

static void       _compS57ID(gpointer data, gpointer user_data)
{
    struct _user_data *udata  = (struct _user_data*) user_data;

    // if allready found do nothing
    if (NULL == udata->obj) {
        S52_obj *obj = (S52_obj*) data;
        S57_geo *geo = S52_PL_getGeo(obj);

        if (udata->S57ID == S57_getGeoID(geo)) {
            udata->obj = obj;
        }
    }
}


static S52_obj   *_getS52obj(unsigned int S57ID)
// traverse all object (again .. FIXME: find a unified way to do that at
// a single place in the code)
{
    /*
    //int cidx = 0;
    for (guint cidx=0; cidx<_cellList->len; ++cidx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, cidx);

        unsigned i,j;
        for (i=0; i<S52_PRIO_NUM; ++i) {
            for (j=0; j<N_OBJ_T; ++j) {
                GPtrArray *rbin = c->renderBin[i][j];
                //unsigned int idx;
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);
                    if (S57ID == S57_getGeoID(geo))
                        return obj;
                }
            }
        }
    }
    */

    struct _user_data  udata;
    udata.S57ID    = S57ID;
    udata.obj      = NULL;
    unsigned int i = 0;
    unsigned int n = _cellList->len-1;
    for (i=n; i>0; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        g_ptr_array_foreach (c->objList_supp, (GFunc)_compS57ID, &udata);
        g_ptr_array_foreach (c->objList_over, (GFunc)_compS57ID, &udata);

        // obj found - no need to go further
        if (NULL != udata.obj)
            return udata.obj;
    }

    // obj not found - search Mariners' Object List (mostly on layer 9)
    {
        //int i = 0;
        for (int i=0; i<N_OBJ_T; ++i) {
            // FIXME: not all on layer 9 (S52_PRIO_MARINR) !!
            GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][i];

            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                // if visible
                if (FALSE == S52_GL_isSupp(obj)) {
                    S57_geo *geo = S52_PL_getGeo(obj);

                    if (S57ID == S57_getGeoID(geo)) {
                        return obj;
                        //udata.obj = obj;
                        //break;
                    }
                }
            }
        }
    }

    return udata.obj;
}

DLL int    STD S52_dumpS57IDPixels(const char *toFilename, unsigned int S57ID, unsigned int width, unsigned int height)
// FIXME: handle S57ID == 0 (viewport dump)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    int ret = FALSE;

    if (0 == S57ID)
        S52_GL_dumpS57IDPixels(toFilename, NULL, width, height);
    else {
        S52_obj *obj = _getS52obj(S57ID);
        if (NULL != obj)
            ret = S52_GL_dumpS57IDPixels(toFilename, obj, width, height);
    }

    g_static_mutex_unlock(&_mp_mutex);

    return ret;
}

DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
                                    unsigned int xyznbr, double *xyz, const char *listAttVal)
{

    //S52_obj     *obj     = NULL;
    S57_geo     *geo     = NULL;
    unsigned int npt     = 0;
    double      *gxyz    = NULL;
    double     **ggxyz   = NULL;
    guint       *gxyznbr = NULL;

    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    // here we can load mariners' object event if no ENC are loaded yet
    if (NULL == _cellList || 0 == _cellList->len) {
        PRINTF("WARNING: no cell loaded .. no cell to project this object to\n");
        goto exit;
    }

    if (NULL == plibObjName) {
        PRINTF("WARNING: plibObjName NULL\n");
        goto exit;
    }

    // debug
    PRINTF("plibObjName:%s, objType:%i, xyznbr:%u, xyz:%p, listAttVal:<%s>\n",
            plibObjName, objType, xyznbr, xyz, (NULL==listAttVal)?"NULL":listAttVal );

    // is this really needed? (how about META!!)
    //if (NULL == xyz)
    //    return NULL;

    /*
    g_static_mutex_lock(&_mp_mutex);
    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return NULL;
    }
    */

    //
    //plibObjName
    // FIXME: check that "plibObjName" is a valid name


    if (0 != xyznbr) {

        // transfer and project xyz
        if (NULL != xyz) {
            double *dst  = NULL;
            double *src  = xyz;

            dst = gxyz = g_new(double, xyznbr*3);
            for (npt=0; npt<(xyznbr*3); ++npt) {
                int remain = npt % 3;

                // do basic check
                switch (remain) {
                case 0: *src = _validate_lon(*src); break;
                case 1: *src = _validate_lat(*src); break;
                case 2: break;
                default:
                    PRINTF("WARNING: remainer problem");
                    break;
                }

                *dst++ = *src++;
            }


#ifdef S52_USE_PROJ
            if (FALSE == S57_geo2prj3dv(xyznbr, gxyz)) {
                g_assert(0);
                //return NULL;
            }
#endif
        }
        else {
            // create an empty xyz buffer
            gxyz = g_new0(double, xyznbr*3);
            //gxyz[0] = 0.0;
            //gxyz[1] = 0.0;
            //xyz = gxyz;
            //PRINTF("WARNING: NULL xyz .. but xyznbr > 0\n");
            //return FALSE;
        }
    }

    if (S52_AREAS == objType) {
        gxyznbr    = g_new(unsigned int, 1);
        gxyznbr[0] = xyznbr;
        ggxyz      = g_new(double*, 1);
       *ggxyz      = gxyz;
    }

    switch (objType) {
        case S52__META: geo = S57_set_META();                  break;
        case S52_POINT: geo = S57_setPOINT(gxyz);              break;
        case S52_LINES: geo = S57_setLINES(xyznbr, gxyz);      break;
        case S52_AREAS: geo = S57_setAREAS(1, gxyznbr, ggxyz); break;

        default:
            PRINTF("WARNING: unknown object type\n");
            g_assert(0);
            //return NULL;
    }

    if (NULL == geo) {
        PRINTF("WARNING: S57 geo object build failed\n");
        g_assert(0);
        //return NULL;
    }

    S57_setName(geo, plibObjName);

    // extent deg decimal
    //S57_setExt(geo, xyz[0], xyz[1], xyz[0], xyz[1]);
    //S57_setExt(geo, -INFINITY, -INFINITY, INFINITY, INFINITY);

    // FIXME: compute extent
    //{
    //    double lonMIN = -170.0;
    //    double latMIN =  -80.0;
    //    double lonMAX =  170.0;
    //    double latMAX =   80.0;
    //    S57_setExt(geo, lonMIN, latMIN, lonMAX, latMAX);
    //}

    // for pastrk skip extent setup
    //if ((0==g_strcmp0(S57_getName(geo), "pastrk", 6)) || (0==g_strcmp0(S57_getName(geo), "afglow", 6)))
    //    S57_setCrntIdx(geo, 0);    // not really needed since geo struct is initialize to 0
    //else

    // full of coordinate
    if (NULL != xyz) {
        S57_setGeoSize(geo, xyznbr);
        _setExt(geo, xyznbr, xyz);
    }

    if (NULL != listAttVal)
        _setAtt(geo, listAttVal);

    // SCAMIN Mariners' object
    //S57_setScamin(geo, 0.0);

    // debug imidiatly destroy it
    //S57_doneData(geo);

    S52_obj *obj = S52_PL_newObj(geo);

    _insertS52Obj(_marinerCell, obj);

    // //S52_obj *obj = _cellAddGeo(_marinerCell, geo);

    // redo CS, because some object might have a CS command word (ex leglin)
    _doCS = TRUE;

    // debug
    //PRINTF("objH:%p, plibObjName:%s, ID:%i, objType:%i, xyznbr:%u, xyz:%p, listAttVal:<%s>\n",
    //        obj,     plibObjName, S57_getGeoID(geo), objType, xyznbr, gxyz,
    //       (NULL==listAttVal)?"NULL":listAttVal);

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return (S52ObjectHandle)obj;
}



// not implemented
//DLL S52ObjectHandle STD S52_setObjGeo(S52ObjectHandle objH, double *xyznbr, double *xyz)
//{
//    if (NULL == obj) return obj;
//    PRINTF("not implemented!\n");
//    return obj;
//}

// not implemented
//DLL S52ObjectHandle STD S52_addObjGeo(S52ObjectHandle objH, double *xyznbr, double *xyz)
//{
//    if (NULL == obj) return obj;
//    PRINTF("not implemented!\n");
//    return (S52ObjectHandle *)obj;
//}

#if 0
/*
DLL S52ObjectHandle STD S52_updMarObjGeo(S52ObjectHandle  objH, unsigned int xyznbr, double *xyz)

    S52_CHECK_INIT;
    S52_CHECK_MERC;

    return_if_null(objH);

    S52_CHECK_MUTX;

    PRINTF("xyznbr:%i\n", xyznbr);

    // synafter glow
    if ((TRUE==_isObjValid(objH, "afgves")) ||
        (TRUE==_isObjValid(objH, "afgshp")) ||
        (TRUE==_isObjValid(objH, "waypnt"))
       ) {

        guint    i   = 0;
        guint    npt = 0;
        double  *ppt = NULL;
        S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);

        if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
            goto unlock;

        if (xyznbr > npt) {
            PRINTF("WARNING: object size too big (%i) cliped to %i\n", xyznbr, npt);
            xyznbr = npt;

            g_assert(0);
        }

        // FIXME: validate position
        // FIX: PROJ will trap out of bound
        double *tmpppt = ppt;
        double *tmpxyz = xyz;
        for (i=0; i<(xyznbr*3); ++i)
            *tmpppt++ = *tmpxyz++;

        _setExt(geo, xyznbr, xyz);

        if (FALSE == S57_geo2prj3dv(xyznbr, ppt)) {
            objH = NULL;

            g_assert(0);
        } else
            S57_setCrntIdx(geo, xyznbr);

    } else {
        PRINTF("WARNING: only 'afglow' or 'agoshp' object can use this call \n");
        objH = NULL;

        g_assert(0);
    }

unlock:
    g_static_mutex_unlock(&_mp_mutex);

    return objH;
}
*/
#endif

DLL S52ObjectHandle STD S52_getMarObjH(unsigned int S57ID)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    S52_CHECK_MUTX;

    if (0 == S57ID) {
        PRINTF("WARNING: invalid S57ID [%i]\n", S57ID);
        g_static_mutex_unlock(&_mp_mutex);
        return NULL;
    }

    //unsigned i,j;
    for (int i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = _marinerCell->renderBin[i][j];
            //unsigned int idx;
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geo = S52_PL_getGeo(obj);
                if (S57ID == S57_getGeoID(geo)) {
                    g_static_mutex_unlock(&_mp_mutex);
                    return (S52ObjectHandle)obj;
                }
            }
        }
    }

    g_static_mutex_unlock(&_mp_mutex);

    return NULL;
}

static
    //S52ObjectHandle       _updateGeoNattVal(S52ObjectHandle objH, double *xyz, const char *listAttVal)
    S52ObjectHandle       _updateGeo(S52ObjectHandle objH, double *xyz)
{
    //S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)objH);
    S57_geo *geo = S52_PL_getGeo(obj);

    // debug
    /*
    {
        S52_obj *obj = (S52_obj *) objH;
        S57_geo *geo = S52_PL_getGeo(obj);
        PRINTF("plibObjName:%s, ID:%i, xyz:%#lX, listAttVal:<%s>\n",
               S57_getName(geo), S57_getGeoID(geo), (long unsigned int)xyz,
               (NULL==listAttVal)?"NULL":listAttVal);
    }
    */

    // update geo
    if (NULL != xyz) {
        guint    i   = 0;
        guint    npt = 0;
        double  *ppt = NULL;

        //if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        //    return FALSE;
        S57_getGeoData(geo, 0, &npt, &ppt);

        for (i=0; i<(npt*3); ++i)
            *ppt++ = *xyz++;
    }

    // update attribute
    //if (NULL != listAttVal)
    //    _setAtt(geo, listAttVal);

    return objH;
}

DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    PRINTF("objH:%p\n", objH);

    // validate this obj and remove if found
    S52_obj *obj = (S52_obj *)objH;
    if (NULL == _removeObj(_marinerCell, obj)) {
        PRINTF("WARNING: couldn't delete .. objH not in Mariners' Object List\n");
        g_static_mutex_unlock(&_mp_mutex);

        return objH;  // contrairy to other call return objH when fail
    }                 // so caller can further process it

    // queue obj for deletion in next APP() cycle
    g_ptr_array_add(_objToDelList, obj);

    g_static_mutex_unlock(&_mp_mutex);

    return (S52ObjectHandle)NULL;
}

DLL S52ObjectHandle STD S52_toggleDispMarObj(S52ObjectHandle  objH)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;
    /*
    g_static_mutex_lock(&_mp_mutex);
    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return NULL;
    }
    */

    S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)objH);
    if (NULL != obj) {

        //S52_objSup supState = S52_PL_toggleObjSUP(obj);

        S57_geo *geo = S52_PL_getGeo(obj);

        if (TRUE == S57_getSup(geo))
            S57_setSup(geo, FALSE);
        else
            S57_setSup(geo, TRUE);
    }

    g_static_mutex_unlock(&_mp_mutex);

    //if (S52_SUP_ERR == supState)
    //    return NULL;

    return objH;
}

DLL S52ObjectHandle STD S52_newCLRLIN(int catclr, double latBegin, double lonBegin, double latEnd, double lonEnd)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    // debug
    PRINTF("catclr:%i, latBegin:%f, lonBegin:%f, latEnd:%f, lonEnd:%f\n",
            catclr,    latBegin,    lonBegin,    latEnd,    lonEnd);

    if (0!=catclr && 1!=catclr && 2!=catclr) {
        PRINTF("WARNING: 'catclr' must be 0 or 1 or 2 (%i).. reset to 0\n", catclr);
        catclr = 0;
    }

    latBegin = _validate_lat(latBegin);
    lonBegin = _validate_lon(lonBegin);
    latEnd   = _validate_lat(latEnd);
    lonEnd   = _validate_lon(lonEnd);

    {
        char attval[256];
        SPRINTF(attval, "catclr:%i", catclr);
        double xyz[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};


        S52ObjectHandle clrlin = S52_newMarObj("clrlin", S52_LINES, 2, xyz, attval);

        return clrlin;
    }
}

//DLL S52ObjectHandle STD S52_iniLEGLIN(int select, double plnspd, double latBegin, double lonBegin, double latEnd, double lonEnd)
//DLL S52ObjectHandle STD S52_iniLEGLIN(int select, double plnspd, double wholinDist, double latBegin, double lonBegin, double latEnd, double lonEnd)
DLL S52ObjectHandle STD S52_newLEGLIN(int select, double plnspd, double wholinDist,
                                      double latBegin, double lonBegin, double latEnd, double lonEnd,
                                      S52ObjectHandle previousLEGLIN)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    //S52_CHECK_MUTX; // mutex in S52_newMarObj()

    // debug
    PRINTF("select:%i, plnspd:%f, wholinDist:%f, latBegin:%f, lonBegin:%f, latEnd:%f, lonEnd:%f\n",
            select,    plnspd,    wholinDist,    latBegin,    lonBegin,    latEnd,    lonEnd);

    // validation
    if (1!=select && 2!=select) {
        PRINTF("WARNING: 'select' must be 1 or 2 (%i).. reset to 1\n", select);
        select = 1;
    }

    // FIXME: validate distance; wholinDist is not greater then start-end
    // FIXME: validate float; plnspd, wholinDist

    latBegin = _validate_lat(latBegin);
    lonBegin = _validate_lon(lonBegin);
    latEnd   = _validate_lat(latEnd);
    lonEnd   = _validate_lon(lonEnd);


    {
        char   attval[256];
        double xyz[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};

        // check if same point
        if (latBegin==latEnd  && lonBegin==lonEnd) {
            PRINTF("WARNING: rejecting this leglin (same point)\n");
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }

        // FIXME: what happen if speed change!?
        if (0.0 != wholinDist)
            SPRINTF(attval, "select:%i,plnspd:%f,_wholin_dist:%f", select, plnspd, wholinDist);
        else
            SPRINTF(attval, "select:%i,plnspd:%f", select, plnspd);

        S52ObjectHandle leglin = S52_newMarObj("leglin", S52_LINES, 2, xyz, attval);

        // validate previous leg
        //if (NULL != previousLEGLIN) {
        // NOTE: FALSE == (VOID*)NULL or FALSE == (int) 0
        if (FALSE != previousLEGLIN) {
            S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)previousLEGLIN);
            if (NULL == obj) {
                PRINTF("WARNING: previousLEGLIN not a valid S52ObjectHandle\n");
                previousLEGLIN = FALSE;
            } else {
                //S52_PL_setNextLeg((S52_obj*)fromLeglin, (S52_obj*)toLeglin);
                S52_PL_setNextLeg((S52_obj*)previousLEGLIN, (S52_obj*)leglin);
            }

        }

        g_static_mutex_unlock(&_mp_mutex);

        return leglin;
    }
}

//DLL S52ObjectHandle* STD S52_iniOWNSHP(double length, double beam)
//DLL S52ObjectHandle STD S52_iniOWNSHP(double a, double b, double c, double d)
//DLL S52ObjectHandle STD S52_iniOWNSHP(const char *label)
DLL S52ObjectHandle STD S52_newOWNSHP(const char *label)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    /*
    if (FALSE != _ownshp) {
        PRINTF("WARNING: OWNSHP already initialize\n");
        //g_assert(0);
        //return NULL;
    }
    */

    char   attval[256];
    double xyz[3] = {_view.cLon, _view.cLat, 0.0};      // quiet the warning in S52_newMarObj()

    // debug
    //label = NULL;

    if (NULL == label) {
        SPRINTF(attval, "_vessel_label: ");
    } else {
        SPRINTF(attval, "_vessel_label:%s", label);
    }

    //_ownshp = S52_newMarObj("ownshp", S52_POINT, 1, xyz, attval);
    return S52_newMarObj("ownshp", S52_POINT, 1, xyz, attval);

    //return _ownshp;
}

DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    PRINTF("objH:%p, a:%f, b:%f, c:%f, d:%f\n", objH, a, b, c, d);

    //  shpbrd: Ship's breadth (beam),
    //  shplen: Ship's length over all,
    //  headng: Heading,
    //  cogcrs: Course over ground,
    //  sogspd: Speed over ground,
    //  ctwcrs: Course through water,
    //  stwspd: Speed through water,

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        goto exit;

    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        double shplen = a+b;
        double shpbrd = c+d;
        double shp_off_y = shplen / 2.0 - b;
        double shp_off_x = shpbrd / 2.0 - c;

        char attval[256];
        //double xyz[3] = {0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()

        //SPRINTF(attval, "shpbrd:%f,shplen:%f,cogcrs:0,sogspd:0,ctwcrs:0,stwspd:0", beam, length);
        SPRINTF(attval, "shpbrd:%f,shplen:%f,_shp_off_x:%f,_shp_off_y:%f",
                shpbrd, shplen, shp_off_x, shp_off_y);

        //_updateGeoNattVal(objH, NULL, attval);
        // update attribute
        S57_geo *geo = S52_PL_getGeo(objH);
        _setAtt(geo, attval);

    } else {
        PRINTF("WARNING: only OWNSHP and VESSEL can use this call \n");
        objH = FALSE;
        //return NULL;
    }

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return objH;
}

//DLL S52ObjectHandle STD S52_setPosition(S52ObjectHandle objH, double latitude, double longitude, double heading)
//DLL S52ObjectHandle STD S52_setVector(S52ObjectHandle objH, double course, double speed, int overGround)
DLL S52ObjectHandle STD S52_setVector(S52ObjectHandle objH,  int vecstb, double course, double speed)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // debug
    PRINTF("objH:%p, vecstb:%i, course:%f, speed:%f\n", objH, vecstb, course, speed);
    course = _validate_deg(course);
    // FIXME: validate float
    //speed  = _validate_int(speed);
    if (vecstb<0 || 3<=vecstb) {
        PRINTF("WARNING: 'vecstb' out of bound, reset to none (0)\n");
        vecstb = 0;
    }

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        goto exit;


    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        char   attval[256];
        //SPRINTF(attval, "cogcrs:%f,sogspd:%f,ctwcrs:%f,stwspd:%f,vecmrk:%i,vecstb:%i", course, speed, course, speed, vecmrk, vecstb);
        //SPRINTF(attval, "cogcrs:%f,sogspd:%f,ctwcrs:%f,stwspd:%f", course, speed, course, speed);
        SPRINTF(attval, "vecstb:%i,cogcrs:%f,sogspd:%f,ctwcrs:%f,stwspd:%f", vecstb, course, speed, course, speed);

        //_updateGeoNattVal(objH, NULL, attval);
        S57_geo *geo = S52_PL_getGeo(objH);
        _setAtt(geo, attval);

    } else {
        PRINTF("WARNING: can't setVector on this object (%p)\n", objH);
        objH = FALSE;
        //return NULL;
    }

exit:
    g_static_mutex_unlock(&_mp_mutex);

    // debug
    //PRINTF("fini\n");

    return objH;
}

//DLL S52ObjectHandle STD S52_iniPASTRK(int catpst,)
//DLL S52ObjectHandle STD S52_iniPASTRK(int catpst, unsigned int maxpts)
DLL S52ObjectHandle STD S52_newPASTRK(int catpst, unsigned int maxpts)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    PRINTF("catpst:%i\n", catpst);

    char attval[256];
    SPRINTF(attval, "catpst:%i", catpst);

    S52ObjectHandle pastrk = S52_newMarObj("pastrk", S52_LINES, maxpts, NULL, attval);

    return pastrk;
}

DLL S52ObjectHandle STD    _setPointPosition(S52ObjectHandle objH, double latitude, double longitude, double heading)
{
    //PRINTF("-0- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);
    //latitude  = _validate_lat(latitude);
    //longitude = _validate_lon(longitude);
    //heading   = _validate_deg(heading);

    char   attval[256];
    double xyz[3] = {longitude, latitude, 0.0};

    // update extent
    S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);
    _setExt(geo, 1, xyz);

    //PRINTF("-1- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);

#ifdef S52_USE_PROJ
    if (FALSE == S57_geo2prj3dv(1, xyz)) {
        objH = FALSE;
        return objH;
    }
#endif

    // FIXME: this look like a hack
    {  // set origine for VRM-EBL
        S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);
        if (0 == g_strcmp0("ownshp", S57_getName(geo))) {
            //_ownshp_lat = xyz[1];
            //_ownshp_lon = xyz[0];
            _ownshp_lat = latitude;
            _ownshp_lon = longitude;
        }
    }

    // optimisation (out of the blue): set 'orient' obj var directly
    S52_PL_setSYorient((S52_obj *)objH, heading);

        /*
        // update VRMEBL if ownshp move
        if (0 == g_strcmp0("ownshp", S57_getName(geo), 6)) {
            //unsigned int i = 0;
            GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][S52_LINES_T];
            for (guint i=0; i<rbin->len; ++i) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, i);
                S57_geo *geo = S52_PL_getGeo(obj);

                if ((0==g_strcmp0("ebline", S57_getName(geo), 6)) ||
                    (0==g_strcmp0("vrmark", S57_getName(geo), 6))
                   ) {

                    // only VRMEBL can have an attribure _setOrigin
                    GString *setOriginstr = S57_getAttVal(geo, "_setOrigin");
                    if (NULL != setOriginstr && 'N' == *setOriginstr->str) {
                        guint    npt    = 0;
                        double  *ppt    = NULL;
                        S57_getGeoData(geo, 0, &npt, &ppt);
                        ppt[0] = xyz[0];
                        ppt[1] = xyz[1];
                    }
                }
            }
        }
        */

    //PRINTF("-2- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);

    SPRINTF(attval, "headng:%f", heading);
    //_updateGeoNattVal(objH, xyz, attval);
    _updateGeo(objH, xyz);

    //S57_geo *geo = S52_PL_getGeo(objH);
    _setAtt(geo, attval);

    //S52_GL_setOWNSHP(obj, heading);

    //PRINTF("-3- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);

    return objH;
}


//DLL S52ObjectHandle STD S52_addPASTRKPosition(S52ObjectHandle objH, double latitude, double longitude, double time)
//DLL S52ObjectHandle STD S52_addPosition(S52ObjectHandle objH, double latitude, double longitude, double user_data)
DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // debug
    PRINTF("objH:%p, latitude:%f, longitude:%f, data:%f\n", objH, latitude, longitude, data);
    latitude  = _validate_lat(latitude);
    longitude = _validate_lon(longitude);
    //time      = _validate_min(time);

    S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)objH);
    if (NULL == obj) {
        PRINTF("WARNING: not a valid S52ObjectHandle\n");
        objH = FALSE;
        //goto unlock;
        g_static_mutex_unlock(&_mp_mutex);
        return objH;
    }

    S57_geo *geo = S52_PL_getGeo(obj);

    //if (0 == g_strcmp0("vessel", S57_getName(geo), 6))
    //    S52_PL_setTimeNow(obj);

    /*
    // re-compute extent
    {
        _extent ext;
        S57_getExt(geo, &ext.W, &ext.S, &ext.E, &ext.N);
        double xyz [3*3] = {longitude, latitude, 0.0, ext.W, ext.S, 0.0, ext.E, ext.N, 0.0};

        _setExt(geo, 3, xyz);
    }
    */

    // POINT
    if (POINT_T == S57_getObjtype(geo)) {
        if (NULL == _setPointPosition(objH, latitude, longitude, data)) {
            PRINTF("_setPointPosition() .. fini\n");
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }
    }
    else // LINE AREA
    {
        double   xyz[3] = {longitude, latitude, 0.0};
        guint    sz     = S57_getGeoSize(geo);

        guint    npt    = 0;
        double  *ppt    = NULL;

        if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
            //goto unlock;
        }

        // debug
        //PRINTF("len:%i, npt:%i\n", len, npt);
        //if (len >= npt) {
        //    PRINTF("WARNING: object full .. position not added\n");
        //    g_static_mutex_unlock(&_mp_mutex);
        //    return FALSE;
        //}

#ifdef S52_USE_PROJ
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            PRINTF("WARNING: S57_geo2prj3dv() fail\n");

            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }
#endif

        if (sz < npt) {
            ppt[sz*3 + 0] = xyz[0];
            ppt[sz*3 + 1] = xyz[1];
            ppt[sz*3 + 2] = data;
            S57_setGeoSize(geo, sz+1);
        } else {
            // if sz == npt, shift npt-1 coord - FIFO
            g_memmove(ppt, ppt+3, (npt-1) * sizeof(double) * 3);
            ppt[((npt-1) * 3) + 0] = xyz[0];
            ppt[((npt-1) * 3) + 1] = xyz[1];
            ppt[((npt-1) * 3) + 2] = data;
        }
    }

//unlock:
    g_static_mutex_unlock(&_mp_mutex);

    //PRINTF("-1- objH:%#lX, latitude:%f, longitude:%f, data:%f\n", (long unsigned int)objH, latitude, longitude, data);

    return objH;
}

//DLL S52ObjectHandle STD S52_iniVESSEL(int vescre, int vestat, const char *label)
//DLL S52ObjectHandle STD S52_iniVESSEL(int vescre, int vestat, int vecper, int vecmrk, int vecstb, const char *label)
//DLL S52ObjectHandle STD S52_iniVESSEL(int vescre, int vestat, int vecmrk, int vecstb, const char *label)
//DLL S52ObjectHandle STD S52_iniVESSEL(int vescre, int vestat, const char *label)
//DLL S52ObjectHandle STD S52_newVESSEL(int vescre, int vestat, const char *label)
//DLL S52ObjectHandle STD S52_newVESSEL(int vescre, const char *label)
DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    // debug
    //label = NULL;
    //PRINTF("vesrce:%i, vestat:%i, label:%s\n", vescre, vestat, label);
    PRINTF("vesrce:%i, label:%s\n", vesrce, label);

    // vescre: Vessel report source, 1 ARPA target, 2 AIS vessel report, 3 VTS report
    if (1!=vesrce && 2!=vesrce && 3!=vesrce) {
        PRINTF("WARNING: 'vescre' must be 1 or 2 or 3 .. reset to 2 (AIS)\n");
        vesrce = 2;
    }

    S52ObjectHandle vessel = 0x0;
    {
        char   attval[256];
        double xyz[3] = {0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()

        if (NULL == label) {
            SPRINTF(attval, "vesrce:%i,_vessel_label: ",vesrce);
        } else {
            SPRINTF(attval, "vesrce:%i,_vessel_label:%s", vesrce, label);
        }

        //S52ObjectHandle vessel = S52_newMarObj("vessel", S52_POINT_T, 1, NULL, attval);
        //S52ObjectHandle vessel = S52_newMarObj("vessel", S52_POINT, 1, xyz, attval);
        vessel = S52_newMarObj("vessel", S52_POINT, 1, xyz, attval);
        S52_PL_setTimeNow(vessel);
    }

    PRINTF("vessel objH: %lu\n", vessel);

    return vessel;
}

DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);
    return_if_null((void*)newLabel);  // what if we need to erase label!

    S52_CHECK_MUTX;

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = FALSE;
        goto exit;
    }

    // commented for debugging - clutter output in Android (logcat)
    //PRINTF("label:%s\n", newLabel);

    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        char attval[256];
        //SPRINTF(attval, "_vessel_label:%s", newLabel);

        if (NULL == newLabel) {
            SPRINTF(attval, "_vessel_label: ");
        } else {
            SPRINTF(attval, "[_vessel_label,%s]", newLabel);
        }
        //_updateGeoNattVal(objH, NULL, attval);
        S57_geo *geo = S52_PL_getGeo(objH);
        _setAtt(geo, attval);


        // change of text label - text need to be reparsed
        // BUG: glBufferData not deleted!
        // FIX: no BufferData()
        S52_PL_resetParseText((S52_obj *)objH);
    } else {
        PRINTF("WARNING: not a 'ownshp' or 'vessel' object\n");
        objH = FALSE;
        //return NULL;
    }

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return objH;

}

DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // debug
    //PRINTF("vesselSelect:%i, vestat:%i, vesselTurn:%i\n", vesselSelect, vestat, vesselTurn);

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        goto exit;

    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        char  attval[60] = {'\0'};
        char *attvaltmp  = attval;
        S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);

        // validate vesselSelect:
        if (0!=vesselSelect && 1!=vesselSelect && 2!=vesselSelect) {
            PRINTF("WARNING: 'vesselSelect' must be 0, 1 or 2 .. reset to 1 (selected)\n");
            vesselSelect = 1;
        }
        if (1 == vesselSelect)
            SPRINTF(attvaltmp, "_vessel_select:Y,");
        if (2 == vesselSelect)
            SPRINTF(attvaltmp, "_vessel_select:N,");

        attvaltmp += S52_strlen(attvaltmp);

        // validate vestat (Vessel Status): 1 AIS active, 2 AIS sleeping
        if (0!=vestat && 1!=vestat && 2!=vestat) {
            PRINTF("WARNING: 'vestat' must be 0, 1 or 2 .. reset to 1\n");
            vestat = 1;
        }
        if (1==vestat || 2==vestat ) {
            SPRINTF(attvaltmp, "vestat:%i,", vestat);
            // FIXME: _doCS to get the new text (and prio)
        }
        attvaltmp += S52_strlen(attvaltmp);

        SPRINTF(attvaltmp, "_vessel_turn:%i", vesselTurn);

        _setAtt(geo, attval);

    } else {
        PRINTF("WARNING: not a 'ownshp' or 'vessel' object\n");
        objH = FALSE;
    }

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return objH;
}


//DLL int           STD S52_iniCSYMB(void)
DLL int             STD S52_newCSYMB(void)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    const char   *attval = NULL;
    //S52ObjectHandle csymb  = NULL;
    double        pos[3] = {0.0, 0.0, 0.0};

    if (FALSE == _iniCSYMB) {
        PRINTF("WARNING: CSYMB allready initialize\n");
        return FALSE;
    }

    // FIXME: should it be global ?
    attval = "$SCODE:SCALEB10";
    _SCALEB10 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    attval = "$SCODE:SCALEB11";
    _SCALEB11 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    attval = "$SCODE:NORTHAR1";
    _NORTHAR1 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    attval = "$SCODE:UNITMTR1";
    _UNITMTR1 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    // all depth in S57 sould be in meter so this is not used
    //attval = "$SCODEUNITFTH1";
    //csymb  = S52_newObj("$CSYMB", S52_POINT_T, 1, pos, attval);



    //--- those symb are used for calibration ---

    // check symbol should be 5mm by 5mm
    attval = "$SCODE:CHKSYM01";
    _CHKSYM01 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    // symbol to be used for checking and adjusting the brightness and contrast controls
    attval = "$SCODE:BLKADJ01";
    _BLKADJ01 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    _iniCSYMB = FALSE;

    return TRUE;
}


//DLL int           STD S52_win2prj(double *x, double *y)
//DLL S52ObjectHandle STD S52_iniVRMEBL(int vrm, int ebl, int normalLineStyle, int ownshpCentered)
//DLL S52ObjectHandle STD S52_iniVRMEBL(int vrm, int ebl, int normalLineStyle)
//DLL S52ObjectHandle STD S52_iniVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin)
DLL S52ObjectHandle STD S52_newVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    // debug
    PRINTF("vrm:%i, ebl:%i, normalLineStyle:%i, setOrigin:%i\n", vrm, ebl, normalLineStyle, setOrigin);


    S52ObjectHandle vrmebl = FALSE;

    char attval[256];
    //SPRINTF(attval, "%s%c,%s%c,%s%c",
    //"_ownshpcentered:",  ((TRUE == ownshpCentered)      ? 'Y' : 'N'),

    if (TRUE == setOrigin) {
        SPRINTF(attval, "%s%c,%s%c,%s",
                "_normallinestyle:", ((TRUE == normalLineStyle)     ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl ) ? 'Y' : 'N'),
                "_setOrigin:Y"
               );
    } else {
        //SPRINTF(attval, "%s%c,%s%c,%s",
        SPRINTF(attval, "%s%c,%s%c",
                "_normallinestyle:", ((TRUE == normalLineStyle)     ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl ) ? 'Y' : 'N')
                //"_setOrigin:N"
               );
    }

    if (FALSE==vrm && FALSE==ebl) {
        PRINTF("WARNING: nothing to do\n");
        return FALSE;
    }

    //*
    // try to get some starting position that make sens
    double latA, lonA;
    //if (NULL == _ownshp) {
    if (INFINITY == _ownshp_lat) {
        latA = _view.cLat;
        lonA = _view.cLon;
    } else {
        latA = _ownshp_lat;
        lonA = _ownshp_lon;
    }
    //*/

    //double xyz[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()
    double xyz[6] = {lonA, latA, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()
    //double xyz[6] = {_view.cLon, _view.cLat, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()

    if (TRUE == ebl) {
        //vrmebl = S52_newMarObj("ebline", S52_LINES_T, 2, NULL, attval);
        vrmebl = S52_newMarObj("ebline", S52_LINES, 2, xyz, attval);
    } else {
        //double xyz[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()
        //double xyz[6] = {_view.cLon, _view.cLat, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()
        //vrmebl = S52_newMarObj("vrmark", S52_LINES_T, 2, NULL, attval);
        vrmebl = S52_newMarObj("vrmark", S52_LINES, 2, xyz, attval);
    }
    return vrmebl;
}

//DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double longitudeBprj, double latitudeBprj)
//DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double x, double y)
//DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y, int origin)
//DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y)
DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y, double *brg, double *rge)
{
    S52_CHECK_INIT;
    //S52_CHECK_MERC;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // user can put NULL
    //return_if_null(brg);
    //return_if_null(rge);
    //*brg = 0.0;
    //*rge = 0.0;

    // debug: this fire at each mouse move
    //PRINTF("x:%f, y:%f, origne:%i\n", x, y, origine);
    //PRINTF("pixels_x:%f, pixels_y:%f\n", pixels_x, pixels_y);

    /*
    g_static_mutex_lock(&_mp_mutex);
    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return NULL;
    }
    */

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        goto exit;

    if (TRUE != _isObjNameValid(objH, "ebline") && TRUE != _isObjNameValid(objH, "vrmark")) {
        PRINTF("WARNING: not a 'ebline' or 'vrmark' object\n");
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }

    double latA;
    double lonA;
    double latB;
    double lonB;

    S57_geo *geo           = S52_PL_getGeo((S52_obj *)objH);
    GString *_setOriginstr = S57_getAttVal(geo, "_setOrigin");

    // set origine
    if (NULL == _setOriginstr) {
        //if (FALSE == _ownshp) {
        if (INFINITY == _ownshp_lat) {
            latA = _view.cLat;
            lonA = _view.cLon;
        } else {
            latA = _ownshp_lat;
            lonA = _ownshp_lon;
        }

        // to prj
        double xyz[3] = {lonA, latA, 0.0};
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            g_static_mutex_unlock(&_mp_mutex);
            return FALSE;
        }

        lonA = xyz[0];
        latA = xyz[1];

        latB = pixels_y;
        lonB = pixels_x;
        _win2prj(&lonB, &latB);
    } else {
        if ('Y' == *_setOriginstr->str) {
            lonA = pixels_x;
            latA = pixels_y;
            _win2prj(&lonA, &latA);
            lonB = lonA;
            latB = latA;
        } else {
            guint    npt = 0;
            double  *ppt = NULL;
            S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);

            S57_getGeoData(geo, 0, &npt, &ppt);
            lonA = ppt[0];
            latA = ppt[1];

            lonB = pixels_x;
            latB = pixels_y;
            _win2prj(&lonB, &latB);
        }
    }

    // save cursor position
    _cursor_lat = latB;
    _cursor_lon = lonB;

    {
        //double xyz[6] = {longitudeA, latitudeA, 0.0, longitudeBprj, latitudeBprj, 0.0};
        double xyz[6] = {lonA, latA, 0.0, lonB, latB, 0.0};
        double dist   = sqrt(pow(xyz[3]-xyz[0], 2) + pow( xyz[4]-xyz[1], 2));
        //double deg    = atan2(xyz[3]-xyz[0], xyz[4]-xyz[1]) * RAD_TO_DEG;
        double deg   = ATAN2TODEG(xyz);
        //char   unit  = 'm';

        // debug
        //projXY uv     = {lonB, latB};
        //.uv = S57_prj2geo(uv);
        //printf("lat/long: %f, %f\n", uv.v, uv.u);

        // in metre bellow 1852 m
        //if (dist >  1852) {
        //    dist /= 1852;
        //    unit  = 'N';
        //}

        if (deg < 0)
            deg += 360;

        //printf("%.1f%c / %.1f deg\n", dist, unit, deg);


        if (NULL != brg)
            *brg = deg;
        if (NULL != rge)
            *rge = dist;

        /*
        if (NULL != _setOriginstr)
            _updateGeoNattVal(objH, xyz, "_setOrigin:N");
        else
            _updateGeoNattVal(objH, xyz, NULL);
        */

        _updateGeo(objH, xyz);

        if (NULL != _setOriginstr) {
            S57_geo *geo = S52_PL_getGeo(objH);
            _setAtt(geo, "_setOrigin:N");
        }


    }

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return objH;
}


#if 0
//DLL S52ObjectHandle STD S52_setRoute(unsigned int nLeg, S52ObjectHandle *pobjH)
DLL S52ObjectHandle STD S52_setRoute(unsigned int nLeg, S52ObjectHandle *pobjH)
{

    PRINTF("WARNING: DEPRECATED DO NOT USE (will return NULL in any case)\n");
    return NULL;


    unsigned int i = 0;
    S52ObjectHandle *pobjHtmp = NULL;

    S52_CHECK_INIT;
    S52_CHECK_MERC;

    //return_if_null(pobjH);

    // debug
    //PRINTF("nLeg:%i\n", nLeg);

    g_static_mutex_lock(&_mp_mutex);
    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return NULL;
    }

    /*
    if (TRUE != _isObjValid(objHfrom, "leglin") && TRUE != _isObjValid(objHto, "leglin")) {
        PRINTF("WARNING: not a 'leglin' object\n");
        g_static_mutex_unlock(&_mp_mutex);
        return NULL;
    }

    S52_PL_setNextLeg((S52_obj*)objHfrom, (S52_obj*)objHto);
    */

    // delete all previous wholin object
    /*
    if (0 != _wholin->len) {
        //unsigned int i = 0;

        for (guint i=0; i<_wholin->len; ++i) {
            S52_obj *o = (S52_obj *)g_ptr_array_index(_route, i);

            S52_delMarObj(o);
        }
        g_ptr_array_set_size(_wholin, 0);
    }
    */

    /*
    pobjHtmp = pobjH;
    for (i=0; i<(nLeg-1); ++i) {
        S52_obj *obj = (S52_obj*) *pobjHtmp++;

        //return_if_null(obj);

        // check if next
        //if (i >= nLeg-1)
        //    break;

        if (TRUE == _isObjValid(obj, "leglin")) {
            S57_geo *geoA          = S52_PL_getGeo(obj);
            GString *wholinDiststr = S57_getAttVal(geoA, "_wholin_dist");
            double   wholinDist    = (NULL == wholinDiststr) ? 0.0 : S52_atof(wholinDiststr->str) * 1852.0;

            if (0.0 != wholinDist) {
                char attval[256];
                SPRINTF(attval, "loctim:%s,usrmrk:%s", "auto wholin loctim", "auto wholin usrmrk");
                double orientB = 0.0;

                // find orient of next leglin
                {
                    S52_obj  *objB = (S52_obj*) *pobjHtmp;
                    S57_geo  *geoB = S52_PL_getGeo(objB);
                    double   *pptB = NULL;
                    guint     nptB = 0;

                    if (FALSE==S57_getGeoData(geoB, 0, &nptB, &pptB)) {
                        g_assert(0);
                        return FALSE;
                    }

                    //orientB = 90.0 - atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
                    orientB = ATAN2TODEG(pptB);
                    //orientB = atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
                    if (orientB < 0)
                        orientB += 360.0;
                }

                double *pptA = NULL;
                guint   nptA = 0;

                if (FALSE==S57_getGeoData(geoA, 0, &nptA, &pptA)) {
                    g_assert(0);
                    return FALSE;
                }

                double x  = pptA[3];
                double y  = pptA[4];
                double x1 = 0.0;
                double y1 = 0.0;
                double x2 = 0.0;
                double y2 = 0.0;
                //double orientA = 90.0 - atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;
                double orientA = ATAN2TODEG(pptA);
                //double orientA = atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;

                if (orientA < 0)
                    orientA += 360.0;

                S52_GL_movePoint(&x,  &y,  orientA + 180.0, wholinDist);
                x1 = x2 = x;
                y1 = y2 = y;
                S52_GL_movePoint(&x1, &y1, orientB,         wholinDist);
                S52_GL_movePoint(&x2, &y2, orientB + 180.0, wholinDist);

                projXY uv1 = {x1, y1};
                uv1 = S57_prj2geo(uv1);
                projXY uv2 = {x2, y2};
                uv2 = S57_prj2geo(uv2);


                double xyz[6] = {uv1.u, uv1.v, 0.0, uv2.u, uv2.v, 0.0};

                g_static_mutex_unlock(&_mp_mutex);
                S52ObjectHandle wholin = S52_newMarObj("wholin", S52_LINES_T, 2, xyz, attval);
                //S52ObjectHandle wholin = S52_newMarObj("wholin", S52_LINES_T, 2, xyz, NULL);
                g_static_mutex_lock(&_mp_mutex);


                {
                    S52ObjectHandle wholinOld = S52_PL_getWholin(obj);
                    if (NULL != wholinOld)
                        S52_delMarObj(wholinOld);
                    S52_PL_setWholin((S52_obj *)wholin);
                }

                // keep a ref for deletion
                //g_ptr_array_add(_wholin, wholin);

            }
        } else {
            PRINTF("WARNING: only LEGLIN can use this call (%s)\n", S57_getName(S52_PL_getGeo(obj)));
            g_static_mutex_unlock(&_mp_mutex);
            g_assert(0);
            return NULL;
        }
    }
    //*/

    ///*
    //g_ptr_array_set_size(_route, 0);

    //S57_geo *geo            = NULL;
    GString *wholin_diststr = NULL;
    pobjHtmp                = pobjH;
    for (i=0; i<(nLeg-1); ++i) {
        S52_obj *fromLeglin = (S52_obj*) *pobjHtmp++;
        S52_obj *toLeglin   = (S52_obj*) *pobjHtmp;

        //if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        //    goto exit;


        if ((TRUE == _isObjNameValid(fromLeglin, "leglin")) &&
            (TRUE == _isObjNameValid(toLeglin,   "leglin"))
            ) {


            /*
            geo = S52_PL_getGeo(obj);
            if (NULL != wholin_diststr) {
                char attval[80];
                SPRINTF(attval, "_prev_wholin_dist:%s", wholin_diststr->str);
                _setAtt(geo, attval);
            }
            //g_ptr_array_add(_route, obj);

            // save wholin_dist of this leg for insterting in attlist of the next leg
            wholin_diststr = S57_getAttVal(geo, "_wholin_dist");
            */

            S52_PL_setNextLeg((S52_obj*)fromLeglin, (S52_obj*)toLeglin);


        } else {
            PRINTF("WARNING: only LEGLIN can use this call \n");
            g_static_mutex_unlock(&_mp_mutex);
            return NULL;
        }

    }
    //*/

    g_static_mutex_unlock(&_mp_mutex);

    return pobjH;

}
#endif

#if 0
DLL int             STD S52_setCurveLeg(S52ObjectHandle fromLeglin, S52ObjectHandle toLeglin)
{
    //unsigned int i = 0;
    //S52ObjectHandle *pobjHtmp = NULL;

    S52_CHECK_INIT;
    S52_CHECK_MERC;

    //return_if_null(pobjH);

    // debug
    //PRINTF("nLeg:%i\n", nLeg);

    g_static_mutex_lock(&_mp_mutex);
    //  check if we are shuting down
    if (NULL == _marinerCell) {
        g_static_mutex_unlock(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH))
        goto exit;


    if (TRUE != _isObjNameValid(fromLeglin, "leglin") && TRUE != _isObjNameValid(toLeglin, "leglin")) {
        PRINTF("WARNING: not a 'leglin' object\n");
        g_static_mutex_unlock(&_mp_mutex);
        return FALSE;
    }

    S52_PL_setNextLeg((S52_obj*)fromLeglin, (S52_obj*)toLeglin);

    // debug
    PRINTF("fromLeglin:%#lX, toLeglin:%#lX\n", (long unsigned int)fromLeglin, (long unsigned int)toLeglin);

    // delete all previous wholin object
    /*
    if (0 != _wholin->len) {
        //unsigned int i = 0;

        for (guint i=0; i<_wholin->len; ++i) {
            S52_obj *o = (S52_obj *)g_ptr_array_index(_route, i);

            S52_delMarObj(o);
        }
        g_ptr_array_set_size(_wholin, 0);
    }
    */

    /*
    pobjHtmp = pobjH;
    for (i=0; i<nLeg; ++i) {
        S52_obj *obj = (S52_obj*) *pobjHtmp++;

        return_if_null(obj);

        // check if next
        if (i >= nLeg-1)
            break;

        if (TRUE == _isObjValid(obj, "leglin")) {
            S57_geo *geoA          = S52_PL_getGeo(obj);
            GString *wholinDiststr = S57_getAttVal(geoA, "_wholin_dist");
            double   wholinDist    = (NULL == wholinDiststr) ? 0.0 : S52_atof(wholinDiststr->str) * 1852.0;

            if (0.0 != wholinDist) {
                char attval[256];
                SPRINTF(attval, "loctim:%s,usrmrk:%s", "auto wholin loctim", "auto wholin usrmrk");
                double orientB = 0.0;

                // find orient of next leglin
                {
                    S52_obj  *objB = (S52_obj*) *pobjHtmp;
                    S57_geo  *geoB = S52_PL_getGeo(objB);
                    double   *pptB = NULL;
                    guint     nptB = 0;

                    if (FALSE==S57_getGeoData(geoB, 0, &nptB, &pptB)) {
                        g_assert(0);
                        return FALSE;
                    }

                    //orientB = 90.0 - atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
                    orientB = ATAN2TODEG(pptB);
                    //orientB = atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
                    if (orientB < 0)
                        orientB += 360.0;
                }

                double *pptA = NULL;
                guint   nptA = 0;

                if (FALSE==S57_getGeoData(geoA, 0, &nptA, &pptA)) {
                    g_assert(0);
                    return FALSE;
                }

                double x  = pptA[3];
                double y  = pptA[4];
                double x1 = 0.0;
                double y1 = 0.0;
                double x2 = 0.0;
                double y2 = 0.0;
                //double orientA = 90.0 - atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;
                double orientA = ATAN2TODEG(pptA);
                //double orientA = atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;

                if (orientA < 0)
                    orientA += 360.0;

                _movePoint(&x,  &y,  orientA + 180.0, wholinDist);
                x1 = x2 = x;
                y1 = y2 = y;
                _movePoint(&x1, &y1, orientB,         wholinDist);
                _movePoint(&x2, &y2, orientB + 180.0, wholinDist);

                projXY uv1 = {x1, y1};
                uv1 = S57_prj2geo(uv1);
                projXY uv2 = {x2, y2};
                uv2 = S57_prj2geo(uv2);


                double xyz[6] = {uv1.u, uv1.v, 0.0, uv2.u, uv2.v, 0.0};
                S52ObjectHandle wholin = S52_newMarObj("wholin", S52_LINES_T, 2, xyz, attval);
                //S52ObjectHandle wholin = S52_newMarObj("wholin", S52_LINES_T, 2, xyz, NULL);

                // keep a ref for deletion
                g_ptr_array_add(_wholin, wholin);
            }
        } else {
            PRINTF("WARNING: only LEGLIN can use this call (%s)\n", S57_getName(S52_PL_getGeo(obj)));
            g_assert(0);
            return NULL;
        }
    }
    //*/

    /*
    g_ptr_array_set_size(_route, 0);

    S57_geo *geo            = NULL;
    GString *wholin_diststr = NULL;
    pobjHtmp                = pobjH;
    for (i=0; i<nLeg; ++i) {
        S52_obj *obj = (S52_obj*) *pobjHtmp++;

        //return_if_null(obj);
        if (NULL == obj)
            break;

        if (TRUE == _isObjValid(obj, "leglin")) {


            geo = S52_PL_getGeo(obj);
            if (NULL != wholin_diststr) {
                char attval[80];
                SPRINTF(attval, "_prev_wholin_dist:%s", wholin_diststr->str);
                _setAtt(geo, attval);
            }
            g_ptr_array_add(_route, obj);

            // save wholin_dist of this leg for insterting in attlist of the next leg
            wholin_diststr = S57_getAttVal(geo, "_wholin_dist");


        } else {
            PRINTF("WARNING: only LEGLIN can use this call \n");
        }
    }
    */

exit:
    g_static_mutex_unlock(&_mp_mutex);

    return TRUE;
}
#endif




#ifdef S52_USE_DBUS

// ------------ DBUS API  -----------------------
//
// duplicate some S52.h, mostly used for testing Mariners' Object
// async command and thread (here dbus)
//
//

// FIXME: use GDBus (in Gio) instead (thread prob with low-level DBus API)

/*
#ifdef __cplusplus
extern "C" {
#endif

extern gboolean  update_cb();

#ifdef __cplusplus
}
#endif
*/

static DBusHandlerResult   _sendDBusMessage         (DBusConnection *dbus, DBusMessage *reply)
// send the reply && flush the connection
{
    dbus_uint32_t serial = 0;
    //dbus_uint32_t serial = 1;
    if (!dbus_connection_send(dbus, reply, &serial)) {
        fprintf(stderr, "_sendDBusMessage():Out Of Memory!\n");
        exit(1);
    }
    dbus_connection_flush(dbus);

    // free the reply
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult   _dbus_draw               (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    //dbus_int32_t  i   = 0;
    //double        d   = 0;
    dbus_int32_t    ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_draw();
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_draw() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_drawLast           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    //dbus_int32_t  i   = 0;
    //double        d   = 0;
    dbus_int32_t    ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_drawLast();
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_drawLast() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getMarinerParam    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    dbus_int32_t  i   = 0;
    double        ret = 0;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
        g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_getMarinerParam((S52MarinerParameter)i);
    PRINTF("S52_getMarinerParam() val: %i, return: %f\n", i, ret);
    // ret == 0 (false) if first palette
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_getMarinerParam() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_setMarinerParam    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    dbus_int32_t  i   = 0;
    double        d   = 0;
    dbus_int32_t  ret = FALSE;

    (void)user_data;

    //PRINTF("got S52_setMarinerParam msg !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_DOUBLE, &d, DBUS_TYPE_INVALID)) {
        g_print("received: %i, %f\n", i, d);
        //dbus_free(s);
        //g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_setMarinerParam((S52MarinerParameter)i, d);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_setMarinerParam() failed .. send a dbus error!\n");
        g_assert(0);
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getRGB             (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char *colorName;
    dbus_int32_t  ret = FALSE;
    unsigned char R;
    unsigned char G;
    unsigned char B;

    (void)user_data;

    dbus_error_init(&error);

    //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_DOUBLE, &d, DBUS_TYPE_INVALID)) {
    if (dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &colorName, DBUS_TYPE_INVALID)) {
        //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
        g_print("received: %s\n", colorName);
        //dbus_free(s);
        //g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_getRGB(colorName, &R, &G, &B);
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_getRGB() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    // ret
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // R
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &R)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // G
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &G)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // B
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &B)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_newMarObj          (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    (void)user_data;

    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    char         *plibObjName = NULL;
    dbus_int32_t  objType     = 0;
    dbus_uint32_t xyznbr      = 0;
    char        **str         = NULL;
    dbus_uint32_t strnbr      = 0;
    char         *listAttVal  = NULL;

    S52ObjectHandle objH      = FALSE;

    //double        xyz[3] = {5.5,6.6,7.7};
    //double       *pxyz   = xyz;
    //dbus_int32_t  x[3] = {5, 6, 7};
    //dbus_int32_t *pxyz   = x;
    //dbus_int32_t  nel    = 3;
    //double          x    = 3.0;
    //double          y    = 3.0;

    dbus_error_init(&error);

    // FIXME: gjs-DBus can't pass array of double
    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_STRING, &plibObjName,
                              DBUS_TYPE_INT32,  &objType,
                              DBUS_TYPE_UINT32, &xyznbr,
                              //DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE, &pxyz, &nel,   // broken
                              //DBUS_TYPE_ARRAY, DBUS_TYPE_INT32,  &pxyz, &nel,   // broken
                              DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,   &str,  &strnbr,    // OK
                              DBUS_TYPE_STRING, &listAttVal,
                              DBUS_TYPE_INVALID)) {

        double    *xyz = g_new0(double, xyznbr*3);
        unsigned int i = 0;
        char      **tmp = str;
        for (i=0; i<strnbr; i+=3) {
            xyz[i*3 + 0] = g_ascii_strtod(*str++, NULL);
            xyz[i*3 + 1] = g_ascii_strtod(*str++, NULL);
            xyz[i*3 + 2] = g_ascii_strtod(*str++, NULL);
            PRINTF("received: %f, %f, %f\n", xyz[i*3 + 0], xyz[i*3 + 1], xyz[i*3 + 2]);
        }


        // make the S52 call
        objH = S52_newMarObj(plibObjName, (S52ObjectType)objType, xyznbr, xyz, listAttVal);
        if (FALSE == objH) {
            PRINTF("FIXME: send a dbus error!\n");
            //g_assert(0);
        }
        g_free(xyz);
        g_strfreev(tmp);

    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    // ret
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT64, &objH)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

#if 0
static DBusHandlerResult   _dbus_signal_draw        (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    int             ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i\n", (long unsigned int)o, i);
        //dbus_free(s);
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_draw();
    if (FALSE == ret) {
        PRINTF("FIXME: send a dbus error!\n");
        g_assert(0);
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_signal_drawLast    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    //DBusMessage*    reply;
    //DBusMessageIter args;
    DBusError       error;
    int             ret = FALSE;

    (void)dbus;
    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i\n", (long unsigned int)o, i);
        //dbus_free(s);
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_drawLast();
    if (FALSE == ret) {
        PRINTF("FIXME: send a dbus error!\n");
        g_assert(0);
    }
    return DBUS_HANDLER_RESULT_HANDLED;

    /*
    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    return _sendDBusMessage(dbus, reply);
    */
}
#endif

static DBusHandlerResult   _dbus_setVESSELstate     (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    void *          objH   = NULL;
    dbus_int32_t    select = 0;
    dbus_int32_t    vestat = 0;
    dbus_int32_t    turn   = 0;
    dbus_int64_t    ret    = 0;

    (void)user_data;

    dbus_error_init(&error);

    //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT64, &o, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_DOUBLE, &objH,
                              DBUS_TYPE_INT32,  &select,
                              DBUS_TYPE_INT32,  &vestat,
                              DBUS_TYPE_INT32,  &turn,
                              DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i %i\n", (long unsigned int)o, sel, vestat);
    } else {
        g_print("ERROR:: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_setVESSELstate((S52ObjectHandle)objH, select, vestat, turn);
    //if (NULL == ret) {
    //    g_print("FIXME: S52_setVESSELstate() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getPLibsIDList     (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPLibsIDList();
    if (NULL == str) {
        g_print("FIXME: S52_getPLibsIDList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getPalettesNameList(DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPalettesNameList();
    if (NULL == str) {
        g_print("FIXME: S52_getPalettesNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_getCellNameList    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getCellNameList();
    if (NULL == str) {
        g_print("FIXME: S52_getCellNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getS57ObjClassList (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *cellName;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &cellName, DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getS57ObjClassList(cellName);
    if (NULL == str) {
        g_print("FIXME: S52_getS57ObjClassList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getObjList         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *cellName;
    char           *className;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &cellName,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // debug
    //PRINTF("%s,%s\n", cellName, className);

    // make the S52 call
    const char *str = S52_getObjList(cellName, className);
    if (NULL == str) {
        g_print("FIXME: S52_getObjList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getAttList         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    dbus_uint32_t   s57id  = 0;
    //dbus_int64_t    ret    = 0;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_UINT32, &s57id,
                               DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char *str = S52_getAttList(s57id);
    if (NULL == str) {
        g_print("FIXME: S52_getAttList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getS57ObjClassSupp (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *className;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // debug
    //PRINTF("_dbus_getS57ObjClassSupp\n");

    // make the S52 call
    dbus_int32_t ret = S52_getS57ObjClassSupp(className);
    //if (NULL == str) {
    //    g_print("FIXME: _dbus_getS57ObjClassSupp() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_setS57ObjClassSupp (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *className;
    dbus_int32_t    value;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INT32,  &value,
                               DBUS_TYPE_INVALID)) {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int32_t ret = S52_setS57ObjClassSupp(className, value);
    //if (NULL == str) {
    //    g_print("FIXME: _dbus_setS57ObjClassSupp() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_loadCell           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *str;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &str,
                               DBUS_TYPE_INVALID))
    {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadCell(str, NULL);
    if (FALSE == ret) {
        g_print("FIXME: S52_loadCell() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_loadPLib           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *str;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &str,
                               DBUS_TYPE_INVALID))
    {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadPLib(str);
    if (FALSE == ret) {
        g_print("FIXME: S52_loadPLib() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_dumpS57IDPixels    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *fname;
    dbus_uint32_t   s57id = 0;
    dbus_uint32_t   width = 0;
    dbus_uint32_t   height= 0;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &fname,
                               DBUS_TYPE_UINT32, &s57id,
                               DBUS_TYPE_UINT32, &width,
                               DBUS_TYPE_UINT32, &height,
                               DBUS_TYPE_INVALID))
    {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_dumpS57IDPixels(fname, s57id, width, height);
    if (FALSE == ret) {
        g_print("FIXME: S52_dumpS57IDPixels() return FALSE\n");
        //g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_selectCall         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_draw")) {
        return _dbus_draw(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_drawLast")) {
        return _dbus_drawLast(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getMarinerParam")) {
        return _dbus_getMarinerParam(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setMarinerParam")) {
        return _dbus_setMarinerParam(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getRGB")) {
        return _dbus_getRGB(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setVESSELstate")) {
        return _dbus_setVESSELstate(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getPLibsIDList")) {
        return _dbus_getPLibsIDList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getPalettesNameList")) {
        return _dbus_getPalettesNameList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getCellNameList")) {
        return _dbus_getCellNameList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getS57ObjClassList")) {
        return _dbus_getS57ObjClassList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getObjList")) {
        return _dbus_getObjList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getAttList")) {
        return _dbus_getAttList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getS57ObjClassSupp")) {
        return _dbus_getS57ObjClassSupp(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setS57ObjClassSupp")) {
        return _dbus_setS57ObjClassSupp(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_loadCell")) {
        return _dbus_loadCell(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_loadPLib")) {
        return _dbus_loadPLib(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_dumpS57IDPixels")) {
        return _dbus_dumpS57IDPixels(dbus, message, user_data);
    }


    //DBusDispatchStatus dbusDispStatus = dbus_connection_get_dispatch_status(dbus);



    // ----------------------------------------------------------------------------------------

    PRINTF("=== DBus msg not handled ===\n");
    PRINTF("member   : %s\n", dbus_message_get_member(message));
    PRINTF("sender   : %s\n", dbus_message_get_sender(message));
    PRINTF("signature: %s\n", dbus_message_get_signature(message));
    PRINTF("path     : %s\n", dbus_message_get_path(message));
    PRINTF("interface: %s\n", dbus_message_get_interface(message));

    if (0 == g_strcmp0(dbus_message_get_member(message), "Disconnected", 12)) {
        PRINTF("ERROR: received DBus msg member 'Disconnected' .. \n" \
               "DBus force exit if dbus_connection_set_exit_on_disconnect(_dbus, TRUE);!\n");


        //DBusMessage*    reply = dbus_message_new_method_return(message);;
        //DBusMessageIter args;

        // add the arguments to the reply
        //dbus_message_iter_init_append(reply, &args);

        //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
        //    fprintf(stderr, "Out Of Memory!\n");
        //    g_assert(0);
        //}

        //return _sendDBusMessage(dbus, reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // this will exit thread (sometime!)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int                 _initDBus()
{
    int ret;

    if (NULL != _dbus)
        return FALSE;

    PRINTF("starting DBus ..\n");

    dbus_g_thread_init();

    dbus_error_init(&_dbusError);
    _dbus = dbus_bus_get(DBUS_BUS_SESSION, &_dbusError);
    if (!_dbus) {
        g_warning("Failed to connect to the D-BUS daemon: %s", _dbusError.message);
        dbus_error_free(&_dbusError);
        return 1;
    }

    //if (!dbus_bus_name_has_owner(_dbus, S52_DBUS_OBJ_NAME, &_dbusError)) {
    //    g_warning("Name has no owner on the bus!\n");
    //    return EXIT_FAILURE;
    //}

    ret = dbus_bus_request_name(_dbus, S52_DBUS_OBJ_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &_dbusError);
    if (-1 == ret) {
        PRINTF("%s:%s\n", _dbusError.name, _dbusError.message);
        dbus_error_free(&_dbusError);
        return 1;
    } else {
        if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER == ret)
            PRINTF("dbus_bus_request_name() reply OK: DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER (%i)\n", ret);
        else
            PRINTF("dbus_bus_request_name() reply OK (%i)\n", ret);
    }

    dbus_connection_setup_with_g_main(_dbus, NULL);

    // do not exit on disconnect
    dbus_connection_set_exit_on_disconnect(_dbus, FALSE);

    // listening to messages from all objects as no path is specified
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_draw'",                &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_drawLast'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getRGB'",              &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setVESSELstate'",      &_dbusError);

    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPLibsIDList'",      &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPalettesNameList'", &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getCellNameList'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getS57ObjClassList'",  &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getObjList'",          &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getAttList'",          &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getS57ObjClassSupp'",  &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setS57ObjClassSupp'",  &_dbusError);

    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_loadCell'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_loadPLib'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_dumpS57IDPixels'",     &_dbusError);
    //dbus_bus_add_match(_dbus, "type='method_call',sender='org.ecs.dbus',member='S52_newMarObj'",       &_dbusError);

    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_draw'",          &_dbusError);
    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_drawLast'",      &_dbusError);
    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_setState'",      &_dbusError);

    PRINTF("%s:%s\n", _dbusError.name, _dbusError.message);

    //if (FALSE == dbus_connection_add_filter(bus, _DBusSignal, loop, NULL)) {
    //if (FALSE == dbus_connection_add_filter(_dbus, _setMarinerParam, NULL, NULL)) {
    if (FALSE == dbus_connection_add_filter(_dbus, _dbus_selectCall, NULL, NULL)) {
        PRINTF("fail .. \n");
        exit(0);
    }

    //g_main_loop_run (_loop);

    return TRUE;
}
#endif /* S52_USE_DBUS */


// -----------------------------------------------------------------
// listen to socket
//
#ifdef S52_USE_SOCK
#include <gio/gio.h>
#include "parson.h"

#define BLOCK 2048
static char  _result[BLOCK];
static char  _response[BLOCK];
static int   _request_id = 0;
static int   _strCursor  = 0;  // cursor for writing strings in _result[]

static gchar               _setErr(gchar *errmsg)
{
    //gchar errmsg[] = "key \"command\" not found";
    memcpy(_result, errmsg, strlen(errmsg));
    _result[strlen(errmsg) + 1] = '\0';

    return TRUE;
}

static int                 _encode(char *buffer, const char *frmt, ...)
{
    va_list argptr;
    va_start(argptr, frmt);
    int n = g_vsnprintf(buffer, BLOCK, frmt, argptr);
    va_end(argptr);

    //PRINTF("n:%i\n", n);

    if (n < (BLOCK-1)) {
        buffer[n]   = '\0';
        buffer[n+1] = '\0';
        return TRUE;
    } else {
        buffer[BLOCK-1] = '\0';
        PRINTF("g_vsnprintf(): fail - no space in buffer\n");
        return FALSE;
    }

}

static int                 _handle_method(GString *instr)
// call the correponding S52_* function named 'method'
// here 'method' meen function name (or command name)
// SL4A call it method since is OOP
{
    // FIXME: use btree for name/function lookup
    // -OR-
    // FIXME: order cmdName by frequency


    // debug
    //PRINTF("------------------\n");
    //PRINTF("instr->str:%s", instr->str);

    // JSON parser
    JSON_Value *val       = json_parse_string(instr->str);
    if (NULL == val) {
        PRINTF("ERROR: json_parse_string() failed (NULL) \n");
        return FALSE;
    }

    // init JSON Object
    JSON_Object *obj      = json_value_get_object(val);

    // get S52_* Command Name
    const char  *cmdName  = json_object_dotget_string(obj, "method");
    //PRINTF("JSON cmdName:%s\n", cmdName);

    // start work - fetch cmdName parameters
    JSON_Array *paramsArr = json_object_get_array(obj, "params");
    if (NULL == paramsArr)
        goto exit;

    // get the number of parameters
    size_t      count     = json_array_get_count (paramsArr);

    // FIXME: check param type
    // ...


    // ---------------------------------------------------------------------
    //
    // call command - return answer to caller
    //

    //extern DLL S52ObjectHandle STD S52_newOWNSHP(const char *label);
    if (0 == S52_strncmp(cmdName, "S52_newOWNSHP", strlen("S52_newOWNSHP"))) {
        const char *label = json_array_get_string (paramsArr, 0);
        if ((NULL==label) || (1!=count)) {
            _setErr("params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newOWNSHP(label);
        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //extern DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);
    if (0 == S52_strncmp(cmdName, "S52_newVESSEL", strlen("S52_newVESSEL"))) {
        if (2 != count) {
            _setErr("params 'vesrce'/'label' not found");
            goto exit;
        }

        int         vesrce = (int)json_array_get_number(paramsArr, 0);
        const char *label  =      json_array_get_string(paramsArr, 1);
        if (NULL == label) {
            _setErr("params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newVESSEL(vesrce, label);
        _encode(_result, "[%lu]", (long unsigned int *) objH);
        //PRINTF("objH: %lu\n", objH);

        goto exit;
    }

    // extern DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel);
    if (0 == S52_strncmp(cmdName, "S52_setVESSELlabel", strlen("S52_setVESSELlabel"))) {
        if (2 != count) {
            _setErr("params 'objH'/'newLabel' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        const char *label = json_array_get_string (paramsArr, 1);
        if (NULL == label) {
            _setErr("params 'label' not found");
            goto exit;
        }

        objH = S52_setVESSELlabel(objH, label);
        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //extern DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data);
    if (0 == S52_strncmp(cmdName, "S52_pushPosition", strlen("S52_pushPosition"))) {
        if (4 != count) {
            _setErr("params 'objH'/'latitude'/'longitude'/'data' not found");
            goto exit;
        }

        long unsigned int lui     = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH      = (S52ObjectHandle) lui;
        double          latitude  = json_array_get_number(paramsArr, 1);
        double          longitude = json_array_get_number(paramsArr, 2);
        double          data      = json_array_get_number(paramsArr, 3);

        objH  = S52_pushPosition(objH, latitude, longitude, data);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //extern DLL S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);
    if (0 == S52_strncmp(cmdName, "S52_setVector", strlen("S52_setVector"))) {
        if (4 != count) {
            _setErr("params 'objH'/'vecstb'/'course'/'speed' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH   = (S52ObjectHandle) lui;
        int             vecstb = (int) json_array_get_number(paramsArr, 1);
        double          course = json_array_get_number(paramsArr, 2);
        double          speed  = json_array_get_number(paramsArr, 3);

        objH  = S52_setVector(objH, vecstb, course, speed);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //extern DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);
    if (0 == S52_strncmp(cmdName, "S52_setDimension", strlen("S52_setDimension"))) {
        if (5 != count) {
            _setErr("params 'objH'/'a'/'b'/'c'/'d' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        double          a     = json_array_get_number(paramsArr, 1);
        double          b     = json_array_get_number(paramsArr, 2);
        double          c     = json_array_get_number(paramsArr, 3);
        double          d     = json_array_get_number(paramsArr, 4);

        objH  = S52_setDimension(objH, a, b, c, d);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;

    }

    //extern DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn);
    if (0 == S52_strncmp(cmdName, "S52_setVESSELstate", strlen("S52_setVESSELstate"))) {
        if (4 != count) {
            _setErr("params 'objH'/'vesselSelect'/'vestat'/'vesselTurn' not found");
            goto exit;
        }

        long unsigned int lui        = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH         = (S52ObjectHandle) lui;
        int             vesselSelect = (int) json_array_get_number(paramsArr, 1);
        int             vestat       = (int) json_array_get_number(paramsArr, 2);
        int             vesselTurn   = (int) json_array_get_number(paramsArr, 3);


        objH  = S52_setVESSELstate(objH, vesselSelect, vestat, vesselTurn);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }


    //extern DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);
    if (0 == S52_strncmp(cmdName, "S52_delMarObj", strlen("S52_delMarObj"))) {
        if (1 != count) {
            _setErr("params 'objH' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle   objH = (S52ObjectHandle) lui;
        objH = S52_delMarObj(objH);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    // FIXME: not all param paresed
    //extern DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
    //                                     unsigned int xyznbrmax, double *xyz, const char *listAttVal);
    if (0 == S52_strncmp(cmdName, "S52_newMarObj", strlen("S52_newMarObj"))) {
        if (3 != count) {
            _setErr("params 'plibObjName'/'objType'/'xyznbrmax' not found");
            goto exit;
        }

        const char *plibObjName = json_array_get_string(paramsArr, 0);
        int         objType     = (int) json_array_get_number(paramsArr, 1);
        int         xyznbrmax   = (int) json_array_get_number(paramsArr, 2);
        double     *xyz         = NULL;
        gchar      *listAttVal  = NULL;


        S52ObjectHandle objH = S52_newMarObj(plibObjName, objType, xyznbrmax, xyz, listAttVal);

        // debug
        //PRINTF("S52_newMarObj -> objH: %lu\n", (long unsigned int *) objH);

        _encode(_result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //extern DLL const char * STD S52_getPalettesNameList(void);
    if (0 == S52_strncmp(cmdName, "S52_getPalettesNameList", strlen("S52_getPalettesNameList"))) {
        const gchar *palListstr = S52_getPalettesNameList();

        _encode(_result, "%s", palListstr);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL const char * STD S52_getCellNameList(void);
    if (0 == S52_strncmp(cmdName, "S52_getCellNameList", strlen("S52_getCellNameList"))) {
        const gchar *cellNmListstr = S52_getCellNameList();

        _encode(_result, "%s", cellNmListstr);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL double STD S52_getMarinerParam(S52MarinerParameter paramID);
    if (0 == S52_strncmp(cmdName, "S52_getMarinerParam", strlen("S52_getMarinerParam"))) {
        if (1 != count) {
            _setErr("params 'paramID' not found");
            goto exit;
        }

        int paramID = (int) json_array_get_number(paramsArr, 0);

        double d = S52_getMarinerParam(paramID);

        _encode(_result, "[%f]", d);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val);
    if (0 == S52_strncmp(cmdName, "S52_setMarinerParam", strlen("S52_setMarinerParam"))) {
        if (2 != count) {
            _setErr("params 'paramID'/'val' not found");
            goto exit;
        }

        int paramID = (int) json_array_get_number(paramsArr, 0);
        double val  =       json_array_get_number(paramsArr, 1);
        double d = S52_setMarinerParam(paramID, val);

        _encode(_result, "[%f]", d);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_drawLast(void);
    if (0 == S52_strncmp(cmdName, "S52_drawLast", strlen("S52_drawLast"))) {
        int i = S52_drawLast();

        _encode(_result, "[%i]", i);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_draw(void);
    if (0 == S52_strncmp(cmdName, "S52_draw", strlen("S52_draw"))) {
        int i = S52_draw();

        _encode(_result, "[%i]", i);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);
    if (0 == S52_strncmp(cmdName, "S52_getRGB", strlen("S52_getRGB"))) {
        if (1 != count) {
            _setErr("params 'colorName' not found");
            goto exit;
        }

        const char *colorName  = json_array_get_string(paramsArr, 0);

        unsigned char R;
        unsigned char G;
        unsigned char B;
        int ret = S52_getRGB(colorName, &R, &G, &B);
        PRINTF("%i, %i, %i\n", R,G,B);
        //PRINTF("%c, %c, %c\n", R,G,B);

        if (TRUE == ret)
            _encode(_result, "[%i,%i,%i]", R, G, B);
        else
            _encode(_result, "[0]");

        //PRINTF("%s\n", _result);

        goto exit;
    }

    //extern DLL int    STD S52_setTextDisp(int dispPrioIdx, int count, int state);
    if (0 == S52_strncmp(cmdName, "S52_setTextDisp", strlen("S52_setTextDisp"))) {
        if (3 != count) {
            _setErr("params '&dispPrioIdx' / 'count' / 'state' not found");
            goto exit;
        }

        int dispPrioIdx = (int) json_array_get_number(paramsArr, 0);
        int count       = (int) json_array_get_number(paramsArr, 1);
        int state       = (int) json_array_get_number(paramsArr, 2);

        int ret = S52_setTextDisp(dispPrioIdx, count, state);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else
            _encode(_result, "[0]");

        //PRINTF("%s\n", _result);

        goto exit;
    }

    //extern DLL int    STD S52_getTextDisp(int dispPrioIdx);
    if (0 == S52_strncmp(cmdName, "S52_getTextDisp", strlen("S52_getTextDisp"))) {
        if (1 != count) {
            _setErr("params 'dispPrioIdx' not found");
            goto exit;
        }

        int dispPrioIdx = (int) json_array_get_number(paramsArr, 0);

        int ret = S52_getTextDisp(dispPrioIdx);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            if (-1 == ret)
                _encode(_result, "[-1]");
            else // FALSE
                _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        goto exit;
    }

    //extern DLL int    STD S52_loadCell        (const char *encPath,  S52_loadObject_cb loadObject_cb);
    if (0 == S52_strncmp(cmdName, "S52_loadCell", strlen("S52_loadCell"))) {
        if (1 != count) {
            _setErr("params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);

        int ret = S52_loadCell(encPath, NULL);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        goto exit;
    }

    //extern DLL int    STD S52_doneCell        (const char *encPath);
    if (0 == S52_strncmp(cmdName, "S52_doneCell", strlen("S52_doneCell"))) {
        if (1 != count) {
            _setErr("params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);
        int ret = S52_doneCell(encPath);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        goto exit;
    }

    return FALSE;

exit:
    json_value_free(val);
    return TRUE;

    // read command name
    /*
    gchar *cmdstr = g_strrstr(instr->str, "\"method\"");
    gchar cmdName[32];
    if (NULL != cmdstr) {
        int r = sscanf(cmdstr, "\"method\" : \"%31c", cmdName);
        if (0 == r) {
            _setErr("method name not found");
            return FALSE;
        }
    } else {
        _setErr("key 'method' not found");
        return FALSE;
    }
    */


    // need this to compile
    //gchar jsonarr[128];

    /*
    // read parameter array
    gchar *paramsstr = g_strrstr(instr->str, "\"params\"");
    gchar jsonarr[128];
    if (NULL != paramsstr) {
        int ret = sscanf(paramsstr, "\"params\" : [ %127c", jsonarr);
        if (0 == ret) {
            _setErr("array value for key 'params' not found");
            return FALSE;
        }
    } else {
        _setErr("key 'params' not found");
        return FALSE;
    }

    // {"id":0,"command":"S52_newOWNSHP","params":["test"]}
    //extern DLL S52ObjectHandle STD S52_newOWNSHP(const char *label);
    if (0 == S52_strncmp(cmdName, "S52_newOWNSHP", strlen("S52_newOWNSHP"))) {
        gchar label[128];
        int r = sscanf(jsonarr, "\"%127c", label);
        if (0 == r) {
            _setErr("params 'label' not found");
            return FALSE;
        }
        gchar  *cptr = label;
        while (*cptr != '\"') cptr++;
        *cptr = '\0';
        S52ObjectHandle objH  = S52_newOWNSHP(label);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }

    // {'id':0,'command':'S52_newVESSEL','vesrce':2,'label':'test'}
    //extern DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);
    if (0 == S52_strncmp(cmdName, "S52_newVESSEL", strlen("S52_newVESSEL"))) {
        int       vesrce = 0;
        gchar     label[128];

        // debug
        //PRINTF("%s", jsonarr);

        int r = sscanf(jsonarr, "%i , \"%127c", &vesrce, label);
        if (2 != r) {
            _setErr("params 'vesrce'/'label' not found");
            return FALSE;
        }
        gchar  *cptr = label;
        while (*cptr != '\"') cptr++;
        *cptr = '\0';
        S52ObjectHandle objH   = S52_newVESSEL(vesrce, label);

        _encode(_result, "[%p]", objH);

        // debug
        //PRINTF("%s", _result);

        return TRUE;
    }

    // {'id':0,'command':'S52_setVESSELlabel','objH':9064736,'newLabel':'test-2'}
    // extern DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel);
    if (0 == S52_strncmp(cmdName, "S52_setVESSELlabel", strlen("S52_setVESSELlabel"))) {
        S52ObjectHandle objH;
        gchar    label[128];
        int r = sscanf(jsonarr, "%p , \"%127c", &objH, label);
        if (2 != r) {
            _setErr("params 'objH'/'newLabel' not found");
            return FALSE;
        }
        gchar  *cptr = label;
        while (*cptr != '\"') cptr++;
        *cptr = '\0';

        objH  = S52_setVESSELlabel(objH, label);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }

    //extern DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data);
    if (0 == S52_strncmp(cmdName, "S52_pushPosition", strlen("S52_pushPosition"))) {
        S52ObjectHandle objH      ;
        double          latitude  ;
        double          longitude ;
        double          data      ;

        int r = sscanf(jsonarr, "%p , %lf , %lf , %lf", &objH, &latitude, &longitude, &data);
        if (4 != r) {
            _setErr("params 'objH'/'latitude'/'longitude'/'data' not found");
            return FALSE;
        }


        objH  = S52_pushPosition(objH, latitude, longitude, data);

        _encode(_result, "[%p]", objH);
        return TRUE;
    }

    //extern DLL S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);
    if (0 == S52_strncmp(cmdName, "S52_setVector", strlen("S52_setVector"))) {
        S52ObjectHandle objH   ;
        int             vecstb ;
        double          course ;
        double          speed  ;

        int r = sscanf(jsonarr, "%p , %i , %lf , %lf", &objH, &vecstb, &course, &speed);
        if (4 != r) {
            _setErr("params 'objH'/'vecstb'/'course'/'speed' not found");
            return FALSE;
        }

        objH  = S52_setVector(objH, vecstb, course, speed);

        _encode(_result, "[%p]", objH);
        return TRUE;
    }

    //extern DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);
    if (0 == S52_strncmp(cmdName, "S52_setDimension", strlen("S52_setDimension"))) {
        S52ObjectHandle objH;
        double          a   ;
        double          b   ;
        double          c   ;
        double          d   ;

        int r = sscanf(jsonarr, "%p , %lf , %lf , %lf , %lf", &objH, &a, &b, &c, &d);
        if (5 != r) {
            _setErr("params 'objH'/'a'/'b'/'c'/'d' not found");
            return FALSE;
        }


        objH  = S52_setDimension(objH, a, b, c, d);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }

    //extern DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn);
    if (0 == S52_strncmp(cmdName, "S52_setVESSELstate", strlen("S52_setVESSELstate"))) {
        S52ObjectHandle objH        ;
        int             vesselSelect;
        int             vestat      ;
        int             vesselTurn  ;

        int r = sscanf(jsonarr, "%p , %i , %i , %i", &objH, &vesselSelect, &vestat, &vesselTurn);
        if (4 != r) {
            _setErr("params 'objH'/'vesselSelect'/'vestat'/'vesselTurn' not found");
            return FALSE;
        }

        objH  = S52_setVESSELstate(objH, vesselSelect, vestat, vesselTurn);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }


    //extern DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);
    if (0 == S52_strncmp(cmdName, "S52_delMarObj", strlen("S52_delMarObj"))) {
        S52ObjectHandle objH;
        int r = sscanf(jsonarr, "%p", &objH);
        if (1 != r) {
            _setErr("params 'objH' not found");
            return FALSE;
        }

        objH = S52_delMarObj(objH);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }

    //extern DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
    //                                     unsigned int xyznbrmax, double *xyz, const char *listAttVal);
    if (0 == S52_strncmp(cmdName, "S52_newMarObj", strlen("S52_newMarObj"))) {
        gchar   plibObjName[32];
        int     objType     ;
        int     xyznbrmax   ;
        double *xyz        = NULL;
        gchar  *listAttVal = NULL;
        gchar  *cptr       = jsonarr;

        // 1 - get string plibObjName
        cptr++;
        while (*cptr != '"') cptr++;
        memcpy(plibObjName, jsonarr+1, cptr-(jsonarr+1));
        //memcpy(plibObjName, jsonarr+1, cptr-jsonarr);
        plibObjName[6] = '\0';

        // 2 & 3 - objType, xyznbrmax
        //int r = sscanf(jsonarr, "\"%s\" , %i , %i , %s", plibObjName, &objType, &xyznbrmax, &rest);
        //int r = sscanf(jsonarr, "\"%31c\" , %i , %i", plibObjName, &objType, &xyznbrmax);
        int r = sscanf(cptr, "\" , %i , %i", &objType, &xyznbrmax);
        //PRINTF("plibObjName:%s objType:%i &xyznbrmax:%i jsonarr:%s", plibObjName, objType, xyznbrmax, jsonarr);
        if (2 != r) {
            _setErr("params 'objType'/'xyznbrmax' not found");
            return FALSE;
        }

        // 4 & 5 - FIXME: NULL for now
        S52ObjectHandle objH = S52_newMarObj(plibObjName, objType, xyznbrmax, xyz, listAttVal);

        _encode(_result, "[%p]", objH);

        return TRUE;
    }

    // {"id":0,"command":"S52_getPalettesNameList","params":[]}
    //extern DLL const char * STD S52_getPalettesNameList(void);
    if (0 == S52_strncmp(cmdName, "S52_getPalettesNameList", strlen("S52_getPalettesNameList"))) {
        const gchar *palListstr = S52_getPalettesNameList();

        _encode(_result, "%s", palListstr);

        //PRINTF("%s", _result);

        return TRUE;
    }

    // {"id":0,"command":"S52_getCellNameList","params":[]}
    //extern DLL const char * STD S52_getCellNameList(void);
    if (0 == S52_strncmp(cmdName, "S52_getCellNameList", strlen("S52_getCellNameList"))) {
        const gchar *cellNmListstr = S52_getCellNameList();

        _encode(_result, "%s", cellNmListstr);

        PRINTF("%s", _result);

        return TRUE;
    }

    //extern DLL double STD S52_getMarinerParam(S52MarinerParameter paramID);
    if (0 == S52_strncmp(cmdName, "S52_getMarinerParam", strlen("S52_getMarinerParam"))) {
        int paramID;

        //PRINTF("S52_getMarinerParam: %s", jsonarr);

        int r = sscanf(jsonarr, " %i", &paramID);
        if (1 != r) {
            _setErr("params 'paramID' not found");
            return FALSE;
        }
        double d = S52_getMarinerParam(paramID);

        _encode(_result, "[%f]", d);

        //PRINTF("%s", _result);

        return TRUE;
    }

    //extern DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val);
    if (0 == S52_strncmp(cmdName, "S52_setMarinerParam", strlen("S52_setMarinerParam"))) {
        int paramID;
        double val;
        int r = sscanf(jsonarr, " %i , %lf", &paramID, &val);
        if (2 != r) {
            _setErr("params 'paramID'/'val' not found");
            return FALSE;
        }
        double d = S52_setMarinerParam(paramID, val);

        _encode(_result, "[%f]", d);

        //PRINTF("%s", _result);

        return TRUE;
    }

    // WARNING: check for drawLast() before draw()
    //extern DLL int    STD S52_drawLast(void);
    if (0 == S52_strncmp(cmdName, "S52_drawLast", strlen("S52_drawLast"))) {
        int i = S52_drawLast();

        _encode(_result, "[%i]", i);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_draw(void);
    if (0 == S52_strncmp(cmdName, "S52_draw", strlen("S52_draw"))) {
        int i = S52_draw();

        _encode(_result, "[%i]", i);

        //PRINTF("%s", _result);

        goto exit;
    }

    //extern DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);
    if (0 == S52_strncmp(cmdName, "S52_getRGB", strlen("S52_getRGB"))) {
        gchar colorName[6];
        int r = sscanf(jsonarr, " \"%5c", colorName);
        if (1 != r) {
            _setErr("params 'colorName' not found");
            return FALSE;
        }
        colorName[5] = '\0';

        unsigned char R;
        unsigned char G;
        unsigned char B;
        int ret = S52_getRGB(colorName, &R, &G, &B);
        //PRINTF("%i, %i, %i\n", R,G,B);

        if (TRUE == ret)
            _encode(_result, "[%02X,%02X,%02X]", R, G, B);
        else
            _encode(_result, "[0]");

        //PRINTF("%s\n", _result);

        return TRUE;
    }

    //extern DLL int    STD S52_setTextDisp(int dispPrioIdx, int count, int state);
    if (0 == S52_strncmp(cmdName, "S52_setTextDisp", strlen("S52_setTextDisp"))) {
        int dispPrioIdx;
        int count;
        int state;
        int r = sscanf(jsonarr, " %i , %i , %i", &dispPrioIdx, &count, &state);
        if (3 != r) {
            _setErr("params '&dispPrioIdx' / 'count' / 'state' not found");
            return FALSE;
        }
        int ret = S52_setTextDisp(dispPrioIdx, count, state);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else
            _encode(_result, "[0]");

        //PRINTF("%s\n", _result);

        return TRUE;
    }

    //extern DLL int    STD S52_getTextDisp(int dispPrioIdx);
    if (0 == S52_strncmp(cmdName, "S52_getTextDisp", strlen("S52_getTextDisp"))) {
        int dispPrioIdx;
        int r = sscanf(jsonarr, " %i ", &dispPrioIdx);
        if (1 != r) {
            _setErr("params 'dispPrioIdx' not found");
            return FALSE;
        }
        int ret = S52_getTextDisp(dispPrioIdx);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            if (-1 == ret)
                _encode(_result, "[-1]");
            else // FALSE
                _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        return TRUE;
    }

    //extern DLL int    STD S52_loadCell        (const char *encPath,  S52_loadObject_cb loadObject_cb);
    if (0 == S52_strncmp(cmdName, "S52_loadCell", strlen("S52_loadCell"))) {
        char encPath[128];

        //PRINTF("XXXX %s\n", jsonarr);

        int r = sscanf(jsonarr, " \"%127c ", encPath);
        if (1 != r) {
            _setErr("params 'encPath' not found");
            return FALSE;
        }

        //PRINTF("XXXX %s\n", encPath);

        gchar  *cptr = encPath;
        while (*cptr != '\"') cptr++;
        *cptr = '\0';

        int ret = S52_loadCell(encPath, NULL);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        return TRUE;
    }

    //extern DLL int    STD S52_doneCell        (const char *encPath);
    if (0 == S52_strncmp(cmdName, "S52_doneCell", strlen("S52_doneCell"))) {
        if (1 != count) {
            _setErr("params 'encPath' not found");
            return FALSE;
        }

        char encPath[128];

        //PRINTF("XXXX %s\n", jsonarr);

        int r = sscanf(jsonarr, " \"%127c ", encPath);
        //PRINTF("XXXX %s\n", encPath);

        gchar  *cptr = encPath;
        while (*cptr != '\"') cptr++;
        *cptr = '\0';

        int ret = S52_doneCell(encPath);

        if (TRUE == ret)
            _encode(_result, "[1]");
        else {
            _encode(_result, "[0]");
        }

        //PRINTF("%s\n", _result);

        return TRUE;
    }
    //*/
}

gboolean                   _socket_read_write(GIOChannel *source, GIOCondition cond, gpointer user_data)
{
    GString *instr = g_string_new(NULL);
    GError  *error = NULL;

    switch(cond) {
    case G_IO_IN: {

        //PRINTF("G_IO_IN\n");

        GIOStatus ret = g_io_channel_read_line_string(source, instr, NULL, &error);
        if (NULL != error) {
            g_string_free(instr, TRUE);
            g_object_unref((GSocketConnection*)user_data);
            PRINTF("ERROR receiving:[ret:%i err:%s]\n", ret, error->message);
            return FALSE;
        }

        // catch TERM
        if (0 == instr->len) {
            g_object_unref((GSocketConnection*)user_data);
            return FALSE;
        }

        // debug
        //PRINTF("received:%s\n", instr->str);

        _strCursor    = 0;
        guint  n      = 0;
        int    outstr = _handle_method(instr);
        if (FALSE == outstr) {
            //_encode(_response, "{\"id\":%i,\"error\":\"%s\"}\n", _request_id, _result);
            n = g_snprintf(_response, BLOCK, "{\"id\":%i,\"error\":\"%s\"}\n", _request_id, _result);
            _response[n] = '\0';
            /*
            n = g_snprintf(_response, BLOCK, "{\"id\":%i,\"error\":\"%s\"}\n", _request_id, _result);
            if (n > BLOCK) {
                PRINTF("g_snprintf(): no space in buffer\n");
                return FALSE;
            }
            _response[n] = '\0';
            outstr = _response;
            */
        } else {
            //_encode(_response, "{\"id\":%i,\"error\":\"no error\",\"result\":%s}\n", _request_id, outstr);
            //_encode(_response, "{\"id\":%i,\"error\":\"no error\",\"result\":%s}\n", _request_id, _result);
            //*                              "{\"id\":%i,\"error\":\"no error\",\"result\":%s}\n"
            //n = g_snprintf(_response, BLOCK, "{\"id\":%i,\"error\":\"no error\",\"result\":%s}\n", _request_id, outstr);
            n = g_snprintf(_response, BLOCK, "{\"id\":%i,\"error\":\"no error\",\"result\":%s}\n", _request_id, _result);
            _response[n] = '\0';
            //PRINTF("n:%i\n", n);
            //*/

            // FIXME: _encode() & g_snprintf() do basically the same thing and the resulting
            // string is the same .. but only g_snprintf() string pass io channel !!!
        }

        gsize bytes_written = 0;
        ret = g_io_channel_write_chars(source, _response, n, &bytes_written, &error);
        if (NULL != error) {
            g_string_free(instr, TRUE);
            g_object_unref((GSocketConnection*)user_data);
            PRINTF("ERROR sending:[ret:%i err:%s]\n", ret, error->message);
            return FALSE;
        }
        g_io_channel_flush(source, NULL);
        ++_request_id;

        // debug
        //PRINTF("sended:%s", _response);

        break;
    }

    //case G_IO_IN:
    case G_IO_OUT: PRINTF("G_IO_OUT\n"); return FALSE; break;
    case G_IO_PRI: PRINTF("G_IO_PRI\n"); break;
    case G_IO_ERR: PRINTF("G_IO_ERR\n"); break;
    case G_IO_HUP: PRINTF("G_IO_HUP\n"); break;
    case G_IO_NVAL:PRINTF("G_IO_NVAL\n");break;
    }

    g_string_free(instr, TRUE);

    return TRUE;
    //return FALSE;
}

#define UNUSED(expr) do { (void)(expr); } while (0)
gboolean                   _new_connection(GSocketService    *service,
                                           GSocketConnection *connection,
                                           GObject           *source_object,
                                           gpointer           user_data)
{
    // quiet gcc warning (unused param)
    (void)service;
    (void)source_object;
    (void)user_data;

    g_object_ref(connection);    // tell glib not to disconnect

    GSocketAddress *sockaddr = g_socket_connection_get_remote_address(connection, NULL);
    GInetAddress   *addr     = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(sockaddr));
    guint16         port     = g_inet_socket_address_get_port   (G_INET_SOCKET_ADDRESS(sockaddr));

    PRINTF("New Connection from %s:%d\n", g_inet_address_to_string(addr), port);

    GSocket    *socket  = g_socket_connection_get_socket(connection);
    gint        fd      = g_socket_get_fd(socket);
    GIOChannel *channel = g_io_channel_unix_new(fd);

    g_io_add_watch(channel, G_IO_IN , (GIOFunc)_socket_read_write, connection);

    return FALSE;
}

//static
int                        _initSock()
{
    PRINTF("start to listen to socket ..\n");

    //_sockReadThread(NULL);
    GError *error = NULL;

    g_type_init();
    GSocketService *service        = g_socket_service_new();
    GInetAddress   *address        = g_inet_address_new_from_string("127.0.0.1");
    //GSocketAddress *socket_address = g_inet_socket_address_new(address, 4000);
    GSocketAddress *socket_address = g_inet_socket_address_new(address, 2950); // GPSD use 2947

    g_socket_listener_add_address(G_SOCKET_LISTENER(service), socket_address, G_SOCKET_TYPE_STREAM,
                                  G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error);

    g_object_unref(socket_address);
    g_object_unref(address);
    if (NULL != error) {
        g_printf("ERROR: %s\n", error->message);
        return FALSE;
    }

    g_socket_service_start(service);

    g_signal_connect(service, "incoming", G_CALLBACK(_new_connection), NULL);


    return TRUE;
}
#endif
