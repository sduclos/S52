// s52gtkegl.c: simple S52 driver using EGL & GTK (2 & 3).
//
// SD 2013AUG30 - Vitaly

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


#include <gtk/gtk.h>
#include <gdk/gdk.h>                // GTK3 - GdkMonitor
#include <gdk/gdkkeysyms.h>         // GDK_*
#include <gdk/gdkkeysyms-compat.h>  // compat GDK_a/GDK_KEY_a
//#include <math.h>                   // INFINITY

#ifdef _MINGW
#include <gdk/gdkwin32.h>
#else
#include <gdk/gdkx.h>
#endif

#include "S52.h"

#ifdef USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif

#define RAD2DEG    57.29577951308232
#define DEG2RAD     0.0174532925199432958


//----------------------------------------------
//
// Common stuff for s52egl.c, s52gtk2.c, s52gtkegl.c
//

#ifdef USE_TEST_OBJ
#include "_s52_setupmarfea.i"  // _s52_setupmarfea()
#include "_s52_setupVRMEBL.i"  // _s52_setupVRMEBL()
#include "_s52_setupPASTRK.i"  // _s52_setupPASTRK()
#include "_s52_setupLEGLIN.i"  // _s52_setupLEGLIN(), _s52_setupIceRte()
#include "_s52_setupCLRLIN.i"  // _s52_setupCLRLIN()
#include "_s52_setupPRDARE.i"  // _s52_setupPRDARE()
#endif  // USE_TEST_OBJ

#ifdef USE_FAKE_AIS
#include "_s52_setupOWNSHP.i"  // _s52_setupOWNSHP()
#include "_s52_setupVESSEL.i"  // _s52_setupVESSEL()
#endif

#include "_s52_setupMarPar.i"  // _s52_setupMarPar(): S52_setMarinerParam()
#include "_s52_setupMain.i"    // _s52_setupMain(), various common test setup, LOG*(), loadCell()

#include "_egl.i"              // _egl_init(), _egl_beg(), _egl_end(), _egl_done()
//-----------------------------------------------

// FIXME: mutex this if share data
typedef struct s52engState {
    int         do_S52init;
    // initial view
    double      cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)

    gint        width;
    gint        height;
    gint        wmm;
    gint        hmm;
} s52engState;

//
typedef struct s52engine {
    s52engState state;
    EGLState    eglState;          // def in _egl.i

    // local
    int         do_S52draw;        // TRUE to call S52_draw()
    int         do_S52drawLast;    // TRUE to call S52_drawLast() - S52_draw() was called at least once
    int         do_S52setViewPort; // set in Android callback

    // Xoom - dpi = 160 (density)
    //int32_t             dpi;            // = AConfiguration_getDensity(engine->app->config);

    //GTimeVal            timeLastDraw;

} s52engine;

static s52engine _engine;

static int      _s52_getView(s52engState *state)
{
    // FIXME: if multiple cell are loaded indivitualy
    // then get total extent with S52_getCellExtent() + S52_setView() to display all cells at the start

    if (TRUE != S52_getView(&state->cLat, &state->cLon, &state->rNM, &state->north)) {
        g_print("_s52_getView(): S52_getView() failed\n");
        g_assert(0);
    }

    /*
    double S,W,N,E;
    if (FALSE == S52_getCellExtent(NULL, &S, &W, &N, &E))
        return FALSE;

    state->cLat  =  (N + S) / 2.0;
    state->cLon  =  (E + W) / 2.0;
    state->rNM   = ((N - S) / 2.0) * 60.0;
    state->north = 0.0;
    if (TRUE != S52_setView(&state->cLat, state->cLon, &state->rNM, &state->north)) {
        g_print("_s52_getView(): S52_getView() failed\n");
        g_assert(0);
    }
    */

    return TRUE;
}

