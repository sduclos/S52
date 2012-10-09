// S52GL.h: display S57 data using S52 symbology and OpenGL.
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



#ifndef _S52GL_H_
#define _S52GL_H_

#include "S52PL.h"	// S52_obj

extern int   S52_GL_init(void);
extern int   S52_GL_setDotPitch(int w, int h, int wmm, int hmm);
extern int   S52_GL_setFontDL(int fontDL);

// -- framebuffer stuff --------------------------------
// init frame, save OpenGL state (see S52_GL_end())
extern int   S52_GL_begin(int cursorPick, int drawLast);
// render an object to frame
extern int   S52_GL_draw(S52_obj *obj, gpointer user_data);
// draw lights
extern int   S52_GL_drawLIGHTS(S52_obj *obj);
// draw text
extern int   S52_GL_drawText(S52_obj *obj, gpointer user_data);
// copy from frame buffer to memory, return pixels
extern unsigned char *S52_GL_readFBPixels(void);
// debug, caller must free mem
//extern unsigned char *S52_GL_readPixels(int x, int y, int width, int height);
// debug
extern int            S52_GL_dumpS57IDPixels(const char *toFilename, S52_obj *obj, unsigned int width, unsigned int height);

// copy from memory to frame buffer
extern int   S52_GL_drawFBPixels(void);
extern int   S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north);

extern int   S52_GL_resetVBOID(void);

// done frame, restore OpenGL state
// drawLast: FALSE then the next S52_drawLast() will pull the chart background from memory
extern int   S52_GL_end(int drawLast);
// ----------------------------------


extern int   S52_GL_isSupp(S52_obj *obj);
extern int   S52_GL_isOFFscreen(S52_obj *obj);


// delete GL data of object (DL of geo)
extern int   S52_GL_del(S52_obj *obj);

// next S52_GL_draw call will do cursor pick instead of rendering
//extern S57_geo* S52_GL_doPick(double x, double y);

// flush GL objects, clean up mem
extern int   S52_GL_done(void);

extern int   S52_GL_setView(double centerLat, double centerLon, double rangeNM, double north);
//extern int  S52_GL_getView(double *s, double *w, double *n, double *e);

extern int   S52_GL_getPRJView(double *s, double *w, double *n, double *e);
extern int   S52_GL_setPRJView(double  s, double  w, double  n, double  e);

//extern int   S52_GL_win2prj(double *x, double *y, double *z);
//extern int   S52_GL_prj2win(double *x, double *y, double *z);
extern int   S52_GL_win2prj(double *x, double *y);
extern int   S52_GL_prj2win(double *x, double *y);

extern int   S52_GL_setViewPort(int  x, int  y, int  width, int  height);
extern int   S52_GL_getViewPort(int *x, int *y, int *width, int *height);

// return the name of the top object
extern char *S52_GL_getNameObjPick(void);

//extern int   S52_GL_setOWNSHP(double breadth, double length);
//extern int   S52_GL_setOWNSHP(S52_obj *obj);

//extern int   S52_GL_tess(S57_geo *geoData);
extern int   S52_GL_drawStr(double x, double y, char *str, unsigned int bsize, unsigned int weight);
extern int   S52_GL_drawStrWin(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str);
extern int   S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str);

extern int   S52_GL_drawGraticule(void);
// draw an arc from A to B
//extern int   S52_GL_drawArc(S52_obj *objA, S52_obj *objB);
extern int   S52_GL_movePoint(double *x, double *y, double angle, double dist_m);



#endif
