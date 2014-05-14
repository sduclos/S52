// S52.c: top-level interface to libS52.so plug-in
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2014 Sylvain Duclos sduclos@users.sourceforge.net

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
// (meanwhile fold all function to get an overview)
// possible split:
// S52sock.c - all network code (no header needed since it implement S52.h)
// FIXME: document JSON call to libS52
// ..

#include "S52.h"        // S52_view
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

#include <string.h>     // memmove(), memcpy()
#include <glib.h>       // GString, GArray, GPtrArray, guint64, ..
#include <math.h>       // INFINITY

// mkfifo
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>     // unlink()
#define PIPENAME "/tmp/S52_pipe_01"

#include "gdal.h"       // to handle Raster

#ifdef S52_USE_GOBJECT
// Not really using GObect for now but this emphasize that the
// opaque pointer is typedef'ed to something that 'gjs' can understand

typedef guint64 _S52ObjectHandle;

// NOTE: gjs doesn't seem to understand 'gpointer', so send the
// handle as a 64 bits unsigned integer (guint64)
// FIXME: try gdouble that is also 64bits on 32 and 64 bits machine

#else   // S52_USE_GOBJECT

// in real life S52ObjectHandle is juste a ordinary opaque pointer
//typedef S52_obj* _S52ObjectHandle;

#endif  // S52_USE_GOBJECT


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

#ifdef S52_USE_SOCK
#include <sys/types.h>
#include <sys/socket.h>
#endif

#ifdef S52_USE_DBUS
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
static DBusConnection *_dbus    = NULL;
static DBusError       _dbusError;
#endif


static GTimer         *_timer   = NULL;  // debug - lap timer

// trap signal (ESC abort rendering)
// must be compiled with -std=gnu99
#include <sys/types.h>
#include <signal.h>
static volatile gint G_GNUC_MAY_ALIAS _atomicAbort;

#ifdef _MINGW
// not available on win32
#else
// 1) SIGHUP	 2) SIGINT	 3) SIGQUIT	 4) SIGILL	 5) SIGTRAP
// 6) SIGABRT	 7) SIGBUS	 8) SIGFPE	 9) SIGKILL	10) SIGUSR1
//11) SIGSEGV	12) SIGUSR2	13) SIGPIPE	14) SIGALRM	15) SIGTERM
static struct   sigaction             _old_signal_handler_SIGINT;   //  2
static struct   sigaction             _old_signal_handler_SIGQUIT;  //  3
static struct   sigaction             _old_signal_handler_SIGTRAP;  //  5
static struct   sigaction             _old_signal_handler_SIGABRT;  //  6
static struct   sigaction             _old_signal_handler_SIGKILL;  //  9
static struct   sigaction             _old_signal_handler_SIGUSR1;  // 10
static struct   sigaction             _old_signal_handler_SIGSEGV;  // 11
static struct   sigaction             _old_signal_handler_SIGUSR2;  // 12
static struct   sigaction             _old_signal_handler_SIGTERM;  // 15
#endif

// not available on win32
#ifdef S52_USE_BACKTRACE
#ifndef S52_USE_ANDROID
#include <execinfo.h>
#endif
#endif


// IMO Radar: 0.25, 0.5, 0.75, 1.5, 3, 6, 12 and 24nm
//#define MIN_RANGE  0.25 // minimum range (NM)
#define MIN_RANGE  0.01        // minimum range (NM)
#define MAX_RANGE  45.0 * 60.0 // maximum range (NM) [45deg]

#define SCROLL_FAC 0.1
#define ZOOM_FAC   0.1
#define ZOOM_INI   1.0

#define S57_CELL_NAME_MAX_LEN 8 // cell name maximum lenght

typedef struct _extent {
    double S,W,N,E;
} _extent;

typedef struct _cell {
    _extent    ext;

    GString   *filename;  // encName/baseName
    gchar     *encPath;   // original user path/name

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
    // FIXME: this could be move out of '_cell' if it is only used
    // at load time by _loadBaseCell()
    guint      baseEdgeRCID;
    GPtrArray *ConnectedNodes;  // doesn't work without sort, ConnectedNodes rcid random in some case (CA4579016)
    GPtrArray *Edges;
#endif

    // journal - place holder for object to be drawn (after culling)
    GPtrArray *objList_supp;   // list of object on the "Supress by Radar" layer
    GPtrArray *objList_over;   // list of object on the "Over Radar" layer  (ie on top)
    GPtrArray *textList;       // hold ref to object with text (drawn on top of everything)

    GString   *S57ClassList;   // hold the name of S57 class of this cell

#ifdef S52_USE_PROJ
    int        projDone;       // TRUE this cell has been projected
#endif

    /*
    // optimisation - do CS only on obj affected by a change in a MP
    // instead of resolving the CS logic at render-time.
    // Will help to clean up the render-time code.
    // list of S52_obj that have CS in theire LUP
    GPtrArray *DEPARElist;
    GPtrArray *DEPCNTlist;
    GPtrArray *OBSTRNlist;
    GPtrArray *RESARElist;
    GPtrArray *WRECKSlist;
    */

} _cell;

// BBTree of key/value pair: LNAM --> geo (--> S57ID (for cursor pick))
// BBTree of LANM 'key' with S57_geo as 'value'
static GTree     *_lnamBBT      = NULL;
static GPtrArray *_cellList     = NULL;    // list of loaded cells - sorted, big to small scale (small to large region)
static _cell     *_crntCell     = NULL;    // current cell (passed around when loading --FIXME: global var (dumb))
static _cell     *_marinerCell  = NULL;    // place holder MIO's, and other (fake) S57 object
#define MARINER_CELL   "--6MARIN.000"     // a chart purpose 6 (bellow knowm IHO chart pupose)
#define WORLD_SHP_EXT  ".shp"             // shapefile ext
#define WORLD_BASENM   "--0WORLD"         // '--' - agency (none), 0 - chart purpose (S52 is 1-5 (harbour))
#define WORLD_SHP      WORLD_BASENM WORLD_SHP_EXT

static GString   *_plibNameList = NULL;    // string that gather plibName
static GString   *_paltNameList = NULL;    // string that gather palette name
static GString   *_S57ClassList = NULL;    // string that gather cell S57 class name
static GString   *_S52ObjNmList = NULL;    // string that gather cell S52 obj name
static GString   *_cellNameList = NULL;    // string that gather cell name

static GPtrArray *_objToDelList = NULL;    // list of obj to delete in next APP cycle


static int        _doInit       = TRUE;    // init the lib

// FIXME: reparse CS of the affected MP only (ex: ship outline MP need only to reparse OWNSHP CS)
static int        _doCS         = FALSE;   // TRUE will recreate *all* CS at next draw() or drawLast()
                                       // (not only those affected by Marine's Parameter)

static int        _doCullLights = FALSE; // TRUE will do lights_sector culling when _cellList change

#include <locale.h>                    // setlocal()
static char      *_intl         = NULL;

// statistic
static int        _nCull        = 0;
static int        _nTotal       = 0;

// helper - save user center of view in degree
typedef struct {
    double cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} _view_t;
static _view_t _view = {0.0, 0.0, 0.0, 0.0};

// Note: thread awarness to prenvent two threads from writing into the 'scene graph' at once
// (ex data comming from gpsd,) so this is mostly Mariners' Object.
// Note: the mutex never have to do work with the main_loop already serializing call.
// Note: that DBus and socket/WebSocket are running from the main loop but the handling is done from threads

#ifdef S52_USE_ANDROID
static GStaticMutex       _mp_mutex = G_STATIC_MUTEX_INIT;  // protect _ais_list
#define GMUTEXLOCK   g_static_mutex_lock
#define GMUTEXUNLOCK g_static_mutex_unlock
#else
static GMutex             _mp_mutex; // protect _ais_list
#define GMUTEXLOCK   g_mutex_lock
#define GMUTEXUNLOCK g_mutex_unlock
#endif


// CSYMB init scale bar, north arrow, unit, CHKSYM
static int             _iniCSYMB = TRUE;

// when S52_USE_GOBJECT S52ObjectHandle is an int so FALSE resolve to zero
// else its a pointer and FALSE resolve to NULL
static S52ObjectHandle _OWNSHP   = FALSE;
static S52ObjectHandle _SELECTED = FALSE;  // debug: used when an AIS target is selected
static S52ObjectHandle _SCALEB10 = FALSE;
static S52ObjectHandle _SCALEB11 = FALSE;
static S52ObjectHandle _NORTHAR1 = FALSE;
static S52ObjectHandle _UNITMTR1 = FALSE;
static S52ObjectHandle _CHKSYM01 = FALSE;
static S52ObjectHandle _BLKADJ01 = FALSE;

//static S52_RADAR_cb  _RADAR_cb   = NULL;
//static int          _doRADAR  = TRUE;
static GPtrArray    *_rasterList = NULL;    // list of Raster
//static S52_GL_ras   *_raster     = NULL;

static char _version[] = "$Revision: 1.126 $\n"
      "libS52 0.131\n"
#ifdef _MINGW
      "_MINGW\n"
#endif
#ifdef S52_USE_GV
      "S52_USE_GV\n"
#endif
#ifdef GV_USE_DOUBLE_PRECISION_COORD
      "GV_USE_DOUBLE_PRECISION_COORD\n"
#endif
#ifdef S52_USE_GLIB2
      "S52_USE_GLIB2\n"
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
#ifdef S52_USE_EGL
      "S52_USE_EGL\n"
#endif 
#ifdef S52_USE_GL2
      "S52_USE_GL2\n"
#endif
#ifdef S52_USE_GLES2
      "S52_USE_GLES2\n"
#endif
#ifdef S52_USE_ANDROID
      "S52_USE_ANDROID\n"
#endif
#ifdef S52_USE_TEGRA2
      "S52_USE_TEGRA2\n"
#endif
#ifdef S52_USE_ADRENO
      "S52_USE_ADRENO\n"
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
#ifdef S52_USE_SYM_VESSEL_DNGHL
      "S52_USE_SYM_VESSEL_DNGHL\n"
#endif
#ifdef S52_USE_TXT_SHADOW
      "S52_USE_TXT_SHADOW\n"
#endif
#ifdef S52_USE_RADAR
      "S52_USE_RADAR\n"
#endif
#ifdef S52_USE_MESA3D
      "S52_USE_MESA3D\n"
#endif
#ifdef S52_USE_C_AGGR_C_ASSO
      "S52_USE_C_AGGR_C_ASSO\n"
#endif
;


// callback to eglMakeCurrent() / eglSwapBuffers()
#ifdef S52_USE_EGL
static EGL_cb _eglBeg = NULL;
static EGL_cb _eglEnd = NULL;
static void  *_EGLctx = NULL;
// WARNING: call BEFORE mutex
#define EGL_BEG(tag)    if (NULL != _eglBeg) {                   \
                            if (FALSE == _eglBeg(_EGLctx,#tag))  \
    					        return FALSE;                    \
						}

// WARNING: call AFTER mutex
#define EGL_END(tag)    if (NULL != _eglEnd) _eglEnd(_EGLctx,#tag);

#else  // S52_USE_EGL

#define EGL_BEG(tag)
#define EGL_END(tag)

#endif  // S52_USE_EGL


// check basic init
#define S52_CHECK_INIT  if (TRUE == _doInit) {                                                 \
                           PRINTF("WARNING: libS52 not initialized --try S52_init() first\n"); \
                           return FALSE;                                                       \
                        }

// check if we are shuting down
#define S52_CHECK_MUTX  GMUTEXLOCK(&_mp_mutex);           \
                        if (NULL == _marinerCell) {       \
                           GMUTEXUNLOCK(&_mp_mutex);      \
                           PRINTF("ERROR: mutex lock\n"); \
                           g_assert(0);                   \
                           exit(0);                       \
                           return FALSE;                  \
                        }


////////////////////////////////////////////////////
//
// USER INPUT VALIDATION
//

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
    int crntMask = (int) S52_MP_get(S52_MAR_DISP_CATEGORY);
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

    // check if the newMask has one of possible bit
    if (!(0 == newMask)                           &&
        !(S52_MAR_DISP_CATEGORY_BASE   & newMask) &&
        !(S52_MAR_DISP_CATEGORY_STD    & newMask) &&
        !(S52_MAR_DISP_CATEGORY_OTHER  & newMask) &&
        !(S52_MAR_DISP_CATEGORY_SELECT & newMask) ) {

        PRINTF("WARNING: ignoring category value (%i)\n", newMask);

        return crntMask;
    }

    PRINTF("Display Priority: current mask:0x%x --> new mask:0x%x)\n", crntMask, newMask);


    if (crntMask  & newMask)
        crntMask -= newMask;
    else
        crntMask += newMask;

    return (double)crntMask;
}

static double     _validate_mar(double val)
// S52_MAR_DISP_LAYER_LAST  - MARINERS' CATEGORY (drawn on top - last)
{
    //int crntMask = (int) S52_getMarinerParam(S52_MAR_DISP_LAYER_LAST);
    int crntMask = (int) S52_MP_get(S52_MAR_DISP_LAYER_LAST);
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

    return val;
}

static double     _validate_lat(double lat)
{
    if (lat < -90.0 || 90.0 < lat) {
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
    int crntMask = (int) S52_MP_get(S52_CMD_WRD_FILTER);
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
    }

    if (crntMask  & newMask)
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

DLL double STD S52_getMarinerParam(S52MarinerParameter paramID)
// return Mariner parameter or the value in S52_MAR_ERROR if fail
// FIXME: check mariner param against groups selection
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    double val = S52_MP_get(paramID);

    PRINTF("paramID:%i, val:%f\n", paramID, val);

    GMUTEXUNLOCK(&_mp_mutex);

    return val;
}

DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val)
// validate and set Mariner Parameter
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    PRINTF("paramID:%i, val:%f\n", paramID, val);

    switch (paramID) {
        case S52_MAR_ERROR               : break;
        case S52_MAR_SHOW_TEXT           : val = _validate_bool(val);                   break;
        // _SEABED01->DEPARE01;
        case S52_MAR_TWO_SHADES          : val = _validate_bool(val);  _doCS = TRUE;    break;
        // DEPCNT02; _SEABED01->DEPARE01; _UDWHAZ03->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_CONTOUR      : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SNDFRM02->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_DEPTH        : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_CONTOUR     : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_DEEP_CONTOUR        : val = _validate_meter(val); _doCS = TRUE;    break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_PATTERN     : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_SHIPS_OUTLINE       : val = _validate_bool(val);                   break;
        case S52_MAR_DISTANCE_TAGS       : val = _validate_nm(val);    _fixme(paramID); break;
        case S52_MAR_TIME_TAGS           : val = _validate_min(val);   _fixme(paramID); break;
        case S52_MAR_FULL_SECTORS        : val = _validate_bool(val);                   break;
        // RESARE02;
        case S52_MAR_SYMBOLIZED_BND      : val = _validate_bool(val);  _doCS = TRUE;    break;
        case S52_MAR_SYMPLIFIED_PNT      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_CATEGORY       : val = _validate_disp(val);                   break;
        case S52_MAR_COLOR_PALETTE       : val = _validate_pal(val);                    break;

        case S52_MAR_VECPER              : val = _validate_min(val);                    break;
        // OWNSHP02; VESSEL01;
        case S52_MAR_VECMRK              : val = _validate_int(val);   _doCS = TRUE;    break;
        case S52_MAR_VECSTB              : val = _validate_int(val);                    break;

        case S52_MAR_HEADNG_LINE         : val = _validate_bool(val);                   break;
        case S52_MAR_BEAM_BRG_NM         : val = _validate_nm(val);                     break;

        //---- experimental variables ----
        //
        case S52_MAR_FONT_SOUNDG         : val = _validate_bool(val);                   break;
        // DEPARE01; DEPCNT02; _DEPVAL01; SLCONS03; _UDWHAZ03;
        case S52_MAR_DATUM_OFFSET        : val = _validate_meter(val); _doCS = TRUE;    break;
        case S52_MAR_SCAMIN              : val = _validate_bool(val);                   break;
        case S52_MAR_ANTIALIAS           : val = _validate_bool(val);                   break;
        case S52_MAR_QUAPNT01            : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_OVERLAP        : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_LAYER_LAST     : val = _validate_mar (val);                   break;

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
        case S52_MAR_DISP_WORLD          : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_RND_LN_END     : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_VRMEBL_LABEL   : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_RASTER         : val = _validate_bool(val);                   break;

        case S52_CMD_WRD_FILTER          : val = _validate_filter(val);                 break;

        default:
            PRINTF("WARNING: unknown Mariner's Parameter type (%i)\n", paramID);

            GMUTEXUNLOCK(&_mp_mutex);

            return FALSE;
    }

    int ret = S52_MP_set(paramID, val);

    ///////////////////////////////////////////////
    // FIXME: process _doCS == TRUE immediatly
    //
    //S52_MAR_SYMBOLIZED_BND
    // RESARE02;

    //S52_MAR_SAFETY_DEPTH
    // OBSTRN04,
    // WRECKS02;

    //S52_MAR_SAFETY_CONTOUR
    // DEPARE01;
    // DEPCNT02;
    // OBSTRN04,
    // WRECKS02;
    //S52_MAR_SHALLOW_CONTOUR
    // DEPARE01;
    //S52_MAR_DEEP_CONTOUR
    // DEPARE01;
    //S52_MAR_SHALLOW_PATTERN
    // DEPARE01;
    //S52_MAR_TWO_SHADES
    // DEPARE01
    //S52_MAR_VECMRK
    // OWNSHP02;
    // VESSEL01;


    // optimisation: resolve only symb affected OR resolve immediatly on setMarParam()
    // 1 - for all cell
    //   1.1 - resloveSMB(obj) in XXXlist
    //   1.2 - _mobeObj()

    // unique CS:
    // MP - doesn't change layer, Mariners' Param (no _moveObj())
    // OP - can change layer, Override Prio, need to call _moveObj()
    // DEPARE01; MP
    // DEPCNT02; MP, OP
    // OBSTRN04;     OP
    // RESARE02;     OP
    // WRECKS02;     OP

    // OWNSHP02; MP
    // VESSEL01; MP

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}
////////////////////////////////////////////////////////////////////////


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

static gint       _cmpCell(gconstpointer a, gconstpointer b)
// sort cell: bigger region (small scale) last (eg 553311)
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

static int        _isCellLoaded(const char *baseName)
// TRUE if cell loaded, else FALSE
{
    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, i);
        if (0 == g_strcmp0(c->filename->str, baseName)) {
            return TRUE;
        }
    }

    return FALSE;
}

static _cell     *_newCell(const char *filename)
// add this cell else NULL (if allready loaded)
// assume filename is not NULL
{
    if (NULL == _cellList)
        _cellList = g_ptr_array_new();

    // strip path
    gchar *baseName = g_path_get_basename(filename);
    if (TRUE == _isCellLoaded(baseName)) {
        g_free(baseName);
        return NULL;
    }

    {   // init cell
        _cell *cell = g_new0(_cell, 1);
        for (int i=0; i<S52_PRIO_NUM; ++i) {
            for (int j=0; j<S52_N_OBJ; ++j)
                cell->renderBin[i][j] = g_ptr_array_new();
        }

        cell->filename = g_string_new(baseName);
        g_free(baseName);

        cell->ext.S =  INFINITY;
        cell->ext.W =  INFINITY;
        cell->ext.N = -INFINITY;
        cell->ext.E = -INFINITY;

        cell->local = S52_CS_init();

        // journal
        cell->objList_supp = g_ptr_array_new();
        cell->objList_over = g_ptr_array_new();
        cell->textList     = g_ptr_array_new();

        cell->S57ClassList = g_string_new("");

        cell->projDone     = FALSE;

        /*
        cell->DEPARElist = g_ptr_array_new();
        cell->DEPCNTlist = g_ptr_array_new();
        cell->OBSTRNlist = g_ptr_array_new();
        cell->RESARElist = g_ptr_array_new();
        cell->WRECKSlist = g_ptr_array_new();
        */

        /*
        g_ptr_array_add(_cellList, cell);
        // sort cell: bigger region (small scale) last (eg 553311)
        g_ptr_array_sort(_cellList, _cmpCell);
        */

        _crntCell = cell;
    }

    return _crntCell;
}

static S52_obj   *_delObj(S52_obj *obj)
{
    S57_geo *geo = S52_PL_getGeo(obj);

    // debug
    //PRINTF("objH:%#lX, ID:%i\n", (long unsigned int)obj, S57_getGeoID(geo));

    S52_GL_del(obj);

    S57_doneData(geo, NULL);
    S52_PL_setGeo(obj, NULL);

    obj = S52_PL_delDta(obj);
    g_free(obj);

    //return obj; // NULL
    return NULL; // NULL
}