static int      _s52_init   (s52engine *engine)
{
    if ((NULL==engine->eglState.eglDisplay) || (EGL_NO_DISPLAY==engine->eglState.eglDisplay)) {
        g_print("_init_S52(): no EGL display ..\n");
        return FALSE;
    }

    eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_WIDTH,  &engine->state.width);
    eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_HEIGHT, &engine->state.height);

    // return constant value EGL_UNKNOWN (-1) with Mesa
    // The value returned is equal to the actual dot pitch, in pixels/meter, multiplied by the constant value EGL_DISPLAY_SCALING.
    // EGL_DISPLAY_SCALING is the constant value 10000.
    eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_HORIZONTAL_RESOLUTION, &engine->state.wmm);
    eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_VERTICAL_RESOLUTION,   &engine->state.hmm);

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

        /*
        GdkDisplay *gdpy = gdk_display_get_default();
        GdkMonitor *gmon = gdk_display_get_primary_monitor(gdpy);
        wmm = gdk_monitor_get_width_mm(gmon);
        hmm = gdk_monitor_get_height_mm(gmon);

        GdkRectangle workarea;
        gdk_monitor_get_workarea(gmon, &workarea);
        w = workarea.width;
        h = workarea.height;
        */

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
    }

    // load ENC, ..
    _s52_setupMain();

    // get where we are looking _AFTER_ loadCell
    _s52_getView(&engine->state);

    // set Mariner's Parameter
    _s52_setupMarPar();

    // init decoration (scale bar, North arrow, unit, calib.)
    S52_newCSYMB();


#ifdef USE_TEST_OBJ
    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL
    _s52_setupVRMEBL(engine->state.cLat, engine->state.cLon);

    // guard zone OFF (pick need GL projection)
    //S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);
    _s52_setupIceRte();
    _s52_setupLEGLIN(engine->state.cLat, engine->state.cLon);
    S52_setMarinerParam(S52_MAR_GUARDZONE_ALARM, 0.0);  // clear alarm

    _s52_setupPRDARE(engine->state.cLat, engine->state.cLon);
    _s52_setupmarfea(engine->state.cLat, engine->state.cLon);
    _s52_setupPASTRK(engine->state.cLat, engine->state.cLon);
    _s52_setupCLRLIN(engine->state.cLat, engine->state.cLon);

#ifdef USE_FAKE_AIS
    _s52_setupOWNSHP(engine->state.cLat, engine->state.cLon);
    _s52_setupVESSEL(engine->state.cLat, engine->state.cLon);
#endif

#endif  // USE_TEST_OBJ


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
    //s52engine *engine = (s52engine*)
    (void)user_data;
    s52engine *engine = (s52engine*)&_engine;

    // debug
    //g_print("s52gtkegl:_s52_draw_cb(): begin ..");

    /*
    GTimeVal now;  // 2 glong (at least 32 bits each - but amd64 !?
    g_get_current_time(&now);
    if (0 == (now.tv_sec - engine->timeLastDraw.tv_sec))
        goto exit;
    //*/

    if (NULL == engine) {
        g_print("DEBUG: s52gtkegl:_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    if ((NULL==engine->eglState.eglDisplay) || (EGL_NO_DISPLAY==engine->eglState.eglDisplay)) {
        g_print("DEBUG: s52gtkegl:_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    // wait for libS52 to init - no use to go further - bailout
    if (TRUE == engine->state.do_S52init) {
        g_print("DEBUG: s52gtkegl:_s52_draw_cb(): re-starting .. waiting for S52_init() to finish\n");
        goto exit;
    }

    // no draw at all, the window is not visible
    if ((FALSE==engine->do_S52draw) && (FALSE==engine->do_S52drawLast)) {
        g_print("DEBUG: s52gtkegl:_s52_draw_cb(): nothing to draw (do_S52draw & do_S52drawLast FALSE)\n");
        goto exit;
    }

#if !defined(S52_USE_EGL)
    _egl_beg(engine->eglState, "test");
#endif

    // draw background
    if (TRUE == engine->do_S52draw) {
        if (TRUE == engine->do_S52setViewPort) {
            eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_WIDTH,  &engine->state.width);
            eglQuerySurface(engine->eglState.eglDisplay, engine->eglState.eglSurface, EGL_HEIGHT, &engine->state.height);

            S52_setViewPort(0, 0, engine->state.width, engine->state.height);

            engine->do_S52setViewPort = FALSE;
        }

        S52_draw();

        engine->do_S52draw = FALSE;
    }

    // draw last (AIS)
    if (TRUE == engine->do_S52drawLast) {

#ifdef USE_FAKE_AIS
        _s52_updFakeAIS(engine->state.cLat, engine->state.cLon);
#endif
        S52_drawLast();
    }

#if !defined(S52_USE_EGL)
    _egl_end(engine->eglState);
#endif


exit:

    // debug
    //g_print("DEBUG: s52gtkegl:_s52_draw_cb(): end .. \n");

    return TRUE;
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
        case GDK_KEY_Left : _engine.state.cLon -= _engine.state.rNM/(60.0*10.0); break;
        case GDK_KEY_Right: _engine.state.cLon += _engine.state.rNM/(60.0*10.0); break;
        case GDK_KEY_Up   : _engine.state.cLat += _engine.state.rNM/(60.0*10.0); break;
        case GDK_KEY_Down : _engine.state.cLat -= _engine.state.rNM/(60.0*10.0); break;
    }

    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    return TRUE;
}

