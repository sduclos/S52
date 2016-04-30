// S52gtk3egl.c: simple S52 driver using EGL & GTK (2 & 3).
//
// SD 2013AUG30 - Vitaly

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


#include <EGL/egl.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>         // GDK_*
#include <gdk/gdkkeysyms-compat.h>  // compat GDK_a/GDK_KEY_a

#ifdef _MINGW
#include <gdk/gdkwin32.h>
#else
#include <gdk/gdkx.h>
#endif

#include "S52.h"

#ifdef USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif


// FIXME: mutex this share data
typedef struct s52droid_state_t {
    int        do_S52init;
    // initial view
    double     cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} s52droid_state_t;

//
typedef struct s52engine {
	GtkWidget          *window;

    EGLNativeWindowType eglWindow;         // GdkDrawable in GTK2 and GdkWindow in GTK3
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;
    EGLConfig           eglConfig;

    // local
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

#include "_s52_setupMarPar.i"  // _s52_setupMarPar()
//#include "_s52_setupMarFea.i"  // _s52_setupMarFea()
#include "_s52_setupOWNSHP.i"  // _s52_setupOWNSHP()
#include "_s52_setupVESSEL.i"  // _s52_setupVESSEL()
#include "_s52_setupVRMEBL.i"  // _s52_setupVRMEBL()
//#include "_s52_setupPASTRK.i"  // _s52_setupPASTRK()
#include "_s52_setupLEGLIN.i"  // _s52_setupLEGLIN(), _s52_setupIceRte()
//#include "_s52_setupCLRLIN.i"  // _s52_setupCLRLIN()
#include "_s52_setupPRDARE.i"  // _s52_setupPRDARE()

#include "_s52_setupMain.i"    // _s52_setupMain(), various common test setup, LOG*()

#include "_egl.i"              // _egl_init(), _egl_beg(), _egl_end(), _egl_done()


//-----------------------------

static int      _s52_getView(s52droid_state_t *state)
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

static int      _s52_init   (s52engine *engine)
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
            engine->state.do_S52init = FALSE;
            return FALSE;
        }

        //g_print("_init_S52(): start -2- ..\n");

        //S52_setViewPort(0, 0, w, h);

    }

    // load ENC, ..
    _s52_setupMain();

    // if first start find where we are looking
    _s52_getView(&engine->state);
    // then (re)position the 'camera'
    S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    _s52_setupMarPar();

    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL
    _s52_setupVRMEBL(engine->state.cLat, engine->state.cLon);

    // guard zone OFF (pick need GL projection)
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);
    _s52_setupIceRte();
    _s52_setupLEGLIN(engine->state.cLat, engine->state.cLon);
    S52_setMarinerParam(S52_MAR_GUARDZONE_ALARM, 0.0);  // clear alarm

    _s52_setupPRDARE(engine->state.cLat, engine->state.cLon);

#ifdef USE_FAKE_AIS
    _s52_setupOWNSHP(engine->state.cLat, engine->state.cLon);
    _s52_setupVESSEL(engine->state.cLat, engine->state.cLon);
#endif



    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, engine);


    engine->do_S52draw        = TRUE;
    engine->do_S52drawLast    = TRUE;
    engine->do_S52setViewPort = FALSE;
    engine->state.do_S52init  = FALSE;

    return EGL_TRUE;
}

static int      _s52_done   (s52engine *engine)
{
    (void)engine;

    S52_done();

    return TRUE;
}

