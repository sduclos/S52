// S52.c: top-level interface to libS52.so
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2017 Sylvain Duclos sduclos@users.sourceforge.net

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


#include "S52.h"        // --
#include "S52utils.h"   // PRINTF(), S52_utils_getConfig()
#include "S52PL.h"      // S52_PRIO_NUM
#include "S52MP.h"      // S52MarinerParameter
#include "S57data.h"    // S57_prj2geo(), S52_geo2prj*(), projXY, S57_geo
#include "S52CS.h"      // S52_CS_*()
#include "S52GL.h"      // S52_GL_draw()

#ifdef S52_USE_GV
#include "S57gv.h"      // S57_gvLoadCell()
#else
#include "S57ogr.h"     // S57_ogrLoadCell()
#endif // S52_USE_GV

#include <string.h>     // memmove(), memcpy()
#include <math.h>       // INFINITY
#include <stdio.h>      // setbuf()
#include <glib.h>       // GString, GArray, GPtrArray, guint64, ..
//#include <gio/gio.h>    // gsetbuf()
#include <glib/gprintf.h> // g_sprintf()
//#include <glib/gstdio.h>  // FILE

// Network
#if defined(S52_USE_SOCK) || defined(S52_USE_DBUS) || defined(S52_USE_PIPE)
#include "_S52.i"
#endif

#include "gdal.h"       // GDAL_RELEASE_NAME and handle Raster

#ifdef S52_USE_PROJ
#include <proj_api.h>   // projUV, projXY, projPJ
#else
typedef struct { double u, v; } projUV;
#define projXY projUV
#define RAD_TO_DEG    57.29577951308232
#define DEG_TO_RAD     0.0174532925199432958
#endif  // S52_USE_PROJ

#define ATAN2TODEG(xyz)   (90.0 - atan2(xyz[4]-xyz[1], xyz[3]-xyz[0]) * RAD_TO_DEG)

static GTimer *_timer = NULL;

// trap signal (ESC abort rendering)
// must be compiled with -std=gnu99 or -std=c99 -D_POSIX_C_SOURCE=199309L
#include <unistd.h>      // getuid()
#include <sys/types.h>
#include <signal.h>      // siginfo_t
#include <locale.h>      // setlocal()
static volatile gint G_GNUC_MAY_ALIAS _atomicAbort;

#ifdef _MINGW
// not available on win32
#else
// 1) SIGHUP	 2) SIGINT	 3) SIGQUIT	 4) SIGILL	 5) SIGTRAP
// 6) SIGABRT	 7) SIGBUS	 8) SIGFPE	 9) SIGKILL	10) SIGUSR1
//11) SIGSEGV	12) SIGUSR2	13) SIGPIPE	14) SIGALRM	15) SIGTERM
static struct sigaction _old_signal_handler_SIGINT;   //  2
static struct sigaction _old_signal_handler_SIGQUIT;  //  3
static struct sigaction _old_signal_handler_SIGTRAP;  //  5
static struct sigaction _old_signal_handler_SIGABRT;  //  6
static struct sigaction _old_signal_handler_SIGKILL;  //  9
static struct sigaction _old_signal_handler_SIGUSR1;  // 10
static struct sigaction _old_signal_handler_SIGSEGV;  // 11
static struct sigaction _old_signal_handler_SIGUSR2;  // 12
static struct sigaction _old_signal_handler_SIGTERM;  // 15
#endif  // _MINGW

// not available on win32
#ifdef S52_USE_BACKTRACE
#if !defined(S52_USE_ANDROID) || !defined(_MINGW)
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

//#define S57_CELL_NAME_MAX_LEN 8 // cell name maximum lenght

typedef struct _cell {
    ObjExt_t   geoExt;     // cell geo extent

    GString   *filename;  // encName/baseName
    gchar     *encPath;   // original user path/name
                          // Note: MARINER_CELL has no encPath

    // S52 Object
    GPtrArray *renderBin[S52_PRIO_NUM][S52_N_OBJ];

    GPtrArray *lights_sector;   // see _doCullLights

    localObj  *local;         // reference to object locality for CS

    // FIXME: extract legend to struct
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
    GString   *dsid_intustr;       // intended usage (nav purp)
    double     dsid_heightOffset;  // bring height datum to depth datum

    // legend from M_CSCL
    GString   *cscalestr;     // compilation scale DSID:DSPM_CSCL or M_CSCL

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
    // FIXME: check if it must be inside a cell
    guint      baseRCID;        // offset of "ConnectedNode" (first primitive)
    GPtrArray *ConnectedNodes;  // Note: ConnectedNodes rcid are random in some case (CA4579016)
    GPtrArray *S57Edges;        // final segment build from ENs and CNs
#endif

    // journal - place holder for object to be drawn (after culling)
    GPtrArray *objList_supp;   // list of object on the "Supress by Radar" layer
    GPtrArray *objList_over;   // list of object on the "Over Radar" layer  (ie on top)
    GPtrArray *textList;       // hold ref to object with text (drawn on top of everything)

    GString   *S57ClassList;   // hold the names of S57 class of this cell

#ifdef S52_USE_PROJ
    int        projDone;       // TRUE this cell has been projected
#endif

    /*
    // optimisation - do CS only on obj affected by a change in a MP
    // instead of resolving the CS logic at render-time.
    // Will help to clean up the render-time code.
    // list of ref of S52_obj that have CS in their LUP
    GPtrArray *DEPARElist;
    GPtrArray *DEPCNTlist;
    GPtrArray *OBSTRNlist;
    GPtrArray *RESARElist;
    GPtrArray *WRECKSlist;
    */

} _cell;

#ifdef S52_USE_C_AGGR_C_ASSO
// BBTree of key/value pair: LNAM --> geo (--> S57ID (for cursor pick))
// BBTree of LANM 'key' with S57_geo as 'value'
static GTree     *_lnamBBT      = NULL;
#endif  // S52_USE_C_AGGR_C_ASSO

static GPtrArray *_cellList     = NULL;    // list of loaded cells - sorted, big to small scale (small to large region)
static _cell     *_crntCell     = NULL;    // current cell (passed around when loading --FIXME: global var (dumb))
static _cell     *_marinerCell  = NULL;    // place holder MIO's, and other (fake) S57 object
#define MARINER_CELL   "--6MARIN.000"     // a chart purpose 6 (bellow knowm IHO chart purpose)
#define WORLD_SHP_EXT  ".shp"             // shapefile ext
#define WORLD_BASENM   "--0WORLD"         // '--' - agency (none), 0 - chart purpose (S52 is 1-5 (harbour))
#define WORLD_SHP      WORLD_BASENM WORLD_SHP_EXT

static GString   *_plibNameList = NULL;    // string that gather plibName
static GString   *_paltNameList = NULL;    // string that gather palette name
static GString   *_S57ClassList = NULL;    // string that gather cell S57 class name
static GString   *_S52ObjNmList = NULL;    // string that gather cell S52 obj name
static GString   *_cellNameList = NULL;    // string that gather cell name

static int        _doInit       = TRUE;    // init the lib

// FIXME: reparse CS of the affected MP only (ex: ship outline MP need only to reparse OWNSHP CS)
static int        _doAPP_CS     = FALSE;   // TRUE will recreate *all* CS at next draw() or drawLast()
static int        _doAPP_DATCVR = FALSE;   // TRUE will compute HO Data Limit (CSP union), scale boundary, ..
static int        _doAPP_RASTER = FALSE;   // TRUE will compute raster texture

static int        _doCullLights = FALSE;   // TRUE will do lights_sector culling when _cellList change

static char      *_intl         = NULL;    // setlocal()

// statistic
static guint      _nCull        = 0;
static guint      _nTotal       = 0;

// CSYMB init scale bar, north arrow, unit, CHKSYM
static int             _iniCSYMB = TRUE;

static S52ObjectHandle _OWNSHP   = FALSE;
static S52ObjectHandle _SELECTED = FALSE;  // debug: used when an AIS target is selected
static S52ObjectHandle _SCALEB10 = FALSE;
static S52ObjectHandle _SCALEB11 = FALSE;
static S52ObjectHandle _NORTHAR1 = FALSE;
static S52ObjectHandle _UNITMTR1 = FALSE;
static S52ObjectHandle _CHKSYM01 = FALSE;
static S52ObjectHandle _BLKADJ01 = FALSE;

// obj of union of all HO Data Limit
static S52ObjectHandle _HODATAUnion = FALSE;
// list of scale boundary reference (system generated DATCVR01-3)
static GArray         *_sclbdyList  = NULL;
static const guint     INTU_SEP     = 0;  // intu separator in _sclbdyList

// experimental: list of sclbdU - union of obj in _sclbdyList for each INTU
static GArray         *_sclbdUList  = NULL;

static int             _hodataSupp  = TRUE;   // suppress display of HODATA
static int             _sclbdySupp  = TRUE;   // suppress display of SCLBDY

static GPtrArray      *_rasterList  = NULL;  // list of Raster

static GPtrArray      *_tmpRenderBin= NULL;  // list of obj that overide prio

// callback to eglMakeCurrent() / eglSwapBuffers()
#ifdef S52_USE_EGL
static S52_EGL_cb _eglBeg = NULL;
static S52_EGL_cb _eglEnd = NULL;
static void      *_EGLctx = NULL;

#define EGL_BEG(tag)    if (NULL != _eglBeg) {                    \
                           if (FALSE == _eglBeg(_EGLctx,#tag)) {  \
                                goto exit;                        \
                           }                                      \
                        }

#define EGL_END(tag)    if (NULL != _eglEnd) _eglEnd(_EGLctx,#tag);

#else  // S52_USE_EGL

#define EGL_BEG(tag)
#define EGL_END(tag)

#endif  // S52_USE_EGL


// Note: thread awarness to prenvent two threads from writing into the 'scene graph' at once
// (ex data comming from gpsd,) so this is mostly Mariners' Object.
// Note: the mutex never have to do work with the main_loop already serializing call.
// Note: that DBus and socket/WebSocket are running from the main loop but the handling is done from threads

#if (defined(S52_USE_ANDROID) || defined(_MINGW))
static GStaticMutex  _mp_mutex = G_STATIC_MUTEX_INIT;
#define GMUTEXLOCK   g_static_mutex_lock
#define GMUTEXUNLOCK g_static_mutex_unlock
#else
static GMutex        _mp_mutex;
#define GMUTEXLOCK   g_mutex_lock
#define GMUTEXUNLOCK g_mutex_unlock
#endif


// check basic init
#define S52_CHECK_INIT  if (TRUE == _doInit) {                                                  \
                           PRINTF("WARNING: libS52 not initialized --call S52_init() first\n"); \
                           goto exit;                                                           \
                        }
#define S52_CHECK_MUTX                   GMUTEXLOCK(&_mp_mutex);
#define S52_CHECK_MUTX_INIT              GMUTEXLOCK(&_mp_mutex); S52_CHECK_INIT
#define S52_CHECK_MUTX_INIT_EGLBEG(tag)  GMUTEXLOCK(&_mp_mutex); S52_CHECK_INIT EGL_BEG(tag)

/* FIXME: iterating on _cell then on renderBin is common to a lot calls
typedef struct func {
    //void (*GFunc) (gpointer data, gpointer user_data);
    void (*func1) (gpointer data, gpointer user_data);
    void (*func2) (gpointer data, gpointer user_data);
} func;
func f = {func1, func2};

g_ptr_array_foreach(_cellList, f.func1, f.func2);

-OR- where func1 == _delObj()
#define travRBin1(cell, func1) \
    for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) { \
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {    \
            //g_ptr_array_foreach(cell->renderBin[i][j], func1, NULL); \
            //-OR-                                                     \
            //(func1);                                                   \
        }  \
    }

#define travRBin1(cell, printf("test\n"))
#define travRBin2(cell, func1, func2) ...
*/

// traverse Render Bin and call func1() on each bin
#define TRAV_RBIN_ij(func1)                                    \
    for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) { \
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {    \
            func1;                                             \
        }                                                      \
    }

//TRAV_RBIN_ij(printf("test\n"))

#define ALL_C_TRAV_RBIN_ij(func1)                                  \
    for (guint k=0; k<_cellList->len; ++k) {                       \
        _cell *c = (_cell*) g_ptr_array_index(_cellList, k);       \
        for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) { \
            for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {    \
                (func1);                                           \
            }                                                      \
        }                                                          \
    }

//ALL_C_TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j], S52_PL_resloveSMB, NULL))

////////////////////////////////////////////////////
//
// USER INPUT VALIDATION
//
// FIXME: move some to utils
static double     _validate_bool(double val)
{
    val = (val==0.0)? 0.0 : 1.0;

    PRINTF("NOTE: toggle to: %s\n", (val==0.0)? "OFF" : "ON");

    return val;
}

static double     _validate_meter(double val)
{
    PRINTF("NOTE: Meter: %f\n", val);

    return val;
}

static double     _validate_nm(double val)
{
    if (val < 0.0) val = -val;

    PRINTF("NOTE: Nautical Mile: %f\n", val);

    return val;
}

static double     _validate_min(double val)
{

    if (val < 0.0) {
        PRINTF("WARNING: time negatif, reset to 0.0: %f\n", val);
        val = 0.0;
    }

    PRINTF("NOTE: Minute: %f\n", val);

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

    PRINTF("DEBUG: Display Priority before: crntMask:0x%x, newMask:0x%x)\n", crntMask, newMask);

    if (crntMask  & newMask)
        crntMask -= newMask;
    else
        crntMask += newMask;

    PRINTF("DEBUG: Display Priority after : crntMask:0x%x, newMask:0x%x)\n", crntMask, newMask);

    return (double)crntMask;
}

static double     _validate_mar(double val)
// S52_MAR_DISP_LAYER_LAST  - MARINERS' CATEGORY (drawn on top - last)
{
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

    if (newMask & crntMask) {
        //crntMask &= ~newMask;
        crntMask -= newMask;
    } else
        crntMask += newMask;

    return (double)crntMask;
}

static double     _validate_pal(double val)
{
    int palTblsz = S52_PL_getPalTableSz();

    if (val >= palTblsz) val = 0.0;
    if (val <  0.0     ) val = palTblsz-1;

    PRINTF("NOTE: Color Palette set to: %s (%f)\n", S52_PL_getPalTableNm((int)val), val);

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
    // FIXME: 85.051125°
    if (lat < -90.0 || 90.0 < lat || isnan(lat)) {
        // FIXME: check proj4
        PRINTF("WARNING: latitude out of bound [-90.0 .. +90.0], reset to 0.0: %f\n", lat);
        lat = 0.0;
    }

    //PRINTF("NOTE: set degree to: %f\n", lat);

    return lat;
}

static double     _validate_lon(double lon)
{
    // FIXME: def this! abs()!
    if (lon < -180.0 || 180.0 < lon || isnan(lon)) {
        PRINTF("WARNING: longitude out of bound [-180.0 .. +180.0], reset to 0.0: %f\n", lon);
        lon = 0.0;
    }

    //PRINTF("NOTE: set degree to: %f\n", lon);

    return lon;
}

static int        _validate_screenPos(double *xx, double *yy)
// return TRUE
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

#ifdef S52_DEBUG
    PRINTF("DEBUG: Command Word Filter State:\n");
    PRINTF("DEBUG: S52_CMD_WRD_FILTER_SY:%s\n",(S52_CMD_WRD_FILTER_SY & crntMask) ? "TRUE" : "FALSE");
    PRINTF("DEBUG: S52_CMD_WRD_FILTER_LS:%s\n",(S52_CMD_WRD_FILTER_LS & crntMask) ? "TRUE" : "FALSE");
    PRINTF("DEBUG: S52_CMD_WRD_FILTER_LC:%s\n",(S52_CMD_WRD_FILTER_LC & crntMask) ? "TRUE" : "FALSE");
    PRINTF("DEBUG: S52_CMD_WRD_FILTER_AC:%s\n",(S52_CMD_WRD_FILTER_AC & crntMask) ? "TRUE" : "FALSE");
    PRINTF("DEBUG: S52_CMD_WRD_FILTER_AP:%s\n",(S52_CMD_WRD_FILTER_AP & crntMask) ? "TRUE" : "FALSE");
#endif

    return (double)crntMask;
}

static double     _validate_positive(double val)
{
    // FIXME: print warning if val < 0

    return ABS(val);
}

static double     _validate_pos_hodata(double val)
{
    val = _validate_positive(val);
    _hodataSupp = TRUE;

    return val;
}

static double     _validate_pos_sclbdy(double val)
{
    val = _validate_positive(val);
    _sclbdySupp = TRUE;

    return val;
}

static int        _fixme(S52MarinerParameter paramName)
{
    PRINTF("FIXME: S52MarinerParameter %i not implemented\n", paramName);

    return TRUE;
}
////////////////////////////////////////////////////////////////////////

DLL double STD S52_getMarinerParam(S52MarinerParameter paramID)
// return Mariner parameter or the value in S52_MAR_ERROR if fail
// FIXME: check mariner param against groups selection
{
    S52_CHECK_MUTX;

    double val = S52_MP_get(paramID);

    PRINTF("NOTE: paramID:%i, val:%f\n", paramID, val);

    GMUTEXUNLOCK(&_mp_mutex);

    return val;
}

DLL int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val)
// validate and set Mariner Parameter
{
    S52_CHECK_MUTX;

    PRINTF("NOTE: paramID:%i, val:%f\n", paramID, val);

    // don't set same value
    if (val == S52_MP_get(paramID)) {
        GMUTEXUNLOCK(&_mp_mutex);
        return FALSE;
    }

    // FIXME: _doAPP_RAS find if S52_MAR_SAFETY_CONTOUR / S52_MAR_DEEP_CONTOUR / S52_MAR_DATUM_OFFSET has change

    switch (paramID) {
        case S52_MAR_ERROR               : break;
        case S52_MAR_SHOW_TEXT           : val = _validate_bool(val);                   break;
        // _SEABED01->DEPARE01;
        case S52_MAR_TWO_SHADES          : val = _validate_bool(val);  _doAPP_CS = TRUE;break;
        // DEPCNT02; _SEABED01->DEPARE01; _UDWHAZ03->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_CONTOUR      : val           = _validate_meter(val);
                                           _doAPP_CS     = TRUE;
                                           _doAPP_RASTER = TRUE;
                                           break;
        // _SNDFRM02->OBSTRN04, WRECKS02;
        case S52_MAR_SAFETY_DEPTH        : val = _validate_meter(val); _doAPP_CS = TRUE;break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_CONTOUR     : val = _validate_meter(val); _doAPP_CS = TRUE;break;
        // _SEABED01->DEPARE01;
        case S52_MAR_DEEP_CONTOUR        : val           = _validate_meter(val);
                                           _doAPP_CS     = TRUE;
                                           _doAPP_RASTER = TRUE;
                                           break;
        // _SEABED01->DEPARE01;
        case S52_MAR_SHALLOW_PATTERN     : val = _validate_bool(val);  _doAPP_CS = TRUE;break;
        case S52_MAR_SHIPS_OUTLINE       : val = _validate_bool(val);                   break;
        case S52_MAR_DISTANCE_TAGS       : val = _validate_nm(val);    _fixme(paramID); break;
        case S52_MAR_TIME_TAGS           : val = _validate_min(val);   _fixme(paramID); break;
        case S52_MAR_FULL_SECTORS        : val = _validate_bool(val);                   break;
        // RESARE02;
        case S52_MAR_SYMBOLIZED_BND      : val = _validate_bool(val);  _doAPP_CS = TRUE;break;
        case S52_MAR_SYMPLIFIED_PNT      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_CATEGORY       : val = _validate_disp(val);                   break;
        case S52_MAR_COLOR_PALETTE       : val = _validate_pal(val);                    break;

        case S52_MAR_VECPER              : val = _validate_min(val);                    break;
        case S52_MAR_VECMRK              : val = _validate_int(val);                    break;
        case S52_MAR_VECSTB              : val = _validate_int(val);                    break;

        case S52_MAR_HEADNG_LINE         : val = _validate_bool(val);                   break;
        case S52_MAR_BEAM_BRG_NM         : val = _validate_nm(val);                     break;

        case S52_MAR_DISP_GRATICULE      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_WHOLIN         : val = _validate_int(val);                    break;
        case S52_MAR_DISP_LEGEND         : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_CALIB          : val = _validate_bool(val);                   break;

        //
        //---- experimental variables / debug ----
        //
        case S52_MAR_FONT_SOUNDG         : val = _fixme(val);                           break;
        // DEPARE01; DEPCNT02; _DEPVAL01; SLCONS03; _UDWHAZ03;
        case S52_MAR_DATUM_OFFSET        : val           = _validate_meter(val);
                                           _doAPP_CS     = TRUE;
                                           _doAPP_RASTER = TRUE;
                                           break;
        case S52_MAR_SCAMIN              : val = _validate_bool(val);                   break;
        case S52_MAR_ANTIALIAS           : val = _validate_bool(val);                   break;
        case S52_MAR_QUAPNT01            : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_OVERLAP        : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_LAYER_LAST     : val = _validate_mar (val);                   break;

        case S52_MAR_ROT_BUOY_LIGHT      : val = _validate_deg(val);                    break;

        case S52_MAR_DISP_CRSR_PICK      : val = _validate_int(val);                    break;

        case S52_MAR_DOTPITCH_MM_X       : val = _validate_positive(val);               break;
        case S52_MAR_DOTPITCH_MM_Y       : val = _validate_positive(val);               break;

        case S52_MAR_DISP_DRGARE_PATTERN : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_NODATA_LAYER   : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_VESSEL_DELAY   : val = _validate_int(val);                    break;
        case S52_MAR_DISP_AFTERGLOW      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_CENTROIDS      : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_WORLD          : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_RND_LN_END     : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_VRMEBL_LABEL   : val = _validate_bool(val);                   break;
        case S52_MAR_DISP_RADAR_LAYER    : val = _validate_bool(val);                   break;

        case S52_CMD_WRD_FILTER          : val = _validate_filter(val);                 break;

        case S52_MAR_GUARDZONE_BEAM      : val = _validate_positive(val);               break;
        case S52_MAR_GUARDZONE_LENGTH    : val = _validate_positive(val);               break;
        case S52_MAR_GUARDZONE_ALARM     : val = _validate_positive(val);               break;

        case S52_MAR_DISP_HODATA_UNION   : val = _validate_pos_hodata(val);             break;
        case S52_MAR_DISP_SCLBDY_UNION   : val = _validate_pos_sclbdy(val);             break;

        default:
            PRINTF("WARNING: unknown Mariner's Parameter type (%i)\n", paramID);

            GMUTEXUNLOCK(&_mp_mutex);
            return FALSE;
    }

    int ret = S52_MP_set(paramID, val);

    ///////////////////////////////////////////////
    // FIXME: process _doAPP_CS == TRUE immediatly
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

DLL int    STD S52_setTextDisp(unsigned int prioIdx, unsigned int count, unsigned int state)
{
    S52_CHECK_MUTX;

    state = _validate_bool(state);

    int ret = S52_MP_setTextDisp(prioIdx, count, state);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_getTextDisp(unsigned int prioIdx)
{
    S52_CHECK_MUTX;

    int ret = S52_MP_getTextDisp(prioIdx);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

static gint       _cmpCellINTU(gconstpointer a, gconstpointer b)
// sort cell: bigger region (small scale) last (eg 553311)
{
    _cell *A = *(_cell**) a;
    _cell *B = *(_cell**) b;

    // Note: _marinerCell as no dsid_intustr
    if (A->filename->str[2] ==  B->filename->str[2])
    //if (A->dsid_intustr->str[0] ==  B->dsid_intustr->str[0])
        return 0;

    if (A->filename->str[2] >  B->filename->str[2])
    //if (A->dsid_intustr->str[0] >  B->dsid_intustr->str[0])
        return -1;
    else
        return  1;
}

static guint      _isCellLoaded(const char *baseName)
// return idx of cell if loaded, else FALSE (0 - mariner cell)
{
    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, i);
        if (0 == g_strcmp0(c->filename->str, baseName)) {
            //return TRUE;
            return i;
        }
    }

    return FALSE;
}

static void       _delObj(S52_obj *obj);  // forward decl
static _cell     *_newCell(const char *filename)
// add this cell else NULL (if allready loaded)
// assume filename is not NULL
{
    // strip path
    gchar *baseName = g_path_get_basename(filename);
    if (TRUE == !!_isCellLoaded(baseName)) {
        _crntCell = NULL;
    }
    else
    {   // init cell
        //_cell *cell = g_new0(_cell, 1);
        _cell *cell = _g_new0(_cell, 1);

        TRAV_RBIN_ij(cell->renderBin[i][j] = g_ptr_array_new_with_free_func((GDestroyNotify)_delObj))

        cell->filename = g_string_new(baseName);

        cell->geoExt.S =  INFINITY;
        cell->geoExt.W =  INFINITY;
        cell->geoExt.N = -INFINITY;
        cell->geoExt.E = -INFINITY;

        cell->lights_sector = g_ptr_array_new_with_free_func((GDestroyNotify)_delObj);

        cell->local = S52_CS_init();

        // journal - obj ref, no free_func()
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

        _crntCell = cell;
    }

    g_free(baseName);

    return _crntCell;
}

static void       _delObj(S52_obj *obj)
{
    //(void)user_data;

    S52_GL_delDL(obj);

    // Note: cleanup here, can't be done in PL - collision
    S57_geo *geo = S52_PL_delObj(obj, TRUE);

    g_free(obj);

    S57_doneData(geo, NULL);

    return;
}

static void       _freeCell(_cell *c)
{
    if (NULL != c->filename)
        g_string_free(c->filename, TRUE);
    g_free(c->encPath);

    TRAV_RBIN_ij(g_ptr_array_free(c->renderBin[i][j], TRUE));

    S52_CS_done(c->local);

    g_ptr_array_free(c->lights_sector, TRUE);

    // Note: all bellow are ref to obj - no free_func / _delObj() on array
    g_ptr_array_free(c->textList,      TRUE);
    g_ptr_array_free(c->objList_supp,  TRUE);
    g_ptr_array_free(c->objList_over,  TRUE);

    /*
    if (NULL != c->DEPARElist)   g_ptr_array_free(c->DEPARElist, TRUE);
    if (NULL != c->DEPCNTlist)   g_ptr_array_free(c->DEPCNTlist, TRUE);
    if (NULL != c->OBSTRNlist)   g_ptr_array_free(c->OBSTRNlist, TRUE);
    if (NULL != c->RESARElist)   g_ptr_array_free(c->RESARElist, TRUE);
    if (NULL != c->WRECKSlist)   g_ptr_array_free(c->WRECKSlist, TRUE);
    */

    g_string_free(c->S57ClassList, TRUE);

    g_free(c);

    //return TRUE;
    return;
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


void              _dump_crash_report(unsigne pid)
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
        PRINTF("WARNING: unw_init_remote() failed [pid %i]\n", pid);
        _UPT_destroy(ui);
        return;
    }

    PRINTF("DEBUG: backtrace of the remote process (pid %d) using libunwind-ptrace:\n", pid);

    do {
        unw_word_t ip, sp, offp;
        char buf[512];

        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        unw_get_proc_name(&cursor, buf, sizeof (buf), &offp);

        PRINTF("DEBUG:   ip: %10p, sp: %10p   %s\n", (void*) ip, (void*) sp, buf);

    } while ((ret = unw_step (&cursor)) > 0);

    _UPT_destroy (ui);
}

