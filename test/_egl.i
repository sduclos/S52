// _egl.i: common EGL code for s52egl.c, s52gtkegl.c
//
// SD 2016APR28
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2018 Sylvain Duclos sduclos@users.sourceforge.net

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
#include <EGL/eglext.h>  // robustness

//#define WIDTH  1280
//#define HEIGHT 1024
#define WIDTH  600
#define HEIGHT 400

typedef struct EGLState {
    EGLNativeWindowType eglWindow;         // GdkDrawable in GTK2 and GdkWindow in GTK3
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;
    EGLConfig           eglConfig;

#if !defined(S52_USE_ANDROID)
#ifdef GTK_MAJOR_VERSION
    GtkWidget          *window;
#else
    Display            *dpy;  // X11
#endif
#endif  // !S52_USE_ANDROID

} EGLState;

typedef void (*PFNGLINSERTEVENTMARKEREXT)(int length, const char *marker);
//typedef void (GL_APIENTRY *PFNGLPUSHGROUPMARKEREXT)  (GLsizei length, const char *marker);
//typedef void (GL_APIENTRY *PFNGLPOPGROUPMARKEREXT)   (void);

static PFNGLINSERTEVENTMARKEREXT _glInsertEventMarkerEXT = NULL;
//static PFNGLPUSHGROUPMARKEREXT   _glPushGroupMarkerEXT   = NULL;
//static PFNGLPOPGROUPMARKEREXT    _glPopGroupMarkerEXT    = NULL;

//#ifdef S52_USE_TEGRA2
//typedef EGLuint64NV (*PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC) (void);
//typedef EGLuint64NV (*PFNEGLGETSYSTEMTIMENVPROC)          (void);
//static PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC _eglGetSystemTimeFrequencyNV = NULL;
//static PFNEGLGETSYSTEMTIMENVPROC          _eglGetSystemTimeNV          = NULL;
//#endif
#define EGL_KHR_create_context_no_error 1
#define EGL_CONTEXT_OPENGL_NO_ERROR_KHR   0x31B3

