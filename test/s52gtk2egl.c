// S52gtk2egl.c: simple S52 driver using EGL & GTK2.
//
// SD 2014FEB10

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2015 Sylvain Duclos sduclos@users.sourceforge.net

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


#include <EGL/egl.h>

#include <gtk/gtk.h>

#ifdef _MINGW
#include <gdk/gdkwin32.h>
#else
#include <gdk/gdkx.h>
#endif

#include <gdk/gdkkeysyms.h>  // GDK_*

#include "S52.h"

#ifdef USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif

#define PATH "/home/sduclos/dev/gis/data"
#define PLIB "PLAUX_00.DAI"
#define COLS "plib_COLS-3.4-a.rle"


// test - St-Laurent Ice Route
static S52ObjectHandle _waypnt1 = FALSE;
static S52ObjectHandle _waypnt2 = FALSE;
static S52ObjectHandle _waypnt3 = FALSE;
static S52ObjectHandle _waypnt4 = FALSE;

static S52ObjectHandle _leglin1 = FALSE;
static S52ObjectHandle _leglin2 = FALSE;
static S52ObjectHandle _leglin3 = FALSE;

// test - VRMEBL
// S52 object name:"ebline"
//static int             _drawVRMEBLtxt = FALSE;
static S52ObjectHandle _vrmeblA = FALSE;

// test - cursor DISP 9 (instead of IHO PLib DISP 8)
// need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = FALSE;

// test - centroid
static S52ObjectHandle _prdare  = FALSE;

// FIXME: mutex this share data
typedef struct s52droid_state_t {

    // initial view
    double     cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} s52droid_state_t;

//
typedef struct s52engine {
    GtkWidget          *window;

    // EGL - android or X11 window
    EGLNativeWindowType eglWindow;         // GdkDrawable in GTK2 and GdkWindow in GTK3
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;

    // local
    int                 do_S52init;
    int                 do_S52draw;        // TRUE to call S52_draw()
    int                 do_S52drawLast;    // TRUE to call S52_drawLast() - S52_draw() was called at least once
    int                 do_S52setViewPort; // set in Android callback

    int32_t             width;
    int32_t             height;
    // Xoom - dpi = 160 (density)
    int32_t             dpi;            // = AConfiguration_getDensity(engine->app->config);
    int32_t             wmm;
    int32_t             hmm;

    GTimeVal            timeLastDraw;

    s52droid_state_t    state;
} s52engine;

static s52engine _engine;

//----------------------------------------------
//
// Common stuff
//

#define VESSELTURN_UNDEFINED 129

//------ FAKE AIS - DEBUG ----
// debug - no real AIS, then fake target
#ifdef USE_FAKE_AIS
static S52ObjectHandle _vessel_ais = FALSE;
#define VESSELLABEL "~~MV Non Such~~ "           // bug: last char will be trimmed
// test - ownshp
static S52ObjectHandle _ownshp     = FALSE;
#define OWNSHPLABEL "OWNSHP\\n220 deg / 6.0 kt"


#ifdef S52_USE_AFGLOW
#define MAX_AFGLOW_PT (12 * 20)   // 12 min @ 1 vessel pos per 5 sec
//#define MAX_AFGLOW_PT 10        // debug
static S52ObjectHandle _vessel_ais_afglow = FALSE;

#endif  // S52_USE_AFGLOW

#endif  // USE_FAKE_AIS
//-----------------------------


static int      _egl_init       (s52engine *engine)
{
    g_print("s52egl:_egl_init(): starting ..\n");

    if ((NULL!=engine->eglDisplay) && (EGL_NO_DISPLAY!=engine->eglDisplay)) {
        g_print("_egl_init(): EGL is already up .. skipped!\n");
        return FALSE;
    }

// EGL Error code -
// #define EGL_SUCCESS             0x3000
// #define EGL_NOT_INITIALIZED     0x3001
// #define EGL_BAD_ACCESS          0x3002
// #define EGL_BAD_ALLOC           0x3003
// #define EGL_BAD_ATTRIBUTE       0x3004
// #define EGL_BAD_CONFIG          0x3005
// #define EGL_BAD_CONTEXT         0x3006
// #define EGL_BAD_CURRENT_SURFACE 0x3007
// #define EGL_BAD_DISPLAY         0x3008
// #define EGL_BAD_MATCH           0x3009
// #define EGL_BAD_NATIVE_PIXMAP   0x300A
// #define EGL_BAD_NATIVE_WINDOW   0x300B
// #define EGL_BAD_PARAMETER       0x300C
// #define EGL_BAD_SURFACE         0x300D
// #define EGL_CONTEXT_LOST        0x300E


    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;

    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);
    if (EGL_TRUE != ret)
        g_print("eglBindAPI() failed. [0x%x]\n", eglGetError());

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (EGL_NO_DISPLAY == eglDisplay)
        g_print("eglGetDisplay() failed. [0x%x]\n", eglGetError());

    EGLint major = 2;
    EGLint minor = 0;
    if (EGL_FALSE == eglInitialize(eglDisplay, &major, &minor) || EGL_SUCCESS != eglGetError())
        g_print("eglInitialize() failed. [0x%x]\n", eglGetError());

    g_print("EGL Version   :%s\n", eglQueryString(eglDisplay, EGL_VERSION));
    g_print("EGL Vendor    :%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
    g_print("EGL Extensions:%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));

    // Here, the application chooses the configuration it desires. In this
    // sample, we have a very simplified selection process, where we pick
    // the first EGLConfig that matches our criteria
    //EGLint     tmp;
    //EGLConfig  eglConfig[320];
    EGLint     eglNumConfigs = 0;
    EGLConfig  eglConfig;

    //eglGetConfigs(eglDisplay, eglConfig, 320, &tmp);
    eglGetConfigs(eglDisplay, NULL, 0, &eglNumConfigs);
    g_print("eglNumConfigs = %i\n", eglNumConfigs);

    /*
    int i = 0;
    for (i = 0; i<eglNumConfigs; ++i) {
        EGLint samples = 0;
        //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLES, &samples))
        //    printf("eglGetConfigAttrib in loop for an EGL_SAMPLES fail at i = %i\n", i);
        if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLE_BUFFERS, &samples))
            printf("eglGetConfigAttrib in loop for an  EGL_SAMPLE_BUFFERS fail at i = %i\n", i);

        if (samples > 0)
            printf("sample found: %i\n", samples);

    }
    eglGetConfigs(eglDisplay, configs, num_config[0], num_config))
    */

    // Here specify the attributes of the desired configuration.
    // Below, we select an EGLConfig with at least 8 bits per color
    // component compatible with on-screen windows
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        EGL_NONE
    };

    if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs))
        g_print("eglChooseConfig() failed. [0x%x]\n", eglGetError());
    if (0 == eglNumConfigs)
        g_print("eglChooseConfig() eglNumConfigs no matching config [0x%x]\n", eglGetError());