static int        _Unwind_Reason_Code _trace_func(struct _Unwind_Context *ctx, void *user_data)
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

    PRINTF("DEBUG: ==== backtrace() returned %d addresses ====\n", nptrs);

    strings = backtrace_symbols(buffer, nptrs);
    if (NULL == strings) {
        PRINTF("WARNING: backtrace_symbols() .. no symbols");
        return FALSE;
    }

    for (int i=0; i<nptrs; ++i) {
        PRINTF("DEBUG: ==== %s\n", strings[i]);  // clang - null dereference - return false above
    }

    free(strings);

    return TRUE;
}
#endif  // S52_USE_BACKTRACE

#ifdef _MINGW
static void       _trapSIG(int sig)
{
    //void  *buffer[100];
    //char **strings;

    // Ctrl-C
    if (SIGINT == sig) {
        PRINTF("NOTE: Signal SIGINT(%i) cought .. setting up atomic to abort draw()\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);
        return;
    }

    if (SIGSEGV == sig) {
        PRINTF("NOTE: Segmentation violation cought (%i) ..\n", sig);
    } else {
        PRINTF("NOTE: other signal(%i) trapped\n", sig);
    }

    // shouldn't reach this !?
    g_assert_not_reached();  // turn off via G_DISABLE_ASSERT

    exit(sig);
}

#else  // _MINGW

static void       _trapSIG(int sig, siginfo_t *info, void *secret)
{
    // 2 - Interrupt (ANSI), Ctrl-C
    if (SIGINT == sig) {
        PRINTF("NOTE: Signal SIGINT(%i) cought .. setting up atomic to abort\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGINT.sa_sigaction)
            _old_signal_handler_SIGINT.sa_sigaction(sig, info, secret);

        return;
    }

    //  3  - Quit (POSIX)
    if (SIGQUIT == sig) {
        PRINTF("NOTE: Signal SIGQUIT(%i) cought .. Quit\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGQUIT.sa_sigaction)
            _old_signal_handler_SIGQUIT.sa_sigaction(sig, info, secret);

        return;
    }

    //  5  - Trap (ANSI)
    if (SIGTRAP == sig) {
        PRINTF("NOTE: Signal SIGTRAP(%i) cought .. debugger\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTRAP.sa_sigaction)
            _old_signal_handler_SIGTRAP.sa_sigaction(sig, info, secret);

        return;
    }

    //  6  - Abort (ANSI)
    if (SIGABRT == sig) {
        PRINTF("NOTE: Signal SIGABRT(%i) cought .. Abort\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGABRT.sa_sigaction)
            _old_signal_handler_SIGABRT.sa_sigaction(sig, info, secret);

        return;
    }

    //  9  - Kill, unblock-able (POSIX)
    if (SIGKILL == sig) {
        PRINTF("NOTE: Signal SIGKILL(%i) cought .. Kill\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGKILL.sa_sigaction)
            _old_signal_handler_SIGKILL.sa_sigaction(sig, info, secret);

        return;
    }

    // 11 - Segmentation violation
    if (SIGSEGV == sig) {
        PRINTF("NOTE: Segmentation violation cought (%i) ..\n", sig);

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
        PRINTF("NOTE: Signal SIGTERM(%i) cought .. Termination\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTERM.sa_sigaction)
            _old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);

        return;
    }

    // 10
    if (SIGUSR1 == sig) {
        PRINTF("NOTE: Signal 'User-defined 1' cought - SIGUSR1(%i)\n", sig);
        return;
    }
    // 12
    if (SIGUSR2 == sig) {
        PRINTF("NOTE: Signal 'User-defined 2' cought - SIGUSR2(%i)\n", sig);
        return;
    }



//#ifdef S52_USE_ANDROID
        // break loop - android debuggerd rethrow SIGSEGV
        //exit(0);
//#endif


    // shouldn't reach this !?
    PRINTF("WARNING: Signal not hangled (%i)\n", sig);
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
    signal(SIGINT,  _trapSIG);  //  2 - Interrupt (ANSI).
    signal(SIGSEGV, _trapSIG);  // 11 - Segmentation violation (ANSI).
#else
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = _trapSIG;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;  // -std=c99 -D_POSIX_C_SOURCE=199309L

    //  2 - Interrupt (ANSI) - user press Ctrl-C to stop long
    //  running process in _draw(), _drawLast() and _suppLineOverlap()
    g_atomic_int_set(&_atomicAbort, FALSE);
    sigaction(SIGINT,  &sa, &_old_signal_handler_SIGINT);
    //  3 - Quit (POSIX)
    sigaction(SIGQUIT, &sa, &_old_signal_handler_SIGQUIT);
    //  5 - Trap (ANSI)
    sigaction(SIGTRAP, &sa, &_old_signal_handler_SIGTRAP);
    //  6 - Abort (ANSI)
    sigaction(SIGABRT, &sa, &_old_signal_handler_SIGABRT);
    //  9 - Kill, unblock-able (POSIX)
    sigaction(SIGKILL, &sa, &_old_signal_handler_SIGKILL);
    // 11 - Segmentation violation (ANSI).
    //sigaction(SIGSEGV, &sa, &_old_signal_handler_SIGSEGV);   // loop in android
    // 15 - Termination (ANSI)
    //sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);

    // 10 -
    sigaction(SIGUSR1, &sa, &_old_signal_handler_SIGUSR1);
    // 12 -
    sigaction(SIGUSR2, &sa, &_old_signal_handler_SIGUSR2);

    // debug - will trigger SIGSEGV for testing
    //_cell *c = 0x0;
    //c->geoExt.S = INFINITY;
#endif

    return TRUE;
}

static ObjExt_t   _getCellsExt(void);  // forward decl
static int        _initPROJview(void)
{
    // skip if Projection allready set
    if (NULL != S57_getPrjStr())
        return TRUE;

    ObjExt_t ext = _getCellsExt();
    double cLat  =  (ext.N + ext.S) / 2.0;
    double cLon  =  (ext.W + ext.E) / 2.0;
    double rNM   = ((ext.N - ext.S) / 2.0) * 60.0;
    double north = 0.0;
    S52_GL_setView(cLat, cLon, rNM, north);

    // FIXME: cLon break bathy projection
    // anti-meridian trick: use cLon, but this break bathy
    S57_setMercPrj(cLat, cLon);
    //S57_setMercPrj(0.0, cLon); // test - 0 cLat
    //S57_setMercPrj(0.0, 0.0);  // test - 0 cLat


    /* debug
    {
        double xyz[3] = {_view.cLon, _view.cLat, 0.0};
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            return FALSE;
        }
        PRINTF("PROJ CENTER: lat:%f, lon:%f, rNM:%f\n", xyz[0], xyz[1], _view.rNM);
    }
    */

    return TRUE;
}

//void (*GFunc) (gpointer data, gpointer user_data);
static void       _S57_geo2prj(S52_obj *obj, gpointer dummy) {(void)dummy; S57_geo2prj(S52PLGETGEO(obj));}
static int        _projectCells(void)
{
    for (guint k=0; k<_cellList->len; ++k) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, k);
        if (FALSE == c->projDone) {
            TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j], (GFunc)_S57_geo2prj, NULL));

            g_ptr_array_foreach(c->lights_sector, (GFunc)_S57_geo2prj, NULL);

            c->projDone = TRUE;
        }
    }

    return TRUE;
}

static void       __S52_CS_touch(S52_obj *obj, localObj *local) {S52_CS_touch(local, S52PLGETGEO(obj));}
static int        _collect_CS_touch(_cell* c)
// setup object used by CS
{
    /*
    TRAV_RBIN_ij(GPtrArray *rbin = c->renderBin[i][j];
                 for (guint idx=0; idx<rbin->len; ++idx) {
                     S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                     S52_CS_touch(c->local, S52PLGETGEO(obj));
                 });
    */

    TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j], (GFunc)__S52_CS_touch, c->local));

    /* then process lights_sector
    for (guint i=0; i<c->lights_sector->len; ++i) {
        S52_obj *obj  = (S52_obj *)g_ptr_array_index(c->lights_sector, i);
        S52_CS_touch(c->local, S52PLGETGEO(obj));
    }
    */
    g_ptr_array_foreach(c->lights_sector, (GFunc)__S52_CS_touch, c->local);

    // need to do a _resolveCS() at the next _app()
    _doAPP_CS = TRUE;

    return TRUE;
}

DLL int    STD S52_init(int screen_pixels_w, int screen_pixels_h, int screen_mm_w, int screen_mm_h, S52_log_cb log_cb)
// init basic stuff (outside of the main loop)
{
    //libS52Zdso();

    // check if init already done
    if (!_doInit) {
        PRINTF("WARNING: libS52 already initalized\n");
        return FALSE;
    }

#ifdef S52_DEBUG
    // FIXME: check timming, might be slower, write() immediatly
    //setbuf(stdout, NULL);
    // FIXME: GIO !
    //gsetbuf(stdout, NULL);
#endif

    S52_utils_initLog(log_cb);

    PRINTF("screen_pixels_w: %i, screen_pixels_h: %i, screen_mm_w: %i, screen_mm_h: %i\n",
            screen_pixels_w,     screen_pixels_h,     screen_mm_w,     screen_mm_h);

    // FIXME: validate
    if (screen_pixels_w<1 || screen_pixels_h<1 || screen_mm_w<1 || screen_mm_h<1) {
        PRINTF("WARNING: screen dim < 1\n");
        return FALSE;
    }

#if !defined(_MINGW)
    // check if run as root
    if (0 == getuid()) {
        PRINTF("WARNING: do NOT run as SUPERUSER (root) .. exiting\n");
        return FALSE;
    }
#endif

    ///////////////////////////////////////////////////////////
    //
    // init signal handler
    //
    _initSIG();

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

    S52_GL_setDotPitch(screen_pixels_w, screen_pixels_h, screen_mm_w, screen_mm_h);

    S52_GL_setViewPort(0, 0, screen_pixels_w, screen_pixels_h);


    ///////////////////////////////////////////////////////////
    // init env stuff for GDAL/OGR/S57
    //

    PRINTF("GDAL VERSION: %s\n", GDAL_RELEASE_NAME);

    // GDAL/OGR/S57 options (1: overwrite env)

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

    // FIXME: check setlocale (LC_ALL, ""); (see https://developer.gnome.org/glib/stable/glib-running.html#local)
    // FIXME: check if g_ascii_*() in utils alone will do on no-english machine
    // - Warning: what about sscanf()
    _intl = setlocale(LC_ALL, "C");


    ///////////////////////////////////////////////////////////
    // init S52 stuff
    //
    // load basic def (ex color, CS, ...)
    S52_PL_init();

    PRINTF("S52 CS VERSION: %s\n", S52_CS_version());

    // put an error No, default to 0 - no error
    //S52_MP_set(S52_MAR_ERROR, 0.0);

    // set default to show all text
    S52_MP_setTextDisp(0, 100, TRUE);

    // setup the virtual cell that will hold mariner's objects
    // Note: there is no IHO cell at scale '6', this garanty that
    // objects of this 'cell' will be drawn last (ie on top)
    // Note: most Mariners' Object land on the "fast" layer 9
    // But 'pastrk' (and other) are drawn on layer < 9.
    //if (NULL == _cellList)
    //    _cellList = g_ptr_array_new();
    if (NULL == _cellList)
        _cellList = g_ptr_array_new_with_free_func((GDestroyNotify)_freeCell);

    _marinerCell = _newCell(MARINER_CELL);
    g_ptr_array_add (_cellList, _marinerCell);
    g_ptr_array_sort(_cellList, _cmpCellINTU);

    // init extend
    _marinerCell->geoExt.S = -INFINITY;
    _marinerCell->geoExt.W = -INFINITY;
    _marinerCell->geoExt.N =  INFINITY;
    _marinerCell->geoExt.E =  INFINITY;

    // init raster
    if (NULL == _rasterList)
        _rasterList = g_ptr_array_new();

    // init tmp renderBin OPrio
    if (NULL == _tmpRenderBin)
        _tmpRenderBin = g_ptr_array_new();

    // scale boudary
    if (NULL == _sclbdyList)
        _sclbdyList = g_array_new(FALSE, FALSE, sizeof(unsigned int));

    // scale boudary Union List
    if (NULL == _sclbdUList)
        _sclbdUList = g_array_new(FALSE, FALSE, sizeof(unsigned int));


    ///////////////////////////////////////////////////////////
    // init sock stuff
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


    // flag APP() stage
    _doAPP_CS     = TRUE;
    _doAPP_RASTER = TRUE;

    _timer =  g_timer_new();

    _doInit = FALSE;


    PRINTF("S52_INIT() .. DONE\n");

    return TRUE;
}

DLL CCHAR *STD S52_version(void)
{
    PRINTF("%s\n", S52_utils_version());

    return S52_utils_version();
}

DLL int    STD S52_done(void)
// clear all - shutdown libS52
{
    S52_CHECK_MUTX_INIT;

    // this call free_func() if set
    g_ptr_array_free(_cellList, TRUE);
    _cellList    = NULL;
    _marinerCell = NULL;

    S52_GL_done();
    S52_PL_done();

    S57_donePROJ();

    _intl   = NULL;

    S52_utils_doneLog();

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

    // flush raster (bathy,..)
    // FIXME: foreach
    // this call free_func() if set
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        //S52_GL_delRaster(r, FALSE);
        S52_GL_delRaster(r);
    }
    // this call free_func() if set
    g_ptr_array_free(_rasterList, TRUE);
    _rasterList = NULL;

    // obj allready deleted
    g_ptr_array_free(_tmpRenderBin, TRUE);
    _tmpRenderBin = NULL;

    // scale boudary list - obj allready deleted
    g_array_free(_sclbdyList, TRUE);
    _sclbdyList = NULL;

    // scale boudary Union list - obj allready deleted
    g_array_free(_sclbdUList, TRUE);
    _sclbdUList = NULL;

#ifdef S52_USE_EGL
    _eglBeg = NULL;
    _eglEnd = NULL;
    _EGLctx = NULL;
#endif

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    PRINTF("libS52 .. done\n");

    return TRUE;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
static int        _suppLineOverlap()
// no SUPP in case manual chart correction (LC(CHCRIDnn) and LC(CHCRDELn))
// Note: for now, work for LC() only (LS() not processed)
// FIXME: does NAME_RCNM, NAME_RCID and MASK value refer to original winding?

// Note: RCNM ReCord NaMe
// 110 - VI, isolated node
// 120 - VC, connected node
// 130 - VE, edge
// In GDAL/OGR value are in the form = (<num value>:<val1>,<val2>, .. ,<valn>) in Attribute
// NAME_RCNM (IntegerList) = (1:130)
// NAME_RCID (IntegerList) = (1:72)
{
    return_if_null(_crntCell->S57Edges);
    //return_if_null(_crntCell->ConnectedNodes); // not used (yet!)

    // assume that there is nothing on layer S52_PRIO_NODATA
    for (S52_disPrio prio=S52_PRIO_MARINR; prio>S52_PRIO_NODATA; --prio) {
        // lines, areas
        for (S52ObjectType obj_t=S52_LINES; obj_t>S52__META; --obj_t) {
            GPtrArray *rbin = _crntCell->renderBin[prio][obj_t];
            for (guint idx=0; idx<rbin->len; ++idx) {

                // degug - Ctrl-C land here also now
                //for (;;) {
                    g_atomic_int_get(&_atomicAbort);
                    if (TRUE == _atomicAbort) {
                        PRINTF("NOTE: abort _suppLineOverlap() .. \n");
#ifdef S52_USE_BACKTRACE
                        _backtrace();
#endif
                        g_atomic_int_set(&_atomicAbort, FALSE);
                        goto exit;
                    }
                //    g_usleep(1000 * 1000); // 1.0 sec
                //}

                // one object
                S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                // get edge ID list
                S57_geo *geo = S52_PL_getGeo(obj);

                GString *name_rcnmstr = S57_getAttVal(geo, "NAME_RCNM");
                GString *name_rcidstr = S57_getAttVal(geo, "NAME_RCID");

                if ((NULL==name_rcnmstr) || (NULL==name_rcidstr)) {
                    PRINTF("WARNING: no RCNM/RCID for obj:%s\n", S57_getName(geo));
                    g_assert(0);
                    break;
                }

                // see ogr/ogrfeature.cpp:994
                #define OGR_TEMP_BUFFER_SIZE 80
                if ((OGR_TEMP_BUFFER_SIZE==name_rcnmstr->len) && ('.'==name_rcnmstr->str[name_rcnmstr->len-1])) {
                    PRINTF("FIXME: OGR buffer TEMP_BUFFER_SIZE in ogr/ogrfeature.cpp:994 overflow\n");
                    PRINTF("FIXME: apply patch in S52/doc/ogrfeature.cpp.diff to OGR source code\n");
                    g_assert(0);
                    return FALSE;
                }
                /* FIXME: overkill, find a better way . maybe when loading att
                {   // check for substring ",...)" if found at the end
                    // this mean that TEMP_BUFFER_SIZE in OGR is not large anought.
                    // see ogr/ogrfeature.cpp:994
                    // #define OGR_TEMP_BUFFER_SIZE 80
                    // apply patch in S52/doc/ogrfeature.cpp.diff to OGR source code
                    const gchar *substr = ",...)";
                    gchar       *found1 = g_strrstr(name_rcnmstr->str, substr);
                    gchar       *found2 = g_strrstr(name_rcidstr->str, substr);
                    if (NULL!=found1 || NULL!=found2) {
                        PRINTF("FIXME: OGR buffer TEMP_BUFFER_SIZE in ogr/ogrfeature.cpp:994 overflow\n");
                        PRINTF("FIXME: apply patch in S52/doc/ogrfeature.cpp.diff to OGR source code\n");
                        g_assert(0);
                        return FALSE;
                    }
                }
                //*/


                /////////////////////// DEBUG MASK ////////////////////////////////////////////////
                //
                // CA379035.000 Tadoussac has Mask on TSSLPT:5339,5340
                // but TSSLPT has no LC() or LS() to mask !
                // Note: RESARE02 has LC()!
                // TSSLPT:5339 : MASK (IntegerList) = (7:1,2,255,255,255,255,2)
                // 1 - mask, 2 - show, 255 - NULL, Masking is no relevant (exterior boundary truncated by the data limit)

                // Note: use clip plane - Z_CLIP_PLANE = S57_OVERLAP_GEO_Z + 1

                /*
                GString *maskstr = S57_getAttVal(geo, "MASK");
                if (NULL != maskstr) {
                    // check if buff is large enough
                    // the check will be done previously (see above)
                    //gchar *substr = ",...)";
                    //gchar *found1 = g_strrstr(maskstr->str, substr);
                    //if (NULL != found1) {
                    //    PRINTF("ERROR: OGR buffer TEMP_BUFFER_SIZE in ogr/ogrfeature.cpp:994 overflow\n");
                    //    g_assert(0);
                    //} else {
                        // buff size OK
                        gchar **splitMASK = g_strsplit_set(maskstr->str+1, "():,", 0);
                        gchar **topMASK   = splitMASK;

                        // the first integer is the lenght (ie the number of mask value)
                        guint n = atoi(*splitMASK++);
                        for (guint i=0; i<n; ++splitMASK, ++i) {
                            if (('1'==*splitMASK[0]) || ('5'==*splitMASK[1])) {
                                // debug
                                PRINTF("DEBUG: 'MASK' FOUND ---> %s:%i : %s\n", S57_getName(geo), S57_getS57ID(geo), maskstr->str);
                                // TSSLPT:5339 : (7:1,2,255,255,255,255,2)
                                // TSSLPT:5340 : (5:1,2,255,255,2)
                                //g_assert(0);
                            }
                        }
                        g_strfreev(topMASK);
                    //}
                }
                //*/
                ///////////////////////////////////////////////////////////////////////////


                // take only Edge (ie rcnm == 130 (Edge type))
                gchar **splitrcnm  = g_strsplit_set(name_rcnmstr->str+1, "():,", 0);
                gchar **splitrcid  = g_strsplit_set(name_rcidstr->str+1, "():,", 0);
                gchar **toprcnm    = splitrcnm;
                gchar **toprcid    = splitrcid;

                guint nRCNM = atoi(*splitrcnm);
                guint nRCID = atoi(*splitrcid);

                // debug
                if (nRCNM != nRCID) {
                    PRINTF("DEBUG: nRCNM != nRCID .. exit\n");
                    g_assert(0);
                    return FALSE;
                }

                /* check if splitrcnm / splitrcid are valid
                for (guint i=0; i<nRCNM; ++splitrcnm, ++i) {
                    if (NULL == *splitrcnm) {
                        PRINTF("ERROR: *splitrcnm\n");
                        g_assert(0);
                    }
                }
                for (guint i=0; i<nRCID; ++splitrcid, ++i) {
                    if (NULL == *splitrcid) {
                        PRINTF("ERROR: *splitrcid\n");
                        g_assert(0);
                    }
                }
                */


                //////////////////////////////////////////////////////////////////
                //
                // Algo:
                //for (i=0; NULL!=str; ++i)
                // for all rcnm == 130
                //   take rcid
                //   get Edge with rcid
                //   if geo is null
                //       make Edge to point to geo
                //   else
                //       make geo coord z==-1 for all vertex in Edge that are found in geo
                //
                // So vertex in Edge are matched to vertex in geo - what ever the winding is!
                //

                // NAME_RCNM (IntegerList) = (1:130)
                // NAME_RCID (IntegerList) = (1:72)

                // FIXME: does this reversing really help - find a solid test case
                // check original winding and reverse splitrcnm if CCW
                int increment = 0;

                // reset list
                /*
                S57_AW_t origAW = S57_getOrigAW(geo);
                if (S57_AW_CCW == origAW) {
                    splitrcnm = toprcnm + nRCNM;
                    splitrcid = toprcid + nRCID;
                    increment = -1;
                } else {
                    splitrcnm = toprcnm;
                    splitrcid = toprcid;
                    splitrcnm++;  // skip first integer (length)
                    splitrcid++;  // skip first integer (length)
                    increment = 1;
                }
                //*/

                //*
                splitrcnm = toprcnm;
                splitrcid = toprcid;
                splitrcnm++;  // skip first integer (length)
                splitrcid++;  // skip first integer (length)
                increment = 1;
                //*/

                for (guint i=0; i<nRCNM; ++i, splitrcnm+=increment, splitrcid+=increment) {

                    //if (0 == g_strcmp0(*splitrcnm, "130")) {
                    // the S57 name for Edge (130 --> S57_RCNM_VE = '3')
                    //if (S57_RCNM_VE == *(*splitrcnm+1)) {
                    if (S57_RCNM_VE == (*splitrcnm)[1]) {
                        // search Edges with the same RCID then one of geo RCID
                        for (guint j=0; j<_crntCell->S57Edges->len; ++j) {
                            S57_geo *geoEdge      = (S57_geo *)g_ptr_array_index(_crntCell->S57Edges, j);
                            gchar   *name_rcidstr = S57_getRCIDstr(geoEdge);

                            // failsafe
                            if (NULL == name_rcidstr) {
                                PRINTF("DEBUG: RCID NULL geo: %s ID:%i\n", S57_getName(geo), S57_getS57ID(geo));
                                g_assert(0);
                                continue;
                            }

                            // same RCID found
                            if (0 == g_strcmp0(name_rcidstr, *splitrcid)) {
                                if (NULL == S57_getEdgeOwner(geoEdge)) {
                                    S57_setEdgeOwner(geoEdge, geo);
                                } else {
                                    //PRINTF("DEBUG: edge overlap found on %s ID:%i\n", S57_getName(geo), S57_getS57ID(geo));
                                    S57_markOverlapGeo(geo, geoEdge);
                                }

                                // edge found, in S57 a geometry can't have the same edge twice,
                                // hence bailout because this edge will not apear again
                                break;
                            }
                        }
                    }
                }
                g_strfreev(toprcnm);
                g_strfreev(toprcid);
            }
        }
    }

exit:

    {   // free all overlaping line data
        // these are not S52_obj, so no delObj()
        int quiet = TRUE;

        g_ptr_array_foreach(_crntCell->S57Edges,       (GFunc)S57_doneData, &quiet);
        g_ptr_array_foreach(_crntCell->ConnectedNodes, (GFunc)S57_doneData, &quiet);

        // this call free_func() if set
        g_ptr_array_free(_crntCell->S57Edges,       TRUE);
        g_ptr_array_free(_crntCell->ConnectedNodes, TRUE);

        _crntCell->S57Edges       = NULL;
        _crntCell->ConnectedNodes = NULL;
        _crntCell->baseRCID       = 0;
    }

    return TRUE;
}
#endif  // S52_USE_SUPP_LINE_OVERLAP

