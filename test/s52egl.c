// S52egl.c: simple S52 driver using only EGL.
//
// SD 2011NOV08 - update

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


#include "S52.h"

#ifdef USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>      // as a side effect,
#include <EGL/eglext.h>   // X is included OR android stuff

#include <stdio.h>        // printf()
#include <stdlib.h>       // exit(0)
#include <string.h>       // memset()

// compiled with -std=gnu99 instead of -std=c99 will define M_PI
#include <math.h>         // sin(), cos(), atan2(), pow(), sqrt(), floor(), INFINITY, M_PI

#include <glib.h>
#include <glib-object.h>  // signal
#include <glib/gprintf.h> // g_sprintf(), g_ascii_strtod(), g_strrstr()
#include <glibconfig.h>
#include <gio/gio.h>      // mutex

//extern GMemVTable *glib_mem_profiler_table;

#define DEG_TO_RAD     0.01745329238
#define RAD_TO_DEG    57.29577951308232
#define INCH2MM       25.4

// FIXME: suppress output on Android if not in DEBUG, where uers=prg mem grow
#ifdef S52_USE_ANDROID
#include <jni.h>
#include <errno.h>
#include <android/sensor.h>
#include <android/log.h>
#include <android/window.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>  // struct android_app
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

#define PATH     "/sdcard/s52droid"      // Android 4.2
#define PLIB     PATH "/PLAUX_00.DAI"
#define COLS     PATH "/plib_COLS-3.4-a.rle"
#define GPS      PATH "/bin/sl4agps"
#define AIS      PATH "/bin/s52ais"
#define PID           ".pid"
#define ALLSTOP  PATH "/bin/run_allstop.sh"

#else   // S52_USE_ANDROID

#define  PATH "/home/sduclos/dev/gis/data"
#define  PLIB "PLAUX_00.DAI"
#define  COLS "plib_COLS-3.4-a.rle"
#define  LOGI(...)   g_print(__VA_ARGS__)
#define  LOGE(...)   g_print(__VA_ARGS__)

#endif  // S52_USE_ANDROID

// ebline draw flag (F4 toggle ON/OFF)
//static int _drawVRMEBL = TRUE;
static int _drawVRMEBL = FALSE;


// test - St-Laurent Ice Route
static S52ObjectHandle _waypnt1 = FALSE;
static S52ObjectHandle _waypnt2 = FALSE;
static S52ObjectHandle _waypnt3 = FALSE;
static S52ObjectHandle _waypnt4 = FALSE;

static S52ObjectHandle _leglin1 = FALSE;
static S52ObjectHandle _leglin2 = FALSE;
static S52ObjectHandle _leglin3 = FALSE;
static S52ObjectHandle _leglin4 = FALSE;

// lat/lon/begin/end
static double _leglin4xy[2*2];

// test - VRMEBL
// S52 object name:"ebline"
static S52ObjectHandle _vrmeblA = FALSE;

// test - cursor DISP 9 (instead of IHO PLib DISP 8)
// need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = FALSE;  // 2 - open cursor

// test - centroid
static S52ObjectHandle _prdare  = FALSE;


// FIXME: mutex this share data
typedef struct s52droid_state_t {
    // initial view
    double     cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)

    double     dx_pc, dy_pc, dz_pc, dw_pc; // Blit param, dw_pc = north;

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
    //EGLNativeWindowType eglWindow;
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;
    EGLConfig           eglConfig;

    //EGLClientBuffer     eglClientBuf;
    //EGLNativePixmapType eglPixmap;       // eglCopyBuffers()


    // draw thread
    GMainLoop          *main_loop;

    // debug - test with rendering thread
    //GThread            *drawThread;
    //GAsyncQueue        *queue;             // asynchronous communication between threads
    //GTimeVal            end_time;
    //guint               idle_id;           // start _s52_draw_cb
    //GStaticMutex        mutex;

    // flags use in _s52_draw_cb
    int                 do_S52draw;        // TRUE to call S52_draw()
    int                 do_S52drawLast;    // TRUE to call S52_drawLast() - S52_draw() was called at least once
    int                 do_S52drawBlit;    // TRUE to call S52_drawBlit() - S52_draw() was called at least once
    int                 do_S52setViewPort; // TRUE get orientation - set in Android callback
    int32_t             orientation;       // 1=180, 2=090

    int32_t             width;
    int32_t             height;
    // Xoom - dpi = 160 (density)
    int32_t             dpi;            // = AConfiguration_getDensity(engine->app->config);
    int32_t             wmm;
    int32_t             hmm;

    s52droid_state_t    state;
} s52engine;

static s52engine    _engine;


//----------------------------------------------
//
// Common stuff
//


// debug - lap timer
static GTimer *_timer = NULL;

// debug - exprimental
//static int _doLoadRADAR = TRUE;

#define VESSELTURN_UNDEFINED 129



//------ DEBUG ----
// debug - no real AIS, then fake target
#ifdef USE_FAKE_AIS
static S52ObjectHandle _vessel_ais        = NULL;
#define VESSELLABEL "~~MV Non Such~~ "           // last char will be trimmed

// test synthetic after glow on VESSEL
#ifdef S52_USE_AFGLOW
#define MAX_AFGLOW_PT (12 * 20)   // 12 min @ 1 vessel pos per 5 sec
//#define MAX_AFGLOW_PT 10        // debug
static S52ObjectHandle _vessel_ais_afglow = NULL;
#endif

// test - ownshp
static S52ObjectHandle _ownshp            = NULL;
#define OWNSHPLABEL "OWNSHP\n220 deg / 6.0 kt"

#endif  // USE_FAKE_AIS

typedef void (*PFNGLINSERTEVENTMARKEREXT)(int length, const char *marker);
//typedef void (GL_APIENTRY *PFNGLPUSHGROUPMARKEREXT)  (GLsizei length, const char *marker);
//typedef void (GL_APIENTRY *PFNGLPOPGROUPMARKEREXT)   (void);

static PFNGLINSERTEVENTMARKEREXT _glInsertEventMarkerEXT = NULL;
//static PFNGLPUSHGROUPMARKEREXT   _glPushGroupMarkerEXT   = NULL;
//static PFNGLPOPGROUPMARKEREXT    _glPopGroupMarkerEXT    = NULL;

typedef EGLuint64NV (*PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC) (void);
typedef EGLuint64NV (*PFNEGLGETSYSTEMTIMENVPROC)          (void);
static PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC _eglGetSystemTimeFrequencyNV = NULL;
static PFNEGLGETSYSTEMTIMENVPROC          _eglGetSystemTimeNV          = NULL;

/*
// GL not GLES2-3 When GL_EXT_framebuffer_multisample is supported, GL_EXT_framebuffer_object and GL_EXT_framebuffer_blit are also supported.
#ifndef GL_EXT_multisampled_render_to_texture
#define GL_EXT_multisampled_render_to_texture 1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT 0x8D6C
#define GL_RENDERBUFFER_SAMPLES_EXT       0x8CAB
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT 0x8D56
#define GL_MAX_SAMPLES_EXT                0x8D57
typedef void (GL_APIENTRYP PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)  (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples);
#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glRenderbufferStorageMultisampleEXT  (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
GL_APICALL void GL_APIENTRY glFramebufferTexture2DMultisampleEXT (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples);
#endif
#endif // GL_EXT_multisampled_render_to_texture
*/

//-----------------------------

#ifdef S52_USE_RADAR
#define RADARLOG PATH "/radar/RADAR_imitator/radarlog"

// Description of management structures and radar images
typedef struct {
    unsigned int RAIN      : 1;
    unsigned int FRUIT     : 1;
    unsigned int SCALE     : 4;
    unsigned int MODE_2    : 1;
    unsigned int _Status    : 1;     // collide with symbole 'Status' !
    unsigned int reserved1 : 8;
    unsigned int reserved2 : 8;
    unsigned int reserved3 : 8;
} PSO_APMode;

typedef struct {
    unsigned int    dwHeader;
    PSO_APMode      mrAPMode;
    unsigned short  Td;
    unsigned short  IPCHG;
    unsigned short  iStringsCount;
    unsigned short  iStringLength;
    unsigned char   reserved[4];
    unsigned short  iCurrentString; // number of line [0..2047]
    unsigned char   image[1280];    // image line
} PSO_ImageDGram;

#define ANGLEmax 2048
#define Rmax     1280
static FILE *_fd = NULL;

typedef struct {
    double x;
    double y;
} POINT;

static guchar _RADARtex[Rmax*2][Rmax*2];  // Alpha
//static guchar _RADARtex[Rmax*2][Rmax*2][4];  // RGBA
static POINT  _Polar_Matrix_Of_Coords[ANGLEmax][Rmax];
static int      _radar_init()
{
    if (NULL == (_fd = fopen(RADARLOG, "rb"))) {
        g_print("s52egl:_initRadar(): can't open file %s\n", RADARLOG);
        g_assert(0);
        return FALSE;
    }

    memset(_Polar_Matrix_Of_Coords, 0, sizeof(_Polar_Matrix_Of_Coords));
    memset(_RADARtex,               0, sizeof(_RADARtex));

    // calculate polar coords
    for (int ANGLE = 0; ANGLE < ANGLEmax; ANGLE++) {
        double ANGLE_RAD = (double)ANGLE/ANGLEmax*2*M_PI;
        double cosinus   = cos(ANGLE_RAD);
        double sinus     = sin(ANGLE_RAD);

        for (int R = 0; R < Rmax; R++) {
            _Polar_Matrix_Of_Coords[ANGLE][R].x = R * cosinus + Rmax;
            _Polar_Matrix_Of_Coords[ANGLE][R].y = R * sinus   + Rmax;
        }
    }

    return TRUE;
}

static int      _radar_writePoint (unsigned char VALUE, int ANGLE, int R)
// Alpha texture,
{
    double x = _Polar_Matrix_Of_Coords[ANGLE][R].x;
    double y = _Polar_Matrix_Of_Coords[ANGLE][R].y;


    //_RADARtex[(int)y][(int)x] = VALUE;  // Alpha
    //_RADARtex[(int)(y+0.5)][(int)(x+0.5)] = VALUE;  // Alpha
    //_RADARtex[(int)y][(int)x][3] = VALUE;  // Alpha

    // debug - rounding x\y+.5 make no diff
    _RADARtex[(int)y][(int)x] = 255 - VALUE;  // Alpha reverse (more conspic)
    //_RADARtex[(int)(y+0.5)][(int)(x+0.5)] = 255 - VALUE;

    return TRUE;
}