#ifdef _MINGW
    GdkDrawable *drawable = (GdkDrawable *) GDK_WINDOW_HWND(gtk_widget_get_window(engine->window));
#else
    GdkDrawable *drawable = (GdkDrawable *) GDK_WINDOW_XID (gtk_widget_get_window(engine->window));
#endif

    // debug
    //g_print("DEBUG: eglDisplay = 0X%lX\n", (long)drawable);
    //g_print("DEBUG: eglConfig  = 0X%lX\n", (long)eglConfig);
    //g_print("DEBUG: drawable   = 0X%lX\n", (long)drawable);

    if (FALSE == drawable) {
        g_print("ERROR: EGLNativeWindowType is NULL (can't draw)\n");
        g_assert(0);
    }

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)drawable, NULL);
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError())
        g_print("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());

    // Then we can create the context and set it current:
    EGLint eglContextList[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, eglContextList);
    if (EGL_NO_CONTEXT == eglContext || EGL_SUCCESS != eglGetError())
        g_print("eglCreateContext() failed. [0x%x]\n", eglGetError());

    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        g_print("Unable to eglMakeCurrent()\n");

    engine->eglDisplay = eglDisplay;
    engine->eglContext = eglContext;
    engine->eglSurface = eglSurface;

    g_print("s52egl:_egl_init(): end ..\n");

    return TRUE;
}

static void     _egl_done       (s52engine *engine)
// Tear down the EGL context currently associated with the display.
{
    if (engine->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->eglDisplay, engine->eglContext);
        }
        if (engine->eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->eglDisplay, engine->eglSurface);
        }
        eglTerminate(engine->eglDisplay);
    }

    //engine->animating  = 0;
    engine->eglDisplay = EGL_NO_DISPLAY;
    engine->eglContext = EGL_NO_CONTEXT;
    engine->eglSurface = EGL_NO_SURFACE;

    return;
}

static void     _egl_beg        (s52engine *engine, const char *tag)
{
    (void)tag;

    // On Android, Blit x10 slower whitout
    if (EGL_FALSE == eglWaitGL()) {
        g_print("_egl_beg(): eglWaitGL() failed. [0x%x]\n", eglGetError());
        return;
    }

    if (EGL_FALSE == eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext)) {
        g_print("_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
    }

    return;
}

static void     _egl_end        (s52engine *engine)
{
    if (EGL_TRUE != eglSwapBuffers(engine->eglDisplay, engine->eglSurface)) {
        g_print("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        //return FALSE;
    }

    return;
}

static int      _s52_computeView(s52droid_state_t *state)
{
    double S,W,N,E;

    if (FALSE == S52_getCellExtent(NULL, &S, &W, &N, &E))
        return FALSE;

    state->cLat  =  (N + S) / 2.0;
    state->cLon  =  (E + W) / 2.0;
    state->rNM   = ((N - S) / 2.0) * 60.0;  // FIXME: pick dominan projected N-S or E-W
    state->north = 0.0;

    return TRUE;
}

#ifdef USE_FAKE_AIS
static int      _s52_setupVESSEL(s52droid_state_t *state)
{
    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _engine.state.cLat + 0.01, _engine.state.cLon - 0.02, 045.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_pushPosition(_vessel_ais, _engine.state.cLat - 0.02, _engine.state.cLon + 0.02, 045.0);
    //S52_pushPosition(_vessel_ais, state->cLat - 0.04, state->cLon + 0.04, 045.0);
    S52_pushPosition(_vessel_ais, state->cLat - 0.01, state->cLon + 0.01, 045.0);
    S52_setVector(_vessel_ais, 1, 060.0, 16.0);   // ground

    // (re) set label
    S52_setVESSELlabel(_vessel_ais, VESSELLABEL);
    int vesselSelect = 0;  // OFF
    int vestat       = 1;  // AIS active
    int vesselTurn   = VESSELTURN_UNDEFINED;
    S52_setVESSELstate(_vessel_ais, vesselSelect, vestat, vesselTurn);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_pushPosition(_vessel_ais, _engine.state.cLat - 0.02, _engine.state.cLon + 0.02, 045.0);

    // VTS (this will not draw anything!)
    //_vessel_vts = S52_newVESSEL(3, dummy);

#ifdef S52_USE_AFGLOW
    // afterglow
    _vessel_ais_afglow = S52_newMarObj("afgves", S52_LINES, MAX_AFGLOW_PT, NULL, NULL);
#endif

    return TRUE;
}

static int      _s52_setupOWNSHP(s52droid_state_t *state)
{
    _ownshp = S52_newOWNSHP(OWNSHPLABEL);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 0.0, 30.0);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 15.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 100.0, 0.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 0.0, 15.0, 0.0);
    _ownshp = S52_setDimension(_ownshp, 0.0, 100.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 1000.0, 50.0, 15.0, 15.0);

    S52_pushPosition(_ownshp, state->cLat - 0.02, state->cLon - 0.01, 180.0 + 045.0);

    S52_setVector(_ownshp, 0, 220.0, 6.0);  // ownship use S52_MAR_VECSTB

    return TRUE;
}
#endif  // USE_FAKE_AIS

