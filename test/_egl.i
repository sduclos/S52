// _egl.i: common EGL code for s52egl.c, s52gtkegl.c
//
// SD 2016APR28


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


#ifdef S52_USE_ANDROID
static void     _egl_doneSurface(s52engine *engine)
{
    if (engine->eglSurface != EGL_NO_SURFACE) {
        eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        eglDestroySurface(engine->eglDisplay, engine->eglSurface);
        engine->eglSurface = EGL_NO_SURFACE;
    }

    return;
}
#endif  // S52_USE_ANDROID

static void     _egl_done       (s52engine *engine)
// Tear down the EGL context currently associated with the display.
{
    if (engine->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (engine->eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->eglDisplay, engine->eglContext);
            engine->eglContext = EGL_NO_CONTEXT;
        }

#if !defined(S52_USE_ANDROID)
        //_egl_doneSurface(engine);
        if (engine->eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->eglDisplay, engine->eglSurface);
            engine->eglSurface = EGL_NO_SURFACE;
        }
#endif

        eglTerminate(engine->eglDisplay);
        engine->eglDisplay = EGL_NO_DISPLAY;
    }

    return;
}

static int      _egl_beg        (s52engine *engine, const char *tag)
{
    (void)engine;
    (void)tag;

    //LOGE("_egl_beg() .. \n");
    //g_timer_reset(_timer);

    //EGL_SUCCESS             0x3000
    //LOGI("_egl_beg(): eglGetError(): 0x%x\n", eglGetError());

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

    /*
    // this prevent EGL_BAD_ACCESS on Adreno/Tegra - eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
    if (EGL_NO_CONTEXT == eglGetCurrentContext()) {
        LOGI("_egl_beg(): EGL_NO_CONTEXT .. exit FALSE\n");
        return FALSE;
    }
    //*/

    //*
    if (engine->eglContext != eglGetCurrentContext()) {
        //LOGI("_egl_beg(): engine->eglContext ..\n");
        if (EGL_FALSE == eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext)) {
            // eglMakeCurrent() output the same error msg:
            // eglMakeCurrent:671 error 3002 (EGL_BAD_ACCESS)
            LOGE("_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
            return FALSE;
        }
    }
    //*/
    /*
    } else {
        LOGI("_egl_beg(): NOT engine->eglContext ..\n");
        return FALSE;
    }
    */

    /* Mesa3D 10.1 generate a glError() in _checkError(): from S52_GL_begin() -0-: 0x502 (GL_INVALID_OPERATION)
    // Note: egltrace.so (apitrace) handle it
    if (NULL != _glInsertEventMarkerEXT) {
        //_glInsertEventMarkerEXT(strlen(tag), tag);
        _glInsertEventMarkerEXT(0, tag);
    }
    */

    return TRUE;
}

static int      _egl_end        (s52engine *engine)
{
    if (EGL_FALSE == eglWaitGL()) {
        //LOGE("_egl_end(): eglWaitGL() failed - NO SWAP [0x%x]\n", eglGetError());
        return FALSE;
    }

    //g_timer_reset(_timer);

    if (EGL_TRUE != eglSwapBuffers(engine->eglDisplay, engine->eglSurface)) {
        //LOGE("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        return FALSE;
    }

    //double sec = g_timer_elapsed(_timer, NULL);
    //LOGI("_egl_end():eglSwapBuffers(): %.0f msec --------------------------------------\n", sec * 1000);

    return TRUE;
}

// -------------------------------------------------------------------------
//  ARM/Android, X11, GTK
//

static int      _egl_init       (s52engine *engine)
{
    LOGI("_egl_init(): beg ..\n");

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



    EGLNativeWindowType eglWindow  = FALSE;
    EGLDisplay          eglDisplay = EGL_NO_DISPLAY;
    EGLSurface          eglSurface = EGL_NO_SURFACE;
    EGLContext          eglContext = EGL_NO_CONTEXT;

    // --- BindAPI GL/GLES ------------------------------------------------
#ifdef S52_USE_GLES2
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);

    // debug MSAA - EGL/GL Mesa3D 10.1 GLSL fail at gl_PointCoord
    //EGLBoolean ret = eglBindAPI(EGL_OPENGL_API);