static int        _freeCell(_cell *c)
{
    if (NULL == _cellList) {
        PRINTF("WARNING: no cell\n");
        return FALSE;
    }

    if (NULL != c->filename)
        g_string_free(c->filename, TRUE);

    if (NULL != c->encPath)
        g_free(c->encPath);

    for (int j=0; j<S52_PRIO_NUM; ++j) {
        for (int k=0; k<N_OBJ_T; ++k) {
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

    /*
    if (NULL != cell->DEPARElist) {
        g_ptr_array_free(c->objList_over, TRUE);
        cell->DEPARElist = NULL;
    }
    if (NULL != cell->DEPCNTlist) {
        g_ptr_array_free(c->objList_over, TRUE);
        cell->DEPCNTlist = NULL;
    }
    if (NULL != cell->OBSTRNlist) {
        g_ptr_array_free(c->objList_over, TRUE);
        cell->OBSTRNlist = NULL;
    }
    if (NULL != cell->RESARElist) {
        g_ptr_array_free(c->objList_over, TRUE);
        cell->RESARElist = NULL;
    }
    if (NULL != cell->WRECKSlist) {
        g_ptr_array_free(c->objList_over, TRUE);
        cell->WRECKSlist = NULL;
    }
    */

    if (NULL != c->S57ClassList) {
        g_string_free(c->S57ClassList, TRUE);
        c->S57ClassList = NULL;
    }

    g_free(c);

    return TRUE;
}

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

static _Unwind_Reason_Code _trace_func(struct _Unwind_Context *ctx, void *user_data)
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

#if 0
static guint      _GetLibraryAddress(const char* libraryName)
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
#endif

#if 0
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
        //state.libAdjustment = _GetLibraryAddress("libs52droid.so");
        state.libAdjustment = 0;
        state.crntFrame     = 0;
        state.ptrArr[0]     = 0;


        _Unwind_Reason_Code code = _Unwind_Backtrace(_trace_func, (void*)&state);
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

    for (int i=0; i<nptrs; ++i)
        PRINTF("==== %s\n", strings[i]);

    free(strings);

    return TRUE;
}
#endif // S52_USE_BACKTRACE

#ifdef _MINGW
static void       _trapSIG(int sig)
{
    void  *buffer[100];
    char **strings;

    if (SIGINT == sig) {
        PRINTF("Signal SIGINT(%i) cought .. setting up atomic to abort draw()\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);
        return;
    }

    if (SIGSEGV == sig) {
        PRINTF("Segmentation violation cought (%i) ..\n", sig);
    } else {
        PRINTF("other signal(%i) trapped\n", sig);
    }


    // shouldn't reach this !?
    g_assert_not_reached();  // turn off via G_DISABLE_ASSERT

    exit(sig);
}

#else  // _MINGW

static void       _trapSIG(int sig, siginfo_t *info, void *secret)
{
    // 2 -
    if (SIGINT == sig) {
        PRINTF("Signal SIGINT(%i) cought .. setting up atomic to abort draw()\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGINT.sa_sigaction)
            _old_signal_handler_SIGINT.sa_sigaction(sig, info, secret);

        //return;
    }

    //  3  - Quit (POSIX)
    if (SIGQUIT == sig) {
        PRINTF("Signal SIGQUIT(%i) cought .. Quit\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGQUIT.sa_sigaction)
            _old_signal_handler_SIGQUIT.sa_sigaction(sig, info, secret);

        //return;
    }

    //  5  - Trap (ANSI)
    if (SIGTRAP == sig) {
        PRINTF("Signal SIGTRAP(%i) cought .. debugger\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTRAP.sa_sigaction)
            _old_signal_handler_SIGTRAP.sa_sigaction(sig, info, secret);

        return;
    }

    //  6  - Abort (ANSI)
    if (SIGABRT == sig) {
        PRINTF("Signal SIGABRT(%i) cought .. Abort\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGABRT.sa_sigaction)
            _old_signal_handler_SIGABRT.sa_sigaction(sig, info, secret);

        return;
    }

    //  9  - Kill, unblock-able (POSIX)
    if (SIGKILL == sig) {
        PRINTF("Signal SIGKILL(%i) cought .. Kill\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGKILL.sa_sigaction)
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

#endif  //S52_USE_BACKTRACE

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGSEGV.sa_sigaction)
            _old_signal_handler_SIGSEGV.sa_sigaction(sig, info, secret);

        return;
    }

    // 15 - Termination (ANSI)
    if (SIGTERM == sig) {
        PRINTF("Signal SIGTERM(%i) cought .. Termination\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTERM.sa_sigaction)
            _old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);

        //return;
    }

    // 10
    if (SIGUSR1 == sig) {
        PRINTF("Signal 'User-defined 1' cought - SIGUSR1(%i)\n", sig);
        return;
    }
    // 12
    if (SIGUSR2 == sig) {
        PRINTF("Signal 'User-defined 2' cought - SIGUSR2(%i)\n", sig);
        return;
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
#endif  // _MINGW

static int        _initSIG(void)
// init signal handler
{

#ifdef _MINGW
    //signal(SIGINT,  _trapSIG);  //  2 - Interrupt (ANSI).
    //signal(SIGSEGV, _trapSIG);  // 11 - Segmentation violation (ANSI).
#else
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
    //  5  - Trap (ANSI)
    sigaction(SIGTRAP, &sa, &_old_signal_handler_SIGTRAP);
    //  6  - Abort (ANSI)
    sigaction(SIGABRT, &sa, &_old_signal_handler_SIGABRT);
    //  9  - Kill, unblock-able (POSIX)
    sigaction(SIGKILL, &sa, &_old_signal_handler_SIGKILL);
    // 11 - Segmentation violation (ANSI).
    sigaction(SIGSEGV, &sa, &_old_signal_handler_SIGSEGV);   // loop in android
    // 15 - Termination (ANSI)
    //sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);

    // 10
    sigaction(SIGUSR1, &sa, &_old_signal_handler_SIGUSR1);
    // 12
    sigaction(SIGUSR2, &sa, &_old_signal_handler_SIGUSR2);

    // debug - will trigger SIGSEGV for testing
    //_cell *c = 0x0;
    //c->ext.S = INFINITY;
#endif

    return TRUE;
}

static int        _getCellsExt(_extent* ext);
static int        _initPROJ(void)
{
    if (TRUE == _mercPrjSet)
        return TRUE;

    _extent ext;
    if (FALSE == _getCellsExt(&ext)) {
        PRINTF("WARNING: failed, no cell loaded\n");
        return FALSE;
    }

    double cLat = (ext.N + ext.S) / 2.0;
    double cLon = (ext.W + ext.E) / 2.0;

    // FIXME: cLng break bathy projection
    // anti-meridian trick: use cLng, but this break bathy
    _mercPrjSet = S57_setMercPrj(cLat, cLon);
    //_mercPrjSet = S57_setMercPrj(0.0, cLon); // test - 0 cLat
    //_mercPrjSet = S57_setMercPrj(0.0, 0.0); // test - 0 cLat

    // while here, set default view center
    _view.cLat  =  (ext.N + ext.S) / 2.0;
    _view.cLon  =  (ext.W + ext.E) / 2.0;
    _view.rNM   = ((ext.N - ext.S) / 2.0) * 60.0;
    _view.north = 0.0;
    S52_GL_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);

    {// debug
        double xyz[3] = {_view.cLon, _view.cLat, 0.0};
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            return FALSE;
        }
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
                for (guint j=S52_AREAS; j<N_OBJ_T; ++j) {
                //for (guint j=0; j<N_OBJ_T; ++j) {
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

static int        _collect_CS_touch(_cell* c)
// setup object used by CS
{
    for (guint i=0; i<S52_PRIO_NUM; ++i) {
        for (guint j=S52_AREAS; j<N_OBJ_T; ++j) {
        //for (guint j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = c->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geo = S52_PL_getGeo(obj);

                // debug
                //if (== S57_getGeoID(geo)) {
                //    PRINTF("found\n");
                //    S57_dumpData(geo);
                //}

                S52_CS_touch(c->local, geo);
            }
        }
    }

    // then process lights_sector
    if (NULL != c->lights_sector) {
        for (guint i=0; i<c->lights_sector->len; ++i) {
            S52_obj *obj  = (S52_obj *)g_ptr_array_index(c->lights_sector, i);
            S57_geo *geo  = S52_PL_getGeo(obj);

            S52_CS_touch(c->local, geo);
        }
    }

    // need to do a _resolveCS() at the next _app()
    _doCS = TRUE;

    return TRUE;
}

DLL int    STD S52_loadLayer (const char *layername, void *layer, S52_loadObject_cb loadObject_cb);  // forward decl

#ifdef S52_USE_DBUS
static int        _initDBus(void);  // forward decl
#endif
#ifdef S52_USE_SOCK
static int        _initSock(void);  // forward decl
#endif

DLL int    STD S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h, S52_error_cb err_cb)
// init basic stuff (outside of the main loop)
{
    //libS52Zdso();
    // debug
    if (NULL != err_cb)
        err_cb("S52_init(): test err log\n");

    S52_initLog(err_cb);

    PRINTF("screen_pixels_w: %i, screen_pixels_h: %i, screen_mm_w: %i, screen_mm_h: %i\n",
            screen_pixels_w,     screen_pixels_h,     screen_mm_w,     screen_mm_h);

    if (screen_pixels_w<1 || screen_pixels_h<1 || screen_mm_w<1 || screen_mm_h<1) {
        PRINTF("ERROR: screen dim < 1\n");
        return FALSE;
    }

#if !defined(S52_USE_ANDROID) && defined(S52_USE_GOBJECT)
    // check size of S52ObjectHandle == guint64 = unsigned long long int
    // when S52_USE_GOBJECT is defined
    if (sizeof(guint64) != sizeof(unsigned long long int)) {
        PRINTF("sizeof(guint64) != sizeof(unsigned long long int)\n");
        g_assert(0);
    }
#endif

#if !defined(_MINGW)
    // check if run as root
    if (0 == getuid()) {
        PRINTF("ERROR: do NOT run as SUPERUSER (root) .. exiting\n");
        exit(0);
    }
#endif

    // check if init already done
    if (!_doInit) {
        PRINTF("WARNING: libS52 already initalized\n");
        return FALSE;
    }

    ///////////////////////////////////////////////////////////
    //
    // init signal handler
    //
    _initSIG();

    ///////////////////////////////////////////////////////////
    //
    // init mem stat stuff
    //
    //GMemVTable *glib_mem_profiler_table;
    //g_mem_set_vtable(glib_mem_profiler_table);
    //g_mem_profile();



    g_atomic_int_set(&_atomicAbort, FALSE);

    ///////////////////////////////////////////////////////////
    // init global info
    //
    if (NULL == _plibNameList)
        _plibNameList = g_string_new("S52raz-3.2.rle (Internal Chart No 1)");
    if (NULL == _paltNameList)
        _paltNameList = g_string_new("");
    if (NULL == _cellNameList)
        _cellNameList = g_string_new("");
    if (NULL == _S57ClassList)
        _S57ClassList = g_string_new("");
    if (NULL == _S52ObjNmList)
        _S52ObjNmList = g_string_new("");
    if (NULL == _objToDelList)
        _objToDelList = g_ptr_array_new();

    //if (NULL == _geoList) {
    //    _geoList = g_ptr_array_sized_new(1000);
    //}


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


    // FIXME: validate
    if (0==screen_pixels_w || 0==screen_pixels_h || 0==screen_mm_w || 0==screen_mm_h) {
        PRINTF("ERROR: invalid screen size\n");
        return FALSE;
    }

    S52_GL_setDotPitch(screen_pixels_w, screen_pixels_h, screen_mm_w, screen_mm_h);


    ///////////////////////////////////////////////////////////
    // init env stuff for GDAL/OGR/S57
    //

    // GDAL/OGR/S57 options (1: overwrite env)

    // GDAL debug info ON
    //g_setenv("CPL_DEBUG", "ON", 1);

#ifdef S52_USE_GLIB2

#ifdef S52_USE_ANDROID
    g_setenv("S57_CSV", "/sdcard/s52droid/gdal_data", 1);
#endif


#ifdef S52_USE_SUPP_LINE_OVERLAP
    // make OGR return primitive and linkage
    g_setenv("OGR_S57_OPTIONS",
             "UPDATES=APPLY,SPLIT_MULTIPOINT=ON,PRESERVE_EMPTY_NUMBERS=ON,RETURN_PRIMITIVES=ON,RETURN_LINKAGES=ON,LNAM_REFS=ON,RECODE_BY_DSSI=ON",
             1);
#else
    g_setenv("OGR_S57_OPTIONS",
             "LNAM_REFS=ON,UPDATES=APPLY,SPLIT_MULTIPOINT=ON,PRESERVE_EMPTY_NUMBERS=ON",
             1);


#endif // S52_USE_SUPP_LINE_OVERLAP

#else  // S52_USE_GLIB2

    const char *name = "OGR_S57_OPTIONS";
    //const char *value= "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON,PRESERVE_EMPTY_NUMBERS:ON,RETURN_LINKAGES:ON";
    const char *value= "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON,PRESERVE_EMPTY_NUMBERS:ON";

    //setenv("OGR_S57_OPTIONS", "LNAM_REFS:ON,UPDATES:ON,SPLIT_MULTIPOINT:ON", 1);
#include <stdlib.h>
    //int setenv(const char *name, const char *value, int overwrite);
    setenv(name, value, 1);
     const char *env = g_getenv("OGR_S57_OPTIONS");
    PRINTF("%s\n", env);
#endif  // S52_USE_GLIB2


    _intl = setlocale(LC_ALL, "C");


    ///////////////////////////////////////////////////////////
    // init S52 stuff
    //
    // load basic def (ex color, CS, ...)
    S52_PL_init();

    // put an error No in S52_MAR_ERROR
    //S52_MP_set(S52_MAR_ERROR, INFINITY);
    S52_MP_set(S52_MAR_ERROR, 0.0);

    // set default to show all text
    S52_MP_setTextDisp(0, 100, TRUE);

#if !defined(S52_USE_GLES2)
    // broken on GL1 (it use stencil)
    S52_MP_set(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);
#endif


    // setup the virtual cell that will hold mariner's objects
    // NOTE: there is no IHO cell at scale '6', this garanty that
    // objects of this 'cell' will be drawn last (ie on top)
    // NOTE: most Mariners' Object land on the "fast" layer 9
    // But 'pastrk' (and other) are drawn on layer < 9.
    // So MARINER_CELL must be checked for all chart for
    // object on layer 0-8 during draw()
    _marinerCell = _newCell(MARINER_CELL);
    g_ptr_array_add (_cellList, _marinerCell);
    g_ptr_array_sort(_cellList, _cmpCell);

    // init extend
    // FIXME: init struct
    _marinerCell->ext.S = -INFINITY;
    _marinerCell->ext.W = -INFINITY;
    _marinerCell->ext.N =  INFINITY;
    _marinerCell->ext.E =  INFINITY;

    // FIXME: def this
    // init raster
    if (NULL == _rasterList)
        _rasterList = g_ptr_array_new();


    ///////////////////////////////////////////////////////////
    // init experimental stuff
    //

#ifdef S52_USE_DBUS
    _initDBus();
#endif

#ifdef S52_USE_SOCK
    _initSock();
#endif

#ifdef S52_USE_PIPE
    _pipeWatch(NULL);
#endif
    ///////////////////////////////////////////////////////////



    _timer =  g_timer_new();

    _doInit = FALSE;


    PRINTF("S52_INIT() .. DONE\n");

    return TRUE;
}

DLL cchar *STD S52_version(void)
{
    PRINTF("%s", _version);

    return _version;
}

DLL int    STD S52_done(void)
// clear all - shutdown libS52
{
    S52_CHECK_INIT;
    // FIXME: check if we are in the middle of draw() call
    // (ie user call done() via RADAR_cb)
    // the client must let the draw() finish before exiting!
    S52_CHECK_MUTX;

    if (NULL != _cellList) {
        // FIXME: check if foreach() work here
        for (guint i=0; i<_cellList->len; ++i) {
            _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
            _freeCell(c);
        }
        g_ptr_array_free(_cellList, TRUE);
        //g_ptr_array_unref(_cellList);
        _cellList = NULL;
    }
    // FIXME: first thingh to do
    _marinerCell = NULL;


    S52_GL_done();
    S52_PL_done();

    S57_donePROJ();
    _mercPrjSet = FALSE;

    _intl   = NULL;

    //g_mem_profile();


    S52_doneLog();

    g_timer_destroy(_timer);
    _timer = NULL;

    _doInit = FALSE;

#ifdef S52_USE_DBUS
    dbus_connection_unref(_dbus);
    _dbus = NULL;
#endif

    g_string_free(_plibNameList, TRUE); _plibNameList = NULL;
    g_string_free(_paltNameList, TRUE); _paltNameList = NULL;
    g_string_free(_cellNameList, TRUE); _cellNameList = NULL;
    g_string_free(_S57ClassList, TRUE); _S57ClassList = NULL;
    g_string_free(_S52ObjNmList, TRUE); _S52ObjNmList = NULL;

    g_ptr_array_free(_objToDelList, TRUE); _objToDelList = NULL;

    // flush raster
    // FIXME: foreach
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        S52_GL_delRaster(r, FALSE);
        g_free(r);
    }
    g_ptr_array_free(_rasterList, TRUE);
    _rasterList = NULL;

#ifdef S52_USE_EGL
    _eglBeg = NULL;
    _eglEnd = NULL;
    _EGLctx = NULL;
#endif

    GMUTEXUNLOCK(&_mp_mutex);

    PRINTF("libS52 done\n");

    return TRUE;
}

#if 0
//  DEPRECATED
DLL int    STD S52_setFont(int font)
{
    S52_CHECK_INIT;

    S52_GL_setFontDL(font);

    return font;
}
#endif

#ifdef S52_USE_C_AGGR_C_ASSO
static int        _linkRel2LNAM(_cell* c)
// link geo to C_AGGR / C_ASSO geo
{
    for (guint i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {

            GPtrArray *rbin = c->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj    = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geoRel = S52_PL_getGeo(obj);

                GString *lnam_refsstr = S57_getAttVal(geoRel, "LNAM_REFS");
                if (NULL != lnam_refsstr) {
                    GString *refs_geo  = NULL;
                    gchar  **splitLNAM = g_strsplit_set(lnam_refsstr->str+1, "():,", 0);
                    gchar  **topLNAM   = splitLNAM;

                    splitLNAM++;  // skip number of item
                    while (NULL != *splitLNAM) {
                        if ('\000' == **splitLNAM) {
                            splitLNAM++;
                            continue;
                        }

                        S57_geo *geo = (S57_geo *)g_tree_lookup(_lnamBBT, *splitLNAM);
                        if (NULL == geo) {
                            PRINTF("WARNING: LNAM (%s) not found\n", *splitLNAM);
                            splitLNAM++;
                            continue;
                        }

                        // link geo to C_AGGR / C_ASSO geo
                        S57_setRelationship(geo, geoRel);

                        if (NULL == refs_geo) {
                            refs_geo = g_string_new("");
                            g_string_printf(refs_geo, "%p", (void*)geo);
                        } else {
                            g_string_append_printf(refs_geo, ",%p", (void*)geo);
                        }
                        splitLNAM++;
                    }
                    // add geo to C_AGGR / C_ASSO LNAM_REFS_GEO
                    if (NULL != refs_geo)
                        S57_setAtt(geoRel, "_LNAM_REFS_GEO", refs_geo->str);

                    g_string_free(refs_geo, TRUE);

                    g_strfreev(topLNAM);
                }
            }
        }
    }

    if (NULL != _lnamBBT) {
        g_tree_destroy(_lnamBBT);
        _lnamBBT = NULL;
    }

    return TRUE;
}
#endif  // S52_USE_C_AGGR_C_ASSO

#ifdef S52_USE_SUPP_LINE_OVERLAP
static int        _suppLineOverlap()
// no SUPP in case manual chart correction (LC(CHCRIDnn) and LC(CHCRDELn))
// Note: for now _suppLineOverlap() work for LC() only.
{
    // FIXME: some valid edge are supress,
    // check if odd vertex supress on to many edge

    return_if_null(_crntCell->Edges);
    return_if_null(_crntCell->ConnectedNodes);

    // assume that there is nothing on layer S52_PRIO_NODATA
    for (int prio=S52_PRIO_MARINR; prio>S52_PRIO_NODATA; --prio) {
        for (int obj_t=S52_LINES; obj_t>S52__META; --obj_t) {
            GPtrArray *rbin = _crntCell->renderBin[prio][obj_t];
            for (guint idx=0; idx<rbin->len; ++idx) {
                // one object
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                // get edge ID list
                S57_geo *geo = S52_PL_getGeo(obj);

                GString *name_rcnmstr = S57_getAttVal(geo, "NAME_RCNM");
                GString *name_rcidstr = S57_getAttVal(geo, "NAME_RCID");

                if ((NULL==name_rcnmstr) || (NULL==name_rcidstr))
                    break;

                {   // check for substring ",...)" if found at the end
                    // this mean that TEMP_BUFFER_SIZE in OGR is not large anought.
                    // see ogr/ogrfeature.cpp:994
                    // #define TEMP_BUFFER_SIZE 80
                    // apply patch in S52/doc/ogrfeature.cpp.diff to OGR source code
                    const gchar *substr = ",...)";
                    gchar       *found1 = g_strrstr(name_rcnmstr->str, substr);
                    gchar       *found2 = g_strrstr(name_rcidstr->str, substr);
                    if (NULL!=found1 || NULL!=found2) {
                        PRINTF("ERROR: OGR buffer TEMP_BUFFER_SIZE in ogr/ogrfeature.cpp:994 overflow\n");
                        g_assert(0);
                    }
                }


                //* CA379035.000 (Tadoussac) has Mask
                //MASK (IntegerList) = (7:255,255,255,255,255,255,255)
                GString *maskstr = S57_getAttVal(geo, "MASK");
                if (NULL != maskstr) {
                    gchar **splitMASK = g_strsplit_set(maskstr->str+1, "():,", 0);
                    gchar **topMASK   = splitMASK;

                    // the first integer is the lenght (ie the number of mask value)
                    guint n = atoi(*splitMASK++);
                    // 1 - mask, 2 - show, 255 - null (exterior boundary truncated by the data limit)
                    for (guint i=0; i<n; ++splitMASK, ++i) {
                        if (('1'==*splitMASK[0]) || ('5'==*splitMASK[1])) {
                            PRINTF("FIXME: 'MASK' FOUND ---> %s : %s\n", S57_getName(geo), maskstr->str);

                            // debug - CA379035.000 (Tadoussac) pass here
                            //g_assert(0);
                        }
                    }
                    g_strfreev(topMASK);
                }
                //*/

                // take only Edge (ie rcnm == 130 (Edge type))
                gchar **splitrcnm  = g_strsplit_set(name_rcnmstr->str+1, "():,", 0);
                gchar **splitrcid  = g_strsplit_set(name_rcidstr->str+1, "():,", 0);
                gchar **toprcnm    = splitrcnm;
                gchar **toprcid    = splitrcid;

                {
                    // check if splitrcnm / splitrcid are valid
                    // the first integer is the lenght
                    guint n = atoi(*splitrcnm);
                    for (guint i=0; i<n; ++splitrcnm, ++i) {
                        if (NULL == *splitrcnm) {
                            PRINTF("ERROR: *splitrcnm\n");
                            g_assert(0);
                        }
                    }
                    n = atoi(*splitrcid);
                    for (guint i=0; i<n; ++splitrcid, ++i) {
                        if (NULL == *splitrcid) {
                            PRINTF("ERROR: *splitrcid\n");
                            g_assert(0);
                        }
                    }
                }

                // reset list
                splitrcnm = toprcnm;
                splitrcid = toprcid;
                splitrcnm++;
                splitrcid++;

                //NAME_RCNM (IntegerList) = (1:130)
                //NAME_RCID (IntegerList) = (1:72)
                //for (i=0; NULL!=str
                // for all rcnm == 130
                //   take rcid
                //   get Edge with rcid
                //   make Edge to point to geo if null
                //   else make geo coord z==-1 for all vertex in Egde that are found in geo

                //for (guint i=0; NULL!=*splitrcnm; splitrcnm++) {
                for ( ; NULL!=*splitrcnm; splitrcnm++) {

                    // the S57 name for Edge (130)
                    //if (0 == g_strcmp0(*splitrcnm, "130")) {
                    if (0 == S52_strncmp(*splitrcnm, "130", 3)) {
                        for (guint j=0; j<_crntCell->Edges->len; ++j) {
                            S57_geo *geoEdge = (S57_geo *)g_ptr_array_index(_crntCell->Edges, j);

                            // FIXME: optimisation: save in rcid in geoEdge
                            //GString *rcidEdgestr = S57_getAttVal(geoEdge, "RCID");
                            //gint     rcidEdge    = (NULL == rcidEdgestr) ? 0 : atoi(rcidEdgestr->str);
                            GString *rcidstr = S57_getRCIDstr(geoEdge);

                            //if (0 == S52_strncmp(rcidstr->str, *splitrcid, MAX(strlen(rcidstr->str), strlen(*splitrcid)))) {
                            if (0 == g_strcmp0(rcidstr->str, *splitrcid)) {
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
                g_strfreev(toprcnm);
                g_strfreev(toprcid);

            }
        }
    }

    // free all overlaping line data
    {   // flush OGR primitive geo
        int quiet = TRUE;
        g_ptr_array_foreach(_crntCell->Edges,          (GFunc)S57_doneData, &quiet);
        g_ptr_array_foreach(_crntCell->ConnectedNodes, (GFunc)S57_doneData, &quiet);
    }

    g_ptr_array_free(_crntCell->Edges,          TRUE);
    g_ptr_array_free(_crntCell->ConnectedNodes, TRUE);

    _crntCell->Edges          = NULL;
    _crntCell->ConnectedNodes = NULL;
    _crntCell->baseEdgeRCID   = 0;

    return TRUE;
}
#endif


static _cell     *_loadBaseCell(char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb)
{
    _cell *ch = NULL;
    //FILE  *fd = NULL;

    // skip file not terminated by .000
    char *base = g_path_get_basename(filename);
    if ((0!=g_strcmp0(base+8, ".000")) && (0!=g_strcmp0(base+8, ".shp"))) {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated] or .shp\n", filename);
        g_free(base);
        return NULL;
    }
    g_free(base);

    ch = _newCell(filename);
    if (NULL == ch) {
        PRINTF("WARNING: _newCell() failed\n");
        //g_assert(0);
        return NULL;
    }
    g_ptr_array_add(_cellList, ch);
    g_ptr_array_sort(_cellList, _cmpCell);

    //if (NULL == cb) {
    //    PRINTF("NOTE: using default S52_loadLayer() callback\n");
    //    cb = S52_loadLayer;
    //}

#ifdef S52_USE_GV
    S57_gvLoadCell (filename, layer_cb);
#else
    S57_ogrLoadCell(filename, loadLayer_cb, loadObject_cb);
#endif

    // FIXME: resolve heightdatum correction here!
    // FIX: go trouht all layer that have to look for
    // VERCSA, VERCLR, VERCCL, VERCOP
    // ...

#ifdef S52_USE_SUPP_LINE_OVERLAP
    PRINTF("DEBUG: resolving line overlap for cell: %s ...\n", filename);

    _suppLineOverlap();
#endif

#ifdef S52_USE_C_AGGR_C_ASSO
    _linkRel2LNAM(ch);
#endif

    _collect_CS_touch(ch);


    {   // failsafe - check if a PLib put an object on the NODATA layer
        PRINTF("DEBUG: NODATA Layer check -START- ==============================================\n");
        for (guint i=0; i<_cellList->len; ++i) {
            // one cell
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            for (int obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                // one object type
                GPtrArray *rbin = ci->renderBin[S52_PRIO_NODATA][obj_t];
                for (guint idx=0; idx<rbin->len; ++idx) {
                    // one object
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    //
                    if (0 == g_strcmp0(S57_getName(geo), "DSID"  )) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "C_AGGR")) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "C_ASSO")) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "M_NPUB")) continue;

                    PRINTF("WARNING: object:'%s' type:'%i' is on NODATA layer\n", S57_getName(geo), obj_t);

                    // debug
                    //S57_dumpData(geo, FALSE);
                }
            }
        }
        PRINTF("DEBUG: NODATA Layer check -END-   ==============================================\n");
    }

    return ch;
}

#ifdef S52_USE_OGR_FILECOLLECTOR
// in libgdal.so
// Note: must add 'extern "C"' to GDAL/OGR at S57.h:40
// (is linking with g++ fix this)
char **S57FileCollector( const char *pszDataset );

#if 0
//#include "iso8211.h"
static int        _loadCATALOG(char *filename)
{
    FILE *fd = NULL;
    filename = g_strstrip(filename);

    if (NULL == (fd = S52_fopen(filename, "r"))) {
        PRINTF("WARNING: CATALOG not found (%s)\n", filename);

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
#endif  // 0
#endif  // S52_USE_OGR_FILECOLLECTOR

// see http://www.gdal.org/warptut.html
#include "gdal_alg.h"
#include "ogr_srs_api.h"
#include "gdalwarper.h"

//static char      *_getSRS(const char *str)
static char      *_getSRS(void)
{
    char       *ret    = NULL;
    const char *prjStr = S57_getPrjStr();

    if (NULL == prjStr) {
        g_assert(0);
        return NULL;
    }

    // FIXME: cLng break bathy projection
    OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);
    if (OGRERR_NONE == OSRSetFromUserInput(hSRS, prjStr)) {
        OSRExportToWkt(hSRS, &ret);
    } else {
        PRINTF("Translating source or target SRS failed:%s\n", prjStr );
        g_assert(0);
    }

    OSRDestroySpatialReference(hSRS);

    return ret;
}

static GDALDatasetH _createDSTfile(GDALDatasetH hSrcDS, const char *pszFilename,
                                   GDALDriverH hDriver, const char *pszSourceSRS,
                                   const char *pszTargetSRS)

{
    double adfDstGeoTransform[6];
    int nPixels = 0;
    int nLines  = 0;

    // Create a transformation object from the source to destination coordinate system.
    void *hTransformArg = GDALCreateGenImgProjTransformer(hSrcDS, pszSourceSRS, NULL, pszTargetSRS, TRUE, 1000.0, 0);
    if (NULL == hTransformArg) {
        PRINTF("WARNING: GDALCreateGenImgProjTransformer() failed\n");
        return NULL;
    }

    // Get approximate output definition.
    if (CE_None != GDALSuggestedWarpOutput(hSrcDS, GDALGenImgProjTransform, hTransformArg, adfDstGeoTransform, &nPixels, &nLines)) {
        PRINTF("WARNING: GDALSuggestedWarpOutput() failed\n");
        return NULL;
    }

    GDALDestroyGenImgProjTransformer(hTransformArg);

    // Create the output file.
    PRINTF("Creating output file is that %dP x %dL.\n", nPixels, nLines);

    //GDALDriverH hDriver = GDALGetDriverByName(pszFormat);
    GDALDatasetH hDstDS = GDALCreate(hDriver, pszFilename, nPixels, nLines,
                                     GDALGetRasterCount(hSrcDS),
                                     GDALGetRasterDataType(GDALGetRasterBand(hSrcDS,1)),
                                     NULL);
    if (NULL == hDstDS) {
        PRINTF("WARNING: GDALCreate() failed\n");
        return NULL;
    }

    // Write out the projection definition.
    GDALSetProjection  (hDstDS, pszTargetSRS);
    GDALSetGeoTransform(hDstDS, adfDstGeoTransform);

    // Copy the color table, if required.
    GDALColorTableH hCT = GDALGetRasterColorTable(GDALGetRasterBand(hSrcDS,1));
    if (NULL !=  hCT)
        GDALSetRasterColorTable(GDALGetRasterBand(hDstDS,1), hCT);

    return hDstDS;
}

static int        _warp(GDALDatasetH hSrcDS, GDALDatasetH hDstDS)
{
    // Setup warp options.
    GDALWarpOptions *psWarpOptions = GDALCreateWarpOptions();

    psWarpOptions->hSrcDS = hSrcDS;
    psWarpOptions->hDstDS = hDstDS;

    psWarpOptions->nBandCount  = 1;
    //psWarpOptions->panSrcBands = (int *) CPLMalloc(sizeof(int) * psWarpOptions->nBandCount );
    psWarpOptions->panSrcBands = (int *) malloc(sizeof(int) * psWarpOptions->nBandCount );
    psWarpOptions->panSrcBands[0] = 1;
    //psWarpOptions->panDstBands = (int *) CPLMalloc(sizeof(int) * psWarpOptions->nBandCount );
    psWarpOptions->panDstBands = (int *) malloc(sizeof(int) * psWarpOptions->nBandCount );
    psWarpOptions->panDstBands[0] = 1;

    psWarpOptions->pfnProgress = GDALTermProgress;

    // Establish reprojection transformer.
    psWarpOptions->pTransformerArg = GDALCreateGenImgProjTransformer(hSrcDS, GDALGetProjectionRef(hSrcDS),
                                                                     hDstDS, GDALGetProjectionRef(hDstDS),
                                                                     FALSE, 0.0, 1 );
    psWarpOptions->pfnTransformer = GDALGenImgProjTransform;

    // Initialize and execute the warp operation.
    GDALWarpOperationH wOP = GDALCreateWarpOperation(psWarpOptions);
    GDALChunkAndWarpImage(wOP, 0, 0, GDALGetRasterXSize(hDstDS), GDALGetRasterYSize(hDstDS));

    // clean up
    GDALDestroyGenImgProjTransformer(psWarpOptions->pTransformerArg);
    GDALDestroyWarpOptions(psWarpOptions);
    GDALDestroyWarpOperation(wOP);

    return TRUE;
}

int               _loadRaster(const char *fname)
{
    char fnameMerc[1024];  // MAXPATH!
    g_sprintf(fnameMerc, "%s%s", fname, ".merc");

    // check if allready loaded
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        if (0 == g_strcmp0(r->fnameMerc->str, fnameMerc))
            return FALSE;
    }

    // no, convert to GeoTiff Mercator
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (NULL == driver) {
        PRINTF("WARNING: fail to get GDAL driver\n");
        return FALSE;
    }

    // FIXME: what if projected Merc tiff was made from different projection
    GDALDatasetH datasetDST = GDALOpen(fnameMerc, GA_ReadOnly);
    // no Merc on disk, then create it
    if (NULL == datasetDST) {
        GDALDatasetH datasetSRC = GDALOpen(fname, GA_ReadOnly);
        if (NULL == datasetSRC) {
            PRINTF("WARNING: fail to read raster\n");
            return FALSE;
        }

        //
        // FIXME: convert to Merc at draw() time, if no ENC loaded this will fail
        //

        // FIXME: check if SRS of merc is same srs_DST if not convert again
        char *srs_DST = _getSRS();
        if (NULL != srs_DST) {
            char *srs_SRC = g_strdup(GDALGetProjectionRef(datasetSRC));
            datasetDST = _createDSTfile(datasetSRC, fnameMerc, driver, srs_SRC, srs_DST);
            g_free((gpointer)srs_SRC);

            // convert to Mercator
            _warp(datasetSRC, datasetDST);
        }
        GDALClose(datasetSRC);
    }

    if (NULL != datasetDST) {
        // get data for texure
        GDALRasterBandH bandA = GDALGetRasterBand(datasetDST, 1);

        // GDT_Float32
        GDALDataType gdt = GDALGetRasterDataType(bandA);
        int gdtSz        = GDALGetDataTypeSize(gdt) / 8;

        int w = GDALGetRasterXSize(datasetDST);
        int h = GDALGetRasterYSize(datasetDST);

        int nodata_set = FALSE;
        double nodata  = GDALGetRasterNoDataValue(bandA, &nodata_set);

        // 32 bits
        unsigned char *data = g_new0(unsigned char, w * h * gdtSz);
        GDALRasterIO(bandA, GF_Read, 0, 0, w, h, data, w, h, gdt, 0, 0);

        double gt[6] = {0.0,1.0,0.0,0.0,0.0,1.0};
        if (CE_None != GDALGetGeoTransform(datasetDST, (double *) &gt)) {
            PRINTF("WARNING: GDALGetGeoTransform() failed\n");
            g_assert(0);
        }

        // store data
        S52_GL_ras *ras = g_new0(S52_GL_ras, 1);
        ras->fnameMerc  = g_string_new(fnameMerc);
        ras->w          = w;
        ras->h          = h;
        ras->gdtSz      = gdtSz;
        ras->data       = data;
        ras->nodata     = nodata;  // check nodata_set
        ras->S          = gt[3] + 0 * gt[4] + 0 * gt[5];  // YgeoLL;
        ras->W          = gt[0] + 0 * gt[1] + 0 * gt[2];  // XgeoLL;
        ras->N          = gt[3] + w * gt[4] + h * gt[5];  // YgeoUR;
        ras->E          = gt[0] + w * gt[1] + h * gt[2];  // XgeoUR;
        memcpy(ras->gt, gt, sizeof(double) * 6);

        g_ptr_array_add(_rasterList, ras);
    }

    // finish with GDAL
    GDALClose(datasetDST);  // if NULL ?
    GDALDestroyDriverManager();

    return TRUE;
}

DLL int    STD S52_loadCell(const char *encPath, S52_loadObject_cb loadObject_cb)
// FIXME: handle each type of cell separatly
// OGR:
// - S57:
//    - CATALOG
//    - *.000 (and update)
//    - ENC_ROOT/
// - shapefile
// GDAL:
// - GeoTIFF
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;   // can't load 2 sets of charte at once

    valueBuf chartPath = {'\0'};
    char    *fname     = NULL;
    _cell   *ch        = NULL;    // cell handle

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
        static int  silent = FALSE;
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("       (this msg will not repeat)\n");
            silent = TRUE;
        }
        // FIXME: add macro in S52_utils.h
        //WARN_ONCE("NOTE: using default S52_loadObject() callback\n");
        loadObject_cb = S52_loadObject;
    }