static int      _s52_setupLEGLIN(void)
{
/*

http://www.marinfo.gc.ca/fr/Glaces/index.asp

SRCN04 CWIS 122100
Bulletin des glaces pour le fleuve et le golfe Saint-Laurent de Les Escoumins aux détroits de
Cabot et de Belle-Isle émis à 2100TUC dimanche 12 février 2012 par le Centre des glaces de
Québec de la Garde côtière canadienne.

Route recommandée no 01
De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4847N 06830W puis
point de changement CHARLIE: 4900N 06800W puis
point de changement DELTA:   4930N 06630W puis
point de changement ECHO:    4930N 06425W puis
point de changement FOXTROT: 4745N 06000W puis
route normale de navigation.

Route recommandée no 05
Émise à 1431UTC le 17 FEVRIER 2012
par le Centre des Glaces de Québec de la Garde côtière canadienne.

De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4930N 06630W puis
point de changement CHARLIE: 4945N 06450W puis
point de changement DELTA:   4730N 06000W puis
route normale de navigation.
*/
    typedef struct WPxyz_t {
        double x,y,z;
    } WPxyz_t;

    //*
    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-68.5,     48.78333, 0.0},  // WP2 - BRAVO
        {-68.0,     49.00,    0.0},  // WP3 - CHARLIE
        {-66.5,     49.5,     0.0}   // WP4 - DELTA
    };
    //*/
    /*
    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-66.5,     49.5,     0.0}   // WP2 - BRAVO
    };
    */
    char attVal1[] = "select:2,OBJNAM:ALPHA";    // waypoint on alternate planned route
    char attVal2[] = "select:2,OBJNAM:BRAVO";    // waypoint on alternate planned route
    char attVal3[] = "select:2,OBJNAM:CHARLIE";  // waypoint on alternate planned route
    char attVal4[] = "select:2,OBJNAM:DELTA";    // waypoint on alternate planned route

    _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[0], attVal1);
    _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[1], attVal2);
    _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[2], attVal3);
    _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[3], attVal4);

    double gz = S52_getMarinerParam(S52_MAR_GUARDZONE_BEAM);
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);  // trun off

#define ALT_RTE 2
    // select: alternate (2) legline for Ice Route 2012-02-12T21:00:00Z
    _leglin1 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[0].y, WPxyz[0].x, WPxyz[1].y, WPxyz[1].x, FALSE);
    _leglin2 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[1].y, WPxyz[1].x, WPxyz[2].y, WPxyz[2].x, _leglin1);
    _leglin3 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[2].y, WPxyz[2].x, WPxyz[3].y, WPxyz[3].x, _leglin2);
    //_leglin4  = S52_newLEGLIN(1, 0.0, 0.0, WPxyz[3].y, WPxyz[3].x, WPxyz[4].y, WPxyz[4].x, _leglin3);

    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, gz);  // trun on

    /*
    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_engine.state.cLon, _engine.state.cLat, 0.0,  _engine.state.cLon + 0.01, _engine.state.cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }
    */

    return TRUE;
}

static int      _s52_setupVRMEBL(s52droid_state_t *state)
{
    //char *attVal   = NULL;      // ordinary cursor
    char  attVal[] = "cursty:2,_cursor_label:0.0N 0.0W";  // open cursor
    double xyz[3] = {state->cLon, state->cLat, 0.0};
    int S52_VRMEBL_vrm = TRUE;
    int S52_VRMEBL_ebl = TRUE;
    int S52_VRMEBL_sty = TRUE;  // normalLineStyle
    int S52_VRMEBL_ori = TRUE;  // (user) setOrigin

    _cursor2 = S52_newMarObj("cursor", S52_POINT, 1, xyz, attVal);
    //int ret = S52_toggleObjClassOFF("cursor");
    //g_print("_s52_setupVRMEBL(): S52_toggleObjClassOFF('cursor'); ret=%i\n", ret);
    //int ret = S52_toggleObjClassON("cursor");
    //g_print("_s52_setupVRMEBL(): S52_toggleObjClassON('cursor'); ret=%i\n", ret);


    _vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    //S52_toggleObjClassON("cursor");  // suppression ON
    //S52_toggleObjClassON("ebline");
    //S52_toggleObjClassON("vrmark");

    // suppression ON
    S52_setS57ObjClassSupp("cursor", TRUE);
    S52_setS57ObjClassSupp("ebline", TRUE);
    S52_setS57ObjClassSupp("vrmark", TRUE);

    return TRUE;
}