//void (*GFunc) (gpointer data, gpointer user_data);
#ifdef S52_USE_C_AGGR_C_ASSO
static void       __linkRel2LNAM(S52_obj *obj, gpointer user_data)
// link geo to C_AGGR / C_ASSO geo
{
    (void)user_data;

    S57_geo *geoRel = S52PLGETGEO(obj);

    GString *lnam_refsstr = S57_getAttVal(geoRel, "LNAM_REFS");
    //GString *lnam_refsstr = S57_getAttVal(S52PLGETGEO(obj), "LNAM_REFS");
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
            //S57_setRelationship(geo, S52PLGETGEO(obj));

            if (NULL == refs_geo) {
                refs_geo = g_string_new("");
                g_string_printf(refs_geo, "%p", (void*)geo);
            } else {
                g_string_append_printf(refs_geo, ",%p", (void*)geo);
            }
            splitLNAM++;
        }
        // add geo to C_AGGR / C_ASSO LNAM_REFS_GEO
        if (NULL != refs_geo) {
            S57_setAtt(geoRel, "_LNAM_REFS_GEO", refs_geo->str);
            //S57_setAtt(S52PLGETGEO(obj), "_LNAM_REFS_GEO", refs_geo->str);
        }
        g_string_free(refs_geo, TRUE);

        g_strfreev(topLNAM);
    }

    //return TRUE;
    return;
}
#endif  // S52_USE_C_AGGR_C_ASSO

static void       __dumpNODATAlayer(S52_obj *obj, gpointer dummy)
{
    (void)dummy;

    S57_geo *geo = S52_PL_getGeo(obj);

    // these render nothing
    if (0 == g_strcmp0(S57_getName(geo), "DSID"  )) return;
    if (0 == g_strcmp0(S57_getName(geo), "C_AGGR")) return;
    if (0 == g_strcmp0(S57_getName(geo), "C_ASSO")) return;
    if (0 == g_strcmp0(S57_getName(geo), "M_NPUB")) return;

    PRINTF("WARNING: objName:'%s' S52ObjectType:'%i' is on NODATA layer\n", S57_getName(geo), S57_getObjtype(geo));

    // debug
    //S57_dumpData(geo, FALSE);

    return;
}

static _cell     *_loadBaseCell(char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb)
{
    if ((FALSE==g_str_has_suffix(filename, ".000")) &&
        (FALSE==g_str_has_suffix(filename, ".shp")))
    {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated] or .shp\n", filename);
        return NULL;
    }

    _cell *c = _newCell(filename);
    if (NULL == c) {
        PRINTF("WARNING: _newCell() failed\n");
        //g_assert(0);
        return NULL;
    }
    g_ptr_array_add(_cellList, c);
    g_ptr_array_sort(_cellList, _cmpCellINTU);

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
    TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j],  (GFunc)__linkRel2LNAM, NULL));

    // finish with this BBTree
    if (NULL != _lnamBBT) {
        g_tree_destroy(_lnamBBT);
        _lnamBBT = NULL;
    }
#endif

    _collect_CS_touch(c);


    {   // failsafe - check if a PLib put an object on the NODATA layer
        PRINTF("DEBUG: NODATA Layer check -START- ==============================================\n");
        // No need to scan all cells
        //for (guint i=0; i<_cellList->len; ++i) {
        //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        for (S52ObjectType obj_t=S52__META; obj_t<S52_N_OBJ; ++obj_t) {
            /*
                GPtrArray *rbin = c->renderBin[S52_PRIO_NODATA][obj_t];
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // these render nothing
                    if (0 == g_strcmp0(S57_getName(geo), "DSID"  )) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "C_AGGR")) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "C_ASSO")) continue;
                    if (0 == g_strcmp0(S57_getName(geo), "M_NPUB")) continue;

                    PRINTF("WARNING: objName:'%s' S52ObjectType:'%i' is on NODATA layer\n", S57_getName(geo), obj_t);

                    // debug
                    //S57_dumpData(geo, FALSE);
                }
                */

                g_ptr_array_foreach(c->renderBin[S52_PRIO_NODATA][obj_t], (GFunc)__dumpNODATAlayer, NULL);
            }

        //}
        PRINTF("DEBUG: NODATA Layer check -END-   ==============================================\n");
    }

    return c;
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

    if (NULL == (fd = g_fopen(filename, "r"))) {
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

    fclose(fd);

    return TRUE;
}
#endif  // 0
#endif  // S52_USE_OGR_FILECOLLECTOR

#if (defined(S52_USE_RADAR) || defined(S52_USE_RASTER))
// see http://www.gdal.org/warptut.html
#include "gdal_alg.h"
#include "ogr_srs_api.h"
#include "gdalwarper.h"

static const char*_getSRS(void)
{
    //const char *ret    = NULL;
    char *ret    = NULL;
    const char *prjStr = S57_getPrjStr();

    if (NULL == prjStr) {
        g_assert(0);
        return NULL;
    }

    // FIXME: cLon break bathy projection
    OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);
    if (OGRERR_NONE == OSRSetFromUserInput(hSRS, prjStr)) {
        OSRExportToWkt(hSRS, &ret);
    } else {
        PRINTF("WARNING: Translating source or target SRS failed:%s\n", prjStr );
        g_assert(0);
        return NULL;
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
    PRINTF("NOTE: Creating output file is that %dP x %dL.\n", nPixels, nLines);

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
    // FIXME: MAXPATH!
    char fnameMerc[1024];
    g_sprintf(fnameMerc, "%s%s", fname, ".merc");

    // check if allready loaded
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        if ((NULL!=r->fnameMerc) && (0==g_strcmp0(r->fnameMerc->str, fnameMerc))) {
            return FALSE;
        }
    }

    // no, convert to GeoTiff Mercator
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (NULL == driver) {
        PRINTF("WARNING: fail to get GDAL driver\n");
        return FALSE;
    }

    // FIXME: - read file "version.txt" in bathy
    //        - check S52_version()
    //        - check Mercator Latitude projection
    //        - delete *.merc if need to project again

    GDALDatasetH datasetDST = GDALOpen(fnameMerc, GA_ReadOnly);
    // no Merc on disk, then create it
    if (NULL == datasetDST) {
        GDALDatasetH datasetSRC = GDALOpen(fname, GA_ReadOnly);
        if (NULL == datasetSRC) {
            PRINTF("WARNING: fail to read raster\n");
            return FALSE;
        }

        //
        // FIXME: this will fail if convert to Merc at draw() time and no ENC loaded
        //        -OR-
        //        set proj here!
        //


        // FIXME: check if SRS of merc is same srs_DST if not convert again
        const char *srs_DST = _getSRS();
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
        CPLErr err = CE_None;
        err = GDALRasterIO(bandA, GF_Read, 0, 0, w, h, data, w, h, gdt, 0, 0);
        if (CE_None != err) {
            g_assert(0);
        }

        double gt[6] = {0.0,1.0,0.0,0.0,0.0,1.0};
        if (CE_None != GDALGetGeoTransform(datasetDST, (double *) &gt)) {
            PRINTF("WARNING: GDALGetGeoTransform() failed\n");
            g_assert(0);
            return FALSE;
        }

        // store data
        S52_GL_ras *ras = S52_GL_newRaster(fnameMerc);
        ras->isRADAR    = FALSE;
        ras->w          = w;
        ras->h          = h;
        ras->data       = data;
        ras->nodata     = (TRUE==nodata_set) ? nodata : -INFINITY;

        // not canonize because it will flip some bathy
        ras->pext.S = gt[3] + 0 * gt[4] + 0 * gt[5];
        ras->pext.W = gt[0] + 0 * gt[1] + 0 * gt[2];
        ras->pext.N = gt[3] + w * gt[4] + h * gt[5];
        ras->pext.E = gt[0] + w * gt[1] + h * gt[2];

        //*
        {   // convert view extent to deg
            projUV uv1 = {ras->pext.W,  ras->pext.S};
            projUV uv2 = {ras->pext.E,  ras->pext.N};
            uv1 = S57_prj2geo(uv1);
            uv2 = S57_prj2geo(uv2);
            double S = uv1.v;
            double W = uv1.u;
            double N = uv2.v;
            double E = uv2.u;

            // canonise geo extent
            if (S > N) {
                double tmp = S;
                S = N;
                N = tmp;
            }
            if (W > E) {
                double tmp = W;
                W = E;
                E = tmp;
            }
            ras->gext.S = S;
            ras->gext.W = W;
            ras->gext.N = N;
            ras->gext.E = E;
        }
        //*/

        g_ptr_array_add(_rasterList, ras);
    }

    // FIXME: write "version.txt" if abscent

    // finish with GDAL
    GDALClose(datasetDST);  // if NULL ?
    GDALDestroyDriverManager();

    return TRUE;
}
#endif  // S52_USE_RADAR S52_USE_RASTER

int            S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb);  // forward decl
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
    int    ret   = FALSE;
    char  *fname = NULL;
    //_cell *c     = NULL;

    S52_loadLayer_cb loadLayer_cb = S52_loadLayer;

    S52_CHECK_MUTX_INIT;

    if (NULL == loadObject_cb) {
        static int  silent = FALSE;
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("NOTE: (this msg will not repeat)\n");
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
    // FIXME: _dupPath(
    if (NULL == encPath) {
        valueBuf chartPath = {'\0'};
        // FIXME: refactor to return "const char *"
        if (FALSE == S52_utils_getConfig(CFG_CHART, chartPath)) {
            PRINTF("WARNING: CHART label not found in .cfg!\n");
            g_assert(0);

            goto exit;
        }
        fname = g_strdup(chartPath);
    } else {
        // normal
        fname = g_strdup(encPath);
    }
    fname = g_strstrip(fname);  // strip blank in place

    if (TRUE != g_file_test(fname, (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
        PRINTF("WARNING: file or DIR not found (%s)\n", fname);

        goto exit;
    }

#ifdef S52_USE_WORLD
    {   // experimental - load world shapefile
        gchar *basename = g_path_get_basename(fname);
        if (0 == g_strcmp0(basename, WORLD_SHP)) {
            PRINTF("NOTE: loading %s\n", fname);
            _loadBaseCell(fname, loadLayer_cb, loadObject_cb);
        }
        g_free(basename);
    }
#endif

#ifdef S52_USE_RASTER
    {   // experimental - load raster (GeoTIFF)
        if ((TRUE==g_str_has_suffix(fname, ".tif" )) ||
            (TRUE==g_str_has_suffix(fname, ".tiff"))) {
            _loadRaster(fname);

            ret = TRUE;
            goto exit;
        }
    }

#ifdef S52_USE_RADAR
    /*
    {   // debug - load raw raster RADAR (RAW)
        // FIXME: upgrade to glib-2.2: gboolean g_str_has_suffix(const gchar *str, const gchar *suffix);
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
                    ras->data  = data;
                    //ras->S     = gt[3] + 0 * gt[4] + 0 * gt[5];  // YgeoLL;
                    //ras->W     = gt[0] + 0 * gt[1] + 0 * gt[2];  // XgeoLL;
                    //ras->N     = gt[3] + w * gt[4] + h * gt[5];  // YgeoUR;
                    //ras->E     = gt[0] + w * gt[1] + h * gt[2];  // XgeoUR;
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
    */
#endif  // S52_USE_RADAR

#endif  // S52_USE_RASTER

#ifdef S52_USE_OGR_FILECOLLECTOR
    {

        //const char *base = g_basename(fname);
        //if (0 != g_strcmp0(base, "CATALOG.03")) {
        //    // cell extend - build cell index
        //    _loadCATALOG(fname);
        //}
        //g_chdir("ENC_ROOT");
        //char **encList = S57FileCollector("CATALOG.031");


        char **encList = S57FileCollector(fname);
        if (NULL != encList) {
            for (guint i=0; NULL!=encList[i]; ++i) {
                char *encName = encList[i];
                _loadBaseCell(encName, loadLayer_cb, loadObject_cb);
                g_free(encName);
            }
            g_free(encList);
        } else {
            PRINTF("WARNING: S57FileCollector(%s) return NULL\n", fname);
        }
    }

#else   // S52_USE_OGR_FILECOLLECTOR
    if (NULL == _loadBaseCell(fname, loadLayer_cb, loadObject_cb)) {
        goto exit;
    }
#endif  // S52_USE_OGR_FILECOLLECTOR

#ifdef S52_USE_PROJ
    if (TRUE == _initPROJview()) {
        ret = _projectCells();
    } else {
        goto exit;
    }
#endif  // S52_USE_PROJ


    // _app() specific to sector light
    _doCullLights = TRUE;
    // _app() - compute HO Data Limit
    _doAPP_DATCVR = TRUE;

exit:

    g_free(fname);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_doneCell(const char *encPath)
// FIXME: the (futur) chart manager (CM) should to this by itself
// so loadCell would load a CATALOG then CM would load individual cell
// to fill the view (and unload cell outside the view)
{
    return_if_null(encPath);

    int    ret      = FALSE;
    gchar *fname    = NULL;

    S52_CHECK_MUTX_INIT;

    PRINTF("%s\n", encPath);

    fname = g_strdup(encPath);
    fname = g_strstrip(fname);

    // check if file on disk
    // skip internal pseudo-cell MARINER_CELL (ie: "--6MARIN.000").
    if (TRUE != g_file_test(fname, (GFileTest) (G_FILE_TEST_EXISTS))) {
        PRINTF("WARNING: Cell not found on disk (%s)\n", fname);
        goto exit;
    }

#ifdef S52_USE_RASTER
    // unload .TIF
    if ((TRUE==g_str_has_suffix(fname, ".tif" )) ||
        (TRUE==g_str_has_suffix(fname, ".tiff"))) {
        char fnameMerc[1024];  // max name length
        g_sprintf(fnameMerc, "%s%s", fname, ".merc");
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            if ((NULL!=r->fnameMerc) && (0==g_strcmp0(r->fnameMerc->str, fnameMerc))) {
                //S52_GL_delRaster(r, FALSE);
                S52_GL_delRaster(r);

                ret = TRUE;
                goto exit;
            }
        }
    }

#ifdef S52_USE_RADAR
    /* debug - unload .raw (radar)
    if (0 != g_strcmp0(basename+8, ".raw")) {
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *r = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            if (0 == g_strcmp0(r->fnameMerc->str, fname)) {
                S52_GL_delRaster(r, FALSE);
                //g_free(r);
                ret = TRUE;
                goto exit;
            }
        }
    }
    */
#endif  // S52_USE_RADAR
#endif  // S52_USE_RASTER

    // skip file not terminated by .000 and .shp
    if ((FALSE==g_str_has_suffix(fname, ".000")) &&
        (FALSE==g_str_has_suffix(fname, ".shp")))
    {
        PRINTF("WARNING: filename (%s) not a S-57 base ENC [.000 terminated] or .shp\n", encPath);
        goto exit;
    }

    gchar *baseName = g_path_get_basename(fname);
    guint  i        = _isCellLoaded(baseName);
    if (0 < i) {
        // this call free_func() if set
        //_cell *c = (_cell*)
        g_ptr_array_remove_index(_cellList, i);
        //_freeCell(c);
        ret = TRUE;
    }
    g_free(baseName);

exit:
    // _app() - compute HO Data Limit
    _doAPP_DATCVR = TRUE;

    g_free(fname);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
static int        __builS57Edge(S57_geo *geo, double *ppt_0, double *ppt_1)
// build a S57 edge (segment) with ENs and CNs
{
    // old Edge - ENs
    guint   npt         = 0;
    double *ppt         = NULL;
    // Note: get pointer - not data since geo can be empty!
    S57_getGeoData(geo, 0, &npt, &ppt);

    /* debug
    if (0 == npt) {
        PRINTF("DEBUG: geo empty\n");
        //g_assert(0);
    }
    */

    // new S57 Edge = ENs + CNs
    guint   npt_new     = npt + 2;  // the new edge will have 2 more point - one at each end
    double *ppt_new     = g_new(double, npt_new*3);

    // set coords at both ends
    ppt_new[0] = ppt_0[0];                  // CN-0
    ppt_new[1] = ppt_0[1];
    ppt_new[(npt_new-1)*3 + 0] = ppt_1[0];  // CN-1
    ppt_new[(npt_new-1)*3 + 1] = ppt_1[1];

    // transfert ENs coords
    if (0 != npt) {
        // +3 step over first pos.
        memcpy(ppt_new+3, ppt, sizeof(double) * 3 * npt);
    }
    g_free(ppt);

    // update S57 Edge
    S57_setGeoLine(geo, npt_new, ppt_new);

    // add to this cell (crntCell)
    if (NULL == _crntCell->S57Edges)
        _crntCell->S57Edges = g_ptr_array_new();

    g_ptr_array_add(_crntCell->S57Edges, geo);

    return TRUE;
}

static int        _loadEdge(const char *name, void *Edge)
// 2nd - collecte OGR "Edge" shape
// ConnectedNode (CN), EdgeNode (EN): resulting S57 edge ==> CN - EN - .. -EN - CN
{
    if ((NULL==name) || (NULL==Edge)) {
        PRINTF("DEBUG: objname / shape  --> NULL\n");
        g_assert(0);
        return FALSE;
    }

    S57_geo *geo = S57_ogrLoadObject(name, (void*)Edge);
    if (NULL == geo) {
        PRINTF("WARNING: OGR fail to load object: %s\n", name);
        g_assert(0);
        return FALSE;
    }

    // get CN at edge end
    GString *name_rcid_0str = S57_getAttVal(geo, "NAME_RCID_0");
    guint    name_rcid_0    = (NULL == name_rcid_0str) ? 1 : atoi(name_rcid_0str->str);
    GString *name_rcid_1str = S57_getAttVal(geo, "NAME_RCID_1");
    guint    name_rcid_1    = (NULL == name_rcid_1str) ? 1 : atoi(name_rcid_1str->str);

    // debug
    if (NULL==name_rcid_0str || NULL==name_rcid_1str) {
        PRINTF("DEBUG: Edge with no end point\n");
        g_assert(0);
        return FALSE;
    }

    // node-0
    if ((name_rcid_0 - _crntCell->baseRCID) > _crntCell->ConnectedNodes->len) {
        PRINTF("DEBUG: Edge end point 0 (%s) and ConnectedNodes array lenght mismatch\n", name_rcid_0str->str);
        g_assert(0);
        return FALSE;
    }
    S57_geo *node_0 =  (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, (name_rcid_0 - _crntCell->baseRCID));
    if (NULL == node_0) {
        PRINTF("DEBUG: got empty node_0 at name_rcid_0 = %i\n", name_rcid_0);
        g_assert(0);
        return FALSE;
    }
    guint   npt_0 = 0;
    double *ppt_0 = NULL;
    S57_getGeoData(node_0, 0, &npt_0, &ppt_0);

    // node-1
    if ((name_rcid_1 - _crntCell->baseRCID) > _crntCell->ConnectedNodes->len) {
        PRINTF("DEBUG: Edge end point 1 (%s) and ConnectedNodes array lenght mismatch\n", name_rcid_1str->str);
        g_assert(0);
        return FALSE;
    }
    S57_geo *node_1 =  (S57_geo *)g_ptr_array_index(_crntCell->ConnectedNodes, (name_rcid_1 - _crntCell->baseRCID));
    if (NULL == node_1) {
        // if we land here it meen that there no ConnectedNodes at this index
        // ptr_array has hold (NULL) because of S57 update
        PRINTF("DEBUG: got empty node_1 at name_rcid_1 = %i\n", name_rcid_1);
        g_assert(0);
        return FALSE;
    }
    guint   npt_1   = 0;
    double *ppt_1   = NULL;
    S57_getGeoData(node_1, 0, &npt_1, &ppt_1);

    // debug - invariant
    {   // check that index are in sync with rcid
        // check assumion that ConnectedNodes are continuous
        // FIXME: when applying update this is no longer true (ie continuous)
        GString *rcid_0str = S57_getAttVal(node_0, "RCID");
        guint    rcid_0    = (NULL == rcid_0str) ? 1 : atoi(rcid_0str->str);
        GString *rcid_1str = S57_getAttVal(node_1, "RCID");
        guint    rcid_1    = (NULL == rcid_1str) ? 1 : atoi(rcid_1str->str);

        if (name_rcid_0 != rcid_0) {
            PRINTF("DEBUG: name_rcid_0 mismatch\n");
            g_assert(0);
            return FALSE;
        }
        if (name_rcid_1 != rcid_1) {
            PRINTF("DEBUG: name_rcid_1 mismatch\n");
            g_assert(0);
            return FALSE;
        }
    }

    // 3rd - build actual chaine node (complete geo edge ready for overlap test - _suppLineOverlap())
    __builS57Edge(geo, ppt_0, ppt_1);

    // debug
    //PRINTF("%X len:%i\n", _crntCell->Edges->pdata, _crntCell->Edges->len);
    //PRINTF("XXX %s\n", S57_getName(geo));

    return TRUE;
}

static int        _loadConnectedNode(const char *name, void *ConnectedNode)
// 1st - collect "ConnectedNode"
{
    if ((NULL==name) || (NULL==ConnectedNode)) {
        PRINTF("WARNING: objname / shape  --> NULL\n");
        g_assert(0);
        return FALSE;
    }

    S57_geo *geo = S57_ogrLoadObject(name, (void*)ConnectedNode);
    if (NULL == geo) {
        PRINTF("WARNING: OGR fail to load object: %s\n", name);
        g_assert(0);
        return FALSE;
    }

    {
        GString *rcidstr = S57_getAttVal(geo, "RCID");
        guint    rcid    = (NULL == rcidstr) ? 0 : atoi(rcidstr->str);

        // debug
        if (NULL == rcidstr) {
            PRINTF("DEBUG: no Att RCID .. \n");
            g_assert(0);
            return FALSE;
        }

        if (0 == _crntCell->baseRCID) {
            _crntCell->baseRCID = rcid;
        }

        // add to this cell (crntCell)
        if (NULL == _crntCell->ConnectedNodes) {
            _crntCell->ConnectedNodes = g_ptr_array_new();
        }

        // set_size is over grown - should be  'rcid - _crntCell->baseRCID + 1'
        if (rcid > _crntCell->ConnectedNodes->len) {
            g_ptr_array_set_size(_crntCell->ConnectedNodes, rcid);
            //g_assert(0);
        }

        _crntCell->ConnectedNodes->pdata[rcid - _crntCell->baseRCID] = geo;
        //_crntCell->ConnectedNodes->pdata[rcid] = geo;
    }

    return TRUE;
}
#endif  // S52_USE_SUPP_LINE_OVERLAP

int            S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb)
{
#ifdef S52_USE_GV
    // init w/ dummy cell name --we get here from OpenEV now (!?)
    if (NULL == _crntCell) {
        _cell *c = _newCell("dummy");
        g_ptr_array_add(_cellList, c);
    }
#endif

    if ((NULL==layername) || (NULL==layer)) {
        PRINTF("WARNING: layername / layer NULL\n");
        g_assert(0);
        return FALSE;
    }

    PRINTF("DEBUG: LOADING LAYER NAME: %s\n", layername);

#ifdef S52_USE_SUPP_LINE_OVERLAP
    // --- trap OGR/S57 low level primitive ---


    // unused
    if (0 == g_strcmp0(layername, "IsolatedNode"))
        return TRUE;

    // --------------------------------------------
    // FIXME: abort if too slow on large area
    // (define slow - or - C^ trap --> show something - heartbeat)
    // -OR- skip general INTUS = 1

    // ConnectedNode use to complete an Edge
    if (0 == g_strcmp0(layername, "ConnectedNode")) {
        S57_ogrLoadLayer(layername, layer, _loadConnectedNode);
        return TRUE;
    }
    // Edge is use to resolve overlapping line
    if (0 == g_strcmp0(layername, "Edge")) {
        S57_ogrLoadLayer(layername, layer, _loadEdge);
        return TRUE;
    }
    // --------------------------------------------

    // unused
    if (0 == g_strcmp0(layername, "Face"))
        return TRUE;


#endif  // S52_USE_SUPP_LINE_OVERLAP

    // debug: too slow for Lake Superior
    // FIXME
    //if (0 == g_strcmp0(layername, "OBSTRN"))
    //    return 1;
    //if (0 == g_strcmp0(layername, "UWTROC"))
    //    return 1;

    if (NULL == loadObject_cb) {
        static int silent = FALSE;
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("NOTE: (this msg will not repeat)\n");
            silent = TRUE;
        }
        loadObject_cb = S52_loadObject;
    }

    // save S57 class name
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
    if (0 == g_strcmp0(S52_PL_getOBCL(obj), "LIGHTS")) {
        S57_geo *geo       = S52_PL_getGeo(obj);
        GString *sectr1str = S57_getAttVal(geo, "SECTR1");
        GString *sectr2str = S57_getAttVal(geo, "SECTR2");

        if (NULL!=sectr1str || NULL!=sectr2str) {
            g_ptr_array_add(c->lights_sector, obj);

            // go it - bailout
            return TRUE;
        }
    }

    return FALSE;
}

static S52_obj   *_insertS57geo(_cell *c, S57_geo *geo)
// insert a S52_obj in a cell from a S57_geo
// return the new S52_obj
{
    S52_obj      *obj        = S52_PL_newObj(geo);
    S52_disPrio   disPrioIdx = S52_PL_getDPRI(obj);
    S52ObjectType obj_t      = S52_PL_getFTYP(obj);

    if (NULL == obj) {
        PRINTF("WARNING: S52 object build failed\n");
        g_assert(0);
        return NULL;
    }

#ifdef S52_DEBUG
    /* debug - show obj on NODATA layer
    if (S52_PRIO_NODATA == disPrioIdx) {
        //S57_highlightON(geo);
        S57_setHighlight(geo, TRUE);

        //disPrioIdx = S52_PRIO_SYM_AR;  // layer 6
        disPrioIdx = S52_PRIO_HAZRDS;  // layer 8

        ObjExt_t ext = S57_getExt(geo);
        PRINTF("DEBUG: %s:%i object on layer 0 moved to layer %i, highlightON() - %f %f -- %f %f\n",
               S57_getName(geo), S57_getS57ID(geo), disPrioIdx, ext.W, ext.S, ext.E, ext.N);
    }
    //*/
#endif

    // special prossesing for light sector
    if (FALSE == _insertLightSec(c, obj)) {
        // insert normal object (ie not a light with sector)
        g_ptr_array_add(c->renderBin[disPrioIdx][obj_t], obj);

        /* optimisation: recompute only CS that change due to new MarParam value
        // save reference for quickly find CS to re-compute after a MarinerParameter change
        // will replace the ugly APP() code that handle _doAPP_CS
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
    if (0 == g_strcmp0(S57_getName(geo), WORLD_BASENM)){
        S57_geo *geoNext = NULL;
        if (NULL != (geoNext = S57_getNextPoly(geo))) {
            // recurssion
            _insertS57geo(c, geoNext);
        }
    }
#endif

    return obj;
}

static S52_obj   *_insertS52obj(_cell *c, S52_obj *obj)
// inster 'obj' in cell 'c' - called by S52_loadPLib()
{
    S52_disPrio   disPrioIdx = S52_PL_getDPRI(obj);
    S52ObjectType obj_t      = S52_PL_getFTYP(obj);

    // special prossesing for light sector
    if (FALSE == _insertLightSec(c, obj)) {
        // insert normal object (ie not a light with sector)
        g_ptr_array_add(c->renderBin[disPrioIdx][obj_t], obj);

        /* optimisation: recompute only CS that change due to new MarParam value
        // save reference for quickly find CS to re-compute after a MarinerParameter change
        // will replace the ugly APP() code that handle _doAPP_CS
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

    return obj;
}

//DLL int    STD S52_loadObject(const char *objname, void *shape)
int            S52_loadObject(const char *objname, void *shape)
{
    S57_geo *geo = NULL;

    if ((NULL==objname) || (NULL==shape)) {
        PRINTF("WARNING: objname / shape NULL\n");
        return FALSE;
    }

#ifdef S52_USE_GV
    // debug: filter out metadata
    if (0 == g_strcmp0("DSID", objname))
        return FALSE;

    geo = S57_gvLoadObject (objname, (void*)shape);
#else
    geo = S57_ogrLoadObject(objname, (void*)shape);
#endif

    if (NULL == geo) {
        PRINTF("OBJNAME:%s skipped .. no geo\n", objname);
        return FALSE;
    }

    // set cell extent from each area object
    // Note: should be the same as CATALOG.03x
    if (S57__META_T != S57_getObjtype(geo)) {
        // Note: C1-ed3.1 has no M_COVR - will fail to display

        // FIXME: coverage in catalog
        // -OR-
        // use gdal to get M_COVR if existe

        //if ((S57_AREAS_T==S57_getObjtype(geo)) && (0==g_strcmp0(objname, "M_COVR"))) {
        if (S57_AREAS_T == S57_getObjtype(geo)) {
            ObjExt_t ext = S57_getExt(geo);

            // Note: debug obj on layer 0 moved up (layer 6-8) have no extent

            // Note: it is CATCOV (not CATCVR as in doc/pslb03_2.pdf)

            // FIXME: optimisation: skip getAttVal, save S57obj M_COVR:CATCOV quark

            // Note: commented for C1-ed3.1, same as above with M_COVR


            // M_COVR:CATCOV=1, HO data limit
            //GString *catcovstr = S57_getAttVal(geo, "CATCOV");
            //if ((NULL!=catcovstr) && ('1'==*catcovstr->str)) {
                //PRINTF("DEBUG: OBJNAME:%s CATCOV == 1\n", objname);

                // +inf
                if (0 != isinf(_crntCell->geoExt.S)) {
                    _crntCell->geoExt.S = ext.S;
                    _crntCell->geoExt.W = ext.W;
                    _crntCell->geoExt.N = ext.N;
                    _crntCell->geoExt.E = ext.E;
                } else {
                    // lat
                    if (_crntCell->geoExt.N < ext.N)
                        _crntCell->geoExt.N = ext.N;
                    if (_crntCell->geoExt.S > ext.S)
                        _crntCell->geoExt.S = ext.S;

                    // init W,E limits
                    // put W-E in first quadrant [0..360]
                    if ((_crntCell->geoExt.W + 180.0) > (ext.W + 180.0))
                        _crntCell->geoExt.W = ext.W;
                    if ((_crntCell->geoExt.E + 180.0) < (ext.E + 180.0))
                        _crntCell->geoExt.E = ext.E;
                }

                /* debug: check if this cell is crossing the prime-meridian
                 if ((_crntCell->geoExt.W < 0.0) && (0.0 < _crntCell->geoExt.E)) {
                 PRINTF("DEBUG: CELL crossing prime:%s :: MIN: %f %f  MAX: %f %f\n", objname, _crntCell->geoExt.W, _crntCell->geoExt.S, _crntCell->geoExt.E, _crntCell->geoExt.N);
                 g_assert(0);
                 }
                 // check if this cell is crossing the anti-meridian
                 if ((_crntCell->geoExt.W > -180.0) && (180.0 > _crntCell->geoExt.E)) {
                 PRINTF("DEBUG: CELL crossing anti:%s :: MIN: %f %f  MAX: %f %f\n", objname, _crntCell->geoExt.W, _crntCell->geoExt.S, _crntCell->geoExt.E, _crntCell->geoExt.N);
                 g_assert(0);
                 }
                 //*/
            //}  // CATCOV
        }  // S57_AREAS_T/M_COVR

        {
            // check M_QUAL:CATZOC
            if (0 == g_strcmp0(objname, "M_QUAL"))
                _crntCell->catzocstr = S57_getAttVal(geo, "CATZOC");  // data quality indicator

            // check M_ACCY:POSACC
            if (0 == g_strcmp0(objname, "M_ACCY"))
                _crntCell->posaccstr = S57_getAttVal(geo, "POSACC");  // data quality indicator

            // check MAGVAR
            if (0 == g_strcmp0(objname, "MAGVAR")) {
                // MAGVAR:VALMAG and
                _crntCell->valmagstr = S57_getAttVal(geo, "VALMAG");  //
                // MAGVAR:RYRMGV and
                _crntCell->ryrmgvstr = S57_getAttVal(geo, "RYRMGV");  //
                // MAGVAR:VALACM
                _crntCell->valacmstr = S57_getAttVal(geo, "VALACM");  //
            }

            // check M_CSCL compilation scale
            if (0 == g_strcmp0(objname, "M_CSCL")) {
                _crntCell->cscalestr = S57_getAttVal(geo, "CSCALE");
            }

            // check M_SDAT:VERDAT
            if (0 == g_strcmp0(objname, "M_SDAT")) {
                _crntCell->sverdatstr = S57_getAttVal(geo, "VERDAT");
            }
            // check M_VDAT:VERDAT
            if (0 == g_strcmp0(objname, "M_VDAT")) {
                _crntCell->vverdatstr = S57_getAttVal(geo, "VERDAT");
            }

#ifdef S52_DEBUG
            // debug - check M_NSYS:
            //if (0 == g_strcmp0(objname, "M_NSYS")) {
            //    S57_dumpData(geo, FALSE);
            //}

            /*
             {   // debug - check for LNAM_REFS in regular S57 object
             GString *key_lnam_refs = S57_getAttVal(geo, "LNAM_REFS");
             if (NULL != key_lnam_refs) {
             GString *key_ffpt_rind = S57_getAttVal(geo, "FFPT_RIND");
             GString *key_lnam      = S57_getAttVal(geo, "LNAM");
             PRINTF("DEBUG: LNAM: %s, LNAM_REFS: %s, FFPT_RIND: %s\n", key_lnam->str, key_lnam_refs->str, key_ffpt_rind->str);
             }
             }
             */
#endif
        }
    } else {
        // S57__META_T

        // debug
        //PRINTF("DEBUG: S57__META_T:OBJNAME:%s\n", objname);

        // check DSID (Data Set ID - metadata)
        if (0 == g_strcmp0(objname, "DSID")) {
            {
                GString *dsid_sdatstr = S57_getAttVal(geo, "DSID_SDAT");
                GString *dsid_vdatstr = S57_getAttVal(geo, "DSID_VDAT");
                double   dsid_sdat    = (NULL == dsid_sdatstr) ? 0.0 : S52_atof(dsid_sdatstr->str);
                double   dsid_vdat    = (NULL == dsid_vdatstr) ? 0.0 : S52_atof(dsid_vdatstr->str);
                _crntCell->dsid_heightOffset = dsid_vdat - dsid_sdat;
            }

            // legend DSPM
            _crntCell->dsid_dunistr = S57_getAttVal(geo, "DSPM_DUNI");  // units for depth
            _crntCell->dsid_hunistr = S57_getAttVal(geo, "DSPM_HUNI");  // units for height
            _crntCell->dsid_csclstr = S57_getAttVal(geo, "DSPM_CSCL");  // scale  of display
            _crntCell->dsid_sdatstr = S57_getAttVal(geo, "DSPM_SDAT");  // sounding datum
            _crntCell->dsid_vdatstr = S57_getAttVal(geo, "DSPM_VDAT");  // vertical datum
            _crntCell->dsid_hdatstr = S57_getAttVal(geo, "DSPM_HDAT");  // horizontal datum

            // legend DSID
            _crntCell->dsid_isdtstr = S57_getAttVal(geo, "DSID_ISDT");  // date of latest update
            _crntCell->dsid_updnstr = S57_getAttVal(geo, "DSID_UPDN");  // number of latest update
            _crntCell->dsid_edtnstr = S57_getAttVal(geo, "DSID_EDTN");  // edition number
            _crntCell->dsid_uadtstr = S57_getAttVal(geo, "DSID_UADT");  // edition date
            _crntCell->dsid_intustr = S57_getAttVal(geo, "DSID_INTU");  // intended usage (navigational purpose)
            if (_crntCell->filename->str[2] != *_crntCell->dsid_intustr->str) {
                PRINTF("DEBUG: DSID_INTU mismatch filename nav purp\n");
                g_assert(0);
            }

            // debug
            //S57_dumpData(geo, FALSE);
        }
    }  // S57__META_T

#ifdef S52_USE_WORLD
    if (0 == g_strcmp0(objname, WORLD_BASENM)) {
        _insertS57geo(_crntCell, geo);

        // unlink Poly chain - else will loop forever in S52_loadPLib()
        S57_delNextPoly(geo);

        return TRUE;
    }
#endif

    _insertS57geo(_crntCell, geo);

    S52_CS_add(_crntCell->local, geo);

#ifdef S52_USE_C_AGGR_C_ASSO
    //--------------------------------------------------
    // helper: save LNAM/geo to lnamBBT
    if (NULL == _lnamBBT) {
        //_lnamBBT = g_tree_new(__cmpLNAM);
        //gint (*GCompareFunc) (gconstpointer a, gconstpointer b);
        _lnamBBT = g_tree_new((GCompareFunc)g_strcmp0);
    }

    GString *key_lnam = S57_getAttVal(geo, "LNAM");
    if (NULL != key_lnam)
        g_tree_insert(_lnamBBT, key_lnam->str, geo);

    //--------------------------------------------------
#endif  // S52_USE_C_AGGR_C_ASSO

    return TRUE;
}