#define EGL_DUMP_SRF(attribute)                                                     \
    eglQuerySurface(eglState->eglDisplay, eglState->eglSurface, attribute, &value); \
    LOGI("_egl_dump(): %-26s = %i\n", #attribute, value);                              \
    value = EGL_UNKNOWN;  // reset -1

static int _EGL_EXT_create_context_robustness = FALSE;

static int      _egl_beg (EGLState *eglState, const char *tag)
{
    (void)eglState;
    (void)tag;

    //LOGE("_egl_beg() .. \n");
    //g_timer_reset(_timer);

    /* EGL_SUCCESS             0x3000
    if (0x3000 == eglGetError()) {
        LOGI("_egl_beg(): eglGetError(): 0x3000 == EGL_SUCCESS\n");
    } else {
        LOGI("_egl_beg(): eglGetError(): EGL ERROR \n");
    }
    */

    // Android-09, Blit x10 slower whitout
    // Android-19, no diff
    /*
    if (EGL_FALSE == eglWaitGL()) {
        LOGE("_egl_beg(): eglWaitGL() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    /* Xoom no diff
    if (EGL_FALSE == eglWaitClient()) {
        LOGE("_egl_beg():eglWaitClient() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    /* make sure Android is finish - Xoom no diff
    if (EGL_FALSE == eglWaitNative(EGL_CORE_NATIVE_ENGINE)) {
        LOGE("_egl_beg():eglWaitNative() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }
    //*/

    //*
    // this prevent EGL_BAD_ACCESS on Adreno/Tegra - eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
    // also for robustness EGL_EXT_create_context_robustness (ie lose context on fault)
    // EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, EGL_LOSE_CONTEXT_ON_RESET,  // 0x31BF
    if (EGL_NO_CONTEXT == eglGetCurrentContext()) {
        LOGI("_egl_beg(): EGL_NO_CONTEXT .. exit FALSE\n");
        return FALSE;
    }
    //*/

    //*
    if (eglState->eglContext != eglGetCurrentContext()) {
        LOGI("_egl_beg(): eglState->eglContext not current..\n");
        if (EGL_FALSE == eglMakeCurrent(eglState->eglDisplay, eglState->eglSurface, eglState->eglSurface, eglState->eglContext)) {
            // eglMakeCurrent() output the same error msg:
            // eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
            LOGE("_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
            return FALSE;
        }
    } else {
        //LOGI("_egl_beg(): DEBUG eglContext OK\n");

    }
    //*/

    /* debug - this code fail to trigger context reset
    // also 'suspend' or 'lock' fail to lose context
    if (NULL!=tag && 'L'==tag[0]) {
        LOGI("DEBUG: test to trigger CONTEXT RESET\n");
        eglDestroyContext(eglState->eglDisplay, eglState->eglContext);
        eglState->eglContext = EGL_NO_CONTEXT;
    }
    //*/

    /* Mesa3D 10.1 generate a glError() in _checkError(): from S52_GL_begin() -0-: 0x502 (GL_INVALID_OPERATION)
    // Note: egltrace.so (apitrace) handle it
    if (NULL != _glInsertEventMarkerEXT) {
        //_glInsertEventMarkerEXT(strlen(tag), tag);
        _glInsertEventMarkerEXT(0, tag);
    }
    */

    return TRUE;
}

static int      _egl_end (EGLState *eglState, const char *tag)
{
    (void)eglState;
    (void)tag;

    // debug - test trigger a context reset
    //if (NULL!=tag && 'D'==tag[0]) {
    //    LOGE("_egl_end(): FIXME: RESET CONTEXT [0x%x]\n", eglGetError());
    //}

    // DRAW
    //if (NULL!=tag && 'D'==tag[0]) {
    //    LOGE("_egl_end(): end of draw() [0x%x]\n", eglGetError());
    //}
    // LAST
    //if (NULL!=tag && 'L'==tag[0]) {
    //    LOGE("_egl_end(): FIXME: RESET CONTEXT [0x%x]\n", eglGetError());
    //}
    // STR
    //if (NULL!=tag && 'S'==tag[0]) {
    //    LOGE("_egl_end(): end of drawStr() [0x%x]\n", eglGetError());
    //}

    //if (EGL_FALSE == eglWaitGL()) {
    //    LOGE("_egl_end(): eglWaitGL() failed - NO SWAP [0x%x]\n", eglGetError());
    //    return FALSE;
    //}

    //g_timer_reset(_timer);

    if (EGL_TRUE != eglSwapBuffers(eglState->eglDisplay, eglState->eglSurface)) {
        LOGE("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    //double sec = g_timer_elapsed(_timer, NULL);
    //LOGI("_egl_end():eglSwapBuffers(): flushing pipe line took: %.0f msec --------------------------------------\n", sec * 1000);

    return TRUE;
}

static int      _egl_dump(EGLState *eglState)
{
    EGLint value = EGL_UNKNOWN;  // -1

    // Returns the ID of the EGL frame buffer configuration with respect to which the surface was created.
    EGL_DUMP_SRF(EGL_CONFIG_ID);
    // Returns the height of the surface in pixels.
    EGL_DUMP_SRF(EGL_HEIGHT);
    //Returns the horizontal dot pitch of the display on which a window surface is visible. The value returned is equal to the actual dot pitch, in pixels/meter, multiplied by the constant value EGL_DISPLAY_SCALING.
    EGL_DUMP_SRF(EGL_HORIZONTAL_RESOLUTION);
    // Returns the same attribute value specified when the surface was created with eglCreatePbufferSurface. For a window or pixmap surface, value is not modified.
    EGL_DUMP_SRF(EGL_LARGEST_PBUFFER);
    // Returns which level of the mipmap to render to, if texture has mipmaps.
    EGL_DUMP_SRF(EGL_MIPMAP_LEVEL);
    // Returns EGL_TRUE if texture has mipmaps, EGL_FALSE otherwise.
    EGL_DUMP_SRF(EGL_MIPMAP_TEXTURE);
    // Returns the filter used when resolving the multisample buffer. The filter may be either EGL_MULTISAMPLE_RESOLVE_DEFAULT or EGL_MULTISAMPLE_RESOLVE_BOX, as described for eglSurfaceAttrib.
    EGL_DUMP_SRF(EGL_MULTISAMPLE_RESOLVE);
    // Returns the aspect ratio of an individual pixel (the ratio of a pixel's width to its height). The value returned is equal to the actual aspect ratio multiplied by the constant value EGL_DISPLAY_SCALING.
    EGL_DUMP_SRF(EGL_PIXEL_ASPECT_RATIO);
    // Returns the buffer which client API rendering is requested to use. For a window surface, this is the same attribute value specified when the surface was created. For a pbuffer surface, it is always EGL_BACK_BUFFER. For a pixmap surface, it is always EGL_SINGLE_BUFFER. To determine the actual buffer being rendered to by a context, call eglQueryContext.
    EGL_DUMP_SRF(EGL_RENDER_BUFFER);
    // Returns the effect on the color buffer when posting a surface with eglSwapBuffers. Swap behavior may be either EGL_BUFFER_PRESERVED or EGL_BUFFER_DESTROYED, as described for eglSurfaceAttrib.
    EGL_DUMP_SRF(EGL_SWAP_BEHAVIOR);
    //Returns format of texture. Possible values are EGL_NO_TEXTURE, EGL_TEXTURE_RGB, and EGL_TEXTURE_RGBA.
    EGL_DUMP_SRF(EGL_TEXTURE_FORMAT);
    // Returns type of texture. Possible values are EGL_NO_TEXTURE, or EGL_TEXTURE_2D.
    EGL_DUMP_SRF(EGL_TEXTURE_TARGET);
    //Returns the vertical dot pitch of the display on which a window surface is visible. The value returned is equal to the actual dot pitch, in pixels/meter, multiplied by the constant value EGL_DISPLAY_SCALING.
    EGL_DUMP_SRF(EGL_VERTICAL_RESOLUTION);
    // Returns the width of the surface in pixels.
    EGL_DUMP_SRF(EGL_WIDTH);

    return TRUE;
}

static int      _egl_init(EGLState *eglState)
{
    LOGI("_egl_init(): beg ..\n");

    if ((NULL != eglState) && (eglState->eglDisplay != EGL_NO_DISPLAY)) {
        if (eglState->eglDisplay != EGL_NO_DISPLAY)
            LOGI("_egl_init(): EGL DISPLAY OK\n");
        if (eglState->eglContext != EGL_NO_CONTEXT)
            LOGI("_egl_init(): EGL CONTEXT OK\n");
        if (eglState->eglSurface != EGL_NO_SURFACE)
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



    EGLNativeWindowType eglWindow  = FALSE;
    EGLDisplay          eglDisplay = EGL_NO_DISPLAY;
    EGLSurface          eglSurface = EGL_NO_SURFACE;
    EGLContext          eglContext = EGL_NO_CONTEXT;

    // --- BindAPI GL/GLES ------------------------------------------------
#if defined(S52_USE_GLES2)
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);
#else
    // OpenGL 1.x
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_API);
#endif
    if (EGL_TRUE != ret)
        LOGE("eglBindAPI() failed. [0x%x]\n", eglGetError());


    // --- get eglDisplay ------------------------------------------------
#if defined(S52_USE_ANDROID) || defined(GTK_MAJOR_VERSION)
    eglDisplay    = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    eglState->dpy = XOpenDisplay(NULL);
    eglDisplay    = eglGetDisplay(eglState->dpy);
#endif

    if (EGL_NO_DISPLAY == eglDisplay) {
        LOGE("eglGetDisplay() failed. [0x%x]\n", eglGetError());
        g_assert(0);
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (EGL_FALSE == eglInitialize(eglDisplay, &major, &minor)) {
        LOGE("eglInitialize() failed. [0x%x]\n", eglGetError());
        g_assert(0);
    }
    LOGI("eglInitialize(): major:%i minor:%i\n", major, minor);

    LOGI("EGL Client API:%s\n", eglQueryString(eglDisplay, EGL_CLIENT_APIS));
    LOGI("EGL Version   :%s\n", eglQueryString(eglDisplay, EGL_VERSION));
    LOGI("EGL Vendor    :%s\n", eglQueryString(eglDisplay, EGL_VENDOR));

    const char *extensions = eglQueryString(eglDisplay, EGL_EXTENSIONS);
    LOGI("EGL Extensions:%s\n", extensions);

    // EGL_EXT_create_context_robustness
    if (NULL != g_strrstr(extensions, "EGL_EXT_create_context_robustness")) {
        LOGI("DEBUG: EGL_EXT_create_context_robustness OK\n");
        _EGL_EXT_create_context_robustness = TRUE;
    } else {
        LOGI("DEBUG: EGL_EXT_create_context_robustness FAILED\n");
        _EGL_EXT_create_context_robustness = FALSE;
    }

    // --- set eglConfig ------------------------------------------------
    // Here specify the attributes of the desired configuration.
    // Below, we select an EGLConfig with at least 8 bits per color
    // component compatible with on-screen windows


    // FIXME: if EGL_EXT_create_context_robustness available then
    //           EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT set to EGL_TRUE
    //           EGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_EXT set to <reset notification behavior>

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
#define EGL_OPENGL_ES3_BIT_KHR    0x00000040
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,

        //EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        //EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,  // GLES3 to get NPOT texture in blit

        // this bit open access to ES3 functions on QCOM hardware pre-Android support for ES3
        // WARNING: this break MSAA on Android Kit-Kat 4.4.2, 4.4.3 - and -lGLESv3 Android.mk
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES3_BIT_KHR,

        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,

        // Note: MSAA work on Andreno in: setting > developer > MSAA
        EGL_SAMPLE_BUFFERS,     1,
        EGL_SAMPLES,            4,
        //EGL_SAMPLES,             8,

        EGL_NONE
    };
#endif  // S52_USE_ADRENO

#else   // S52_USE_ANDROID

#ifdef S52_USE_GLES2
    // Mesa GL, GLES 2.x, 3.x
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        //EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,  // test

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        // MSAA - fail on MESA with "export MESA_GLES_VERSION_OVERRIDE=2.0"
        // black frame flicker
        EGL_SAMPLE_BUFFERS,      1,
        EGL_SAMPLES,             1,

        //EGL_SAMPLES,             4,
        //EGL_SAMPLES,             8,

        // test - MSAA off for testing cursor pick - no noticable diff
        // and set eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
        //EGL_SAMPLE_BUFFERS,      0,  // The default value is zero.
        //EGL_SAMPLES,             0,

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

        EGL_SAMPLE_BUFFERS,     1,
        EGL_SAMPLES,            4,
        //EGL_SAMPLES,             8,

        EGL_NONE
    };
#endif  // S52_USE_GLES2
#endif  // S52_USE_ANDROID


    // Here, the application chooses the configuration it desires. In this
    // sample, we have a very simplified selection process, where we pick
    // the first EGLConfig that matches our criteria
    EGLint eglNumConfigs = 0;
    eglGetConfigs(eglDisplay, NULL, 0, &eglNumConfigs);
    if (0 == eglNumConfigs) {
        LOGI("eglGetConfigs(): eglNumConfigs == zero matching config [0x%x]\n", eglGetError());
        g_assert(0);
    }
    LOGI("eglNumConfigs = %i\n", eglNumConfigs);

    /*
    {
        EGLConfig eglConfig[eglNumConfigs];

        for (int i = 0; i<eglNumConfigs; ++i) {
            EGLint samples = 0;

            //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLES, &samples))
            //    LOGE(("eglGetConfigAttrib in loop for an EGL_SAMPLES fail at i = %i\n", i);

            if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLE_BUFFERS, &samples)) {
                LOGE("eglGetConfigAttrib in loop for an  EGL_SAMPLE_BUFFERS fail at i = %i\n", i);

                //if (samples > 0)
                //    LOGE("sample found: %i\n", samples);
            }
        }

        if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs)) {
            //if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, eglConfig, 27, &eglNumConfigs))
            LOGI("eglChooseConfig(): call failed [0x%x]\n", eglGetError());
            g_assert(0);
        }
    }
    //*/


    EGLConfig eglConfig = NULL;
    if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs)) {
    //if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, eglConfig, 27, &eglNumConfigs))
        LOGI("eglChooseConfig(): call failed [0x%x]\n", eglGetError());
        g_assert(0);
    }
    if (NULL == eglConfig) {
        LOGI("eglChooseConfig(): call failed eglConfig[0] == NULL\n");
        g_assert(0);
    }

    // --- get eglWindow ------------------------------------------------
#ifdef S52_USE_ANDROID
    // EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
    // guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
    // As soon as we picked a EGLConfig, we can safely reconfigure the
    // ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    EGLint vid = -1;
    if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vid)) {
    //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[5], EGL_NATIVE_VISUAL_ID, &vid))
        LOGE("ERROR: eglGetConfigAttrib() failed\n");
        g_assert(0);
    }

    // native android window
    //eglWindow = (EGLNativeWindowType) engine->app->window;
    eglWindow = (EGLNativeWindowType) eglState->eglWindow;

    // WARNING: do not use native get/set Width()/Height() has it break rotation
    //ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, vid);
    ANativeWindow_setBuffersGeometry(eglWindow, 0, 0, vid);