#ifdef _MINGW
    // on Windows 32 the callback is broken
    loadObject_cb = S52_loadObject;
#endif

    // debug - if NULL check in file s52.cfg
    if (NULL == encPath) {
        // FIXME: refactor to return "const char *"
        if (FALSE == S52_getConfig(CONF_CHART, &chartPath)) {
            PRINTF("S57 file not found!\n");
            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }
        fname = g_strdup(chartPath);
    } else {
        fname = g_strdup(encPath);
    }

    fname = g_strstrip(fname);

    if (TRUE != g_file_test(fname, (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
        PRINTF("file or DIR not found (%s)\n", fname);
        g_free(fname);
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }

#ifdef S52_USE_WORLD
    {   // experimental - load world shapefile
        gchar *basename = g_path_get_basename(fname);
        if (0 == g_strcmp0(basename, WORLD_SHP)) {
            PRINTF("loading %s\n", fname);
            ch = _loadBaseCell(fname, loadLayer_cb, loadObject_cb);
        }
        g_free(basename);
    }
#endif

    //*
    {   // experimental - load raster (GeoTIFF)
        gchar *basename = g_path_get_basename(fname);
        int len = strlen(basename);
        if (0 == g_strcmp0(basename+(len-4), ".tif")) {
            _loadRaster(fname);

            g_free(basename);
            g_free(fname);
            GMUTEXUNLOCK(&_mp_mutex);

            return TRUE;
        }
    }
    //*/

    /*
    {   // experimental - load raw raster RADAR
        gchar *basename = g_path_get_basename(fname);
        int len = strlen(basename);
        if (0 == g_strcmp0(basename+(len-4), ".raw")) {
            unsigned char *data = g_new0(unsigned char, 1280 * 2048);
            FILE *fd2;
            if (NULL != (fd2 = fopen(fname, "rb"))) {
                if (1 == fread(data, 1280 * 2048, 1, fd2)) {
                    S52_GL_ras *ras = g_new0(S52_GL_ras, 1);
                    ras->fnameMerc = g_string_new(fname);
                    ras->w     = 1280;
                    ras->h     = 2048;
                    ras->gdtSz = 1;    // byte
                    ras->data  = data;
                    //ras->S     = gt[3] + 0 * gt[4] + 0 * gt[5];  // YgeoLL;
                    //ras->W     = gt[0] + 0 * gt[1] + 0 * gt[2];  // XgeoLL;
                    //ras->N     = gt[3] + w * gt[4] + h * gt[5];  // YgeoUR;
                    //ras->E     = gt[0] + w * gt[1] + h * gt[2];  // XgeoUR;
                    //memcpy(ras->gt, gt, sizeof(double) * 6);
                    S52_GL_getPRJView(&ras->S, &ras->W, &ras->N, &ras->E);


                    g_ptr_array_add(_rasterList, ras);
                } else {
                    PRINTF("fread = 0\n");
                }
            } else {
                PRINTF("can't open file %s\n", fname);
            }

            g_free(basename);
            g_free(fname);
            GMUTEXUNLOCK(&_mp_mutex);

            return TRUE;
        }
    }
    //*/

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
        if (NULL != encList) {
            for (guint i=0; NULL!=encList[i]; ++i) {
                char *encName = encList[i];

                // HACK: g_mem_profile() break the call to S57FileCollector()
                // it return 0x1 instead of 0x0 at the end of encList
                //if (1 == GPOINTER_TO_INT(encName))
                //    break;

                ch = _loadBaseCell(encName, loadLayer_cb, loadObject_cb);
                g_free(encName);
            }
            g_free(encList);
        } else {
            PRINTF("WARNING: S57FileCollector(%s) return NULL\n", fname);
        }
    }

#else
    ch = _loadBaseCell(fname, loadLayer_cb, loadObject_cb);
#endif

    if (NULL == ch) {
        g_free(fname);
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }
    ch->encPath = fname;

#ifdef S52_USE_PROJ
    {
        if (TRUE == _initPROJ())
            _projectCells();

    }
#endif

    // _app() specific to sector light
    _doCullLights = TRUE;

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_doneCell(const char *encPath)
// FIXME: the (futur) chart manager (CM) should to this by itself
// so loadCell would load a CATALOG then CM would load individual cell
// to fill the view (and unload cell outside the view)
{
    S52_CHECK_INIT;

    return_if_null(encPath);

    S52_CHECK_MUTX;

    PRINTF("%s\n", encPath);

    int ret = FALSE;
    gchar *fname = g_strdup(encPath);
    fname = g_strstrip(fname);

    // Note: skip internal pseudo-cell MARINER_CELL (ie: "--6MARIN.000").
    if (TRUE != g_file_test(fname, (GFileTest) (G_FILE_TEST_EXISTS))) {
        PRINTF("file not found (%s)\n", fname);
        goto exit;
    }

    // unload .TIF
    gchar *basename = g_path_get_basename(fname);
    int len = strlen(basename);
    if (0 == g_strcmp0(basename+(len-4), ".tif")) {
        char fnameMerc[1024];
        g_sprintf(fnameMerc, "%s%s", fname, ".merc");
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            if (0 == g_strcmp0(r->fnameMerc->str, fnameMerc)) {
                S52_GL_delRaster(r, FALSE);
                g_free(r);
                ret = TRUE;
                goto exit;
            }
        }
    }

    // unload .raw (radar)
    if (0 != g_strcmp0(basename+8, ".raw")) {
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            if (0 == g_strcmp0(r->fnameMerc->str, fname)) {
                S52_GL_delRaster(r, FALSE);
                g_free(r);
                ret = TRUE;
                goto exit;
            }
        }
    }

    // skip file not terminated by .000
    if (0 != g_strcmp0(basename+8, ".000")) {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated]\n", encPath);
        goto exit;
    }

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        // check if allready loaded
        if (0 == g_strcmp0(basename, c->filename->str)) {
            _freeCell(c);
            g_ptr_array_remove_index(_cellList, idx);
            ret = TRUE;
            goto exit;
        }
    }

exit:
    g_free(basename);
    g_free(fname);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
static int        _loadEdge(const char *name, void *Edge)
{
    if ( (NULL==name) || (NULL==Edge)) {
        PRINTF("ERROR: objname / shape  --> NULL\n");
        g_assert(0);
        return FALSE;
    }

    S57_geo *geoData = S57_ogrLoadObject(name, (void*)Edge);
    if (NULL == geoData) {
        PRINTF("OGR fail to load object: %s\n", name);
        g_assert(0);
        return FALSE;
    }

    // add to this cell (crntCell)
    if (NULL == _crntCell->Edges)
        _crntCell->Edges = g_ptr_array_new();

    {
        guint   npt     = 0;
        double *ppt     = NULL;
        double *ppttmp  = NULL;

        guint   npt_new = 0;
        double *ppt_new = NULL;
        double *ppt_tmp = NULL;

        S57_getGeoData(geoData, 0, &npt, &ppt);

        npt_new = npt + 2;  // why +2!, miss a segment if +1
        //npt_new = npt + 1;  // why +2! (+1)

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
            PRINTF("ERROR: Edge end point 0 (%s) and ConnectedNodes array lenght mismatch\n", name_rcid_0str->str);
            g_assert(0);
        }

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
            PRINTF("ERROR: Edge end point 1 (%s) and ConnectedNodes array lenght mismatch\n", name_rcid_1str->str);
            g_assert(0);
        }

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
        for (guint i=0; i<npt*3; ++i)
            *ppt_tmp++ = *ppttmp++;

        if (NULL != ppt)
            g_free(ppt);


        S57_setGeoLine(geoData, npt_new, ppt_new);
    }

    g_ptr_array_add(_crntCell->Edges, geoData);

    // add geo/LNAM
    //g_ptr_array_add(_geoList, geoData);

    // debug
    //PRINTF("%X len:%i\n", _crntCell->Edges->pdata, _crntCell->Edges->len);
    //PRINTF("XXX %s\n", S57_getName(geoData));

    return TRUE;
}

