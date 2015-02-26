// S52glx.c: simple S52 driver using only GLX.
//
// Inspired from glxsimple.c
//
//

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


#include "S52.h"

#include <X11/Xlib.h>          // X*()
#include <X11/XKBlib.h>        // Xkb*()
#include <X11/keysymdef.h>

#include <GL/glx.h>            // glX*()

#include <stdio.h>             // printf()
#include <stdlib.h>            // exit()

#define WIDTH  800
#define HEIGHT 600

static int _attr[] = {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE,       8,
    GLX_GREEN_SIZE,     8,
    GLX_BLUE_SIZE,      8,
    GLX_ALPHA_SIZE,     8,
    GLX_STENCIL_SIZE,   1,  // used for pattern
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

    return True;
}

static Display*     _getXdis()
// open a connection to the X server
{
    XSetErrorHandler(_Xerror);

    Display *dpy = XOpenDisplay(NULL);
    if (NULL == dpy) {
        printf("could not open display");
    }

    return dpy;
}

static XVisualInfo* _getXvis(Display *dpy, int *attr)
// find an OpenGL-capable RGBA visual
{
    XVisualInfo *vis = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (vis == NULL) {
        printf("ERROR: no RGBA visual");
        exit(0);
    }

    return vis;
}

static Window       _setXwin(Display *dpy, XVisualInfo *visInfo)
// create an X window with the selected visual
{
    Colormap             cmap;
    XSetWindowAttributes swa;
    Window               win = RootWindow(dpy, visInfo->screen);

    // create an X colormap since probably not using default visual
    cmap = XCreateColormap(dpy, win, visInfo->visual, AllocNone);

    swa.colormap     = cmap;
    swa.border_pixel = 0;
    //swa.event_mask   = ExposureMask | ButtonPressMask | StructureNotifyMask;
    //swa.event_mask   = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    swa.event_mask   = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask;

    win = XCreateWindow(dpy, win, 
                        0, 0, WIDTH, HEIGHT, 0, visInfo->depth,
                        InputOutput, visInfo->visual,
                        CWBorderPixel | CWColormap | CWEventMask, &swa);

    XSetStandardProperties(dpy, win, "s52glx", "s52glx", None, NULL, 0, NULL);


    // request the X window to be displayed on the screen
    XMapWindow(dpy, win);

    return win;
}

static GLXContext   _getGLXctx(Display *dpy, XVisualInfo *visInfo)
{
    int dummy;

    if (!glXQueryExtension(dpy, &dummy, &dummy)) {
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

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Display     *dpy = _getXdis  ();
    XVisualInfo *vis = _getXvis  (dpy, _attr);
    Window       win = _setXwin  (dpy, vis);
    GLXContext   ctx = _getGLXctx(dpy, vis);

    // bind the rendering context to the window
    Bool ret = glXMakeCurrent(dpy, win, ctx);
    if (False == ret) {
        printf("ERROR: glXMakeCurrent() fail\n");
        exit(0);
    }

    {   // init S52 lib (Screen No 0)
#ifdef SET_SCREEN_SIZE
        int w      = 1280;
        int h      = 1024;
        int wmm    = 376;
        int hmm    = 307;
#else
        int h   = XDisplayHeight  (dpy, 0);
        int hmm = XDisplayHeightMM(dpy, 0);
        int w   = XDisplayWidth   (dpy, 0);
        int wmm = XDisplayWidthMM (dpy, 0);
#endif
        S52_init(w, h, wmm, hmm, NULL);
    }

    S52_setViewPort(0, 0, WIDTH, HEIGHT);
    S52_loadCell(NULL, NULL);

    { // main loop
        XEvent event;
        while (True) {
            do {
                XNextEvent(dpy, &event);
                switch (event.type) {
                case ConfigureNotify: break;
                //case GraphicsExpose:
                //    S52_setViewPort(0, 0, event.xconfigure.width, event.xconfigure.height); break;
                //case ...

                case KeyPress:
                case KeyRelease:
                    {
                        unsigned int keycode = ((XKeyEvent *)&event)->keycode;
                        unsigned int keysym  = XkbKeycodeToKeysym(dpy, keycode, 0, 1);

                        // ESC - quit
                        if (XK_Escape == keysym) {
                            goto exit;
                        }
                    }
                }
            } while (XPending(dpy));
            S52_draw();
            S52_drawLast();  // nothing to do

            glXSwapBuffers(dpy,  win);
        }
    }

exit:

    XKillClient(dpy, win);

    S52_done();

    return True;
}
