// s52gtk2.c: test driver for libS52.so & libGDAL.so over GTK
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



#include "S52.h"            // S52_init(), S52_loadCell(), ..
#include <gtk/gtk.h>        // gtk_init(), ..
#include <gdk/gdkkeysyms.h> // GDK_left, ..  (key syms)
#include <GL/gl.h>          // glGenLists() for font and testing GL drwing
#include <string.h>         // strcmp(), memcpy()

#ifdef S52_USE_AIS
#include "s52ais.h"
gboolean update_cb(void *dummy);
#endif

#define  g_print printf     // prevent writing help / dump to log

#if _MINGW
//#define  g_print printf     // prevent writing help / dump to log
#define  g_sprintf sprintf
#else
#include <glib.h>           // g_print()
#include <glib/gprintf.h>   // g_sprintf()
#endif

#include <gtk/gtkgl.h>      // gtk_gl_*(), gdk_gl_*()
#include <gdk/gdkgl.h>      // gtk_gl_*(), gdk_gl_*()

#include <signal.h>         // raise() - abort drawing


typedef struct S52_extent {
    double S,W,N,E;
} S52_extent;

// view
typedef struct S52_view {
    double cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} S52_view;
static S52_view _view;
static double _width  = 0.0;
static double _height = 0.0;
static double _x      = 0.0;
static double _y      = 0.0;
static double _brg    = 0.0;
static double _rge    = 0.0;

typedef struct pt2 {
    double lat, lon;
} pt2;

// scroll to a tenth of the range
#define SCROLL_FAC 0.1
#define ZOOM_FAC   2.0
#define ZOOM_INI   1.0

#define VESSELTURN_UNDEFINED 129

static S52ObjectHandle _vrmeblA     = NULL;
static S52ObjectHandle _vrmeblB     = NULL;
static int             _originIsSet = FALSE;

// VESSEL
static S52ObjectHandle _ownshp      = NULL;
static S52ObjectHandle _vessel_arpa = NULL;
static S52ObjectHandle _vessel_ais  = NULL;

static S52ObjectHandle _pastrk      = NULL;

static S52ObjectHandle _leglin1     = NULL;
static S52ObjectHandle _leglin2     = NULL;
static S52ObjectHandle _leglin3     = NULL;
//static S52ObjectHandle _leglin4     = NULL;
//static S52ObjectHandle _leglin5     = NULL;
//static S52ObjectHandle _route[5]    = {NULL, NULL, NULL, NULL, NULL};
static S52ObjectHandle _waypnt0     = NULL;
static S52ObjectHandle _waypnt1     = NULL;
static S52ObjectHandle _waypnt2     = NULL;
static S52ObjectHandle _waypnt3     = NULL;
static S52ObjectHandle _waypnt4     = NULL;
static S52ObjectHandle _wholin      = NULL;

static S52ObjectHandle _clrlin      = NULL;

static S52ObjectHandle _marfea_area = NULL;
static S52ObjectHandle _marfea_line = NULL;
static S52ObjectHandle _marfea_point= NULL;

static int _doRenderHelp = FALSE;

static GtkWidget    *_win         = NULL;
static GtkWidget    *_winArea     = NULL;

static GTimer       *_timer       = NULL;

// debug - command-line options for test
static gint     _execOpt = FALSE;
static gchar   *_version = NULL;
static gchar   *_outpng  = NULL;
static gint     _s57id   = 0;
static gchar   *_encnm   = NULL;



static int      _usage(const char *arg)
{
    g_print("\n");
    g_print("Usage: %s [-h] [-f] S57..\n", arg);
    g_print("\t-h\t:this help\n");
    g_print("\t-f\t:S57 file to load --else search .cfg\n");
    g_print("\n");
    g_print("Mouse:\n");
    g_print("\tRight\t:recenter\n");
    g_print("\tLeft \t:cursor pick\n");
    g_print("\tCtl-Left\t:set origine VRMEBL\n");
    g_print("\n");
    g_print("Key:\n");
    g_print("\th    \t:this help\n");
    g_print("\tLeft \t:move view to West\n");
    g_print("\tRight\t:move view to Est\n");
    g_print("\tUp   \t:move view to North\n");
    g_print("\tDown \t:move view to South\n");
    g_print("\tPgUp \t:zoom in\n");
    g_print("\tPgDwn\t:zoom out\n");
    g_print("\t+,=  \t:rotate right (and reset)\n");
    g_print("\t-    \t:rotate left  (and reset)\n");
    g_print("\tr    \t:render\n");
    g_print("\tESC  \t:abort draw & reset view\n");
    g_print("\tx    \t:dump all Mariner Parameter\n");
    g_print("\tv    \t:version\n");
    g_print("\tq    \t:quit\n");
    g_print("\n");
    g_print("S52_MAR toggle [ON/OFF] ------------\n");
    g_print("\tw    \t:S52_MAR_TWO_SHADES\n");
    g_print("\ts    \t:S52_MAR_SHALLOW_PATTERN\n");
    g_print("\to    \t:S52_MAR_SHIPS_OUTLINE\n");
    g_print("\tl    \t:S52_MAR_FULL_SECTORS\n");
    g_print("\tb    \t:S52_MAR_SYMBOLIZED_BND\n");
    g_print("\tp    \t:S52_MAR_SYMPLIFIED_PNT\n");
    g_print("\tu    \t:S52_MAR_SCAMIN\n");
    g_print("\t5    \t:S52_MAR_HEADNG_LINE\n");
    g_print("\ttT   \t:S52_MAR_SHOW_TEXT\n");

    g_print("S52_MAR meter  [+-] ------------\n");
    g_print("\tcC   \t:S52_MAR_SAFETY_CONTOUR\n");
    g_print("\tdD   \t:S52_MAR_SAFETY_DEPTH\n");
    g_print("\taA   \t:S52_MAR_SHALLOW_CONTOUR\n");
    g_print("\teE   \t:S52_MAR_DEEP_CONTOUR\n");
    g_print("\tfF   \t:S52_MAR_DISTANCE_TAGS\n");
    g_print("\tgG   \t:S52_MAR_TIME_TAGS\n");
    g_print("\tyY   \t:S52_MAR_BEAM_BGR\n");

    g_print("S52_MAR_DISP_CATEGORY\n");
    g_print("\t7    \t:DISPLAYBASE (0 -> 'D' - 68)\n");
    g_print("\t8    \t:STANDARD    (1 -> 'S' - 83)\n");
    g_print("\t9    \t:OTHER       (2 -> 'O' - 79)\n");
    g_print("\t0    \t:SELECT      (3 -> 'A' - 65)\n");

    g_print("S52_MAR_COLOR_PALETTE\n");
    g_print("\tkK   \t:cycle palette\n");
    g_print("\n");
    g_print("EXPERIMENTAL ----------------\n");
    g_print("\tn    \t:S52_MAR_FONT_SOUNDG [ON/OFF]\n");
    g_print("\tmM   \t:S52_MAR_DATUM_OFFSET [+-] (font_soundg must be ON)\n");
    g_print("\ti    \t:S52_MAR_ANTIALIAS [ON/OFF]\n");
    g_print("\tj    \t:S52_MAR_QUAPNT01 [ON/OFF]\n");
    g_print("\tz    \t:S52_MAR_DISP_OVERLAP [ON/OFF]\n");
    g_print("\t1!   \t:S52_MAR_DISP_LAYER_LAST (0,1,2,3)\n");
    g_print("\t2    \t:S52_MAR_ROT_BUOY_LIGHT (+15 deg)\n");
    g_print("\t3    \t:S52_MAR_DISP_CRSR_POS [ON/OFF]\n");
    g_print("\t4    \t:S52_MAR_DISP_GRATICULE [ON/OFF]\n");
    g_print("\t6^   \t:S52_MAR_DISP_WHOLIN [cycle]\n");
    g_print("\t3    \t:S52_MAR_DISP_LEGEND [ON/OFF]\n");
    g_print("\tF1-6 \t:S52_CMD_WRD_FILTER [F1-6:ON/OFF]\n");
    g_print("\tF7   \t:S52_DOTPITCH_MM_X (+0.01)\n");
    g_print("\tF8   \t:S52_DOTPITCH_MM_Y (+0.01)\n");
    g_print("\tF9   \t:S52_NODATA_LAYER_OFF [ON/OFF]\n");

    return 1;
}