//---------------------------------------------------
//
// CULL
//
//---------------------------------------------------

static int        _intersectEXT(ObjExt_t A, ObjExt_t B)
// TRUE if intersec, FALSE if outside
// A - ENC ext, B - view ext
{
    // N-S
    if (B.N < A.S) return FALSE;
    if (B.S > A.N) return FALSE;

    // E-W
    if (B.W > B.E) {
        // anti-meridian
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

    /* _intersectEXT
    if (B.N < A.S) return FALSE;
    if (B.S > A.N) return FALSE;
    if (B.E < A.W) return FALSE;
    if (B.W > A.E) return FALSE;
    */
    /*  S52CS.c:_intersectGEO
    if (extB.N < extA.S) return FALSE;
    if (extB.E < extA.W) return FALSE;
    if (extB.S > extA.N) return FALSE;
    if (extB.W > extA.E) return FALSE;
    */
    return TRUE;
}

static int        _intersectM_COVR(_cell *cell, S57_geo *geoM_COVR)
{
    ObjExt_t ext = S57_getExt(geoM_COVR);

    // skip Mariners Cell
    for (guint i=1; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // check that the new cell nav purp (INTU) is strictly bigger
        if (*cell->dsid_intustr->str <= *c->dsid_intustr->str) {
            //PRINTF("DEBUG: check nav purp ci:%s cj:%s\n", c->filename->str, cj->filename->str);
            continue;
        }
        //PRINTF("DEBUG: check nav purp ci:%s cj:%s\n", c->dsid_intustr->str, cj->dsid_intustr->str);

        //  M_COVR intersect smaller scale extent
        if (TRUE == _intersectEXT(c->geoExt, ext)) {
            return TRUE;
        }
    }

    return FALSE;
}

static double    *_revArray(guint npt, double *ppt, double *outArr)
// reverse winding
{
    typedef struct pt3 { double x, y, z; } pt3;
    pt3 *ppt3 = (pt3 *)ppt;
    pt3 *rev  = (pt3 *)outArr;

    // FIXME: optimisation: used p++ = --q
    // OR swap first/last
    for (guint i=0; i<npt; ++i) {
        rev[(npt - 1) - i] = ppt3[i];
    }

    return outArr;
}

// forward decl
static S52ObjectHandle _newMarObj(const char *plibObjName, S52ObjectType objType, unsigned int xyznbr, double *xyz, const char *listAttVal);
static S52_obj        *_updateGeo(S52_obj *obj, double *xyz);
static int        _appSclbdy(GArray *sclbdyList, S57_geo *geoM_COVR, char intu)
// SCALE BOUNDARIES: system generated CS DATCVR01-3
// generate a sclbdy obj for a M_COVR:CATCOV=1 geo obj
{
    static char _intu = 0;

    if (_intu != intu) {
        g_array_append_val(sclbdyList, INTU_SEP);
        _intu = intu;
    }

    guint   npt = 0;
    double *ppt = NULL;
    S57_getGeoData(geoM_COVR, 0, &npt, &ppt);

    S52ObjectHandle sclbdyH = _newMarObj("sclbdy", S52_AREAS, npt, NULL, NULL);
    if (FALSE != sclbdyH) {
        S52_obj *obj = S52_PL_isObjValid(sclbdyH);
        _updateGeo(obj, ppt);

        // optimisation
        ObjExt_t ext = S57_getExt(geoM_COVR);
        S57_geo *geo = S52_PL_getGeo(obj);
        S57_setExt(geo, ext.W, ext.S, ext.E, ext.N);

        g_array_append_val(sclbdyList, sclbdyH);
        //PRINTF("DEBUG: add sclbdy from %s:%i\n", S57_getName(geoM_COVR), S57_getS57ID(geoM_COVR));

    } else {
        PRINTF("WARNING: 'sclbdy' fail - check PLib AUX\n");
        g_assert(0);
    }

    return TRUE;
}

static int        _appHODATA(GArray *sclbdyList)
// compute HO data limit CS DATCVR01-2
// Note: will populate sclbdyList for further processing in _appSCLBDU()
{
    // combine HODATA ==> union gluTessProperty(tobj, ..);
    // GLU_TESS_WINDING_NONZERO or GLU_TESS_WINDING_POSITIVE winding rules.

    // begin union
    S52_GLU_begUnion();

    // skip Mariners Cell
    //for (guint i=0; i<_cellList->len; ++i) {
    for (guint i=1; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // M_COVR:CATCOV=1, link to PLib_AUX "m_covr" as ";OP(3OD11060);LC(HODATA01)"
        // (ie 3 - S52_PRIO_AREA_2, Over Radar, Display Base)
        //LUPT   40LU00102NILm_covrA00003OPLAIN_BOUNDARIES
        //LUPT   45LU00357NILm_covrA00003OSYMBOLIZED_BOUNDARIES

        // M_COVR:CATCOV=2, link to PLib
        // LUPT   40LU00102NILM_COVRA00001SPLAIN_BOUNDARIES
        // LUPT   45LU00357NILM_COVRA00001SSYMBOLIZED_BOUNDARIES
        GPtrArray *rbin = c->renderBin[S52_PRIO_GROUP1][S52_AREAS];

        for (guint idx=0; idx<rbin->len; ++idx) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
            S57_geo *geo = S52_PL_getGeo(obj);

            if (0 == g_strcmp0(S57_getName(geo), "M_COVR")) {
                GString *catcovstr = S57_getAttVal(geo, "CATCOV");
                if ((NULL!=catcovstr) && ('1'==*catcovstr->str)) {
                    // add this M_COVR to HO data limit set
                    S52_GLU_addUnion(geo);

                    // SCALE BOUNDARIES: system generated CS DATCVR01-3
                    // add this M_COVR to scale Boundary if
                    // intersect smaller nav purp cells
                    if (TRUE == _intersectM_COVR(c, geo)) {
                        _appSclbdy(sclbdyList, geo, *c->dsid_intustr->str);
                    }
                }
            }
        }
    }

    // get union of HO data
    // FIXME: can Union output more than 1 poly
    // (ie: 2 non-overlap poly!)
    guint   npt = 0;
    double *ppt = NULL;
    S52_GLU_endUnion(&npt, &ppt);
    if (0 == npt)
        return FALSE;

    // reverse Union output - CCW -> CW (S57 winding)
    double rev[npt*3];
    ppt = _revArray(npt, ppt, rev);

    // PLib AUX link to "m_covr" ;OP(3OD11060);LC(HODATA01)
    _HODATAUnion = _newMarObj("m_covr", S52_AREAS, npt, NULL, "CATCOV:1");
    if (FALSE != _HODATAUnion) {
        S52_obj *obj = S52_PL_isObjValid(_HODATAUnion);
        _updateGeo(obj, ppt);

        // FIXME: optimisation: unproject
        //S57_geo *geo = S52_PL_getGeo(obj);
        //_setExt(geo, npt, (double*)rev);
        // then:
        //   - get extent
        //   - unproj
        //   - set ext

         //ObjExt_t ext   = S57_getExt(geo);
         //S57_geo *geoSB = S52_PL_getGeo(objSB);
         //S57_setExt(geoSB, ext.W, ext.S, ext.E, ext.N);
    } else {
        PRINTF("WARNING: 'm_cover' fail (check PLib AUX)\n");
        g_assert(0);
    }

    return TRUE;
}

static int        _appSclbdU(GArray *sclbdyList, GArray *sclbdUList)
// SCALE BOUNDARIES: system generated CS DATCVR01-3
{
    //
    // FIXME: experimental: get union of sclbdy for a nav purp (INTU)
    // CSG - Computational Solid Geometry
    //*
    for (guint i=0; i<sclbdyList->len; ++i) {
        // begin sclbdy union
        S52_GLU_begUnion();

        S52ObjectHandle sclbdyH = (S52ObjectHandle) g_array_index(sclbdyList, unsigned int, i);
        while ((INTU_SEP!=sclbdyH) && (i<sclbdyList->len)) {
        // FIXME: compare INTU !!!
        //while (i < sclbdyList->len) {
            S52_obj *obj = S52_PL_isObjValid(sclbdyH);
            S57_geo *geo = S52_PL_getGeo(obj);

            S52_GLU_addUnion(geo);
            // all obj after i are of sclbdyH, union them
            PRINTF("DEBUG: add sclbdy from %s:%i\n", S57_getName(geo), S57_getS57ID(geo));

            sclbdyH = (S52ObjectHandle) g_array_index(sclbdyList, unsigned int, ++i);
        }

        // get union of sclbdy
        // FIXME: what if more than 1 poly
        guint   npt = 0;
        double *ppt = NULL;
        S52_GLU_endUnion(&npt, &ppt);
        if (0 == npt) {
            PRINTF("DEBUG: 'sclbdU' no poly\n");
            continue;
        } else {
            PRINTF("DEBUG: 'sclbdU' union\n");
        }

        // reverse Union output - CCW -> CW (S57 winding)
        double rev[npt*3];
        ppt = _revArray(npt, ppt, rev);

        // PLib AUX link to "sclbdU"
        S52ObjectHandle sclbdyUnion = _newMarObj("sclbdU", S52_AREAS, npt, NULL, NULL);
        if (FALSE != sclbdyUnion) {
            S52_obj *obj = S52_PL_isObjValid(sclbdyUnion);
            _updateGeo(obj, ppt);

            // FIXME: optimisation: unproject
            S57_geo *geo = S52_PL_getGeo(obj);
            //_setExt(geo, npt, (double*)rev);
            // then:
            //   - get extent
            //   - unproj
            //   - set ext

            //ObjExt_t ext   = S57_getExt(geo);
            //S57_geo *geoSB = S52_PL_getGeo(objSB);
            //S57_setExt(geoSB, ext.W, ext.S, ext.E, ext.N);

            g_array_append_val(sclbdUList, sclbdyUnion);
            PRINTF("DEBUG: add sclbdU from %s:%i\n", S57_getName(geo), S57_getS57ID(geo));
        } else {
            PRINTF("WARNING: 'sclbdU' fail (check PLib AUX)\n");
            g_assert(0);
        }


    }
    //*/

    return TRUE;
}

static int        _appMoveObj(_cell *c, GPtrArray *tmpRenderBin)
{
    for (guint idx=0; idx<tmpRenderBin->len; ++idx) {
        S52_obj      *obj   = (S52_obj *)g_ptr_array_index(tmpRenderBin, idx);
        S52_disPrio   prio  = S52_PL_getDPRI(obj);
        S52ObjectType obj_t = S52_PL_getFTYP(obj);

        g_ptr_array_add(c->renderBin[prio][obj_t], obj);
    }
    g_ptr_array_set_size(tmpRenderBin, 0);

    return TRUE;
}

static void       __findOPrioObj(GPtrArray *rbin)
// find all obj that have prio override and move to _tmpRenderBin
{
    guint idx = 0;
    while (idx<rbin->len) {
        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
        if (TRUE == S52_PL_isPrioO(obj)) {
            // remove_index_fast() - sans free_func() code
            if (idx != rbin->len - 1)
                rbin->pdata[idx] = rbin->pdata[rbin->len - 1];

            rbin->len             -= 1;
            rbin->pdata[rbin->len] = NULL;

            g_ptr_array_add(_tmpRenderBin, obj);
        } else {
            ++idx;
        }
    }

    return;
}

