// S52egl.c: simple S52 driver using only EGL.
//
// SD 2011NOV08 - update

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2012 Sylvain Duclos sduclos@users.sourceforge.net

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



#include "S52.h"

#ifdef S52_USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif


#include <EGL/egl.h>      // as a side effect,
#include <EGL/eglext.h>   // X is included OR android stuff

#include <stdio.h>        // printf()
#include <stdlib.h>       // exit(0)

#include <glib.h>
#include <glib-object.h>  // signal
#include <glib/gprintf.h> // g_sprintf(), g_ascii_strtod(), g_strrstr()

//extern GMemVTable *glib_mem_profiler_table;

#ifdef S52_USE_ANDROID
#include <jni.h>
#include <errno.h>
#include <android/sensor.h>
#include <android/log.h>
#include <android/window.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
//#include <sys/types.h>

//ANDROID_LOG_VERBOSE,
//ANDROID_LOG_DEBUG,
//ANDROID_LOG_INFO,
//ANDROID_LOG_WARN,
//ANDROID_LOG_ERROR,
//ANDROID_LOG_FATAL,

#define  LOG_TAG    "s52droid"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define  g_print    g_message


//#include <glib-android/glib-android.h>  // g_android_init()

#define RAD_TO_DEG    57.29577951308232

//#define PATH     "/data/media"         // android 4.1
#define PATH     "/sdcard/s52droid"      // android 4.2
#define PLIB     PATH "/PLAUX_00.DAI"
#define COLS     PATH "/plib_COLS-3.4.1.rle"
#define GPS      PATH "/bin/sl4agps"
#define AIS      PATH "/bin/s52ais"
#define PID           ".pid"
#define ALLSTOP  PATH "/bin/run_allstop.sh"

#include <glibconfig.h>
#include <gio/gio.h>

static GStaticMutex _engine_mutex = G_STATIC_MUTEX_INIT;  // protect engine

#else   // S52_USE_ANDROID

#define  PATH "/home/sduclos/dev/gis/data"
#define  PLIB "PLAUX_00.DAI"
#define  COLS "plib_COLS-3.4.1.rle"
#define  LOGI(...)   g_print(__VA_ARGS__)
#define  LOGE(...)   g_print(__VA_ARGS__)

#endif  // S52_USE_ANDROID

// test - St-Laurent Ice Route
static S52ObjectHandle _waypnt1 = NULL;
static S52ObjectHandle _waypnt2 = NULL;
static S52ObjectHandle _waypnt3 = NULL;
static S52ObjectHandle _waypnt4 = NULL;

static S52ObjectHandle _leglin1 = NULL;
static S52ObjectHandle _leglin2 = NULL;
static S52ObjectHandle _leglin3 = NULL;

// test - VRMEBL
// S52 object name:"ebline"
static int             _drawVRMEBLtxt = FALSE;
static S52ObjectHandle _vrmeblA       = NULL;

// test - cursor DISP 9 (instead of IHO PLib DISP 8)
// need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = NULL;

// test - centroid
static S52ObjectHandle _prdare = NULL;


// FIXME: mutex this share data
typedef struct s52droid_state_t {
    // GLib stuff
    GMainLoop *main_loop;

    guint      s52_draw_sigID;
    gpointer   gobject;
    gulong     handler;

    int        do_S52init;

    // initial view
    double     cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} s52droid_state_t;

//
typedef struct s52engine {

#ifdef S52_USE_ANDROID
    struct android_app        *app;
           ASensorManager     *sensorManager;
    //const  ASensor            *accelerometerSensor;
    //const  ASensor            *lightSensor;
    const  ASensor            *gyroSensor;
           ASensorEventQueue  *sensorEventQueue;
           AAssetManager      *assetManager;
           AConfiguration     *config;

           int32_t             configBits;

    struct ANativeActivityCallbacks* callbacks;

           GSocketConnection  *connection;

#else  // EGL/X11
           Display            *dpy;
#endif

    // EGL - android or X11 window
    EGLNativeWindowType eglWindow;
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;

    //EGLClientBuffer     eglClientBuf;
    //EGLNativePixmapType eglPixmap;      // eglCopyBuffers()


    // local
    int                 do_S52draw;        // TRUE to call S52_draw()
    int                 do_S52drawLast;    // TRUE to call S52_drawLast() - S52_draw() was called at least once
    int                 do_S52setViewPort; // set in Android callback

    int32_t             orientation;       // 1=180, 2=090
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
#ifdef S52_USE_FAKE_AIS
static S52ObjectHandle _vessel_ais        = NULL;
#define VESSELLABEL "~~MV Non Such~~ "           // last char will be trimmed
// test - ownshp
static S52ObjectHandle _ownshp            = NULL;
#define OWNSHPLABEL "OWNSHP\\n220 deg / 6.0 kt"


#ifdef S52_USE_AFGLOW
#define MAX_AFGLOW_PT (12 * 20)   // 12 min @ 1 vessel pos per 5 sec
//#define MAX_AFGLOW_PT 10        // debug
static S52ObjectHandle _vessel_ais_afglow = NULL;

#endif

#endif
//-----------------------------


static int      _egl_init       (s52engine *engine)
{
    LOGI("s52egl:_egl_init(): starting ..\n");

    if ((NULL!=engine->eglDisplay) && (EGL_NO_DISPLAY!=engine->eglDisplay)) {
        LOGE("_egl_init(): EGL is already up .. skipped!\n");
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



    EGLDisplay eglDisplay;
    EGLSurface eglSurface;
    EGLContext eglContext;

    // Here specify the attributes of the desired configuration.
    // Below, we select an EGLConfig with at least 8 bits per color
    // component compatible with on-screen windows

#ifdef S52_USE_TEGRA2
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,        EGL_WINDOW_BIT,
        EGL_RED_SIZE,            8,
        EGL_GREEN_SIZE,          8,
        EGL_BLUE_SIZE,           8,

        // Tegra 2 CSAA (anti-aliase)
        EGL_RENDERABLE_TYPE,     4,  // EGL_OPENGL_ES2_BIT
        EGL_COVERAGE_BUFFERS_NV, 1,  // TRUE
        //EGL_COVERAGE_BUFFERS_NV, 0,
        //EGL_COVERAGE_SAMPLES_NV, 2,  // always 5 in practice on tegra 2

        EGL_NONE
    };
#else

#ifdef S52_USE_ADRENO
    EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,

        // Note: MSAA work on Andreno in: setting > developer > MSAA
        //EGL_SAMPLES,             1,  // fail on Adreno
        //EGL_SAMPLE_BUFFERS,      4,  // fail on Adreno

        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        //EGL_ALPHA_SIZE,         8,

        // this bit opens access to ES3 functions on QCOM hardware pre-Android support for ES3
        //EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES3_BIT_KHR,
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,

        EGL_NONE
    };
#else

    // Mesa
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

        //EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        //EGL_CONTEXT_CLIENT_VERSION,3,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        //EGL_RED_SIZE,        5,
        //EGL_GREEN_SIZE,      6,
        //EGL_BLUE_SIZE,       5,

        EGL_NONE
    };
#endif  // S52_USE_ADRENO
#endif

    /*
    const EGLint eglConfigList[] = {
        EGL_SAMPLES,             0,   // > 0, fail on xoom
        EGL_SAMPLE_BUFFERS,      1,   // 0 - MSAA fail (anti-aliassing)
        EGL_RED_SIZE,            5,   // exact
        EGL_GREEN_SIZE,          6,   // exact
        EGL_BLUE_SIZE,           5,   // exact
        EGL_ALPHA_SIZE,          0,   // exact
        //EGL_BUFFER_SIZE,        16,     // any
        //EGL_DEPTH_SIZE,         16,     // any
        EGL_STENCIL_SIZE,        0,
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,      // exact
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,  // exact
        EGL_NONE
    };
    //*/


    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);
    if (EGL_TRUE != ret)
        LOGE("eglBindAPI() failed. [0x%x]\n", eglGetError());

#ifdef S52_USE_ANDROID
    eglDisplay  = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    engine->dpy = XOpenDisplay(NULL);
    eglDisplay  = eglGetDisplay(engine->dpy);