static int      _renderHelp(GtkWidget *widget)
{
    gchar str[80] = {'\0'};

    gint width  = widget->allocation.width;
    gint height = widget->allocation.height;

    gint dy     = 18;
    gint y      = height-100;
    gint x      = 150;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, 0, height, 1, -1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    // background
    {
        unsigned char R, G, B;
        S52_getRGB("UIBCK", &R, &G, &B);
        glColor4ub(R, G, B, 200);
    }

    glBegin(GL_POLYGON);
    glVertex2i(      100,        100);
    glVertex2i(      100, height-100);
    glVertex2i(width-100, height-100);
    glVertex2i(width-100,        100);
    glEnd();


    // border
    {
        unsigned char R, G, B;
        S52_getRGB("UIBDR", &R, &G, &B);
        glColor4ub(R, G, B, 200);
    }

    // S52 say 3 pixel wide, but
    // blending slide everything of by one
    //glLineWidth(3); 
    glLineWidth(2);

    // not on edge of poly (S52 nothing about that)
    glBegin(GL_LINE_LOOP);
    glVertex2i(      100 + 2,        100 + 2);
    glVertex2i(      100 + 2, height-100 - 2);
    glVertex2i(width-100 - 2, height-100 - 2);
    glVertex2i(width-100 - 2,        100 + 2);
    glEnd();

    glDisable(GL_BLEND);


    // text

    //S52_drawStr(50, 50, "UINFF", 3, "test");
    S52_drawStr(x, y-=dy, "UINFF", 1, "UINFF - UI text");
    S52_drawStr(x, y-=dy, "UIBCK", 1, "UIBCK - UI backgound");
    {
        unsigned char R, G, B;
        S52_getRGB("UIBCK", &R, &G, &B);
        printf("UIBCK rgb: %i %i %i\n", R, G, B);
    }

    S52_drawStr(x, y-=dy, "UIBDR", 1, "UIBDR - UI border");
    S52_drawStr(x, y-=dy, "UINFD", 1, "UINFD - conspic");
    S52_drawStr(x, y-=dy, "UINFR", 1, "UINFR - red");
    S52_drawStr(x, y-=dy, "UINFG", 1, "UINFG - green");
    S52_drawStr(x, y-=dy, "UINFO", 1, "UINFO - orange");
    S52_drawStr(x, y-=dy, "UINFB", 1, "UINFB - blue");
    S52_drawStr(x, y-=dy, "UINFM", 1, "UINFM - magenta");
    S52_drawStr(x, y-=dy, "UIAFD", 1, "UIAFD - blue fill area");
    S52_drawStr(x, y-=dy, "UIAFF", 1, "UIAFF - brown fill area");

    S52_drawStr(x, y-=dy, "UINFF", 0, "Font size: 12");
    S52_drawStr(x, y-=dy, "UINFF", 1, "Font size: 14");
    S52_drawStr(x, y-=dy, "UINFF", 2, "Font size: 16");
    S52_drawStr(x, y-=dy, "UINFF", 3, "Font size: 20");

    dy = 14;

    x += 400;
    y  = height-100;
    g_sprintf(str, "S52_MAR_SHOW_TEXT         t %4.1f", S52_getMarinerParam(S52_MAR_SHOW_TEXT));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_TWO_SHADES        w %4.1f", S52_getMarinerParam(S52_MAR_TWO_SHADES));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SAFETY_CONTOUR    c %4.1f", S52_getMarinerParam(S52_MAR_SAFETY_CONTOUR));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SAFETY_DEPTH      d %4.1f", S52_getMarinerParam(S52_MAR_SAFETY_DEPTH));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SHALLOW_CONTOUR   a %4.1f", S52_getMarinerParam(S52_MAR_SHALLOW_CONTOUR));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DEEP_CONTOUR      e %4.1f", S52_getMarinerParam(S52_MAR_DEEP_CONTOUR));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SHALLOW_PATTERN   s %4.1f", S52_getMarinerParam(S52_MAR_SHALLOW_PATTERN));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SHIPS_OUTLINE     o %4.1f", S52_getMarinerParam(S52_MAR_SHIPS_OUTLINE));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISTANCE_TAGS     f %4.1f", S52_getMarinerParam(S52_MAR_DISTANCE_TAGS));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_TIME_TAGS         g %4.1f", S52_getMarinerParam(S52_MAR_TIME_TAGS));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_BEAM_BRG_NM       y %4.1f", S52_getMarinerParam(S52_MAR_BEAM_BRG_NM));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_FULL_SECTORS      l %4.1f", S52_getMarinerParam(S52_MAR_FULL_SECTORS));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SYMBOLIZED_BND    b %4.1f", S52_getMarinerParam(S52_MAR_SYMBOLIZED_BND));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SYMPLIFIED_PNT    p %4.1f", S52_getMarinerParam(S52_MAR_SYMPLIFIED_PNT));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_CATEGORY   7-0 %4.1f", S52_getMarinerParam(S52_MAR_DISP_CATEGORY));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_COLOR_PALETTE     k %4.1f", S52_getMarinerParam(S52_MAR_COLOR_PALETTE));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_FONT_SOUNDG       n %4.1f", S52_getMarinerParam(S52_MAR_FONT_SOUNDG));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DATUM_OFFSET      m %4.1f", S52_getMarinerParam(S52_MAR_DATUM_OFFSET));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_SCAMIN            u %4.1f", S52_getMarinerParam(S52_MAR_SCAMIN));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_ANTIALIAS         i %4.1f", S52_getMarinerParam(S52_MAR_ANTIALIAS));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_QUAPNT01          j %4.1f", S52_getMarinerParam(S52_MAR_QUAPNT01));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_OVERLAP      z %4.1f", S52_getMarinerParam(S52_MAR_DISP_OVERLAP));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_LAYER_LAST   1 %4.1f", S52_getMarinerParam(S52_MAR_DISP_LAYER_LAST));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_ROT_BUOY_LIGHT    2 %4.1f", S52_getMarinerParam(S52_MAR_ROT_BUOY_LIGHT));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_CRSR_POS     3 %4.1f", S52_getMarinerParam(S52_MAR_DISP_CRSR_POS));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_GRATICULE    4 %4.1f", S52_getMarinerParam(S52_MAR_DISP_GRATICULE));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_HEADNG_LINE       5 %4.1f", S52_getMarinerParam(S52_MAR_HEADNG_LINE));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_WHOLIN       6 %4.1f", S52_getMarinerParam(S52_MAR_DISP_WHOLIN));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_LEGEND       3 %4.1f", S52_getMarinerParam(S52_MAR_DISP_LEGEND));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DOTPITCH_MM_X    F7 %4.2f", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_X));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DOTPITCH_MM_Y    F8 %4.2f", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_Y));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_CALIB        3 %4.2f", S52_getMarinerParam(S52_MAR_DISP_CALIB));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_DRGARE_PATTERN 3 %4.2f", S52_getMarinerParam(S52_MAR_DISP_DRGARE_PATTERN));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "S52_MAR_DISP_NODATA_LAYER F9 %4.2f", S52_getMarinerParam(S52_MAR_DISP_NODATA_LAYER));
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    //g_sprintf(str, "S52_CMD_WRD_FILTER     F1-5 %4.1f", S52_getMarinerParam(S52_CMD_WRD_FILTER));
    //S52_drawStr(x, y-=dy, "UINFF", 0, str);

    y-=dy;
    int crntVal = (int) S52_getMarinerParam(S52_CMD_WRD_FILTER);

    g_sprintf(str, "Command Word Filter State:");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F1 - S52_CMD_WRD_FILTER_SY: %s", (S52_CMD_WRD_FILTER_SY & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F2 - S52_CMD_WRD_FILTER_LS: %s", (S52_CMD_WRD_FILTER_LS & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F3 - S52_CMD_WRD_FILTER_LC: %s", (S52_CMD_WRD_FILTER_LC & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F4 - S52_CMD_WRD_FILTER_AC: %s", (S52_CMD_WRD_FILTER_AC & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F5 - S52_CMD_WRD_FILTER_AP: %s", (S52_CMD_WRD_FILTER_AP & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);
    g_sprintf(str, "F6 - S52_CMD_WRD_FILTER_TX: %s", (S52_CMD_WRD_FILTER_TX & crntVal) ? "TRUE" : "FALSE");
    S52_drawStr(x, y-=dy, "UINFF", 0, str);

    return TRUE;
}

static int      _renderCrsrPos(GtkWidget *widget, double x, double y, double _brg, double _rge)
{
    // debug
    //if (FALSE == S52_getMarinerParam(S52_MAR_DISP_CRSR_POS))
    //    return FALSE;

    //gint width  = widget->allocation.width;
    //gint height = widget->allocation.height;

    //gint dy     = 18;
    //gint y      = height-100;

    /*
    glViewport(0, 0, _width, _height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    //glOrtho(0, _width, 0, _height, 1, -1);
    glOrtho(0, _width, _height, 0, 1, -1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        unsigned char R, G, B;
        S52_getRGB("UIBCK", &R, &G, &B);
        glColor4ub(R, G, B, 200);
    }
    //x = 100.0;
    //y = 100.0;

    //glBegin(GL_TRIANGLES);
    glBegin(GL_POLYGON);
    glVertex2i(x,           y);
    glVertex2i(x,           y + 18);
    glVertex2i(x +  8 * 18, y + 18);
    glVertex2i(x +  8 * 18, y);
    glEnd();

    glDisable(GL_BLEND);
    */

    char str[80] = {'\0'};
    sprintf(str, "%05.1f° / %.1f m", _brg, _rge);

    S52_drawStr(x + 5, _height - y - 15, "UINFF", 1, str);

    return TRUE;
}

static int      _S52_init()
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

    // return -1 (not avaible)
    //gint hmm0 = gdk_screen_get_monitor_height_mm(screen, 0);
    //gint hmm1 = gdk_screen_get_monitor_height_mm(screen, 1);
    //gint wmm0 = gdk_screen_get_monitor_width_mm(screen, 0);
    //gint wmm1 = gdk_screen_get_monitor_width_mm(screen, 1);
    //printf("screen monitor 0: (w/h): %i / %i\n", wmm0, hmm0);  // 338 / 270
    //printf("screen monitor 1: (w/h): %i / %i\n", wmm1, hmm1);  // 376 / 301
    //return 0;
    //GdkRectangle dest;
    //gdk_screen_get_monitor_geometry(screen, 0, &dest);
    //gdk_screen_get_monitor_geometry(screen, 0, &dest);

    // debug --info from xrandr
    //w      = 1280;
    //h      = 1024;
    //wmm    = 376;
    //hmm    = 301; // wrong
    //hmm    = 307;

    printf("screen (w/h/wmm/hmm): %i / %i / %i / %i\n", w, h, wmm, hmm);

    // this will trigger:
    // GLib-CRITICAL **: g_string_append: assertion `string != NULL' failed
    //g_string_append(NULL, "test");

    //S52_init(w, h, wmm, hmm);
    //S52_init(w, h, wmm, hmm, _err_cb);
    S52_init(w, h, wmm, hmm, NULL);
    //S52_init(10, 10, 30, 30);

    return TRUE;
}