#elif GTK_MAJOR_VERSION
    eglWindow = (EGLNativeWindowType) GDK_WINDOW_XID(gtk_widget_get_window(eglState->window));
#else
    // Xlib
    {
        XSetWindowAttributes wa;
        XSizeHints    sh;
        unsigned long mask    = CWBackPixel | CWBorderPixel | CWEventMask | CWColormap;
        long          screen  = 0;
        XVisualInfo  *visual  = NULL;
        XVisualInfo   tmplt;
        int           n;
        Window        window  = 0;
        Display      *display = eglState->dpy;

#ifdef S52_USE_GLES2
        const char   *title   = "EGL/OpenGL ES 2.0 on a Linux Desktop";
#else
        const char   *title   = "EGL/OpenGL 2.0 on a Linux Desktop";
#endif
        EGLint vID = -1;
        if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vID)) {
            LOGE("ERROR: eglGetConfigAttrib() failed\n");
            g_assert(0);
        }
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

        window = XCreateWindow(display, RootWindow(display, screen), 0, 0, WIDTH, HEIGHT, //1280, 1024,
                               0, visual->depth, InputOutput, visual->visual, mask, &wa);
                               //0, 0, InputOutput, NULL, mask, &wa);
        XFree(visual);

        sh.flags = USPosition;
        sh.x = 0;
        sh.y = 0;
        XSetStandardProperties(display, window, title, title, None, 0, 0, &sh);
        XMapWindow(display, window);
        XSetWMColormapWindows(display, window, &window, 1);
        XFlush(display);

        eglWindow = (EGLNativeWindowType) window;
    }