#endif

    if (EGL_NO_DISPLAY == eglDisplay)
        LOGE("eglGetDisplay() failed. [0x%x]\n", eglGetError());

    //EGLint major = 2;
    EGLint major = 0;
    EGLint minor = 0;
    if (EGL_FALSE == eglInitialize(eglDisplay, &major, &minor))
        LOGE("eglInitialize() failed. [0x%x]\n", eglGetError());

    LOGI("EGL Version   :%s\n", eglQueryString(eglDisplay, EGL_VERSION));
    LOGI("EGL Vendor    :%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
    LOGI("EGL Extensions:%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));

    // Here, the application chooses the configuration it desires. In this
    // sample, we have a very simplified selection process, where we pick
    // the first EGLConfig that matches our criteria
    //EGLint     tmp;
    //EGLConfig  eglConfig[320];
    //EGLConfig  eglConfig[27];
    EGLConfig  eglConfig;
    EGLint     eglNumConfigs = 0;

    //eglGetConfigs(eglDisplay, eglConfig, 320, &tmp);
    eglGetConfigs(eglDisplay, NULL, 0, &eglNumConfigs);
    LOGI("eglNumConfigs = %i\n", eglNumConfigs);

    /*
    for (int i = 0; i<eglNumConfigs; ++i) {
        EGLint samples = 0;
        //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLES, &samples))
        //    LOGE(("eglGetConfigAttrib in loop for an EGL_SAMPLES fail at i = %i\n", i);
        if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLE_BUFFERS, &samples))
            LOGE(("eglGetConfigAttrib in loop for an  EGL_SAMPLE_BUFFERS fail at i = %i\n", i);

        if (samples > 0)
            LOGE(("sample found: %i\n", samples);

    }
    //*/

    if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs)) {
    //if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, eglConfig, 27, &eglNumConfigs))
        LOGI("eglChooseConfig() failed, exiting .. [0x%x]\n", eglGetError());
        exit(0);
    }
    if (0 == eglNumConfigs)
        LOGI("eglChooseConfig() eglNumConfigs no matching config [0x%x]\n", eglGetError());
    else
        LOGI("eglChooseConfig() eglNumConfigs = %i\n", eglNumConfigs);

    // EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
    // guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
    // As soon as we picked a EGLConfig, we can safely reconfigure the
    // ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    EGLint vid;
    if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vid))
    //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[5], EGL_NATIVE_VISUAL_ID, &vid))
        LOGE("Error: eglGetConfigAttrib() failed\n");

	

#ifdef S52_USE_ANDROID
    //ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, vid);
    ANativeWindow_setBuffersGeometry(engine->app->window,
                                     ANativeWindow_getWidth(engine->app->window),
                                     ANativeWindow_getHeight(engine->app->window),
                                     vid);
    engine->eglWindow = (EGLNativeWindowType) engine->app->window;
#else
    {
        XSetWindowAttributes wa;
        XSizeHints    sh;
        //XEvent        e;
        unsigned long mask   = CWBackPixel | CWBorderPixel | CWEventMask | CWColormap;
        long          screen = 0;
        XVisualInfo  *visual = NULL;
        XVisualInfo   tmplt;
        //Colormap      colormap;
        int           vID, n;
        Window        window;
        Display      *display = engine->dpy;
        char         *title   = "OpenGL ES 2.0 on a Linux Desktop";

        eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vID);
        tmplt.visualid = vID;
        visual = XGetVisualInfo(display, VisualIDMask, &tmplt, &n);

        screen = DefaultScreen(display);
        wa.colormap         = XCreateColormap(display, RootWindow(display, screen), visual->visual, AllocNone);
        wa.background_pixel = 0xFFFFFFFF;
        wa.border_pixel     = 0;
        wa.event_mask       = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask;

        window = XCreateWindow(display, RootWindow(display, screen), 0, 0, 1280, 1024,
                               0, visual->depth, InputOutput, visual->visual, mask, &wa);

        sh.flags = USPosition;
        sh.x = 0;
        sh.y = 0;
        XSetStandardProperties(display, window, title, title, None, 0, 0, &sh);
        XMapWindow(display, window);
        XSetWMColormapWindows(display, window, &window, 1);
        XFlush(display);

        engine->eglWindow = (EGLNativeWindowType) window;
    }