static int      _radar_writeString(guchar *string, int ANGLE)
{
    for (int R = 0; R < Rmax; R++)
        _radar_writePoint(string[R], ANGLE, R);

    return TRUE;
}

static int      _radar_readLog(int nLine)
{
    //LOGI("_radar_readLog()\n");

    PSO_ImageDGram img;
    while (nLine--) {
        if (1 == fread(&img, sizeof(PSO_ImageDGram), 1, _fd)) {
            _radar_writeString(img.image, img.iCurrentString);
        } else {
            // return to the top of the file
            rewind(_fd);
            g_print("fread = 0\n");
        }
        if (0 != ferror(_fd)) {
            // handle error
            g_print("ferror != 0\n");
            g_assert(0);
            return FALSE;
        }
    }

    return TRUE;
}
#endif  // S52_USE_RADAR

static int      _egl_init       (s52engine *engine)
{
    LOGI("s52egl:_egl_init(): beg ..\n");

    if ((NULL != engine) && (engine->eglDisplay != EGL_NO_DISPLAY)) {
        if (engine->eglDisplay != EGL_NO_DISPLAY)
            LOGI("_egl_init(): EGL DISPLAY OK\n");
        if (engine->eglContext != EGL_NO_CONTEXT)
            LOGI("_egl_init(): EGL CONTEXT OK\n");
        if (engine->eglSurface != EGL_NO_SURFACE)
            LOGI("_egl_init(): EGL SURFACE OK\n");

        LOGE("_egl_init(): EGL is already up .. init skipped!\n");

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



    EGLNativeWindowType eglWindow;
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;

    // Here specify the attributes of the desired configuration.
    // Below, we select an EGLConfig with at least 8 bits per color
    // component compatible with on-screen windows

#ifdef S52_USE_ANDROID
#ifdef S52_USE_TEGRA2
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,        EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,     EGL_OPENGL_ES2_BIT,

        EGL_RED_SIZE,            8,
        EGL_GREEN_SIZE,          8,
        EGL_BLUE_SIZE,           8,
        //EGL_ALPHA_SIZE,          8,

        // Tegra 2 CSAA (anti-aliase)
        EGL_COVERAGE_BUFFERS_NV, 1,  // TRUE
        //EGL_COVERAGE_BUFFERS_NV, 0,

        EGL_COVERAGE_SAMPLES_NV, 2,  // always 5 in practice on tegra 2

        EGL_NONE
    };
#endif  // S52_USE_TEGRA2

#ifdef S52_USE_ADRENO
#define EGL_OPENGL_ES3_BIT_KHR				    0x00000040
    EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,

        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,

        // this bit open access to ES3 functions on QCOM hardware pre-Android support for ES3
        // WARNING: this break MSAA on Android Kit-Kat 4.4.2, 4.4.3 - and -lGLESv3 Android.mk
        //EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES3_BIT_KHR,


        // Note: MSAA work on Andreno in: setting > developer > MSAA
        //EGL_SAMPLES,            1,  // fail on Adreno
        //EGL_SAMPLE_BUFFERS,     4,  // fail on Adreno

        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,

        EGL_NONE
    };
#endif  // S52_USE_ADRENO

#else   // S52_USE_ANDROID

#ifdef S52_USE_GLES2
    // Mesa GL, GLES 2.x, 3.x
    // MSAA: RGBA8, depth24, stencil8 - fail on Ubuntu 14.04 (Wayland!)
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,

        // EGL/GL Mesa3D 10.1 GLSL fail at gl_PointCoord
        //EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        //EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        //EGL_BUFFER_SIZE,        16,
        //EGL_BUFFER_SIZE,        24,

        //EGL_DEPTH_SIZE,         1,
        //EGL_DEPTH_SIZE,         16,
        //EGL_DEPTH_SIZE,         24,

        //EGL_STENCIL_SIZE,        8,

        // fail on Mesa (MSAA)
        //EGL_SAMPLES,             1,   // say EGL_SUCCESS, but 0 config num
        //EGL_SAMPLE_BUFFERS,      1,   // say EGL_SUCCESS, but 0 config num
        //EGL_SAMPLE_BUFFERS,      2,
        //EGL_SAMPLE_BUFFERS,      4,
        //EGL_SAMPLE_BUFFERS,      8,

        EGL_NONE
    };
#else   // S52_USE_GLES2

    // Mesa OpenGL 1.x
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        EGL_NONE
    };
#endif  // S52_USE_GLES2

#endif  // S52_USE_ANDROID


#ifdef S52_USE_ANDROID
    eglDisplay  = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    engine->dpy = XOpenDisplay(NULL);
    eglDisplay  = eglGetDisplay(engine->dpy);
#endif

    if (EGL_NO_DISPLAY == eglDisplay)
        LOGE("eglGetDisplay() failed. [0x%x]\n", eglGetError());

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
    //EGLConfig  eglConfig[780];
    EGLConfig  eglConfig;
    EGLint     eglNumConfigs = 0;

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
        LOGI("eglChooseConfig(): call failed [0x%x]\n", eglGetError());
        g_assert(0);
    }
    if (0 == eglNumConfigs) {
        LOGI("eglChooseConfig(): eglNumConfigs zero matching config [0x%x]\n", eglGetError());
        g_assert(0);
    }
    // debug
    //else
    //    LOGI("eglChooseConfig() eglNumConfigs = %i\n", eglNumConfigs);

#ifdef S52_USE_ANDROID
    // EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
    // guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
    // As soon as we picked a EGLConfig, we can safely reconfigure the
    // ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    EGLint vid;
    if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vid))
    //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[5], EGL_NATIVE_VISUAL_ID, &vid))
        LOGE("Error: eglGetConfigAttrib() failed\n");

    // WARNING: do not use native get/set Width()/Height() has it break rotation
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, vid);
    eglWindow = (EGLNativeWindowType) engine->app->window;
    if (NULL == eglWindow) {
        LOGE("ERROR: ANativeWindow is NULL (can't draw)\n");
        g_assert(0);
    }
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

#ifdef S52_USE_GLES2
        char         *title   = "EGL/OpenGL ES 2.0 on a Linux Desktop";
#else
        char         *title   = "EGL/OpenGL on a Linux Desktop";
#endif
        eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vID);
        tmplt.visualid = vID;
        visual = XGetVisualInfo(display, VisualIDMask, &tmplt, &n);
        if (NULL == visual) {
            LOGE("XGetVisualInfo() failed.\n");
            g_assert(0);
        }
        screen = DefaultScreen(display);
        wa.colormap         = XCreateColormap(display, RootWindow(display, screen), visual->visual, AllocNone);
        //wa.colormap         = XCreateColormap(display, RootWindow(display, screen), NULL, AllocNone);
        wa.background_pixel = 0xFFFFFFFF;
        wa.border_pixel     = 0;
        wa.event_mask       = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

        window = XCreateWindow(display, RootWindow(display, screen), 0, 0, 1280, 1024,
                               0, visual->depth, InputOutput, visual->visual, mask, &wa);
                               //0, 0, InputOutput, NULL, mask, &wa);

        sh.flags = USPosition;
        sh.x = 0;
        sh.y = 0;
        XSetStandardProperties(display, window, title, title, None, 0, 0, &sh);
        XMapWindow(display, window);
        XSetWMColormapWindows(display, window, &window, 1);
        XFlush(display);

        eglWindow = (EGLNativeWindowType) window;

    }
#endif

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, NULL);
    //eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig[5], eglWindow, NULL);
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError()) {
        LOGE("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());
        g_assert(0);
    }

    // when swapping Adreno clear old buffer
    // http://www.khronos.org/registry/egl/specs/EGLTechNote0001.html
    eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);


#ifdef S52_USE_GLES2
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);

    //EGL/GL Mesa3D 10.1 GLSL fail at gl_PointCoord
    //EGLBoolean ret = eglBindAPI(EGL_OPENGL_API);
#else
    // OpenGL 1.x
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_API);
#endif
    if (EGL_TRUE != ret)
        LOGE("eglBindAPI() failed. [0x%x]\n", eglGetError());

    // Then we can create the context and set it current:
    // 1 - GLES1.x, 2 - GLES2.x, 3 - GLES3.x
    EGLint eglContextList[] = {
#ifdef S52_USE_ADRENO
        EGL_CONTEXT_CLIENT_VERSION, 3, // GLES3 to get NPOT texture in blit
#else
        EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
        EGL_NONE
    };

#ifdef S52_USE_GLES2
    // GLES
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, eglContextList);
    //eglContext = eglCreateContext(eglDisplay, eglConfig[5], EGL_NO_CONTEXT, eglContextList);

    // EGL/GL Mesa3D 10.1 GLSL fail at gl_PointCoord
    //eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, NULL);
#else
    // GL
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, NULL);
#endif

    if (EGL_NO_CONTEXT == eglContext || EGL_SUCCESS != eglGetError()) {
        LOGE("eglCreateContext() failed. [0x%x]\n", eglGetError());
        g_assert(0);
    }

    // when swapping Adreno clear old buffer
    // http://www.khronos.org/registry/egl/specs/EGLTechNote0001.html
    eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);

    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        LOGE("Unable to eglMakeCurrent()\n");

    // test - no interval, "non-sync" mode - seem uneffective
    // Note: must be called after MakeCurrent()
    //eglSwapInterval(eglDisplay, 0);  // bad - make blit jurky
    eglSwapInterval(eglDisplay, 1);  // default


    //--------------------------------------------------------------------------------------------------
    // get EGL Marker & Timer
    // Note: on Mesa3D eglGetProcAddress() return an invalid address
    _glInsertEventMarkerEXT = (PFNGLINSERTEVENTMARKEREXT) eglGetProcAddress("glInsertEventMarkerEXT");
    if (NULL == _glInsertEventMarkerEXT) {
        LOGE("DEBUG: eglGetProcAddress(glInsertEventMarkerEXT()) FAILED\n");
    }

    // get GPU driver timer - EGL_NV_system_time
    const char *extstr = eglQueryString(eglDisplay, EGL_EXTENSIONS);
    if (NULL != g_strrstr(extstr, "EGL_NV_system_time")) {
        LOGI("DEBUG: EGL_NV_system_time OK\n");
        _eglGetSystemTimeFrequencyNV = (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC) eglGetProcAddress("eglGetSystemTimeFrequencyNV");
        if (_eglGetSystemTimeFrequencyNV) {
            EGLuint64NV freq = _eglGetSystemTimeFrequencyNV();
            LOGI("DEBUG: eglGetSystemTimeFrequencyNV(): freg:%u\n", (guint)freq);
        }
        _eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC) eglGetProcAddress("eglGetSystemTimeNV");
        if (NULL != _eglGetSystemTimeNV) {
            EGLuint64NV time = _eglGetSystemTimeNV();
            LOGI("DEBUG: eglGetSystemTimeNV(): time:%u\n", (guint)time);
        }
    } else {
        LOGI("DEBUG: EGL_NV_system_time FAILED\n");
    }
    //--------------------------------------------------------------------------------------------------

    engine->eglDisplay = eglDisplay;
    engine->eglContext = eglContext;
    engine->eglSurface = eglSurface;
    //engine->eglWindow  = eglWindow;
    engine->eglConfig  = eglConfig;

    LOGI("s52egl:_egl_init(): end ..\n");

    return EGL_TRUE;
}