#endif  // S52_USE_ANDROID

    if (FALSE == eglWindow) {
        LOGE("ERROR: eglWindow is NULL (can't draw)\n");
        g_assert(0);
    }

    // --- get eglSurface ------------------------------------------------
    /*  test double buffer
    const EGLint eglSurfaceAttribs[] = {
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
        EGL_NONE,
    };
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, eglSurfaceAttribs);
    */
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, NULL);
    //eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig[5], eglWindow, NULL);
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError()) {
        LOGE("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());
        g_assert(0);
    }


    // Note: eglSurfaceAttrib() EGL_SWAP_BEHAVIOR default is implementation specific
#if defined(S52_USE_ANDROID) || defined(GTK_MAJOR_VERSION)
    // FIXME: check that android and GTK work
    // when swapping Adreno clear old buffer
    // http://www.khronos.org/registry/egl/specs/EGLTechNote0001.html
    eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
#else
    // s52eglx/S52_drawStr() fail if EGL_BUFFER_PRESERVED
    //eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
    eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED);
#endif

    // --- get eglContext ------------------------------------------------
    // Then we can create the context and set it current:
    // 1 - GLES1.x, 2 - GLES2.x, 3 - GLES3.x
    EGLint eglContextList[9] = {
#if defined(S52_USE_ADRENO)
        EGL_CONTEXT_CLIENT_VERSION, 3, // GLES3 to get NPOT texture in blit
#else
        EGL_CONTEXT_CLIENT_VERSION, 2,
        //EGL_CONTEXT_CLIENT_VERSION, 3,  // test
#endif

//#ifdef EGL_KHR_create_context_no_error
//        EGL_CONTEXT_OPENGL_NO_ERROR_KHR,  // 0x31B3
//#endif

        EGL_NONE
    };