static char   **_option(int argc, char **argv)
{
    /*
    char *prgnm = *argv;

    argc--;  argv++;
    while (argc--) {

        if (NULL == *argv)
            break;

        if (0 == strcmp(*argv, "-h"))
            _usage(prgnm);

        if (0 == strcmp(*argv, "-f"))
            ;

        ++argv;
    }
    */


    GOptionEntry entries[] =
    {
        { "version",  'v', 0, G_OPTION_ARG_NONE,   &_version, "libS52 version",             NULL },
        { "outpng",   'o', 0, G_OPTION_ARG_STRING, &_outpng,  "output PNG fname",           NULL },
        //{ "s57id",    'i', 0, G_OPTION_ARG_INT,   &_s57id,   "S57 object ID (used w/ o)",  NULL },
        { "encnm",    'e', 0, G_OPTION_ARG_STRING, &_encnm,   "ENC name or $ENC_ROOT",      NULL },
        { NULL }
    };

    {
        GError         *error   = NULL;
        GOptionContext *context = NULL;

        context = g_option_context_new("- test S52 rendering");
        g_option_context_add_main_entries(context, entries, NULL);
        if (FALSE == g_option_context_parse(context, &argc, &argv, &error)) {
            printf("option parsing failed: %s\n", error->message);
            g_error_free(error);
            *argv = NULL;
        } else {
            _execOpt = TRUE;
        }

        g_option_context_free(context);
    }

    return argv;
}

static int      _dumpENV()
{
    {
        //unsigned int value = HUGE_VAL;
        //int nan   = NaN;
        //g_print("int infinity value = -2147483648 =  HUGE_VAL = %i\n", value);
        //g_print("int infinity size = 8 = sizeof(HUGE_VAL) = %i\n", sizeof(value));
        //g_print("int          : sizeof(int)    = %i\n", sizeof(int));
        //g_print("float        : sizeof(float)  = %i\n", sizeof(float));
        //g_print("double       : sizeof(double) = %i\n", sizeof(double));
        //g_print("unsigned long: sizeof(unsigned long)= %i\n",sizeof(unsigned long));
        //exit(0);
    }

    {   // setup env. var for OGR/S57
        //g_print("CURRENT SETUP:\n");
        //g_print("   OGR_S57_OPTIONS = %s \n", g_getenv("OGR_S57_OPTIONS"));
        //g_print("   S57_CSV         = %s \n", g_getenv("S57_CSV"));
        //g_print("   OGRMakefile CFG = %s \n", g_getenv("CFG"));
        //g_print("   gvgeocoord size = %i \n", sizeof(geocoord));
        //g_print("   libgv  HAVE_OGR = %s \n", (gv_have_ogr_support())? "Yes":"No");
        //g_print("   libgv  HAVE_S52 = %s \n", (gv_have_S52_support())? "Yes":"No");


        //g_assert(gv_have_ogr_support());
        //g_assert(gv_have_S52_support());

        // jump into GDB here
        //g_on_error_query (NULL); 
    }

    return 1;
}

static int      _computeView(S52_view *view)
{
    S52_extent ext;

    //if (FALSE == S52_getCellExtent(NULL, &ext))
    if (FALSE == S52_getCellExtent(NULL, &ext.S, &ext.W, &ext.N, &ext.E))
        return FALSE;

    //g_print("extent: S %f, W %f, N %f, E %f\n", ext.s, ext.w, ext.n, ext.e);

    //S52_getCellExtent(_c, &ext);
    //g_print("extent: S %f, W %f, N %f, E %f\n", ext.s, ext.w, ext.n, ext.e);

    view->cLat  =  (ext.N + ext.S) / 2.0;
    view->cLon  =  (ext.E + ext.W) / 2.0;
    view->rNM   = ((ext.N - ext.S) / 2.0) * 60.0;
    view->north = 0.0;

    //view.rNM  = ZOOM_INI;

    return TRUE;
}

static int      _resetView(S52_view *view)
// reset global var
{
    // abort drawing
    raise(SIGINT);

    _computeView(view);

    //S52_setView(view);
    S52_setView(view->cLat, view->cLon, view->rNM, view->north);

    return TRUE;
}

GdkCursor      *_cursor_new(GdkCursorType ctype)
{
    // This data is in X bitmap format, and can be created with the 'bitmap'utility.
#define cursor1_width  16
#define cursor1_height 16
    /*
    static char cursor1_bits[] = {
        0x80, 0x01, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08, 0x08, 0x10, 0x04, 0x20,
        0x82, 0x41, 0x41, 0x82, 0x41, 0x82, 0x82, 0x41, 0x04, 0x20, 0x08, 0x10,
        0x10, 0x08, 0x20, 0x04, 0x40, 0x02, 0x80, 0x01};
 
    static char cursor1mask_bits[] = {
        0x80, 0x01, 0xc0, 0x03, 0x60, 0x06, 0x30, 0x0c, 0x18, 0x18, 0x8c, 0x31,
        0xc6, 0x63, 0x63, 0xc6, 0x63, 0xc6, 0xc6, 0x63, 0x8c, 0x31, 0x18, 0x18,
        0x30, 0x0c, 0x60, 0x06, 0xc0, 0x03, 0x80, 0x01};
    */
    //GdkPixmap *source, *mask;

    //GdkColor fg = { 0, 65535, 0, 0 }; /* Red. */
    //GdkColor bg = { 0, 0, 0, 65535 }; /* Blue. */
    //source = gdk_bitmap_create_from_data(NULL, cursor1_bits,     cursor1_width, cursor1_height);
    //mask   = gdk_bitmap_create_from_data(NULL, cursor1mask_bits, cursor1_width, cursor1_height);
    //cursor = gdk_cursor_new_from_pixmap(source, mask, &fg, &bg, 8, 8);

    //gdk_pixmap_unref(source);
    //gdk_pixmap_unref(mask);

    GdkCursor*  cursor  = gdk_cursor_new(GDK_CROSS);
    //GdkCursor* cursor = gdk_cursor_new(GDK_CROSSHAIR);
    //GdkCursor* cursor = gdk_cursor_new(GDK_CROSS_REVERSE);

    /*
    GdkPixbuf* pixelCursor = gdk_cursor_get_image(cursor);

    int width, height, rowstride, n_channels;
    guchar *pixels, *p;

    n_channels = gdk_pixbuf_get_n_channels(pixelCursor);

    g_assert(gdk_pixbuf_get_colorspace(pixelCursor) == GDK_COLORSPACE_RGB);
    g_assert(gdk_pixbuf_get_bits_per_sample(pixelCursor) == 8);
    g_assert(gdk_pixbuf_get_has_alpha(pixelCursor));
    g_assert(n_channels == 4);

    width     = gdk_pixbuf_get_width    (pixelCursor);
    height    = gdk_pixbuf_get_height   (pixelCursor);
    rowstride = gdk_pixbuf_get_rowstride(pixelCursor);
    pixels    = gdk_pixbuf_get_pixels   (pixelCursor);

    //p = pixels + (height/4 * rowstride) + (width/4 * n_channels);
    //p = pixels + ((height-1) * rowstride) + ((width-1) * n_channels);
    p = pixels;

    p[0] =   0; // R
    p[1] = 255; // G
    p[2] =   0; // B
    p[3] = 255; // A

    GdkDisplay *display = gdk_cursor_get_display(cursor);
    GdkCursor  *c       = gdk_cursor_new_from_pixbuf(display, pixelCursor, width/2, height/2);

    return c;
    */


    return cursor;
}