static void     _egl_done       (s52engine *engine)
// Tear down the EGL context currently associated with the display.
{
    if (engine->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (engine->eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->eglDisplay, engine->eglContext);
            engine->eglContext = EGL_NO_CONTEXT;
        }

        //if (engine->eglSurface != EGL_NO_SURFACE) {
        //    eglDestroySurface(engine->eglDisplay, engine->eglSurface);
        //    engine->eglSurface = EGL_NO_SURFACE;
        //}

        eglTerminate(engine->eglDisplay);
        engine->eglDisplay = EGL_NO_DISPLAY;
    }

    return;
}

static void     _egl_doneSurface(s52engine *engine)
{
    eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    eglDestroySurface(engine->eglDisplay, engine->eglSurface);
    engine->eglSurface = EGL_NO_SURFACE;

    return;
}

static int      _egl_beg        (s52engine *engine, const char *tag)
{
    (void)engine;
    (void)tag;

    //LOGE("s52egl:_egl_beg() .. \n");
    g_timer_reset(_timer);

    //EGL_SUCCESS             0x3000
    //LOGI("s52egl:_egl_beg(): eglGetError(): 0x%x\n", eglGetError());

    // Android-09, Blit x10 slower whitout
    // Android-19, no diff
    /*
    if (EGL_FALSE == eglWaitGL()) {
        LOGE("s52egl:_egl_beg(): eglWaitGL() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    /* Xoom no diff
    if (EGL_FALSE == eglWaitClient()) {
        LOGE("s52egl:_egl_beg():eglWaitClient() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    /* make sure Android is finish - Xoom no diff
    if (EGL_FALSE == eglWaitNative(EGL_CORE_NATIVE_ENGINE)) {
        LOGE("s52egl:_egl_beg():eglWaitNative() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    /*
    // this prevent EGL_BAD_ACCESS on Adreno/Tegra - eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
    if (EGL_NO_CONTEXT == eglGetCurrentContext()) {
        LOGI("s52egl:_egl_beg(): EGL_NO_CONTEXT .. exit FALSE\n");
        return FALSE;
    }
    //*/

    //*
    if (engine->eglContext != eglGetCurrentContext()) {
        //LOGI("s52egl:_egl_beg(): engine->eglContext ..\n");
        if (EGL_FALSE == eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext)) {
            // eglMakeCurrent() output the same error msg:
            // eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
            LOGE("s52egl:_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
            return FALSE;
        }
    }
    //*/
    /*
    } else {
        LOGI("s52egl:_egl_beg(): NOT engine->eglContext ..\n");
        return FALSE;
    }
    */

#if !defined(S52_USE_MESA3D)
    // Mesa3D 10.1 generate a glError() in _checkError(): from S52_GL_begin() -0-: 0x502 (GL_INVALID_OPERATION)
    // Note: egltrace.so (apitrace) handle it
    if (NULL != _glInsertEventMarkerEXT) {
        //_glInsertEventMarkerEXT(strlen(tag), tag);
        _glInsertEventMarkerEXT(0, tag);
    }
#endif

    return TRUE;
}

static int      _egl_end        (s52engine *engine)
{
    if (EGL_FALSE == eglWaitGL()) {
        LOGE("_egl_end(): eglWaitGL() failed - NO SWAP [0x%x]\n", eglGetError());
        return FALSE;
    }

    //g_timer_reset(_timer);

    if (EGL_TRUE != eglSwapBuffers(engine->eglDisplay, engine->eglSurface)) {
        LOGE("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    //double sec = g_timer_elapsed(_timer, NULL);
    //LOGI("s52egl:_egl_end():eglSwapBuffers(): %.0f msec --------------------------------------\n", sec * 1000);

    return TRUE;
}

static int      _s52_computeView(s52droid_state_t *state)
{
    double S,W,N,E;

    if (FALSE == S52_getCellExtent(NULL, &S, &W, &N, &E))
        return FALSE;

    state->cLat  =  (N + S) / 2.0;
    state->cLon  =  (W + E) / 2.0;
    state->rNM   = ((N - S) / 2.0) * 60.0;  // FIXME: pick dominan projected N-S or E-W
    state->north = 0.0;

    // crossing anti-meridian
    if (W > E)
        state->cLon += 180.0;

#define MAX_RANGE  45.0 * 60.0 // maximum range (NM) [45deg]

    // debug - max out range
    if (state->rNM > MAX_RANGE)
        state->rNM = MAX_RANGE;

    return TRUE;
}

#ifdef USE_FAKE_AIS
static int      _s52_setupVESSEL(s52droid_state_t *state)
{
    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _view.cLat + 0.01, _view.cLon - 0.02, 0.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 0.0);
    //S52_pushPosition(_vessel_ais, state->cLat - 0.04, state->cLon + 0.04, 0.0);
    S52_pushPosition(_vessel_ais, state->cLat - 0.01, state->cLon + 0.01, 45.0);
    S52_setVector(_vessel_ais, 1, 060.0, 16.0);   // ground

    // (re) set label
    S52_setVESSELlabel(_vessel_ais, VESSELLABEL);
    //int vesselSelect = 0;  // OFF
    int vesselSelect = 1;  // ON
    int vestat       = 1; // AIS active
    //int vestat       = 2; // AIS sleeping
    //int vestat       = 3;  // AIS red, close quarters (compile with S52_USE_SYM_VESSEL_DNGHL)
    int vesselTurn   = VESSELTURN_UNDEFINED;
    S52_setVESSELstate(_vessel_ais, vesselSelect, vestat, vesselTurn);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 0.0);

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
    _ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 15.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 100.0, 0.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 0.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 0.0, 100.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 1000.0, 50.0, 15.0, 15.0);

    S52_pushPosition(_ownshp, state->cLat + 0.01, state->cLon - 0.01, 0.0);
    //S52_pushPosition(_ownshp, state->cLat - 0.01, state->cLon - 0.01, 300.0);

    S52_setVector(_ownshp, 0, 290.0, 6.0);  // ownship use S52_MAR_VECSTB

    return TRUE;
}
#endif  // USE_FAKE_AIS

static int      _s52_setupIceRoute(void)
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

    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-68.5,     48.78333, 0.0},  // WP2 - BRAVO
        {-68.0,     49.00,    0.0},  // WP3 - CHARLIE
        {-66.5,     49.5,     0.0}   // WP4 - DELTA

        //{-66.5,     49.0,     0.0}   // WP4 - test horizontal dot
        //{-66.0,     49.5,     0.0}   // WP4 - test vertical dot
    };

    char attVal1[] = "select:2,OBJNAM:ALPHA";    // waypoint on alternate planned route
    char attVal2[] = "select:2,OBJNAM:BRAVO";    // waypoint on alternate planned route
    char attVal3[] = "select:2,OBJNAM:CHARLIE";  // waypoint on alternate planned route
    char attVal4[] = "select:2,OBJNAM:DELTA";    // waypoint on alternate planned route

    _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[0], attVal1);
    _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[1], attVal2);
    _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[2], attVal3);
    _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[3], attVal4);

    // need to turn OFF guard zone because projection not set yet (set via S52_draw())
    double gz = S52_getMarinerParam(S52_MAR_GUARDZONE_BEAM);
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);  // trun off
#define ALT_RTE 2
    // select: alternate (2) legline for Ice Route 2012-02-12T21:00:00Z
    _leglin1 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[0].y, WPxyz[0].x, WPxyz[1].y, WPxyz[1].x, FALSE);
    _leglin2 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[1].y, WPxyz[1].x, WPxyz[2].y, WPxyz[2].x, _leglin1);
    _leglin3 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[2].y, WPxyz[2].x, WPxyz[3].y, WPxyz[3].x, _leglin2);
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, gz);  // trun on

    /*
    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_view.cLon, _view.cLat, 0.0,  _view.cLon + 0.01, _view.cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }
    */

    return TRUE;
}

static int      _s52_setupLEGLIN(s52droid_state_t *state)
{
    (void) state;

    if (FALSE != _leglin4) {
        _leglin4 = S52_delMarObj(_leglin4);
        if (FALSE != _leglin4) {
            LOGI("s52egl:_s52_setupLEGLIN(): delMarObj _leglin4 failed\n");
            g_assert(0);
        }
        // clear alarm
        //S52_setMarinerParam(S52_MAR_ERROR, 0.0);
        S52_setMarinerParam(S52_MAR_GUARDZONE_ALARM, 0.0);
    }

    // test vertical route on DEPCNT
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat - 0.02, state->cLon + 0.00, state->cLat + 0.02, state->cLon + 0.00, NULL);

    // oblique / upbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.01, state->cLon + 0.00, state->cLat + 0.05, state->cLon + 0.02, NULL);

    // oblique - no alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon + 0.00, state->cLat + 0.05, state->cLon + 0.02, NULL);

    // oblique \  upbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat - 0.01, state->cLon + 0.00, state->cLat + 0.02, state->cLon - 0.02, NULL);

    // oblique / downbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon + 0.02, state->cLat - 0.05, state->cLon - 0.02, NULL);

    // oblique \  downbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon - 0.02, state->cLat - 0.02, state->cLon + 0.02, NULL);


    // test LEGLIN setup via cursor
    _leglin4 = S52_newLEGLIN(1, 0.0, 0.0, _leglin4xy[1], _leglin4xy[0], _leglin4xy[3], _leglin4xy[2], FALSE);
    //if (FALSE == _leglin4) {
    //    LOGI("s52egl:_s52_setupLEGLIN(): failed\n");
        if (1.0 == S52_getMarinerParam(S52_MAR_GUARDZONE_ALARM))
            LOGI("s52egl:_s52_setupLEGLIN(): ALARM ON\n");
        if (2.0 == S52_getMarinerParam(S52_MAR_GUARDZONE_ALARM))
            LOGI("s52egl:_s52_setupLEGLIN(): INDICATION ON\n");
    //}

    return TRUE;
}