static int      _s52_setupPRDARE(s52droid_state_t *state)
// test - centroid (PRDARE: wind farm)
{
    // AREA (CW: to center the text)
    double xyzArea[6*3]  = {
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
        state->cLon - 0.005, state->cLat + 0.004, 0.0,
        state->cLon - 0.010, state->cLat + 0.000, 0.0,
        state->cLon - 0.010, state->cLat + 0.005, 0.0,
        state->cLon + 0.000, state->cLat + 0.005, 0.0,
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
    };

    // PRDARE/WNDFRM51/CATPRA9
    char attVal[] = "CATPRA:9";
    _prdare = S52_newMarObj("PRDARE", S52_AREAS, 6, xyzArea,  attVal);

    return TRUE;
}

static int      _s52_init       (s52engine *engine)
{
    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        g_print("_init_S52(): no EGL display ..\n");
        return FALSE;
    }

    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

    // return constant value EGL_UNKNOWN (-1) with Mesa
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HORIZONTAL_RESOLUTION, &engine->wmm);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_VERTICAL_RESOLUTION,   &engine->hmm);

    {
        // FIXME: broken on some monitor
        GdkScreen    *screen   = NULL;
        gint         w,h;
        gint         wmm,hmm;

        screen = gdk_screen_get_default();
        w      = gdk_screen_get_width    (screen);
        h      = gdk_screen_get_height   (screen);
        wmm    = gdk_screen_get_width_mm (screen);
        hmm    = gdk_screen_get_height_mm(screen);

        //w   = 1280;
        //h   = 1024;
        //w   = engine->width;
        //h   = engine->height;
        //wmm = 376;
        //hmm = 301; // wrong
        //hmm = 307;

        //g_print("_init_S52(): start -1- ..\n");

        if (FALSE == S52_init(w, h, wmm, hmm, NULL)) {
            engine->do_S52init = FALSE;
            return FALSE;
        }

        //g_print("_init_S52(): start -2- ..\n");

        //S52_setViewPort(0, 0, w, h);

    }

    // can be called any time
    S52_version();

#ifdef S52_USE_EGL
    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, engine);
#endif

    // read cell location fron s52.cfg
    S52_loadCell(NULL, NULL);

    // Rimouski
    //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000", NULL);

    //S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000", NULL);
	//S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000", NULL);

    // Ice - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // Bathy - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307260_LOD2.merc.tif", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307250_LOD2.merc.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

    // load AIS select symb.
    //S52_loadPLib("plib-test-priv.rle");


#ifdef S52_USE_WORLD
    // World data
    if (TRUE == S52_loadCell(PATH "/0WORLD/--0WORLD.shp", NULL)) {
        //S52_setMarinerParam(S52_MAR_DISP_WORLD, 0.0);   // default
        S52_setMarinerParam(S52_MAR_DISP_WORLD, 1.0);     // show world
    }
#endif

    // debug - remove clutter from this symb in SELECT mode
    //S52_setS57ObjClassSupp("M_QUAL", TRUE);  // supress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // displaythe U pattern
    //S52_toggleObjClassON ("M_QUAL");           //  suppression ON
    //S52_toggleObjClassOFF("M_QUAL");         //  suppression OFF
    S52_setS57ObjClassSupp("M_QUAL", TRUE);


    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    // -- DEPTH COLOR ------------------------------------
    S52_setMarinerParam(S52_MAR_TWO_SHADES,      0.0);   // 0.0 --> 5 shades
    //S52_setMarinerParam(S52_MAR_TWO_SHADES,      1.0);   // 1.0 --> 2 shades

    // sounding color
    //S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    15.0);


    //S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  3.0);

    //S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 10.0);
    S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 5.0);

    //S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   11.0);
    S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   10.0);

    //S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 0.0);  // (default off)
    S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 1.0);  // ON
    // -- DEPTH COLOR ------------------------------------



    S52_setMarinerParam(S52_MAR_SHIPS_OUTLINE,   1.0);
    S52_setMarinerParam(S52_MAR_HEADNG_LINE,     1.0);
    S52_setMarinerParam(S52_MAR_BEAM_BRG_NM,     1.0);
    //S52_setMarinerParam(S52_MAR_FULL_SECTORS,    0.0);    // (default ON)

    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_BASE);    // always ON
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_STD);     // default
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_OTHER);
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_BASE | S52_MAR_DISP_CATEGORY_STD | S52_MAR_DISP_CATEGORY_OTHER);
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_STD | S52_MAR_DISP_CATEGORY_OTHER);
    S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_SELECT);

    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_NONE );
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_STD );   // default
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_OTHER);
    S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_SELECT);   // All Mariner (Standard(default) + Other)

    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   0.0);     // DAY (default)
    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   1.0);     // DAY DARK
    S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   5.0);     // DAY 60
    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   6.0);     // DUSK 60

    //S52_setMarinerParam(S52_MAR_SCAMIN,          1.0);   // ON (default)
    //S52_setMarinerParam(S52_MAR_SCAMIN,          0.0);   // debug OFF - show all

    // remove QUAPNT01 symbole (black diagonal and a '?')
    S52_setMarinerParam(S52_MAR_QUAPNT01,        0.0);   // off

    S52_setMarinerParam(S52_MAR_DISP_CALIB,      1.0);

    // --- TEXT ----------------------------------------------
    S52_setMarinerParam(S52_MAR_SHOW_TEXT,       1.0);
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       0.0);

    S52_setTextDisp(0, 100, TRUE);                      // show all text
    //S52_setTextDisp(0, 100, FALSE);                   // no text

    // cell's legend
    //S52_setMarinerParam(S52_MAR_DISP_LEGEND, 1.0);   // show
    S52_setMarinerParam(S52_MAR_DISP_LEGEND, 0.0);   // hide (default)
    // -------------------------------------------------------


    //S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 0.0);  // OFF
    //S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 1.0);  // ON (default)

    S52_setMarinerParam(S52_MAR_ANTIALIAS,       1.0);   // on
    //S52_setMarinerParam(S52_MAR_ANTIALIAS,       0.0);     // off

    //S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.3);
    //S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.3);

    // a delay of 0.0 to tell to not delete old AIS (default +600 sec old)
    //S52_setMarinerParam(S52_MAR_DISP_VESSEL_DELAY, 0.0);

    //S52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 0.0);  // off (default)
    S52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 1.0);  // on

    // debug - use for timing redering
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX);


    // if first start find where we are looking
    _s52_computeView(&engine->state);
    // then (re)position the 'camera'
    S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    S52_newCSYMB();

    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL

    _s52_setupVRMEBL(&engine->state);

    _s52_setupLEGLIN();

    _s52_setupPRDARE(&engine->state);