static gboolean _draw(gpointer data)
{
    //g_print("start _draw()\n");

    gtk_widget_draw((GtkWidget *)data, NULL);

    //g_print("finish _draw()\n");

    return TRUE;
}

static gint     _execOption()
{
    if (NULL != _version) {
        printf("S52 version: %s\n", S52_version());
    } else {
        if (NULL != _encnm) {
            if (FALSE == S52_loadCell(_encnm, NULL)) {
                // load default cell (in s52.cfg)
                //S52_loadCell(NULL, _my_S52_loadObject_cb);
                S52_loadCell(NULL, NULL);
            }
        }
    }

    return TRUE;
}

static void     realize(GtkWidget *widget, gpointer   data)
{

#ifdef S52_USE_AIS
    // Note: data form AIS start too fast for the main loop
    s52ais_initAIS(update_cb);

    // for continuous drawing
    //g_idle_add(_draw, widget);
    //g_idle_add(update_cb, widget);

    //g_print("install hook to loop on gtk_widget_draw when idle\n");
#endif

}

static gboolean configure_event(GtkWidget         *widget,
                                GdkEventConfigure *event,
                                gpointer           data)
{
    GdkGLContext  *glcontext  = gtk_widget_get_gl_context (widget);
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

    if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
        return FALSE;

    _computeView(&_view);

    S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);
    S52_setViewPort(0, 0, widget->allocation.width, widget->allocation.height);

    _width  = widget->allocation.width;
    _height = widget->allocation.height;

    gdk_gl_drawable_gl_end(gldrawable);

    return TRUE;
}


//extern void cogl_begin_gl(void);
//extern void cogl_end_gl(void);
static gboolean expose_event(GtkWidget      *widget,
                             GdkEventExpose *event,
                             gpointer        data)
{
    GdkGLContext  *glcontext  = gtk_widget_get_gl_context (widget);
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

    // test - swith symbol
    //S52_toggleObjSUP(_waypnt1);
    //S52_toggleObjSUP(_waypnt2);


    if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
        g_assert(0);
        return FALSE;
    }

    if (TRUE == _execOpt)
        _execOption();

    //  draw 'base'
    S52_draw();

    /*
    {
        // draw you stuff on top (draw() above) the base chart
        // ...

        static S52_view view;

        _computeView(&view);

        S52_setView(view.cLat, view.cLon, view.rNM, view.north);
        S52_setViewPort(50, 50, 200, 200);

        S52_setMarinerParam(S52_MAR_NODATA_LAYER_OFF, TRUE);  // do not display layer 0 (no data) (default off)

        S52_draw();

        S52_setMarinerParam(S52_MAR_NODATA_LAYER_OFF, FALSE);

        // reset normal view
        S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);
        S52_setViewPort(0, 0, _width, _height);
    }
    */

    // this draw on chart and drawLast() will draw 'above' that
    //if (TRUE == _doRenderHelp)
    //    _renderHelp(widget);

    //_renderCrsrPos(widget, _x, _y, _brg, _rge);

    // draw reste
    S52_drawLast();

    // this draw help 'above' drawLast()
    if (TRUE == _doRenderHelp)
        _renderHelp(widget);

    _renderCrsrPos(widget, _x, _y, _brg, _rge);

    if (gdk_gl_drawable_is_double_buffered(gldrawable))
        gdk_gl_drawable_swap_buffers(gldrawable);

    gdk_gl_drawable_gl_end(gldrawable);

    if (TRUE == _execOpt) {
        if (NULL != _outpng) {
            S52_dumpS57IDPixels(_outpng, _s57id, 200, 200);
            gtk_main_quit();
        }
    }
    // FIXME: two expose event .. why?
    _execOpt = FALSE;

    return TRUE;
    //return FALSE;
}

static gboolean _scroll(GtkWidget *widget, GdkEventKey *event)
{
    switch(event->keyval) {
        case GDK_Left : _view.cLon -= _view.rNM/(60.0*10.0); S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
        case GDK_Right: _view.cLon += _view.rNM/(60.0*10.0); S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
        case GDK_Up   : _view.cLat += _view.rNM/(60.0*10.0); S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
        case GDK_Down : _view.cLat -= _view.rNM/(60.0*10.0); S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
    }

    return TRUE;
}

static gboolean _zoom(GtkWidget *widget, GdkEventKey *event)
{
    switch(event->keyval) {
        // zoom in
    	case GDK_Page_Up  : _view.rNM /= 2.0; S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
        // zoom out
        case GDK_Page_Down: _view.rNM *= 2.0; S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north); break;
    }

    return TRUE;
}

static gboolean _rotation(GtkWidget *widget, GdkEventKey *event)
{
    //S52_getView(&_view);

    switch(event->keyval) {
        // -
        case GDK_minus:
            _view.north += 1.0;
            if (360.0 < _view.north)
                _view.north -= 360.0;
            break;
        // +
        case GDK_equal:
        case GDK_plus :
            _view.north -= 1.0;
            if (_view.north < 0.0)
                _view.north += 360.0;
            break;
    }

    //S52_setView(&_view);
    S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);

    return TRUE;
}

static gboolean _toggle(S52MarinerParameter paramName)
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