static int      _s52_setupVRMEBL(s52droid_state_t *state)
{
    //char *attVal   = NULL;      // ordinary cursor
    //char  attVal[] = "cursty:2,_cursor_label:0.0N 0.0W";  // open cursor
    char  attVal[] = "cursty:2";  // open cursor
    double xyz[3] = {state->cLon, state->cLat, 0.0};
    int S52_VRMEBL_vrm = TRUE;
    int S52_VRMEBL_ebl = TRUE;
    int S52_VRMEBL_sty = TRUE;  // normalLineStyle
    int S52_VRMEBL_ori = TRUE;  // (user) setOrigin

    _cursor2 = S52_newMarObj("cursor", S52_POINT, 1, xyz, attVal);

    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    _vrmeblA = S52_newVRMEBL(!S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    if (FALSE == _drawVRMEBL) {
        // suppression ON
        S52_setS57ObjClassSupp("cursor", TRUE);
        S52_setS57ObjClassSupp("ebline", TRUE);
        S52_setS57ObjClassSupp("vrmark", TRUE);

        // or supp one obj
        //S52_toggleDispMarObj(_cursor2);
        //S52_toggleDispMarObj(_vrmeblA);
    }

    return TRUE;
}

static int      _s52_setupPRDARE(s52droid_state_t *state)
// test - centroid (PRDARE: wind farm)
{
    // Note: centroid work on both CW and CCW
    //*
    // AREA (CW)
    double xyzArea[6*3]  = {
        state->cLon + 0.000, state->cLat + 0.000, 0.0,  // SE
        state->cLon - 0.005, state->cLat + 0.004, 0.0,  // center
        state->cLon - 0.010, state->cLat + 0.000, 0.0,  // SW
        state->cLon - 0.010, state->cLat + 0.005, 0.0,  // NW
        state->cLon + 0.000, state->cLat + 0.005, 0.0,  // NE
        state->cLon + 0.000, state->cLat + 0.000, 0.0,  // SE
    };
    //*/

    /*
    // AREA (CCW)
    double xyzArea[6*3]  = {
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
        state->cLon + 0.000, state->cLat + 0.005, 0.0,
        state->cLon - 0.010, state->cLat + 0.005, 0.0,
        state->cLon - 0.010, state->cLat + 0.000, 0.0,
        state->cLon - 0.005, state->cLat + 0.004, 0.0,
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
    };
    */

    // PRDARE/WNDFRM51/CATPRA9
    char attVal[] = "CATPRA:9";
    _prdare = S52_newMarObj("PRDARE", S52_AREAS, 6, xyzArea,  attVal);

    return TRUE;
}

static int      _s52_setupMarPar(void)
{
    // -- DEPTH COLOR ------------------------------------
    S52_setMarinerParam(S52_MAR_TWO_SHADES,      0.0);   // 0.0 --> 5 shades
    //S52_setMarinerParam(S52_MAR_TWO_SHADES,      1.0);   // 1.0 --> 2 shades

    // sounding color
    //S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    15.0);

    S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  10.0);
    //S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  5.0);     // for triggering symb ISODGR01 (ODD winding) at Rimouski
    //S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  3.0);     // for white chanel in Rimouski
    //S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  1.0);

    S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 10.0);
    //S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 5.0);

    //S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   10.0);
    //S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   11.0);
    S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   12.0);

    //S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 0.0);  // (default off)
    S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 1.0);  // ON (GPU expentive)
    // -- DEPTH COLOR ------------------------------------

    S52_setMarinerParam(S52_MAR_SYMBOLIZED_BND, 1.0);  // on (default) [Note: this tax the GPU]
    //S52_setMarinerParam(S52_MAR_SYMBOLIZED_BND, 0.0);  // off

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
    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   5.0);     // DAY 60 - need plib_COLS-3.4-a.rle
    S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   6.0);     // DUSK 60 - need plib_COLS-3.4-a.rle

    //S52_setMarinerParam(S52_MAR_VECPER,         12.0);  // vecper: Vector-length time-period (min) (normaly 6 or 12)
    S52_setMarinerParam(S52_MAR_VECMRK,          1.0);  // vecmrk: Vector time-mark interval (0 - none, 1 - 1&6 min, 2 - 6 min)
    //S52_setMarinerParam(S52_MAR_VECMRK,          2.0);  // vecmrk: Vector time-mark interval (0 - none, 1 - 1&6 min, 2 - 6 min)
    //S52_setMarinerParam(S52_MAR_VECSTB,          0.0);  // vecstb: Vector Stabilization (0 - none, 1 - ground, 2 - water)

    S52_setMarinerParam(S52_MAR_SCAMIN,          1.0);   // ON (default)
    //S52_setMarinerParam(S52_MAR_SCAMIN,          0.0);   // debug OFF - show all

    // remove QUAPNT01 symbole (black diagonal and a '?')
    S52_setMarinerParam(S52_MAR_QUAPNT01,        0.0);   // off

    S52_setMarinerParam(S52_MAR_DISP_CALIB,      1.0);

    // cell's legend
    //S52_setMarinerParam(S52_MAR_DISP_LEGEND, 1.0);   // show
    S52_setMarinerParam(S52_MAR_DISP_LEGEND, 0.0);   // hide (default)

    //S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 0.0);  // OFF
    S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 1.0);  // ON (default)

    S52_setMarinerParam(S52_MAR_ANTIALIAS,       1.0);   // on
    //S52_setMarinerParam(S52_MAR_ANTIALIAS,       0.0);     // off

    // trick to force symbole size
#ifdef S52_USE_TEGRA2
    // smaller on xoom so that proportion look the same
    // as a 'normal' screen - since the eye is closer to the 10" screen of the Xoom
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.3);
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.3);
#endif

#ifdef S52_USE_ADRENO
    // Nexus 7 (2013) [~323 ppi]
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.2);
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.2);
#endif

#if !defined(SET_SCREEN_SIZE) && !defined(S52_USE_ANDROID)
    // NOTE: S52 pixels for symb are 0.3 mm
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.3);
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.3);
#endif

    // a delay of 0.0 to tell to not delete old AIS (default +600 sec old)
    //S52_setMarinerParam(S52_MAR_DISP_VESSEL_DELAY, 0.0);
    // older AIS - case where s52ais reconnect
    // FIXME: maybe check AIS mmsi, so that same ohjH is used!
    S52_setMarinerParam(S52_MAR_DISP_VESSEL_DELAY, 700.0);

    //S52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 0.0); // debug: no NODATA layer
    S52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 1.0);   // default

    //S52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 0.0);  // off (default)
    S52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 1.0);  // on

    //*
    // debug - use for timing rendering
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX);
    //*/

    return TRUE;
}

#ifdef S52_USE_LOG
static int      _s52_error_cb   (const char *err)
{
    LOGI("%s", err);
    return TRUE;
}
#endif

#ifdef S52_USE_RADAR
static guchar  *_s52_radar_cb1  (double *cLat, double *cLng, double *rNM)
{
    *cLat = _engine.state.cLat + 0.01;
    *cLng = _engine.state.cLon - 0.01;

    // Cap Sante
    *cLat = 46.65;
    *cLng = -71.7;

    //*rNM = 12.0;  // rNM
    *rNM = 3.0;  // rNM
    //*rNM = 1.5;  // rNM

    return (unsigned char *)_RADARtex;
    //return (unsigned char *)NULL;
}

static guchar  *_s52_radar_cb2  (double *cLat, double *cLng, double *rNM)
{
    *cLat = _engine.state.cLat - 0.01;
    *cLng = _engine.state.cLon - 0.02;

    //*rNM = 12.0;  // rNM
    //*rNM = 3.0;  // rNM
    *rNM = 1.5;  // rNM

    return (unsigned char *)_RADARtex;
    //return (unsigned char *)NULL;
}
#endif

static int      _s52_init       (s52engine *engine)
{
    LOGI("s52egl:_s52_init(): beg ..\n");

    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        LOGE("_init_S52(): no EGL display ..\n");
        return FALSE;
    }

    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

    // return constant value EGL_UNKNOWN (-1) with Mesa, Adreno, Tegra2
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HORIZONTAL_RESOLUTION, &engine->wmm);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_VERTICAL_RESOLUTION,   &engine->hmm);

    {
        int w   = 0;
        int h   = 0;
        int wmm = 0;
        int hmm = 0;

#ifdef S52_USE_ANDROID
        // Xoom : pixels_w: 1280, pixels_h: 752,  mm_w: 203, mm_h: 101
        // Nexus: pixels_w: 1920, pixels_h: 1200, mm_w: ---, mm_h: --- (323 PPI)
        w   = engine->width;
        h   = engine->height;

        wmm = (int)(w / engine->dpi) * INCH2MM;
        hmm = (int)(h / engine->dpi) * INCH2MM;

#else   // S52_USE_ANDROID

#ifdef SET_SCREEN_SIZE
        // Acer Aspire 5542G - 15.6" HD 1366 x 768 pixels, 16:9 aspect ratio
        w   = engine->width  = 1366;
        h   = engine->height =  768;

        double diagMM        = 15.6 * INCH2MM;   // diagonal mm
        double diagPx        = sqrt(w*w + h*h);  // diagonal pixels
        wmm = engine->wmm    = diagMM/diagPx * w;
        hmm = engine->hmm    = diagMM/diagPx * h;

#else   // SET_SCREEN_SIZE

        // dual-screen: 2646 x 1024 pixels, 700 x 271 mm
        w   = XDisplayWidth   (engine->dpy, 0);
        wmm = XDisplayWidthMM (engine->dpy, 0);
        h   = XDisplayHeight  (engine->dpy, 0);
        hmm = XDisplayHeightMM(engine->dpy, 0);

        //w   = 1280;
        //h   = 1024;
        //hmm = 301; // wrong

        //wmm = engine->wmm    =  376;
        //hmm = engine->hmm    =  307;
        //hmm = engine->hmm    =  200;
        //w   = engine->width  = 1280;
        //h   = engine->height =  693;

#endif  // SET_SCREEN_SIZE
#endif  // S52_USE_ANDROID

#ifdef S52_USE_LOG
        // Nexus: no root, can't do: $ su -c "setprop log.redirect-stdio true"
        if (FALSE == S52_init(w, h, wmm, hmm, _s52_error_cb))
#else
        if (FALSE == S52_init(w, h, wmm, hmm, NULL))
#endif
        {
            LOGE("ERROR:_init_S52():S52_init(%i,%i,%i,%i)\n", w, h, wmm, hmm);
            g_assert(0);
            exit(0);
            return FALSE;
        }

        //LOGI("s52egl:_init_S52():S52_init(%i,%i,%i,%i)\n", w, h, wmm, hmm);

        // init() will do that by default at startup
        //S52_setViewPort(0, 0, w, h);
    }

    // load PLib in s52.cfg
    //S52_loadPLib(NULL);

    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    // Inland Waterway rasterization rules (form OpenCPN)
    //S52_loadPLib("S52RAZDS.RLE");