static int        _loadConnectedNode(const char *name, void *ConnectedNode)
{
    //return_if_null(name);
    //return_if_null(ConnectedNode);
    if ( (NULL==name) || (NULL==ConnectedNode)) {
        PRINTF("ERROR: objname / shape  --> NULL\n");
        g_assert(0);
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

    // add geo/LNAM
    //g_ptr_array_add(_geoList, geoData);


    //g_ptr_array_add(_crntCell->ConnectedNodes, geoData);
    //
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

    static int silent = FALSE;

#ifdef S52_USE_GV
    // init w/ dummy cell name --we get here from OpenEV now (!?)
    if (NULL == _crntCell) {
        _cell *c = _newCell("dummy");
        g_ptr_array_add(_cellList, c);
    }

#endif

    if ((NULL==layername) || (NULL==layer)) {
        PRINTF("ERROR: layername / layer NULL\n");
        return FALSE;
    }

    PRINTF("LOADING LAYER NAME: %s\n", layername);

#ifdef S52_USE_SUPP_LINE_OVERLAP
    // --- trap primitive ---
    // reject unused low level primitive
    //if (0==g_strcmp0(layername, "IsolatedNode"))
    if (0==S52_strncmp(layername, "IsolatedNode", 12))
        return TRUE;

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

    // debug: too slow for Lake Superior
    // FIXME
    //if (0== g_strcmp0(layername, "OBSTRN", 6))
    //    return 1;
    //if (0==g_strcmp0(layername, "UWTROC"))
    //    return 1;

    if (NULL == loadObject_cb) {
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("       (this msg will not repeat)\n");
            silent = TRUE;
        }
        loadObject_cb = S52_loadObject;
    }

    // save S57 object name
    if (0 != _crntCell->S57ClassList->len)
        g_string_append(_crntCell->S57ClassList, ",");

    g_string_append(_crntCell->S57ClassList, layername);


#ifdef S52_USE_GV
    S57_gvLoadLayer (layername, layer, loadObject_cb);
#else
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
            // create array, only if needed
            if (NULL == c->lights_sector)
                c->lights_sector = g_ptr_array_new();

            g_ptr_array_add(c->lights_sector, obj);

            // go it - bailout
            return TRUE;
        }
    }

    return FALSE;
}

static S52_obj   *_insertS57Obj(_cell *c, S52_obj *objOld, S57_geo *geoData)
// insert a S52_obj in a cell from a S57_obj
// insert container objOld if not NULL
// return the new S52_obj
{
    int         obj_t;

    S52_Obj_t   ot         = S57_getObjtype(geoData);
    S52_obj    *obj        = S52_PL_newObj(geoData);
    S52_disPrio disPrioIdx = S52_PL_getDPRI(obj);

    // debug
    //if (899 == S57_getGeoID(geoData)) {
    //    PRINTF("found %i XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n", S57_getGeoID(geoData));
    //    //    return TRUE;
    //}

    // FIXME: refactor this !
    if (NULL != objOld) {
        S52_PL_delDta(objOld);     // clean S52 obj internal data
        objOld = S52_PL_cpyObj(objOld, obj);
        g_free(obj);
        obj = objOld;
    }

    if (NULL == obj) {
        PRINTF("WARNING: S52 object build failed\n");
        g_assert(0);
        return FALSE;
    }
    if (NULL == c) {
        PRINTF("WARNING: no cell to add to\n");
        g_assert(0);
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

        /* optimisation: recompute only CS that change due to new MarParam value
        // save reference for quickly find CS to re-compute after a MarinerParameter change
        // will replace the ugly APP() code that handle _doCS
        const char *CSnm = S52_PL_hasCS(obj);
        if (NULL != CSnm) {
            if (0 == strncmp(CSnm, "DEPARE", 5)) g_ptr_array_add(c->DEPARElist, obj);
            if (0 == strncmp(CSnm, "DEPCNT", 5)) g_ptr_array_add(c->DEPCNTlist, obj);
            if (0 == strncmp(CSnm, "OBSTRN", 5)) g_ptr_array_add(c->OBSTRNlist, obj);
            if (0 == strncmp(CSnm, "RESARE", 5)) g_ptr_array_add(c->RESARElist, obj);
            if (0 == strncmp(CSnm, "WRECKS", 5)) g_ptr_array_add(c->WRECKSlist, obj);

            // GPtrArray *DEPARE01L;  MP
            // GPtrArray *DEPCNT02L;  MP, OP()
            // GPtrArray *OBSTRN04L;  OP()
            // GPtrArray *RESARE02L;  OP()
            // GPtrArray *WRECKS02L;  OP()
        }
        */
    }

#ifdef S52_USE_WORLD
    {
        S57_geo *geoDataNext = NULL;
        if (NULL != (geoDataNext = S57_getNextPoly(geoData))) {
            // recurssion
            _insertS57Obj(c, NULL, geoDataNext);
        }
    }
#endif

    return obj;
}

static S52_obj   *_insertS52Obj(_cell *c, S52_obj *obj)
// inster 'obj' in cell 'c' (use to store Mariners' Object)
{
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

    // special prossesing for light sector
    if (FALSE == _insertLightSec(c, obj)) {
        // insert normal object (ie not a light with sector)
        g_ptr_array_add(c->renderBin[disPrioIdx][obj_t], obj);
    }

    return obj;
}

static S52_obj   *_removeObj(_cell *c, S52_obj *obj)
// remove the S52 object from the cell (not the object itself)
// return the oject removed, else return NULL if object not found
{
    // NOTE: cannot find the right renderBin from 'obj'
    // because 'obj' could be dandling

    for (int i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = c->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *o = (S52_obj *)g_ptr_array_index(rbin, idx);

                if (obj == o) {
                    g_ptr_array_remove_index_fast(rbin, idx);
                    return o;
                }
            }
        }
    }

    PRINTF("WARNING: object handle not found\n");

    return NULL;
}

static int        _compLNAM(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(a,b);
}

DLL int    STD S52_loadObject(const char *objname, void *shape)
{
    S57_geo *geoData = NULL;

    S52_CHECK_INIT;

    if ((NULL==objname) || (NULL==shape)) {
        PRINTF("ERROR: objname / shape NULL\n");
        return FALSE;
    }

#ifdef S52_USE_GV
    // debug: filter out GDAL/OGR metadata
    if (0 == g_strcmp0("DSID", objname))
        return FALSE;

    geoData = S57_gvLoadObject (objname, (void*)shape);
#else
    geoData = S57_ogrLoadObject(objname, (void*)shape);
#endif

    if (NULL == geoData) {
        PRINTF("OBJNAME:%s skipped .. no geo\n", objname);
        return FALSE;
    }

    // set cell extent from each object
    // NOTE: should be the same as CATALOG.03x
    if (_META_T != S57_getObjtype(geoData)) {
        _extent ext;

        S57_getExt(geoData, &ext.W, &ext.S, &ext.E, &ext.N);

        // FIXME: flag init extent
        if (isinf(_crntCell->ext.S)) {
            _crntCell->ext.S = ext.S;
            _crntCell->ext.W = ext.W;
            _crntCell->ext.N = ext.N;
            _crntCell->ext.E = ext.E;
        } else {
            // lat
            if (_crntCell->ext.N < ext.N)
                _crntCell->ext.N = ext.N;
            if (_crntCell->ext.S > ext.S)
                _crntCell->ext.S = ext.S;

            // init W,E limits
            // put W-E in first quadrant [0..360]
            //_crntCell->ext.W = ((_crntCell->ext.W + 180.0) < (ext.W + 180.0)) ? (_crntCell->ext.W + 180.0) : (ext.W + 180.0);
            //_crntCell->ext.E = ((_crntCell->ext.E + 180.0) > (ext.E + 180.0)) ? (_crntCell->ext.E + 180.0) : (ext.E + 180.0);
            //double W = ((_crntCell->ext.W + 180.0) < (ext.W + 180.0)) ? (_crntCell->ext.W + 180.0) : (ext.W + 180.0);
            //double E = ((_crntCell->ext.E + 180.0) > (ext.E + 180.0)) ? (_crntCell->ext.E + 180.0) : (ext.E + 180.0);
            if ((_crntCell->ext.W + 180.0) > (ext.W + 180.0))
                _crntCell->ext.W = ext.W;
            if ((_crntCell->ext.E + 180.0) < (ext.E + 180.0))
                _crntCell->ext.E = ext.E;
        }

        /*
        if (isinf(_crntCell->ext.W)) {
            _crntCell->ext.W = ext.W;
            _crntCell->ext.E = ext.E;
        } else {
            // lng
            if (_crntCell->ext.W > ext.W)
                _crntCell->ext.W = ext.W;
            if (_crntCell->ext.E < ext.E)
                _crntCell->ext.E = ext.E;
        }
        */

        /*
        // check if this cell is crossing the prime-meridian
        if ((_crntCell->ext.W < 0.0) && (0.0 < _crntCell->ext.E)) {
            PRINTF("DEBUG:CELL crossing prime:%s :: MIN: %f %f  MAX: %f %f\n", objname, _crntCell->ext.W, _crntCell->ext.S, _crntCell->ext.E, _crntCell->ext.N);
            g_assert(0);
        }
        */
        // FIXME:
        // check if this cell is crossing the anti-meridian
        //if ((_crntCell->ext.W > -180.0) && (180.0 > _crntCell->ext.E)) {
        //    PRINTF("DEBUG:CELL crossing anti:%s :: MIN: %f %f  MAX: %f %f\n", objname, _crntCell->ext.W, _crntCell->ext.S, _crntCell->ext.E, _crntCell->ext.N);
        //    g_assert(0);
        //}


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

        {   // debug - check for LNAM_REFS in regular S57 object
            GString *key_lnam_refs = S57_getAttVal(geoData, "LNAM_REFS");
            if (NULL != key_lnam_refs) {
                GString *key_ffpt_rind = S57_getAttVal(geoData, "FFPT_RIND");
                GString *key_lnam      = S57_getAttVal(geoData, "LNAM");
                PRINTF("LNAM: %s, LNAM_REFS: %s, FFPT_RIND: %s\n", key_lnam->str, key_lnam_refs->str, key_ffpt_rind->str);
            }
        }

    } else {
        PRINTF("_META_T:OBJNAME:%s ###################################################\n", objname);
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

#ifdef S52_USE_WORLD
    if (0 == strcmp(objname, WORLD_BASENM)) {
        _insertS57Obj(_crntCell, NULL, geoData);

        // unlink Poly chain - else will loop forever in S52_loadPLib()
        S57_delNextPoly(geoData);

        return TRUE;
    }
#endif

    _insertS57Obj(_crntCell, NULL, geoData);

    // helper: save LNAM/geoData to lnamBBT
    if (NULL == _lnamBBT)
        _lnamBBT = g_tree_new(_compLNAM);

    GString *key_lnam = S57_getAttVal(geoData, "LNAM");
    if (NULL != key_lnam)
        g_tree_insert(_lnamBBT, key_lnam->str, geoData);

    S52_CS_add(_crntCell->local, geoData);

    return TRUE;
}


//---------------------------------------------------
//
// CULL
//
//---------------------------------------------------

static int        _intersec(_extent A, _extent B)
// TRUE if intersec, FALSE if outside
// A - ENC ext, B - view ext
{
    // N-S
    if (B.N < A.S) return FALSE;
    if (B.S > A.N) return FALSE;

    // E-W
    //if (_gmax.u < _gmin.u) {
    if (B.W > B.E) {
        // anti-meridian
        //if ((x2 < _gmin.u) && (x1 > _gmax.u))
        if (((A.W < B.W) || (A.W > B.E)) && ((A.E < B.W) || (A.E > B.E))) {
        //if ((B.E > A.W) && (B.W > A.E)) {
        //if ((A.W < B.E) && (B.W > A.E))
            return TRUE;
        } else
            return FALSE;
    } else {
        if (B.E < A.W) return FALSE;
        if (B.W > A.E) return FALSE;
    }

    /*
    if (B.N < A.S) return FALSE;
    if (B.S > A.N) return FALSE;
    if (B.E < A.W) return FALSE;
    if (B.W > A.E) return FALSE;
    */

    return TRUE;
}

static int        _moveObj(_cell *cell, S52_disPrio oldPrio, S52ObjectType obj_t, GPtrArray *oldBin, guint idx)
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

    // Note: if the newPrio is greater than oldPrio then the obj will pass here again
    S52_disPrio newPrio = S52_PL_getDPRI(obj);
    if (newPrio != oldPrio) {
        GPtrArray *newBin = cell->renderBin[newPrio][obj_t];

        // add obj to rbin of newPrio
        g_ptr_array_add(newBin, obj);

        // del obj to rbin of oldPrio
        if (NULL == g_ptr_array_remove_index_fast(oldBin, idx)) {
            PRINTF("ERROR: no object to remove\n");
            g_assert(0);
        }

        return TRUE;
    }

    return FALSE;
}

static int        _app()
// WARNING: not reentrant
{
    //PRINTF("_app(): -.0-\n");
    // 1- delete pending mariner
    for (guint i=0; i<_objToDelList->len; ++i) {
        S52_obj *obj = (S52_obj *)g_ptr_array_index(_objToDelList, i);

        // delete ref to _OWNSHP
        if (obj == _OWNSHP) {
            _OWNSHP = FALSE;  // NULL but S52ObjectHandle can be an gint in some config
        }

        _delObj(obj);
    }
    g_ptr_array_set_size(_objToDelList, 0);

    //PRINTF("_app(): -.1-\n");

    // FIXME: resolve CS in S52_setMarinerParam(), then all logic can be move back to CS (where it belong)
    // instead of doing part of the logic at render time (ex: no need to do S52_PL_cmpCmdParam())
    // Test doesn't show that the logic for POIN_T in GL at render-time cost anything noticable.
    // So the idea to move back the CS logic into CS.c is an esthetic one!
    // 2 -
    if (TRUE == _doCS) {
        // 2.1 - reparse CS
        for (guint i=0; i<_cellList->len; ++i) {
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            // one cell
            for (S52_disPrio prio=S52_PRIO_NODATA; prio<S52_PRIO_NUM; ++prio) {
                // one layer
                //for (int obj_t=S52_AREAS; obj_t<S52_N_OBJ; ++obj_t) {
                for (S52ObjectType obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                    // one object type (render bin)
                    GPtrArray *rbin = ci->renderBin[prio][obj_t];
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        // one object
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                        S52_PL_resloveSMB(obj);
                    }
                }
            }
        }

        //PRINTF("_app(): -0-\n");
        // 2.2 - move obj
        for (guint i=0; i<_cellList->len; ++i) {
            _cell *ci = (_cell*) g_ptr_array_index(_cellList, i);
            // one cell
            for (S52_disPrio prio=S52_PRIO_NODATA; prio<S52_PRIO_NUM; ++prio) {
                // one layer
                for (S52ObjectType obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
                    // one object type (render bin)
                    GPtrArray *rbin = ci->renderBin[prio][obj_t];
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        // one object
                        int check = TRUE;
                        while (TRUE == check)
                            check = _moveObj(ci, prio, obj_t, rbin, idx);
                    }
                }
            }
        }

        // 2.3 - flush all texApha, when raster is bathy, if S52_MAR_SAFETY_CONTOUR as change
        // FIXME: find if SAFETY_CONTOUR as change
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *ras = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            S52_GL_delRaster(ras, TRUE);
        }

    }

    //PRINTF("_app(): -1-\n");
    // done rebuilding CS
    _doCS = FALSE;

    return TRUE;
}

static int        _cullLights(void)
// CULL (first draw() after APP, on all cells)
{
    // FIXME: this compare to cell 'above', but len-1 could rollover
    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // a cell can have no lights sector
        if (NULL == c->lights_sector) {
            //PRINTF("DEBUG: NO lights_sector : %s\n", c->filename->str);
            continue;
        }

        // FIXME: use for_each()
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            S57_geo *geo = S52_PL_getGeo(obj);
            _extent oext;
            S57_getExt(geo, &oext.W, &oext.S, &oext.E, &oext.N);

            S52_PL_resloveSMB(obj);

            // traverse the cell 'above' to check if extent overlap this light
            // FIXME: this compare to cell 'above', but len-1 could rollover
            for (guint k=i-1; k>0 ; --k) {
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
    }

    return TRUE;
}

static int        _cullObj(_cell *c)
// one cell, cull object out side the view and object supressed
// object culled are not inserted in the list of object to draw (journal)
// Note: extent are taken from the obj itself
{
    // Chart No 1 put object on layer 9 (Mariners' Objects)
    // for each layers
    //for (int j=0; j<S52_PRIO_NUM; ++j) {
    for (int j=0; j<S52_PRIO_MARINR; ++j) {

        // for each object type - one layer, skip META
        for (int k=S52_AREAS; k<N_OBJ_T; ++k) {
        //for (int k=0; k<N_OBJ_T; ++k) {
            GPtrArray *rbin = c->renderBin[j][k];

            // for each object
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geo = S52_PL_getGeo(obj);

                ++_nTotal;

                // debug - anti-meridian, US5HA06M/US5HA06M.000
                //if
                //(103 == S57_getGeoID(geo)) {
                //    PRINTF("ISODGR01 found\n");
                //}

                // SCAMIN & PLib (disp cat)
                if (TRUE == S52_GL_isSupp(obj)) {
                    ++_nCull;
                    continue;
                }

                //*
                // outside view
                // NOTE: object can be inside 'ext' but outside the 'view' (cursor pick)
                if (TRUE == S52_GL_isOFFscreen(obj)) {
                    ++_nCull;
                    continue;
                }
                //*/

                // is this object supress by user
                if (TRUE == S57_getSup(geo)) {
                    ++_nCull;
                    continue;
                }

                // store object according to radar flags
                // note: default to 'over' if something else than 'supp'
                if (S52_RAD_SUPP == S52_PL_getRPRI(obj)) {
                    g_ptr_array_add(c->objList_supp, obj);
                } else {
                    g_ptr_array_add(c->objList_over, obj);
                }

                // if this object has TX or TE, draw text last (on top)
                if (TRUE == S52_PL_hasText(obj)) {
                    g_ptr_array_add(c->textList, obj);
                }
            }

            // traverse all mariner object for each layer (of each chart)
            // that are bellow S52_PRIO_MARINR
            // BUG: this overdraw mariner object (ex: all pastck is drawn on each chart)
            // FIX: use chart extent to clip
            // BUG: the overdraw seem to mixup cursor pick!
            {
                GPtrArray *rbin = _marinerCell->renderBin[j][k];
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    if (TRUE != S57_getSup(geo) && FALSE == S52_GL_isSupp(obj)) {
                        if (S52_RAD_SUPP == S52_PL_getRPRI(obj)) {
                            g_ptr_array_add(c->objList_supp, obj);
                        } else {
                            g_ptr_array_add(c->objList_over, obj);
                        }

                        if (TRUE == S52_PL_hasText(obj))
                            g_ptr_array_add(c->textList, obj);
                    }
                }  // objects
            }  // object type
        } // layer
    }  // cell

    return TRUE;
}

static int        _cull(_extent ext)
// FIXME: allow for chart rotation - north != 0.0

// cull chart not in view extent
// - viewport
// - small cell region on top
{
    // reset journal
    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        g_ptr_array_set_size(c->objList_supp, 0);
        g_ptr_array_set_size(c->objList_over, 0);
        g_ptr_array_set_size(c->textList,     0);
    }

    // all cells - larger region first (small scale)
    // Note: skip MARINERS' Object (those on layer 9)
    //guint n = _cellList->len-1;
    //for (guint i=n; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
    for (guint i=_cellList->len; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);

        if ((0==g_strcmp0(WORLD_SHP, c->filename->str)) && (FALSE==S52_MP_get(S52_MAR_DISP_WORLD)))
            continue;

        // is this chart visible
        if (TRUE == _intersec(c->ext, ext)) {
            _cullObj(c);
        }
    }

    // bebug
    //PRINTF("nbr of object culled: %i (%i)\n", _nCull, _nTotal);

    return TRUE;
}

#ifdef S52_USE_GLES2
static int        _drawRaster()
{
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *raster = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        if (FALSE == raster->isRADAR) {
            // bathy
            S52_GL_drawRaster(raster);
            continue;
        }

#ifdef S52_USE_RADAR
        double cLat = 0.0;
        double cLng = 0.0;
        double rNM  = 0.0;

        raster->texAlpha = raster->RADAR_cb(&cLat, &cLng, &rNM);

        double xyz[3] = {cLng, cLat, 0.0};
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            PRINTF("WARNING: S57_geo2prj3dv() failed\n");
            return FALSE;
        }
        raster->cLng = xyz[0];
        raster->cLat = xyz[1];
        raster->rNM  = rNM;

        // set extent for filter in drawRaster()
        S52_GL_getPRJView(&raster->S, &raster->W, &raster->N, &raster->E);

        S52_GL_drawRaster(raster);
#endif
    }

    return TRUE;
}
#endif  // S52_USE_GLES2