#else
    // OpenGL 1.x
    EGLBoolean ret = eglBindAPI(EGL_OPENGL_API);
#endif
    if (EGL_TRUE != ret)
        LOGE("eglBindAPI() failed. [0x%x]\n", eglGetError());


    // --- get eglDisplay ------------------------------------------------
#if defined(S52_USE_ANDROID) || defined(GTK_MAJOR_VERSION)
    eglDisplay  = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    engine->dpy = XOpenDisplay(NULL);
    eglDisplay  = eglGetDisplay(engine->dpy);
#endif

    if (EGL_NO_DISPLAY == eglDisplay)
        LOGE("eglGetDisplay() failed. [0x%x]\n", eglGetError());

    EGLint major = 2;
    EGLint minor = 0;
    if (EGL_FALSE == eglInitialize(eglDisplay, &major, &minor))
        LOGE("eglInitialize() failed. [0x%x]\n", eglGetError());

    LOGI("EGL Version   :%s\n", eglQueryString(eglDisplay, EGL_VERSION));
    LOGI("EGL Vendor    :%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
    LOGI("EGL Extensions:%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));


    // --- set eglConfig ------------------------------------------------
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
    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,

        //EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        //EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        //EGL_ALPHA_SIZE,      8,

        // MSAA
        //EGL_SAMPLE_BUFFERS,      1,
        //EGL_SAMPLES,             4,
        //EGL_SAMPLES,             8,

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


    // Here, the application chooses the configuration it desires. In this
    // sample, we have a very simplified selection process, where we pick
    // the first EGLConfig that matches our criteria
    EGLConfig  eglConfig;
    EGLint     eglNumConfigs = 0;

    eglGetConfigs(eglDisplay, NULL, 0, &eglNumConfigs);
    LOGI("eglNumConfigs = %i\n", eglNumConfigs);
    if (0 == eglNumConfigs) {
        LOGI("eglGetConfigs(): eglNumConfigs == zero matching config [0x%x]\n", eglGetError());
        g_assert(0);
    }

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


    // --- get eglWindow ------------------------------------------------
#ifdef S52_USE_ANDROID
    // EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
    // guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
    // As soon as we picked a EGLConfig, we can safely reconfigure the
    // ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    EGLint vid;
    if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vid)) {
    //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[5], EGL_NATIVE_VISUAL_ID, &vid))
        LOGE("ERROR: eglGetConfigAttrib() failed\n");
    }
    // WARNING: do not use native get/set Width()/Height() has it break rotation
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, vid);

    eglWindow = (EGLNativeWindowType) engine->app->window;

#elif GTK_MAJOR_VERSION   // S52_USE_ANDROID
	eglWindow = (EGLNativeWindowType) gtk_widget_get_window(engine->window);
#else  // S52_USE_ANDROID
    {
        XSetWindowAttributes wa;
        XSizeHints    sh;
        unsigned long mask    = CWBackPixel | CWBorderPixel | CWEventMask | CWColormap;
        long          screen  = 0;
        XVisualInfo  *visual  = NULL;
        XVisualInfo   tmplt;
        int           vID, n;
        Window        window  = 0;
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
#endif  // S52_USE_ANDROID

    if (FALSE == eglWindow) {
        LOGE("ERROR: eglWindow is NULL (can't draw)\n");
        g_assert(0);
    }

    // --- get eglSurface ------------------------------------------------
    // FIXME: PBuffer for AA!
#ifdef GTK_MAJOR_VERSION
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, GDK_WINDOW_XID(gtk_widget_get_window(engine->window)), NULL);
#else
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, NULL);
    //eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig[5], eglWindow, NULL);