#ifdef S52_USE_ANDROID

    // set GDAL data path
    g_setenv("S57_CSV", "/sdcard/s52droid/gdal_data", 1);

    // read cell location fron s52.cfg
    //S52_loadCell(NULL, NULL);

    // Tadoussac
    //S52_loadCell(PATH "/ENC_ROOT/CA379035.000", NULL);
    // load all 3 S57 charts
    //S52_loadCell(PATH "/ENC_ROOT", NULL);

    // Rimouski
    //S52_loadCell(PATH "/ENC_ROOT_RIKI/CA579041.000", NULL);
    // Estuaire du St-Laurent
    S52_loadCell(PATH "/ENC_ROOT_RIKI/CA279037.000", NULL);

    // Bec
    //S52_loadCell(PATH "/ENC_ROOT/CA579016.000", NULL);

    // Portneuf
    //S52_loadCell(PATH "/ENC_ROOT/CA479017.000", NULL);
    //S52_loadCell(PATH "/bathy/SCX_CapSante.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

#else  // S52_USE_ANDROID

    // Note:
    // GDAL profile (s57attributes_<profile>.csv, s57objectclasses_<profile>.csv)
    // iw: Inland Waterway profile add object classe OBCL:17000-17065 (and attributes)
    //g_setenv("S57_PROFILE", "iw", 1);
    //g_setenv("S57_PROFILE", "iw2", 1);
    // GDAL debug info ON
    //g_setenv("CPL_DEBUG", "ON", 1);

    // read cell location fron s52.cfg
    //S52_loadCell(NULL, NULL);

    // S-64 ENC
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/GB5X01SE.000", NULL);

    // debug anti-meridian
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/US5HA06M/US5HA06M.000", NULL);
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/US1EEZ1M/US1EEZ1M.000", NULL);

    // Rimouski
    S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000", NULL);

    // load PLib in s52.cfg
    //S52_loadPLib(NULL);

    // Estuaire du St-Laurent
    S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000", NULL);

    //Tadoussac
    //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA379035.000", NULL);

    // Ice - experimental (HACK: ice symb link to --0WORLD.shp for one shot test)
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // ------------ Bathy HD for CA479017.000 ----------------------
    // Bathy - experimental Cap Sante / Portneuf
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479017.000", NULL);
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479020.000", NULL);

    //S52_loadCell(PATH "/bathy/SCX_CapSante.tif", NULL);  // old test bathy

    // bathy overlapping CA479017.000 and CA479020.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4666N7170W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7170W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7170W_5.tiff", NULL);

    // CA479017.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7172W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7172W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7174W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7174W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7176W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7176W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7178W_5.tiff", NULL);

    // ------------ Bathy HD for CA579016.000 ----------------------
    // ENC Bec
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA579016.000", NULL);
    // ENC TRV
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479014.000", NULL);


    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4642N7236W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4640N7238W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4642N7238W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4640N7240W_5.tiff", NULL);

    // West of CA479017.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4636N7246W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4638N7246W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4636N7248W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4638N7248W_5.tiff", NULL);


    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

    // RADAR - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/radar/RADAR_imitator/out.raw", NULL);


#endif  // S52_USE_ANDROID

#ifdef S52_USE_WORLD
    // World data
    if (TRUE == S52_loadCell(PATH "/0WORLD/--0WORLD.shp", NULL)) {
        //S52_setMarinerParam(S52_MAR_DISP_WORLD, 0.0);   // default
        S52_setMarinerParam(S52_MAR_DISP_WORLD, 1.0);     // show world
    }
#endif


    // debug - remove clutter from this symb in SELECT mode
    S52_setS57ObjClassSupp("M_QUAL", TRUE);  // suppress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // display the U pattern

    S52_setS57ObjClassSupp("M_NSYS", TRUE);   // boundary between IALA-A and IALA-B systems (--A--B--, LC(MARSYS51))

    // CS DATCVY01:M_COVR:CATCOV=2, OTHER
    //S52_setMarinerParam(S52_MAR_DISP_HODATA, 2.0);  // draw all M_COVR individualy
    //CS DATCVY01:M_COVR:CATCOV=1, BASE
    //S52_setMarinerParam(S52_MAR_DISP_HODATA, 1.0);  // union: combite M_COVR as one poly 'm_covr'
    S52_setS57ObjClassSupp("M_COVR", TRUE);         // HO data limit __/__/__ - LC(HODATA01)

    _s52_setupMarPar();

    //S52_setTextDisp(0, 100, TRUE);                // show all text (default)
    S52_setMarinerParam(S52_MAR_SHOW_TEXT,   1.0);  // default
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT, 0.0);

    // if first start, find where we are looking
    _s52_computeView(&engine->state);
    // then (re)position the 'camera'
    S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    // debug - anti-meridian
    //engine->state.rNM = 2300.0;
    //S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    S52_newCSYMB();

    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL
    _s52_setupVRMEBL(&engine->state);

    // set route
    _s52_setupIceRoute();

    // can't check guard zone here because projection not set yet (set via S52_draw())
    //_s52_setupLEGLIN(&engine->state);

    // wind farme for testing centroids in a concave poly
    //_s52_setupPRDARE(&engine->state);


#ifdef USE_FAKE_AIS
    _s52_setupOWNSHP(&engine->state);
    _s52_setupVESSEL(&engine->state);
#endif

#ifdef S52_USE_EGL
    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, engine);
#endif

#ifdef USE_AIS
    s52ais_initAIS();
#endif

#ifdef S52_USE_RADAR
    _radar_init();
    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);
    S52_setRADARCallBack(_s52_radar_cb1, Rmax);
    S52_setRADARCallBack(_s52_radar_cb2, Rmax);
#endif

    engine->do_S52draw        = TRUE;
    engine->do_S52drawLast    = TRUE;
    engine->do_S52drawBlit    = FALSE;
    engine->do_S52setViewPort = FALSE;

    S52_version();

    // debug: should fail
    //S52_drawStr(100, engine->height - 100, "CURSR", 1, "Test S52_drawStr()");

    LOGI("Palettes: %s\n", S52_getPalettesNameList());

    LOGI("s52egl:_s52_init(): end ..\n");

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
#endif  // USE_FAKE_AIS

static int      _s52_draw_user  (s52engine *engine)
{
    (void) engine; // quiet compiler

    /*
    // debug - S57 obj ID of Becancour Cell (CA579016.000)
    {
        static int takeScreenShot = TRUE;
        if (TRUE == takeScreenShot) {
            //S52_dumpS57IDPixels("test.png", 954, 200, 200); // waypnt
            //S52_dumpS57IDPixels("test.png", 556, 200, 200); // land
            S52_dumpS57IDPixels("/sdcard/s52droid/test.png", 0, 200, 200);
        }
        takeScreenShot = FALSE;
    }
    */

    // test
    //S52_drawStr(100, engine->height - 100, "CURSR", 1, "Test S52_drawStr()");

    return TRUE;
}