#ifdef USE_FAKE_AIS
    _s52_setupVESSEL(&engine->state);

    _s52_setupOWNSHP(&engine->state);
#endif


    engine->do_S52init        = FALSE;

    engine->do_S52draw        = TRUE;
    engine->do_S52drawLast    = TRUE;

    engine->do_S52setViewPort = FALSE;


    return EGL_TRUE;
}

static int      _s52_done       (s52engine *engine)
{
    (void)engine;

    S52_done();

    return TRUE;
}

#ifdef USE_FAKE_AIS
static int      _s52_updTimeTag (s52engine *engine)
{
    (void)engine;


    // fake one AIS
    if (NULL != _vessel_ais) {
        gchar         str[80];
        GTimeVal      now;
        static double hdg = 0.0;

        hdg = (hdg >= 359.0) ? 0.0 : hdg+1;  // fake rotating hdg

        g_get_current_time(&now);
        g_sprintf(str, "%s %lis", VESSELLABEL, now.tv_sec);
        S52_setVESSELlabel(_vessel_ais, str);
        S52_pushPosition(_vessel_ais, engine->state.cLat - 0.01, engine->state.cLon + 0.01, hdg);
        S52_setVector(_vessel_ais, 1, hdg, 16.0);   // ground

#ifdef S52_USE_AFGLOW
        // stay at the same place but fill internal S52 buffer - in the search for possible leak
        S52_pushPosition(_vessel_ais_afglow, engine->state.cLat, engine->state.cLon, 0.0);
#endif
    }


    return TRUE;
}
#endif

static int      _s52_draw_cb    (gpointer user_data)
// return TRUE for the signal to be called again
{
    s52engine *engine = (s52engine*)user_data;

    //g_print("s52egl:_s52_draw_cb(): begin .. \n");

    if (NULL == engine) {
        g_print("_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    // wait for libS52 to init - no use to go further - bailout
    if (TRUE == engine->do_S52init) {
        g_print("s52egl:_s52_draw_cb(): re-starting .. waiting for S52_init() to finish\n");
        goto exit;
    }

    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        g_print("_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    // no draw at all, the window is not visible
    if ((FALSE==engine->do_S52draw) && (FALSE==engine->do_S52drawLast)) {
        //g_print("s52egl:_s52_draw_cb(): nothing to draw (do_S52draw & do_S52drawLast FALSE)\n");
        goto exit;
    }

#if !defined(S52_USE_EGL)
    _egl_beg(engine, "test");
#endif

    // draw background
    if (TRUE == engine->do_S52draw) {
        if (TRUE == engine->do_S52setViewPort) {
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

            S52_setViewPort(0, 0, engine->width, engine->height);

            engine->do_S52setViewPort = FALSE;
        }

        S52_draw();
        engine->do_S52draw = FALSE;
    }

    // draw AIS
    if (TRUE == engine->do_S52drawLast) {

#ifdef USE_FAKE_AIS
        _s52_updTimeTag(engine);
#endif
        S52_drawLast();
    }

#if !defined(S52_USE_EGL)
    _egl_end(engine);
#endif


exit:

    // debug
    //g_print("s52egl:_s52_draw_cb(): end .. \n");

    return EGL_TRUE;
}

//static int      _s52_screenShot(void)
// debug - S57 obj ID of Becancour Cell (CA579016.000)
//{
//    static int takeScreenShot = TRUE;
//    if (TRUE == takeScreenShot) {
//        //S52_dumpS57IDPixels("test.png", 954, 200, 200); // waypnt
//        S52_dumpS57IDPixels("test.png", 556, 200, 200); // land
//    }
//    takeScreenShot = FALSE;
//
//    return EGL_TRUE;
//}


static gboolean _scroll  (GdkEventKey *event)
{
    // TODO: test blit
    /*
    switch(event->keyval) {
    case GDK_Left :
        _engine.state.cLon -= _engine.state.rNM/(60.0*10.0);
        S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);
        break;
        case GDK_Right: _engine.state.cLon += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_Up   : _engine.state.cLat += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_Down : _engine.state.cLat -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }
    */

    //*
    switch(event->keyval) {
        case GDK_Left : _engine.state.cLon -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_Right: _engine.state.cLon += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_Up   : _engine.state.cLat += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_Down : _engine.state.cLat -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }
    //*/

    return TRUE;
}

static gboolean _zoom    (GdkEventKey *event)
{
    //*
    switch(event->keyval) {
        // zoom in
    	case GDK_Page_Up  : _engine.state.rNM /= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        // zoom out
        case GDK_Page_Down: _engine.state.rNM *= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }
    //*/
    return TRUE;
}

static gboolean _rotation(GdkEventKey *event)
{
    //S52_getView(&_view);

    //*
    switch(event->keyval) {
        // -
        case GDK_minus:
            _engine.state.north += 1.0;
            if (360.0 < _engine.state.north)
                _engine.state.north -= 360.0;
            break;
        // +
        case GDK_equal:
        case GDK_plus :
            _engine.state.north -= 1.0;
            if (_engine.state.north < 0.0)
                _engine.state.north += 360.0;
            break;
    }
    //*/

    //S52_setView(&_view);
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    return TRUE;
}

static gboolean _toggle  (S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, !val);

    return TRUE;
}

static gboolean _meterInc(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, ++val);

    return TRUE;
}

static gboolean _meterDec(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, --val);

    return TRUE;
}