static gboolean _disp(S52MarinerParameter paramName, const char disp)
{
    double val = (double) disp;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _cpal(S52MarinerParameter paramName, double val)
{
    val = S52_getMarinerParam(paramName) + val;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _inc(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName) + 15.0;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _mmInc(S52MarinerParameter paramName)
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
    g_print("S52_MAR_DISP_CRSR_POS     3 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_CRSR_POS));
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

static gboolean key_release_event(GtkWidget   *widget,
                                  GdkEventKey *event,
                                  gpointer     data)
{
    switch(event->keyval) {
        case GDK_Left  :
        case GDK_Right :
        case GDK_Up    :
        case GDK_Down  :_scroll(widget, event);            break;

        case GDK_equal :
        case GDK_plus  :
        case GDK_minus :_rotation(widget, event);          break;

        case GDK_Page_Down:
        case GDK_Page_Up:_zoom(widget, event);             break;


        case GDK_Escape:_resetView(&_view);                break;
        case GDK_r     : /*gtk_widget_draw(widget, NULL);*/break;
        case GDK_h     :_doRenderHelp = !_doRenderHelp;
                        _usage("s52gtk2");
                        break;
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

        case GDK_3     :_toggle(S52_MAR_DISP_CRSR_POS);
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
        case GDK_0     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT);  break; // OTHER (all)

        case GDK_k     :_cpal(S52_MAR_COLOR_PALETTE,  1.0);break;
        case GDK_K     :_cpal(S52_MAR_COLOR_PALETTE, -1.0);break;

        case GDK_6     :_meterInc(S52_MAR_DISP_WHOLIN);    break;
        case GDK_asciicircum:
        case GDK_question:
        case GDK_caret :_meterDec(S52_MAR_DISP_WHOLIN);    break;


        //case GDK_3     :_cpal("S52_MAR_COLOR_PALETTE", 2.0); break; // DAY_WHITEBACK
        //case GDK_4     :_cpal("S52_MAR_COLOR_PALETTE", 3.0); break; // DUSK
        //case GDK_5     :_cpal("S52_MAR_COLOR_PALETTE", 4.0); break; // NIGHT

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

    // redraw
    gtk_widget_draw(widget, NULL);

    return TRUE;
}

static gboolean button_release_event(GtkWidget      *widget,
                                     GdkEventButton *event,
                                     gpointer        data)
{

    // Ctl + left click: set origine for 'freely movable' VRMEBL
    if ((GDK_CONTROL_MASK & event->state) && (1==event->button)) {
        double x = event->x;
        double y = event->y;

        // set origine 
        //S52_setVRMEBLorigine(_vrmebl, x, y);
        //S52_setVRMEBL(_vrmebl, x, y, TRUE);

        double brg = 0.0;
        double rge = 0.0;
        S52_setVRMEBL(_vrmeblB, x, y, &brg, &rge);
        _originIsSet = TRUE;

        return TRUE;
    }

    switch(event->button) {
        case 3: // right click
            {
                //S52_centerAt(event->x, event->y);
                double x = event->x;
                double y = event->y;
                S52_xy2LL(&x, &y);

                _view.cLat = y;
                _view.cLon = x;
                S52_setView(_view.cLat, _view.cLon, _view.rNM, _view.north);
                gtk_widget_draw(widget, NULL);

                break; 
            }
        case 1: // left click
            {
                // FIXME: why this does work without this
                // because same context
                GdkGLContext  *glcontext  = gtk_widget_get_gl_context (widget);
                GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

                if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
                    return FALSE;

                const char *name = S52_pickAt(event->x, event->y);
                if (NULL != name)
                    g_print("OBJ(%.f, %.f): %s\n", event->x, event->y, name);

                S52_draw();
                S52_drawLast();

                if (gdk_gl_drawable_is_double_buffered(gldrawable))
                    gdk_gl_drawable_swap_buffers(gldrawable);

                gdk_gl_drawable_gl_end(gldrawable);

                gtk_widget_draw(widget, NULL);

                break;
            }
        default:
            g_print("EVENT(button_release_event)\n");
    }


    return TRUE;
}

static gboolean motion_notify_event(GtkWidget      *widget,
                                    GdkEventMotion *event,
                                    gpointer        data)
{
    GdkGLContext  *glcontext  = gtk_widget_get_gl_context (widget);
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

    _x = event->x;
    _y = event->y;

    /* debug, for fun: S52_xy2LL() --> S52_LL2xy() should be the same
    {
    // NOTE:  (0,0) the OpenGL origine (not GTK origine)
        double Xlon = 0.0;
        double Ylat = 0.0;
        S52_xy2LL(&Xlon, &Ylat);
        S52_LL2xy(&Xlon, &Ylat);
        printf("xy2LL(0,0) --> LL2xy ==  Xlon: %f  Ylat: %f\n", Xlon, Ylat);
    }
    */

    if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
        return FALSE;

    _brg = 0.0;
    _rge = 0.0;
    S52_setVRMEBL(_vrmeblA, _x, _y, &_brg, &_rge);

    if (TRUE == _originIsSet) {
        S52_setVRMEBL(_vrmeblB, _x, _y, &_brg, &_rge);
    }

    //*
    if (TRUE == S52_drawLast()) {

        if (TRUE == _doRenderHelp)
            _renderHelp(widget);

        _renderCrsrPos(widget, _x, _y, _brg, _rge);

        if (gdk_gl_drawable_is_double_buffered(gldrawable))
            gdk_gl_drawable_swap_buffers(gldrawable);
    }
    //*/

    gdk_gl_drawable_gl_end(gldrawable);

    return TRUE;
}

static int      _setOWNSHP()
{
    //_ownshp = S52_newOWNSHP("OWNSHP");
    _ownshp = S52_newOWNSHP(NULL);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 0.0, 30.0);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 15.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 100.0, 0.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 0.0, 15.0, 0.0);
    _ownshp = S52_setDimension(_ownshp, 0.0, 100.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 1000.0, 50.0, 15.0, 15.0);
 
    // position 'ownshp' with reasonable value
    _computeView(&_view);

    //S52_setPosition(_ownshp, _view.cLat, _view.cLon, 030.0);
    //S52_setPosition(_ownshp, _view.cLat, _view.cLon, 000.0);
    //S52_setPosition(_ownshp, _view.cLat, _view.cLon, 180.0+045.0);
    S52_pushPosition(_ownshp, _view.cLat, _view.cLon, 180.0+045.0);

    S52_setVector(_ownshp, 0, 220.0, 6.0);  // ownship use S52_MAR_VECSTB

    return TRUE;
}

static int      _setVRMEBL()
{
    // normal VRM/EBL
    //_vrmeblA = S52_newVRMEBL(TRUE, TRUE, TRUE, FALSE);

    // normal VRM (vrmark)
    _vrmeblA = S52_newVRMEBL(TRUE, FALSE, FALSE, FALSE);

    // alterned VRM/EBL line style
    //_vrmebl = S52_newVRMEBL(TRUE, TRUE, FALSE);

    // alternate VRM/EBL, freely moveable
    //_vrmeblB = S52_iniVRMEBL(TRUE, TRUE, FALSE, TRUE);
    // alternate VRM, freely moveable
    //_vrmeblB = S52_newVRMEBL(TRUE, FALSE, FALSE, TRUE);

    return TRUE;
}

static int      _setVESSEL()
{
    //int dummy = 0;

    _computeView(&_view);

    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _view.cLat + 0.01, _view.cLon - 0.02, 045.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    //_vessel_ais = S52_newVESSEL(2, 1, "MV Non Such");
    //_vessel_ais = S52_newVESSEL(2, "MV Non Such");
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_setPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);
    S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);
    //S52_setVector(_vessel_ais, 1, 060.0, 3.0);   // ground

    // (re) set label
    //S52_setVESSELlabel(_vessel_ais, "~~MV Non Such~~");
    S52_setVESSELstate(_vessel_ais, 1, 1, VESSELTURN_UNDEFINED);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_setPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 045.0);

    // VTS (this will not draw anything!)
    //_vessel_vts = S52_newVESSEL(3, dummy);


    return TRUE;
}

static int      _setPASTRK()
{
    _pastrk = S52_newPASTRK(1, 10);

    _computeView(&_view);
    //S52_getView(&_view);


    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.01, _view.cLon - 0.01, 1.0);
    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.01, _view.cLon + 0.01, 2.0);
    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.02, _view.cLon + 0.02, 3.0);
    //S52_addPosition(_pastrk, _view.cLat + 0.01, _view.cLon - 0.01, 1.0);
    //S52_addPosition(_pastrk, _view.cLat + 0.01, _view.cLon + 0.01, 2.0);
    //S52_addPosition(_pastrk, _view.cLat + 0.02, _view.cLon + 0.02, 3.0);
    S52_pushPosition(_pastrk, _view.cLat + 0.01, _view.cLon - 0.01, 1.0);
    S52_pushPosition(_pastrk, _view.cLat + 0.01, _view.cLon + 0.01, 2.0);
    S52_pushPosition(_pastrk, _view.cLat + 0.02, _view.cLon + 0.02, 3.0);

    // test failure
    //S52_addPASTRKPosition(NULL, _view.cLat + 0.03, _view.cLon + 0.03, 3.0);
    //S52_addPASTRKPosition((S52ObjectHandle)0x01, _view.cLat + 0.03, _view.cLon + 0.03, 3.0);
    //S52_addPASTRKPosition(_pastrk, 91.0, _view.cLon + 0.03, 3.0);
    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.03, 181.0, 3.0);
    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.03, _view.cLon + 0.03, -3.0);


    // SW - NE
    //S52_addPASTRKPosition(_pastrk, _view.cLat - 0.01, _view.cLon - 0.01, 1.0);
    //S52_addPASTRKPosition(_pastrk, _view.cLat + 0.01, _view.cLon + 0.01, 1.0);
    // vertical
    //S52_addPASTRKPosition(_pastrk, _view.cLat - 0.01, _view.cLon + 0.01, 1.0);
    // horizontal
    //S52_addPASTRKPosition(_pastrk, _view.cLat - 0.01, _view.cLon - 0.01, 1.0);

    return TRUE;
}