#if !defined(S52_USE_ANDROID)
    if (TRUE == _EGL_EXT_create_context_robustness) {
        LOGI("DEBUG: create EGLcontext robustness\n");
        // will propagate to glGetGraphicsResetStatus() if GL_EXT_robustness is supported
        eglContextList[4] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT;
        eglContextList[5] = EGL_TRUE;

        // 2 strategy available
        eglContextList[6] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT;
        //eglContextList[7] = EGL_NO_RESET_NOTIFICATION,  // 0x31BE
        //eglContextList[7] = EGL_LOSE_CONTEXT_ON_RESET;  // 0x31BF

        eglContextList[8] = EGL_NONE;
    }
#endif  // !S52_USE_ANDROID

#if defined(S52_USE_GLES2)
    // GLES
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, eglContextList);
    //eglContext = eglCreateContext(eglDisplay, eglConfig[5], EGL_NO_CONTEXT, eglContextList);
#else
    // GL
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, NULL);
#endif

    if (EGL_NO_CONTEXT == eglContext || EGL_SUCCESS != eglGetError()) {
        LOGE("eglCreateContext() failed. [0x%x]\n", eglGetError());
        g_assert(0);
    }

    //---- MAKE CURRENT -------------------------------------------------------------------------------
    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        LOGE("Unable to eglMakeCurrent()\n");

    // test - 0 interval, "non-sync" mode - seem uneffective
    // Note: must be called after MakeCurrent()
    //eglSwapInterval(eglDisplay, 0);  // bad - make blit jumpy
    //eglSwapInterval(eglDisplay, 1);  // default


    //---- Misc ----------------------------------------------------------------------------------------------
    // get EGL Marker & Timer
    // Note: on Mesa3D eglGetProcAddress() return an invalid address
    _glInsertEventMarkerEXT = (PFNGLINSERTEVENTMARKEREXT) eglGetProcAddress("glInsertEventMarkerEXT");
    if (NULL == _glInsertEventMarkerEXT) {
        LOGE("DEBUG: eglGetProcAddress(glInsertEventMarkerEXT) FAILED\n");
    } else {
        LOGE("DEBUG: eglGetProcAddress(glInsertEventMarkerEXT) OK\n");
    }

    /* get GPU driver timer - EGL_NV_system_time
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
    */
    //--------------------------------------------------------------------------------------------------

    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, eglState);

    //engine->eglWindow  = eglWindow;
    eglState->eglDisplay = eglDisplay;
    eglState->eglContext = eglContext;
    eglState->eglSurface = eglSurface;
    eglState->eglConfig  = eglConfig;

    LOGI("_egl_init(): end ..\n");

    _egl_dump(eglState);

    return EGL_TRUE;
}

static void     _egl_done(EGLState *eglState)
// Tear down the EGL context currently associated with the display.
{
    if (eglState->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglState->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (eglState->eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglState->eglDisplay, eglState->eglContext);
            eglState->eglContext = EGL_NO_CONTEXT;
        }

#if !defined(S52_USE_ANDROID)
        if (eglState->eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglState->eglDisplay, eglState->eglSurface);
            eglState->eglSurface = EGL_NO_SURFACE;
        }
#endif

        eglTerminate(eglState->eglDisplay);
        eglState->eglDisplay = EGL_NO_DISPLAY;
    }

    return;
}