static int        _drawLayer(_extent ext, int layer)
{
    // all cells --larger region first
    for (guint i=_cellList->len; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);

    //guint n = _cellList->len-1;
    //for (guint i=n; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        if (TRUE == _intersec(c->ext, ext)) {

            // one layer
            for (int k=S52_AREAS; k<N_OBJ_T; ++k) {
            //for (int k=0; k<N_OBJ_T; ++k) {
                GPtrArray *rbin = c->renderBin[layer][k];

                // one object
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

                        // doing this after the draw because draw() will parse the text
                        if (TRUE == S52_PL_hasText(obj))
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

static int        _drawLights(void)
// draw all lights of all cells outside view extend
// so that sector and legs show up on screen event if
// the light itself is outside
// FIXME: all are S52_RAD_OVER by default, check if UNDER RADAR make sens
{
    if (TRUE == _doCullLights)
        _cullLights();
    _doCullLights = FALSE;

    // DRAW (use normal filter (SCAMIN,Supp,..) on all object of all cells)

    // this way all the lights sector are drawn
    // light outside but with part of sector visible are drawn
    // but light bellow a cell is not drawn
    // also light are drawn last (ie after all cells)
    // so a sector is not shoped by an other cell next to it

    for (guint i=_cellList->len; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);

    //guint n = _cellList->len-1;
    //for (guint i=n; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // a cell can have no lights sector
        if (NULL == c->lights_sector)
            continue;

        // FIXME: use for_each()
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            // SCAMIN & PLib (disp prio)
            if (TRUE != S52_GL_isSupp(obj)) {
                S57_geo *geo = S52_PL_getGeo(obj);
                if (TRUE != S57_getSup(geo))
                    S52_GL_draw(obj, NULL);
            }
        }
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

        // FIXME: isinf()
        //if (INFINITY==c->ext.W || -INFINITY==c->ext.W)
        //    return FALSE;

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

static int        _draw()
// draw object inside view
// then draw object's text
{
    //for (guint i=_cellList->len; i>0; --i) {
    for (guint i=_cellList->len; i>1; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);

    // FIXME: assume that len > 0 because of MARINER
    //guint n = _cellList->len-1;
    //for (guint i=n; i>0; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        g_atomic_int_get(&_atomicAbort);
        if (TRUE == _atomicAbort) {
            PRINTF("abort drawing .. \n");
            return TRUE;
        }

        // FIXME: GOURD 1 - face of earth - sort and then glDraw() on a whole surface
        // APP/CURL must reset sort if color change by user
        // draw under radar
        g_ptr_array_foreach(c->objList_supp, (GFunc)S52_GL_draw, NULL);

#ifdef S52_USE_GLES2
        // draw radar (raster)
        if (1.0 == S52_MP_get(S52_MAR_DISP_RASTER))
            _drawRaster();
#endif

        // draw over radar
        g_ptr_array_foreach(c->objList_over, (GFunc)S52_GL_draw, NULL);

        // draw text
        // FIXME: implicit call to S52_PL_hasText() again
        g_ptr_array_foreach(c->textList,     (GFunc)S52_GL_drawText, NULL);
    }

    return TRUE;
}

DLL int    STD S52_draw(void)
{
    // debug
    //PRINTF("DRAW: start ..\n");

    S52_CHECK_INIT;

    EGL_BEG(DRAW);


    // do not wait if an other thread is allready drawing
#ifdef S52_USE_ANDROID
    if (FALSE == g_static_mutex_trylock(&_mp_mutex)) {
#else
    if (FALSE == g_mutex_trylock(&_mp_mutex)) {
#endif
        PRINTF("WARNING: trylock failed\n");
        goto exit;
    }

    //
    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");

        g_assert(0);
        goto exit;
    }

    //  check if we are shutting down
    if (NULL == _marinerCell) {
        PRINTF("shutting down\n");

        g_assert(0);
        goto exit;
    }

    // debug
    //PRINTF("DRAW: start ..\n");

    g_timer_reset(_timer);

    int ret = FALSE;

    if (TRUE == S52_GL_begin(S52_GL_DRAW)) {

        //PRINTF("S52_draw() .. -1.2-\n");

        //////////////////////////////////////////////
        // APP:  .. update object
        _app();

        //////////////////////////////////////////////
        // CULL: .. supress display of object (eg outside view)
        _extent ext;
#ifdef S52_USE_PROJ
        projUV uv1, uv2;
        S52_GL_getPRJView(&uv1.v, &uv1.u, &uv2.v, &uv2.u);

        // test - optimisation using viewPort to draw area
        //    S52_CMD_WRD_FILTER_AC = 1 << 3,   // 001000 - AC
        /*
        int x, y, width, height;
        if (S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER)) {
            S52_GL_getViewPort(&x, &y, &width, &height);
            S52_GL_setViewPort(0, 0, width, 200);
            double newN = (200/height) * (uv2.v - uv1.v);
            S52_GL_setPRJView(uv1.v, uv1.u, uv1.v + newN, uv2.u);
        }
        //*/

        // convert view extent to deg
        uv1 = S57_prj2geo(uv1);
        uv2 = S57_prj2geo(uv2);
        ext.S = uv1.v;
        ext.W = uv1.u;
        ext.N = uv2.v;
        ext.E = uv2.u;


        // debug - anti-meridian
        //if (ext.W > ext.E) {
        //    ext.W = ext.W - 360.0;
        //}

        _cull(ext);
#endif

        //PRINTF("S52_draw() .. -1.3-\n");

        //////////////////////////////////////////////
        // DRAW: .. render

        if (TRUE == S52_MP_get(S52_MAR_DISP_OVERLAP)) {
            for (int layer=0; layer<S52_PRIO_NUM; ++layer) {
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

        S52_GL_end(S52_GL_DRAW);

        // for each cell, not after all cell,
        // because city name appear twice
        // FIXME: cull object of overlapping region of cell of DIFFERENT nav pourpose
        // NOTE: no culling of object of overlapping region of cell of SAME nav pourpose
        // display priority 8
        // FIX: it's seem like a HO could handle this, but when zoomig-out it's a S52 overlapping symb. probleme

        //PRINTF("S52_draw() .. -2-\n");

        ret = TRUE;

        // test - optimisation
        //S52_GL_setViewPort(x, y, width, height);

    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");
    }

    // FIXME: timing for APP: CULL: DRAW:
    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("    DRAW: %.0f msec --------------------------------------\n", sec * 1000);

exit:
    GMUTEXUNLOCK(&_mp_mutex);

#if !defined(S52_USE_RADAR)
    EGL_END(DRAW);
#endif

    return ret;
}

static void       _delOldVessel(gpointer data, gpointer user_data)
{
    S52_obj *obj = (S52_obj *)data;
    S57_geo *geo = S52_PL_getGeo(obj);

    if ((0==g_strcmp0("vessel", S57_getName(geo))) ||
        (0==g_strcmp0("afgves", S57_getName(geo)))) {

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

    return;
}

static int        _drawLast(void)
{
    // debug
    //PRINTF("DRAWLAST: ..  -2-\n");

    // then draw the Mariners' Object on top of it
    for (int i=S52_AREAS; i<N_OBJ_T; ++i) {
        GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][i];
        // FIFO
        //for (guint idx=0; idx<rbin->len; ++idx) {
        // LIFO: so that 'cursor' is drawn last (on top)
        for (guint idx=rbin->len; idx>0; --idx) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx-1);

            g_atomic_int_get(&_atomicAbort);
            if (TRUE == _atomicAbort) {
                PRINTF("abort drawing .. \n");
                return TRUE;
            }

            // in some graphic driver this is expensive
            if (FALSE == S52_GL_isSupp(obj)) {
                S52_GL_draw(obj, NULL);
                S52_GL_drawText(obj, NULL);
            }
        }
    }

    return TRUE;
}

DLL int    STD S52_drawLast(void)
{
    // debug
    //PRINTF("DRAWLAST: .. start -0-\n");
    //return TRUE;

    S52_CHECK_INIT;

    if (S52_MAR_DISP_LAYER_LAST_NONE == S52_MP_get(S52_MAR_DISP_LAYER_LAST))
        return TRUE;

#if !defined(S52_USE_RADAR)
    EGL_BEG(LAST);
#endif

#ifdef S52_USE_ANDROID
    // do not wait if an other thread is allready drawing
    if (FALSE == g_static_mutex_trylock(&_mp_mutex)) {
#else
    if (FALSE == g_mutex_trylock(&_mp_mutex)) {
#endif
        PRINTF("WARNING: trylock failed\n");
        //g_assert(0);
        goto exit;
    }

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        g_assert(0);
        goto exit;
    }

    //  check if we are shuting down
    if (NULL == _marinerCell) {
        PRINTF("Shutting down\n");
        g_assert(0);
        goto exit;
    }

    g_atomic_int_set(&_atomicAbort, FALSE);

    g_timer_reset(_timer);

    ////////////////////////////////////////////////////////////////////
    // APP: init the journal flush previous journal
    // rebuilding CS if need be
    _app();

    // check stray vessel (occur when s52ais restart)
    if (0.0 != S52_MP_get(S52_MAR_DEL_VESSEL_DELAY)) {
        GPtrArray *rbinPT = _marinerCell->renderBin[S52_PRIO_MARINR][S52_POINT];
        g_ptr_array_foreach(rbinPT, _delOldVessel, rbinPT);
        GPtrArray *rbinLN = _marinerCell->renderBin[S52_PRIO_MARINR][S52_LINES];
        g_ptr_array_foreach(rbinLN, _delOldVessel, rbinLN);
    }

    ////////////////////////////////////////////////////////////////////
    // no CULL (so no journal)
    // cull()


    ////////////////////////////////////////////////////////////////////
    // DRAW:
    //

    int ret = FALSE;
    if (TRUE == S52_GL_begin(S52_GL_LAST)) {
        ret = _drawLast();
        S52_GL_end(S52_GL_LAST);
    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");
    }

    // debug
    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("DRAWLAST: %.0f msec\n", sec * 1000);

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    EGL_END(LAST);

    return ret;
}

#ifdef S52_USE_GV
static int        _drawObj(const char *name)
{
    // all cells --larger region first
    guint n = _cellList->len;
    for (guint i=0; i<n; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // one cell
        for (int j=0; j<S52_PRIO_NUM; ++j) {

            // one layer
            for (int k=S52_AREAS; k<N_OBJ_T; ++k) {
            //for (int k=0; k<N_OBJ_T; ++k) {
                GPtrArray *rbin = c->renderBin[j][k];

                // one object
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // debug
                    //PRINTF("%s\n", S57_getName(geo));

                    //if (0==S52_strncmp(name, S57_getName(geo), 6))
                    if (0 == g_strcmp0(name, S57_getName(geo)))
                        S52_GL_draw(obj);

                    //if (TRUE == S52_PL_hasText(obj))
                    //    g_ptr_array_add(_textList, obj);

                }
            }
        }
    }

    return TRUE;
}
#endif  // S52_USE_GV

DLL int    STD S52_drawLayer(const char *name)
{
    PRINTF("name: %s\n", name);

#ifdef S52_USE_GV

    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }


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

    if (TRUE == S52_GL_begin(S52_GL_DRAW)) {

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
        _cull(ext);
#endif

        //////////////////////////////////////////////
        // DRAW .. render

        _drawObj(name);
        //_drawText();

        // done rebuilding CS
        _doCS = FALSE;

        S52_GL_end(S52_GL_DRAW);
    }

    GMUTEXUNLOCK(&_mp_mutex);
#endif

    return TRUE;
}

DLL int    STD S52_drawStr(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
{
    S52_CHECK_INIT;

    return_if_null(colorName);
    return_if_null(str);

    EGL_BEG(STR);

    S52_CHECK_MUTX;

    //PRINTF("X:%f Y:%f color:%s bsize:%i str:%s\n", pixels_x, pixels_y, colorName, bsize, str);

    // FIXME: check x,y
    S52_GL_drawStrWin(pixels_x, pixels_y, colorName, bsize, str);

    GMUTEXUNLOCK(&_mp_mutex);

    EGL_END(STR);

    return TRUE;
}

DLL int    STD S52_setEGLcb(EGL_cb eglBeg, EGL_cb eglEnd, void *EGLctx)
{
    (void)eglBeg;
    (void)eglEnd;
    (void)EGLctx;

#ifdef S52_USE_EGL
    PRINTF("set EGL_cb .. \n");

    _eglBeg = eglBeg;
    _eglEnd = eglEnd;
    _EGLctx = EGLctx;

#endif

    return TRUE;
}

DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    S52_CHECK_INIT;
    EGL_BEG(BLIT);

    // FIXME: try lock skip touch - appent when broken EGL does a glFinish() in EGL swap
    S52_CHECK_MUTX;

    int ret = FALSE;

    // debug
    PRINTF("scale_x:%f, scale_y:%f, scale_z:%f, north:%f\n", scale_x, scale_y, scale_z, north);

    if (1.0 < ABS(scale_x)) {
        PRINTF("zoom factor X overflow (>1.0) [%f]\n", scale_x);
        goto exit;
    }

    if (1.0 < ABS(scale_y)) {
        PRINTF("zoom factor Y overflow (>1.0) [%f]\n", scale_y);
        goto exit;
    }

    if (0.5 < ABS(scale_z)) {
        PRINTF("zoom factor Z overflow (>0.5) [%f]\n", scale_z);
        goto exit;
    }

    if ((north<0.0) || (360.0<=north)) {
        PRINTF("WARNING: north (%f), reset to %f\n", north, _view.north);
        // FIXME: get the real value
        north = _view.north;
        //north = 0.0;
    }

    g_timer_reset(_timer);

    if (TRUE == S52_GL_begin(S52_GL_BLIT)) {
        S52_GL_drawBlit(scale_x, scale_y, scale_z, north);

        S52_GL_end(S52_GL_BLIT);
        ret = TRUE;
    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");
    }


    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("DRAWBLIT: %.0f msec\n", sec * 1000);


exit:
    GMUTEXUNLOCK(&_mp_mutex);

    EGL_END(BLIT);

    return ret;
}

static int        _win2prj(double *pixels_x, double *pixels_y)
{
    int x;
    int y;
    int width;
    int height;

    S52_GL_getViewPort(&x, &y, &width, &height);


    // FIXME: coordinate correction varie from cursor to cursor!
    // FIXME: NO AXIS TRANSFORT FROM NOW ON - experimantal
    //*pixels_y  = height - *pixels_y - 1;
    //*pixels_x += 1.35;

    if (FALSE == S52_GL_win2prj(pixels_x, pixels_y))
        return FALSE;

    return TRUE;
}

DLL int    STD S52_xy2LL(double *pixels_x, double *pixels_y)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // check bound
    if (FALSE == _validate_screenPos(pixels_x, pixels_y)) {
        PRINTF("WARNING: _validate_screenPos() failed\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    if (FALSE == _win2prj(pixels_x, pixels_y)) {
        PRINTF("WARNING: _win2prj() failed\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    projXY uv = {*pixels_x, *pixels_y};
    uv = S57_prj2geo(uv);
    *pixels_x = uv.u;
    *pixels_y = uv.v;

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_LL2xy(double *longitude, double *latitude)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    double xyz[3] = {*longitude, *latitude, 0.0};
    if (FALSE == S57_geo2prj3dv(1, xyz)) {
        PRINTF("WARNING: S57_geo2prj3dv() failed\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    S52_GL_prj2win(&xyz[0], &xyz[1]);

    *longitude = xyz[0];
    *latitude  = xyz[1];

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setView(double cLat, double cLon, double rNM, double north)
{
    S52_CHECK_INIT;

    // debug
    PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);

    S52_CHECK_MUTX;

    /*
    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: call failed, no cell loaded to project view .. use S52_loadCell() first\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }
    */

    //*
    if (ABS(cLat) > 90.0) {
        PRINTF("WARNING: cLat outside [-90..+90](%f)\n", cLat);
        GMUTEXUNLOCK(&_mp_mutex);
        //g_assert(0);
        return FALSE;
    }

    if (ABS(cLon) > 180.0) {
        PRINTF("WARNING: cLon outside [-180..+180] (%f)\n", cLon);
        GMUTEXUNLOCK(&_mp_mutex);
        //g_assert(0);
        return FALSE;
    }
    //*/

    if (rNM < 0) {
        rNM = _view.rNM;
    } else {
        if ((rNM < MIN_RANGE) || (rNM > MAX_RANGE)) {
            PRINTF("WARNING:  rNM outside limit (%f)\n", rNM);
            GMUTEXUNLOCK(&_mp_mutex);
            //g_assert(0);
            return FALSE;
        }
    }

    // FIXME: PROJ4 will explode here (INFINITY) for mercator
    // Note: must validate rNM first
    if ((ABS(cLat)*60.0 + rNM) > (90.0*60)) {
        PRINTF("WARNING: rangeNM > 90*60 NM (%f)\n", rNM);
        GMUTEXUNLOCK(&_mp_mutex);
        //g_assert(0);
        return FALSE;
    }

    if (north < 0) {
        north = _view.north;
    } else {
        if ((north>=360.0) || (north<0.0)) {
            PRINTF("WARNING: north outside [0..360[ (%f)\n", north);
            GMUTEXUNLOCK(&_mp_mutex);
            //g_assert(0);
            return FALSE;
        }
    }

    // debug
    //PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);

    S52_GL_setView(cLat, cLon, rNM, north);

    // update local var _view
    _view.cLat  = cLat;
    _view.cLon  = cLon;
    _view.rNM   = rNM;
    _view.north = north;

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_getView(double *cLat, double *cLon, double *rNM, double *north)
{
    S52_CHECK_INIT;
    return_if_null(cLat);
    return_if_null(cLon);
    return_if_null(rNM);
    return_if_null(north);
    S52_CHECK_MUTX;

    /*
    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: call failed, no cell loaded to project view .. use S52_loadCell() first\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }
    */

    // update local var _view
    *cLat  = _view.cLat;
    *cLon  = _view.cLon;
    *rNM   = _view.rNM;
    *north = _view.north;
    /*
    double LLv;
    double LLu;
    double URv;
    double URu;
    S52_GL_getGEOView(&LLv, &LLu, &URv, &URu);
    *cLat  = URv - LLv;
    *cLon  = URu - LLu;
    *rNM   = ((URv - LLv)/ 2.0) * 60.0;
    // FIXME: get the real value
    *north = 0.0;
    */

    // debug
    PRINTF("lat:%f, long:%f, range:%f north:%f\n", *cLat, *cLon, *rNM, *north);

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    PRINTF("pixels_x:%i, pixels_y:%i, pixels_width:%i, pixels_height:%i\n", pixels_x, pixels_y, pixels_width, pixels_height);

    //_validate_screenPos(&x, &y);

    S52_GL_setViewPort(pixels_x, pixels_y, pixels_width, pixels_height);

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

static int        _getCellsExt(_extent* extSum)
{
    int ret = FALSE;
    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // Note: skip speudo ENC is cleaner than adjusting idx for diff config
        // for now just skip these pseudo cells
        if (0 == g_strcmp0(MARINER_CELL, c->filename->str))
            continue;
        if (0 == g_strcmp0(WORLD_SHP,    c->filename->str))
            continue;

        // first pass init extent
        if (FALSE == ret) {
            extSum->S = c->ext.S;
            extSum->W = c->ext.W;
            extSum->N = c->ext.N;
            extSum->E = c->ext.E;

            ret = TRUE;
            continue;
        }

        // handle the rest

        // N-S limits
        if (extSum->N < c->ext.N)
            extSum->N = c->ext.N;
        if (extSum->S > c->ext.S)
            extSum->S = c->ext.S;


        //------------------- E-W limits ------------------------

        // new cell allready inside ext
        if ((extSum->W < c->ext.W) && (c->ext.E < extSum->E))
            continue;

        // new cell totaly cover ext
        if ((c->ext.W < extSum->W) && (extSum->E < c->ext.E)) {
            extSum->W = c->ext.W;
            extSum->E = c->ext.E;
            continue;
        }

        // expand to cover new cell extent
        //
        //                               B2
        //                        A    w|--|e
        //        B1           w|---|e
        //      w|--|e
        //                            C
        //                     w|----------|e
        //    |----------|-----------|..........|..........|
        //    0         180         360        540        720
        //       |------ d1 ----|- d2 --|
        //

        double Aw  = extSum->W + 180.0;
        double B1w = c->ext.W  + 180.0;
        double B2w = B1w + 360.0;

        double Ae  = extSum->E + 180.0;
        double B1e = c->ext.E  + 180.0;
        double B2e = B1e + 360.0;


        // dist 1  >  dist 2
        if ((Aw - B1w) > (B2w - Aw))
            extSum->E = c->ext.E;
        else
            extSum->W = c->ext.W;

        // dist 1  >  dist 2
        if ((Ae - B1e) > (B2e - Ae))
            extSum->E = c->ext.E;
        else
            extSum->W = c->ext.W;

    }

#ifdef S52_USE_WORLD
    // if only world is loaded
    if (FALSE == ret) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, _cellList->len-1);

        extSum->S = c->ext.S;
        extSum->W = c->ext.W;
        extSum->N = c->ext.N;
        extSum->E = c->ext.E;

        ret = TRUE;
    }
#endif

    return ret;
}

DLL int    STD S52_getCellExtent(const char *filename, double *S, double *W, double *N, double *E)
{
    S52_CHECK_INIT;

    if (NULL==S || NULL==W || NULL==N || NULL==E) {
        PRINTF("WARNING: NULL extent S,W,N,E\n");
        return FALSE;
    }
    S52_CHECK_MUTX;

    if (NULL == filename) {
        _extent ext;
        _getCellsExt(&ext);
        *S = ext.S;
        *W = ext.W;
        *N = ext.N;
        *E = ext.E;

        PRINTF("ALL EXT(S,W - N,E): %f, %f -- %f, %f\n", *S, *W, *N, *E);
    } else {
        gchar *fnm   = g_strdup(filename);
        gchar *fname = g_strstrip(fnm);

        // strip path
        gchar *name = g_path_get_basename(fname);

        // check if cell loaded
        if (FALSE == _isCellLoaded(name)) {
            PRINTF("WARNING: file not loaded (%s)\n", name);
            g_free(fnm);
            g_free(name);

            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }

        for (guint idx=0; idx<_cellList->len; ++idx) {
            _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

            if (0 == g_strcmp0(name, c->filename->str)) {
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
        g_free(name);
    }

    GMUTEXUNLOCK(&_mp_mutex);

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
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }

    GMUTEXUNLOCK(&_mp_mutex);

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
        GMUTEXUNLOCK(&_mp_mutex);
        return -1;
    }

    GMUTEXUNLOCK(&_mp_mutex);

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
        GMUTEXUNLOCK(&_mp_mutex);
        return -1;
    }

    GMUTEXUNLOCK(&_mp_mutex);

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
        GMUTEXUNLOCK(&_mp_mutex);
        return -1;
    }

    GMUTEXUNLOCK(&_mp_mutex);

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
        GMUTEXUNLOCK(&_mp_mutex);
        return -1;
    }

    if (TRUE==value  && S52_SUP_ON  == supState) {
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }
    if (FALSE==value && S52_SUP_OFF == supState) {
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }

    GMUTEXUNLOCK(&_mp_mutex);

    S52_toggleObjClass(className);

    return TRUE;
}
// -----------------------------------------------------