static int      _setRoute()
{

    _computeView(&_view);
    //S52_getView(&_view);

    /*
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, _view.cLat - 0.01, _view.cLon - 0.01, _view.cLat - 0.010, _view.cLon + 0.010);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, _view.cLat - 0.01, _view.cLon + 0.01, _view.cLat + 0.010, _view.cLon + 0.010);
    _leglin3  = S52_newLEGLIN(1, 12.0, 0.2, _view.cLat + 0.01, _view.cLon + 0.01, _view.cLat - 0.010, _view.cLon - 0.010);
    _leglin4  = S52_newLEGLIN(1, 12.0, 0.2, _view.cLat - 0.01, _view.cLon + 0.01, _view.cLat - 0.015, _view.cLon + 0.015);
    _leglin5  = S52_newLEGLIN(1, 12.0, 0.2, _view.cLat - 0.01, _view.cLon + 0.01, _view.cLat - 0.005, _view.cLon + 0.015);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    _route[2] = _leglin3;
    S52_setRoute(3, _route);

    _route[0] = _leglin2;
    _route[1] = _leglin3;
    _route[2] = _leglin4;
    S52_setRoute(3, _route);

    //_route[0] = _leglin1;
    //_route[1] = _leglin4;
    //S52_setRoute(2, _route);

    //_route[0] = _leglin1;
    //_route[1] = _leglin5;
    //S52_setRoute(2, _route);
    */

    /*
    //_leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpN.lat, wpN.lon, wpW.lat, wpW.lon);
    //_leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpW.lat, wpW.lon, wpS.lat, wpS.lon);
    //_leglin3  = S52_newLEGLIN(1, 12.0, 0.2, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    //_leglin4  = S52_newLEGLIN(1, 12.0, 0.2, wpE.lat, wpE.lon, wpN.lat, wpN.lon);

    _route[0] = _leglin1;
    _route[1] = _leglin2;
    _route[2] = _leglin3;
    _route[3] = _leglin4;
    S52_setRoute(4, _route);
    */


    pt2 wpN = {_view.cLat + 0.01, _view.cLon       };
    pt2 wpE = {_view.cLat       , _view.cLon + 0.01};
    pt2 wpS = {_view.cLat - 0.01, _view.cLon       };
    pt2 wpW = {_view.cLat       , _view.cLon - 0.01};

    //-----------------------------
    /*
    // heading change from NW to NE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpS.lat, wpS.lon, wpW.lat, wpW.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpW.lat, wpW.lon, wpN.lat, wpN.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    /*
    // heading change from NE to NW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpE.lat, wpE.lon, wpN.lat, wpN.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    //-----------------------------
    /*
    // heading change from NW to SW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpE.lat, wpE.lon, wpN.lat, wpN.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpN.lat, wpN.lon, wpW.lat, wpW.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    /*
    // heading change from SW to NW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpE.lat, wpE.lon, wpS.lat, wpS.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpW.lat, wpW.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    //-----------------------------
    /*
    // heading change from SE to NE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpW.lat, wpW.lon, wpS.lat, wpS.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    /*
    // heading change from NE to SE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpW.lat, wpW.lon, wpN.lat, wpN.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpN.lat, wpN.lon, wpE.lat, wpE.lon);
    _route[0] = _leglin1;
    _route[1] = _leglin2;
    S52_setRoute(2, _route);
    //*/

    //-----------------------------
    //*
    // heading change from SW to SE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpN.lat, wpN.lon, wpW.lat, wpW.lon, NULL);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.2, wpW.lat, wpW.lon, wpS.lat, wpS.lon, _leglin1);
    _leglin3  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpE.lat, wpE.lon, _leglin2);
    //S52_toggleObjSUP(_leglin1);
    //_route[0] = _leglin1;
    //_route[1] = _leglin2;
    //_route[2] = _leglin3;
    //S52_setRoute(3, _route);
    //S52_setCurveLeg(_leglin1, _leglin2);
    //*/



    {   // waypoint
        char attVal1[] = "";            // next waypoint on planned route
        char attVal2[] = "select:1";    // waypoint on planned route
        char attVal3[] = "select:2";    // waypoint on alternate planned route
        double xyz1[3] = {_view.cLon - 0.01, _view.cLat - 0.01, 0.0};
        double xyz2[3] = {_view.cLon + 0.01, _view.cLat - 0.01, 0.0};
        double xyz3[3] = {_view.cLon + 0.01, _view.cLat + 0.01, 0.0};
        double xyz4[3] = {wpW.lon,           wpW.lat,           0.0};

        _waypnt0 = S52_newMarObj("waypnt", S52_POINT, 1, xyz1,  attVal2);
        S52_toggleDispMarObj(_waypnt0); // off
        _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, xyz1,  attVal1);
        _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, xyz2,  attVal2);
        _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, xyz3,  attVal3);
        _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, xyz4,  attVal1);

        // test
        S52_toggleDispMarObj(_waypnt0); // on
        S52_toggleDispMarObj(_waypnt2); // off
        // test - move to wp2 pos
        //S52_updObjGeo(_waypnt1, 1, xyz2);
        //S52_addPosition(_waypnt1,  _view.cLat-0.01, _view.cLon+0.01, 0.0);
        S52_pushPosition(_waypnt1,  _view.cLat-0.01, _view.cLon+0.01, 0.0);

        // test - over drawn a normal WP over an active WP (ugly)
        //_waypnt4 = S52_newMarObj("waypnt", S52_POINT_T, 1, xyz1,  attVal2);
    }

    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_view.cLon, _view.cLat, 0.0,  _view.cLon + 0.01, _view.cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }

    return TRUE;
}

static int      _setCLRLIN()
{
    _computeView(&_view);
    //S52_getView(&_view);

    _clrlin = S52_newCLRLIN(1, _view.cLat - 0.02, _view.cLon - 0.02, _view.cLat - 0.02, _view.cLon + 0.01);


    return TRUE;
}

static int      _radar_cb()
{
    //g_print("_radar_cb()\n");

    return TRUE;
}

static int      _my_S52_loadObject_cb(const char *objname,   void *shape)
{
    //
    // .. do something cleaver with each object of a layer ..
    //

    // this fill the terminal
    //printf("\tOBJECT NAME: %s\n", objname);

    return S52_loadObject(objname, shape);

    //return TRUE;
}

#if 0
static int      _my_S52_loadLayer_cb (const char *layername, void *layer, S52_loadObject_cb loadObject_cb)
{
    //
    // .. do something cleaver with each layer ..
    //

    //printf("\n**** LAYER NAME: %s\n", layername);

    return S52_loadLayer(layername, layer, _my_S52_loadObj_cb);

    //return TRUE;
}
#endif

static int      _setMarFeature()
// exemple to display something define in the PLib directly
{
    /*
    // CCW doesn't center text
    double xyz[5*3] = {
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.005, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.005, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.000, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
    };
    */

    /*
    // AREA (CW: to center the text)
    double xyzArea[5*3]  = {
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.000, 0.0,
        _view.cLon - 0.01, _view.cLat + 0.005, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.005, 0.0,
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
    };
    */

    // LINE
    double xyzLine[2*3]  = {
        _view.cLon + 0.00, _view.cLat + 0.000, 0.0,
        _view.cLon + 0.02, _view.cLat - 0.005, 0.0
    };

    // debug
    //double xyzLine[2*3]  = {
    //    -72.3166666, 46.416666, 0.0,
    //    -72.4,       46.4,      0.0
    //};

    // POINT
    double xyzPoint[1*3] = {
        _view.cLon - 0.02, _view.cLat - 0.005, 0.0
    };

    // add the text
    char attVal[] = "OBJNAM:6.5_marfea";

    //_marfea_area  = S52_newMarObj("marfea", S52_AREAS, 5, xyzArea,  attVal);
    //_marfea_area  = S52_newMarObj("marfea", S52_AREAS, 5, NULL,  attVal);
    //_marfea_line  = S52_newMarObj("marfea", S52_LINES, 2, xyzLine,  attVal);
    _marfea_point = S52_newMarObj("marfea", S52_POINT, 1, xyzPoint, attVal);

    //S52_pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon + 0.00, 0.0);
    //S52_pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon - 0.01, 0.0);
    //S52_pushPosition(_marfea_area, _view.cLat + 0.005, _view.cLon - 0.01, 0.0);
    //S52_pushPosition(_marfea_area, _view.cLat + 0.005, _view.cLon + 0.00, 0.0);
    //S52_pushPosition(_marfea_area, _view.cLat + 0.000, _view.cLon + 0.00, 0.0);

    return TRUE;
}