static S52ObjectHandle _delMarObj(S52ObjectHandle objH);  // forward decl
static int        _app()
// FIXME: doCSMar Mariner Only - time the cost of APP
// -OR-
// try to move Mariner CS logique in GL
{
    // FIXME: resolve CS in S52_setMarinerParam(), then all logic can be move back to CS (where it belong)
    // instead of doing part of the logic at render time (ex: no need to do S52_PL_cmpCmdParam())
    // Test doesn't show that the logic for POIN_T in GL at render-time cost anything noticable.
    // So the idea to move back the CS logic into CS.c is an esthetic one!
    // 2 -
    if (TRUE == _doAPP_CS) {
        // 2.1 - reparse CS
        // FIXME: no need to check mariner cell if all mariner CS is in GL (S52_MP_get(S52_MAR_VECMRK))
        ALL_C_TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j], (GFunc)S52_PL_resloveSMB, NULL));

        // 2.2 - move obj
        for (guint k=0; k<_cellList->len; ++k) {
            _cell *c = (_cell*) g_ptr_array_index(_cellList, k);
            /*
            // one cell
            for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) {
                for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    guint idx = 0;
                    while (idx<rbin->len) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        if (TRUE == S52_PL_isPrioO(obj)) {
                            // remove_index_fast() - sans free_func() code
                            if (idx != rbin->len - 1)
                                rbin->pdata[idx] = rbin->pdata[rbin->len - 1];

                            rbin->len             -= 1;
                            rbin->pdata[rbin->len] = NULL;

                            g_ptr_array_add(_tmpRenderBin, obj);
                        } else {
                            ++idx;
                        }
                    }
                }
            }
            */
            TRAV_RBIN_ij(__findOPrioObj(c->renderBin[i][j]));

            _appMoveObj(c, _tmpRenderBin);
        }

        // done rebuilding CS
        _doAPP_CS = FALSE;
    }

    // 2.3 - texApha, when raster is bathy,
    // if S52_MAR_SAFETY_CONTOUR / S52_MAR_DEEP_CONTOUR / S52_MAR_DATUM_OFFSET has change
    if (TRUE == _doAPP_RASTER) {
        // foreach
        for (guint i=0; i<_rasterList->len; ++i) {
            S52_GL_ras *ras = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
            //S52_GL_delRaster(ras, TRUE);
            S52_GL_udtRaster(ras);
        }
        _doAPP_RASTER = FALSE;
    }

    ////////////////////////////////////////////////////
    //
    // CS DATCVR01-2/3: compute HO Data Limit, scale boundary, ..
    //
    if (TRUE == _doAPP_DATCVR) {
        // del previous _HODATAUnion obj
        if (FALSE != _HODATAUnion) {
            _HODATAUnion = _delMarObj(_HODATAUnion);
        }

        // flush old scale boudary obj
        for (guint i=0; i<_sclbdyList->len; ++i) {
            S52ObjectHandle objH = (S52ObjectHandle) g_array_index(_sclbdyList, unsigned int, i);
            if (0 != _delMarObj(objH))
                g_assert(0);
        }
        g_array_set_size(_sclbdyList, 0);

        // flush old scale boundary Union obj
        for (guint i=0; i<_sclbdUList->len; ++i) {
            S52ObjectHandle objH = (S52ObjectHandle) g_array_index(_sclbdUList, unsigned int, i);
            if (0 != _delMarObj(objH))
                g_assert(0);
        }
        g_array_set_size(_sclbdUList, 0);

        // compute HO data limit union - system generated CS DATCVR01-2
        if (TRUE == _appHODATA(_sclbdyList)) {
            // compute scale boundaries union - system generated CS DATCVR01-3
            _appSclbdU(_sclbdyList, _sclbdUList);
        }

        _doAPP_DATCVR = FALSE;
    }

    // debug
    //PRINTF("_app(): -1-\n");

    return TRUE;
}

static int        _resetJournal(void)
{
    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
        g_ptr_array_set_size(c->objList_supp, 0);
        g_ptr_array_set_size(c->objList_over, 0);
        g_ptr_array_set_size(c->textList,     0);
    }

    return TRUE;
}

static int        _cullLights(void)
// CULL (first draw() after APP, on all cells)
{
    // FIXME: this compare to cell 'above', but len-1 could rollover
    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        /* a cell can have no lights sector
        if (NULL == c->lights_sector) {
            //PRINTF("DEBUG: NO lights_sector : %s\n", c->filename->str);
            continue;
        }
        */

        // FIXME: use for_each()
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj  = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            S57_geo *geo  = S52_PL_getGeo(obj);
            ObjExt_t oext = S57_getExt(geo);

            S52_PL_resloveSMB(obj, NULL);

            // traverse the cell 'above' to check if extent overlap this light
            for (guint k=i-1; k>0 ; --k) {
                _cell *cellAbove = (_cell*) g_ptr_array_index(_cellList, k);
                // skip if same scale
                //if (cellAbove->filename->str[2] > c->filename->str[2]) {
                if (*cellAbove->dsid_intustr->str > *c->dsid_intustr->str) {
                    if (TRUE == _intersectEXT(cellAbove->geoExt, oext)) {
                        // check this: a chart above this light sector
                        // does not have the same lights (this would be a bug in S57)
                        S52_PL_setSupp(obj, TRUE);
                    }
                }
            }
        }
    }

    return TRUE;
}

static int        _cullObj(_cell *c, GPtrArray *rbin)
//static int        _cullObj(S52_obj *obj, _cell *c)
// cull object out side the view and object supressed
// object culled are not inserted in the list of object to draw (journal)
{
    // for each object
    for (guint idx=0; idx<rbin->len; ++idx) {
        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

        // debug: can this happen!
        if (NULL == obj) {
            PRINTF("DEBUG: skip NULL obj\n");
            g_assert(0);
            continue;
        }

        // FIXME: check _sclbdyList and _sclbdyLUidx and Mariner Param DISP_sclbdy_Union

        ++_nTotal;

        // debug - anti-meridian, US5HA06M/US5HA06M.000
        //if (103 == S57_getS57ID(geo)) {
        //    PRINTF("ISODGR01 found\n");
        //}

        // debug
        //if (0 == g_strcmp0("mnufea", S52_PL_getOBCL(obj))) {
        //    PRINTF("mnufea found\n");
        //}
        //if (0 == g_strcmp0("M_COVR", S52_PL_getOBCL(obj))) {
        //    PRINTF("M_COVR found\n");
        //}
        //if (0 == g_strcmp0("sclbdy", S52_PL_getOBCL(obj))) {
        //    PRINTF("sclbdy found\n");
        //}
        //if (0 == strcmp("M_NSYS", S52_PL_getOBCL(obj))) {
        //    PRINTF("M_NSYS found\n");
        //}
        //if (0 == g_strcmp0("OBSTRN", S52_PL_getOBCL(obj))) {
        //    PRINTF("DEBUG: OBSTRN found\n");
        //    //S57_dumpData(S52_PL_getGeo(obj), FALSE);
        //
        //}

        // is *this* object suppressed by user
        if (TRUE == S52_PL_getSupp(obj)) {
            ++_nCull;
            continue;
        }

        // SCAMIN & PLib (disp cat) & S57 class
        if (TRUE == S52_GL_isSupp(obj)) {
            ++_nCull;
            continue;
        }

        // outside view
        // Note: object can be inside 'ext' but outside the 'view' (cursor pick)
        if (TRUE == S52_GL_isOFFview(obj)) {
            ++_nCull;
            continue;
        }

        // store object according to radar flags
        // Note: default to 'over' if something else than 'supp'
        if (S52_RAD_SUPP == S52_PL_getRPRI(obj)) {
            g_ptr_array_add(c->objList_supp, obj);
        } else {
            g_ptr_array_add(c->objList_over, obj);
            S57_geo *geo = S52_PL_getGeo(obj);

            // switch OFF highlight if user acknowledge Alarm / Indication by
            // resetting S52_MAR_GUARDZONE_ALARM to 0 (OFF - no alarm)
            // Note: at this time only S52_PRIO_HAZRDS / S52_RAD_OVER
            //if (0.0==S52_MP_get(S52_MAR_GUARDZONE_ALARM) && TRUE==S57_isHighlighted(geo))
            if (0.0==S52_MP_get(S52_MAR_GUARDZONE_ALARM) && TRUE==S57_getHighlight(geo))
                //S57_highlightOFF(geo);
                S57_setHighlight(geo, FALSE);
        }

        // if this object has TX or TE, draw text last (on top)
        if (TRUE == S52_PL_hasText(obj)) {
            g_ptr_array_add(c->textList, obj);
            //PRINTF("DEBUG: add text %p\n", obj);
        }
    }

    return TRUE;
}

static int        _cullLayer(_cell *c)
// one cell, cull object outside the view and object supressed
// object culled are not inserted in the list of object to draw (journal)
{
    // Note: Chart No 1 put object on layer 9 (Mariners' Objects)
    // layer 0-8
    for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_MARINR; ++i) {
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {

            GPtrArray *c_rbin = c->renderBin[i][j];
            _cullObj(c, c_rbin);

            //_cullObj(c_rbin, c);
            //foreach(c->renderBin[i][j], _cullObj, c);


            GPtrArray *m_rbin = _marinerCell->renderBin[i][j];
            _cullObj(c, m_rbin);

            //_cullObj(m_rbin, c);
            //foreach(_marinerCell->renderBin[i][j], _cullObj, c);
        }
    }

    return TRUE;
}

static int        _cull(ObjExt_t ext)
// cull chart not in view extent
// - viewport
// - small cell region on top
{
    _resetJournal();

    // suppress display of M_COVR/m_covr
    if (TRUE == _hodataSupp) {
        // suppress display of HODATA limit M_COVR
        if (0 == (int) S52_MP_get(S52_MAR_DISP_HODATA_UNION)) {
            if (S52_SUPP_OFF == S52_PL_getObjClassState("M_COVR"))
                S52_PL_toggleObjClass("M_COVR");
        }

        // show all - M_COVR + m_covr
        if (1 == (int) S52_MP_get(S52_MAR_DISP_HODATA_UNION)) {
            if (S52_SUPP_ON  == S52_PL_getObjClassState("M_COVR"))
                S52_PL_toggleObjClass("M_COVR");
        }
        _hodataSupp = FALSE;
    }

    if (TRUE == _sclbdySupp) {
        // suppress display of SCLBDY
        if (0 == (int) S52_MP_get(S52_MAR_DISP_SCLBDY_UNION)) {
            if (S52_SUPP_OFF == S52_PL_getObjClassState("sclbdy"))
                S52_PL_toggleObjClass("sclbdy");  // switch to SUPP ON
            if (S52_SUPP_ON == S52_PL_getObjClassState("sclbdU"))
                S52_PL_toggleObjClass("sclbdU");  // switch to SUPP OFF
        }
        // show all SCLBDY
        if (1 == (int) S52_MP_get(S52_MAR_DISP_SCLBDY_UNION)) {
            if (S52_SUPP_ON == S52_PL_getObjClassState("sclbdy"))
                S52_PL_toggleObjClass("sclbdy");
        }
        _sclbdySupp = FALSE;
    }

    // extend view to fit cell rotation
    // optimisation: use _north angle (in S52GL.c!) if big delta affect GPU
    double LLv, LLu, URv, URu;
    S52_GL_getGEOView(&LLv, &LLu, &URv, &URu);
    //PRINTF("DEBUG: LLv, LLu, URv, URu: %f %f  %f %f\n", LLv, LLu, URv, URu);

    double dLat = ABS((LLv - URv) / 2.0);
    double dLon = ABS((LLu - URu) / 2.0);
    S52_GL_setGEOView(LLv-dLat, LLu-dLon, URv+dLat, URu+dLat);
    //PRINTF("DEBUG: dLat,dLon: %f %f\n", dLat, dLon);
    //PRINTF("DEBUG: LLv, LLu, URv, URu: %f %f  %f %f\n", LLv-dLat, LLu-dLon, URv+dLat, URu+dLat);

    // all cells - larger region first (small scale)
    //for (guint i=_cellList->len; i>0; --i) {
    //for (guint i=_cellList->len; i>1; --i) {
    for (guint i=_cellList->len-1; i>0; --i) {
        //_cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
#ifdef S52_USE_WORLD
        if ((0==g_strcmp0(WORLD_SHP, c->filename->str)) && (FALSE==S52_MP_get(S52_MAR_DISP_WORLD)))
            continue;
#endif
        // is this chart visible
        if (TRUE == _intersectEXT(c->geoExt, ext)) {
            _cullLayer(c);
        }
    }

    // reset original view extent
    S52_GL_setGEOView(LLv, LLu, URv, URu);

    // debug
    //PRINTF("DEBUG: nbr of object culled: %i (%i)\n", _nCull, _nTotal);

    return TRUE;
}

#if defined(S52_USE_GL2)    || defined(S52_USE_GLES2)
#if defined(S52_USE_RASTER) || defined(S52_USE_RADAR)
//static int        _drawRaster(ObjExt_t *cellExt)
static int        _drawRaster(void)
{
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *raster = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        // FIXME: no _intersectEXT() .. but GL scissor is ON .. so this migth not really help
        S52_GL_drawRaster(raster);

        /*
        // bathy
        if (FALSE == raster->isRADAR) {
            if (TRUE == _intersectEXT(*cellExt, raster->gext)) {
                S52_GL_drawRaster(raster);
            }
            continue;
        }

        // radar
        if (TRUE == raster->isRADAR) {
            S52_GL_drawRaster(raster);
        }
        */
    }

    return TRUE;
}
#endif  // S52_USE_RASTER S52_USE_RADAR
#endif  // S52_USE_GL2 S52_USE_GLES2