DLL int    STD S52_loadPLib(const char *plibName)
// (re)load a PLib
// Note: allow to reload a PLib to overwrite rules
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // 1 - load / parse new PLb
    valueBuf PLibPath = {'\0'};
    if (NULL == plibName) {
        if (0 == S52_getConfig(CONF_PLIB, &PLibPath)) {
            PRINTF("default PLIB not found in .cfg (%s)\n", CONF_PLIB);
            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        } else {
            if (TRUE == S52_PL_load(PLibPath)) {
                g_string_append_printf(_plibNameList, ",%s", PLibPath);
            } else {
                GMUTEXUNLOCK(&_mp_mutex);
                return FALSE;
            }
        }
    } else {
        if (TRUE == S52_PL_load(plibName)) {
            g_string_append_printf(_plibNameList, ",%s", plibName);
        } else {
            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }
    }

    // 2 - relink S57 objects to the new rendering rules (S52)
    for (guint k=0; k<_cellList->len; ++k) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, k);

        _cell n;  // new cell
        //bzero(&n, sizeof(_cell));
        memset(&n, 0, sizeof(_cell));

        // init new render bin
        for (int i=0; i<S52_PRIO_NUM; ++i) {
            for (int j=0; j<N_OBJ_T; ++j)
                n.renderBin[i][j] = g_ptr_array_new();
        }

        // insert obj in new cell
        for (int i=0; i<S52_PRIO_NUM; ++i) {
            for (int j=0; j<N_OBJ_T; ++j) {
                GPtrArray *rbin = c->renderBin[i][j];
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    _insertS57Obj(&n, obj, geo);
                }
                // flush old rbin
                g_ptr_array_free(rbin, TRUE);
            }
        }

        // transfert rbin
        for (int i=0; i<S52_PRIO_NUM; ++i) {
            for (int j=0; j<N_OBJ_T; ++j) {
                c->renderBin[i][j] = n.renderBin[i][j];
            }
        }

        if (NULL != c->lights_sector) {
            for (guint i=0; i<c->lights_sector->len; ++i) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, i);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    _insertS57Obj(&n, obj, geo);
            }
            g_ptr_array_free(c->lights_sector, TRUE);
        }
        c->lights_sector = n.lights_sector;
    }

    // signal to rebuild all cmd
    _doCS = TRUE;

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}


//-------------------------------------------------------
//
// FEEDBACK TO HIGHER UP MODULE OF INTERNAL STATE
//


DLL cchar *STD S52_pickAt(double pixels_x, double pixels_y)
{
    // viewport
    int x;
    int y;
    int width;
    int height;

    _extent ext;          // pick extent
    double ps,pw,pn,pe;   // hold PRJ view
    double gs,gw,gn,ge;   // hold GEO view
    double oldAA = 0.0;

    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    if (NULL == _cellList || 0 == _cellList->len || 1 == _cellList->len) {
        PRINTF("WARNING: no cell loaded\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    // check bound
    if (FALSE == _validate_screenPos(&pixels_x, &pixels_y)) {
        PRINTF("WARNING: coord out of scteen\n");
        GMUTEXUNLOCK(&_mp_mutex);
        g_assert(0);
        return FALSE;
    }

    g_timer_reset(_timer);

    // if blending is ON the cursor pick will not work
    oldAA = S52_MP_get(S52_MAR_ANTIALIAS);
    S52_MP_set(S52_MAR_ANTIALIAS, FALSE);

    {   // compute pick view parameter

        // mouse has 'Y' down, opengl is up
        S52_GL_getViewPort(&x, &y, &width, &height);
        double tmp_px_y = y + pixels_y;

        // debug
        PRINTF("pixels_x:%f, pixels_y:%f\n", pixels_x, pixels_y);

        // FIXME: check bound
        // Nexus/Adreno ReadPixels must be POT, hence 8 x 8 extent
        ext.N = tmp_px_y + 4;
        ext.S = tmp_px_y - 4;
        ext.E = pixels_x + 3;
        ext.W = pixels_x - 3;
        S52_GL_win2prj(&ext.W, &ext.S);
        S52_GL_win2prj(&ext.E, &ext.N);

        // save current view
        S52_GL_getPRJView(&ps, &pw, &pn, &pe);

        // set view of the pick (PRJ)
        S52_GL_setViewPort(pixels_x-4, tmp_px_y-4, 8, 8);
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

            S52_GL_getGEOView(&gs, &gw, &gn, &ge);         // hold GEO view
            S52_GL_setGEOView(ext.S, ext.W, ext.N, ext.E);

            PRINTF("PICK LL EXTENT (swne): %f, %f  %f, %f \n", ext.S, ext.W, ext.N, ext.E);
        }
#endif

    }


    if (TRUE == S52_GL_begin(S52_GL_PICK)) {
        _nTotal = 0;
        _nCull  = 0;

        // filter out objects that don't intersec the pick view
        _cull(ext);
        // bebug
        //PRINTF("nbr of object culled: %i (%i)\n", _nCull, _nTotal);

        // render object that fall in the pick view
        _draw();

        // Mariners' (layer 9 - Last)
        // FIXME: cull
        _drawLast();

        S52_GL_end(S52_GL_PICK);
    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");
    }

    // get object picked
    const char *name = S52_GL_getNameObjPick();
    PRINTF("OBJECT PICKED (%6.1f, %6.1f): %s\n", pixels_x, pixels_y, name);


    // restore view
    // FIXME: setPRJview is snapped to the current ViewPort aspect ratio
    // so ViewPort value as to be set before PRJView
    S52_GL_setViewPort(x, y, width, height);
    S52_GL_setPRJView(ps, pw, pn, pe);
    S52_GL_setGEOView(gs, gw, gn, ge);

    // replace original blending state
    S52_MP_set(S52_MAR_ANTIALIAS, oldAA);

    gdouble sec = g_timer_elapsed(_timer, NULL);
    PRINTF("     PICKAT: %.0f msec\n", sec * 1000);

    GMUTEXUNLOCK(&_mp_mutex);

    //return NULL; // debug
    return name;
}

DLL cchar *STD S52_getPLibNameList(void)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char * str = _plibNameList->str;

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL cchar *STD S52_getPalettesNameList(void)
// return a string of palettes name loaded
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char *str = NULL;
    int palTblsz    = S52_PL_getPalTableSz();

    g_string_set_size(_paltNameList, 0);

    for (int i=0; i<palTblsz; ++i) {
        char *frmt = (0 == i) ? "%s" : ",%s";
        g_string_append_printf(_paltNameList, frmt, (char*)S52_PL_getPalTableNm(i));
    }

    str = _paltNameList->str;

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

static GString   *_getMARINClassList()
{
    GString *classList = g_string_new(MARINER_CELL);

    for (int i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = _marinerCell->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                const char *oname = S52_PL_getOBCL(obj);
                if (NULL == S52_strstr(classList->str, oname)) {
                    g_string_append_printf(classList, ",%s", oname);
                }
            }
        }
    }

    return classList;
}

//DLL const char * STD S52_getS57ObjClassList(const char *cellName)
DLL cchar *STD S52_getS57ClassList(const char *cellName)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char * str = NULL;
    g_string_set_size(_S57ClassList, 0);

    for (guint idx=0; idx<_cellList->len; ++idx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, idx);

        if (NULL == cellName) {
            if (0 == _S57ClassList->len)
                g_string_append_printf(_S57ClassList, "%s",  c->S57ClassList->str);
            else
                g_string_append_printf(_S57ClassList, ",%s", c->S57ClassList->str);
        } else {
            // check if filename is loaded
            if (0 == g_strcmp0(cellName, c->filename->str)) {
                // special case
                if (0 == g_strcmp0(MARINER_CELL, c->filename->str)) {
                    GString *classList = _getMARINClassList();

                    g_string_printf(_S57ClassList, "%s", classList->str);

                    g_string_free(classList, TRUE);

                    str = _S57ClassList->str;

                    GMUTEXUNLOCK(&_mp_mutex);

                    return str;
                }
                // normal cell
                if (NULL != c->S57ClassList) {
                    g_string_printf(_S57ClassList, "%s,%s", c->filename->str, c->S57ClassList->str);

                    str = _S57ClassList->str;

                    GMUTEXUNLOCK(&_mp_mutex);

                    return str;
                }
            }
        }
    }

    if (0 != _S57ClassList->len)
        str = _S57ClassList->str;

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL cchar *STD S52_getObjList(const char *cellName, const char *className)
{
    S52_CHECK_INIT;

    return_if_null(cellName);
    return_if_null(className);

    S52_CHECK_MUTX;

    const char *str    = NULL;
    int         header = TRUE;

    PRINTF("cellName: %s, className: %s\n", cellName, className);

    // a _S52ObjList in fact
    //g_string_set_size(_S57ClassList, 0);
    g_string_set_size(_S52ObjNmList, 0);

    for (guint cidx=0; cidx<_cellList->len; ++cidx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, cidx);

        if (0 == g_strcmp0(cellName, c->filename->str)) {
            for (guint i=0; i<S52_PRIO_NUM; ++i) {
                for (guint j=0; j<N_OBJ_T; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                        const char *oname = S52_PL_getOBCL(obj);
                        if (0 == g_strcmp0(className, oname)) {
                            if (TRUE == header) {
                                //g_string_printf(_S57ClassList, "%s,%s", cellName, className);
                                g_string_printf(_S52ObjNmList, "%s,%s", cellName, className);
                                header = FALSE;
                            }
                            S57_geo *geo = S52_PL_getGeo(obj);
                            //  S57ID / geo / disp cat / disp prio
                            //g_string_append_printf(_S57ClassList, ",%i:%c:%c:%i",
                            g_string_append_printf(_S52ObjNmList, ",%i:%c:%c:%i",
                                                   S57_getGeoID(geo),
                                                   S52_PL_getFTYP(obj),    // same as 'j', but in text equivalent
                                                   S52_PL_getDISC(obj),    //
                                                   S52_PL_getDPRI(obj));   // same as 'i'
                        }
                    }
                }
            }
            //PRINTF("%s\n", _S57ClassList->str);
            //str = _S57ClassList->str;
            PRINTF("%s\n", _S52ObjNmList->str);
            str = _S52ObjNmList->str;
            GMUTEXUNLOCK(&_mp_mutex);

            return str;
        }
    }

    GMUTEXUNLOCK(&_mp_mutex);

    return NULL;
}

DLL cchar *STD S52_getAttList(unsigned int S57ID)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // FIXME: put in array of s57id  (what to do if ENC is unloaded)
    PRINTF("S57ID: %i\n", S57ID);
    const char *str = NULL;

    for (guint cidx=0; cidx<_cellList->len; ++cidx) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, cidx);

        for (guint i=0; i<S52_PRIO_NUM; ++i) {
            for (guint j=0; j<N_OBJ_T; ++j) {
                GPtrArray *rbin = c->renderBin[i][j];
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);
                    if (S57ID == S57_getGeoID(geo)) {
                        str = S57_getAtt(geo);
                        goto exit;
                    }
                }
            }
        }
    }

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL cchar *STD S52_getCellNameList(void)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    const char *str = NULL;
    GError *error;
    g_string_set_size(_cellNameList, 0);

    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, i);

        // MARINER_CELL has no encPath
        if (NULL == c->encPath)
            continue;

        /*
        if (0 == _cellNameList->len)
            g_string_append_printf(_cellNameList, "\"*%s\"",  c->encPath);
        else
            g_string_append_printf(_cellNameList, ",\"*%s\"", c->encPath);
        */

        if (0 == _cellNameList->len)
            g_string_append_printf(_cellNameList, "*%s",  c->encPath);
        else
            g_string_append_printf(_cellNameList, ",*%s", c->encPath);

        gchar *path = g_path_get_dirname(c->encPath);
        GDir  *dir  = g_dir_open(path, 0, &error);
        if (NULL != dir) {
            const gchar *file = NULL;
            while (NULL != (file = g_dir_read_name(dir))) {
                // S57 cell name in 8.3 format
                if (12 != strlen(file))
                    continue;

                // base cell NOT loaded
                if ((0==g_strcmp0(file+8, ".000")) && (FALSE==_isCellLoaded(file))) {
                    // check if allready included
                    if (NULL == g_strrstr(_cellNameList->str, file)) {
                        //g_string_append_printf(_cellNameList, ",\"%s/%s\"", path, file);
                        g_string_append_printf(_cellNameList, ",%s/%s", path, file);
                    }
                }
            }
            g_dir_close(dir);
        }
        g_free(path);
    }

    if (0 != _cellNameList->len)
        str = _cellNameList->str;

    GMUTEXUNLOCK(&_mp_mutex);

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

    GMUTEXUNLOCK(&_mp_mutex);

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

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setRADARCallBack(S52_RADAR_cb cb, unsigned int texRadius)
// experimental - load raw raster RADAR via callback
{
    (void)cb;
    (void)texRadius;
#ifdef S52_USE_RADAR

    S52_CHECK_INIT;

    return_if_null(cb);

    S52_CHECK_MUTX;

    PRINTF("cb:#lX, texRadius:%u\n", (long unsigned int)cb, texRadius);

    // prevent dup or dispose
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *raster = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        if (cb == raster->RADAR_cb) {
            if (0 == texRadius) {
                S52_GL_delRaster(raster, TRUE);
                g_ptr_array_remove_index_fast(_rasterList, i);
                g_free(raster);
                GMUTEXUNLOCK(&_mp_mutex);
                return TRUE;
            } else {
                GMUTEXUNLOCK(&_mp_mutex);
                return FALSE;
            }
        }
    }

    // not found - create new
    S52_GL_ras *raster = g_new0(S52_GL_ras, 1);
    raster->isRADAR    = TRUE;
    raster->RADAR_cb   = cb;
    raster->npotX      = texRadius * 2;  // N/S
    raster->npotY      = texRadius * 2;  // E/W
    raster->gdtSz      = 1;    // 1 byte Alpha
    g_ptr_array_add(_rasterList, raster);

    GMUTEXUNLOCK(&_mp_mutex);

#endif

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
    // FIXME: optimisation - cull Mariners' Object

    // FIXME: crossing of anti-meridian
    // FIXME: set a init flag in _extent
    _extent ext = {INFINITY, INFINITY, -INFINITY, -INFINITY};

    for (guint i=0; i<xyznbr; ++i) {
        // X - longitude
        ext.W = (ext.W < *xyz) ? ext.W : *xyz;
        ext.E = (ext.E > *xyz) ? ext.E : *xyz;

        // Y - latitude
        ++xyz;
        ext.S = (ext.S < *xyz) ? ext.S : *xyz;
        ext.N = (ext.N > *xyz) ? ext.N : *xyz;

        // Z - skip
        ++xyz;

        // next X
        ++xyz;
    }

    S57_setExt(geo, ext.W, ext.S, ext.E, ext.N);

    return TRUE;
}

static S52_obj   *_isObjValid(_cell *c, S52_obj *obj)
// return  obj if the oject is in cell else NULL
// Used to validate User Mariners' Object
{
    // FIXME: refactor - objH could be an index into a GPtrArray of the real struct
    // what happend if array is full !
    // if del_fast(array) is used then the last objH in the array has now
    // the index of the deleted one
    // else let the array grow upto guint (at least 2^32 objH - if 64bits system then 2^64)
    // and signal ARRAY_FULL in

    for (int i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = c->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *o = (S52_obj *)g_ptr_array_index(rbin, idx);

                if (obj == o)
                    return obj;
            }
        }
    }

    PRINTF("WARNING: object handle not found\n");

    return NULL;
}

static int        _isObjNameValid(S52ObjectHandle obj, const char *objName)
// return TRUE if the class name of 'obj' is 'objName' else FALSE
{
    S57_geo *geo = S52_PL_getGeo(obj);
    if (0 != g_strcmp0(objName, S57_getName(geo)))
        return FALSE;

    return TRUE;
}

struct _user_data {
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
// traverse all visible object
{
    struct _user_data  udata;
    udata.S57ID    = S57ID;
    udata.obj      = NULL;

    //guint n = _cellList->len-1;
    //for (guint i=n; i>0; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
    for (guint i=_cellList->len; i>0; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
        g_ptr_array_foreach(c->objList_supp, (GFunc)_compS57ID, &udata);
        g_ptr_array_foreach(c->objList_over, (GFunc)_compS57ID, &udata);

        // obj found - no need to go further
        if (NULL != udata.obj)
            return udata.obj;
    }

    {   // obj not found - search Mariners' Object List (mostly on layer 9)
        for (int i=0; i<N_OBJ_T; ++i) {
            // FIXME: not all on layer 9 (S52_PRIO_MARINR) !!
            GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][i];

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
    S52_CHECK_MUTX;

    int ret = FALSE;

    if (0 == S57ID)
        S52_GL_dumpS57IDPixels(toFilename, NULL, width, height);
    else {
        S52_obj *obj = _getS52obj(S57ID);
        if (NULL != obj)
            ret = S52_GL_dumpS57IDPixels(toFilename, obj, width, height);
    }

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
                                      unsigned int xyznbr, double *xyz, const char *listAttVal)
{

    S57_geo     *geo     = NULL;
    double      *gxyz    = NULL;
    double     **ggxyz   = NULL;
    guint       *gxyznbr = NULL;
    S52_obj     *obj     = NULL;

    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    // here we can load mariners' object event if no ENC are loaded yet
    //if (NULL == _cellList || 0 == _cellList->len) {
    if (NULL == _marinerCell) {
        PRINTF("WARNING: no cell loaded .. no cell to project this object to\n");
        goto exit;
    }

    // FIXME: check that "plibObjName" is a valid name
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

    if (0 != xyznbr) {

        // transfer and project xyz
        if (NULL != xyz) {
            double *dst  = NULL;
            double *src  = xyz;

            dst = gxyz = g_new(double, xyznbr*3);
            for (guint npt=0; npt<(xyznbr*3); ++npt) {
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
                PRINTF("WARNING: projection failed\n");
                g_assert(0);
                exit(0);

                // FIXME: if we get here free mem
                return NULL;
            }
#endif
        }
        else {
            // create an empty xyz buffer
            gxyz = g_new0(double, xyznbr*3);

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

    // full of coordinate
    if (NULL != xyz) {
        S57_setGeoSize(geo, xyznbr);
        _setExt(geo, xyznbr, xyz);
    } else {
        S57_setGeoSize(geo, 0);      // not really needed since geo struct is initialize to 0
    }

    if (NULL != listAttVal)
        _setAtt(geo, listAttVal);

    // SCAMIN Mariners' object
    //S57_setScamin(geo, 0.0);

    // debug - imediatly destroy it
    //S57_doneData(geo);

    obj = S52_PL_newObj(geo);
    _insertS52Obj(_marinerCell, obj);

    // set timer for afterglow
    if (0 == g_strcmp0("afgves", S57_getName(geo)))
        S52_PL_setTimeNow(obj);

    // init TX & TE
    S52_PL_resetParseText(obj);

    // redo CS, because some object might have a CS command word (ex leglin)
    _doCS = TRUE;

    // debug
    //PRINTF("objH:%p, plibObjName:%s, ID:%i, objType:%i, xyznbr:%u, xyz:%p, listAttVal:<%s>\n",
    //        obj,     plibObjName, S57_getGeoID(geo), objType, xyznbr, gxyz,
    //       (NULL==listAttVal)?"NULL":listAttVal);

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    return (S52ObjectHandle)obj;
}

DLL S52ObjectHandle STD S52_getMarObjH(unsigned int S57ID)
{
    S52_CHECK_INIT;
    S52_CHECK_MUTX;

    if (0 == S57ID) {
        PRINTF("WARNING: invalid S57ID [%i]\n", S57ID);
        GMUTEXUNLOCK(&_mp_mutex);
        return NULL;
    }

    for (int i=0; i<S52_PRIO_NUM; ++i) {
        for (int j=0; j<N_OBJ_T; ++j) {
            GPtrArray *rbin = _marinerCell->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                S57_geo *geo = S52_PL_getGeo(obj);
                if (S57ID == S57_getGeoID(geo)) {
                    GMUTEXUNLOCK(&_mp_mutex);
                    return (S52ObjectHandle)obj;
                }
            }
        }
    }

    GMUTEXUNLOCK(&_mp_mutex);

    return NULL;
}

static
    S52ObjectHandle        _updateGeo(S52ObjectHandle objH, double *xyz)
{
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
        guint    npt = 0;
        double  *ppt = NULL;
        S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);
        S57_getGeoData(geo, 0, &npt, &ppt);

        for (guint i=0; i<(npt*3); ++i)
            *ppt++ = *xyz++;
    }

    return objH;
}

DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    PRINTF("objH:%p\n", objH);