static int      _s52_draw_cb    (gpointer user_data)
{
    s52engine *engine = (s52engine*)user_data;

    // debug
    //LOGI("s52egl:_s52_draw_cb(): beg .. \n");

    if (NULL == engine) {
        LOGE("s52egl:_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    /*
    if (engine->idle_id) {
        if (TRUE == g_source_remove(engine->idle_id)) {
            engine->idle_id = 0;
        } else {
            LOGE("s52egl:_s52_draw_cb():g_source_remove() faild\n");
            goto exit;
        }
    }
    */

    if (EGL_NO_SURFACE == engine->eglSurface) {
        LOGE("_s52_draw_cb(): no Surface ..\n");
        goto exit;
    }

    if (EGL_NO_DISPLAY == engine->eglDisplay) {
        LOGE("_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    //*
    if (TRUE == engine->do_S52drawBlit) {
        S52_drawBlit(engine->state.dx_pc, engine->state.dy_pc, engine->state.dz_pc, engine->state.dw_pc);
        engine->do_S52drawBlit = FALSE;
        goto exit;
    }
    //*/


    //*
    if (TRUE == engine->do_S52setViewPort) {
#ifdef S52_USE_ADRENO
        // EGL viewport not updated after rotation on Nexus!
        engine->width  = ANativeWindow_getWidth (engine->app->window);
        engine->height = ANativeWindow_getHeight(engine->app->window);
#else
        eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
        eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);
#endif
        LOGI("s52egl:_s52_draw_cb(): w:%i, h:%i\n", engine->width, engine->height);
        S52_setViewPort(0, 0, engine->width, engine->height);
        engine->do_S52setViewPort = FALSE;
    }
    //*/


#if !defined(S52_USE_EGL)
    _egl_beg(engine, "test");
#endif

    // draw background - IHO layer 0-8
    if (TRUE == engine->do_S52draw) {
#ifdef S52_USE_RADAR
        // read 10 lines (or 360 deg == 2048 lines, so 10 lines == 1.7 deg per sector per 0.1 sec)
        _radar_readLog(10);  // seem like nice rotation speed
#else
        engine->do_S52draw = FALSE;
#endif
        S52_draw();

        // user can add stuff on top of draw()
        _s52_draw_user(engine);
    }

    // draw AIS on last layer (IHO layer 9)
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
    //LOGI("s52egl:_s52_draw_cb(): end .. \n");

    return TRUE;
}

#ifdef S52_USE_ANDROID
//----------------------------------------------
//
// android specific code
//

#if 0
// DEPRECATED
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
#endif

#ifdef S52AIS_STANDALONE
static int      _android_init_external_ais(void)
// when s52ais.c is NOT linked to s52egl.c
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
// DEPRECATED:
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
#endif

static int      _android_init_external_UI(s52engine *engine)
// start Android HTML5 UI - also get GPS & Gyro from Android
{
    const gchar cmd[] =
        "/system/bin/sh -c \"             "
        "/system/bin/am start --user 0    "
        "-a android.intent.action.MAIN    "
        "-n nav.ecs.s52droid/.s52ui   \"  ";

    int ret = g_spawn_command_line_async(cmd, NULL);
    if (FALSE == ret) {
        g_print("_android_init_external_UI(): fail to start UI\n");
        return FALSE;
    } else {
        g_print("_android_init_external_UI(): UI started ..\n");

        // stop drawing loop - interfer with S52 view in Touch UI
        engine->do_S52draw     = FALSE;
        engine->do_S52drawLast = FALSE;
    }

    return TRUE;
}

#if 0
// BROKEN
static int      _android_done_external_UI(s52engine *engine)
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
#endif

#if 0
static int      _android_sensors_gyro(gpointer user_data)
// Android Native poll event
{
    s52engine* engine = (s52engine*)user_data;

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

//static int      _android_display_init(s52engine *engine)
static gpointer _android_display_init(gpointer user_data)
{
    s52engine *engine = (s52engine*)user_data;

    if (FALSE == _egl_init(engine)) {
        LOGI("DEBUG: EGL allready up\n");
        return FALSE;
    }
    if (FALSE == _s52_init(engine)) {
        LOGI("DEBUG: S52 allready up\n");
        return FALSE;
    }

    engine->do_S52draw     = TRUE;
    engine->do_S52drawLast = TRUE;

    _s52_draw_cb(user_data);

    g_timeout_add(500, _s52_draw_cb, user_data);  // 0.5 sec
    // 60 fps -> 1000 / 60 = 16.666 msec
    //g_timeout_add(1000/60, _s52_draw_cb, user_data);  // 16 msec

    // debug - init s52ui (HTML5) right at the start
    _android_init_external_UI(engine);

    // debug - test with rendering thread
    //GMainContext *c   = g_main_context_get_thread_default();
    //engine->main_loop = g_main_loop_new(c, FALSE);
    //engine->main_loop = g_main_loop_new(NULL, FALSE);
    //engine->queue     = g_async_queue_new();

    //engine->idle_id   = g_idle_add(_s52_draw_cb, user_data);

    // start draw loop
    //g_main_loop_run(engine->main_loop);

    return NULL;
}

static int      _android_signalDraw  (s52engine *engine, double new_y, double new_x, double new_z, double new_r)
{
    /*
    // debug - test optimisation using viewPort to draw area
    if (engine->state.rNM == new_z) {
        S52_setViewPort(0, 0, 100, 100);
    }
    //*/

    if (TRUE == S52_setView(new_y, new_x, new_z, new_r)) {
        engine->state.cLat  = new_y;
        engine->state.cLon  = new_x;
        engine->state.rNM   = new_z;
        engine->state.north = new_r;

#ifdef S52_USE_EGL
        engine->do_S52draw     = TRUE;
        engine->do_S52drawLast = FALSE;
        _s52_draw_cb(engine);

#else
        _egl_beg(engine, "test");
        _s52_draw_cb(engine);
        _egl_end(engine);
#endif

    }

    return TRUE;
}
//#endif

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
//#define EDGE_X0       50   // 0 at left
//#define EDGE_Y0       50   // 0 at top
#define EDGE_X0      100   // 0 at left (Nexus 7)
#define EDGE_Y0      100   // 0 at top  (Nexus 7)
#define DELTA          5

    //int EDGE_X1 = engine->width - 150;


    int32_t actraw = AMotionEvent_getAction(event);
    int32_t action = actraw & AMOTION_EVENT_ACTION_MASK;

    switch (action) {

    case AMOTION_EVENT_ACTION_DOWN:
        // stop _s52_draw_cb() from drawing
        engine->do_S52draw     = FALSE;
        engine->do_S52drawLast = FALSE;

        ticks = 0;

        start_x = AMotionEvent_getX(event, 0);
        start_y = AMotionEvent_getY(event, 0);

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
                        S52_setS57ObjClassSupp("cursor", FALSE);
                        S52_setS57ObjClassSupp("ebline", FALSE);
                    } else {
                        S52_setS57ObjClassSupp("cursor", TRUE);
                        S52_setS57ObjClassSupp("ebline", TRUE);
                    }
                    mode_vrmebl_set = TRUE;
                }
            } else {
                // motion - find is scroll or vrmebl
                if (TRUE!=mode_vrmebl_set && FALSE==mode_zoom && FALSE==mode_zoom)
                    mode_scroll = TRUE;
            }

            // blit start
#if !defined(S52_USE_EGL)
            _egl_beg(engine, "test");
#endif

            //g_static_mutex_lock(&engine->mutex);
            if (TRUE == mode_rot) {
                double north = engine->state.north + (90.0 * ((start_x - new_x) / engine->width));
                if (north <    0.0) north += 360.0;
                if (north >= 360.0) north -= 360.0;

                //S52_drawBlit(0.0, 0.0, 0.0, north);
                //LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_MOVE: north=%f start_y=%f new_y=%f width=%i\n",
                //     north, start_y, new_y, engine->width);

                //*
                engine->state.dx_pc = 0.0;
                engine->state.dy_pc = 0.0;
                engine->state.dz_pc = 0.0;
                engine->state.dw_pc = north;
                engine->do_S52draw  = FALSE;
                engine->do_S52drawLast = FALSE;
                engine->do_S52drawBlit = TRUE;
                //g_async_queue_push(engine->queue, engine);
                _s52_draw_cb(engine);
                //*/

            }

            if (TRUE == mode_zoom) {
                double dz_pc = (start_y - new_y) / engine->height;
                //LOGI("s52egl:_android_motion_event(): start_y=%f new_y=%f height=%i dz_pc=%f\n", start_y, new_y, engine->height, dz_pc);
                //if (TRUE == S52_drawBlit(0.0, 0.0, dz_pc, 0.0))
                    zoom_fac = dz_pc;

                //*
                engine->state.dx_pc = 0.0;
                engine->state.dy_pc = 0.0;
                engine->state.dz_pc = (start_y - new_y) / engine->height;
                engine->state.dw_pc = 0.0;

                engine->do_S52draw     = FALSE;
                engine->do_S52drawLast = FALSE;
                engine->do_S52drawBlit = TRUE;
                //g_async_queue_push(engine->queue, engine);
                _s52_draw_cb(engine);
                //*/
            }

            /*
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
            */

            if (TRUE==mode_scroll && FALSE==mode_zoom && FALSE==mode_rot && FALSE==mode_vrmebl) {
                //double dx_pc =  (start_x - new_x) / engine->width;  //
                //double dy_pc = -(start_y - new_y) / engine->height; // Y down
                //S52_drawBlit(dx_pc, dy_pc, 0.0, 0.0);

                //*
                engine->state.dx_pc =  (start_x - new_x) / engine->width;
                engine->state.dy_pc = -(start_y - new_y) / engine->height; // Y down
                engine->state.dz_pc = 0.0;
                engine->state.dw_pc = 0.0;
                engine->do_S52draw  = FALSE;
                engine->do_S52drawLast = FALSE;
                engine->do_S52drawBlit = TRUE;
                //g_async_queue_push(engine->queue, engine);
                _s52_draw_cb(engine);
                //*/
            }

            //g_static_mutex_unlock(&engine->mutex);

            // blit end
#if !defined(S52_USE_EGL)
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

        // short tap - cursor pick
        /*
        if (ticks < TICKS_PER_TAP) {
            new_x = (new_x < 10.0) ? 10.0 : new_x;
            new_y = (new_y < 10.0) ? 10.0 : new_y;
            const char *nameid = S52_pickAt(new_x, engine->height - new_y);
            if (NULL != nameid) {
                unsigned int S57ID = atoi(nameid+7);
                LOGI("s52egl:_android_motion_event(): XY(%f, %f): NAME:ID=%s attList(%s)\n",
                     new_x, engine->height-new_y, nameid, S52_getAttList(S57ID));

                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
                new_z = engine->state.rNM;
                new_r = engine->state.north;

                _android_signalDraw(engine, new_y, new_x, new_z, new_r);
            }
            return TRUE;
        }
        */

        /*
        // long tap
        //if (ticks > TICKS_PER_TAP) {
            // no motion
            if ((ABS(start_x - new_x) < 5) && (ABS(start_y - new_y) < 5)) {
                // ensure that a minimum number of call to libS52
                // to turn vrmebl ON/OFF
                if (TRUE == mode_vrmebl_set) {
                    mode_vrmebl = (TRUE==mode_vrmebl) ? FALSE : TRUE;
                    if (TRUE == mode_vrmebl) {
                        S52_setS57ObjClassSupp("cursor", FALSE);
                        S52_setS57ObjClassSupp("ebline", FLASE);
                    } else {
                        S52_setS57ObjClassSupp("cursor", TRUE);
                        S52_setS57ObjClassSupp("ebline", TRUE);
                    }
                    mode_vrmebl_set = FALSE;
                }
                return TRUE;
            }
        //}
        */

        // touch UL (North Arrow) reset chart upright
        if ((new_x < EDGE_X0) && (new_y < EDGE_Y0) && (start_x < EDGE_X0) && (start_y < EDGE_Y0)) {
            LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_UP: north=%f\n", new_r);

            new_x = engine->state.cLon;
            new_y = engine->state.cLat;
            new_z = engine->state.rNM;
            new_r = 0.0;

            // debug - init s52ui (HTML5)
            //_android_init_external_UI(engine);

        } else {
            if (TRUE == mode_rot) {
                double north = engine->state.north + (90.0 * ((start_x - new_x) / engine->width));
                if (north <    0.0) north += 360.0;
                if (north >= 360.0) north -= 360.0;

                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
                new_z = engine->state.rNM;
                new_r = north;
                //LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_UP: north=%f\n", north);
            }

            if (TRUE == mode_zoom) {
                new_x = engine->state.cLon;
                new_y = engine->state.cLat;
                new_z = engine->state.rNM - (zoom_fac * engine->state.rNM * 2); // FIXME: where is the 2 comming from?
                //new_z = engine->state.rNM - (engine->state.dz_pc * engine->state.rNM * 2); // FIXME: where is the 2 comming from?
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
            }
        }

        // signal loop to handle drawing
        _android_signalDraw(engine, new_y, new_x, new_z, new_r);

        LOGI("s52egl:_android_motion_event():AMOTION_EVENT_ACTION_UP: north=%f\n", new_r);
    }
    break;

    }

