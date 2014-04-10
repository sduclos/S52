// S52glx.c: simple S52 driver using only GLX.
//
// Inspired from glxsimple.c
//
//

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



#include "S52.h"

#include <X11/Xlib.h>   // X*()
#include <GL/glx.h>     // glX*()

// FIXME: S52 use glib-2.0, but this g_print could link to glib-1.0
// from makefile instruction and then all hell break loose
//#ifdef S52_USE_GLIB2
//#include <glib/gprintf.h>   // g_print()
//#else
//#include <glib.h>           // g_print()
//#endif

#include <stdio.h>        // printf()
#include <stdlib.h>       // exit(0)


//static char *_fontName[] = {
//    "-*-helvetica-medium-r-normal-*-10-*-*-*-*-*-iso8859-*",
//    "-*-helvetica-medium-r-normal-*-12-*-*-*-*-*-iso8859-*",
//    "-*-helvetica-medium-r-normal-*-14-*-*-*-*-*-iso8859-*",
//};

static int _attr[] = {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE,       8,
    GLX_GREEN_SIZE,     8,
    GLX_BLUE_SIZE,      8,
    GLX_ALPHA_SIZE,     8,
    GLX_STENCIL_SIZE,   1,
    None
};

static int          _Xerror(Display *display, XErrorEvent* err)
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

static Display*     _getXdis()
{
    XSetErrorHandler(_Xerror);

    // open a connection to the X server
    Display *dpy = XOpenDisplay(NULL);
    if (NULL == dpy) {
        //g_print("could not open display");
        printf("could not open display");
        g_assert(0);
    }

    return dpy;
}

/*
static XVisualInfo* _getXinfo(Display *dpy, int *attr)
{
    // find an OpenGL-capable RGBA visual
    XVisualInfo *vis = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (vis == NULL) {
        //g_printf("ERROR: no RGBA visual");
        printf("ERROR: no RGBA visual");
        exit(0);
    }
    //if (vis->class != TrueColor)
    //    g_print("TrueColor visual required for this program");

    return vis;
}
*/


static Window       _setXwin(Display *dpy, XVisualInfo *visInfo)
{
    Colormap             cmap;
    XSetWindowAttributes swa;
    Window               win = RootWindow(dpy, visInfo->screen);


    // create an X colormap since probably not using default visual
    cmap = XCreateColormap(dpy, win, visInfo->visual, AllocNone);

    swa.colormap     = cmap;
    swa.border_pixel = 0;
    swa.event_mask   = ExposureMask | ButtonPressMask | StructureNotifyMask;

    // create an X window with the selected visual
    win = XCreateWindow(dpy, win, 
                        0, 0, 800, 600, 0, visInfo->depth,
                        InputOutput, visInfo->visual,
                        CWBorderPixel | CWColormap | CWEventMask, &swa);

    XSetStandardProperties(dpy, win, "s52glx", "s52glx", None, NULL, 0, NULL);


    // request the X window to be displayed on the screen
    XMapWindow(dpy, win);

    return win;
}

/*
static GLXContext   _getGLXctx(Display *dpy, XVisualInfo *visInfo)
{
    int dummy;

    if (!glXQueryExtension(dpy, &dummy, &dummy)) {
        //g_print("X server has no OpenGL GLX extension");
        printf("X server has no OpenGL GLX extension");
        exit(0);
    }

    GLXContext ctx = glXCreateContext(dpy, visInfo, None, GL_TRUE);  
    if (ctx == NULL) {
        printf("could not create GLX rendering context");
        exit(0);
    }

    return ctx;
}
*/

/*
static int          _setFont(Display *dpy)
{
    int  i;
    Font font;
    int  fontDL = 0;

    for (i=0; i<3; ++i) {
        font = XLoadFont(dpy, _fontName[i]);
        glXUseXFont(font, 0, 256, fontDL);
        S52_setFont(fontDL);
    }

    return 1;
}
*/

int main(int argc, char* argv[])
{
    //*
    Display     *dpy = _getXdis  ();
    /*
    XVisualInfo *vis = _getXvis  (dpy, _attr);
    Window       win = _setXwin  (dpy, vis);
    GLXContext   ctx = _getGLXctx(dpy, vis);

    // bind the rendering context to the window
    Bool ret = glXMakeCurrent(dpy, win, ctx);
    if (False == ret) {
        printf("ERROR: glXMakeCurrent() fail\n");
        exit(0);
    }
    */

#ifdef S52_USE_DOTPITCH
    {   // init S52 lib (Screen No 0)
        //int h   = XDisplayHeight  (dpy, 0);
        //int hmm = XDisplayHeightMM(dpy, 0);
        //int w   = XDisplayWidth   (dpy, 0);
        //int wmm = XDisplayWidthMM (dpy, 0);
        int w      = 1280;
        int h      = 1024;
        int wmm    = 376;
        //hmm    = 301; // wrong
        int hmm    = 307;

        S52_init(w, h, wmm, hmm, NULL);
    }
#else
    S52_init();
#endif

    S52_loadCell(NULL, NULL);

    // set font for S52
    //_setFont(dpy);

    // main loop
    XEvent event;
    //*
    while (1) {
        do {
            XNextEvent(dpy, &event);
            switch (event.type) {
                case ConfigureNotify:
                    glViewport(0, 0, event.xconfigure.width, event.xconfigure.height);
                //case ...

            }
        } while (XPending(dpy));

        S52_draw();

        //glXSwapBuffers(dpy,  win);
    }
    //*/

    S52_done();

    return 1;
}