    // validate this obj and remove if found
    S52_obj *obj = (S52_obj *)objH;
    if (NULL == _removeObj(_marinerCell, obj)) {
        PRINTF("WARNING: couldn't delete .. objH not in Mariners' Object List\n");
        GMUTEXUNLOCK(&_mp_mutex);

        return objH;  // contrairy to other call return objH when fail
    }                 // so caller can further process it

    // queue obj for deletion in next APP() cycle
    g_ptr_array_add(_objToDelList, obj);

    GMUTEXUNLOCK(&_mp_mutex);

    return (S52ObjectHandle)NULL;
}

DLL S52ObjectHandle STD S52_toggleDispMarObj(S52ObjectHandle  objH)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)objH);
    if (NULL != obj) {
        S57_geo *geo = S52_PL_getGeo(obj);

        if (TRUE == S57_getSup(geo))
            S57_setSup(geo, FALSE);
        else
            S57_setSup(geo, TRUE);
    } else {
        objH = FALSE;
    }

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_newCLRLIN(int catclr, double latBegin, double lonBegin, double latEnd, double lonEnd)
{
    S52_CHECK_INIT;
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
            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }

        // FIXME: what happen if speed change!?
        if (0.0 != wholinDist)
            SPRINTF(attval, "select:%i,plnspd:%f,_wholin_dist:%f", select, plnspd, wholinDist);
        else
            SPRINTF(attval, "select:%i,plnspd:%f", select, plnspd);

        S52ObjectHandle leglin = S52_newMarObj("leglin", S52_LINES, 2, xyz, attval);

        // NOTE: FALSE == (VOID*)NULL or FALSE == (int) 0
        if (FALSE != previousLEGLIN) {
            S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)previousLEGLIN);
            if (NULL == obj) {
                PRINTF("WARNING: previousLEGLIN not a valid S52ObjectHandle\n");
                previousLEGLIN = FALSE;
            } else {
                S52_PL_setNextLeg((S52_obj*)previousLEGLIN, (S52_obj*)leglin);
            }

        }

        GMUTEXUNLOCK(&_mp_mutex);

        return leglin;
    }
}

DLL S52ObjectHandle STD S52_newOWNSHP(const char *label)
{
    S52_CHECK_INIT;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    return_if_null((void*)label);  // what if we need to erase label!

    char   attval[256];

    // FIXME: get the real value
    //double xyz[3] = {_view.cLon, _view.cLat, 0.0};      // quiet the warning in S52_newMarObj()
    double xyz[3] = {0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()

    SPRINTF(attval, "_vessel_label:%s", label);

    // only one OWNSHP (bug if we want 2 GPS shown)
    if (NULL == _OWNSHP)
        _OWNSHP = S52_newMarObj("ownshp", S52_POINT, 1, xyz, attval);

    return _OWNSHP;
}

DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d)
{
    S52_CHECK_INIT;

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

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = NULL;
        goto exit;
    }

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
    }

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_setVector(S52ObjectHandle objH,  int vecstb, double course, double speed)
{
    S52_CHECK_INIT;

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

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = NULL;
        goto exit;
    }

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
    GMUTEXUNLOCK(&_mp_mutex);

    // debug
    //PRINTF("fini\n");

    return objH;
}

DLL S52ObjectHandle STD S52_newPASTRK(int catpst, unsigned int maxpts)
{
    S52_CHECK_INIT;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    PRINTF("catpst:%i\n", catpst);

    char attval[256];
    SPRINTF(attval, "catpst:%i", catpst);

    S52ObjectHandle pastrk = S52_newMarObj("pastrk", S52_LINES, maxpts, NULL, attval);

    return pastrk;
}

static
    S52ObjectHandle STD    _setPointPosition(S52ObjectHandle objH, double latitude, double longitude, double heading)
{
    //PRINTF("-0- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);
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
    _updateGeo(objH, xyz);

    // optimisation (out of the blue): set 'orient' obj var directly
    S52_PL_setSYorient((S52_obj *)objH, heading);

    //PRINTF("-2- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);
    {
        char   attval[256];
        SPRINTF(attval, "headng:%f", heading);
        _setAtt(geo, attval);
    }


    //S52_GL_setOWNSHP(obj, heading);

    //PRINTF("-3- latitude:%f, longitude:%f, heading:%f\n", latitude, longitude, heading);

    return objH;
}

DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // debug
    PRINTF("objH:%p, latitude:%f, longitude:%f, data:%f\n", objH, latitude, longitude, data);
    latitude  = _validate_lat(latitude);
    longitude = _validate_lon(longitude);
    //time      = _validate_min(time);

    S52_obj *obj = _isObjValid(_marinerCell, (S52_obj *)objH);
    if (NULL == obj) {
        objH = FALSE;
        GMUTEXUNLOCK(&_mp_mutex);
        return objH;
    }

    S57_geo *geo = S52_PL_getGeo(obj);

    // reset timer for AIS and its aflterglow
    if ((0==g_strcmp0("vessel", S57_getName(geo))) ||
        (0==g_strcmp0("afgves", S57_getName(geo)))) {
        S52_PL_setTimeNow(obj);
    }

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
        _setPointPosition(objH, latitude, longitude, data);
        if (0 == g_strcmp0("cursor", S57_getName(geo))) {
            char attval[80] = {'\0'};
            SPRINTF(attval, "_cursor_label:%f%c %f%c", fabs(latitude), (latitude<0)? 'S':'N', fabs(longitude), (longitude<0)? 'W':'E');
            _setAtt(geo, attval);
            S52_PL_resetParseText(obj);
        }
    }
    else // LINE AREA
    {
        double   xyz[3] = {longitude, latitude, 0.0};
        guint    sz     = S57_getGeoSize(geo);

        guint    npt    = 0;
        double  *ppt    = NULL;

        if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }

#ifdef S52_USE_PROJ
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            PRINTF("WARNING: S57_geo2prj3dv() fail\n");

            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
        }
#endif

        if (sz < npt) {
            ppt[sz*3 + 0] = xyz[0];
            ppt[sz*3 + 1] = xyz[1];
            ppt[sz*3 + 2] = data;
            S57_setGeoSize(geo, sz+1);
        } else {
            // FIFO - if sz == npt, shift npt-1 coord
            g_memmove(ppt, ppt+3, (npt-1) * sizeof(double) * 3);
            ppt[((npt-1) * 3) + 0] = xyz[0];
            ppt[((npt-1) * 3) + 1] = xyz[1];
            ppt[((npt-1) * 3) + 2] = data;
        }
    }

    GMUTEXUNLOCK(&_mp_mutex);

    //PRINTF("-1- objH:%#lX, latitude:%f, longitude:%f, data:%f\n", (long unsigned int)objH, latitude, longitude, data);

    /*
    if (_SELECTED == objH) {
        // FIXME: get the real value
        S52_setView(latitude, longitude, _view.rNM, _view.north);
        S52_draw();
    }
    */

    return objH;
}

DLL S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label)
{
    S52_CHECK_INIT;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    // debug
    //label = NULL;
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
            SPRINTF(attval, "vesrce:%i,_vessel_label: ", vesrce);
        } else {
            SPRINTF(attval, "vesrce:%i,_vessel_label:%s", vesrce, label);
        }

        vessel = S52_newMarObj("vessel", S52_POINT, 1, xyz, attval);
        S52_PL_setTimeNow(vessel);
    }

    PRINTF("vessel objH: %lu\n", vessel);

    return vessel;
}

DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);
    return_if_null((void*)newLabel);  // what if we need to erase label!

    S52_CHECK_MUTX;

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = FALSE;
        goto exit;
    }

    // commented for debugging - clutter output
    //PRINTF("label:%s\n", newLabel);

    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        char attval[256] = {'\0'};
        S57_geo *geo = S52_PL_getGeo(objH);

        SPRINTF(attval, "[_vessel_label,%s]", newLabel);
        _setAtt(geo, attval);

        // FIXME:
        S52_PL_resetParseText((S52_obj *)objH);
    } else {
        PRINTF("WARNING: not a 'ownshp' or 'vessel' object\n");
        objH = FALSE;
    }

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    return objH;

}

DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    // debug
    PRINTF("vesselSelect:%i, vestat:%i, vesselTurn:%i\n", vesselSelect, vestat, vesselTurn);

    //double lat;
    //double lon;
    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = FALSE;
        goto exit;
    }

    if (TRUE == _isObjNameValid(objH, "ownshp") || TRUE == _isObjNameValid(objH, "vessel")) {
        char  attval[60] = {'\0'};
        char *attvaltmp  = attval;
        S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);

        // validate vesselSelect:
        if (0!=vesselSelect && 1!=vesselSelect && 2!=vesselSelect) {
            PRINTF("WARNING: 'vesselSelect' must be 0, 1 or 2 .. reset to 1 (selected)\n");
            vesselSelect = 1;
        }
        if (1 == vesselSelect) {
            SPRINTF(attvaltmp, "_vessel_select:Y,");
            _SELECTED = objH;

            /*
            guint    npt    = 0;
            double  *ppt    = NULL;

            if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
                GMUTEXUNLOCK(&_mp_mutex);
                return FALSE;
            }
            if (1 != npt) {
                GMUTEXUNLOCK(&_mp_mutex);
                return FALSE;
            }
            */
            // FIXME: setView()/draw() immediatly so that the user
            // get feedback sooner than the next pushPos (witch could never come)
        }
        if (2 == vesselSelect) {
            SPRINTF(attvaltmp, "_vessel_select:N,");
            _SELECTED = FALSE;  // NULL
        }

        attvaltmp += S52_strlen(attvaltmp);

        // validate vestat (Vessel Status): 1 AIS active, 2 AIS sleeping
        if (0!=vestat && 1!=vestat && 2!=vestat && 3!=vestat) {
            PRINTF("WARNING: 'vestat' must be 0, 1, 2 or 3.. reset to 1\n");
            vestat = 1;
        }
        if (1==vestat || 2==vestat || 3==vestat ) {
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
    GMUTEXUNLOCK(&_mp_mutex);

    /*
    if (_SELECTED == objH) {

        S52_setView(, , _view.rNM, _view.north);
        S52_draw();
    }
    */

    return objH;
}

DLL int             STD S52_newCSYMB(void)
{
    S52_CHECK_INIT;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    const char   *attval = NULL;
    double        pos[3] = {0.0, 0.0, 0.0};

    if (FALSE == _iniCSYMB) {
        PRINTF("WARNING: CSYMB allready initialize\n");
        return FALSE;
    }

    // FIXME: should it be global ?
    attval = "$SCODE:SCALEB10";     // 1NM
    _SCALEB10 = S52_newMarObj("$CSYMB", S52_POINT, 1, pos, attval);

    attval = "$SCODE:SCALEB11";     // 10NM
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

DLL S52ObjectHandle STD S52_newVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin)
{
    S52_CHECK_INIT;
    //S52_CHECK_MUTX;  // mutex in S52_newMarObj()

    // debug
    PRINTF("vrm:%i, ebl:%i, normalLineStyle:%i, setOrigin:%i\n", vrm, ebl, normalLineStyle, setOrigin);


    S52ObjectHandle vrmebl = FALSE;

    char attval[256];
    if (TRUE == setOrigin) {
        // initialy when user set origine
        SPRINTF(attval, "%s%c,%s%c,%s",
                "_normallinestyle:", ((TRUE == normalLineStyle)    ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl) ? 'Y' : 'N'),
                "_setOrigin:Init"
               );
    } else {
        SPRINTF(attval, "%s%c,%s%c,%s",
        //SPRINTF(attval, "%s%c,%s%c",
                "_normallinestyle:", ((TRUE == normalLineStyle)    ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl) ? 'Y' : 'N'),
                "_setOrigin:N"
               );
    }

    if (FALSE==vrm && FALSE==ebl) {
        PRINTF("WARNING: nothing to do\n");
        return FALSE;
    }

    //double xyz[6] = {lonA, latA, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()
    //double xyz[6] = {INFINITY, INFINITY, 0.0, 0.0, 0.0, 0.0};      // quiet the warning in S52_newMarObj()

    if (TRUE == ebl) {
        //vrmebl = S52_newMarObj("ebline", S52_LINES, 2, xyz, attval);
        vrmebl = S52_newMarObj("ebline", S52_LINES, 2, NULL, attval);
    } else {
        //vrmebl = S52_newMarObj("vrmark", S52_LINES, 2, xyz, attval);
        vrmebl = S52_newMarObj("vrmark", S52_LINES, 2, NULL, attval);
    }

    return vrmebl;
}

DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y, double *brg, double *rge)
{
    S52_CHECK_INIT;

    return_if_null((void*)objH);

    S52_CHECK_MUTX;

    if (NULL == _isObjValid(_marinerCell, (S52_obj *)objH)) {
        objH = FALSE;
        goto exit;
    }

    if (TRUE != _isObjNameValid(objH, "ebline") && TRUE != _isObjNameValid(objH, "vrmark")) {
        PRINTF("WARNING: not a 'ebline' or 'vrmark' object\n");
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }

    double latA = 0.0;
    double lonA = 0.0;
    double latB = pixels_y;
    double lonB = pixels_x;
    _win2prj(&lonB, &latB);

    guint    npt = 0;
    double  *ppt = NULL;
    S57_geo *geo = S52_PL_getGeo((S52_obj *)objH);
    S57_getGeoData(geo, 0, &npt, &ppt);

    GString *setOriginstr = S57_getAttVal(geo, "_setOrigin");
    char     c            = *setOriginstr->str;

    switch (c) {
    case 'I':    // Init freely moveable origin
        lonA = lonB;
        latA = latB;
        _setAtt(geo, "_setOrigin:Y"); // CHECK THIS: does setOriginstr->str become dandling ??
        break;
    case 'Y':    // user set freely moveable origin
        lonA = ppt[0];
        latA = ppt[1];
        break;
    case 'N':    // _OWNSHP origin (FIXME: apply ownshp offset set by S52_setDimension())
        if (NULL != _OWNSHP) {
            guint    npt = 0;
            double  *ppt = NULL;
            S57_geo *geo = S52_PL_getGeo(_OWNSHP);
            S57_getGeoData(geo, 0, &npt, &ppt);
            lonA = ppt[0];
            latA = ppt[1];
        } else {
            // FIXME: get the real value
            lonA = _view.cLon;
            latA = _view.cLat;
            //g_assert(0);
        }
        break;
    }

    {
        double xyz[6] = {lonA, latA, 0.0, lonB, latB, 0.0};
        double dist   = sqrt(pow(xyz[3]-xyz[0], 2) + pow(xyz[4]-xyz[1], 2));
        double deg    = ATAN2TODEG(xyz);
        char   unit   = 'm';
        char attval[256] = {'\0'};

        _updateGeo(objH, xyz);

        // in Nautical Mile if > 1852m (1NM)
        if (dist >  1852) {
            dist /= 1852;
            unit  = 'M';
        }

        if (deg < 0)
            deg += 360;

        SPRINTF(attval, "_vrmebl_label:%.1f deg / %.1f%c", deg, dist, unit);
        _setAtt(geo, attval);
        S52_PL_resetParseText((S52_obj *)objH);

        if (NULL != brg) *brg = deg;
        if (NULL != rge) *rge = dist;
    }

exit:
    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}


#ifdef S52_USE_DBUS

// ------------ DBUS API  -----------------------
//
// duplicate some S52.h, mostly used for testing Mariners' Object
// async command and thread (here dbus)
// Probably not async since it use glib main loop!
//