#endif

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)engine->eglWindow, NULL);
    //eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig[5], (EGLNativeWindowType)engine->eglWindow, NULL);
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError())
        LOGE("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());

    // Then we can create the context and set it current:
    EGLint eglContextList[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, eglContextList);
    //eglContext = eglCreateContext(eglDisplay, eglConfig[5], EGL_NO_CONTEXT, eglContextList);
    if (EGL_NO_CONTEXT == eglContext || EGL_SUCCESS != eglGetError())
        LOGE("eglCreateContext() failed. [0x%x]\n", eglGetError());

    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        LOGE("Unable to eglMakeCurrent()\n");

    engine->eglDisplay = eglDisplay;
    engine->eglContext = eglContext;
    engine->eglSurface = eglSurface;

    LOGI("s52egl:_egl_init(): end ..\n");

    return 1;
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

static int      _egl_beg        (s52engine *engine)
{
    // On Android, Blit x10 slower whitout
    if (EGL_FALSE == eglWaitGL()) {
        LOGE("_egl_beg(): eglWaitGL() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    if (EGL_FALSE == eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext)) {
        LOGE("_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    return TRUE;
}

static int      _egl_end        (s52engine *engine)
{
    if (EGL_TRUE != eglSwapBuffers(engine->eglDisplay, engine->eglSurface)) {
        LOGE("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    return TRUE;
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

#ifdef S52_USE_FAKE_AIS
static int      _s52_setupVESSEL(s52droid_state_t *state)
{
    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _view.cLat + 0.01, _view.cLon - 0.02, 045.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);
    //S52_pushPosition(_vessel_ais, state->cLat - 0.04, state->cLon + 0.04, 045.0);
    S52_pushPosition(_vessel_ais, state->cLat - 0.01, state->cLon + 0.01, 045.0);
    S52_setVector(_vessel_ais, 1, 060.0, 16.0);   // ground

    // (re) set label
    S52_setVESSELlabel(_vessel_ais, VESSELLABEL);
    int vesselSelect = 0;  // OFF
    int vestat       = 1;
    int vesselTurn   = VESSELTURN_UNDEFINED;
    S52_setVESSELstate(_vessel_ais, vesselSelect, vestat, vesselTurn);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);

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
#endif  // S52_USE_FAKE_AIS

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

#define ALT_RTE 2
    // select: alternate (2) legline for Ice Route 2012-02-12T21:00:00Z
    _leglin1 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[0].y, WPxyz[0].x, WPxyz[1].y, WPxyz[1].x, NULL);
    _leglin2 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[1].y, WPxyz[1].x, WPxyz[2].y, WPxyz[2].x, _leglin1);
    _leglin3 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[2].y, WPxyz[2].x, WPxyz[3].y, WPxyz[3].x, _leglin2);
    //_leglin4  = S52_newLEGLIN(1, 0.0, 0.0, WPxyz[3].y, WPxyz[3].x, WPxyz[4].y, WPxyz[4].x, _leglin3);

    //_route[0] = _leglin1;
    //_route[1] = _leglin2;
    //_route[2] = _leglin3;
    //_route[3] = _leglin3;
    //S52_setRoute(4, _route);

    /*
    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_view.cLon, _view.cLat, 0.0,  _view.cLon + 0.01, _view.cLat - 0.01, 0.0};
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
    //LOGE("_s52_setupVRMEBL(): S52_toggleObjClassOFF('cursor'); ret=%i\n", ret);
    //int ret = S52_toggleObjClassON("cursor");
    //LOGE("_s52_setupVRMEBL(): S52_toggleObjClassON('cursor'); ret=%i\n", ret);


    _vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    S52_toggleObjClassON("cursor");  // suppression ON
    S52_toggleObjClassON("ebline");
    S52_toggleObjClassON("vrmark");

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

static int      _s52_error_cb   (const char *err)
{
    LOGI("%s", err);
    return TRUE;
}

static int      _s52_init       (s52engine *engine)
{
    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        LOGE("_init_S52(): no EGL display ..\n");
        return FALSE;
    }

    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

    // return constant value EGL_UNKNOWN (-1) with Mesa
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HORIZONTAL_RESOLUTION, &engine->wmm);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_VERTICAL_RESOLUTION,   &engine->hmm);

    {
        int w   = 0;
        int h   = 0;
        int wmm = 0;
        int hmm = 0;

#ifdef S52_USE_ANDROID
        // Xoom: pixels_w: 1280, pixels_h: 752, mm_w: 203, mm_h: 101
        w   = engine->width;
        h   = engine->height;
        wmm = (int)(w / engine->dpi) * 25.4;  // inch to mm
        hmm = (int)(h / engine->dpi) * 25.4;
#else
        // Aspire 5542G - 15.6" HD 1366 x 768 pixel resolution
        // dual-screen: 2648 x 1024, 700 x 270 mm
        h   = XDisplayHeight  (engine->dpy, 0);
        hmm = XDisplayHeightMM(engine->dpy, 0);
        w   = XDisplayWidth   (engine->dpy, 0);
        wmm = XDisplayWidthMM (engine->dpy, 0);
        //w   = 1280;
        //h   = 1024;
        //w   = engine->width;
        //h   = engine->height;
        //wmm = 376;
        //hmm = 301; // wrong
        //hmm = 307;
#endif

        //if (FALSE == S52_init(w, h, wmm, hmm, _s52_error_cb)) {
        if (FALSE == S52_init(w, h, wmm, hmm, NULL)) {
            LOGE("_init_S52():S52_init(%i,%i,%i,%i)\n", w, h, wmm, hmm);
            engine->state.do_S52init = FALSE;
            //exit(0);
            return FALSE;
        }

        S52_setViewPort(0, 0, w, h);
    }


#ifdef S52_USE_ANDROID
    // Estuaire du St-Laurent
    //S52_loadCell(NULL, NULL);
    //S52_loadCell(PATH "/ENC_ROOT/CA279037.000", NULL);
    // Rimouski
    S52_loadCell(PATH "/ENC_ROOT/CA579041.000", NULL);
    // load all 3 S57 charts
    //S52_loadCell(PATH "/ENC_ROOT", NULL);

#else  // S52_USE_ANDROID

    // read cell location fron s52.cfg
    S52_loadCell(NULL, NULL);

    // Ice - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // Bathy - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307260_LOD2.merc.tif", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307250_LOD2.merc.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RASTER, 1.0);

    // load AIS select symb.
    //S52_loadPLib("plib-test-priv.rle");
#endif  // S52_USE_ANDROID

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
    S52_toggleObjClassON ("M_QUAL");           //  suppression ON
    //S52_toggleObjClassOFF("M_QUAL");         //  suppression OFF


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

    S52_setMarinerParam(S52_MAR_SCAMIN,          1.0);   // ON (default)
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

#ifdef S52_USE_ANDROID
    // trick to force symbole size (smaller on xoom so that proportion look the same
    // as a 'normal' screen - since the eye is closer to the 10" screen of the Xoom)
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.3);
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.3);
#endif

    // a delay of 0.0 to tell to not delete old AIS (default +600 sec old)
    S52_setMarinerParam(S52_MAR_DEL_VESSEL_DELAY, 0.0);

    // debug - use for timing redering
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX);

    //S52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 0.0); // debug: no NODATA layer
    S52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 1.0);   // default

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

#ifdef S52_USE_FAKE_AIS
    _s52_setupVESSEL(&engine->state);

    _s52_setupOWNSHP(&engine->state);
#endif

#ifdef S52_USE_EGL
    S52_setEGLcb((EGL_cb)_egl_beg, (EGL_cb)_egl_end, engine);
#endif

    engine->do_S52draw        = TRUE;
    engine->do_S52drawLast    = TRUE;

    engine->do_S52setViewPort = FALSE;

    engine->state.do_S52init  = FALSE;

    return EGL_TRUE;
}

static int      _s52_done       (s52engine *engine)
{
    (void)engine;

    S52_done();

    return TRUE;
}

#ifdef S52_USE_FAKE_AIS
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

static int      _s52_screenShot (void)
// debug - S57 obj ID of Becancour Cell (CA579016.000)
{
    static int takeScreenShot = TRUE;
    if (TRUE == takeScreenShot) {
        //S52_dumpS57IDPixels("test.png", 954, 200, 200); // waypnt
        //S52_dumpS57IDPixels("test.png", 556, 200, 200); // land
        S52_dumpS57IDPixels("/sdcard/s52droid/test.png", 0, 200, 200);
    }
    takeScreenShot = FALSE;

    return EGL_TRUE;
}


static int      _s52_draw_cb    (gpointer user_data)
// return TRUE for the signal to be called again
{
    struct s52engine *engine = (struct s52engine*)user_data;

    LOGI("s52egl:_s52_draw_cb(): begin .. \n");

    /*
    GTimeVal now;  // 2 glong (at least 32 bits each - but amd64 !?
    g_get_current_time(&now);
    if (0 == (now.tv_sec - engine->timeLastDraw.tv_sec))
        goto exit;
    //*/

    if (NULL == engine) {
        LOGE("_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        LOGE("_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    // wait for libS52 to init - no use to go further - bailout
    if (TRUE == engine->state.do_S52init) {
        LOGI("s52egl:_s52_draw_cb(): waiting for _s52_init() to finish\n");
        goto exit;
    }

    // no draw at all, the window is not visible
    if ((FALSE==engine->do_S52draw) && (FALSE==engine->do_S52drawLast)) {
        //LOGI("s52egl:_s52_draw_cb(): nothing to draw (do_S52draw & do_S52drawLast FALSE)\n");
        goto exit;
    }

#ifndef S52_USE_EGL
    _egl_beg(engine);
#endif

    // draw background
    if (TRUE == engine->do_S52draw) {
        if (TRUE == engine->do_S52setViewPort) {
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

#ifdef S52_USE_ADRENO
            // On Nexus 7, orientation doesn't change EGL w/h!
            if (1 == engine->orientation)
                S52_setViewPort(0, 0, engine->width,  engine->height);
            else
                S52_setViewPort(0, 0, engine->height, engine->width);
#else
            S52_setViewPort(0, 0, engine->width,  engine->height);
#endif
            engine->do_S52setViewPort = FALSE;
        }

        S52_draw();
        engine->do_S52draw = FALSE;

        //debug
        //_s52_screenShot();
    }

    // draw AIS
    if (TRUE == engine->do_S52drawLast) {

#ifdef S52_USE_FAKE_AIS
        _s52_updTimeTag(engine);
#endif
        S52_drawLast();
    }

#ifndef S52_USE_EGL
    _egl_end(engine);
#endif


exit:

    // debug
    //LOGI("s52egl:_s52_draw_cb(): end .. \n");

    return EGL_TRUE;
}


#ifdef S52_USE_ANDROID
//----------------------------------------------
//
// android specific code
//

//static int _androidUIon = FALSE;

static int      _android_init_external_gps(void)
// start sl4agps - get GPS & Gyro from Android
{
    GError *error = NULL;

    // tel GPS to re-connect to libS52 if GPS is allready UP
    if (TRUE == g_file_test(GPS PID, (GFileTest) (G_FILE_TEST_EXISTS))) {
        LOGI("s52egl:GPS prog is allready running (%s)\n", GPS);
        const char connS52[] = "/system/bin/sh -c 'kill -SIGUSR1 `cat " GPS PID "`'";
        if (TRUE != g_spawn_command_line_async(connS52, &error)) {
            LOGI("s52egl:g_spawn_command_line_async() failed [%s]\n", error->message);
            return FALSE;
        }
        LOGI("s52egl:OK re-connect %s\n", GPS);

        return TRUE;
    }

    char run_sl4agps_sh[] = "/system/bin/sh -c "  GPS;
    if (TRUE != g_spawn_command_line_async(run_sl4agps_sh, &error)) {
        LOGE("s52egl:g_spawn_command_line_async() failed [%s]\n", error->message);
        return FALSE;
    }
    LOGI("s52egl:sl4agps started\n");

    return TRUE;
}

static int      _android_init_external_ais(void)
// FIXME: this func is the same as _android_init_external_gps(), except SIGUSR
{
    GError *error = NULL;

    // tell AIS to re-connect to libS52 if AIS is allready UP
    if (TRUE == g_file_test(AIS PID, (GFileTest) (G_FILE_TEST_EXISTS))) {
        LOGI("s52egl:AIS prog is allready running (%s)\n", AIS);
        const char connS52[] = "su -c /system/bin/sh -c 'kill -SIGUSR2 `cat " AIS PID "`'";
        if (TRUE != g_spawn_command_line_async(connS52, &error)) {
            LOGI("s52egl:g_spawn_command_line_async() failed [%s]\n", error->message);
            return FALSE;
        }
        LOGI("s52egl:OK re-connected %s\n", AIS);

        return TRUE;
    }

    char run_s52ais_sh[] = "su -c \"/system/bin/sh -c " AIS "\"";
    if (TRUE != g_spawn_command_line_async(run_s52ais_sh, &error)) {
        LOGE("s52egl:g_spawn_command_line_async() failed [%s]\n", error->message);
        return FALSE;
    }
    LOGI("s52egl:s52ais started\n");

    return TRUE;
}

static int      _android_done_external_sensors(void)
{
    GError *error = NULL;
    char run_allstop_sh[] = "/system/bin/sh -c "  ALLSTOP ;
    // FIXME: can't run 'su' - premission problem
    // but SL4A need root to accept shutdown command
    // sl4agps start another instance (or something) that make
    // the UI real slow
    //char run_allstop_sh[] = "/system/bin/su -c "  ALLSTOP ;

    if (TRUE != g_spawn_command_line_async(run_allstop_sh, &error)) {
        LOGE("s52egl:g_spawn_command_line_async() failed [%s]\n", (NULL==error) ? "NULL" : error->message);
        return FALSE;
    }
    LOGI("s52egl:_android_done_external_sensors() .. done\n");

    return TRUE;
}

static int      _android_init_external_UI (s52engine *engine)
// start UI - get GPS & Gyro from Android
{
    const gchar cmd[] =
        "su -c \"                      "
        "sh /system/bin/am start       "
        "-a android.intent.action.MAIN "
        "-n nav.ecs.s52droid/.s52ui \" ";

    int ret = g_spawn_command_line_async(cmd, NULL);
    if (FALSE == ret) {
        g_print("_android_init_external_UI(): fail to start UI\n");
        return FALSE;
    } else {
        g_print("_android_init_external_UI(): UI started ..\n");
        engine->do_S52draw     = FALSE;
        engine->do_S52drawLast = FALSE;
    }

    return TRUE;
}


static int      _android_done_external_UI (s52engine *engine)
// FIXME: stop UI broken
{
    // this start the UI
    //const gchar cmd[] =
    //    "/system/bin/sh /system/bin/am start "
    //    "--activity-previous-is-top          "
    //    "-a android.intent.action.MAIN       "
    //    "-n nav.ecs.s52droid/.s52ui          ";

    const gchar cmd[] =
        "/system/bin/sh /system/bin/am broadcast "
        "-a nav.ecs.s52droid.s52ui.SHUTDOWN      ";

    int ret = g_spawn_command_line_async(cmd, NULL);
    if (FALSE == ret) {
        g_print("_android_done_external_UI(): fail to stop UI\n");
        return FALSE;
    } else {
        g_print("_android_done_external_UI(): UI stopped ..\n");
        engine->do_S52draw     = TRUE;
        engine->do_S52drawLast = TRUE;
    }

    return TRUE;
}


#if 0
static int      _android_sensors_gyro(gpointer user_data)
// Android Native poll event
{
    struct s52engine* engine = (struct s52engine*)user_data;

    //if (NULL != engine->gyroSensor) {
        int          ident;
        int          nEvent = 0;
        int          events;
        ASensorEvent event;
        struct       android_poll_source* source;
        double       gyro = -1.0;
        //double       pal = S52_getMarinerParam(S52_MAR_COLOR_PALETTE);

        //LOGI("s52egl:light: %f s52palette: %f\n", event.light, pal);

        //LOGI("Sensor Resolution: %f, MinDelay: %i\n",
        //     ASensor_getResolution(engine->lightSensor),
        //     ASensor_getMinDelay  (engine->lightSensor));
        //ident = ALooper_pollAll(0, NULL, &events, (void**)&source);
        //LOGI("_check_light():ident= %i\n", ident);

        //if (0 == ASensorEventQueue_hasEvents(engine->sensorEventQueue)) {
        //    LOGI("no gyro event\n");
        //    return FALSE;
        //}


        // Read all pending events (0 -> no wait)
        while (0 <= (ident = ALooper_pollAll(0, NULL, &events, (void**)&source))) {

            // Process this event.
            if (NULL != source)
                source->process(engine->app, source);

            // If a sensor has data, process it now.
            if (ident == LOOPER_ID_USER) {
                while (0 < ASensorEventQueue_getEvents(engine->sensorEventQueue, &event, 1)) {
                    // more event pending
                    ++nEvent;
                    gyro += event.vector.x;

                    /*
                    if (event.light>100.0 && pal==1.0) {
                        S52_setMarinerParam(S52_MAR_COLOR_PALETTE, 0.0);
                        engine->do_S52draw = TRUE;
                        pal = 0.0;
                    }
                    if (event.light<100.0 && pal==0.0) {
                        S52_setMarinerParam(S52_MAR_COLOR_PALETTE, 1.0);
                        engine->do_S52draw = TRUE;
                        pal = 1.0;
                    }
                    //*/
                }
                ++nEvent;
                //gyro += event.vector.x;
                //gyro = event.vector.x;
            }
        }

        /*
        //LOGI("nEvent: %i last light: %f s52palette: %f\n", nEvent, event.light, pal);
        //LOGI("nEvent:%i sensor:%i type:%i timestamp:%lli - gyro x:%f y:%f z:%f\n",
        //     nEvent, event.sensor, event.type, event.timestamp, event.vector.x, event.vector.y, event.vector.z);
        {
            int    vecstb  = 1; // overground
            double speed   = 60.0;
            //double azimuth = (gyro / nEvent) * RAD_TO_DEG;
            double azimuth = gyro * RAD_TO_DEG;
            //double pitch   = json_real_value(json_array_get(jorientArr, 1)) * RAD_TO_DEG;
            //double roll    = json_real_value(json_array_get(jorientArr, 2)) * RAD_TO_DEG;

            //LOGI("sensorsReadOrientation(): az=%f pitch=%f roll=%f", azimuth, pitch, roll);
            LOGI("sensorsReadOrientation(): az=%f gyro=%f nEvent=%i\n", azimuth, gyro, nEvent);

            //azimuth += 90.0;
            if (azimuth < 0.0)
                azimuth += 360.0;

            S52_setVector(_ownshp, vecstb, azimuth, speed);
        }
        */
    }


    return TRUE;
}

static int      _android_sensorsList_dump(ASensorManager *sensorManager)
{
/*
I/s52droid( 2683): 0 - sensor name: KXTF9 3-axis Accelerometer
I/s52droid( 2683): 1 - sensor name: Ambient Light sensor
I/s52droid( 2683): 2 - sensor name: AK8975 3-axis Magnetic field sensor
I/s52droid( 2683): 3 - sensor name: BMP085 Pressure sensor
I/s52droid( 2683): 4 - sensor name: L3G4200D Gyroscope sensor
I/s52droid( 2683): 5 - sensor name: Rotation Vector Sensor
I/s52droid( 2683): 6 - sensor name: Gravity Sensor
I/s52droid( 2683): 7 - sensor name: Linear Acceleration Sensor
I/s52droid( 2683): 8 - sensor name: Orientation Sensor
I/s52droid( 2683): 9 - sensor name: Corrected Gyroscope Sensor
*/
    int i = 0;
    ASensorList list;
    int ret = ASensorManager_getSensorList(sensorManager, &list);

    LOGI("number of sensors: %i\n", ret);
    for (i=0; i<ret; ++i) {
        ASensor const* sensor = list[i];
        LOGI("%i - sensor name: %s\n", i, ASensor_getName(sensor));
    }

    return TRUE;
}
#endif

static int      _android_display_init(s52engine *engine)
{
    engine->do_S52draw     = FALSE;
    engine->do_S52drawLast = FALSE;

    // is that even possible
    if (NULL == engine->app) {
        LOGI("s52egl:_android_display_init(): engine->app is NULL !!!\n");
        return FALSE;
    }

    _egl_init(engine);

    if (TRUE != _s52_init(engine)) {
        // land here if libS52 as been init before
        // then re-init only GLES2 part of libS52
        //extern int   S52_GL_init_GLES2(void);
        //S52_GL_init_GLES2();
    }
    //else {
    //    _android_init_external_UI(engine);
    //}

    engine->do_S52drawLast = TRUE;

    return EGL_TRUE;
}

static int      _android_render      (s52engine *engine, double new_y, double new_x, double new_z, double new_r)
{
    if (TRUE == S52_setView(new_y, new_x, new_z, new_r)) {
        engine->state.cLat  = new_y;
        engine->state.cLon  = new_x;
        engine->state.rNM   = new_z;
        engine->state.north = new_r;

#ifdef S52_USE_EGL
        S52_draw();
        S52_drawLast();
#else
        _egl_beg(engine);
        S52_draw();
        S52_drawLast();
        _egl_end(engine);
#endif
    }

    return TRUE;
}

static void     _android_config_dump(AConfiguration *config)
// the code is in android-git-master/development/ndk/sources/android/native_app_glue/android_native_app_glue.c:63
// static void print_cur_config(struct android_app* android_app)
{
    char lang[2], country[2];
    AConfiguration_getLanguage(config, lang);
    AConfiguration_getCountry (config, country);

    LOGI("Config: mcc=%d mnc=%d lang=%c%c cnt=%c%c orien=%d touch=%d dens=%d "
            "keys=%d nav=%d keysHid=%d navHid=%d sdk=%d screenSize=%d screenLong=%d "
            "modeType=%d modeNight=%d",
            AConfiguration_getMcc        (config),
            AConfiguration_getMnc        (config),
            lang[0], lang[1], country[0], country[1],
            AConfiguration_getOrientation(config),
            AConfiguration_getTouchscreen(config),
            AConfiguration_getDensity    (config),
            AConfiguration_getKeyboard   (config),
            AConfiguration_getNavigation (config),
            AConfiguration_getKeysHidden (config),
            AConfiguration_getNavHidden  (config),
            AConfiguration_getSdkVersion (config),
            AConfiguration_getScreenSize (config),
            AConfiguration_getScreenLong (config),
            AConfiguration_getUiModeType (config),
            AConfiguration_getUiModeNight(config));
}

static int      _android_motion_event(s52engine *engine, AInputEvent *event)
{
    static int    ticks           = 0;

    // FIXME: use enum for mode
    //static int    mode        = MODE_NONE;
    static int    mode_scroll     = FALSE;
    static int    mode_zoom       = FALSE;
    static int    mode_rot        = FALSE;
    static int    mode_vrmebl     = FALSE;
    static int    mode_vrmebl_set = FALSE;

    static double start_x         = 0.0;
    static double start_y         = 0.0;
    static double zoom_fac        = 0.0;

#define TICKS_PER_TAP  6
#define EDGE_X0       50   // 0 at left
#define EDGE_Y0       50   // 0 at top
#define DELTA          5


    int32_t actraw = AMotionEvent_getAction(event);
    int32_t action = actraw & AMOTION_EVENT_ACTION_MASK;

    switch (action) {

    case AMOTION_EVENT_ACTION_DOWN:
        ticks = 0;

        start_x = AMotionEvent_getX(event, 0);
        start_y = AMotionEvent_getY(event, 0);

        // stop _s52_draw_cb() from drawing
        engine->do_S52draw     = FALSE;
        engine->do_S52drawLast = FALSE;

        mode_zoom   = (start_x < EDGE_X0) ? TRUE : FALSE;
        mode_rot    = (start_y < EDGE_Y0) ? TRUE : FALSE;

        // corner case: (x,y) in corner then zoom mode is selected over rotation mode
        if ((TRUE==mode_zoom) && (TRUE==mode_rot))
            mode_rot = FALSE;

        mode_vrmebl     = FALSE;
        mode_vrmebl_set = FALSE;
        mode_scroll     = FALSE;

        break;

    case AMOTION_EVENT_ACTION_MOVE:
        // FIXME: do rendering outside main thread

        ++ticks;

        // long press
        if (ticks > TICKS_PER_TAP) {
            double new_x = AMotionEvent_getX(event, 0);
            double new_y = AMotionEvent_getY(event, 0);

            // no motion
            if ((ABS(start_x - new_x) < 5) && (ABS(start_y - new_y) < 5)) {
                // ensure that a minimum number of call to libS52
                // to turn vrmebl ON/OFF
                if (FALSE == mode_vrmebl_set) {
                    mode_vrmebl = (TRUE==mode_vrmebl) ? FALSE : TRUE;
                    if (TRUE == mode_vrmebl) {
                        S52_toggleObjClassOFF("cursor");
                        S52_toggleObjClassOFF("ebline");
                    } else {
                        S52_toggleObjClassON("cursor");
                        S52_toggleObjClassON("ebline");
                    }
                    mode_vrmebl_set = TRUE;
                }
            } else {
                // motion - find is scroll or vrmebl
                if (TRUE!=mode_vrmebl_set && FALSE==mode_zoom && FALSE==mode_zoom)
                    mode_scroll = TRUE;
            }

            // blit start
#ifndef S52_USE_EGL
            _egl_beg(engine);
#endif

            if (TRUE == mode_rot) {
                double north = engine->state.north + (90.0 * ((start_x - new_x) / engine->width));
                if (north <    0.0) north += 360.0;
                if (north >= 360.0) north -= 360.0;

                S52_drawBlit(0.0, 0.0, 0.0, north);
                //LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_MOVE: north=%f start_y=%f new_y=%f width=%i\n",
                //     north, start_y, new_y, engine->width);
            }

            if (TRUE == mode_zoom) {
                double dz_pc =  (start_y - new_y) / engine->height; // %
#ifdef S52_USE_ADRENO
                dz_pc /= 30.0;
#endif
                if (TRUE == S52_drawBlit(0.0, 0.0, dz_pc, 0.0))
                    zoom_fac = dz_pc;
            }

            if (TRUE == mode_vrmebl) {
                double brg = 0.0;
                double rge = 0.0;
                double pixels_y = engine->height - new_y;
                S52_setVRMEBL(_vrmeblA, new_x, pixels_y, &brg, &rge);

                // update cursor position (lon/lat)
                if (TRUE == S52_xy2LL(&new_x, &pixels_y)) {
                    S52_pushPosition(_cursor2, pixels_y, new_x, 0.0);
                    S52_drawLast();

                    //char str[80] = {'\0'};
                    //sprintf(str, "%05.1f° / %.1f m", brg, rge)
                    //S52_drawStr(new_x + 5, engine->height - new_y - 15, "CURSR", 1, str);
                }
            }

            if (TRUE==mode_scroll && FALSE==mode_zoom && FALSE==mode_rot && FALSE==mode_vrmebl) {
                double dx_pc =  (start_x - new_x) / engine->width;  // %
                double dy_pc = -(start_y - new_y) / engine->height; // %
#ifdef S52_USE_ADRENO
                dx_pc /= 10.0;
                dy_pc /= 10.0;
#endif

                S52_drawBlit(dx_pc, dy_pc, 0.0, 0.0);
            }

            // blit end
#ifndef S52_USE_EGL
            _egl_end(engine);
#endif
            return TRUE;
        }
        break;

    case AMOTION_EVENT_ACTION_UP: {
        double new_x  = AMotionEvent_getX(event, 0);
        double new_y  = AMotionEvent_getY(event, 0);
        double new_z  = 0.0;
        double new_r  = 0.0;

        S52_getView(&engine->state.cLat, &engine->state.cLon, &engine->state.rNM, &engine->state.north);

        // cursor pick
        if (ticks < TICKS_PER_TAP) {
            new_x = (new_x < 10.0) ? 10.0 : new_x;
            new_y = (new_y < 10.0) ? 10.0 : new_y;
            const char *nameid = S52_pickAt(new_x, engine->height - new_y);
            if (NULL != nameid) {
                unsigned int S57ID = atoi(nameid+7);
                LOGI("s52egl:_android_motion_event(): XY(%f, %f): NAME:ID=%s attList(%s)\n",
                     new_x, engine->height - new_y, nameid, S52_getAttList(S57ID));

                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
                new_z = engine->state.rNM;
                new_r = engine->state.north;

                _android_render(engine, new_y, new_x, new_z, new_r);

                engine->do_S52draw     = FALSE;
                engine->do_S52drawLast = TRUE;
            }
            return TRUE;
        }

        // long press
        if (ticks > TICKS_PER_TAP) {
            double new_x = AMotionEvent_getX(event, 0);
            double new_y = AMotionEvent_getY(event, 0);

            // no motion
            if ((ABS(start_x - new_x) < 5) && (ABS(start_y - new_y) < 5)) {
                // ensure that a minimum number of call to libS52
                // to turn vrmebl ON/OFF
                if (TRUE == mode_vrmebl_set) {
                    mode_vrmebl = (TRUE==mode_vrmebl) ? FALSE : TRUE;
                    if (TRUE == mode_vrmebl) {
                        S52_toggleObjClassOFF("cursor");
                        S52_toggleObjClassOFF("ebline");
                    } else {
                        S52_toggleObjClassON("cursor");
                        S52_toggleObjClassON("ebline");
                    }
                    mode_vrmebl_set = FALSE;
                }
            }
        }

        // touch North reset chart upright
        if ((new_x < EDGE_X0) && (new_y < EDGE_Y0) && (start_x < EDGE_X0) && (start_y < EDGE_Y0)) {
            new_x = engine->state.cLon;
            new_y = engine->state.cLat;
            new_z = engine->state.rNM;
            new_r = 0.0;

        } else {
            // screen Center
            //double cw = engine->width  / 2.0;
            //double ch = engine->height / 2.0;
            //if (FALSE == S52_xy2LL(&cw, &ch))
            //    return FALSE;

            if (TRUE == mode_rot) {
                double north = engine->state.north + (90.0 * ((start_x - new_x) / engine->width));
                if (north <    0.0) north += 360.0;
                if (north >= 360.0) north -= 360.0;

                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
                new_z = engine->state.rNM;
                new_r = north;
                //new_x = cw;
                //new_y = ch;
                //new_z = -1.0;  // don't care
                //new_r = north;
                //LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_UP: north=%f\n", north);
            }

            if (TRUE == mode_zoom) {
                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
#ifdef S52_USE_ADRENO
                new_z = engine->state.rNM - (zoom_fac * engine->state.rNM * 20); // FIXME: where is the 2 comming from?
#else
                new_z = engine->state.rNM - (zoom_fac * engine->state.rNM * 2); // FIXME: where is the 2 comming from?
#endif
                new_r = engine->state.north;
            }

            // mode_scroll
            if (TRUE==mode_scroll && FALSE==mode_zoom && FALSE==mode_rot && FALSE==mode_vrmebl) {
                new_y = engine->height - new_y;
                if (FALSE == S52_xy2LL(&new_x, &new_y))
                    return FALSE;
                start_y = engine->height - start_y;
                if (FALSE == S52_xy2LL(&start_x, &start_y))
                    return FALSE;


                new_x = engine->state.cLon + (start_x - new_x);
                new_y = engine->state.cLat + (start_y - new_y);
                new_z = engine->state.rNM;
                new_r = engine->state.north;
                //new_x = cw + (start_x - new_x);
                //new_y = ch + (start_y - new_y);
                //new_z = -1.0;  // don't care
                //new_r = -1.0;  // don't care
            }

        }

        _android_render(engine, new_y, new_x, ABS(new_z), new_r);
        //LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_UP: north=%f\n", new_r);
    }
    break;

    }

    // normal mode
    engine->do_S52draw     = FALSE;
    engine->do_S52drawLast = TRUE;

    return TRUE;
}

static int32_t  _android_handle_input(struct android_app *app, AInputEvent *event)
// Process the next input event.
// Return 1 the event was handled, 0 for any default dispatching.
{
    struct s52engine *engine = (struct s52engine*)app->userData;

    int32_t eType = AInputEvent_getType(event);

    switch (eType) {
        case AINPUT_EVENT_TYPE_MOTION: {
            LOGI("s52egl:--> AINPUT_EVENT_TYPE_MOTION\n");

            _android_motion_event(engine, event);

            break;
        }

        case AINPUT_EVENT_TYPE_KEY: {
            LOGI("s52egl:--> AINPUT_EVENT_TYPE_KEY\n");
            int32_t devID = AInputEvent_getDeviceId(event);

            // Get the input event source.
            int32_t source = AInputEvent_getSource(event);


            // *** Accessors for key events only. ***
            const AInputEvent* key_event = event;

            // Get the key event flags.
            int32_t flags = AKeyEvent_getFlags(key_event);

            // Get the key code of the key event.
            // This is the physical key that was pressed, not the Unicode character.
            int32_t code = AKeyEvent_getKeyCode(key_event);

            // MENU DN:       - eType:1  devID:-1 source:257 action:0 flags:72 code:82
            // MENU UP:       - eType:1  devID:-1 source:257 action:1 flags:72 code:82
            //LOGI("s52egl:AInputEvent - eType:%i devID:%i source:%i action:%i flags:%X code:%i\n",
            //                    eType,   devID,   source,   action,   flags,   code);

            // AInputEvent - eType:1 devID:-1 source:257 action:1 flags:104 code:82

            // Get the key event action.
            int32_t action = AKeyEvent_getAction(key_event);
            LOGI("s52egl:AInputEvent - eType:%i devID:%i source:%i action:%i flags:%X code:%i\n",
                                       eType,   devID,   source,   action,   flags,   code);

            if (AKEYCODE_MENU==code && 0==action) {
                _android_init_external_UI(engine);

                // remove VRMEBL (no use in s52ui)
                S52_toggleObjClassOFF("cursor");
                S52_toggleObjClassOFF("ebline");
            }

            //*
            if (AKEYCODE_BACK == code) {
                // .. do nothing
                // .. eat the key (ie return TRUE --> event has been handled)
                return EGL_TRUE;
            }
            //*/

            break;
        }

        default:
            LOGI("s52egl:Unknown AInputEvent [%i]\n", eType);
            break;
    }

    return EGL_TRUE;
}

static void     _android_handle_cmd(struct android_app *app, int32_t cmd)
// process the next main command.
{
    s52engine* engine = (s52engine*)app->userData;
    if (NULL == engine) {
        LOGE("ERROR: _android_handle_cmd(): no 's52engine'\n");
        return;
    }

    switch (cmd) {
        case APP_CMD_START: {
            // onRestart() only in Java !!
            LOGI("s52egl:--> APP_CMD_START\n");
            if (NULL == engine->app->window)
                LOGI("s52egl:APP_CMD_START: ANativeWindow is NULL\n");
            else
                LOGI("s52egl:APP_CMD_START: ANativeWindow is NOT NULL\n");

            if (NULL == engine->app->savedState)
                LOGI("s52egl:APP_CMD_START: savedState is NULL\n");
            else
                LOGI("s52egl:APP_CMD_START: savedState is NOT NULL\n");

            break;
        }
        case APP_CMD_RESUME: {
            LOGI("s52egl:--> APP_CMD_RESUME\n");

            // do not start rendering yet, wait for APP_CMD_GAINED_FOCUS instead
            //engine->do_S52draw     = TRUE;
            //engine->do_S52drawLast = TRUE;

            break;
        }
        case APP_CMD_SAVE_STATE: {
            // android tell to save the current state
            LOGI("s52egl:--> APP_CMD_SAVE_STATE\n");

            // NOTE: the docs say that at this point 'savedState' has already been freed
            // so this meen that NativeActivity use alloc()/free()
            if (NULL == engine->app->savedState) {
                engine->app->savedState     = malloc(sizeof(s52droid_state_t));
                engine->app->savedStateSize =        sizeof(s52droid_state_t);
            } else {
                // just checking: this should not happend
                LOGE("ERROR: APP_CMD_SAVE_STATE: savedState not NULL\n");
                g_main_loop_quit(engine->state.main_loop);
            }

            *((s52droid_state_t*)engine->app->savedState) = engine->state;

            break;
        }
        case APP_CMD_INIT_WINDOW: {
            // window is being shown, get it ready
            LOGI("s52egl:--> APP_CMD_INIT_WINDOW\n");

            if (NULL != engine->app->window) {
                if (EGL_TRUE == _android_display_init(engine)) {
                    //_android_init_external_ais();
                    //_android_init_external_gps();
                }
            }
            break;
        }
        case APP_CMD_TERM_WINDOW: {
            LOGI("s52egl:--> APP_CMD_TERM_WINDOW\n");

            // UI on top, so keep rendering
            //if (TRUE == _androidUIon)
            //    break;

            // window hidden or closed
            // check this,
            engine->do_S52draw     = FALSE;
            engine->do_S52drawLast = FALSE;

            _android_done_external_sensors();

            _egl_done(engine);

            break;
        }
        case APP_CMD_GAINED_FOCUS: {
            // app gains focus
            LOGI("s52egl:--> APP_CMD_GAINED_FOCUS\n");

            if (NULL == engine->app->window) {
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: ANativeWindow is NULL\n");
            } else {
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: ANativeWindow is NOT NULL\n");
                //ANativeWindow_acquire(engine->app->window);
            }

            //_android_done_external_UI(engine);
            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;

            break;
        }
        case APP_CMD_LOST_FOCUS: {
            // app loses focus, stop monitoring sensor
            // to avoid consuming battery while not being used.
            LOGI("s52egl:--> APP_CMD_LOST_FOCUS\n");

            break;
        }
        case APP_CMD_CONFIG_CHANGED: {
            // device rotated
            LOGI("s52egl:--> APP_CMD_CONFIG_CHANGED\n");
            int32_t confDiff = AConfiguration_diff(engine->config, engine->app->config);
            LOGI("s52egl: config diff: 0x%04x\n", confDiff);
            _android_config_dump(engine->app->config);
            AConfiguration_copy(engine->config, engine->app->config);

            // no AIS update with drawLast() meen that
            // s52ui handle device orientation
            if (FALSE == engine->do_S52drawLast)
                break;

            // ACONFIGURATION_ORIENTATION == 0x0080,
            // ACONFIGURATION_SCREEN_SIZE == 0x0200,
            if ((ACONFIGURATION_ORIENTATION | ACONFIGURATION_SCREEN_SIZE) & confDiff) {
                engine->orientation       = AConfiguration_getOrientation(engine->config),
                engine->do_S52setViewPort = TRUE;
                engine->do_S52draw        = TRUE;
            }

            break;
        }
        case APP_CMD_DESTROY: {
            LOGI("s52egl:--> APP_CMD_DESTROY\n");
            if (TRUE == engine->app->destroyRequested) {
                LOGI("s52egl:DEBUG (check this): --> APP_CMD_DESTROY: destroyRequested flags set\n");
                //g_main_loop_quit(engine->state.main_loop);
            }

            engine->do_S52draw     = FALSE;
            engine->do_S52drawLast = FALSE;

            break;
        }


    // TODO: what about those !
    case APP_CMD_INPUT_CHANGED:
        LOGI("s52egl:TODO:--> APP_CMD_INPUT_CHANGED\n");
        break;
    case APP_CMD_WINDOW_RESIZED:
        LOGI("s52egl:TODO:--> APP_CMD_WINDOW_RESIZED\n");
        break;
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        LOGI("s52egl:TODO:--> APP_CMD_WINDOW_REDRAW_NEEDED\n");
        break;
    case APP_CMD_CONTENT_RECT_CHANGED:
        LOGI("s52egl:TODO:--> APP_CMD_CONTENT_RECT_CHANGED\n");
        break;
    case APP_CMD_LOW_MEMORY:
        LOGI("s52egl:TODO:--> APP_CMD_LOW_MEMORY\n");
        break;
    case APP_CMD_PAUSE:
        LOGI("s52egl:TODO:--> APP_CMD_PAUSE\n");
        break;
    case APP_CMD_STOP:
        LOGI("s52egl:TODO:--> APP_CMD_STOP\n");
        break;
    }
}

static void     _onConfigurationChanged(ANativeActivity *activity)
// FIXME: not sure if this come from the main thread
// so in case the glib main loop is still running at this point
// the mutex prevent a collision
{
    LOGI("s52egl:_onConfigurationChanged(): starting ..\n");

    g_static_mutex_lock(&_engine_mutex);

    _engine.do_S52setViewPort = TRUE;
    _engine.do_S52draw        = TRUE;
    _engine.do_S52drawLast    = TRUE;

    g_static_mutex_unlock(&_engine_mutex);

    return;
}

static void     _onNativeWindowResized(ANativeActivity* activity, ANativeWindow* window)
// FIXME: not sure if this come from the main thread
// so in case the glib main loop is still running at this point
// the mutex prevent a collision
{
    LOGI("s52egl:_onNativeWindowResized(): starting ..\n");

    g_static_mutex_lock(&_engine_mutex);

    _engine.do_S52setViewPort = TRUE;
    _engine.do_S52draw        = TRUE;
    _engine.do_S52drawLast    = TRUE;

    g_static_mutex_unlock(&_engine_mutex);

    return;
}

void android_main(struct android_app *app)
// This is the main entry point of a native application that is using
// android_native_app_glue.  It runs in its own thread, with its own
// event loop for receiving input events and doing other things.
{
    // debug - provoke a sigsegv that android's debugerd should catch
	//int *p = 0x0;
    //*p = 1;

    LOGI("s52egl:android_main(): starting ..\n");

    // Make sure glue isn't stripped.
    app_dummy();

    memset(&_engine, 0, sizeof(_engine));
    app->userData     = &_engine;
    app->onAppCmd     = _android_handle_cmd;
    app->onInputEvent = _android_handle_input;

    _engine.app        = app;
    _engine.dpi        = AConfiguration_getDensity(app->config);
    _engine.config     = AConfiguration_new();
    _engine.configBits = AConfiguration_diff(app->config, _engine.config);
    AConfiguration_copy(_engine.config, app->config);

    // setup callbacks to detect android rotation
    _engine.callbacks  = _engine.app->activity->callbacks;
    //_engine.callbacks->onConfigurationChanged = _onConfigurationChanged;
    //_engine.callbacks->onNativeWindowResized  = _onNativeWindowResized;

    // prepare to monitor sensor
    //engine.sensorManager    = ASensorManager_getInstance();
    //engine.lightSensor      = ASensorManager_getDefaultSensor(engine.sensorManager, ASENSOR_TYPE_LIGHT);
    //engine.gyroSensor       = ASensorManager_getDefaultSensor(engine.sensorManager, ASENSOR_TYPE_GYROSCOPE);
    //engine.sensorEventQueue = ASensorManager_createEventQueue(engine.sensorManager, app->looper, LOOPER_ID_USER, NULL, NULL);
    //_android_sensorsList_dump(engine.sensorManager);

    ANativeActivity_setWindowFlags(_engine.app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0x0);

    //ActivityManager.MemoryInfo

    /*
    // prepare to read file
    //engine.assetManager     = engine.app->activity->assetManager;
    //AAsset *test =  AAssetManager_open(engine.assetManager, "test.txt", AASSET_MODE_BUFFER);
    AAsset *asset =  AAssetManager_open(engine.assetManager, "text.txt", AASSET_MODE_UNKNOWN);
    if (NULL != asset) {
        //const char *buf = (const char*)AAsset_getBuffer(test);
        char buf[80];
        int ret = AAsset_read(asset, buf, 80);

        LOGI("s52egl:ret:%i buf:%s\n", ret, buf);

        AAsset_close(asset);

        ret = g_file_set_contents("/data/data/nav.ecs.s52droid/text-1.txt", buf, strlen(buf), NULL);
        if (FALSE == ret)
            LOGI("s52egl:g_file_set_contents() fail \n");

    } else {
        LOGI("s52egl:asset not found\n");
    }
    //*/

    //engine.detail = g_quark_from_string("testing");

    // init thread first before any call to glib
    // event if NULL is used and glib call are more relaxe
    g_thread_init(NULL);
    g_type_init();

#ifdef S52_USE_AIS
    // Note: data form AIS start too fast for the main loop
    s52ais_initAIS();
#endif


    //*
    if (NULL == _engine.app->savedState) {

        // first time startup
        _engine.state.do_S52init = TRUE;

        //------------------------------------------
        // FIXME: check this, start after the main loop
        // (should not need to be started before or
        // after the server mainloop)
        // START Clients
        //_android_spawn_gps();
        //_android_spawn_ais();
        //------------------------------------------

        _engine.state.main_loop       = g_main_loop_new(NULL, FALSE);
        /*
        _engine.state.gobject         = g_object_new(G_TYPE_OBJECT, NULL);
        _engine.state.s52_draw_sigID  = g_signal_new("s52-draw",
                                                     G_TYPE_FROM_INSTANCE(_engine.state.gobject),
                                                     G_SIGNAL_RUN_LAST,
                                                     //G_SIGNAL_ACTION,
                                                     //G_SIGNAL_RUN_FIRST,
                                                     0,                      // offset
                                                     NULL, NULL,             // accumulator / data
                                                     NULL,                   // marshaller
                                                     G_TYPE_NONE,            // signal without a return value.
                                                     0);                     // the number of parameter types to follow


        _engine.state.handler = g_signal_connect(G_OBJECT(_engine.state.gobject), "s52-draw",
                                                G_CALLBACK(_s52_draw_cb), (gpointer)&_engine);
        */

        g_timeout_add(500, _s52_draw_cb, (void*)&_engine);     // 0.5 sec (500msec)


    } else {
        // if re-starting - the process is already up
        _engine.state = *(s52droid_state_t*)app->savedState;

        LOGI("s52egl:DEBUG: bypassing _init_S52(), reset state .. \n" );
        LOGI("s52egl:       cLat =%f\n", _engine.state.cLat           );
        LOGI("s52egl:       cLon =%f\n", _engine.state.cLon           );
        LOGI("s52egl:       rNM  =%f\n", _engine.state.rNM            );
        LOGI("s52egl:       north=%f",   _engine.state.north          );

        // don't re-init S52
        //engine.state.do_S52init = FALSE;

        // check if main loop up (seem useless)
        if (FALSE == g_main_loop_is_running(_engine.state.main_loop)) {
            LOGI("s52egl:engine.view.main_loop is running .. reconnecting ..\n");
            g_main_loop_run(_engine.state.main_loop);
        }
    }

    // Damien's glib main loop (http://damien.lespiau.name/blog/)
    //LOGI("s52egl:starting g_main_loop_run() ..\n");
    //g_android_init();
    //g_main_loop_run(engine.state.main_loop);
    //LOGI("s52egl:exiting g_main_loop_run() ..\n");

    //*
    //
    // android main loop - read msg to free android msg queue, mem leak
    // GPSD - OK
    // SL4A - leak (but not makeToast()!)
    LOGI("s52egl:android_main(): while loop start\n");
    while (1) {
        int ident;
        int events;
        struct android_poll_source* source;

        // read all pending events (0 --> no wait).
        //while ((ident = ALooper_pollAll(-1, NULL, &events, (void**) &source)) >= 0) {
        while ((ident = ALooper_pollAll(0, NULL, &events, (void**) &source)) >= 0) {
            // Process this event.
            if (source != NULL)
                source->process(app, source);

            // Check if we are exiting.
            if (0 != _engine.app->destroyRequested) {
                LOGI("s52egl:android_main(): IN while loop .. destroyRecquested\n");
                //engine_term_display(&engine);
                goto exit;
            }
        }

        // process all glib pending events
        while(g_main_context_iteration(NULL, FALSE))
            ;

        // slow down the loop to 1% CPU (without it then 50% CPU)
        g_usleep(10 * 1000);  // 0.01 sec
    }
    //*/


exit:

    //_android_done_external_sensors();

#ifdef S52_USE_AIS
    s52ais_doneAIS();
#endif

    _s52_done(&_engine);

    _egl_done(&_engine);

    AConfiguration_delete(_engine.config);

    return;
}

#else  // end of S52_USE_ANDROID

//----------------------------------------------
//
// X11 specific code
// for testing EGL / GLES2 outside of android
//

#include </usr/include/X11/XKBlib.h>  // XkbKeycodeToKeysym()

static int      _X11_error(Display *display, XErrorEvent *err)
{
    char buf[80];
    XGetErrorText(display, err->error_code, buf, 80);

    printf("*** X error *** %d 0x%x %ld %d %d\n",
           err->type ,
           (unsigned int)err->resourceid,
           (long int)err->error_code ,
           (int)err->request_code,
           err->minor_code
          );
    printf("txt: %s\n", buf);

    return 1;
}

static int      _X11_handleXevent(gpointer user_data)
{
    s52engine *engine = (s52engine *) user_data;

    while (XPending(engine->dpy)) {
        XEvent event;
        XNextEvent(engine->dpy, &event);

        switch (event.type) {
        case ConfigureNotify:
            engine->width  = event.xconfigure.width;
            engine->height = event.xconfigure.height;
            S52_setViewPort(0, 0, event.xconfigure.width, event.xconfigure.height);

            break;

        case Expose:
            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
            g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0);
            break;

        case ButtonRelease:
            {
                XButtonReleasedEvent *mouseEvent = (XButtonReleasedEvent *)&event;

                const char *name = S52_pickAt(mouseEvent->x, engine->height - mouseEvent->y);
                if (NULL != name) {
                    unsigned int S57ID = atoi(name+7);
                    g_print("OBJ(%i, %i): %s\n", mouseEvent->x, engine->height - mouseEvent->y, name);
                    g_print("AttList=%s\n", S52_getAttList(S57ID));

                    {   // debug:  S52_xy2LL() --> S52_LL2xy() should be the same
                        // NOTE:  LL (0,0) is the OpenGL origine (not GTK origine)
                        double Xlon = 0.0;
                        double Ylat = 0.0;
                        S52_xy2LL(&Xlon, &Ylat);
                        S52_LL2xy(&Xlon, &Ylat);
                        g_print("DEBUG: xy2LL(0,0) --> LL2xy ==> Xlon: %f, Ylat: %f\n", Xlon, Ylat);
                    }

                    if (0 == g_strcmp0("vessel", name)) {
                        g_print("vessel found\n");
                        unsigned int S57ID = atoi(name+7);

                        S52ObjectHandle vessel = S52_getMarObjH(S57ID);
                        if (NULL != vessel) {
                            S52_setVESSELstate(vessel, 1, 0, VESSELTURN_UNDEFINED);
                            //g_print("AttList: %s\n", S52_getAttList(S57ID));
                        }
                    }
                }

            }
            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
            g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0);
            break;

        case KeyPress:
        case KeyRelease: {
            // /usr/include/X11/keysymdef.h
            unsigned int keycode = ((XKeyEvent *)&event)->keycode;
            unsigned int keysym  = XkbKeycodeToKeysym(engine->dpy, keycode, 0, 1);

            // ESC
            if (XK_Escape == keysym) {
                g_main_loop_quit(engine->state.main_loop);
                return TRUE;
            }
            // Load Cell
            if (XK_F1 == keysym) {
                S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000", NULL);
                //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA379035.000", NULL);
                //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000", NULL);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // Done Cell
            if (XK_F2 == keysym) {
                S52_doneCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000");
                //S52_doneCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000");
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // Disp. Cat. SELECT
            if (XK_F3 == keysym) {
                S52_setMarinerParam(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // VRMEBL toggle
            if (XK_F4 == keysym) {
                _drawVRMEBLtxt = !_drawVRMEBLtxt;
                if (TRUE == _drawVRMEBLtxt) {
                    S52_toggleObjClassOFF("cursor");
                    S52_toggleObjClassOFF("ebline");
                    S52_toggleObjClassOFF("vrmark");

                    {
                        double brg = 0.0;
                        double rge = 0.0;
                        S52_setVRMEBL(_vrmeblA, 100, 100, &brg, &rge);
                        S52_setVRMEBL(_vrmeblA, 100, 500, &brg, &rge);
                    }

                    S52_pushPosition(_cursor2, engine->state.cLat, engine->state.cLon, 0.0);

                } else {
                    S52_toggleObjClassON("cursor");
                    S52_toggleObjClassON("ebline");
                    S52_toggleObjClassON("vrmark");
                }

                return TRUE;
            }
            // Rot. Buoy Light
            if (XK_F5 == keysym) {
                S52_setMarinerParam(S52_MAR_ROT_BUOY_LIGHT, 180.0);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // ENC list
            if (XK_F6 == keysym) {
                g_print("%s\n", S52_getCellNameList());
                return TRUE;
            }
            // load AIS select symb.
            if (XK_F7 == keysym) {
                S52_loadPLib("plib-test-priv.rle");
                return TRUE;
            }
            // debug - unicode at S57ID:552 on CA579041.000 - Rimouski
            if (XK_F8 == keysym) {
                const char *str = S52_getAttList(552);
                g_print("s52eglx:F8:%s\n", str);

                return TRUE;
            }


            // debug
            g_print("s52egl.c:keysym: %i\n", keysym);


            //
            // debug - basic view movement
            //

            double delta = (engine->state.rNM / 10.0) / 60.0;

            // Move left, left arrow
            if (XK_Left      == keysym) {
                engine->state.cLon -= delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move up, up arrow
            if (XK_Up        == keysym) {
                engine->state.cLat += delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move right, right arrow
            if (XK_Right     == keysym) {
                engine->state.cLon += delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move down, down arrow
            if (XK_Down      == keysym) {
                engine->state.cLat -= delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // zoom in
            if (XK_Page_Up   == keysym) {
                engine->state.rNM -= (engine->state.rNM / 10.0);
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // zoom out
            if (XK_Page_Down == keysym) {
                engine->state.rNM += (engine->state.rNM / 10.0);
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // rot -10.0 deg
            if (XK_Home      == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
#else
                _egl_beg(engine);
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
                _egl_end(engine);
#endif
                engine->state.north -= 10.0;
                if (0.0 > engine->state.north)
                    engine->state.north += 360.0;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // rot +10.0 deg
            if (XK_End       == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
#else
                _egl_beg(engine);
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
                _egl_end(engine);
#endif


                engine->state.north += 10.0;  // +10.0 deg
                if (360.0 <= engine->state.north)
                    engine->state.north -= 360.0;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }

            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
        }
        break;

        }  // switch
    }      // while

    return TRUE;
}

int  main(int argc, char *argv[])
{
    g_print("main():starting: argc=%i, argv[0]=%s\n", argc, argv[0]);

    XSetErrorHandler(_X11_error);

    _egl_init(&_engine);

    // init thread first before any call to glib
    // event if NULL mean that glib call are more relaxe
    g_thread_init(NULL);
    g_type_init();


    _engine.state.main_loop       = g_main_loop_new(NULL, FALSE);
    _engine.state.gobject         = g_object_new(G_TYPE_OBJECT, NULL);
    _engine.state.s52_draw_sigID  = g_signal_new("s52-draw",
                                                 G_TYPE_FROM_INSTANCE(_engine.state.gobject),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL, NULL,
                                                 NULL,
                                                 G_TYPE_NONE, 0);

    _engine.state.handler = g_signal_connect_data(_engine.state.gobject, "s52-draw",
                                                 G_CALLBACK(_s52_draw_cb), (gpointer)&_engine,
                                                 NULL, G_CONNECT_SWAPPED);

    g_timeout_add(500, _X11_handleXevent, (void*)&_engine);  // 0.5 sec
    g_timeout_add(500, _s52_draw_cb,      (void*)&_engine);  // 0.5 sec


    _s52_init(&_engine);


    //g_mem_set_vtable(glib_mem_profiler_table);

    g_main_loop_run(_engine.state.main_loop);

    _s52_done(&_engine);

    _egl_done(&_engine);

    //g_mem_profile();

    g_print("%s .. done\n", argv[0]);

    return TRUE;
}
#endif