#ifdef S52_USE_RADAR
    engine->do_S52draw     = TRUE;
#else
    // normal mode
    engine->do_S52draw     = FALSE;
#endif

    engine->do_S52drawLast = TRUE;

    return TRUE;
}

static int32_t  _android_handle_input(struct android_app *app, AInputEvent *event)
// Process the next input event.
// Return 1 the event was handled, 0 for any default dispatching.
{
    s52engine *engine = (s52engine*)app->userData;

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

            //if (AKEYCODE_MENU==code && 0==action) {
            if (AKEYCODE_BACK==code && 0==action) {
                _android_init_external_UI(engine);

                // remove VRMEBL (no use in s52ui)
                S52_setS57ObjClassSupp("cursor", TRUE);
                S52_setS57ObjClassSupp("ebline", TRUE);
            }

            /*
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

            if (NULL == engine->app->window)
                LOGI("s52egl:APP_CMD_RESUME: ANativeWindow is NULL\n");
            else
                LOGI("s52egl:APP_CMD_RESUME: ANativeWindow is NOT NULL\n");

            if (NULL == engine->app->savedState)
                LOGI("s52egl:APP_CMD_RESUME: savedState is NULL\n");
            else
                LOGI("s52egl:APP_CMD_RESUME: savedState is NOT NULL\n");

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
                g_main_loop_quit(engine->main_loop);
            }

            *((s52droid_state_t*)engine->app->savedState) = engine->state;

            break;
        }
        case APP_CMD_INIT_WINDOW: {
            // window is being shown, get it ready
            LOGI("s52egl:--> APP_CMD_INIT_WINDOW\n");

            if (EGL_NO_CONTEXT != engine->eglContext) {
                // debug
                if (NULL == engine->app->window)
                    LOGI("s52egl:APP_CMD_INIT_WINDOW: ANativeWindow is NULL\n");
                else
                    LOGI("s52egl:APP_CMD_INIT_WINDOW: ANativeWindow is NOT NULL\n");

                //LOGI("APP_CMD_INIT_WINDOW: before eglCreateWindowSurface():EGL error [0x%x]\n", eglGetError());
                engine->eglSurface = eglCreateWindowSurface(engine->eglDisplay, engine->eglConfig, engine->app->window, NULL);
                //engine->eglSurface = eglCreateWindowSurface(engine->eglDisplay, engine->eglConfig, app->window, NULL);
                //LOGI("APP_CMD_INIT_WINDOW: EGL error [0x%x]\n", eglGetError());
                eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext);
            } else {
                _android_display_init(engine);
            }

            break;
        }
        case APP_CMD_TERM_WINDOW: {
            LOGI("s52egl:--> APP_CMD_TERM_WINDOW\n");

            // window hidden or closed
            engine->do_S52draw     = FALSE;
            engine->do_S52drawLast = FALSE;

            //_android_done_external_sensors();

            _egl_doneSurface(engine);

            break;
        }
        case APP_CMD_GAINED_FOCUS: {
            // app (re) gains focus
            LOGI("s52egl:--> APP_CMD_GAINED_FOCUS\n");

            if (NULL == engine->app->window) {
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: ANativeWindow is NULL\n");
            } else {
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: ANativeWindow is NOT NULL\n");
            }

            if (NULL == engine->app->savedState)
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: savedState is NULL\n");
            else
                LOGI("s52egl:APP_CMD_GAINED_FOCUS: savedState is NOT NULL\n");

            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;

            break;
        }
        case APP_CMD_LOST_FOCUS: {
            // app loses focus, stop monitoring sensor
            // to avoid consuming battery while not being used.
            LOGI("s52egl:--> APP_CMD_LOST_FOCUS\n");

            // the UI now handle the draw
            engine->do_S52draw     = FALSE;
            engine->do_S52drawLast = FALSE;


            break;
        }
        case APP_CMD_CONFIG_CHANGED: {
            // device rotated (callbacks onConfigurationChanged must be NULL)
            LOGI("s52egl:--> APP_CMD_CONFIG_CHANGED\n");
            int32_t confDiff = AConfiguration_diff(engine->config, engine->app->config);
            LOGI("s52egl: config diff: 0x%04x\n", confDiff);
            _android_config_dump(engine->app->config);
            AConfiguration_copy(engine->config, engine->app->config);

            // no AIS update with drawLast()
            //if (FALSE == engine->do_S52drawLast)
            //    break;

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
                LOGI("s52egl:DEBUG (check this): --> APP_CMD_DESTROY: destroyRequested flags is set\n");
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
{
    LOGI("s52egl:_onConfigurationChanged(): beg ..\n");

    //g_static_mutex_lock(&_engine.mutex);

    _engine.do_S52setViewPort = TRUE;
    _engine.do_S52draw        = TRUE;
    _engine.do_S52drawLast    = TRUE;

    /*
    // EGL too slow
    _engine.width  = ANativeWindow_getWidth (_engine.app->window);
    _engine.height = ANativeWindow_getHeight(_engine.app->window);
    LOGI("s52egl:_onConfigurationChanged(): w:%i, h:%i\n", _engine.width, _engine.height);
    S52_setViewPort(0, 0, _engine.width, _engine.height);
    //engine->do_S52setViewPort = FALSE;
    //*/

    {
        int32_t confDiff = AConfiguration_diff(_engine.config, _engine.app->config);
        LOGI("s52egl: config diff: 0x%04x\n", confDiff);
        _android_config_dump(_engine.app->config);
        AConfiguration_copy(_engine.config, _engine.app->config);
    }

    //g_static_mutex_unlock(&_engine.mutex);

    return;
}

static void     _onLowMemory(ANativeActivity *activity)
{
    LOGI("s52egl:_onLowMemory(): beg ..\n");

    return;
}

#if 0
static void     _onNativeWindowResized(ANativeActivity* activity, ANativeWindow* window)
{
    //LOGI("s52egl:_onNativeWindowResized(): beg ..\n");

    //g_static_mutex_lock(&_engine.mutex);

    _engine.do_S52setViewPort = TRUE;
    _engine.do_S52draw        = TRUE;
    _engine.do_S52drawLast    = TRUE;

    //g_static_mutex_unlock(&_engine.mutex);

    return;
}
#endif

void     android_main(struct android_app *app)
// This is the main entry point of a native application that is using
// android_native_app_glue.  It runs in its own thread, with its own
// event loop for receiving input events and doing other things.
{
    // debug - provoke a sigsegv that android's debugerd should catch
	//int *p = 0x0;
    //*p = 1;

    LOGI("s52egl:android_main(): starting ..\n");

    // from android_native_app_glue.h
    // * The 'threaded_native_app' static library is used to provide a different
    // * execution model where the application can implement its own main event
    // * loop in a different thread instead.

    // Make sure glue isn't stripped.
    app_dummy();

    memset(&_engine, 0, sizeof(_engine));

    //_engine.mutex = G_STATIC_MUTEX_INIT;  // protect engine
    //g_static_mutex_init(&_engine.mutex);

    app->userData     = &_engine;
    app->onAppCmd     = _android_handle_cmd;
    app->onInputEvent = _android_handle_input;

    _engine.app        = app;
    _engine.dpi        = AConfiguration_getDensity(app->config);
    _engine.config     = AConfiguration_new();
    _engine.configBits = AConfiguration_diff(app->config, _engine.config);
    AConfiguration_copy(_engine.config, app->config);

    // setup callbacks to detect android device orientation and other test
    _engine.callbacks  = _engine.app->activity->callbacks;
    _engine.callbacks->onConfigurationChanged = _onConfigurationChanged;
    //_engine.callbacks->onNativeWindowResized  = _onNativeWindowResized;
    _engine.callbacks->onLowMemory            = _onLowMemory;

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

    _timer = g_timer_new();

    if (NULL != _engine.app->savedState) {
        // if re-starting - the process is already up
        _engine.state = *(s52droid_state_t*)app->savedState;

        LOGI("s52egl:DEBUG: bypassing _init_S52(), reset state .. \n" );
        LOGI("s52egl:       cLat =%f\n", _engine.state.cLat           );
        LOGI("s52egl:       cLon =%f\n", _engine.state.cLon           );
        LOGI("s52egl:       rNM  =%f\n", _engine.state.rNM            );
        LOGI("s52egl:       north=%f",   _engine.state.north          );

        // check if main loop up (seem useless)
        if (FALSE == g_main_loop_is_running(_engine.main_loop)) {
            LOGI("s52egl:engine.view.main_loop is running .. reconnecting ..\n");
            g_main_loop_run(_engine.main_loop);
        }
    } else {
        _engine.main_loop = g_main_loop_new(NULL, FALSE);
    }

    // Damien's glib main loop (http://damien.lespiau.name/blog/)
    //LOGI("s52egl:starting g_main_loop_run() ..\n");
    //g_android_init();
    //g_main_loop_run(engine.state.main_loop);
    //LOGI("s52egl:exiting g_main_loop_run() ..\n");

    // android main loop - read msg to free android msg queue
    LOGI("s52egl:android_main(): while loop start\n");
    while (1) {
        int ident;
        int events;
        struct android_poll_source* source;

        // read all pending events (0 --> no wait).
        //while ((ident = ALooper_pollAll(-1, NULL, &events, (void**) &source)) >= 0) {
        while (0 <= (ident = ALooper_pollAll(0, NULL, &events, (void**) &source))) {
            // Process this event.
            if (source != NULL)
                source->process(app, source);

            // Check if we are exiting.
            if (0 != _engine.app->destroyRequested) {
                LOGI("s52egl:android_main(): IN while loop .. destroyRecquested\n");
                goto exit;
            }
        }

        // process all glib pending events
        while(g_main_context_iteration(NULL, FALSE))
            ;

        // slow down the loop to 1% CPU (without it then 50% CPU)
        g_usleep(10 * 1000);  // 0.01 sec
    }


exit:


    //_android_done_external_sensors();

#ifdef USE_AIS
    s52ais_doneAIS();
#endif

    _s52_done(&_engine);

    _egl_done(&_engine);

    AConfiguration_delete(_engine.config);

    LOGI("s52egl:android_main(): exiting ..\n");

    return;
}

#else  // end of S52_USE_ANDROID

//----------------------------------------------
//
// X11 specific code
// for testing EGL / GLES2 outside of android
//