// FIXME: use GDBus (in Gio) instead (thread prob with low-level DBus API)

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

        double *xyz = g_new0(double, xyznbr*3);
        char  **tmp = str;
        for (guint i=0; i<strnbr; i+=3) {
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
         PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPLibsIDList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getPLibsIDList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPalettesNameList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getPalettesNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getCellNameList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getCellNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getS57ClassList    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *cellName;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &cellName, DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getS57ClassList(cellName);
    if (NULL == str) {
        PRINTF("FIXME: S52_getS57ClassList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
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
        PRINTF("FIXME: S52_getObjList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char *str = S52_getAttList(s57id);
    if (NULL == str) {
        PRINTF("FIXME: S52_getAttList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
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
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
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
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadCell(str, NULL);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_loadCell() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadPLib(str);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_loadPLib() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
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
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_dumpS57IDPixels(fname, s57id, width, height);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_dumpS57IDPixels() return FALSE\n");
        //g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
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
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getS57ClassList")) {
        return _dbus_getS57ClassList(dbus, message, user_data);
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

    // listening to messages from all objects, as no path is specified
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_draw'",                &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_drawLast'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getRGB'",              &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setVESSELstate'",      &_dbusError);

    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPLibsIDList'",      &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPalettesNameList'", &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getCellNameList'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getS57ClassList'",     &_dbusError);
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

    if (FALSE == dbus_connection_add_filter(_dbus, _dbus_selectCall, NULL, NULL)) {
        PRINTF("fail .. \n");
        exit(0);
    }

    return TRUE;
}
#endif // S52_USE_DBUS

#ifdef S52_USE_PIPE
static gboolean            _pipeReadWrite(GIOChannel *source, GIOCondition condition, gpointer user_data)
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

static int                 _pipeWatch(gpointer dummy)
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


// -----------------------------------------------------------------
// listen to socket
//
#ifdef S52_USE_SOCK
#include <gio/gio.h>
#include "parson.h"

#define BLOCK 2048

static gchar               _setErr(char *err, gchar *errmsg)
{
    //_err[0] = '\0';
    g_snprintf(err, BLOCK, "libS52.so:%s", errmsg);

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

static int                 _handle_method(const gchar *str, char *result, char *err)
// call the correponding S52_* function named 'method' in JSON object
// here 'method' meen function name (or command name)
// SL4A call it 'method' since JAVA is OOP
// expect:{"id":1,"method":"S52_*","params":["???"]}
// return: id, error msg in 'error' and a JSON array in 'result'
{
    // FIXME: use btree for name/function lookup
    // -OR-
    // FIXME: order cmdName by frequency


    // debug
    //PRINTF("------------------\n");
    //PRINTF("instr->str:%s", instr->str);

    // reset error string --> 'no error'
    err[0] = '\0';

    // JSON parser
    JSON_Value *val = json_parse_string(str);
    if (NULL == val) {
        _setErr(err, "can't parse json str");
        _encode(result, "[0]");

        PRINTF("WARNING: json_parse_string() failed:%s", str);
        return 0;
    }

    // init JSON Object
    JSON_Object *obj      = json_value_get_object(val);
    double       id       = json_object_get_number(obj, "id");

    // get S52_* Command Name
    const char  *cmdName  = json_object_dotget_string(obj, "method");
    if (NULL == cmdName) {
        _setErr(err, "no cmdName");
        _encode(result, "[0]");
        goto exit;
    }

    //PRINTF("JSON cmdName:%s\n", cmdName);

    // start work - fetch cmdName parameters
    JSON_Array *paramsArr = json_object_get_array(obj, "params");
    if (NULL == paramsArr)
        goto exit;

    // get the number of parameters
    size_t      count     = json_array_get_count (paramsArr);

    // FIXME: check param type


    // ---------------------------------------------------------------------
    //
    // call command - return answer to caller
    //

    //S52ObjectHandle STD S52_newOWNSHP(const char *label);
    //if (0 == S52_strncmp(cmdName, "S52_newOWNSHP", strlen("S52_newOWNSHP"))) {
    if (0 == g_strcmp0(cmdName, "S52_newOWNSHP")) {
        const char *label = json_array_get_string (paramsArr, 0);
        if ((NULL==label) || (1!=count)) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newOWNSHP(label);
        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);
    if (0 == g_strcmp0(cmdName, "S52_newVESSEL")) {
        if (2 != count) {
            _setErr(err, "params 'vesrce'/'label' not found");
            goto exit;
        }

        double      vesrce = json_array_get_number(paramsArr, 0);
        const char *label  = json_array_get_string(paramsArr, 1);
        if (NULL == label) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newVESSEL(vesrce, label);
        _encode(result, "[%lu]", (long unsigned int *) objH);
        //PRINTF("objH: %lu\n", objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel);
    if (0 == g_strcmp0(cmdName, "S52_setVESSELlabel")) {
        if (2 != count) {
            _setErr(err, "params 'objH'/'newLabel' not found");
            goto exit;
        }

        const char *label = json_array_get_string (paramsArr, 1);
        if (NULL == label) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        objH = S52_setVESSELlabel(objH, label);
        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data);
    if (0 == g_strcmp0(cmdName, "S52_pushPosition")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'latitude'/'longitude'/'data' not found");
            goto exit;
        }

        long unsigned int lui     = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH      = (S52ObjectHandle) lui;
        double          latitude  = json_array_get_number(paramsArr, 1);
        double          longitude = json_array_get_number(paramsArr, 2);
        double          data      = json_array_get_number(paramsArr, 3);

        objH = S52_pushPosition(objH, latitude, longitude, data);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);
    if (0 == g_strcmp0(cmdName, "S52_setVector")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'vecstb'/'course'/'speed' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH   = (S52ObjectHandle) lui;
        double          vecstb = json_array_get_number(paramsArr, 1);
        double          course = json_array_get_number(paramsArr, 2);
        double          speed  = json_array_get_number(paramsArr, 3);

        objH = S52_setVector(objH, vecstb, course, speed);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);
    if (0 == g_strcmp0(cmdName, "S52_setDimension")) {
        if (5 != count) {
            _setErr(err, "params 'objH'/'a'/'b'/'c'/'d' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        double          a     = json_array_get_number(paramsArr, 1);
        double          b     = json_array_get_number(paramsArr, 2);
        double          c     = json_array_get_number(paramsArr, 3);
        double          d     = json_array_get_number(paramsArr, 4);

        objH = S52_setDimension(objH, a, b, c, d);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;

    }

    //S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn);
    if (0 == g_strcmp0(cmdName, "S52_setVESSELstate")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'vesselSelect'/'vestat'/'vesselTurn' not found");
            goto exit;
        }

        long unsigned int lui        = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH         = (S52ObjectHandle) lui;
        double          vesselSelect = json_array_get_number(paramsArr, 1);
        double          vestat       = json_array_get_number(paramsArr, 2);
        double          vesselTurn   = json_array_get_number(paramsArr, 3);

        objH = S52_setVESSELstate(objH, vesselSelect, vestat, vesselTurn);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);
    if (0 == g_strcmp0(cmdName, "S52_delMarObj")) {
        if (1 != count) {
            _setErr(err, "params 'objH' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle   objH = (S52ObjectHandle) lui;
        objH = S52_delMarObj(objH);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    // FIXME: not all param paresed
    //S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
    //                                     unsigned int xyznbrmax, double *xyz, const char *listAttVal);
    if (0 == g_strcmp0(cmdName, "S52_newMarObj")) {
        if (3 != count) {
            _setErr(err, "params 'plibObjName'/'objType'/'xyznbrmax' not found");
            goto exit;
        }

        const char *plibObjName = json_array_get_string(paramsArr, 0);
        double      objType     = json_array_get_number(paramsArr, 1);
        double      xyznbrmax   = json_array_get_number(paramsArr, 2);
        double     *xyz         = NULL;
        gchar      *listAttVal  = NULL;


        S52ObjectHandle objH = S52_newMarObj(plibObjName, objType, xyznbrmax, xyz, listAttVal);

        // debug
        //PRINTF("S52_newMarObj -> objH: %lu\n", (long unsigned int *) objH);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //const char * STD S52_getPalettesNameList(void);
    if (0 == g_strcmp0(cmdName, "S52_getPalettesNameList")) {
        const char *palListstr = S52_getPalettesNameList();

        _encode(result, "[\"%s\"]", palListstr);

        //PRINTF("%s", result);

        goto exit;
    }

    //const char * STD S52_getCellNameList(void);
    if (0 == g_strcmp0(cmdName, "S52_getCellNameList")) {
        const char *cellNmListstr = S52_getCellNameList();

        _encode(result, "[\"%s\"]", cellNmListstr);

        //PRINTF("%s", result);

        goto exit;
    }

    //double STD S52_getMarinerParam(S52MarinerParameter paramID);
    if (0 == g_strcmp0(cmdName, "S52_getMarinerParam")) {
        if (1 != count) {
            _setErr(err, "params 'paramID' not found");
            goto exit;
        }

        double paramID = json_array_get_number(paramsArr, 0);

        double d = S52_getMarinerParam(paramID);

        _encode(result, "[%f]", d);

        //PRINTF("%s", result);

        goto exit;
    }

    //int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val);
    if (0 == g_strcmp0(cmdName, "S52_setMarinerParam")) {
        if (2 != count) {
            _setErr(err, "params 'paramID'/'val' not found");
            goto exit;
        }

        double paramID = json_array_get_number(paramsArr, 0);
        double val     = json_array_get_number(paramsArr, 1);

        double d = S52_setMarinerParam(paramID, val);

        _encode(result, "[%f]", d);

        //PRINTF("%s", result);

        goto exit;
    }

    //DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north);
    if (0 == g_strcmp0(cmdName, "S52_drawBlit")) {
        if (4 != count) {
            _setErr(err, "params 'scale_x'/'scale_y'/'scale_z'/'north' not found");
            goto exit;
        }

        double scale_x = json_array_get_number(paramsArr, 0);
        double scale_y = json_array_get_number(paramsArr, 1);
        double scale_z = json_array_get_number(paramsArr, 2);
        double north   = json_array_get_number(paramsArr, 3);

        int ret = S52_drawBlit(scale_x, scale_y, scale_z, north);
        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_drawBlit(): %s\n", result);

        goto exit;
    }

    //int    STD S52_drawLast(void);
    if (0 == g_strcmp0(cmdName, "S52_drawLast")) {
        int i = S52_drawLast();

        _encode(result, "[%i]", i);

        //PRINTF("SOCK:S52_drawLast(): res:%s\n", result);

        goto exit;
    }

    //int    STD S52_draw(void);
    if (0 == g_strcmp0(cmdName, "S52_draw")) {
        int i = S52_draw();

        _encode(result, "[%i]", i);

        //PRINTF("SOCK:S52_draw(): res:%s\n", result);

        goto exit;
    }

    //int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);
    if (0 == g_strcmp0(cmdName, "S52_getRGB")) {
        if (1 != count) {
            _setErr(err, "params 'colorName' not found");
            goto exit;
        }

        const char *colorName  = json_array_get_string(paramsArr, 0);

        unsigned char R;
        unsigned char G;
        unsigned char B;
        int ret = S52_getRGB(colorName, &R, &G, &B);

        PRINTF("%i, %i, %i\n", R,G,B);

        if (TRUE == ret)
            _encode(result, "[%i,%i,%i]", R, G, B);
        else
            _encode(result, "[0]");

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_setTextDisp(int dispPrioIdx, int count, int state);
    if (0 == g_strcmp0(cmdName, "S52_setTextDisp")) {
        if (3 != count) {
            _setErr(err, "params 'dispPrioIdx' / 'count' / 'state' not found");
            goto exit;
        }

        double dispPrioIdx = json_array_get_number(paramsArr, 0);
        double count       = json_array_get_number(paramsArr, 1);
        double state       = json_array_get_number(paramsArr, 2);

        _encode(result, "[%i]", S52_setTextDisp(dispPrioIdx, count, state));

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_getTextDisp(int dispPrioIdx);
    if (0 == g_strcmp0(cmdName, "S52_getTextDisp")) {
        if (1 != count) {
            _setErr(err, "params 'dispPrioIdx' not found");
            goto exit;
        }

        double dispPrioIdx = json_array_get_number(paramsArr, 0);

        _encode(result, "[%i]", S52_getTextDisp(dispPrioIdx));

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_loadCell        (const char *encPath,  S52_loadObject_cb loadObject_cb);
    if (0 == g_strcmp0(cmdName, "S52_loadCell")) {
        if (1 != count) {
            _setErr(err, "params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);

        int ret = S52_loadCell(encPath, NULL);

        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_doneCell        (const char *encPath);
    if (0 == g_strcmp0(cmdName, "S52_doneCell")) {
        if (1 != count) {
            _setErr(err, "params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);

        int ret = S52_doneCell(encPath);

        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_doneCell(): %s\n", result);

        goto exit;
    }

    //const char * STD S52_pickAt(double pixels_x, double pixels_y)
    if (0 == g_strcmp0(cmdName, "S52_pickAt")) {
        if (2 != count) {
            _setErr(err, "params 'pixels_x' or 'pixels_y' not found");
            goto exit;
        }

        double pixels_x = json_array_get_number(paramsArr, 0);
        double pixels_y = json_array_get_number(paramsArr, 1);

        const char *ret = S52_pickAt(pixels_x, pixels_y);

        if (NULL == ret)
            _encode(result, "[0]");
        else {
            _encode(result, "[\"%s\"]", ret);
        }

        goto exit;
    }

    // const char * STD S52_getObjList(const char *cellName, const char *className);
    if (0 == g_strcmp0(cmdName, "S52_getObjList")) {
        if (2 != count) {
            _setErr(err, "params 'cellName'/'className' not found");
            goto exit;
        }

        const char *cellName = json_array_get_string (paramsArr, 0);
        if (NULL == cellName) {
            _setErr(err, "params 'cellName' not found");
            goto exit;
        }
        const char *className = json_array_get_string (paramsArr, 1);
        if (NULL == className) {
            _setErr(err, "params 'className' not found");
            goto exit;
        }

        const char *str = S52_getObjList(cellName, className);
        if (NULL == str)
            _encode(result, "[0]");
        else
            _encode(result, "[\"%s\"]", str);

        goto exit;
    }

    // S52ObjectHandle STD S52_getMarObjH(unsigned int S57ID);
    if (0 == g_strcmp0(cmdName, "S52_getMarObjH")) {
        if (1 != count) {
            _setErr(err, "params 'S57ID' not found");
            goto exit;
        }

        long unsigned int S57ID = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle   objH  = S52_getMarObjH(S57ID);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //const char * STD S52_getAttList(unsigned int S57ID);
    if (0 == g_strcmp0(cmdName, "S52_getAttList")) {
        if (1 != count) {
            _setErr(err, "params 'S57ID' not found");
            goto exit;
        }

        long unsigned int S57ID = (long unsigned int) json_array_get_number(paramsArr, 0);
        const char       *str   = S52_getAttList(S57ID);

        if (NULL == str)
            _encode(result, "[0]");
        else
            _encode(result, "[\"%s\"]", str);

        goto exit;
    }

    //DLL int    STD S52_xy2LL (double *pixels_x,  double *pixels_y);
    if (0 == g_strcmp0(cmdName, "S52_xy2LL")) {
        if (2 != count) {
            _setErr(err, "params 'pixels_x'/'pixels_y' not found");
            goto exit;
        }

        double pixels_x = json_array_get_number(paramsArr, 0);
        double pixels_y = json_array_get_number(paramsArr, 1);

        int ret = S52_xy2LL(&pixels_x, &pixels_y);
        if (TRUE == ret)
            _encode(result, "[%f,%f]", pixels_x, pixels_y);
        else {
            _encode(result, "[0]");
        }

        goto exit;
    }

    //DLL int    STD S52_setView(double cLat, double cLon, double rNM, double north);
    if (0 == g_strcmp0(cmdName, "S52_setView")) {
        if (4 != count) {
            _setErr(err, "params 'cLat'/'cLon'/'rNM'/'north' not found");
            goto exit;
        }

        double cLat  = json_array_get_number(paramsArr, 0);
        double cLon  = json_array_get_number(paramsArr, 1);
        double rNM   = json_array_get_number(paramsArr, 2);
        double north = json_array_get_number(paramsArr, 3);

        int ret = S52_setView(cLat, cLon, rNM, north);
        if (TRUE == ret)
            _encode(result, "[1]");
        else
            _encode(result, "[0]");

        //PRINTF("SOCK:S52_setView(): %s\n", result);

        goto exit;
    }

    //DLL int    STD S52_getView(double *cLat, double *cLon, double *rNM, double *north);
    if (0 == g_strcmp0(cmdName, "S52_getView")) {
        double cLat  = 0.0;
        double cLon  = 0.0;
        double rNM   = 0.0;
        double north = 0.0;

        int ret = S52_getView(&cLat, &cLon, &rNM, &north);

        if (TRUE == ret)
            _encode(result, "[%f,%f,%f,%f]", cLat, cLon, rNM, north);
        else
            _encode(result, "[0]");

        //PRINTF("SOCK:S52_getView(): %s\n", result);

        goto exit;
    }

    //DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height)
    if (0 == g_strcmp0(cmdName, "S52_setViewPort")) {
        if (4 != count) {
            _setErr(err, "params 'pixels_x'/'pixels_y'/'pixels_width'/'pixels_height' not found");
            goto exit;
        }

        double pixels_x      = json_array_get_number(paramsArr, 0);
        double pixels_y      = json_array_get_number(paramsArr, 1);
        double pixels_width  = json_array_get_number(paramsArr, 2);
        double pixels_height = json_array_get_number(paramsArr, 3);

        int ret = S52_setViewPort((int)pixels_x, (int)pixels_y, (int)pixels_width, (int)pixels_height);
        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_setViewPort(): %s\n", result);

        goto exit;
    }



    //
    _encode(result, "[0,\"WARNING:%s(): call not found\"]", cmdName);


exit:
    json_value_free(val);
    return (int)id;
}

static gboolean            _socket_read_write(GIOChannel *source, GIOCondition cond, gpointer user_data)
// FIXME: refactor this mess
{
    // quiet - not used
    (void)user_data;

    GError *error          = NULL;

    // Note: buffer must be local because we can have more than one connection (thread)
    gchar  str_send[BLOCK] = {'\0'};
    gchar  str_read[BLOCK] = {'\0'};
    gchar  response[BLOCK] = {'\0'};
    gchar  result[BLOCK]   = {'\0'};
    gchar  err[BLOCK]      = {'\0'};

    switch(cond) {
    	case G_IO_IN: {

            //PRINTF("G_IO_IN\n");

            gsize length = 0;
            //gsize terminator_pos = 0;
            GIOStatus stat = g_io_channel_read_chars(source, str_read, 1024, &length, &error);

            //G_IO_STATUS_ERROR  An error occurred.
            //G_IO_STATUS_NORMAL Success.
            //G_IO_STATUS_EOF    End of file.
            //G_IO_STATUS_AGAIN  Resource temporarily unavailable.

            // debug
            //PRINTF("GIOStatus:%i, length:%i terminator_pos:%i str_read:%s \n", stat, length, terminator_pos, str_read);
            if (NULL != error) {
                PRINTF("g_io_channel_read_chars(): failed [ret:%i err:%s]\n", stat, error->message);
                return FALSE; //TRUE;
            }
            if (G_IO_STATUS_ERROR == stat) {
                GIOStatus stat = g_io_channel_flush(source, NULL);
                PRINTF("flush GIOStatus:%i\n", stat);
                return FALSE; //TRUE;
            }


            // Not a WebSocket connection - normal JSON handling
            if ('{' == str_read[0]) {
                // FIXME: _encode() & g_snprintf() do basically the same thing and the resulting
                // string is the same .. but only g_snprintf() string pass io channel !!!
                int   id = _handle_method(str_read, result, err);
                guint n  = g_snprintf(response, BLOCK, "{\"id\":%i,\"error\":\"%s\",\"result\":%s}",
                                      id, (err[0] == '\0') ? "no error" : err, result);
                response[n] = '\0';
                //PRINTF("n:%i\n", n);

                gsize bytes_written = 0;
                g_io_channel_write_chars(source, response, n, &bytes_written, &error);
                if (NULL != error) {
                    PRINTF("g_io_channel_write_chars(): failed sending msg [err:%s]\n", error->message);
                    return FALSE;
                }

                // debug
                //PRINTF("sended:%s", _response);
                break;
            }


            // in a WebSocket Frame
            if ('\x81' == str_read[0]) {
                guint len = str_read[1] & 0x7F;
                //PRINTF("in Frame .. first byte: 0x%hhX\n", str_read[0]);
                //PRINTF("Frame: text lenght = %i, (0x%hhX)\n", len, str_read[1]);

                {
                    char *key  = str_read + 2;
                    char *data = key + 4;

                    for (guint i = 0; i<len; ++i)
                        data[i] ^= key[i%4];

                    // devug
                    PRINTF("WebSocket Frame: msg in:%s\n", data);

                    int id = _handle_method(data, result, err);
                    guint n = g_snprintf(response, BLOCK, "{\"id\":%i,\"error\":\"%s\",\"result\":%s}",
                                         id, (err[0] == '\0') ? "no error" : err, result);

                    response[n] = '\0';

                    // debug
                    PRINTF("WebSocket Frame: resp out:%s\n", response);
                }

                guint n = 0;
                unsigned int dataLen = strlen(response);
                if (dataLen <= 125) {
                    // lenght coded with 7 bits <= 125
                    n = g_snprintf(str_send, 1024, "\x81%c%s", (char)(dataLen & 0x7F), response);
                } else {
                    if (dataLen < 65536) {
                        // lenght coded with 16 bits (code 126)
                        // bytesFormatted[1] = 126
                        // bytesFormatted[2] = ( bytesRaw.length >> 8 )
                        // bytesFormatted[3] = ( bytesRaw.length      )
                        n = g_snprintf(str_send, 1024, "\x81%c%c%c%s", 126, (char)(dataLen>>8), (char)dataLen, response);
                    } else {
                        // if need more then 65536 bytes to transfer (code 127)
                        /* dataLen max = 2^64
                        bytesFormatted[1] = 127
                        bytesFormatted[2] = ( bytesRaw.length >> 56 )
                        bytesFormatted[3] = ( bytesRaw.length >> 48 )
                        bytesFormatted[4] = ( bytesRaw.length >> 40 )
                        bytesFormatted[5] = ( bytesRaw.length >> 32 )
                        bytesFormatted[6] = ( bytesRaw.length >> 24 )
                        bytesFormatted[7] = ( bytesRaw.length >> 16 )
                        bytesFormatted[8] = ( bytesRaw.length >>  8 )
                        bytesFormatted[9] = ( bytesRaw.length       )
                        */
                        PRINTF("WebSocket Frame: FIXME: dataLen > 65535 not handled (%i)\n", dataLen);
                        g_assert(0);
                    }
                }

                {   // send response
                    gsize bytes_written = 0;
                    GIOStatus stat = g_io_channel_write_chars(source, str_send, n, &bytes_written, &error);
                    if (G_IO_STATUS_ERROR == stat) {
                        GIOStatus stat = g_io_channel_flush(source, NULL);
                        PRINTF("WebSocket Frame: flush GIOStatus:%i\n", stat);
                        return FALSE; // will close connection
                    }

                    g_io_channel_flush(source, NULL);
                }

                return TRUE;
            }



            {
                // FIXME: find an better way to detect a websocket handshake - sec-websocket-key:
                int   n   = strlen("Sec-WebSocket-Key:");
                char *str = strstr(str_read, "Sec-WebSocket-Key:");
                if (NULL == str) {
                    str = strstr(str_read, "sec-websocket-key:");
                }

                if (NULL != str) {
                    GString *secWebSocketKey = g_string_new(str + n + 1); // FIXME: +1 is for skipping the space
                    secWebSocketKey = g_string_truncate(secWebSocketKey, 24); // shop
                    //PRINTF("secWebSocketKey: %s\n", secWebSocketKey->str);
                    secWebSocketKey = g_string_append(secWebSocketKey, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                    PRINTF("websocket handshake: _SecWebSocketKey>>>%s<<<\n", secWebSocketKey->str);


                    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA1);
                    g_checksum_update(checksum, (const guchar *)secWebSocketKey->str, secWebSocketKey->len);
                    g_string_free(secWebSocketKey, TRUE);

                    guint8 buffer[1024];
                    gsize digest_len = 1023;
                    g_checksum_get_digest(checksum, buffer, &digest_len);
                    gchar *acceptstr = g_base64_encode(buffer, digest_len);

                    GString *respstr = g_string_new("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                                                    "Server: Bla Gateway\r\n"
                                                    "Upgrade: WebSocket\r\n"
                                                    "Connection: Upgrade\r\n"
                                                    "Access-Control-Allow-Origin:http://127.0.0.1:3030\r\n"
                                                    "Access-Control-Allow-Credentials: true\r\n"
                                                    "Access-Control-Allow-Headers: content-type\r\n"
                                                   );

                    g_string_append_printf(respstr,
                                           "Sec-WebSocket-Accept:%s\r\n"
                                           "\r\n",
                                           acceptstr);

                    gsize bytes_written = 0;
                    GIOStatus stat = g_io_channel_write_chars(source, respstr->str, respstr->len, &bytes_written, &error);
                    if (G_IO_STATUS_ERROR == stat) {
                        GIOStatus stat = g_io_channel_flush(source, NULL);
                        PRINTF("flush GIOStatus:%i\n", stat);
                        return FALSE; //TRUE;
                    }

                    // WARNING: multi-thread here so PRINTF is a bit clunky
                    PRINTF("Websocket handshake:stat=%i, send(%i,%i):\n%s\n", stat, respstr->len, bytes_written, respstr->str);

                    g_string_free(respstr, TRUE);

                    if (NULL != error) {
                        //g_object_unref((GSocketConnection*)user_data);
                        PRINTF("Websocket handshake: g_io_channel_write_chars(): failed [ret:%i err:%s]\n", stat, error->message);
                        return FALSE;
                    }

                    g_io_channel_flush(source, NULL);

                    g_free(acceptstr);
                    g_checksum_free(checksum);

                    break;
                }
            }

            PRINTF("FIXME: G_IO_IN not handled (%s)\n", str_read);

            return FALSE;
        }


    	case G_IO_OUT: PRINTF("G_IO_OUT \n"); return FALSE; break;
    	case G_IO_PRI: PRINTF("G_IO_PRI \n"); break;
    	case G_IO_ERR: PRINTF("G_IO_ERR \n"); break;
    	case G_IO_HUP: PRINTF("G_IO_HUP \n"); break;
    	case G_IO_NVAL:PRINTF("G_IO_NVAL\n"); break;
    }


    return TRUE;
    //return FALSE;
}

#define UNUSED(expr) do { (void)(expr); } while (0)
static gboolean            _new_connection(GSocketService    *service,
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

    GError     *error   = NULL;
    GIOStatus   stat    = g_io_channel_set_encoding(channel, NULL, &error);
    if (NULL != error) {
        g_object_unref(connection);
        PRINTF("g_io_channel_set_encoding(): failed [stat:%i err:%s]\n", stat, error->message);
        return FALSE;
    }

    g_io_channel_set_buffered(channel, FALSE);

    g_io_add_watch(channel, G_IO_IN , (GIOFunc)_socket_read_write, connection);

    return FALSE;
}

static int                 _initSock(void)
{
    GError *error = NULL;

    PRINTF("start to listen to socket ..\n");

    // FIXME: check that the glib loop is UP .. or start one
    // FIXME: GLib-CRITICAL **: g_main_loop_is_running: assertion `loop != NULL' failed
    //if (FALSE == g_main_loop_is_running(NULL)) {
    //    PRINTF("DEBUG: main loop is NOT running ..\n");
    //}

    // DEPRECATED
    //g_type_init();

    GSocketService *service        = g_socket_service_new();

    GInetAddress   *address        = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);

    GSocketAddress *socket_address = g_inet_socket_address_new(address, 2950); // GPSD use 2947

    g_socket_listener_add_address(G_SOCKET_LISTENER(service), socket_address, G_SOCKET_TYPE_STREAM,
                                  G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error);

    g_object_unref(socket_address);
    g_object_unref(address);

    if (NULL != error) {
        g_printf("WARNING: g_socket_listener_add_address() failed (%s)\n", error->message);
        return FALSE;
    }

    g_socket_service_start(service);

    g_signal_connect(service, "incoming", G_CALLBACK(_new_connection), NULL);


    return TRUE;
}
#endif