#endif
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError()) {
        LOGE("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());
        g_assert(0);
    }

    // when swapping Adreno clear old buffer
    // http://www.khronos.org/registry/egl/specs/EGLTechNote0001.html
    eglSurfaceAttrib(eglDisplay, eglSurface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);

    // debug GLES2 AA - The initial value of EGL_MULTISAMPLE_RESOLVE is EGL_MULTISAMPLE_RESOLVE_DEFAULT.
    //eglSurfaceAttrib(eglDisplay, eglSurface, EGL_MULTISAMPLE_RESOLVE, EGL_MULTISAMPLE_RESOLVE_DEFAULT);

    // --- get eglContext ------------------------------------------------
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

    //---- MAKE CURRENT -------------------------------------------------------------------------------
    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        LOGE("Unable to eglMakeCurrent()\n");

    // test - no interval, "non-sync" mode - seem uneffective
    // Note: must be called after MakeCurrent()
    //eglSwapInterval(eglDisplay, 0);  // bad - make blit jumpy
    eglSwapInterval(eglDisplay, 1);  // default


    //---- Misc ----------------------------------------------------------------------------------------------
    // get EGL Marker & Timer
    // Note: on Mesa3D eglGetProcAddress() return an invalid address
    _glInsertEventMarkerEXT = (PFNGLINSERTEVENTMARKEREXT) eglGetProcAddress("glInsertEventMarkerEXT");
    if (NULL == _glInsertEventMarkerEXT) {
        LOGE("DEBUG: eglGetProcAddress(glInsertEventMarkerEXT()) FAILED\n");
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

    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, engine);

    //engine->eglWindow  = eglWindow;
    engine->eglDisplay = eglDisplay;
    engine->eglContext = eglContext;
    engine->eglSurface = eglSurface;
    engine->eglConfig  = eglConfig;

    LOGI("_egl_init(): end ..\n");

    return EGL_TRUE;
}





// -------------------------------------------------------------------------
//  EGL for s52gtkegl - GTK
//
#if 0
//#ifdef GTK_MAJOR_VERSION
static int      _egl_init   (s52engine *engine)
{
    g_print("_egl_init(): starting ..\n");

    if ((NULL!=engine->eglDisplay) && (EGL_NO_DISPLAY!=engine->eglDisplay)) {
        if (engine->eglDisplay != EGL_NO_DISPLAY)
            LOGI("_egl_init(): EGL DISPLAY OK\n");
        if (engine->eglContext != EGL_NO_CONTEXT)
            LOGI("_egl_init(): EGL CONTEXT OK\n");
        if (engine->eglSurface != EGL_NO_SURFACE)
            LOGI("_egl_init(): EGL SURFACE OK\n");

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


    EGLNativeWindowType eglWindow = 0;
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
    EGLint     eglNumConfigs = 0;
    EGLConfig  eglConfig;
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

        EGL_NONE
    };

    if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs))
        g_print("eglChooseConfig() failed. [0x%x]\n", eglGetError());
    if (0 == eglNumConfigs)
        g_print("eglChooseConfig() eglNumConfigs no matching config [0x%x]\n", eglGetError());

    // Note: GTK call
	eglWindow = (EGLNativeWindowType) gtk_widget_get_window(engine->window);
    if (FALSE == eglWindow) {
        g_print("ERROR: EGLNativeWindowType is NULL (can't draw)\n");
        g_assert(0);
    }

    // debug
    //g_print("DEBUG: eglDisplay  =0X%X\n", eglDisplay);
    //g_print("DEBUG: eglConfig   =0X%X\n", eglConfig);
    //g_print("DEBUG: eglWindowXID=0X%X\n", eglWindow);

    // GDK_WINDOW_HWND for win32 (_MINGW)
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, GDK_WINDOW_XID(gtk_widget_get_window(engine->window)), NULL);
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
    engine->eglWindow  = eglWindow;

    g_print("_egl_init(): end ..\n");

    return 1;
}

//#else  // GTK_MAJOR_VERSION
#endif  // 0