static int      _setupS52()
// setup some decent setting for testing
{
    int ret = FALSE;

    _S52_init();


    // load default cell (in s52.cfg)
    //S52_loadCell(NULL, _my_S52_loadObject_cb);
    S52_loadCell(NULL, NULL);


    ////////////////////////////////////////////////////////////
    //
    // setup supression of chart object (for debugging)
    //
    // supresse display of adminitrative objects when
    // S52_MAR_DISP_CATEGORY is SELECT, to avoid cluttering
    //S52_toggleObjClass("M_NSYS");   // cell limit (line complex --A--B-- ), buoyage (IALA)
    //S52_toggleObjClass("M_COVR");   // ??
    //S52_toggleObjClass("M_NPUB");   // ??

    // debug - M_QUAL - the U pattern
    //S52_toggleObjClass("M_QUAL");  
    //ret = S52_toggleObjClassOFF("M_QUAL");  // OK - ret == TRUE
    //ret = S52_toggleObjClassON ("M_QUAL");  // OK - ret == TRUE
    //ret = S52_toggleObjClassON ("M_QUAL");  // OK - ret == FALSE
    S52_setS57ObjClassSupp("M_QUAL", TRUE);

    // test
    //S52_toggleObjClass("DRGARE");   // drege area


    ////////////////////////////////////////////////////////////
    //
    // setup internal variable to decent value for debugging
    //
    /*
    S52_setMarinerParam("S52_MAR_SHOW_TEXT",       1.0);
    S52_setMarinerParam("S52_MAR_TWO_SHADES",      0.0);
    S52_setMarinerParam("S52_MAR_SAFETY_CONTOUR", 10.0);
    S52_setMarinerParam("S52_MAR_SAFETY_DEPTH",   10.0);
    S52_setMarinerParam("S52_MAR_SHALLOW_CONTOUR", 5.0);
    S52_setMarinerParam("S52_MAR_DEEP_CONTOUR",   11.0);

    S52_setMarinerParam("S52_MAR_SHALLOW_PATTERN", 0.0);
    //S52_setMarinerParam("S52_MAR_SHALLOW_PATTERN", 1.0);

    S52_setMarinerParam("S52_MAR_SHIPS_OUTLINE",   1.0);
    S52_setMarinerParam("S52_MAR_DISTANCE_TAGS",   0.0);
    S52_setMarinerParam("S52_MAR_TIME_TAGS",       0.0);
    S52_setMarinerParam("S52_MAR_BEAM_BRG_NM",     1.0);

    S52_setMarinerParam("S52_MAR_FULL_SECTORS",    1.0);
    S52_setMarinerParam("S52_MAR_SYMBOLIZED_BND",  1.0);
    S52_setMarinerParam("S52_MAR_SYMPLIFIED_PNT",  1.0);

    //S52_setMarinerParam("S52_MAR_DISP_CATEGORY",  2.0);  // OTHER
    S52_setMarinerParam("S52_MAR_DISP_CATEGORY",   1.0);  // STANDARD (default)

    S52_setMarinerParam("S52_MAR_COLOR_PALETTE",   0.0);  // first palette

    //S52_setMarinerParam("S52_MAR_FONT_SOUNDG",    1.0);
    S52_setMarinerParam("S52_MAR_FONT_SOUNDG",     0.0);

    S52_setMarinerParam("S52_MAR_DATUM_OFFSET",    0.0);
    //S52_setMarinerParam("S52_MAR_DATUM_OFFSET",    5.0);

    S52_setMarinerParam("S52_MAR_SCAMIN",          1.0);
    //S52_setMarinerParam("S52_MAR_SCAMIN",          0.0);

    // remove clutter
    S52_setMarinerParam("S52_MAR_QUAPNT01",        0.0);
    */

    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       15.0);
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,      60.0);
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       0.0);
    S52_setMarinerParam(S52_MAR_SHOW_TEXT,       1.0);
    S52_setTextDisp(0, 100, TRUE);                      // show all text
    // debug
    //S52_setTextDisp(21, 1, FALSE);                      // BOYLAT

    S52_setMarinerParam(S52_MAR_TWO_SHADES,      0.0);
    S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR, 10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,   10.0);
    S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 5.0);
    S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   11.0);

    S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 0.0);
    //S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 1.0);

    S52_setMarinerParam(S52_MAR_SHIPS_OUTLINE,   1.0);
    //S52_setMarinerParam(S52_MAR_DISTANCE_TAGS,   1.0);
    S52_setMarinerParam(S52_MAR_DISTANCE_TAGS,   0.0);
    S52_setMarinerParam(S52_MAR_TIME_TAGS,       0.0);
    S52_setMarinerParam(S52_MAR_HEADNG_LINE,     1.0);
    //S52_setMarinerParam(S52_MAR_HEADNG_LINE,     0.0);
    S52_setMarinerParam(S52_MAR_BEAM_BRG_NM,     1.0);

    //S52_setMarinerParam(S52_MAR_FULL_SECTORS,    1.0);
    S52_setMarinerParam(S52_MAR_FULL_SECTORS,    0.0);
    S52_setMarinerParam(S52_MAR_SYMBOLIZED_BND,  1.0);
    S52_setMarinerParam(S52_MAR_SYMPLIFIED_PNT,  1.0);

    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  0.0);  // BASE
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  1.0);  // STANDARD (default)
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  2.0);  // OTHER
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  3.0);  // SELECT (all)
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  S52_MAR_DISP_CATEGORY_BASE);  // BASE
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  S52_MAR_DISP_CATEGORY_STD);   // STANDARD (default)
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,  S52_MAR_DISP_CATEGORY_OTHER); // OTHER
    S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_SELECT);   // SELECT (all)

    S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   0.0);   // first palette

    //S52_setMarinerParam(S52_MAR_FONT_SOUNDG,    1.0);
    S52_setMarinerParam(S52_MAR_FONT_SOUNDG,     0.0);

    S52_setMarinerParam(S52_MAR_DATUM_OFFSET,    0.0);
    //S52_setMarinerParam(S52_MAR_DATUM_OFFSET,    5.0);

    S52_setMarinerParam(S52_MAR_SCAMIN,          1.0);    // on
    //S52_setMarinerParam(S52_MAR_SCAMIN,          0.0);   // off

    // remove QUAPNT01 symbole (black diagonal and a '?')
    S52_setMarinerParam(S52_MAR_QUAPNT01,        0.0);


    //--------  SETTING FOR CHART NO 1 (PLib C1 3.1) --------
    // Soundings      ON
    // Text           ON
    // Depth Shades    4
    // Safety Contour 10 m
    // Safety Depth    7 m
    // Shallow         5 m
    // Deep           30 m
    /*
    S52_setMarinerParam("S52_MAR_SHOW_TEXT",        1.0);
    S52_setMarinerParam("S52_MAR_DISP_CATEGORY",    3.0); // OTHER_ALL - show all
    S52_setMarinerParam("S52_MAR_TWO_SHADES",       0.0); // Depth Shades
    S52_setMarinerParam("S52_MAR_SAFETY_CONTOUR",  10.0);
    S52_setMarinerParam("S52_MAR_SAFETY_DEPTH",     7.0);
    S52_setMarinerParam("S52_MAR_SHALLOW_CONTOUR",  5.0);
    S52_setMarinerParam("S52_MAR_DEEP_CONTOUR",    30.0);
    */
    //-------------------------------------------------------

    // showing off (OpenGL blending)
    S52_setMarinerParam(S52_MAR_ANTIALIAS,        1.0);



    ////////////////////////////////////////////////////////////
    //
    // setup mariner object (for debugging)
    //

    // load additional PLib (facultative)
    //S52_loadPLib("plib_pilote.rle");
    //S52_loadPLib("plib-test2.rle");

    // load auxiliary PLib (fix waypnt/WAYPNT01, OWNSHP vector)
    S52_loadPLib("PLAUX_00.DAI");

    // load PLib from s52.cfg indication
    S52_loadPLib(NULL);

    // lastest (S52 ed 6.0) IHO colors from www.ecdisregs.com
    S52_loadPLib("plib_COLS-3.4.1.rle");

    // debug - turn off rendering of last layer
    // very slow on some machine
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, 0.0);  // none
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, 1.0);  // Mariner Standard
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, 2.0);  // Mariner Other (EBL VRN)
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, 3.0);    // All Mariner (Standard + Other)
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_NONE);  // none
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_STD);  // Mariner Standard
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_OTHER);  // Mariner Other (EBL VRN)
    S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_STD | S52_MAR_DISP_LAYER_LAST_OTHER);    // All Mariner (Standard + Other)

    // cell's legend
    //S52_setMarinerParam(S52_MAR_DISP_LEGEND, 1.0);   // show
    S52_setMarinerParam(S52_MAR_DISP_LEGEND, 0.0);     // hide

    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, 0.3);
    S52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, 0.3);

    // init OWNSHP
    _setOWNSHP();

    // init decoration (scale bar, North arrow, unit)
    S52_newCSYMB();

    _setVRMEBL();

    _setVESSEL();

    _setPASTRK();

    _setRoute();

    _setCLRLIN();

    S52_setRADARCallBack(_radar_cb);

    _setMarFeature();


    g_print("PLibList    : %s\n", S52_getPLibsIDList());
    g_print("PalettesList: %s\n", S52_getPalettesNameList());
    g_print("CellNameList: %s\n", S52_getCellNameList());
    g_print("ObjClassList: %s\n", S52_getS57ClassList("CA579016.000"));
    g_print("ObjList: %s\n",      S52_getObjList("CA579016.000", "ACHARE"));
    g_print("attList: %s\n",      S52_getAttList(410));

    // debug - suppress display of 'waypnt'
    // atlernate waypnt on MAR STD, normal waypnt on BASE
    //S52_setS57ObjClassSupp("waypnt", TRUE);

    return TRUE;
}

//#if 0
static int      _err_cb(const char *err)
{
    printf("%s\n", err);

    return TRUE;
}
//#endif