static gboolean _disp    (S52MarinerParameter paramName, const char disp)
{
    double val = (double) disp;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _cpal    (S52MarinerParameter paramName, double val)
{
    val = S52_getMarinerParam(paramName) + val;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _inc     (S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName) + 15.0;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _mmInc   (S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName) + 0.01;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _dumpParam()
{
    g_print("S52_MAR_SHOW_TEXT         t %4.1f\n", S52_getMarinerParam(S52_MAR_SHOW_TEXT));
    g_print("S52_MAR_TWO_SHADES        w %4.1f\n", S52_getMarinerParam(S52_MAR_TWO_SHADES));
    g_print("S52_MAR_SAFETY_CONTOUR    c %4.1f\n", S52_getMarinerParam(S52_MAR_SAFETY_CONTOUR));
    g_print("S52_MAR_SAFETY_DEPTH      d %4.1f\n", S52_getMarinerParam(S52_MAR_SAFETY_DEPTH));
    g_print("S52_MAR_SHALLOW_CONTOUR   a %4.1f\n", S52_getMarinerParam(S52_MAR_SHALLOW_CONTOUR));
    g_print("S52_MAR_DEEP_CONTOUR      e %4.1f\n", S52_getMarinerParam(S52_MAR_DEEP_CONTOUR));
    g_print("S52_MAR_SHALLOW_PATTERN   s %4.1f\n", S52_getMarinerParam(S52_MAR_SHALLOW_PATTERN));
    g_print("S52_MAR_SHIPS_OUTLINE     o %4.1f\n", S52_getMarinerParam(S52_MAR_SHIPS_OUTLINE));
    g_print("S52_MAR_DISTANCE_TAGS     f %4.1f\n", S52_getMarinerParam(S52_MAR_DISTANCE_TAGS));
    g_print("S52_MAR_TIME_TAGS         g %4.1f\n", S52_getMarinerParam(S52_MAR_TIME_TAGS));
    g_print("S52_MAR_BEAM_BRG_NM       y %4.1f\n", S52_getMarinerParam(S52_MAR_BEAM_BRG_NM));
    g_print("S52_MAR_FULL_SECTORS      l %4.1f\n", S52_getMarinerParam(S52_MAR_FULL_SECTORS));
    g_print("S52_MAR_SYMBOLIZED_BND    b %4.1f\n", S52_getMarinerParam(S52_MAR_SYMBOLIZED_BND));
    g_print("S52_MAR_SYMPLIFIED_PNT    p %4.1f\n", S52_getMarinerParam(S52_MAR_SYMPLIFIED_PNT));
    g_print("S52_MAR_DISP_CATEGORY   7-0 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_CATEGORY));
    g_print("S52_MAR_COLOR_PALETTE     k %4.1f\n", S52_getMarinerParam(S52_MAR_COLOR_PALETTE));
    g_print("S52_MAR_FONT_SOUNDG       n %4.1f\n", S52_getMarinerParam(S52_MAR_FONT_SOUNDG));
    g_print("S52_MAR_DATUM_OFFSET      m %4.1f\n", S52_getMarinerParam(S52_MAR_DATUM_OFFSET));
    g_print("S52_MAR_SCAMIN            u %4.1f\n", S52_getMarinerParam(S52_MAR_SCAMIN));
    g_print("S52_MAR_ANTIALIAS         i %4.1f\n", S52_getMarinerParam(S52_MAR_ANTIALIAS));
    g_print("S52_MAR_QUAPNT01          j %4.1f\n", S52_getMarinerParam(S52_MAR_QUAPNT01));
    g_print("S52_MAR_DISP_OVERLAP      z %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_OVERLAP));
    g_print("S52_MAR_DISP_LAYER_LAST   1 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_LAYER_LAST));
    g_print("S52_MAR_ROT_BUOY_LIGHT    2 %4.1f\n", S52_getMarinerParam(S52_MAR_ROT_BUOY_LIGHT));
    g_print("S52_MAR_DISP_CRSR_PICK    3 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_CRSR_PICK));
    g_print("S52_MAR_DISP_GRATICULE    4 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_GRATICULE));
    g_print("S52_MAR_HEADNG_LINE       5 %4.1f\n", S52_getMarinerParam(S52_MAR_HEADNG_LINE));
    g_print("S52_MAR_DISP_WHOLIN       6 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_WHOLIN));
    g_print("S52_MAR_DISP_LEGEND       3 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_LEGEND));
    g_print("S52_CMD_WRD_FILTER     F1-5 %4.1f\n", S52_getMarinerParam(S52_CMD_WRD_FILTER));
    g_print("S52_MAR_DOTPITCH_MM_X    F7 %4.2f\n", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_X));
    g_print("S52_MAR_DOTPITCH_MM_Y    F8 %4.2f\n", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_Y));
    g_print("S52_MAR_DISP_NODATA_LAYER F9 %4.2f\n", S52_getMarinerParam(S52_MAR_DISP_NODATA_LAYER));

    int crntVal = (int) S52_getMarinerParam(S52_CMD_WRD_FILTER);

    g_print("\tFilter State:\n");
    g_print("\tF1 - S52_CMD_WRD_FILTER_SY: %s\n", (S52_CMD_WRD_FILTER_SY & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF2 - S52_CMD_WRD_FILTER_LS: %s\n", (S52_CMD_WRD_FILTER_LS & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF3 - S52_CMD_WRD_FILTER_LC: %s\n", (S52_CMD_WRD_FILTER_LC & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF4 - S52_CMD_WRD_FILTER_AC: %s\n", (S52_CMD_WRD_FILTER_AC & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF5 - S52_CMD_WRD_FILTER_AP: %s\n", (S52_CMD_WRD_FILTER_AP & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF6 - S52_CMD_WRD_FILTER_TX: %s\n", (S52_CMD_WRD_FILTER_TX & crntVal) ? "TRUE" : "FALSE");

    return TRUE;
}
static gboolean configure_event(GtkWidget         *widget,
                                 GdkEventConfigure *event,
                                 gpointer           data)
{
    (void)widget;
    (void)event;
    (void)data;


    //g_print("_configure_event\n");

    // FIXME: find the new screen size
    //GtkAllocation allocation;
    //gtk_widget_get_allocation(GTK_WIDGET(widget), &allocation);
    //_engine.width  = allocation.width;
    //_engine.height = allocation.height;


    //gtk_widget_get_allocation(GTK_WIDGET(_engine.window), &allocation);
    //gtk_widget_size_allocate(GTK_WIDGET(_engine.window), &allocation);

    //GtkRequisition requisition;
    //gtk_widget_get_child_requisition(widget, &requisition);

    //*
    if (TRUE == _engine.do_S52init) {
        g_print("s52egl:configure_event()\n");
        _egl_init(&_engine);
        _s52_init(&_engine);
        _engine.do_S52init = FALSE;
    }

    _s52_computeView(&_engine.state);
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);
    S52_setViewPort(0, 0, _engine.width, _engine.height);

    _engine.do_S52draw = TRUE;
    //*/

    return TRUE;
}

static gboolean key_release_event(GtkWidget   *widget,
                                  GdkEventKey *event,
                                  gpointer     data)
{
    (void)widget;
    (void)data;

    //*
    switch(event->keyval) {
        case GDK_Left  :
        case GDK_Right :
        case GDK_Up    :
        case GDK_Down  :_scroll(event);            break;

        case GDK_equal :
        case GDK_plus  :
        case GDK_minus :_rotation(event);          break;

        case GDK_Page_Down:
        case GDK_Page_Up:_zoom(event);             break;


     //   case GDK_KEY_Escape:_resetView(&_engine.state);                break;
        case GDK_r     : //gtk_widget_draw(widget, NULL);
                              break;
     //   case GDK_KEY_h     :_doRenderHelp = !_doRenderHelp;
     //                   _usage("s52gtk2");
     //                   break;
        case GDK_v     :g_print("%s\n", S52_version());    break;
        case GDK_x     :_dumpParam();                      break;
        case GDK_q     :gtk_main_quit();                   break;

        case GDK_w     :_toggle(S52_MAR_TWO_SHADES);       break;
        case GDK_s     :_toggle(S52_MAR_SHALLOW_PATTERN);  break;
        case GDK_o     :_toggle(S52_MAR_SHIPS_OUTLINE);    break;
        case GDK_l     :_toggle(S52_MAR_FULL_SECTORS);     break;
        case GDK_b     :_toggle(S52_MAR_SYMBOLIZED_BND);   break;
        case GDK_p     :_toggle(S52_MAR_SYMPLIFIED_PNT);   break;
        case GDK_n     :_toggle(S52_MAR_FONT_SOUNDG);      break;
        case GDK_u     :_toggle(S52_MAR_SCAMIN);           break;
        case GDK_i     :_toggle(S52_MAR_ANTIALIAS);        break;
        case GDK_j     :_toggle(S52_MAR_QUAPNT01);         break;
        case GDK_z     :_toggle(S52_MAR_DISP_OVERLAP);     break;
        //case GDK_1     :_toggle(S52_MAR_DISP_LAYER_LAST);  break;
        case GDK_1     :_meterInc(S52_MAR_DISP_LAYER_LAST);break;
        case GDK_exclam:_meterDec(S52_MAR_DISP_LAYER_LAST);break;

        case GDK_2     :_inc(S52_MAR_ROT_BUOY_LIGHT);      break;

        case GDK_3     :_toggle(S52_MAR_DISP_CRSR_PICK);
                        _toggle(S52_MAR_DISP_LEGEND);
                        _toggle(S52_MAR_DISP_CALIB);
                        _toggle(S52_MAR_DISP_DRGARE_PATTERN);
                        break;

        case GDK_4     :_toggle(S52_MAR_DISP_GRATICULE);   break;
        case GDK_5     :_toggle(S52_MAR_HEADNG_LINE);      break;

        //case GDK_t     :_meterInc(S52_MAR_SHOW_TEXT);      break;
        //case GDK_T     :_meterDec(S52_MAR_SHOW_TEXT);      break;
        case GDK_t     :
        case GDK_T     :_toggle  (S52_MAR_SHOW_TEXT);      break;
        case GDK_c     :_meterInc(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_C     :_meterDec(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_d     :_meterInc(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_D     :_meterDec(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_a     :_meterInc(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_A     :_meterDec(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_e     :_meterInc(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_E     :_meterDec(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_f     :_meterInc(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_F     :_meterDec(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_g     :_meterInc(S52_MAR_TIME_TAGS);      break;
        case GDK_G     :_meterDec(S52_MAR_TIME_TAGS);      break;
        case GDK_y     :_meterInc(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_Y     :_meterDec(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_m     :_meterInc(S52_MAR_DATUM_OFFSET);   break;
        case GDK_M     :_meterDec(S52_MAR_DATUM_OFFSET);   break;

        //case GDK_7     :_disp(S52_MAR_DISP_CATEGORY, 'D'); break; // DISPLAYBASE
        //case GDK_8     :_disp(S52_MAR_DISP_CATEGORY, 'S'); break; // STANDARD
        //case GDK_9     :_disp(S52_MAR_DISP_CATEGORY, 'O'); break; // OTHER
        //case GDK_0     :_disp(S52_MAR_DISP_CATEGORY, 'A'); break; // OTHER (all)
        //case GDK_7     :_disp(S52_MAR_DISP_CATEGORY, 0);   break; // DISPLAYBASE
        //case GDK_8     :_disp(S52_MAR_DISP_CATEGORY, 1);   break; // STANDARD
        //case GDK_9     :_disp(S52_MAR_DISP_CATEGORY, 2);   break; // OTHER
        //case GDK_0     :_disp(S52_MAR_DISP_CATEGORY, 3);   break; // OTHER (all)
        case GDK_7     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_BASE);   break; // DISPLAYBASE
        case GDK_8     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_STD);    break; // STANDARD
        case GDK_9     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_OTHER);  break; // OTHER
        case GDK_0     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT); break; // OTHER (all)

        case GDK_k     :_cpal(S52_MAR_COLOR_PALETTE,  1.0);break;
        case GDK_K     :_cpal(S52_MAR_COLOR_PALETTE, -1.0);break;

        case GDK_6     :_meterInc(S52_MAR_DISP_WHOLIN);    break;
        case GDK_asciicircum:
        case GDK_question:
        case GDK_caret :_meterDec(S52_MAR_DISP_WHOLIN);    break;


        //case GDK_3     :_cpal("S52_MAR_COLOR_PALETTE", 2.0); break; // DAY_WHITEBACK
        //case GDK_4     :_cpal("S52_MAR_COLOR_PALETTE", 3.0); break; // DUSK
        //case GDK_5     :_cpal("S52_MAR_COLOR_PALETTE", 4.0); break; // NIGHT

        //case GDK_F1    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000"); break;
        //case GDK_F2    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000"); break;
        case GDK_F1    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY); break;
        case GDK_F2    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS); break;
        case GDK_F3    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC); break;
        case GDK_F4    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC); break;
        case GDK_F5    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP); break;
        case GDK_F6    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX); break;

        case GDK_F7    :_mmInc(S52_MAR_DOTPITCH_MM_X); break;
        case GDK_F8    :_mmInc(S52_MAR_DOTPITCH_MM_Y); break;

        case GDK_F9    :_toggle(S52_MAR_DISP_NODATA_LAYER); break;

        default:
            g_print("key: 0x%04x\n", event->keyval);
    }
    //*/

    // redraw
    _engine.do_S52draw = TRUE;

    return TRUE;
}

int main (int argc, char** argv)
{
    gtk_init(&argc, &argv);

    _engine.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(_engine.window), 800, 600);
    gtk_window_set_title(GTK_WINDOW(_engine.window), "OpenGL ES2 in GTK2 application");

    gtk_widget_show_all(_engine.window);

    // move to configure_event()
    //_egl_init(&_engine);
    //_s52_init(&_engine);

    gtk_widget_set_app_paintable     (_engine.window, TRUE );
    gtk_widget_set_double_buffered   (_engine.window, FALSE);
    gtk_widget_set_redraw_on_allocate(_engine.window, TRUE );

    g_signal_connect(G_OBJECT(_engine.window), "destroy",           G_CALLBACK(gtk_main_quit),      NULL);
    g_signal_connect(G_OBJECT(_engine.window), "key_release_event", G_CALLBACK(key_release_event), NULL);
    g_signal_connect(G_OBJECT(_engine.window), "configure_event",   G_CALLBACK(configure_event),   NULL);

    _engine.do_S52init = TRUE;
    g_timeout_add(500, _s52_draw_cb, &_engine); // 0.5 sec

    gtk_main();

    _s52_done(&_engine);
    _egl_done(&_engine);

    g_print("%s .. done\n", argv[0]);

    return 0;
}