static int      _s52_draw_cb(gpointer user_data)
// return TRUE for the signal to be called again
{
    s52engine *engine = (s52engine*)user_data;

    //g_print("s52egl:_s52_draw_cb(): begin .. \n");

    /*
    GTimeVal now;  // 2 glong (at least 32 bits each - but amd64 !?
    g_get_current_time(&now);
    if (0 == (now.tv_sec - engine->timeLastDraw.tv_sec))
        goto exit;
    //*/

    if (NULL == engine) {
        g_print("_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        g_print("_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    // wait for libS52 to init - no use to go further - bailout
    if (TRUE == engine->state.do_S52init) {
        g_print("s52egl:_s52_draw_cb(): re-starting .. waiting for S52_init() to finish\n");
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
        _s52_updFakeAIS(engine->state.cLat, engine->state.cLon);
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
    switch(event->keyval) {
        case GDK_KEY_Left : _engine.state.cLon -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Right: _engine.state.cLon += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Up   : _engine.state.cLat += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Down : _engine.state.cLat -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }

    return TRUE;
}

static gboolean _zoom    (GdkEventKey *event)
{
    switch(event->keyval) {
        // zoom in
    	case GDK_KEY_Page_Up  : _engine.state.rNM /= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        // zoom out
        case GDK_KEY_Page_Down: _engine.state.rNM *= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }

    return TRUE;
}

static gboolean _rotation(GdkEventKey *event)
{
    switch(event->keyval) {
        // -
        case GDK_KEY_minus:
            _engine.state.north += 1.0;
            if (360.0 < _engine.state.north)
                _engine.state.north -= 360.0;
            break;
        // +
        case GDK_KEY_equal:
        case GDK_KEY_plus :
            _engine.state.north -= 1.0;
            if (_engine.state.north < 0.0)
                _engine.state.north += 360.0;
            break;
    }

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

static gboolean configure_event(GtkWidget         *widget,
                                GdkEventConfigure *event,
                                gpointer           data)
{
    (void)event;
    (void)data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(widget), &allocation);
    _engine.width  = allocation.width;
    _engine.height = allocation.height;

    _s52_getView(&_engine.state);
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);
    S52_setViewPort(0, 0, allocation.width, allocation.height);

    _engine.do_S52draw = TRUE;

    // debug
    //g_print("'configure_event' .. done\n");

    return TRUE;
}

static gboolean key_release_event(GtkWidget   *widget,
                                  GdkEventKey *event,
                                  gpointer     data)
{
    (void)widget;
    (void)data;

    switch(event->keyval) {
        case GDK_Left  :
        case GDK_Right :
        case GDK_Up    :
        case GDK_Down  :_scroll(event);            break;

        case GDK_KEY_equal :
        case GDK_KEY_plus  :
        case GDK_KEY_minus :_rotation(event);          break;

        case GDK_KEY_Page_Down:
        case GDK_KEY_Page_Up:_zoom(event);             break;


     //   case GDK_KEY_Escape:_resetView(&_engine.state);                break;
        case GDK_KEY_r     : //gtk_widget_draw(widget, NULL);
                              break;
     //   case GDK_KEY_h     :_doRenderHelp = !_doRenderHelp;
     //                   _usage("s52gtk2");
     //                   break;
        case GDK_KEY_v     :g_print("%s\n", S52_version());    break;
        //case GDK_KEY_x     :_dumpParam();                      break;
        case GDK_KEY_q     :gtk_main_quit();                   break;

        case GDK_KEY_w     :_toggle(S52_MAR_TWO_SHADES);       break;
        case GDK_KEY_s     :_toggle(S52_MAR_SHALLOW_PATTERN);  break;
        case GDK_KEY_o     :_toggle(S52_MAR_SHIPS_OUTLINE);    break;
        case GDK_KEY_l     :_toggle(S52_MAR_FULL_SECTORS);     break;
        case GDK_KEY_b     :_toggle(S52_MAR_SYMBOLIZED_BND);   break;
        case GDK_KEY_p     :_toggle(S52_MAR_SYMPLIFIED_PNT);   break;
        case GDK_KEY_n     :_toggle(S52_MAR_FONT_SOUNDG);      break;
        case GDK_KEY_u     :_toggle(S52_MAR_SCAMIN);           break;
        case GDK_KEY_i     :_toggle(S52_MAR_ANTIALIAS);        break;
        case GDK_KEY_j     :_toggle(S52_MAR_QUAPNT01);         break;
        case GDK_KEY_z     :_toggle(S52_MAR_DISP_OVERLAP);     break;
        //case GDK_1     :_toggle(S52_MAR_DISP_LAYER_LAST);  break;
        case GDK_KEY_1     :_meterInc(S52_MAR_DISP_LAYER_LAST);break;
        case GDK_KEY_exclam:_meterDec(S52_MAR_DISP_LAYER_LAST);break;

        case GDK_KEY_2     :_inc(S52_MAR_ROT_BUOY_LIGHT);      break;

        case GDK_KEY_3     :_toggle(S52_MAR_DISP_CRSR_PICK);
                            _toggle(S52_MAR_DISP_LEGEND);
                            _toggle(S52_MAR_DISP_CALIB);
                            _toggle(S52_MAR_DISP_DRGARE_PATTERN);
                            break;

        case GDK_KEY_4     :_toggle(S52_MAR_DISP_GRATICULE);   break;
        case GDK_KEY_5     :_toggle(S52_MAR_HEADNG_LINE);      break;

        //case GDK_t     :_meterInc(S52_MAR_SHOW_TEXT);      break;
        //case GDK_T     :_meterDec(S52_MAR_SHOW_TEXT);      break;
        case GDK_KEY_t     :
        case GDK_KEY_T     :_toggle  (S52_MAR_SHOW_TEXT);      break;
        case GDK_KEY_c     :_meterInc(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_KEY_C     :_meterDec(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_KEY_d     :_meterInc(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_KEY_D     :_meterDec(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_KEY_a     :_meterInc(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_KEY_A     :_meterDec(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_KEY_e     :_meterInc(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_KEY_E     :_meterDec(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_KEY_f     :_meterInc(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_KEY_F     :_meterDec(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_KEY_g     :_meterInc(S52_MAR_TIME_TAGS);      break;
        case GDK_KEY_G     :_meterDec(S52_MAR_TIME_TAGS);      break;
        case GDK_KEY_y     :_meterInc(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_KEY_Y     :_meterDec(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_KEY_m     :_meterInc(S52_MAR_DATUM_OFFSET);   break;
        case GDK_KEY_M     :_meterDec(S52_MAR_DATUM_OFFSET);   break;

        //case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, 'D'); break; // DISPLAYBASE
        //case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, 'S'); break; // STANDARD
        //case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, 'O'); break; // OTHER
        //case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, 'A'); break; // OTHER (all)
        //case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, 0);   break; // DISPLAYBASE
        //case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, 1);   break; // STANDARD
        //case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, 2);   break; // OTHER
        //case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, 3);   break; // OTHER (all)
        case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_BASE);   break; // DISPLAYBASE
        case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_STD);    break; // STANDARD
        case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_OTHER);  break; // OTHER
        case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT); break; // OTHER (all)

        case GDK_KEY_k     :_cpal(S52_MAR_COLOR_PALETTE,  1.0);break;
        case GDK_KEY_K     :_cpal(S52_MAR_COLOR_PALETTE, -1.0);break;

        case GDK_KEY_6     :_meterInc(S52_MAR_DISP_WHOLIN);    break;
        case GDK_KEY_asciicircum:
        case GDK_KEY_question:
        case GDK_KEY_caret :_meterDec(S52_MAR_DISP_WHOLIN);    break;


        //case GDK_3     :_cpal("S52_MAR_COLOR_PALETTE", 2.0); break; // DAY_WHITEBACK
        //case GDK_4     :_cpal("S52_MAR_COLOR_PALETTE", 3.0); break; // DUSK
        //case GDK_5     :_cpal("S52_MAR_COLOR_PALETTE", 4.0); break; // NIGHT

        //case GDK_KEY_F1    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000"); break;
        //case GDK_KEY_F2    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000"); break;
        case GDK_KEY_F1    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY); break;
        case GDK_KEY_F2    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS); break;
        case GDK_KEY_F3    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC); break;
        case GDK_KEY_F4    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC); break;
        case GDK_KEY_F5    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP); break;
        case GDK_KEY_F6    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX); break;

        case GDK_KEY_F7    :_mmInc(S52_MAR_DOTPITCH_MM_X); break;
        case GDK_KEY_F8    :_mmInc(S52_MAR_DOTPITCH_MM_Y); break;

        case GDK_KEY_F9    :_toggle(S52_MAR_DISP_NODATA_LAYER); break;

        default:
            g_print("key: 0x%04x\n", event->keyval);
    }

    // redraw
    _engine.do_S52draw = TRUE;

    return TRUE;
}

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);

    _engine.window = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_default_size(GTK_WINDOW(_engine.window), 800, 600);
    gtk_window_set_title       (GTK_WINDOW(_engine.window), "EGL / OpenGL ES2 in GTK3 application");

    gtk_widget_set_app_paintable     (GTK_WIDGET(_engine.window), TRUE );
    gtk_widget_set_double_buffered   (GTK_WIDGET(_engine.window), FALSE);
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(_engine.window), TRUE );

    g_signal_connect(G_OBJECT(_engine.window), "destroy",           G_CALLBACK(gtk_main_quit),     NULL);
    //g_signal_connect(G_OBJECT(_engine.window), "draw",              G_CALLBACK(_s52_draw_cb),     &_engine);
    g_signal_connect(G_OBJECT(_engine.window), "key_release_event", G_CALLBACK(key_release_event), NULL);
    g_signal_connect(G_OBJECT(_engine.window), "configure_event",   G_CALLBACK(configure_event),   NULL);

    //g_timeout_add(500, step, gtk_widget_get_window(_engine.window));  // 0.5 sec
    //g_timeout_add(500, (GSourceFunc)_s52_draw_cb, &_engine); // 0.5 sec
    g_timeout_add(500, _s52_draw_cb, &_engine); // 0.5 sec

    gtk_widget_show_all(_engine.window);

    _egl_init(&_engine);
    _s52_init(&_engine);


    gtk_main();

    _s52_done(&_engine);
    _egl_done(&_engine);

    g_print("%s .. done\n", argv[0]);

    return 0;
}