static gboolean _zoom    (GdkEventKey *event)
{
    switch(event->keyval) {
        case GDK_KEY_Page_Up  : _engine.state.rNM /= 2.0; break;  // zoom in
        case GDK_KEY_Page_Down: _engine.state.rNM *= 2.0; break;  // zoom out
    }

    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    return TRUE;
}

static gboolean _rotation(GdkEventKey *event)
{
    switch(event->keyval) {
        // -
        case GDK_KEY_minus:
            _engine.state.north += 1.0;
            if (360.0 <= _engine.state.north)
                _engine.state.north -= 360.0;
            break;
        // +
        case GDK_KEY_equal:
        case GDK_KEY_plus :
            _engine.state.north -= 1.0;
            if (0.0 > _engine.state.north)
                _engine.state.north += 360.0;
            break;
    }

    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    return TRUE;
}

static gboolean _toggle  (S52MarinerParameter paramName)
{
    double val = S52_getMarinerParam(paramName);
    S52_setMarinerParam(paramName, !val);

    return TRUE;
}

static gboolean _meterInc(S52MarinerParameter paramName)
{
    double val = S52_getMarinerParam(paramName);
    S52_setMarinerParam(paramName, ++val);

    return TRUE;
}

static gboolean _meterDec(S52MarinerParameter paramName)
{
    double val = S52_getMarinerParam(paramName);
    S52_setMarinerParam(paramName, --val);

    return TRUE;
}

static gboolean _disp    (S52MarinerParameter paramName, int disp)
{
    S52_setMarinerParam(paramName, disp);

    return TRUE;
}