#include </usr/include/X11/XKBlib.h>  // XkbKeycodeToKeysym()
static int      _s52_setVwNDraw (s52engine *engine, double new_y, double new_x, double new_z, double new_r)
// set View then call draw_cb
{
    //*
    if (ABS(new_x) > 180.0) {
        if (new_x > 0.0)
            new_x = new_x - 360.0;
        else
            new_x = new_x + 360.0;

    }
    //*/

    if (TRUE == S52_setView(new_y, new_x, new_z, new_r)) {
        engine->state.cLat  = new_y;
        engine->state.cLon  = new_x;
        engine->state.rNM   = new_z;
        engine->state.north = new_r;

        engine->do_S52draw     = TRUE;
        engine->do_S52drawLast = TRUE;

        //g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0, NULL);
        _s52_draw_cb((gpointer) engine);
        return TRUE;
    }

    return FALSE;
}

static int      _X11_error(Display *display, XErrorEvent *err)
{
    char buf[80] = {'\0'};
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
            //g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0);
            _s52_draw_cb((gpointer) engine);
            g_print("DEBUG: Expose Event\n");

            break;

        case MotionNotify:
            {
                if (FALSE == _drawVRMEBL)
                    break;

                XMotionEvent *mouseEvent = (XMotionEvent *)&event;
                double Xlon = mouseEvent->x;
                double Ylat = engine->height - mouseEvent->y;

                S52_setVRMEBL(_vrmeblA, Xlon, Ylat, NULL, NULL);

                if (TRUE == S52_xy2LL(&Xlon, &Ylat)) {
                    S52_pushPosition(_cursor2, Ylat, Xlon, 0.0);

                    engine->do_S52draw     = FALSE;
                    engine->do_S52drawLast = TRUE;
                    _s52_draw_cb((gpointer) engine);
                }
            }
            break;

        case ButtonPress:
            {
                if (FALSE == _drawVRMEBL)
                    break;

                XMotionEvent *mouseEvent = (XMotionEvent *)&event;
                double Xlon = mouseEvent->x;
                double Ylat = engine->height - mouseEvent->y;

                // first call set the origine
                S52_setVRMEBL(_vrmeblA, Xlon, Ylat, NULL, NULL);

                if (TRUE == S52_xy2LL(&Xlon, &Ylat)) {
                    S52_pushPosition(_cursor2, Ylat, Xlon, 0.0);
                    _leglin4xy[1] = Ylat;
                    _leglin4xy[0] = Xlon;
                }
            }
            break;

        case ButtonRelease:
            {
                XButtonReleasedEvent *mouseEvent = (XButtonReleasedEvent *)&event;

                // test pick
                //*
                const char *name = S52_pickAt(mouseEvent->x, engine->height - mouseEvent->y);
                if (NULL != name) {
                    unsigned int S57ID = atoi(name+7);
                    g_print("OBJ(%i, %i): %s\n", mouseEvent->x, engine->height - mouseEvent->y, name);
                    g_print("AttList=%s\n", S52_getAttList(S57ID));

                    {   // debug:  S52_xy2LL() --> S52_LL2xy() should be the same
                        // NOTE:  LL (0,0) is the OpenGL origine (not X11 origine)
                        double Xlon = 0.0;
                        double Ylat = 0.0;
                        S52_xy2LL(&Xlon, &Ylat);
                        S52_LL2xy(&Xlon, &Ylat);
                        g_print("DEBUG: xy2LL(0,0) --> LL2xy ==> Xlon: %f, Ylat: %f\n", Xlon, Ylat);
                    }

                    if (0 == g_strcmp0("vessel", name)) {
                        g_print("vessel found\n");
                        unsigned int S57ID = atoi(name+7);

                        S52ObjectHandle vessel = S52_getMarObj(S57ID);
                        if (0 != vessel) {
                            int vesselSelect = 1;  // ON
                            int vestat       = 0;  // AIS state undifined
                            S52_setVESSELstate(vessel, vesselSelect, vestat, VESSELTURN_UNDEFINED);
                            //g_print("AttList: %s\n", S52_getAttList(S57ID));
                        }
                    }
                }
                //*/

                // test LEGLIN on obstruction
                /*
                if (FALSE == _drawVRMEBL)
                    break;

                double Xlon = mouseEvent->x;
                double Ylat = engine->height - mouseEvent->y;

                S52_setVRMEBL(_vrmeblA, mouseEvent->x, engine->height - mouseEvent->y, NULL, NULL);

                if (TRUE == S52_xy2LL(&Xlon, &Ylat)) {
                    S52_pushPosition(_cursor2, Ylat, Xlon, 0.0);

                    //
                    _leglin4xy[3] = Ylat;
                    _leglin4xy[2] = Xlon;
                    _s52_setupLEGLIN(&engine->state);

                    // call to draw needed as LEGLIN is on layer 5
                    engine->do_S52draw     = TRUE;
                    engine->do_S52drawLast = TRUE;
                    _s52_draw_cb((gpointer) engine);
                }
                */
            }
            break;

        case KeyPress:
        case KeyRelease: {
            // /usr/include/X11/keysymdef.h
            unsigned int keycode = ((XKeyEvent *)&event)->keycode;
            unsigned int keysym  = XkbKeycodeToKeysym(engine->dpy, keycode, 0, 1);

            // FIXME: use switch on keysym

            // ESC - quit
            if (XK_Escape == keysym) {
                g_main_loop_quit(engine->main_loop);
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
                _drawVRMEBL = !_drawVRMEBL;
                if (TRUE == _drawVRMEBL) {
                    S52_setS57ObjClassSupp("cursor", FALSE);
                    S52_setS57ObjClassSupp("ebline", FALSE);
                    S52_setS57ObjClassSupp("vrmark", FALSE);

                    //S52_pushPosition(_cursor2, engine->state.cLat, engine->state.cLon, 0.0);

                } else {
                    S52_setS57ObjClassSupp("cursor", TRUE);
                    S52_setS57ObjClassSupp("ebline", TRUE);
                    S52_setS57ObjClassSupp("vrmark", TRUE);
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
            // load PLib in s52.cfg
            if (XK_F7 == keysym) {
                S52_loadPLib(NULL);
                return TRUE;
            }
            // debug - unicode at S57ID:552 on CA579041.000 - Rimouski
            if (XK_F8 == keysym) {
                const char *str = S52_getAttList(552);
                g_print("s52eglx:F8:%s\n", str);

                return TRUE;
            }

#ifdef S52_USE_RADAR
            // dispose
            if (XK_F9 == keysym) {
                if (TRUE == S52_setRADARCallBack(_s52_radar_cb2, 0))
                    g_print("s52eglx:F9: done _s52_radar_cb2\n");
                else
                    g_print("s52eglx:F9: FAIL _s52_radar_cb2\n");

                return TRUE;
            }
#endif

            // debug
            g_print("s52egl.c:keysym: %i\n", keysym);


            //
            // debug - basic view movement
            //

            double delta = (engine->state.rNM / 10.0) / 60.0;

            // Move left, left arrow
            if (XK_Left      == keysym) {
                engine->state.cLon -= delta;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move up, up arrow
            if (XK_Up        == keysym) {
                // test - optimisation
                //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
                engine->state.cLat += delta;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move right, right arrow
            if (XK_Right     == keysym) {
                engine->state.cLon += delta;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move down, down arrow
            if (XK_Down      == keysym) {
                // test - optimisation
                //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
                engine->state.cLat -= delta;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // zoom in
            if (XK_Page_Up   == keysym) {
                double rNM = engine->state.rNM - (engine->state.rNM / 10.0);
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, rNM, engine->state.north);
            }
            // zoom out
            if (XK_Page_Down == keysym) {
                double rNM = engine->state.rNM + (engine->state.rNM / 10.0);
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, rNM, engine->state.north);
            }
            // rot -10.0 deg
            if (XK_Home      == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
#else
                _egl_beg(engine, "test");
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
                _egl_end(engine);
#endif
                engine->state.north -= 10.0;
                if (0.0 > engine->state.north)
                    engine->state.north += 360.0;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // rot +10.0 deg
            if (XK_End       == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
#else
                _egl_beg(engine, "test");
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
                _egl_end(engine);
#endif


                engine->state.north += 10.0;  // +10.0 deg
                if (360.0 <= engine->state.north)
                    engine->state.north -= 360.0;
                _s52_setVwNDraw(engine, engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }

#ifdef S52_USE_RADAR
            engine->do_S52draw     = TRUE;
#else
            engine->do_S52draw     = FALSE;
#endif
            engine->do_S52drawLast = TRUE;
        }
        break;

        }  // switch
    }      // while

    return TRUE;
}

int main(int argc, char *argv[])
{
    g_print("main():starting: argc=%i, argv[0]=%s\n", argc, argv[0]);

    XSetErrorHandler(_X11_error);

    _egl_init(&_engine);
    _s52_init(&_engine);

    _timer = g_timer_new();

#ifdef S52_USE_MESA3D
    // Mesa3D env - signal no vSync
    g_setenv("vblank_mode", "0", 1);

    // Mesa3D env - MSAA = 4
    g_setenv("GALLIUM_MSAA", "4", 1);
    //g_setenv("GALLIUM_MSAA", "2", 1);
#endif


    g_timeout_add(500, _X11_handleXevent, (void*)&_engine);  // 0.5 sec

#ifdef S52_USE_RADAR
    //g_timeout_add(1000/60, _s52_draw_cb,      (void*)&_engine);  // 16 msec
    g_timeout_add(100, _s52_draw_cb,      (void*)&_engine);  // 0.1 sec
#else
    g_timeout_add(500, _s52_draw_cb,      (void*)&_engine);  // 0.5 sec
#endif

    //g_mem_set_vtable(glib_mem_profiler_table);

    _engine.main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(_engine.main_loop);

#ifdef USE_AIS
    s52ais_doneAIS();
#endif

    _s52_done(&_engine);

    _egl_doneSurface(&_engine);
    _egl_done(&_engine);

    //g_mem_profile();

#ifdef S52_USE_RADAR
    // close radarlog
    fclose(_fd);
#endif

#ifdef S52_USE_MESA3D
    // Mesa3D env - remove from env (not stictly needed - env destroy at exit)
    g_unsetenv("vblank_mode");
    g_unsetenv("GALLIUM_MSAA");
#endif


    g_print("%s .. done\n", argv[0]);

    return TRUE;
}
#endif
