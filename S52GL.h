// S52GL.h: display S57 data using S52 symbology and OpenGL.
//
// Project:  OpENCview

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



#ifndef _S52GL_H_
#define _S52GL_H_

#include "S52PL.h"	// S52_obj

// Raster (RADAR, Bathy, ...)
typedef struct S52_GL_ras {
    GString *fnameMerc;       // Mercator GeoTiff file name

    // src
    int w;
    int h;
    int gdtSz;                // gdt: GDALDataType
    unsigned char *data;      // size =  w * h * nbyte_gdt
    double nodata;            // nodata value
    float min;                // exclusing nodata
    float max;                // exclusing nodata
    double gt[6];             // GeoTransform

    double S,W,N,E;           // projected extent

    // dst texture size
    guint npotX;
    guint npotY;

    int            isRADAR;   // TRUE if texAlpha is a RADAR image

    double         cLat;      // projected
    double         cLng;      // projected
    double         rNM;       // RADAR range

#ifdef S52_USE_RADAR
    S52_RADAR_cb   RADAR_cb;  //
#endif

    unsigned char *texAlpha;  // size = potX * potY
    unsigned int   texID;
} S52_GL_ras;

typedef enum S52_GL_cycle {
    S52_GL_NONE,              // state between 2 cycles
    S52_GL_DRAW,              // normal  cycle - first pass draw layer 0-8
    S52_GL_LAST,              // normal  cycle - last/top/repeatable draw of layer 9
    S52_GL_BLIT,              // bitblit cycle - blit FB of first pass
    S52_GL_PICK,              // pick    cycle - cursor pick
    S52_GL_INIT               // state before first S52_GL_DRAW
} S52_GL_cycle;

int   S52_GL_init(void);
int   S52_GL_setDotPitch(int w, int h, int wmm, int hmm);
int   S52_GL_setFontDL(int fontDL);

// -- framebuffer stuff --------------------------------
// init frame, save OpenGL state
// when mode is S52_GL_LAST and previous mode was S52_GL_DRAW
// pull the FB of Draw() from memory
int   S52_GL_begin(S52_GL_cycle cycle);
// render an object to framebuffer
int   S52_GL_draw(S52_obj *obj, gpointer user_data);
// draw lights
int   S52_GL_drawLIGHTS(S52_obj *obj);
// draw text
int   S52_GL_drawText(S52_obj *obj, gpointer user_data);
// draw RADAR,Bathy,...
int   S52_GL_drawRaster(S52_GL_ras *raster);

// copy from framebuffer to memory (or texture), return pixels
unsigned
char *S52_GL_readFBPixels(void);
// debug
int   S52_GL_dumpS57IDPixels(const char *toFilename, S52_obj *obj, unsigned int width, unsigned int height);

// copy from memory to frame buffer
int   S52_GL_drawFBPixels(void);
int   S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north);

// done frame, restore OpenGL state
int   S52_GL_end(S52_GL_cycle cycle);
// ----------------------------------

int   S52_GL_isSupp(S52_obj *obj);
//int   S52_GL_isOFFscreen(S52_obj *obj);
int   S52_GL_isOFFview(S52_obj *obj);

// delete GL data of object (DL of geo)
int   S52_GL_delDL(S52_obj *obj);
// delete raster
int   S52_GL_delRaster(S52_GL_ras *raster, int texOnly);

// flush GL objects, clean up mem
int   S52_GL_done(void);

int   S52_GL_setView(double centerLat, double centerLon, double rangeNM, double north);
int   S52_GL_setPRJView(double  s, double  w, double  n, double  e);
int   S52_GL_getPRJView(double *s, double *w, double *n, double *e);
int   S52_GL_setGEOView(double  s, double  w, double  n, double  e);
int   S52_GL_getGEOView(double *s, double *w, double *n, double *e);

int   S52_GL_win2prj(double *x, double *y);
int   S52_GL_prj2win(double *x, double *y);

int   S52_GL_setViewPort(int  x, int  y, int  width, int  height);
int   S52_GL_getViewPort(int *x, int *y, int *width, int *height);

// return the name of the top object
const
char *S52_GL_getNameObjPick(void);

int   S52_GL_drawStrWorld(double x, double y, char *str, unsigned int bsize, unsigned int weight);
int   S52_GL_drawStr(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str);
int   S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str);

int   S52_GL_drawGraticule(void);
int   S52_GL_movePoint(double *x, double *y, double angle, double dist_m);

int   S52_GL_isHazard(int nxyz, double *xyz);

#endif // _S52GL_H_