#ifdef S52_USE_AIS
//static
gboolean update_cb(void *dummy)
{
    GdkGLContext  *glcontext  = gtk_widget_get_gl_context (_winArea);
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(_winArea);

    // debug
    //printf("update_cb..\n");


    //g_timer_reset(_timer);


    if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
        return FALSE;


    if (TRUE == S52_drawLast()) {

        if (TRUE == _doRenderHelp)
            _renderHelp(_winArea);

        if (gdk_gl_drawable_is_double_buffered(gldrawable))
            gdk_gl_drawable_swap_buffers(gldrawable);
    }

    gdk_gl_drawable_gl_end(gldrawable);

    /* debug
    static int takeScreenShot = TRUE;
    if (TRUE == takeScreenShot) {
        //S52_dumpS57IDPixels("test.png", 954, 200, 200); // waypnt
        S52_dumpS57IDPixels("test.png", 556, 200, 200); // land
        //S52_dumpS57IDPixels("test.gif", 954); // waypnt
        //S52_dumpS57IDPixels("test.tif", 954); // waypnt
    }
    takeScreenShot = FALSE;
    //*/


    //gdouble sec = g_timer_elapsed(_timer, NULL);
    //PRINTF("%.0f msec (%i obj / %i cmd)\n", sec * 1000, _nobj, _ncmd);
    //g_print("%.0f msec (%i obj / %i cmd) renew = %i / %iB\n", sec * 1000, _nobj, _ncmd, _nrealloc, fobj.sz);
    //g_print("DRAW: %.0f msec\n", sec * 1000);
    //printf("update_cb: %.0f msec\n", sec * 1000);

    return TRUE;
}
#endif



int main(int argc, char **argv)
{
    //GtkWidget    *win      = NULL;
    //GtkWidget    *_winArea  = NULL;
    GdkGLConfig  *glconfig = NULL;
    //GdkGLConfigMode mode  = GDK_GL_MODE_RGBA | GDK_GL_MODE_DEPTH | GDK_GL_MODE_STENCIL | GDK_GL_MODE_DOUBLE;
    GdkGLConfigMode mode;
    //GThreadFunctions vtable;
    //int mode = 0;
    //const char  *filename = NULL;

    // export G_SLICE=always-malloc
    // export G_DEBUG=gc-friendly
    //g_mem_set_vtable(glib_mem_profiler_table);
    //g_mem_profile();
    //g_atexit(g_mem_profile);
    //return 1;

    gtk_init(&argc, &argv);
    //gtk_init(NULL, NULL);

    //g_thread_init(&vtable);
    g_thread_init(NULL);


    gtk_gl_init(&argc, &argv);
    //gdk_gl_init(&argc, &argv);
    //gtk_gl_init(NULL, NULL);


    gtk_set_locale();


    ///////////////////////////////////////
    // setup S52 display
    //
    S52_version();

#ifdef S52_USE_AIS
    //when running with GPSD use hard coded setup
    _setupS52();
#else
    {
        // when testing libS52 use setup from command-line option
        // easyer for testing from script
        _S52_init();
        char **argvret = _option(argc, argv);
        if (NULL == argvret[1]) {
            //S52_done();
            _setupS52();

            //return 0;
            ;
        }
        _dumpENV();
    }
#endif
    ///////////////////////////////////////


    _timer = g_timer_new();


    // Drawing Area
    _winArea = gtk_drawing_area_new();
    mode    = (GdkGLConfigMode) (GDK_GL_MODE_RGBA | GDK_GL_MODE_STENCIL | GDK_GL_MODE_DOUBLE);
    //mode = (GdkGLConfigMode) (GDK_GL_MODE_RGBA | GDK_GL_MODE_STENCIL);
    //mode    = (GdkGLConfigMode) (GDK_GL_MODE_RGB | GDK_GL_MODE_STENCIL | GDK_GL_MODE_DOUBLE);
    glconfig = gdk_gl_config_new_by_mode(mode);
    gtk_widget_set_gl_capability(_winArea, glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE);

    gtk_widget_set_events(_winArea, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    //gtk_widget_set_events(_winArea, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    GTK_WIDGET_UNSET_FLAGS(_winArea, GTK_DOUBLE_BUFFERED);



#if S52_USE_GLIB2
    g_signal_connect_after(G_OBJECT(_winArea), "realize",             G_CALLBACK(realize),         NULL);
    g_signal_connect(      G_OBJECT(_winArea), "configure_event",     G_CALLBACK(configure_event), NULL);
    g_signal_connect(      G_OBJECT(_winArea), "expose_event",        G_CALLBACK(expose_event),    NULL);
    g_signal_connect(      G_OBJECT(_winArea), "motion_notify_event", G_CALLBACK(motion_notify_event),  NULL);
    g_signal_connect(      G_OBJECT(_winArea), "button_release_event",G_CALLBACK(button_release_event), NULL);
#else
    gtk_signal_connect_after(GTK_OBJECT(_winArea), "realize",         GTK_SIGNAL_FUNC(realize),         NULL);
    gtk_signal_connect(      GTK_OBJECT(_winArea), "configure_event", GTK_SIGNAL_FUNC(configure_event), NULL);
    gtk_signal_connect(      GTK_OBJECT(_winArea), "expose_event",    GTK_SIGNAL_FUNC(expose_event),    NULL);
#endif

    /*
    g_signal_connect_after(G_OBJECT(winClutter), "realize",             G_CALLBACK(realize),         NULL);
    g_signal_connect(      G_OBJECT(winClutter), "configure_event",     G_CALLBACK(configure_event), NULL);
    g_signal_connect(      G_OBJECT(winClutter), "expose_event",        G_CALLBACK(expose_event),    NULL);
    //g_signal_connect(      G_OBJECT(winClutter), "motion_notify_event", G_CALLBACK(motion_notify_event),  NULL);
    //g_signal_connect(      G_OBJECT(winClutter), "button_release_event",G_CALLBACK(button_release_event), NULL);

    //g_signal_connect(      G_OBJECT(winClutter), "paint",               G_CALLBACK(button_release_event), NULL);
    //g_signal_connect ((ClutterActor*) viewport, "paint", (GCallback) _clutter_actor_paint, NULL);
    g_signal_connect ((ClutterActor*) rect, "paint", (GCallback) _clutter_actor_paint, NULL);
    //g_signal_connect ((ClutterActor*) stage, "paint", (GCallback) _clutter_actor_paint, NULL);

    //gtk_widget_set_events(winClutter, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    //GTK_WIDGET_UNSET_FLAGS(winClutter, GTK_DOUBLE_BUFFERED);
    */

    // Main Window
    _win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // normal
    //gtk_window_fullscreen(GTK_WINDOW(_win));

    printf("main .. \n");

    // debug
    gtk_window_set_default_size(GTK_WINDOW(_win), 800, 600 );
    // 1 screen
    //gtk_window_set_default_size(GTK_WINDOW(_win), 1280, 1024);
    // 2 screen
    //gtk_window_set_default_size(GTK_WINDOW(_win), 2560, 1024);

    //gtk_widget_set_events(win, GDK_POINTER_MOTION_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
    GTK_WIDGET_UNSET_FLAGS(_win, GTK_DOUBLE_BUFFERED);
                                    
    g_signal_connect_after(G_OBJECT(_win), "key_release_event",   G_CALLBACK(key_release_event),    NULL);
    g_signal_connect(      G_OBJECT(_win), "delete_event",        G_CALLBACK(gtk_main_quit),        NULL);
    //g_signal_connect(      G_OBJECT(_win), "button_release_event",G_CALLBACK(button_release_event), NULL);

    gtk_container_add(GTK_CONTAINER(_win), _winArea);

    gtk_widget_show_all(_win);

    GdkCursor* cursor = _cursor_new(GDK_CROSS);
    //GdkColor color;
    //gdk_color_parse ("red", &color);
    //gtk_widget_modify_fg(win, GTK_STATE_NORMAL, &color);

    gdk_window_set_cursor(_win->window, cursor);
    //gdk_cursor_destroy(cursor);   // deprecated

    // start main loop
    gtk_main();


    // S52 cleanup
    _ownshp      = S52_delMarObj(_ownshp);
    _vrmeblA     = S52_delMarObj(_vrmeblA);
    _vrmeblB     = S52_delMarObj(_vrmeblB);
    _vessel_arpa = S52_delMarObj(_vessel_arpa);
    _vessel_ais  = S52_delMarObj(_vessel_ais);
    _pastrk      = S52_delMarObj(_pastrk);
    _leglin1     = S52_delMarObj(_leglin1);
    _leglin2     = S52_delMarObj(_leglin2);
    _leglin3     = S52_delMarObj(_leglin3);
    _clrlin      = S52_delMarObj(_clrlin);

#ifdef S52_USE_AIS
    s52ais_doneAIS();
#endif

    S52_done();

    g_timer_destroy(_timer);

    //g_mem_profile();

    return 0;
}