static int        _drawLayer(ObjExt_t ext, int layer)
{
    // all cells --larger region first
    //for (guint i=_cellList->len; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

    //guint n = _cellList->len-1;
    //for (guint i=n; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        if (TRUE == _intersectEXT(c->geoExt, ext)) {

            // one layer
            //for (S52ObjectType j=S52_AREAS; j<S52_N_OBJ; ++j) {
            for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
                GPtrArray *rbin = c->renderBin[layer][j];

                // one object
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                    // debug
                    //S57_geo *geo = S52_PL_getGeo(obj);
                    //PRINTF("%s\n", S57_getName(geo));

                    // if display of object is not suppressed
                    if (TRUE == S52_PL_getSupp(obj)) {
                        //PRINTF("%s\n", S57_getName(geo));

                        S52_GL_draw(obj, NULL);

                        // doing this after the draw because draw() will parse the text
                        if (TRUE == S52_PL_hasText(obj))
                            g_ptr_array_add(c->textList, obj); // not tested

                    }
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

    //for (guint i=_cellList->len; i>0 ; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
    for (guint i=_cellList->len-1; i>0 ; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        /* a cell can have no lights sector
        if (NULL == c->lights_sector)
            continue;
        */

        // FIXME: use for_each()
        for (guint j=0; j<c->lights_sector->len; ++j) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(c->lights_sector, j);
            // SCAMIN & PLib (disp prio)
            if (TRUE != S52_GL_isSupp(obj)) {
                if (TRUE != S52_PL_getSupp(obj))
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
        double xyz[3]   = {c->geoExt.W, c->geoExt.N, 0.0};

        char   str[80]  = {'\0'};
        //double offset_x = 1.0;
        //double offset_y = 1.0;
        double offset_x = 8.0;
        double offset_y = 8.0;

        if (FALSE == S57_geo2prj3dv(1, xyz))
            return FALSE;

        S52_GL_getStrOffset(&offset_x, &offset_y, str);

        // ENC Name
        if (NULL == c->filename) {
            SNPRINTF(str, 80, "ENC NAME: %s", "Unknown");
        } else {
            SNPRINTF(str, 80, "%.8s", c->filename->str);
        }
        //S52_GL_drawStrWorld(xyz[0], xyz[1], str, 1, 3);
        S52_GL_drawStrWorld(xyz[0], xyz[1], str, 13);

        // DSID:DSPM_DUNI: units for depth
        if (NULL == c->dsid_dunistr) {
            SNPRINTF(str, 80, "dsid_dspm_duni: %s", "NULL");
        } else {
            if ('1' == *c->dsid_dunistr->str) {
                SNPRINTF(str, 80, "DEPTH IN %s", "METER");
            } else {
                SNPRINTF(str, 80, "DEPTH IN :%s", (NULL==c->dsid_dunistr) ? "NULL" : c->dsid_dunistr->str);
            }
        }
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // DSID:DSPM_HUNI: units for height
        if (NULL==c->dsid_hunistr) {
            SNPRINTF(str, 80, "dsid_dpsm_huni: %s", "NULL");
        } else {
            if ('1' == *c->dsid_hunistr->str) {
                SNPRINTF(str, 80, "HEIGHT IN %s", "METER");
            } else {
                SNPRINTF(str, 80, "HEIGHT IN :%s", c->dsid_hunistr->str);
            }
        }
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 0);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 10);

        //7. value of safety depth       Selected by user. Default is 30 metres.
        SNPRINTF(str, 80, "Safety Depth: %.1f", S52_MP_get(S52_MAR_SAFETY_DEPTH));
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 0);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 10);

        //8. value of safety contour     Selected by user. Default is 30 metres.
        SNPRINTF(str, 80, "Safety Contour: %.1f", S52_MP_get(S52_MAR_SAFETY_CONTOUR));
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 0);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 10);

        // scale of display
        xyz[1] -= offset_y; // add some room
        if (NULL==c->dsid_csclstr) {
            SNPRINTF(str, 80, "dsid_cscl:%s", (NULL==c->dsid_csclstr) ? "NULL" : c->dsid_csclstr->str);
        } else {
            SNPRINTF(str, 80, "Scale 1:%s", (NULL==c->dsid_csclstr) ? "NULL" : c->dsid_csclstr->str);
        }
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 2);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 12);


        // ----------- DATUM ----------------------------------------------------------

        // DSID:DSPM_SDAT: sounding datum
        SNPRINTF(str, 80, "dsid_sdat:%s", (NULL==c->dsid_sdatstr) ? "NULL" : c->dsid_sdatstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // vertical datum
        SNPRINTF(str, 80, "dsid_vdat:%s", (NULL==c->dsid_vdatstr) ? "NULL" : c->dsid_vdatstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // horizontal datum
        SNPRINTF(str, 80, "dsid_hdat:%s", (NULL==c->dsid_hdatstr) ? "NULL" : c->dsid_hdatstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // legend from M_SDAT
        // sounding datum
        if (NULL != c->sverdatstr) {
            SNPRINTF(str, 80, "sverdat:%s", c->sverdatstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }
        // legend from M_VDAT
        // vertical datum
        if (NULL != c->vverdatstr) {
            SNPRINTF(str, 80, "vverdat:%s", c->vverdatstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }


        // ---------------------------------------------------------------------

        // date of latest update
        SNPRINTF(str, 80, "dsid_isdt:%s", (NULL==c->dsid_isdtstr) ? "NULL" : c->dsid_isdtstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        // number of latest update
        SNPRINTF(str, 80, "dsid_updn:%s", (NULL==c->dsid_updnstr) ? "NULL" : c->dsid_updnstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        // edition number
        SNPRINTF(str, 80, "dsid_edtn:%s", (NULL==c->dsid_edtnstr) ? "NULL" : c->dsid_edtnstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        // edition date
        SNPRINTF(str, 80, "dsid_uadt:%s", (NULL==c->dsid_uadtstr) ? "NULL" : c->dsid_uadtstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        // intended usage (navigational purpose)
        SNPRINTF(str, 80, "dsid_intu:%s", (NULL==c->dsid_intustr) ? "NULL" : c->dsid_intustr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // legend from M_CSCL
        // scale
        if (NULL != c->cscalestr) {
            SNPRINTF(str, 80, "cscale:%s", c->cscalestr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }


        // legend from M_QUAL CATZOC
        // data quality indicator
        SNPRINTF(str, 80, "catzoc:%s", (NULL==c->catzocstr) ? "NULL" : c->catzocstr->str);
        //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
        S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);

        // legend from M_ACCY POSACC
        // data quality indicator
        if (NULL != c->posaccstr) {
            SNPRINTF(str, 80, "posacc:%s", c->posaccstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }

        // legend from MAGVAR
        // magnetic
        if (NULL != c->valmagstr) {
            SNPRINTF(str, 80, "valmag:%s", c->valmagstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }

        if (NULL != c->ryrmgvstr) {
            SNPRINTF(str, 80, "ryrmgv:%s", c->ryrmgvstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }

        if (NULL != c->valacmstr) {
            SNPRINTF(str, 80, "valacm:%s", c->valacmstr->str);
            //S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 1, 1);
            S52_GL_drawStrWorld(xyz[0], xyz[1] -= offset_y, str, 11);
        }
    }

    return TRUE;
}

static int        _draw()
// draw object inside view
// then draw object's text
{
    // optimisation: GOURD 1 - face of earth - sort and then glDraw() on a whole surface
    //               - app/cull must reset sort if color change by user

    // skip mariner - mariners obj are embeded in cell's journal
    //for (guint i=_cellList->len; i>1; --i) {
    //    _cell *c = (_cell*) g_ptr_array_index(_cellList, i-1);
    for (guint i=_cellList->len-1; i>0; --i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        g_atomic_int_get(&_atomicAbort);
        if (TRUE == _atomicAbort) {
            PRINTF("NOTE: abort drawing .. \n");
#ifdef S52_USE_BACKTRACE
            _backtrace();
#endif
            g_atomic_int_set(&_atomicAbort, FALSE);
            return TRUE;
        }

        // ----------------------------------------------------------------------------
        // FIXME: extract to _LL2XY(guint npt, double *ppt);
        double xyz[6] = {c->geoExt.W, c->geoExt.S, 0.0, c->geoExt.E, c->geoExt.N, 0.0};
        //PRINTF("DEBUG: %f %f %f %f\n", xyz[0], xyz[1], xyz[3], xyz[4]);
        if (FALSE == S57_geo2prj3dv(2, xyz)) {
            PRINTF("WARNING: S57_geo2prj3dv() failed\n");
            g_assert(0);
        }
        //PRINTF("DEBUG: %f %f %f %f\n", xyz[0], xyz[1], xyz[3], xyz[4]);

        S52_GL_prj2win(&xyz[0], &xyz[1]);
        S52_GL_prj2win(&xyz[3], &xyz[4]);
        // ----------------------------------------------------------------------------

        {   //* needed in combining HO DATA limit (check for corner overlap)
            // also mariners obj that overlapp cells
            // FIXME: this also clip calibration symbol if overlap cell & NODATA
            // need to augment the box size for chart rotation, but MIO will overlap!
            //PRINTF("DEBUG: %f %f %f %f\n", xyz[0], xyz[1], xyz[3], xyz[4]);
            int x = floor(xyz[0]);
            int y = floor(xyz[1]);
            int w = floor(xyz[3] - xyz[0]);
            int h = floor(xyz[4] - xyz[1]);
            S52_GL_setScissor(x, y, w, h);
            //*/
        }

        // draw under radar
        g_ptr_array_foreach(c->objList_supp, (GFunc)S52_GL_draw, NULL);

        // USE_RASTER/RADAR
#if defined(S52_USE_GL2)    || defined(S52_USE_GLES2)
#if defined(S52_USE_RASTER) || defined(S52_USE_RADAR)
        // draw radar / raster
        // Note: no raster in MARINER_CELL (i==1, cell idx 0)
        if ((1.0==S52_MP_get(S52_MAR_DISP_RADAR_LAYER)) && (1!=i)) {
            // FIXME: foreach
            //_drawRaster(&(c->geoExt));
            _drawRaster();
        }
#endif
#endif
        // draw over radar
        g_ptr_array_foreach(c->objList_over, (GFunc)S52_GL_draw, NULL);

        // end scissor test
        S52_GL_setScissor(0, 0, -1, -1);

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

    int ret = FALSE;

    // do not wait if an other thread is allready drawing
#if (defined(S52_USE_ANDROID) || defined(_MINGW))
    if (FALSE == g_static_mutex_trylock(&_mp_mutex)) {
#else
    if (FALSE == g_mutex_trylock(&_mp_mutex)) {
#endif
        PRINTF("WARNING: trylock failed\n");
        //goto exit;
        return FALSE;
    }

    S52_CHECK_INIT;

    EGL_BEG(DRAW);

    if (NULL == S57_getPrjStr())
        goto exit;

    g_timer_reset(_timer);


    // debug
    //PRINTF("DRAW: start ..\n");

    if (TRUE == S52_GL_begin(S52_GL_DRAW)) {

        //PRINTF("S52_draw() .. -1.2-\n");

        //////////////////////////////////////////////
        // APP:  .. update object
        _app();

        //////////////////////////////////////////////
        // CULL: .. supress display of object (eg outside view)

        projUV uv1, uv2;
        S52_GL_getPRJView(&uv1.v, &uv1.u, &uv2.v, &uv2.u);

        /*
        // test - optimisation using viewPort to draw area
        //    S52_CMD_WRD_FILTER_AC = 1 << 3,   // 001000 - AC
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

        ObjExt_t ext = {
            .S = uv1.v,
            .W = uv1.u,
            .N = uv2.v,
            .E = uv2.u
        };

        // debug - anti-meridian
        //if (ext.W > ext.E) {
        //    ext.W = ext.W - 360.0;
        //}

        _cull(ext);

        //PRINTF("S52_draw() .. -1.3-\n");

        //////////////////////////////////////////////
        // DRAW: .. render

        if (TRUE == (int) S52_MP_get(S52_MAR_DISP_OVERLAP)) {
            // debug
            for (S52_disPrio layer=S52_PRIO_NODATA; layer<S52_PRIO_NUM; ++layer) {
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
        if (TRUE == (int) S52_MP_get(S52_MAR_DISP_GRATICULE))
            S52_GL_drawGraticule();

        // draw legend
        if (TRUE == (int) S52_MP_get(S52_MAR_DISP_LEGEND))
            _drawLegend();

        ret = S52_GL_end(S52_GL_DRAW);

        // for each cell, not after all cell,
        // because city name appear twice
        // FIXME: cull object of overlapping region of cell of DIFFERENT nav pourpose
        // Note: no culling of object of overlapping region of cell of SAME nav pourpose
        // display priority 8
        // FIX: it's seem like a HO could handle this, but when zoomig-out it's a S52 overlapping symb. probleme

        //PRINTF("S52_draw() .. -2-\n");

        //ret = TRUE;

    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");

        // FIXME: tell EGL to reset context
        //EGL_END(RESET);
        g_assert(0);
    }

exit:

#if !defined(S52_USE_RADAR)
    EGL_END(DRAW);
#endif

#ifdef S52_DEBUG
    {
        gdouble sec = g_timer_elapsed(_timer, NULL);
        PRINTF("    DRAW: %.0f msec --------------------------------------\n", sec * 1000);
    }
#endif

    GMUTEXUNLOCK(&_mp_mutex);

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
        long int old = S52_PL_getTimeSec(obj);

        if (now.tv_sec - old > (int) S52_MP_get(S52_MAR_DISP_VESSEL_DELAY)) {
            GPtrArray *rbin = (GPtrArray *) user_data;
            // remove obj from 'cell'

            // this call free_func() if set
            g_ptr_array_remove(rbin, obj);

            //_delObj(obj);
            //_delObj(obj, NULL);
        }
    }

    return;
}

//static int        _drawLast(void)
static int        _drawLast(GPtrArray *rbin)
// draw the Mariners' Object (layer 9)
{
    //for (S52ObjectType i=S52_AREAS; i<S52_N_OBJ; ++i) {
    // Note: mariner's obj allow for META
    //for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
        //GPtrArray *rbin = _marinerCell->renderBin[S52_PRIO_MARINR][j];
        // FIFO
        //for (guint idx=0; idx<rbin->len; ++idx) {
        // LIFO: so that 'cursor' is drawn last (on top)
        for (guint idx=rbin->len; idx>0; --idx) {
            S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx-1);

            g_atomic_int_get(&_atomicAbort);
            if (TRUE == _atomicAbort) {
                PRINTF("NOTE: abort drawing .. \n");
#ifdef S52_USE_BACKTRACE
                _backtrace();
#endif
                g_atomic_int_set(&_atomicAbort, FALSE);
                return TRUE;
            }

            // debug: can this happen!
            if (NULL == obj) {
                PRINTF("DEBUG: skip NULL obj\n");
                g_assert(0);
                continue;
            }

            /////////////////////////////////////
            // CULL & DRAW
            //
            ++_nTotal;

            // FIXME: use by PRINTF() - move inside
            //S57_geo *geo = S52_PL_getGeo(obj);

            // is this object suppressed by user
            if (TRUE == S52_PL_getSupp(obj)) {
                ++_nCull;

                //PRINTF("DEBUG:%i: supp ON - %s\n", _nTotal, S57_getName(geo));

                continue;
            }

            // outside view
            // Note: object can be inside 'ext' but outside the 'view' (cursor pick)
            if (TRUE == S52_GL_isOFFview(obj)) {
                ++_nCull;

                //PRINTF("DEBUG:%i: OFF view - %s\n", _nTotal, S57_getName(geo));

                continue;
            }

            // SCAMIN & PLib (disp cat)
            if (FALSE == S52_GL_isSupp(obj)) {
                S52_GL_draw(obj, NULL);
                S52_GL_drawText(obj, NULL);

                //PRINTF("DEBUG:%i: _drawLast() - %s\n", _nTotal, S52_PL_getOBCL(obj));
            } else {
                ++_nCull;

                //PRINTF("DEBUG:%i: SCAMIN - %s\n", _nTotal, S57_getName(geo));

                continue;
            }
            /////////////////////////////////////
        }
    //}

    return TRUE;
}

DLL int    STD S52_drawLast(void)
{
    int ret = FALSE;

#if (defined(S52_USE_ANDROID) || defined(_MINGW))
    // do not wait if an other thread is allready drawing
    if (FALSE == g_static_mutex_trylock(&_mp_mutex)) {
#else
    if (FALSE == g_mutex_trylock(&_mp_mutex)) {
#endif
        PRINTF("WARNING: trylock failed\n");
        return FALSE;
    }

    S52_CHECK_INIT;

#if !defined(S52_USE_RADAR)
    EGL_BEG(LAST);
#endif

    if (NULL == S57_getPrjStr())
        goto exit;

    if (S52_MAR_DISP_LAYER_LAST_NONE == (int) S52_MP_get(S52_MAR_DISP_LAYER_LAST))
        goto exit;

    g_timer_reset(_timer);

    if (TRUE == S52_GL_begin(S52_GL_LAST)) {

        ////////////////////////////////////////////////////////////////////
        // APP: no CS code for Mariner - all in GL now
        //_app();

        // check stray vessel (occur when s52ais/gpsd restart)
        if (0.0 != S52_MP_get(S52_MAR_DISP_VESSEL_DELAY)) {
            GPtrArray *rbinPT = _marinerCell->renderBin[S52_PRIO_MARINR][S52_POINT];
            g_ptr_array_foreach(rbinPT, _delOldVessel, rbinPT);
            GPtrArray *rbinLN = _marinerCell->renderBin[S52_PRIO_MARINR][S52_LINES];
            g_ptr_array_foreach(rbinLN, _delOldVessel, rbinLN);
        }

        ////////////////////////////////////////////////////////////////////
        // CULL / DRAW:
        //
        _nCull = 0;
        _nTotal= 0;

        // Mariners' (layer 9 - Last)
        //ret = _drawLast();
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
            _drawLast(_marinerCell->renderBin[S52_PRIO_MARINR][j]);
        }

        S52_GL_end(S52_GL_LAST);
    } else {
        PRINTF("WARNING: S52_GL_begin() failed\n");
    }

exit:

    EGL_END(LAST);

#ifdef S52_DEBUG
    {
        //gdouble sec = g_timer_elapsed(_timer, NULL);
        //PRINTF("DRAWLAST: %.0f msec (cull/total) %i/%i\n", sec * 1000, _nCull, _nTotal);
    }
#endif

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

//void (*GFunc) (gpointer data, gpointer user_data);
static void       _linkPLib(S52_obj *obj, _cell *tmpCell)
{
    // 1 - relink to new rules
    S57_geo *geo    = S52_PL_getGeo(obj);
    S52_obj *tmpObj = S52_PL_newObj(geo);

    // debug - should be the same since we relink existing object
    // this happen when bug overflow in looping on newObj/delObj
    g_assert(obj == tmpObj);

    // 2 - flush old Display List / VBO
    S52_GL_delDL(obj);

    // 3 - put in new render bin
    _insertS52obj(tmpCell, obj);

    return;
}

DLL int    STD S52_loadPLib(const char *plibName)
// (re)load a PLib - rebuild S52_obj wrapper around S57_geo
// Note: allow to reload a PLib to overwrite rules
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    // 1 - load / parse new PLib
    if (NULL != plibName) {
        // load user PLib
        if (TRUE == S52_PL_load(plibName)) {
            g_string_append_printf(_plibNameList, ",%s", plibName);
        } else {
            PRINTF("WARNING: PLIB not found (%s)\n", plibName);
            goto exit;
        }
    } else {
        // check in s52.cfg (debug)
        valueBuf cfgPLib = {'\0'};
        if (TRUE == S52_utils_getConfig(CFG_PLIB, cfgPLib)) {
            if (TRUE == S52_PL_load(cfgPLib)) {
                g_string_append_printf(_plibNameList, ",%s", cfgPLib);
            } else {
                PRINTF("WARNING: PLIB not found (%s)\n", cfgPLib);
                g_assert(0);
                goto exit;
            }
        } else {
            PRINTF("DEBUG: PLIB not found in .cfg (%s)\n", CFG_PLIB);
            goto exit;
        }
    }

    // 2 - relink S57 objects to the new rendering rules of new PLib
    for (guint k=0; k<_cellList->len; ++k) {
        _cell *cell = (_cell*) g_ptr_array_index(_cellList, k);

        // FIXME: skip shapefile
        //if (0 == g_strcmp0(, WORLD_BASENM)){
        //

        _cell tmpCell;
        memset(&tmpCell, 0, sizeof(_cell));

        TRAV_RBIN_ij(tmpCell.renderBin[i][j] = g_ptr_array_new_with_free_func((GDestroyNotify)_delObj));

        /* insert obj in new cell rbin
        for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) {
            for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {

                GPtrArray *rbin = cell->renderBin[i][j];

                // debug - flag empty rbin (ex: GB4X0000.000:S52_PRIO_NODATA/S52_LINES)
                if (0 == rbin->len) {
                    PRINTF("DEBUG: empty rbin found (%s:%i %i)\n", cell->filename->str, i, j);
                } else {
                    // FIXME: DEBUG: dump META rbin to see what in there
                }

                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);

                    // 1 - relink to new rules
                    S57_geo *geo    = S52_PL_getGeo(obj);
                    S52_obj *tmpObj = S52_PL_newObj(geo);

                    // debug - should be the same since we relink existing object
                    // this happen when bug overflow in looping on newObj/delObj
                    g_assert(obj == tmpObj);

                    // 2 - flush old Display List / VBO
                    S52_GL_delDL(obj);

                    // 3 - put in new render bin
                    _insertS52obj(&tmpCell, obj);
                }

                // if GDestroyNotify _delObj() NOT given
                // obj transfert to new rbin (no _delObj)
                //g_ptr_array_free(rbin, TRUE);

                // free old rbin segment by hand if GDestroyNotify given
                gpointer *seg = g_ptr_array_free(rbin, FALSE);
                g_free(seg);
            }
        }
        */

        // link Obj to PLib LUP, save to tmpCell (tmp rbin holder)
        TRAV_RBIN_ij(g_ptr_array_foreach(cell->renderBin[i][j], (GFunc)_linkPLib, &tmpCell));
        // free old rbin segment by hand as GDestroyNotify given
        TRAV_RBIN_ij(g_free(g_ptr_array_free(cell->renderBin[i][j], FALSE)));
        // replace new rbin in cell
        TRAV_RBIN_ij(cell->renderBin[i][j] = tmpCell.renderBin[i][j]);

        /* optimisation: recompute only CS that change due to new MarParam value
        // save reference for quickly find CS to re-compute after a MarinerParameter change
        // will replace the ugly APP() code that handle _doAPP_CS
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

    // signal to rebuild all cmd
    _doAPP_CS = TRUE;

    ret = TRUE;

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}


#ifdef S52_USE_GV
static int        _gv_drawObj(const char *name)
// DEPRECATED
{
    // all cells --larger region first
    for (guint k=0; k<_cellList->len; ++k) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, k);

        // one cell
        for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) {

            // one layer
            for (S52ObjectType j=S52_AREAS; j<S52_N_OBJ; ++j) {
                GPtrArray *rbin = c->renderBin[i][j];

                // one object
                for (guint idx=0; idx<rbin->len; ++idx) {
                    S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                    S57_geo *geo = S52_PL_getGeo(obj);

                    // debug
                    //PRINTF("%s\n", S57_getName(geo));

                    if (0 == g_strcmp0(name, S57_getName(geo)))
                        S52_GL_draw(obj);
                }
            }
        }
    }

    return TRUE;
}

// FIXME: gather all GV stuff
DLL int    STD S52_gv_drawLayer(const char *name)
// DEPRECATED
{
    PRINTF("name: %s\n", name);

    return_if_null(S57_getPrjStr());
    S52_CHECK_MUTX_INIT;

    // debug filter out some layer comming from OpenEV
    /*
    if (0 != g_strcmp0(name, "DEPARE"))
        return FALSE;
    if (0 == g_strcmp0(name, "DSID"))
        return FALSE;
    if (0 == g_strcmp0(name, "M_QUAL"))
        return FALSE;
    if (0 == g_strcmp0(name, "M_COVR"))
        return FALSE;
    if (0 == g_strcmp0(name, "M_NPUB"))
        return FALSE;
    */

    if (TRUE == S52_GL_begin(S52_GL_DRAW)) {

        //////////////////////////////////////////////
        // APP  .. update object (eg moving AIS, ..)
        //_app();


        //////////////////////////////////////////////
        // CULL .. remove object (eg outside view)
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

        //////////////////////////////////////////////
        // DRAW .. render

        _drawObj(name);
        //_drawText();

        // done rebuilding CS
        _doAPP_CS = FALSE;

        S52_GL_end(S52_GL_DRAW);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}
#endif  // S52_USE_GV

DLL int    STD S52_drawStr(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
{
    int ret = FALSE;

    return_if_null(colorName);
    return_if_null(str);

    S52_CHECK_MUTX_INIT_EGLBEG(STR);

    if (NULL == S57_getPrjStr())
        goto exit;

    if (FALSE == (ret = _validate_screenPos(&pixels_x, &pixels_y))) {
        PRINTF("WARNING: _validate_screenPos() failed\n");
        goto exit;
    }

    //PRINTF("X:%f Y:%f color:%s bsize:%i str:%s\n", pixels_x, pixels_y, colorName, bsize, str);

    ret = S52_GL_drawStrWin(pixels_x, pixels_y, colorName, bsize, str);


exit:

    EGL_END(STR);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    int ret = FALSE;

    // FIXME: try lock skip touch -
    // happen when broken EGL does a glFinish() in EGL swap
    S52_CHECK_MUTX_INIT_EGLBEG(BLIT);

    if (NULL == S57_getPrjStr())
        goto exit;

    // debug
    PRINTF("scale_x:%f, scale_y:%f, scale_z:%f, north:%f\n", scale_x, scale_y, scale_z, north);

    if (1.0 < ABS(scale_x)) {
        PRINTF("WARNING: zoom factor X overflow (>1.0) [%f]\n", scale_x);
        goto exit;
    }

    if (1.0 < ABS(scale_y)) {
        PRINTF("WARNING: zoom factor Y overflow (>1.0) [%f]\n", scale_y);
        goto exit;
    }

    if (0.5 < ABS(scale_z)) {
        PRINTF("WARNING: zoom factor Z overflow (>0.5) [%f]\n", scale_z);
        goto exit;
    }

    if ((north<0.0) || (360.0<=north)) {
        PRINTF("WARNING: north (%f) over/underflow\n", north);
        goto exit;
    }

    g_timer_reset(_timer);

    if (TRUE == S52_GL_begin(S52_GL_BLIT)) {
        ret = S52_GL_drawBlit(scale_x, scale_y, scale_z, north);

        S52_GL_end(S52_GL_BLIT);
    } else {
        PRINTF("WARNING: S52_GL_begin() failed\n");
    }

#ifdef S52_DEBUG
    {
        //gdouble sec = g_timer_elapsed(_timer, NULL);
        //PRINTF("DRAWBLIT: %.0f msec\n", sec * 1000);
    }
#endif


exit:

    EGL_END(BLIT);

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_xy2LL(double *pixels_x, double *pixels_y)
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr())
        goto exit;

    // check bound
    if (FALSE == _validate_screenPos(pixels_x, pixels_y)) {
        PRINTF("WARNING: _validate_screenPos() failed\n");
        goto exit;
    }

    if (FALSE == S52_GL_win2prj(pixels_x, pixels_y)) {
        PRINTF("WARNING: S52_GL_win2prj() failed\n");
        goto exit;
    }

    {
        projXY uv = {*pixels_x, *pixels_y};
        uv = S57_prj2geo(uv);
        *pixels_x = uv.u;
        *pixels_y = uv.v;
        ret = TRUE;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_LL2xy(double *longitude, double *latitude)
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr())
        goto exit;


    // ----------------------------------------------------------------------------
    // FIXME: extract to _LL2XY(guint npt, double *ppt);
    double xyz[3] = {*longitude, *latitude, 0.0};
    if (FALSE == S57_geo2prj3dv(1, xyz)) {
        PRINTF("WARNING: S57_geo2prj3dv() failed\n");
        goto exit;
    }

    S52_GL_prj2win(&xyz[0], &xyz[1]);

    *longitude = xyz[0];
    *latitude  = xyz[1];

    ret = TRUE;
exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

DLL int    STD S52_setView(double cLat, double cLon, double rNM, double north)
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    // debug
    PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);

    //*
    if (ABS(cLat) > 90.0) {
        PRINTF("WARNING: FAIL, cLat outside [-90..+90](%f)\n", cLat);
        goto exit;
    }

    if (ABS(cLon) > 180.0) {
        PRINTF("WARNING: FAIL, cLon outside [-180..+180] (%f)\n", cLon);
        goto exit;
    }
    //*/

    if ((rNM < MIN_RANGE) || (rNM > MAX_RANGE)) {
        PRINTF("WARNING: FAIL, rNM outside limit (%f)\n", rNM);
        goto exit;
    }

    // FIXME: PROJ4 will explode here (INFINITY) for mercator
    // Note: must validate rNM first
    if ((ABS(cLat)*60.0 + rNM) > (90.0*60)) {
        PRINTF("WARNING: FAIL, rangeNM > 90*60 NM (%f)\n", rNM);
        goto exit;
    }

    if ((north>=360.0) || (north<0.0)) {
        PRINTF("WARNING: FAIL, north outside [0..360[ (%f)\n", north);
        goto exit;
    }
    //}

    // debug
    //PRINTF("lat:%f, long:%f, range:%f north:%f\n", cLat, cLon, rNM, north);

    ret = S52_GL_setView(cLat, cLon, rNM, north);

    /* update local var _view
    _view.cLat  = cLat;
    _view.cLon  = cLon;
    _view.rNM   = rNM;
    _view.north = north;
    */

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    //return TRUE;
    return ret;
}

DLL int    STD S52_getView(double *cLat, double *cLon, double *rNM, double *north)
{
    return_if_null(cLat);
    return_if_null(cLon);
    return_if_null(rNM);
    return_if_null(north);

    S52_CHECK_MUTX_INIT;

    /* update local var _view
    *cLat  = _view.cLat;
    *cLon  = _view.cLon;
    *rNM   = _view.rNM;
    *north = _view.north;
    */

    S52_GL_getView(cLat, cLon, rNM, north);

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

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height)
{
    S52_CHECK_MUTX_INIT;

    PRINTF("pixels_x:%i, pixels_y:%i, pixels_width:%i, pixels_height:%i\n", pixels_x, pixels_y, pixels_width, pixels_height);

    //_validate_screenPos(&x, &y);

    S52_GL_setViewPort(pixels_x, pixels_y, pixels_width, pixels_height);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

static ObjExt_t   _getCellsExt(void)
{
    ObjExt_t extSum = {
        .W =  INFINITY,
        .S =  INFINITY,
        .E = -INFINITY,
        .N = -INFINITY
    };

    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, i);

        // Note: skip speudo ENC is cleaner than adjusting idx for diff config
        // for now just skip these pseudo cells
        if (0 == g_strcmp0(MARINER_CELL, c->filename->str))
            continue;
#ifdef S52_USE_WORLD
        if (0 == g_strcmp0(WORLD_SHP,    c->filename->str))
            continue;
#endif

        // +inf
        if (0 != isinf(c->geoExt.S)) {
            continue;
        }

        // N-S limits
        if (extSum.N < c->geoExt.N)
            extSum.N = c->geoExt.N;
        if (extSum.S > c->geoExt.S)
            extSum.S = c->geoExt.S;


        //------------------- E-W limits ------------------------

        // new cell allready inside ext
        if ((extSum.W < c->geoExt.W) && (c->geoExt.E < extSum.E))
            continue;

        // new cell totaly cover ext
        if ((c->geoExt.W < extSum.W) && (extSum.E < c->geoExt.E)) {
            extSum.W = c->geoExt.W;
            extSum.E = c->geoExt.E;
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

        double Aw  = extSum.W     + 180.0;
        double B1w = c->geoExt.W  + 180.0;
        double B2w = B1w          + 360.0;

        double Ae  = extSum.E     + 180.0;
        double B1e = c->geoExt.E  + 180.0;
        double B2e = B1e          + 360.0;


        // dist 1  >  dist 2
        if ((Aw - B1w) > (B2w - Aw))
            extSum.E = c->geoExt.E;
        else
            extSum.W = c->geoExt.W;

        // dist 1  >  dist 2
        if ((Ae - B1e) > (B2e - Ae))
            extSum.E = c->geoExt.E;
        else
            extSum.W = c->geoExt.W;

    }

#ifdef S52_USE_WORLD
    /* if only world is loaded
    if (FALSE == ret) {
        _cell *c = (_cell*) g_ptr_array_index(_cellList, _cellList->len-1);

        extSum->S = c->geoExt.S;
        extSum->W = c->geoExt.W;
        extSum->N = c->geoExt.N;
        extSum->E = c->geoExt.E;

        ret = TRUE;
    }
    */
#endif

    //return ret;
    return extSum;
}

DLL int    STD S52_getCellExtent(const char *filename, double *S, double *W, double *N, double *E)
{
    int ret = FALSE;

    if (NULL==S || NULL==W || NULL==N || NULL==E) {
        PRINTF("WARNING: NULL extent S,W,N,E\n");
        return FALSE;
    }

    S52_CHECK_MUTX_INIT;

    if (NULL == filename) {
        ObjExt_t ext = _getCellsExt();
        *S = ext.S;  // !?! clang - assign garbage or undefined
        *W = ext.W;
        *N = ext.N;
        *E = ext.E;

        ret = TRUE;

        PRINTF("DEBUG: ALL EXT(S,W - N,E): %f, %f -- %f, %f\n", *S, *W, *N, *E);
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

            goto exit;
        }

        for (guint i=0; i<_cellList->len; ++i) {
            _cell *c = (_cell*)g_ptr_array_index(_cellList, i);

            if (0 == g_strcmp0(name, c->filename->str)) {
                *S = c->geoExt.S;
                *W = c->geoExt.W;
                *N = c->geoExt.N;
                *E = c->geoExt.E;
                //PRINTF("%s: %f, %f, %f, %f\n", filename, ext->s, ext->w, ext->n, ext->e);

                // cell found
                ret = TRUE;
                break;
            }
        }

        g_free(fnm);
        g_free(name);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    //return TRUE;
    return ret;
}


// -----------------------------------------------------
//
// Selecte object (other then DISPLAYBASE) to display
// when in User Selected mode
//

DLL int    STD S52_getS57ObjClassSupp(const char *className)
{
    return_if_null(className);

    S52_objSupp suppState = S52_SUPP_ERR;

    S52_CHECK_MUTX_INIT;

    suppState = S52_PL_getObjClassState(className);
    if (S52_SUPP_ERR == suppState) {
        PRINTF("WARNING: can't toggle %s\n", className);
        GMUTEXUNLOCK(&_mp_mutex);
        return -1;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    if (S52_SUPP_ON == suppState)
        return TRUE;
    else
        return FALSE;
}

DLL int    STD S52_setS57ObjClassSupp(const char *className, int value)
{
    return_if_null(className);

    int ret = FALSE;
    //S52_objSupp suppState = S52_SUPP_ERR;

    S52_CHECK_MUTX_INIT;

    S52_objSupp suppState = S52_PL_getObjClassState(className);
    if (S52_SUPP_ERR == suppState) {
        PRINTF("WARNING: can't toggle %s\n", className);
        ret = -1;
        goto exit;
    }

    if (TRUE==value  && S52_SUPP_ON==suppState) {
        goto exit;
    }

    if (FALSE==value && S52_SUPP_OFF==suppState) {
        goto exit;
    }

    ret = S52_PL_toggleObjClass(className);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}
// --- Selecte object --------------------------------------------------




//-------------------------------------------------------
//
// FEEDBACK TO HIGHER UP MODULE OF INTERNAL STATE
//

DLL CCHAR *STD S52_pickAt(double pixels_x, double pixels_y)
{
    static const char *name;
    name = NULL;

    S52_CHECK_MUTX_INIT_EGLBEG(PICK);

    if (NULL == S57_getPrjStr())
        goto exit;

    // viewport
    int x;
    int y;
    int width;
    int height;

    double ps,pw,pn,pe;   // hold PRJ view
    double gs,gw,gn,ge;   // hold GEO view
    double oldAA = 0.0;

    if (0.0 == S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
        goto exit;
    }

#if !defined(S52_USE_C_AGGR_C_ASSO)
    if (3.0 == S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
        PRINTF("WARNING: mode 3 need -DS52_USE_C_AGGR_C_ASSO\n");
        goto exit;
    }
#endif

    // check bound
    if (FALSE == _validate_screenPos(&pixels_x, &pixels_y)) {
        PRINTF("WARNING: coord out of scteen\n");
        goto exit;
    }

    g_timer_reset(_timer);

    // if blending is ON the cursor pick will not work
    oldAA = S52_MP_get(S52_MAR_ANTIALIAS);
    S52_MP_set(S52_MAR_ANTIALIAS, FALSE);

    // compute pick view parameter

    // mouse has 'Y' down, opengl is up
    S52_GL_getViewPort(&x, &y, &width, &height);
    double tmp_px_y = y + pixels_y;

    // debug
    PRINTF("DEBUG: pixels_x:%f, pixels_y:%f\n", pixels_x, pixels_y);

    // FIXME: check bound
    // Nexus/Adreno ReadPixels must be POT, hence 8 x 8 extent

    // pick extent
    ObjExt_t ext = {
        .N = tmp_px_y + 4,
        .S = tmp_px_y - 4,
        .E = pixels_x + 3,
        .W = pixels_x - 3
    };

    S52_GL_win2prj(&ext.W, &ext.S);
    S52_GL_win2prj(&ext.E, &ext.N);

    // save current view
    S52_GL_getPRJView(&ps, &pw, &pn, &pe);
    // set view of the pick
    S52_GL_setPRJView(ext.S, ext.W, ext.N, ext.E);
    //S52_GL_setViewPort(pixels_x-4, tmp_px_y-4, 8, 8);
    S52_GL_setViewPort(pixels_x, tmp_px_y, 1, 1);
    //PRINTF("PICK PRJ EXTENT (swne): %f, %f  %f, %f \n", ext.s, ext.w, ext.n, ext.e);

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

        PRINTF("DEBUG: PICK LL EXTENT (swne): %f, %f  %f, %f \n", ext.S, ext.W, ext.N, ext.E);
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
        //_drawLast();
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
            _drawLast(_marinerCell->renderBin[S52_PRIO_MARINR][j]);
        }

        S52_GL_end(S52_GL_PICK);
    } else {
        PRINTF("WARNING:S52_GL_begin() failed\n");
        goto cleanup;
    }

    // get object picked
    name = S52_GL_getNameObjPick();
    PRINTF("DEBUG: OBJECT PICKED (%6.1f, %6.1f): %s\n", pixels_x, pixels_y, name);


#ifdef S52_DEBUG
    {
        gdouble sec = g_timer_elapsed(_timer, NULL);
        PRINTF("DEBUG:     PICKAT: %.0f msec\n", sec * 1000);
    }
#endif

cleanup:
    // restore view
    S52_GL_setViewPort(x, y, width, height);
    S52_GL_setPRJView(ps, pw, pn, pe);
    S52_GL_setGEOView(gs, gw, gn, ge);

    // replace original blending state
    S52_MP_set(S52_MAR_ANTIALIAS, oldAA);

exit:

    EGL_END(PICK);

    GMUTEXUNLOCK(&_mp_mutex);

    return name;
}

DLL CCHAR *STD S52_getPLibNameList(void)
{
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    str = _plibNameList->str;

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL CCHAR *STD S52_getPalettesNameList(void)
// return a string of palettes name loaded
{
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    g_string_set_size(_paltNameList, 0);

    int palTblsz = S52_PL_getPalTableSz();
    for (int i=0; i<palTblsz; ++i) {
        char *frmt = (0 == i) ? "%s" : ",%s";
        g_string_append_printf(_paltNameList, frmt, (char*)S52_PL_getPalTableNm(i));
    }

    str = _paltNameList->str;

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

//void (*GFunc) (gpointer data, gpointer user_data);
//static GString   *_getMARINClassList(void)
static void       _getS57ClassList(S52_obj *obj, GString *classList)
{
    //GString *classList = g_string_new(MARINER_CELL);

    /*
    for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) {
        for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
            GPtrArray *rbin = _marinerCell->renderBin[i][j];
            for (guint idx=0; idx<rbin->len; ++idx) {
                S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                const char *oname = S52_PL_getOBCL(obj);
                if (NULL == g_strrstr(classList->str, oname)) {
                    g_string_append_printf(classList, ",%s", oname);
                }
            }
        }
    }
    */

    //TRAV_RBIN_ij(g_ptr_array_foreach(_marinerCell->renderBin[i][j], (GFunc)__saveList, classList));

    const char *oname = S52_PL_getOBCL(obj);
    if (NULL == g_strrstr(classList->str, oname)) {
        g_string_append_printf(classList, ",%s", oname);
    }


    //return classList;
    return;
}

DLL CCHAR *STD S52_getS57ClassList(const char *cellName)
{
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    g_string_set_size(_S57ClassList, 0);

    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, i);

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
                    //GString *classList = _getMARINClassList();
                    GString *classList = g_string_new(MARINER_CELL);
                    TRAV_RBIN_ij(g_ptr_array_foreach(_marinerCell->renderBin[i][j], (GFunc)_getS57ClassList, classList));


                    g_string_printf(_S57ClassList, "%s", classList->str);

                    g_string_free(classList, TRUE);

                    goto exit;
                }
                // normal cell
                if (NULL != c->S57ClassList) {
                    g_string_printf(_S57ClassList, "%s,%s", c->filename->str, c->S57ClassList->str);

                    goto exit;
                }
            }
        }
    }

exit:

    if (0 != _S57ClassList->len)
        str = _S57ClassList->str;

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

//void (*GFunc) (gpointer data, gpointer user_data);
static void       _getObjList(S52_obj *obj, const char *className)
{
    const char *oname = S52_PL_getOBCL(obj);
    if (0 == g_strcmp0(className, oname)) {
        S57_geo *geo = S52_PL_getGeo(obj);

        //  S57ID / geo / disp cat / disp prio
        g_string_append_printf(_S52ObjNmList, ",%i:%c:%c:%i",
                               S57_getS57ID(geo),
                               S57_getObjtype(geo),    // return same val as S52_PL_getFTYP()
                               //S52_PL_getFTYP(obj),  // same as 'j', but in text (char) equivalent
                               S52_PL_getDISC(obj),    //
                               S52_PL_getDPRI(obj));   // same as 'i'
    }

    return;
}

DLL CCHAR *STD S52_getObjList(const char *cellName, const char *className)
{
    return_if_null(cellName);
    return_if_null(className);

    //static const char *str = NULL;  // clang complain NULL never used
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    PRINTF("cellName: %s, className: %s\n", cellName, className);

    //int header = TRUE;
    g_string_set_size(_S52ObjNmList, 0);

    for (guint k=0; k<_cellList->len; ++k) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, k);

        if (0 == g_strcmp0(cellName, c->filename->str)) {

            /*
            for (S52_disPrio i=S52_PRIO_NODATA; i<S52_PRIO_NUM; ++i) {
                for (S52ObjectType j=S52__META; j<S52_N_OBJ; ++j) {
                    GPtrArray *rbin = c->renderBin[i][j];
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj    *obj   = (S52_obj *)g_ptr_array_index(rbin, idx);
                        const char *oname = S52_PL_getOBCL(obj);
                        if (0 == g_strcmp0(className, oname)) {
                            if (TRUE == header) {
                                g_string_printf(_S52ObjNmList, "%s,%s", cellName, className);
                                header = FALSE;
                            }
                            S57_geo *geo = S52_PL_getGeo(obj);
                            //  S57ID / geo / disp cat / disp prio
                            g_string_append_printf(_S52ObjNmList, ",%i:%c:%c:%i",
                                                   S57_getS57ID(geo),
                                                   S57_getObjtype(geo),    // return same val as S52_PL_getFTYP()
                                                   //S52_PL_getFTYP(obj),  // same as 'j', but in text (char) equivalent
                                                   S52_PL_getDISC(obj),    //
                                                   S52_PL_getDPRI(obj));   // same as 'i'
                        }
                    }
                }
            }
            */


            TRAV_RBIN_ij(g_ptr_array_foreach(c->renderBin[i][j], (GFunc)_getObjList, (void*)className));

            // insert header
            if (0 != _S52ObjNmList->len) {
                //    insert className at 0
                g_string_insert  (_S52ObjNmList, 0, className);
            //    insert char ',' at 0
                g_string_insert_c(_S52ObjNmList, 0, ',');
            //    insert cellName at 0
                g_string_insert  (_S52ObjNmList, 0, cellName);

            }

            PRINTF("%s\n", _S52ObjNmList->str);
            str = _S52ObjNmList->str;

            // cell found - bailout
            break;
        }
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL CCHAR *STD S52_getAttList(unsigned int S57ID)
{
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    PRINTF("S57ID: %i\n", S57ID);

    S52_obj *obj = S52_PL_isObjValid(S57ID);
    if (NULL != obj) {
        S57_geo *geo = S52_PL_getGeo(obj);
        if (NULL != geo)
            str = S57_getAtt(geo);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}

DLL CCHAR *STD S52_getCellNameList(void)
{
    static const char *str;
    str = NULL;

    S52_CHECK_MUTX_INIT;

    g_string_set_size(_cellNameList, 0);

    for (guint i=0; i<_cellList->len; ++i) {
        _cell *c = (_cell*)g_ptr_array_index(_cellList, i);

        // MARINER_CELL has no encPath
        if (NULL == c->encPath)
            continue;

        if (0 == _cellNameList->len)
            g_string_append_printf(_cellNameList, "*%s",  c->encPath);
        else
            g_string_append_printf(_cellNameList, ",*%s", c->encPath);

        GError *error = NULL;
        gchar  *path  = g_path_get_dirname(c->encPath);
        GDir   *dir   = g_dir_open(path, 0, &error);
        if (NULL != error) {
            PRINTF("WARNING: g_dir_open() failed (%s)\n", error->message);
            g_error_free(error);
        }
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

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return str;
}
//--- FEEDBACK ----------------------------------------------------


DLL int    STD S52_setRGB(const char *colorName, unsigned char  R, unsigned char  G, unsigned char  B)
{
    return_if_null(colorName);

    S52_CHECK_MUTX_INIT;

    PRINTF("colorName:%s, R:%c, G:%c, B:%c\n", colorName, R, G, B);

    S52_PL_setRGB(colorName, R, G, B);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B)
{
    return_if_null(colorName);
    return_if_null(R);
    return_if_null(G);
    return_if_null(B);

    S52_CHECK_MUTX_INIT;

    PRINTF("colorName:%s, R:%#lX, G:%#lX, B:%#lX\n", colorName, (long unsigned int)R, (long unsigned int)G, (long unsigned int)B);

    S52_PL_getRGB(colorName, R, G, B);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return TRUE;
}

DLL int    STD S52_setRADARCallBack(S52_RADAR_cb cb, unsigned int texRadius)
// experimental - load raw raster RADAR via callback
{
    (void)cb;
    (void)texRadius;

    int ret = TRUE;

#ifdef S52_USE_RADAR

    return_if_null(cb);

    S52_CHECK_MUTX_INIT;

    PRINTF("cb:#lX, texRadius:%u\n", (long unsigned int)cb, texRadius);

    // prevent dup or dispose
    for (guint i=0; i<_rasterList->len; ++i) {
        S52_GL_ras *raster = (S52_GL_ras *) g_ptr_array_index(_rasterList, i);
        if (cb == raster->RADAR_cb) {
            if (0 == texRadius) {
                // will call free_func() if set
                g_ptr_array_remove_index_fast(_rasterList, i);

                S52_GL_delRaster(raster, TRUE);

                goto exit;
            } else {
                ret = FALSE;
                goto exit;
            }
        }
    }

    // not found - create new
    S52_GL_ras *raster = S52_GL_newRaster(NULL);
    raster->isRADAR    = TRUE;
    raster->RADAR_cb   = cb;
    raster->h          = texRadius * 2;  // N/S
    raster->w          = texRadius * 2;  // E/W
    g_ptr_array_add(_rasterList, raster);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

#endif

    //return TRUE;
    return ret;
}

DLL int    STD S52_setEGLCallBack(S52_EGL_cb eglBeg, S52_EGL_cb eglEnd, void *EGLctx)
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

static int        _setAtt(S57_geo *geo, const char *listAttVal)
// must be in name/value pair,
// FIXME: is this OK .. use '---' for a NULL value for any attribute name
{
    return_if_null(geo);
    return_if_null(listAttVal);

    gchar delim[4] = {":,\0"};

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

    gchar** freeL = attvalL;

    /* FIXME: do not replace '\n' by ' ', for JSON
     if (0 == g_strcmp0("_vessel_label", attName)) {
     for (guint i=0; i<attList->len; ++i) {
     if ('\n' == attList->str[i]) {
     attList->str[i] = ' ';
     //g_string_insert_c(attList, i, SLASH);
     return;
     }
     }
     }
     */

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
        //if ('\0' == *attvalL[1]) {  // clang - dereference null ptr
        if ((NULL==attvalL[1]) || ('\0'==*attvalL[1])) {
            PRINTF("WARNING: mixed up val, the rest of list fail [%s]\n", listAttVal);
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

    g_strfreev(freeL);

    return TRUE;
}

static int        _setExt(S57_geo *geo, unsigned int xyznbr, double *xyz)
{
    // FIXME: crossing of anti-meridian
    ObjExt_t ext = {
        .S =  INFINITY,
        .W =  INFINITY,
        .N = -INFINITY,
        .E = -INFINITY
    };

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

static int        _isObjNameValid(S52_obj *obj, const char *objName)
// return TRUE if the class name of 'obj' is 'objName' else FALSE
{
    S57_geo *geo = S52_PL_getGeo(obj);
    if (0 != g_strcmp0(objName, S57_getName(geo))) {
        //PRINTF("WARNING: object name invalid\n");
        return FALSE;
    }

    return TRUE;
}

DLL int    STD S52_dumpS57IDPixels(const char *toFilename, unsigned int S57ID, unsigned int width, unsigned int height)
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr())
        goto exit;

    if (0 == S57ID) {
        S52_GL_dumpS57IDPixels(toFilename, NULL, width, height);
    } else {
        S52_obj *obj = S52_PL_isObjValid(S57ID);
        if (NULL != obj)
            ret = S52_GL_dumpS57IDPixels(toFilename, obj, width, height);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

static S52ObjectHandle     _newMarObj(const char *plibObjName, S52ObjectType objType,
                                      unsigned int xyznbr, double *xyz, const char *listAttVal)
{
    S57_geo  *geo     = NULL;
    double   *gxyz    = NULL;
    double  **ggxyz   = NULL;
    guint    *gxyznbr = NULL;
    //S52_obj  *obj     = NULL;

    if (0 != xyznbr) {

        if (NULL != xyz) {
            // transfer and project xyz
            double *dst  = NULL;
            double *src  = xyz;

            /* FIXME: check area: close poly
             if ((ppt[0]!=ppt[3*(npt-1) + 0]) || (ppt[1]!=ppt[3*(npt-1) + 1]))  {
             PRINTF("DEBUG: first pt != last pt\n");
             g_assert(0);
             }
             */

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


            if (FALSE == S57_geo2prj3dv(xyznbr, gxyz)) {
                PRINTF("WARNING: projection failed\n");
                g_assert(0);
                // FIXME: if we get here free mem
                //return NULL;
                return FALSE;
            }
        } else {
            // create an empty xyz buffer
            gxyz = g_new0(double, xyznbr*3);
        }

    } else {
        PRINTF("WARNING: no xyz nbr\n");
        g_assert(0);

        return FALSE;
    }

    // FIXME: outer ring only
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
        default: PRINTF("WARNING: invalid obj type\n");        break;
    }

    if (NULL == geo) {
        PRINTF("WARNING: S57 geo object build failed\n");
        g_free(gxyznbr);
        g_free(ggxyz);
        g_free(gxyz);

        return FALSE;
    }

    S57_setName(geo, plibObjName);

    // full of coordinate
    if (NULL != xyz) {
        if (0 == xyznbr) {
            PRINTF("WARNING: unknown size geo XYZ \n");
            g_assert(0);
        }
        S57_setGeoSize(geo, xyznbr);
        _setExt(geo, xyznbr, xyz);
    }

    if (NULL != listAttVal)
        _setAtt(geo, listAttVal);

    S52_obj  *obj = _insertS57geo(_marinerCell, geo);
    if (NULL == obj) {
        S57_doneData(geo, NULL);
        return FALSE;
    }

    // init TX & TE
    S52_PL_resetParseText(obj);

    // doCS now (intead of _app() - expensive)
    S52_PL_resloveSMB(obj, NULL);

    // set timer for afterglow
    if (0 == g_strcmp0("vessel", S57_getName(geo))) {
        S52_PL_setTimeNow(obj);
    }
#ifdef S52_USE_AFGLOW
    else {
        if (0 == g_strcmp0("afgves", S57_getName(geo))) {
            S52_PL_setTimeNow(obj);
        }
    }
#endif

    //return S57_getS57ID(geo);
    return S57GETS57ID(geo);
}

DLL S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
                                      unsigned int xyznbr, double *xyz, const char *listAttVal)
{
    return_if_null(plibObjName);

    S52ObjectHandle objH = FALSE;

    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr())
        goto exit;

    // debug
    PRINTF("plibObjName:%s, objType:%i, xyznbr:%u, xyz:%p, listAttVal:<%s>\n",
            plibObjName, objType, xyznbr, xyz, (NULL==listAttVal)?"NULL":listAttVal );

    // is this really needed? (how about META!!)
    if (0 == xyznbr) {
        PRINTF("WARNING: xyznbr == 0\n");
        g_assert(0);
        goto exit;
    }

    // uint
    if (S52_POINT < objType) {
        PRINTF("WARNING: unknown object type\n");
        g_assert(0);
        goto exit;
    }

    objH = _newMarObj(plibObjName, objType, xyznbr, xyz, listAttVal);


    // debug
    //PRINTF("objH:%u, plibObjName:%s, ID:%i, objType:%i, xyznbr:%u, xyz:%p, listAttVal:<%s>\n",
    //        obj,     plibObjName, S57_getS57ID(geo), objType, xyznbr, gxyz,
    //       (NULL==listAttVal)?"NULL":listAttVal);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_getMarObj(unsigned int S57ID)
{
    S52ObjectHandle ret = 0;

    S52_CHECK_MUTX_INIT;

    S52_obj *obj = S52_PL_isObjValid(S57ID);
    if (NULL != obj) {
        ret = S57ID;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}

static S52ObjectHandle     _delMarObj(S52ObjectHandle objH)
// validate this obj and remove if found
{
    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        // will happen when AIS self-destruct on S52_MAR_DISP_VESSEL_DELAY
        PRINTF("WARNING: couldn't delete .. objH not in Mariners' Object List\n");
        return objH;
    }

    GPtrArray *array = NULL;
    if (0 == g_strcmp0(S52_PL_getOBCL(obj), "LIGHTS")) {
        array = _marinerCell->lights_sector;
    } else {
        // select rbin
        S52_disPrio   disPrioIdx = S52_PL_getDPRI(obj);
        S52ObjectType obj_t      = S52_PL_getFTYP(obj);

        array = _marinerCell->renderBin[disPrioIdx][obj_t];
    }

    // will call _delObj() if free_func() set
    if (TRUE == g_ptr_array_remove(array, obj)) {
        //_delObj(obj, NULL);

        return FALSE;
    }

    PRINTF("WARNING: objH not found in Mariners' Object\n");
    g_assert(0);

    return objH;
}

DLL S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH)
// contrairy to other call return objH when fail
// so caller can further process it
// Note: will happen when an AIS get older than S52_MAR_DISP_VESSEL_DELAY
//       and self-destruct
{
    S52_CHECK_MUTX_INIT;

    PRINTF("objH:%u\n", objH);

    if (objH == _OWNSHP) {
        _OWNSHP = FALSE;
    }

    objH = _delMarObj(objH);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_toggleDispMarObj(S52ObjectHandle  objH)
{
    S52_CHECK_MUTX_INIT;

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL != obj) {
        if (TRUE == S52_PL_getSupp(obj)) {
            S52_PL_setSupp(obj, FALSE);
        } else {
            S52_PL_setSupp(obj, TRUE);
        }
    } else {
        objH = FALSE;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_newCLRLIN(int catclr, double latBegin, double lonBegin, double latEnd, double lonEnd)
{
    S52ObjectHandle clrlin = FALSE;

    S52_CHECK_MUTX_INIT;

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
        char attval[80];
        SNPRINTF(attval, 80, "catclr:%i", catclr);
        double xyz[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};

        clrlin = _newMarObj("clrlin", S52_LINES, 2, xyz, attval);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return clrlin;
}

DLL S52ObjectHandle STD S52_newLEGLIN(int select, double plnspd, double wholinDist,
                                      double latBegin, double lonBegin, double latEnd, double lonEnd,
                                      S52ObjectHandle previousLEGLIN)
{
    S52ObjectHandle leglinH = FALSE;

    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr())
        goto exit;

    PRINTF("select:%i, plnspd:%f, wholinDist:%f, latBegin:%f, lonBegin:%f, latEnd:%f, lonEnd:%f\n",
            select,    plnspd,    wholinDist,    latBegin,    lonBegin,    latEnd,    lonEnd);

    // validation
    if (1!=select && 2!=select) {
        PRINTF("WARNING: 'select' must be 1 or 2 (%i).. reset to 1\n", select);
        select = 1;
    }

    // FIXME: validate_distance; wholinDist is not greater then begin-end
    // FIXME: validate_double; plnspd, wholinDist
    latBegin = _validate_lat(latBegin);
    lonBegin = _validate_lon(lonBegin);
    latEnd   = _validate_lat(latEnd);
    lonEnd   = _validate_lon(lonEnd);

    // check if same point
    if (latBegin==latEnd  && lonBegin==lonEnd) {
        PRINTF("WARNING: rejecting this leglin (same point)\n");
        goto exit;
    }

    // ---  check if hazard inside Guard Zone ---
    // FIXME: call _GuardZoneCheck(), document what object is checked for, right now CS/DEPCNT02
    // FIXME: GL_proj will fail if no GL_draw() call was invoqued first
    // - FIX: call _draw() or GL_proj_init
    if (0.0 != S52_MP_get(S52_MAR_GUARDZONE_BEAM)) {
        double oldCRSR_PICK = S52_MP_get(S52_MAR_DISP_CRSR_PICK);
        double beam2        = S52_MP_get(S52_MAR_GUARDZONE_BEAM)/2.0;
        double ps,pw,pn,pe;  // hold PRJ view
        double gs,gw,gn,ge;  // hold GEO view
        int    x,y,w,h;      // hold ViewPort

        ObjExt_t ext = {
            .S =  INFINITY,
            .W =  INFINITY,
            .N = -INFINITY,
            .E = -INFINITY
        };

        // set pick to stack mode
        S52_MP_set(S52_MAR_DISP_CRSR_PICK, 2.0);

        ext.N = (latBegin > latEnd) ? latBegin : latEnd;
        ext.S = (latBegin < latEnd) ? latBegin : latEnd;
        ext.E = (lonBegin < lonEnd) ? lonBegin : lonEnd;
        ext.W = (lonBegin > lonEnd) ? lonBegin : lonEnd;

                                                                       //   +-------------+
        // save current view & set view of the pick                    //   |  | beam2    |
        S52_GL_getViewPort(&x, &y, &w, &h);                            //   |  |          |
        S52_GL_getPRJView(&ps, &pw, &pn, &pe);                         //   |--o A        |
        S52_GL_getGEOView(&gs, &gw, &gn, &ge);                         //   |  \\\        |
                                                                       //   |   \\\       |
                                                                       //   |    \\\  LEG |
                                                                       //   |     \\\     |
        double xyz[6] = {ext.W, ext.S, 0.0, ext.E, ext.N, 0.0};        //   |      \\\    |
        S57_geo2prj3dv(2, xyz);                                        //   |       \\\   |
                                                                       //   |        \\\  |
                                                                       //   |        B o--|
                                                                       //   |          |  |
                                                                       //   |    beam2 |  |
                                                                       //   +-------------+

        S52_GL_setViewPort(w/2, h/2, 1, 1);             // if chart rotated --> x=w/2, y=h/2
        // FIXME: is -+beam2 the max
        S52_GL_setPRJView(xyz[1]-beam2, xyz[0]-beam2, xyz[4]+beam2, xyz[3]+beam2);  // snap to viewPort
        // FIXME: augment GEO for -+beam2
        S52_GL_setGEOView(ext.S, ext.W, ext.N, ext.E);  // to cull obj

        if (TRUE == S52_GL_begin(S52_GL_PICK)) {
            // only "over" used
            _resetJournal();

            _app();

            // cull
            // all cells - larger region first (small scale)
            for (guint i=_cellList->len-1; i>0; --i) {
                _cell *c = (_cell*) g_ptr_array_index(_cellList, i);
#ifdef S52_USE_WORLD
                if ((0==g_strcmp0(WORLD_SHP, c->filename->str)) && (FALSE==(int) S52_MP_get(S52_MAR_DISP_WORLD)))
                    continue;
#endif
                // is this chart visible
                if (TRUE == _intersectEXT(c->geoExt, ext)) {
                    // layer 8, over radar, BASE, LINE
                    // FIXME: later check AREA & POINT (OBSTRN04/Area,Line,Ppoint, WRECKS02/Area,Point)
                    GPtrArray *rbin = c->renderBin[S52_PRIO_HAZRDS][S52_LINES];

                    // for each object
                    for (guint idx=0; idx<rbin->len; ++idx) {
                        S52_obj *obj = (S52_obj *)g_ptr_array_index(rbin, idx);
                        S57_geo *geo = S52_PL_getGeo(obj);

                        if (FALSE == S57_isHazard(geo))
                            continue;

                        // outside view
                        // Note: object can be inside 'ext' but outside the 'view' (cursor pick)
                        if (TRUE == S52_GL_isOFFview(obj)) {
                            ++_nCull;
                            continue;
                        }

                        // over radar
                        g_ptr_array_add(c->objList_over, obj);
                    }
                }
            }

            // render object that fall in the pick view
            // FIXME: need timer to see if using GPU really faster vs CPU only
            _draw();

            S52_GL_end(S52_GL_PICK);
        } else {
            PRINTF("WARNING:S52_GL_begin() failed\n");
        }

        // put back original value
        S52_GL_setViewPort(x, y, w, h);
        S52_GL_setPRJView(ps, pw, pn, pe);
        S52_GL_setGEOView(gs, gw, gn, ge);

        S52_MP_set(S52_MAR_DISP_CRSR_PICK, oldCRSR_PICK);

        {
            double xyz[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};
            S57_geo2prj3dv(2, xyz);
            double cog = ATAN2TODEG(xyz);
            double xyz2[5*3];

            // experiment I - align guardzone on lat/lon
            //  LEG/
            //    /    /
            //   /    / beam/2
            //   |\  /
            //   | \/
            //   | /
            //   |/
            //   +   lat_beam2
            //   lon_leg


            /* CCW
            xyz2[ 0] = xyz[0] - beam2/sin((90.0 - cog)*DEG_TO_RAD);  // lonBeg
            xyz2[ 1] = xyz[1];                                       // latBeg
            //xyz2[ 0] = xyz[0];  // lonBeg
            //xyz2[ 1] = xyz[1] - beam2/sin(cog*DEG_TO_RAD);  // latBeg
            xyz2[ 2] = 0.0;

            //xyz2[ 3] = xyz[3];                                       // lonEnd
            //xyz2[ 4] = xyz[4] + beam2/sin(cog*DEG_TO_RAD);           // latEnd
            xyz2[ 3] = xyz[3] + beam2/sin((90.0 - cog)*DEG_TO_RAD);                                       // lonEnd
            xyz2[ 4] = xyz[4];           // latEnd
            xyz2[ 5] = 0.0;

            xyz2[ 9] = xyz[0];                                       // lonBeg
            xyz2[10] = xyz[1] - beam2/sin(cog*DEG_TO_RAD);           // latBeg
            xyz2[11] = 0.0;

            xyz2[ 6] = xyz[3] + beam2/sin((90.0 - cog)*DEG_TO_RAD);  // lonEnd
            xyz2[ 7] = xyz[4];                                       // latEnd
            xyz2[ 8] = 0.0;
            //*/

            /* CW
            xyz2[ 0] = xyz[0] - beam2/sin((90.0 - cog)*DEG_TO_RAD);  // lonBeg
            xyz2[ 1] = xyz[1];                                       // latBeg
            xyz2[ 2] = 0.0;

            xyz2[ 9] = xyz[3];                                       // lonEnd
            xyz2[10] = xyz[4] + beam2/sin(cog*DEG_TO_RAD);           // latEnd
            xyz2[11] = 0.0;

            xyz2[ 3] = xyz[0];                                       // lonBeg
            xyz2[ 4] = xyz[1] - beam2/sin(cog*DEG_TO_RAD);           // latBeg
            xyz2[ 5] = 0.0;

            xyz2[ 6] = xyz[3] + beam2/sin((90.0 - cog)*DEG_TO_RAD);  // lonEnd
            xyz2[ 7] = xyz[4];                                       // latEnd
            xyz2[ 8] = 0.0;

            xyz2[12] =   // lonEnd
            xyz2[13] =   // latEnd
            xyz2[14] = 0.0;
            //*/

            // experiment II - check rectangular GuardZone then check WP GuardZone
            // OR add 2 pts (one at each end) beam2 length that extend leg!

            // Corner-case, need rounded end
            //
            //    LEG1  *
            // -------+ *   DEPCNT
            //        | *
            //        | *****
            //     +--+--+
            //     |  |  |
            //     |  |  |  LEG2
            // ----+--+  |
            //     |     |
            //     |     |

            double dlon = cos(cog*DEG_TO_RAD) * beam2;
            double dlat = sin(cog*DEG_TO_RAD) * beam2;

            // CW
            // starboard
            xyz2[ 0] = xyz[0] + dlon;  // lonBeg
            xyz2[ 1] = xyz[1] - dlat;  // latBeg
            xyz2[ 2] = 0.0;

            // port
            xyz2[ 3] = xyz[0] - dlon;  // lonBeg
            xyz2[ 4] = xyz[1] + dlat;  // latBeg
            xyz2[ 5] = 0.0;

            // port
            xyz2[ 6] = xyz[3] - dlon;  // lonEnd
            xyz2[ 7] = xyz[4] + dlat;  // latEnd
            xyz2[ 8] = 0.0;

            // sarboard
            xyz2[ 9] = xyz[3] + dlon;  // lonEnd
            xyz2[10] = xyz[4] - dlat;  // latEnd
            xyz2[11] = 0.0;

            // loop line
            xyz2[12] = xyz2[ 0];       //
            xyz2[13] = xyz2[ 1];       //
            xyz2[14] = 0.0;

            if (TRUE == S52_GL_isHazard(5, xyz2)) {
                //S52_MP_set(S52_MAR_ERROR, 2.0);  // indication
                S52_MP_set(S52_MAR_GUARDZONE_ALARM, 2.0);  // indication
            }
        }
    }

    {   // create LEGLIN
        char   attval[80];
        double xyztmp[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};
        S57_geo2prj3dv(2, xyztmp);
        double cog = ATAN2TODEG(xyztmp);

        // FIXME: used leglin to get cog when rendering
        if (0.0 != wholinDist) {
            //SNPRINTF(attval, 80, "select:%i,plnspd:%f,_wholin_dist:%f", select, plnspd, wholinDist);
            SNPRINTF(attval, 80, "leglin:%f,select:%i,plnspd:%f,_wholin_dist:%f", cog, select, plnspd, wholinDist);
        } else {
            //SNPRINTF(attval, 80, "select:%i,plnspd:%f", select, plnspd);
            SNPRINTF(attval, 80, "leglin:%f,select:%i,plnspd:%f", cog, select, plnspd);
        }

        double xyz[6] = {lonBegin, latBegin, 0.0, lonEnd, latEnd, 0.0};
        leglinH = _newMarObj("leglin", S52_LINES, 2, xyz, attval);
        if (FALSE == leglinH) {
            PRINTF("WARNING: LEGLIN_H not a valid S52ObjectHandle\n");
            g_assert(0);
            goto exit;
        }

        // Note: FALSE == (VOID*)NULL or FALSE == (int) 0
        if (FALSE != previousLEGLIN) {
            S52_obj *objPrevLeg = S52_PL_isObjValid(previousLEGLIN);
            if (NULL == objPrevLeg) {
                PRINTF("WARNING: previousLEGLIN not a valid S52ObjectHandle\n");
            } else {
                S52_obj *obj = S52_PL_isObjValid(leglinH);
                if (NULL != obj) {
                    S52_PL_setNextLeg(objPrevLeg, obj);
                }
            }
        }

        // check alarm for this leg
        //if (0.0 != S52_MP_get(S52_MAR_ERROR)) {
        if (0.0 != S52_MP_get(S52_MAR_GUARDZONE_ALARM)) {
            S52_obj *obj = S52_PL_isObjValid(leglinH);
            // this test is not required since the obj has just been created
            if (NULL == obj) {
                S57_geo *geo = S52_PL_getGeo(obj);
                S57_setHighlight(geo, TRUE);
            }
        }
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return leglinH;
}

DLL S52ObjectHandle STD S52_newOWNSHP(const char *label)
{
    S52_CHECK_MUTX_INIT;

    char   attval[80];

    SNPRINTF(attval, 80, "_vessel_label:%s", (NULL==label) ? "":label);

    // only one OWNSHP
    // FIXME: what if we want 2 GPS shown
    if (FALSE == _OWNSHP) {
        //_OWNSHP = _newMarObj("ownshp", S52_POINT, 1, xyz, attval);
        _OWNSHP = _newMarObj("ownshp", S52_POINT, 1, NULL, attval);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return _OWNSHP;
}

DLL S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d)
{
    S52_CHECK_MUTX_INIT;

    PRINTF("objH:%u, a:%f, b:%f, c:%f, d:%f\n", objH, a, b, c, d);

    //  shpbrd: Ship's breadth (beam),
    //  shplen: Ship's length over all,
    //  headng: Heading,
    //  cogcrs: Course over ground,
    //  sogspd: Speed over ground,
    //  ctwcrs: Course through water,
    //  stwspd: Speed through water,

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        objH = FALSE;
        goto exit;
    }

    if (TRUE==_isObjNameValid(obj, "ownshp") || TRUE==_isObjNameValid(obj, "vessel")) {
        double shplen = a+b;
        double shpbrd = c+d;
        double shp_off_y = shplen / 2.0 - b;
        double shp_off_x = shpbrd / 2.0 - c;

        char attval[80];
        SNPRINTF(attval, 80, "shpbrd:%f,shplen:%f,_shp_off_x:%f,_shp_off_y:%f",
                 shpbrd, shplen, shp_off_x, shp_off_y);

        S57_geo *geo = S52_PL_getGeo(obj);
        _setAtt(geo, attval);

    } else {
        PRINTF("WARNING: only OWNSHP and VESSEL can use this call \n");
        objH = FALSE;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL S52ObjectHandle STD S52_setVector(S52ObjectHandle objH, int vecstb, double course, double speed)
{
    S52_CHECK_MUTX_INIT;

    // debug
    PRINTF("objH:%u, vecstb:%i, course:%f, speed:%f\n", objH, vecstb, course, speed);
    course = _validate_deg(course);
    // FIXME: validate float
    //speed  = _validate_int(speed);
    if (vecstb<0 || 3<=vecstb) {
        PRINTF("WARNING: 'vecstb' out of bound, reset to none (0)\n");
        vecstb = 0;
    }

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        PRINTF("WARNING: invalid S52ObjectHandle objH\n");
        objH = FALSE;
        goto exit;
    }

    if (TRUE==_isObjNameValid(obj, "ownshp") || TRUE==_isObjNameValid(obj, "vessel")) {
        char   attval[80];
        SNPRINTF(attval, 80, "vecstb:%i,cogcrs:%f,sogspd:%f,ctwcrs:%f,stwspd:%f", vecstb, course, speed, course, speed);

        S57_geo *geo = S52_PL_getGeo(obj);
        _setAtt(geo, attval);

    } else {
        PRINTF("WARNING: can't setVector on this object (%u)\n", obj);
        objH = FALSE;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    // debug
    //PRINTF("fini\n");

    return objH;
}

DLL S52ObjectHandle STD S52_newPASTRK(int catpst, unsigned int maxpts)
{
    S52ObjectHandle pastrk = FALSE;

    S52_CHECK_MUTX_INIT;

    if (0 == maxpts) {
        PRINTF("WARNING: maxpts == 0, call fail\n");
        g_assert(0);
        goto exit;
    }

    // FIXME: validate catpst
    PRINTF("catpst:%i\n", catpst);

    char attval[80];
    SNPRINTF(attval, 80, "catpst:%i", catpst);

    pastrk = _newMarObj("pastrk", S52_LINES, maxpts, NULL, attval);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return pastrk;
}

static S52_obj            *_updateGeo(S52_obj *obj, double *xyz)
// update geo
{
    if (NULL != xyz) {
        guint    npt = 0;
        double  *ppt = NULL;
        S57_geo *geo = S52_PL_getGeo(obj);
        S57_getGeoData(geo, 0, &npt, &ppt);

        for (guint i=0; i<(npt*3); ++i)
            *ppt++ = *xyz++;
    }

    return obj;
}

static S52_obj            *_setPointPosition(S52_obj *obj, double latitude, double longitude, double heading)
{
    double xyz[3] = {longitude, latitude, 0.0};

    // update extent
    S57_geo *geo = S52_PL_getGeo(obj);
    _setExt(geo, 1, xyz);

    if (FALSE == S57_geo2prj3dv(1, xyz)) {
        return FALSE;
    }

    _updateGeo(obj, xyz);

    // reset timer for AIS
    if (0 == g_strcmp0("vessel", S57_getName(geo))) {
        S52_PL_setTimeNow(obj);
        // set 'orient' obj var directly rather then read it from attribut
        S52_PL_setSYorient(obj, heading);

        {
            char   attval[80];
            SNPRINTF(attval, 80, "headng:%f", heading);
            _setAtt(geo, attval);
        }
    }

    /* FIXME: check Guard Zone
    if (0 == g_strcmp0("ownshp", S57_getName(geo))) {
    }
    */

    return obj;
}

DLL S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data)
// FIXME: if ownshp check alarm
{
    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr()) {
        objH = FALSE;
        goto exit;
    }

    // clutter
    //PRINTF("objH:%u, latitude:%f, longitude:%f, data:%f\n", objH, latitude, longitude, data);

    latitude  = _validate_lat(latitude);
    longitude = _validate_lon(longitude);
    //time      = _validate_min(time);

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        objH = FALSE;
        goto exit;
    }

    S57_geo *geo = S52_PL_getGeo(obj);

    // POINT
    if (S57_POINT_T == S57_getObjtype(geo)) {
        _setPointPosition(obj, latitude, longitude, data);

        /* experimental: display cursor lat/lng
        if (0 == g_strcmp0("cursor", S57_getName(geo))) {
            char attval[80] = {'\0'};
            SNPRINTF(attval, 80, "_cursor_label:%f%c %f%c", fabs(latitude), (latitude<0)? 'S':'N', fabs(longitude), (longitude<0)? 'W':'E');
            _setAtt(geo, attval);
            S52_PL_resetParseText(obj);
        }
        */
    }
    else // LINE AREA
    {
        guint   sz  = S57_getGeoSize(geo);
        guint   npt = 0;
        double *ppt = NULL;
        S57_getGeoData(geo, 0, &npt, &ppt);

        double xyz[3] = {longitude, latitude, 0.0};
        if (FALSE == S57_geo2prj3dv(1, xyz)) {
            PRINTF("WARNING: S57_geo2prj3dv() fail\n");
            objH = FALSE;
            goto exit;
        }

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

        //* ajuste extent - use for culling
        // FIXME: LINES 'pastrk' and 'afgves' have extent that allway grow
        // but pushPos is stack base, so the extent should be ajusted accordingly
        if (0 == sz) {
            // first pos set extent directly
            S57_setExt(geo, longitude, latitude, longitude, latitude);
        } else {
            ObjExt_t ext = S57_getExt(geo);
            double xyz[3*3] = {longitude, latitude, 0.0, ext.W, ext.S, 0.0, ext.E, ext.N, 0.0};

            _setExt(geo, 3, xyz);
        }
        //*/

#ifdef S52_USE_AFGLOW
        // update time for afterglow LINE
        if (0 == g_strcmp0("afgves", S57_getName(geo))) {
            S52_PL_setTimeNow(obj);
        }
#endif

    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    //PRINTF("-3- objH:%u, latitude:%f, longitude:%f, data:%f\n", objH, latitude, longitude, data);

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
    S52ObjectHandle vessel = FALSE;

    S52_CHECK_MUTX_INIT;

    PRINTF("vesrce:%i, label:%s\n", vesrce, (NULL==label) ? "":label);

    // vescre: Vessel report source, 1 ARPA target, 2 AIS vessel report, 3 VTS report
    if (1!=vesrce && 2!=vesrce && 3!=vesrce) {
        PRINTF("WARNING: 'vescre' must be 1 or 2 or 3 .. reset to 2 (AIS)\n");
        vesrce = 2;
    }

    {
        char   attval[80];
        //double xyz[3] = {0.0, 0.0, 0.0};

        if (NULL == label) {
            SNPRINTF(attval, 80, "vesrce:%i,_vessel_label: ", vesrce);
        } else {
            // move code in _setAtt()
            /* FIXME: do not replace '\n' by ' ', for JSON
             if (0 == g_strcmp0("_vessel_label", attName)) {
             for (guint i=0; i<attList->len; ++i) {
             if ('\n' == attList->str[i]) {
             attList->str[i] = ' ';
             //g_string_insert_c(attList, i, SLASH);
             return;
             }
             }
             }
             */
            SNPRINTF(attval, 80, "vesrce:%i,_vessel_label:%s", vesrce, label);
        }

        //vessel = _newMarObj("vessel", S52_POINT, 1, xyz, attval);
        vessel = _newMarObj("vessel", S52_POINT, 1, NULL, attval);
    }

    //PRINTF("objH:%u\n", vessel);

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return vessel;
}

DLL S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel)
{
    S52_CHECK_MUTX_INIT;

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        objH = FALSE;
        goto exit;
    }

    // clutter
    //PRINTF("newLabel:%s\n", newLabel);

    if (TRUE==_isObjNameValid(obj, "ownshp") || TRUE==_isObjNameValid(obj, "vessel")) {
        char attval[80] = {'\0'};
        S57_geo *geo = S52_PL_getGeo(obj);

        // move code in _setAtt()
        /* FIXME: do not replace '\n' by ' ', for JSON
         if (0 == g_strcmp0("_vessel_label", attName)) {
         for (guint i=0; i<attList->len; ++i) {
         if ('\n' == attList->str[i]) {
         attList->str[i] = ' ';
         //g_string_insert_c(attList, i, SLASH);
         return;
         }
         }
         }
         */
        SNPRINTF(attval, 80, "[_vessel_label,%s]", newLabel);
        _setAtt(geo, attval);

        S52_PL_resetParseText(obj);
    } else {
        PRINTF("WARNING: not a 'ownshp' or 'vessel' object\n");
        objH = FALSE;
        goto exit;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;

}

DLL S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn)
{
    S52_CHECK_MUTX_INIT;

    PRINTF("vesselSelect:%i, vestat:%i, vesselTurn:%i\n", vesselSelect, vestat, vesselTurn);

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        objH = FALSE;
        goto exit;
    }

    if (TRUE==_isObjNameValid(obj, "ownshp") || TRUE==_isObjNameValid(obj, "vessel")) {
        char  attval[80] = {'\0'};
        char *attvaltmp  = attval;
        S57_geo *geo = S52_PL_getGeo(obj);

        // validate vesselSelect:
        if (0!=vesselSelect && 1!=vesselSelect && 2!=vesselSelect) {
            PRINTF("WARNING: 'vesselSelect' must be 0, 1 or 2 .. reset to 1 (selected)\n");
            vesselSelect = 1;
        }
        if (1 == vesselSelect) {
            SNPRINTF(attvaltmp, 80, "_vessel_select:%c,", 'Y');
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
            SNPRINTF(attvaltmp, 80, "_vessel_select:%c,", 'N');
            _SELECTED = FALSE;  // NULL
        }


        // validate vestat (Vessel Status): 1 AIS active, 2 AIS sleeping, 3 AIS close quarter (red)
        if (0!=vestat && 1!=vestat && 2!=vestat && 3!=vestat) {
            PRINTF("WARNING: 'vestat' must be 0, 1, 2 or 3.. reset to 1\n");
            vestat = 1;
        }

        int offset = strlen(attvaltmp);
        if (1==vestat || 2==vestat || 3==vestat ) {
            SNPRINTF(attvaltmp+offset, 80-offset, "vestat:%i,", vestat);
            // FIXME: _doAPP_CS to get the new text (and prio)
        }

        offset = strlen(attvaltmp);
        SNPRINTF(attvaltmp+offset, 80-offset, "_vessel_turn:%i", vesselTurn);

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

DLL S52ObjectHandle STD S52_newVRMEBL(int vrm, int ebl, int normalLineStyle, int setOrigin)
{
    S52ObjectHandle vrmebl = FALSE;

    S52_CHECK_MUTX_INIT;

    PRINTF("vrm:%i, ebl:%i, normalLineStyle:%i, setOrigin:%i\n", vrm, ebl, normalLineStyle, setOrigin);

    char attval[80];
    if (TRUE == setOrigin) {
        // initialy when user set origine
        SNPRINTF(attval, 80, "%s%c,%s%c,%s",
                "_normallinestyle:", ((TRUE == normalLineStyle)    ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl) ? 'Y' : 'N'),
                "_setOrigin:Init"
               );
    } else {
        SNPRINTF(attval, 80, "%s%c,%s%c,%s",
                "_normallinestyle:", ((TRUE == normalLineStyle)    ? 'Y' : 'N'),
                "_symbrngmrk:",      ((TRUE == vrm && TRUE == ebl) ? 'Y' : 'N'),
                "_setOrigin:N"
               );
    }

    if (FALSE==vrm && FALSE==ebl) {
        PRINTF("WARNING: nothing to do\n");
        goto exit;
    }

    if (TRUE == ebl) {
        vrmebl = _newMarObj("ebline", S52_LINES, 2, NULL, attval);
    } else {
        vrmebl = _newMarObj("vrmark", S52_LINES, 2, NULL, attval);
    }

    {   // set VRMEBL extent to INFINITY
        double xyz[6] = {-INFINITY, -INFINITY, 0.0, INFINITY, INFINITY, 0.0};

        S52_obj *obj = S52_PL_isObjValid(vrmebl);
        S57_geo *geo = S52_PL_getGeo(obj);
        _setExt(geo, 2, xyz);
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return vrmebl;
}

DLL S52ObjectHandle STD S52_setVRMEBL(S52ObjectHandle objH, double pixels_x, double pixels_y, double *brg, double *rge)
{
    S52_CHECK_MUTX_INIT;

    if (NULL == S57_getPrjStr()) {
        objH = FALSE;
        goto exit;
    }

    S52_obj *obj = S52_PL_isObjValid(objH);
    if (NULL == obj) {
        objH = FALSE;
        goto exit;
    }

    if (TRUE!=_isObjNameValid(obj, "ebline") && TRUE!=_isObjNameValid(obj, "vrmark")) {
        PRINTF("WARNING: not a 'ebline' or 'vrmark' object\n");
        objH = FALSE;
        goto exit;
    }

    double latA = 0.0;
    double lonA = 0.0;
    double latB = pixels_y;
    double lonB = pixels_x;
    if (FALSE == S52_GL_win2prj(&lonB, &latB)) {
        PRINTF("WARNING: S52_GL_win2prj() failed\n");
        objH = FALSE;
        goto exit;
    }

    guint    npt = 0;
    double  *ppt = NULL;
    S57_geo *geo = S52_PL_getGeo(obj);
    S57_getGeoData(geo, 0, &npt, &ppt);

    GString *setOriginstr = S57_getAttVal(geo, "_setOrigin");
    char     c            = *setOriginstr->str;

    switch (c) {
    case 'I':    // Init freely moveable origin
        lonA = lonB;
        latA = latB;
        _setAtt(geo, "_setOrigin:Y"); // FIXME: does setOriginstr->str become dandling ??
        break;
    case 'Y':    // user set freely moveable origin
        lonA = ppt[0];
        latA = ppt[1];
        break;
    case 'N':    // _OWNSHP origin (FIXME: apply ownshp offset set by S52_setDimension())
        if (FALSE != _OWNSHP) {
            guint    npt = 0;
            double  *ppt = NULL;
            S52_obj *obj = S52_PL_isObjValid(_OWNSHP);
            if (NULL == obj)
                break;
            S57_geo *geo = S52_PL_getGeo(obj);
            S57_getGeoData(geo, 0, &npt, &ppt);
            lonA = ppt[0];
            latA = ppt[1];
        } else {
            // FIXME: get the real value
            double cLat, cLon, rNM, north;
            S52_getView(&cLat, &cLon, &rNM, &north);
            lonA = cLon;
            latA = cLat;
            //lonA = _view.cLon;
            //latA = _view.cLat;
        }
        break;
    }

    {
        double xyz[6] = {lonA, latA, 0.0, lonB, latB, 0.0};
        double dist   = sqrt(pow(xyz[3]-xyz[0], 2) + pow(xyz[4]-xyz[1], 2));
        double deg    = ATAN2TODEG(xyz);
        char   unit   = 'm';
        char attval[80] = {'\0'};

        _updateGeo(obj, xyz);

        // in Nautical Mile if > 1852m (1NM)
        if (dist >  1852) {
            dist /= 1852;
            unit  = 'M';
        }

        if (deg < 0)
            deg += 360;

        SNPRINTF(attval, 80, "_vrmebl_label:%.1f deg / %.1f%c", deg, dist, unit);
        _setAtt(geo, attval);
        S52_PL_resetParseText(obj);

        if (NULL != brg) *brg = deg;
        if (NULL != rge) *rge = dist;
    }

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return objH;
}

DLL int             STD S52_newCSYMB(void)
{
    int ret = FALSE;

    S52_CHECK_MUTX_INIT;

    const char *attval = NULL;
    //double      pos[3] = {0.0, 0.0, 0.0};

    if (FALSE == _iniCSYMB) {
        PRINTF("WARNING: CSYMB fail, allready initialize\n");
        goto exit;
    }

    // FIXME: should it be global ?
    attval = "$SCODE:SCALEB10";     // 1NM
    //_SCALEB10 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _SCALEB10 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    attval = "$SCODE:SCALEB11";     // 10NM
    //_SCALEB11 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _SCALEB11 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    attval = "$SCODE:NORTHAR1";
    //_NORTHAR1 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _NORTHAR1 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    attval = "$SCODE:UNITMTR1";
    //_UNITMTR1 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _UNITMTR1 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    // all depth in S57 sould be in meter so this is not used
    //attval = "$SCODEUNITFTH1";
    //csymb  = S52_newObj("$CSYMB", S52_POINT_T, 1, pos, attval);


    //--- those symb are used for calibration ---

    // check symbol should be 5mm by 5mm
    attval = "$SCODE:CHKSYM01";
    //_CHKSYM01 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _CHKSYM01 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    // symbol to be used for checking and adjusting the brightness and contrast controls
    attval = "$SCODE:BLKADJ01";
    //_BLKADJ01 = _newMarObj("$CSYMB", S52_POINT, 1, pos, attval);
    _BLKADJ01 = _newMarObj("$CSYMB", S52_POINT, 1, NULL, attval);

    _iniCSYMB = FALSE;
    ret       = TRUE;

exit:

    GMUTEXUNLOCK(&_mp_mutex);

    return ret;
}