static gboolean _cpal    (S52MarinerParameter paramName, double val)
{
    val = S52_getMarinerParam(paramName) + val;
    S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _inc     (S52MarinerParameter paramName)
{
    double val = S52_getMarinerParam(paramName) + 15.0;
    S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _mmInc   (S52MarinerParameter paramName)
{
    double val = S52_getMarinerParam(paramName) + 0.01;
    S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean configure_event(GtkWidget         *widget,
                                GdkEventConfigure *event,
                                gpointer           data)
{
    (void)event;
    (void)data;

    //g_print("DEBUG: s52gtkegl:configure_event() \n");

    gint width, height;
    gtk_window_get_size(GTK_WINDOW(widget), &width, &height);

    if (TRUE == S52_setViewPort(0, 0, width, height)) {
        _engine.state.width  = width;
        _engine.state.height = height;

        _engine.do_S52draw     = TRUE;
        _engine.do_S52drawLast = TRUE;
    }

    return TRUE;
}

static gboolean key_release_event(GtkWidget   *widget,
                                  GdkEventKey *event,
                                  gpointer     data)
{
    //(void)widget;
    (void)data;

    switch(event->keyval) {
        case GDK_Left  :
        case GDK_Right :
        case GDK_Up    :
        case GDK_Down  :_scroll(event);            break;

        case GDK_KEY_equal :
        case GDK_KEY_plus  :
        case GDK_KEY_minus :_rotation(event);          break;
        //case GDK_KEY_minus :S52_drawBlit(0.0, 0.0, 0.0, 10.0); break;

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

        case GDK_KEY_Escape:
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
        case GDK_KEY_F10   :_toggle(S52_MAR_DISP_HODATA);       break;

        default:
            g_print("key: 0x%04x\n", event->keyval);
    }

    // redraw
    _engine.do_S52draw     = TRUE;
    _engine.do_S52drawLast = TRUE;

    //_s52_draw_cb(&_engine);
    gtk_widget_queue_draw(widget);

    return TRUE;
}

#if GTK_MAJOR_VERSION == 3
//////////////////////////////////////////////////
// code lifted from gtk/demos/gtk-demo/gesture.c
//

// Gestures

//  Perform gestures on touchscreens and other input devices. This
//  demo reacts to long presses and swipes from all devices, plus
//  multi-touch rotate and zoom gestures.

//static gdouble     _gswipe_x      = 0;
//static gdouble     _gswipe_y      = 0;
//static gboolean    _glong_pressed = FALSE;
static gdouble     _gstart_x      = 0.0;
static gdouble     _gstart_y      = 0.0;
static GtkGesture *_grotate       = NULL;
static GtkGesture *_gzoom         = NULL;
static gdouble     _gdelta        = 0.0;
static gdouble     _gscale        = 0.0;

#if 0
static gboolean drawing_area_draw(GtkWidget *widget,
                                  cairo_t   *cr)
{
    (void)cr;

    g_print("drawing_area_draw(): \n");

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    if (_gswipe_x != 0 || _gswipe_y != 0) {
        /*
        cairo_save (cr);
        cairo_set_line_width (cr, 6);
        cairo_move_to (cr, allocation.width / 2, allocation.height / 2);
        cairo_rel_line_to (cr, _gswipe_x, _gswipe_y);
        cairo_set_source_rgba (cr, 1, 0, 0, 0.5);
        cairo_stroke (cr);
        cairo_restore (cr);
        */
    }

    if (gtk_gesture_is_recognized(_grotate) || gtk_gesture_is_recognized(_gzoom)) {
        //double angle = gtk_gesture_rotate_get_angle_delta(GTK_GESTURE_ROTATE(_grotate));
        //double scale = gtk_gesture_zoom_get_scale_delta(GTK_GESTURE_ZOOM(_gzoom));

        /*
        cairo_pattern_t *pat;
        cairo_matrix_t matrix;
        gdouble angle, scale;

        cairo_get_matrix (cr, &matrix);
        cairo_matrix_translate (&matrix, allocation.width / 2, allocation.height / 2);

        cairo_save (cr);

        angle = gtk_gesture_rotate_get_angle_delta(GTK_GESTURE_ROTATE(_grotate));
        cairo_matrix_rotate(&matrix, angle);

        scale = gtk_gesture_zoom_get_scale_delta(GTK_GESTURE_ZOOM(_gzoom));
        cairo_matrix_scale (&matrix, scale, scale);

        cairo_set_matrix (cr, &matrix);
        cairo_rectangle (cr, -100, -100, 200, 200);

        pat = cairo_pattern_create_linear (-100, 0, 200, 0);
        cairo_pattern_add_color_stop_rgb (pat, 0, 0, 0, 1);
        cairo_pattern_add_color_stop_rgb (pat, 1, 1, 0, 0);
        cairo_set_source (cr, pat);
        cairo_fill (cr);

        cairo_restore (cr);

        cairo_pattern_destroy (pat);
        */
    }

    if (_glong_pressed) {
        /*
        cairo_save (cr);
        cairo_arc (cr, allocation.width / 2, allocation.height / 2, 50, 0, 2 * G_PI);

        cairo_set_source_rgba (cr, 0, 1, 0, 0.5);
        cairo_stroke (cr);

        cairo_restore (cr);
        */
    }

    return TRUE;
}

static void     swipe_gesture_swept(GtkGestureSwipe *gesture,
                                    gdouble          velocity_x,
                                    gdouble          velocity_y,
                                    GtkWidget       *widget)
{
    (void)gesture;

    g_print("swipe_gesture_swept(): \n");

    _gswipe_x = velocity_x / 10;
    _gswipe_y = velocity_y / 10;
    gtk_widget_queue_draw(widget);
}

static void     long_press_gesture_pressed(GtkGestureLongPress *gesture,
                                           gdouble              x,
                                           gdouble              y,
                                           gpointer             user_data)
{
    (void)gesture;
    (void)x;
    (void)y;
    (void)user_data;

    g_print("-----------DEBUG: long_press_gesture_pressed(): start\n");

    _glong_pressed = TRUE;

    return;
}

static void     long_press_gesture_end(GtkGesture       *gesture,
                                       GdkEventSequence *sequence,
                                       gpointer          user_data)
{
    (void)gesture;
    (void)sequence;

    _glong_pressed = FALSE;

    // start draw loop
    _engine.do_S52draw     = TRUE;
    _engine.do_S52drawLast = TRUE;

    (void)user_data;
    //GtkWidget *widget = (GtkWidget *)user_data;
    //gtk_widget_queue_draw(widget);

    g_print("-----------DEBUG: long_press_gesture_end(): \n");

    return;
}
#endif  // 0

static void     rotation_angle_changed(GtkGestureRotate *gesture,
                                       gdouble           angle,
                                       gdouble           delta,
                                       GtkWidget        *widget)
{
    (void)gesture;
    //(void)angle;
    (void)widget;

    // FIXME: test min angle before blitting

    _gdelta = 360.0 - delta*RAD2DEG;
    if (360.0 <= _gdelta) _gdelta -= 360.0;
    if (_gdelta < 0.0)    _gdelta += 360.0;
    S52_drawBlit(0.0, 0.0, _gscale-1.0, _gdelta);

    g_print("XXXXXXXXXXXDEBUG: s52gtkegl:rotation_angle_changed(): angle:%f delta:%f _gdelta:%f\n", angle*RAD2DEG, delta*RAD2DEG, _gdelta);

    return;
}

static void     zoom_scale_changed(GtkGestureZoom *gesture,
                                   gdouble         scale,
                                   gpointer        user_data)
{
    (void)gesture;
    (void)user_data;

    // Note: scale [0.0 .. max] --> scale-1 [-0.5 .. +0.5] (initialy scale is 1:1)
    _gscale = scale;
    S52_drawBlit(0.0, 0.0, _gscale-1.0, _gdelta);

    g_print("XXXXXXXXXXX DEBUG: s52gtkegl:zoom_scale_changed(): rNM:%f scale:%f scale-1:%f\n", _engine.state.rNM, scale, scale-1.0);

    return;
}

static void     drag_beg(GtkGestureDrag *gesture,
                         gdouble         start_x,
                         gdouble         start_y,
                         gpointer        user_data)
{
    (void)gesture;
    (void)user_data;

    g_print("-----------DEBUG: drag_beg(): x:%f y:%f\n", start_x, start_y);

    _gstart_x = start_x;
    _gstart_y = start_y;

    return;
}

static void     drag_upd(GtkGestureDrag *gesture,
                         gdouble         offset_x,
                         gdouble         offset_y,
                         gpointer        user_data)
{
    (void)gesture;

    GtkWidget *drawing_area = (GtkWidget *)user_data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(drawing_area, &allocation);

    //g_print("-----------DEBUG: drag_upd(): w:%i h:%i ox:%f oy:%f\n", allocation.width, allocation.height, offset_x, offset_y);

    double dx_pc = -offset_x / allocation.width;  //
    double dy_pc =  offset_y / allocation.height; //
    S52_drawBlit(dx_pc, dy_pc, 0.0, 0.0);

    return;
}

static void     drag_end(GtkGestureDrag *gesture,
                         gdouble         offset_x,
                         gdouble         offset_y,
                         gpointer        user_data)
{
    (void)gesture;

    GtkWidget *drawing_area = (GtkWidget *)user_data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(drawing_area, &allocation);

    double h  = allocation.height;
    double x0  = _gstart_x;
    double y0  = h - _gstart_y;
    double x1  = x0 + offset_x;
    double y1  = y0 + offset_y;
    S52_xy2LL(&x0, &y0);
    S52_xy2LL(&x1, &y1);
    double dx = x1 - x0;
    double dy = y1 - y0;

    _engine.state.cLat += dy;
    _engine.state.cLon -= dx;
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    // start draw loop
    //_engine.do_S52draw     = TRUE;
    //_engine.do_S52drawLast = TRUE;
    //gtk_widget_queue_draw(drawing_area);

    g_print("-----------DEBUG: drag_end(): \n");

    return;
}

static gboolean _event_cb(GtkWidget *widget,
                          GdkEvent  *event,
                          gpointer   user_data)
{
    //(void)widget;
    (void)user_data;

    static int nSeq = 0;

    /*
    char *str = NULL;
    switch(event->type) {
        case GDK_MOTION_NOTIFY : str = "GDK_MOTION_NOTIFY ";  break;
        case GDK_CONFIGURE     : str = "GDK_CONFIGURE ";      break;

        case GDK_TOUCH_BEGIN   : str = "GDK_TOUCH_BEGIN";     break;
        case GDK_TOUCH_UPDATE  : str = "GDK_TOUCH_UPDATE";    break;
        case GDK_TOUCH_END     : str = "GDK_TOUCH_END";       break;
        case GDK_TOUCH_CANCEL  : str = "GDK_TOUCH_CANCEL";    break;
        default                : str = ">>> EVENT ???";
    }
    g_print("event->type:%i %s\n", event->type, str);
    //*/

    if (GDK_TOUCH_BEGIN  == event->type) {
        ++nSeq;

        // stop draw loop
        _engine.do_S52draw     = FALSE;
        _engine.do_S52drawLast = FALSE;
    }

    //if (GDK_TOUCH_UPDATE == event->type) {
    //}

    if (GDK_TOUCH_END    == event->type) {
        --nSeq;

        if (0 == nSeq) {
            g_print("event->type:GDK_TOUCH_END nSeq:%i\n", nSeq);

            // zoom
            double rNMnew;
            if (_gscale < 1.0) {
                rNMnew = _engine.state.rNM + (_engine.state.rNM * _gscale);
            } else {
                rNMnew = _engine.state.rNM - (_engine.state.rNM * _gscale);
            }
            rNMnew = (rNMnew < 0.0) ? -rNMnew : rNMnew; // ABS()

            // rotation
            double delta = _engine.state.north + _gdelta;
            if (360.0 <= delta) delta -= 360.0;
            if (delta <  0.0  ) delta += 360.0;

            if (TRUE == S52_setView(_engine.state.cLat, _engine.state.cLon, rNMnew, delta)) {
                _engine.state.rNM   = rNMnew;
                _engine.state.north = delta;
                _gdelta             = 0.0;
                _gscale             = 0.0;
            }

            // start draw loop
            _engine.do_S52draw     = TRUE;
            _engine.do_S52drawLast = TRUE;

            gtk_widget_queue_draw(widget);
        }
    }

    if (GDK_TOUCH_CANCEL == event->type) {
        nSeq = 0;

        // start draw loop
        _engine.do_S52draw     = TRUE;
        _engine.do_S52drawLast = TRUE;

        gtk_widget_queue_draw(widget);

        return TRUE;
    }


    /*
    //if (event->type == GDK_LEAVE_NOTIFY) {
    //if (event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) {
    */

    return FALSE;  // event will propagate
    //return TRUE; // stop event propagation
}

static int      _gtk_init_gestures(GtkWidget *window)
{
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window), drawing_area);
    gtk_widget_add_events(drawing_area,
                          GDK_BUTTON_PRESS_MASK   |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_TOUCH_MASK
                         );

    g_signal_connect(drawing_area, "draw",  G_CALLBACK(_s52_draw_cb), &_engine);
    g_signal_connect(drawing_area, "event", G_CALLBACK(_event_cb),    &_engine);

    // Drag
    GtkGesture *_gdrag = gtk_gesture_drag_new(drawing_area);
    g_signal_connect(_gdrag, "drag-begin",  G_CALLBACK(drag_beg), drawing_area);
    g_signal_connect(_gdrag, "drag-update", G_CALLBACK(drag_upd), drawing_area);
    g_signal_connect(_gdrag, "drag-end",    G_CALLBACK(drag_end), drawing_area);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(_gdrag), GTK_PHASE_BUBBLE);
    //g_object_weak_ref(G_OBJECT(drawing_area), (GWeakNotify)g_object_unref, _gdrag);

    // Rotate
    _grotate = gtk_gesture_rotate_new(drawing_area);
    g_signal_connect(_grotate, "angle-changed", G_CALLBACK(rotation_angle_changed), drawing_area);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(_grotate), GTK_PHASE_BUBBLE);
    //g_object_weak_ref(G_OBJECT(drawing_area), (GWeakNotify) g_object_unref, _grotate);

    // Zoom
    _gzoom = gtk_gesture_zoom_new(drawing_area);
    g_signal_connect(_gzoom, "scale-changed", G_CALLBACK(zoom_scale_changed), drawing_area);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(_gzoom), GTK_PHASE_BUBBLE);
    //g_object_weak_ref(G_OBJECT(drawing_area), (GWeakNotify) g_object_unref, _gzoom);


    /*
    // Long press
    //GtkGesture *gesture = gtk_gesture_long_press_new(drawing_area);
    //g_signal_connect(gesture, "pressed", G_CALLBACK(long_press_gesture_pressed), drawing_area);
    //g_signal_connect(gesture, "end",     G_CALLBACK(long_press_gesture_end),     drawing_area);
    //gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture),  GTK_PHASE_BUBBLE);
    //g_object_weak_ref(G_OBJECT(drawing_area), (GWeakNotify) g_object_unref, gesture);

    // Swipe
    GtkGesture *gesture = gtk_gesture_swipe_new(drawing_area);
    g_signal_connect(gesture, "swipe", G_CALLBACK(swipe_gesture_swept), drawing_area);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_BUBBLE);
    g_object_weak_ref(G_OBJECT(drawing_area), (GWeakNotify) g_object_unref, gesture);
    */

    return TRUE;
}
#endif  // GTK_MAJOR_VERSION == 3

static int      _gtk_init(int argc, char** argv)
{
    gtk_init(&argc, &argv);

    // gtk window stuff --------------------------------------------------------
    _engine.eglState.window = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_default_size(GTK_WINDOW(_engine.eglState.window), 800, 600);
    //gtk_window_fullscreen      (GTK_WINDOW(_engine.window));
    gtk_window_set_title       (GTK_WINDOW(_engine.eglState.window), "EGL / OpenGL ES2 in GTK application");

    //gtk_window_set_position(GTK_WINDOW(_engine.window), GTK_WIN_POS_CENTER);
    gtk_window_set_position(GTK_WINDOW(_engine.eglState.window), GTK_WIN_POS_MOUSE);


/*                                        default for toplevel
NONE                          = 1 << 0
GDK_EXPOSURE_MASK             = 1 << 1,
GDK_POINTER_MOTION_MASK       = 1 << 2,
GDK_POINTER_MOTION_HINT_MASK  = 1 << 3,

GDK_BUTTON_MOTION_MASK        = 1 << 4,   X
GDK_BUTTON1_MOTION_MASK       = 1 << 5,
GDK_BUTTON2_MOTION_MASK       = 1 << 6,
GDK_BUTTON3_MOTION_MASK       = 1 << 7,

GDK_BUTTON_PRESS_MASK         = 1 << 8,   X
GDK_BUTTON_RELEASE_MASK       = 1 << 9,   X
GDK_KEY_PRESS_MASK            = 1 << 10,
GDK_KEY_RELEASE_MASK          = 1 << 11,

GDK_ENTER_NOTIFY_MASK         = 1 << 12,
GDK_LEAVE_NOTIFY_MASK         = 1 << 13,
GDK_FOCUS_CHANGE_MASK         = 1 << 14,
GDK_STRUCTURE_MASK            = 1 << 15,

GDK_PROPERTY_CHANGE_MASK      = 1 << 16,
GDK_VISIBILITY_NOTIFY_MASK    = 1 << 17,
GDK_PROXIMITY_IN_MASK         = 1 << 18,
GDK_PROXIMITY_OUT_MASK        = 1 << 19,

GDK_SUBSTRUCTURE_MASK         = 1 << 20,
GDK_SCROLL_MASK               = 1 << 21,
GDK_TOUCH_MASK                = 1 << 22,  X
GDK_SMOOTH_SCROLL_MASK        = 1 << 23,

GDK_TOUCHPAD_GESTURE_MASK     = 1 << 24,
*/

    // gtk widget stuff --------------------------------------------------------
    g_print("EventMask:0x%X\n", gtk_widget_get_events(_engine.eglState.window));
    // =>EventMask:0x400310  -->  0100 0000 0000 0011 0001 0000

    gtk_widget_set_app_paintable     (GTK_WIDGET(_engine.eglState.window), TRUE );
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(_engine.eglState.window), TRUE );

    // FIXME: GTK3 GDK_DEPRECATED_IN_3_14, call work, but not portable.
    gtk_widget_set_double_buffered   (GTK_WIDGET(_engine.eglState.window), FALSE);

    g_signal_connect(G_OBJECT(_engine.eglState.window), "destroy",           G_CALLBACK(gtk_main_quit),     NULL);
    g_signal_connect(G_OBJECT(_engine.eglState.window), "key_release_event", G_CALLBACK(key_release_event), NULL);
    g_signal_connect(G_OBJECT(_engine.eglState.window), "configure_event",   G_CALLBACK(configure_event),   NULL);

    //g_timeout_add(100, _s52_draw_cb, &_engine); // 0.1 sec
    g_timeout_add(500,     _s52_draw_cb, &_engine); // 0.5 sec
    //g_timeout_add(500*4,   _s52_draw_cb, &_engine); // 2.0 sec debug
    //g_timeout_add(500*4*2, _s52_draw_cb, &_engine); // 4.0 sec debug

#if GTK_MAJOR_VERSION == 3
    _gtk_init_gestures(_engine.eglState.window);  // only in GTK3
#endif  // GTK_MAJOR_VERSION == 3

    gtk_widget_show_all(_engine.eglState.window);

    return TRUE;
}

int main(int argc, char** argv)
{
    _gtk_init(argc, argv);

    _egl_init(&_engine.eglState);
    _s52_init(&_engine);

#ifdef USE_AIS
    s52ais_initAIS();
#endif

    gtk_main();

#ifdef USE_AIS
    s52ais_doneAIS();
#endif

    _s52_done(&_engine);
    _egl_done(&_engine.eglState);

    g_print("%s .. done\n", argv[0]);

    return 0;
}
