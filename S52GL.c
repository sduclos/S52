// S52GL.c: display S57 data using S52 symbology and OpenGL.
//
// Project:  OpENCview

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


// - S52GL.c : S52 OpenGL rendering
//   - _GL1.i: GL1.x / GLSC1.0  - fixe-function pipeline
//   - _GL2.i: GL2.x / GLES2.x  - programmable/shader (GLSL)

#include "S52GL.h"

#include "S52MP.h"        // S52_MP_get/set()
#include "S52utils.h"     // PRINTF()

#include <glib.h>
#include <glib/gstdio.h>  // g_file_test(),

// compiled with -std=gnu99 (or -std=c99 -D_POSIX_C_SOURCE=???) will define M_PI
#include <math.h>         // sin(), cos(), atan2(), pow(), sqrt(), floor(), fabs(), INFINITY, M_PI

// FIXME: for C99
#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif


///////////////////////////////////////////////////////////////////
// state
static int          _doInit        = TRUE;    // initialize (but GL context --need main loop)
static int          _ctxValidated  = FALSE;   // validate GL context
static GPtrArray   *_objPick       = NULL;    // list of object picked
static GString     *_strPick       = NULL;    // hold temps val
static int          _doHighlight   = FALSE;   // TRUE then _objhighlight point to the object to hightlight
static S52_GL_cycle _crnt_GL_cycle = S52_GL_INIT; // state before first S52_GL_DRAW

//    S52_GL_NONE; // failsafe - keep cycle in sync between begin / end

// FIXME: rename to something like _doInitViewFirstTime
static int          _symbCreated   = FALSE;   // TRUE if PLib symb created (DList/VBO)

// now used by _renderLC() only
static GArray      *_tmpWorkBuffer = NULL;    // tmp buffer


////////////////////////////////////////////////////////////////////
// Projection View


// optimisation
//static int          _identity_MODELVIEW = FALSE;   // TRUE then identity matrix for modelview is on GPU (optimisation for AC())
//static int          _identity_MODELVIEW_cnt = 0;   // count saving

/*
static double       _north     = 0.0;  // from north to top / ship's head up (deg)
static double       _rangeNM   = 0.0;
static double       _centerLat = 0.0;
static double       _centerLon = 0.0;
*/

//* helper - save user center of view in degree
typedef struct _view_t{
    double cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} _view_t;
static _view_t _view = {0.0, 0.0, 0.0, 0.0};
//*/

static double       _SCAMIN    = 1.0;  // screen scale (SCAle MINimum in S57)
static double       _scalex    = 1.0;  // meter per pixel in X
static double       _scaley    = 1.0;  // meter per pixel in Y

// projected view
static projUV _pmin = { INFINITY,  INFINITY};
static projUV _pmax = {-INFINITY, -INFINITY};
// _pmin, _pmax convert to GEO for culling object with their extent (in deg)
static projUV _gmin = { INFINITY,  INFINITY};
static projUV _gmax = {-INFINITY, -INFINITY};

// current ViewPort
typedef struct vp_t {
    guint x;
    guint y;
    guint w;
    guint h;
} vp_t;
static vp_t _vp;

// GL_PROJECTION matrix
typedef enum _VP {
    VP_PRJ,         // projected coordinate
    VP_WIN,         // window coordinate
    VP_NUM          // number of coord. systems type
} VP;

//#define Z_CLIP_PLANE 10000   // clipped beyon this plane
//#define Z_CLIP_PLANE (S57_OVERLAP_GEO_Z + 1)
#define Z_CLIP_PLANE (S57_OVERLAP_GEO_Z - 1)

////////////////////////////////////////////////////////////////////

typedef struct  pt3  { double   x,y,z; } pt3;
typedef struct  pt3v { vertex_t x,y,z; } pt3v;

// NOTE: S52 pixels for symb are 0.3 mm
// this is the real (physical) dotpitch of the device as computed at init() time
// virtual dotpitch is set by user with S52_MP_set(S52_MAR_DOTPITCH_MM_X/Y, ..);
static double _dotpitch_mm_x = 0.3;  // will be overwright at init()
static double _dotpitch_mm_y = 0.3;  // will be overwright at init()
#define MM2INCH  25.4
#define PICA      0.351  // mm

#define S52_MAX_FONT  4


/////////////////////////////////////////////////////
//
// include the apropriate declaration/definition
//

// sanity checks
#if defined(S52_USE_GLES2) && !defined(S52_USE_GL2)
#define S52_USE_GL2  // this signal to load _GL2.i and
#endif               // switch to GL2 code path
#if defined(S52_USE_GLSC2) && !defined(S52_USE_GL2)
#define S52_USE_GL2  // this signal to load _GL2.i and
#endif               // switch to GL2 code path
#if defined(S52_USE_GL2) && !defined(S52_USE_OPENGL_VBO)
#define S52_USE_OPENGL_VBO  // GL2 need VBO
#endif

#if defined(S52_USE_FREETYPE_GL) && !(defined(S52_USE_GL2) || defined(S52_USE_GLES2))
#error "Need GL2 or GLES2 for Freetype GL"
#endif
#if defined(S52_USE_GL1) && defined(S52_USE_GL2)
#error "GL1 or GL2, not both"
#endif
#if !defined(S52_USE_GL1) && !defined(S52_USE_GL2)
#error "must define GL1 or GL2"
#endif
#if defined(S52_USE_GLSC2) && !defined(S52_USE_EGL)
#error "GLSC2 need EGL"
#endif

// GL1.x
#ifdef S52_USE_GL1
#include "_GL1.i"
#endif

// GL2.x, GLES2.x, GLSC2
#ifdef S52_USE_GL2
#include "_GL2.i"
#endif

// GL3.x, GLES3.x -- in a day (npot)
//#ifdef S52_USE_GL3
//#define S52_USE_GL2 // super set of GL2
//#include "_GL3.i"
//#include "_GL2.i"
//#endif


///////////////////////////////////////////////////////////////////
//
// statistique
//
static guint   _nobj   = 0;     // number of object drawn during lap
static guint   _ncmd   = 0;     // number of command drawn during lap
static guint   _oclip  = 0;     // number of object clipped
static guint   _nFrag  = 0;     // number of pixel fragment (color switch)
static int     _drgare = 0;     // DRGARE
static int     _depare = 0;     // DEPARE
static int     _nAC    = 0;     // total AC (Area Color)

// tesselated area stat
static guint   _ntris     = 0;     // area GL_TRIANGLES      count
static guint   _ntristrip = 0;     // area GL_TRIANGLE_STRIP count
static guint   _ntrisfan  = 0;     // area GL_TRIANGLE_FAN   count
static guint   _nCall     = 0;
static guint   _npoly     = 0;     // total polys

// debug
//static int   _debug  = 0;
//static int   _DEBUG  = FALSE;
//static guint _S57ID  = 0;

// hold copy of FrameBuffer
static guint          _fb_pixels_id   = 0;     // texture ID
static unsigned char *_fb_pixels      = NULL;
static guint          _fb_pixels_size = 0;
static int            _fb_pixels_udp  = TRUE;  // TRUE flag that the FB changed
#define _RGB           3
#define _RGBA          4
#ifdef S52_USE_ADRENO
static int            _fb_pixels_format      = _RGB;   // alpha blending done in shader
#else
static int            _fb_pixels_format      = _RGBA;
//static int            _fb_pixels_format      = _RGB ;  // NOTE: on TEGRA2 RGB (3) very slow
#endif


// GL utility
#include "_GLU.i"

#ifdef S52_USE_PROJ
#include <proj_api.h>   // projUV, projXY
#else
// same thing as in proj_api.h
typedef struct { double u, v; } projUV;
#define projXY projUV
#define RAD_TO_DEG    57.29577951308232
#define DEG_TO_RAD     0.0174532925199432958
#endif

#define ATAN2TODEG(xyz)   (90.0 - (atan2(xyz[4]-xyz[1], xyz[3]-xyz[0]) * RAD_TO_DEG))

//#define SHIPS_OUTLINE_MM    10.0   // 10 mm
#define SHIPS_OUTLINE_MM     6.0   // 6 mm

// symbol twice as big (see _pushScaletoPixel())
#define STRETCH_SYM_FAC 2.0

#define NM_METER 1852.0

#ifdef S52_USE_AFGLOW
// experimental: synthetic after glow
static GArray  *_aftglwColorArr    = NULL;
static GLuint   _vboIDaftglwVertID = 0;
static GLuint   _vboIDaftglwColrID = 0;
#endif

// experimental
static vertex_t _hazardZone[5*3];

static
inline void      _checkError(const char *msg)
{
#ifdef S52_DEBUG

/*
GL_NO_ERROR
                No error has been recorded. The value of this
                    symbolic constant is guaranteed to be 0.
GL_INVALID_ENUM
                An unacceptable value is specified for an
                    enumerated argument. The offending command is ignored,
                    and has no other side effect than to set the error
                    flag.
GL_INVALID_VALUE
                A numeric argument is out of range. The offending
                    command is ignored, and has no other side effect than to
                    set the error flag.
GL_INVALID_OPERATION
                The specified operation is not allowed in the
                    current state. The offending command is ignored, and has
                    no other side effect than to set the error flag.
GL_STACK_OVERFLOW
                This command would cause a stack overflow. The
                    offending command is ignored, and has no other side
                    effect than to set the error flag.
GL_STACK_UNDERFLOW
                This command would cause a stack underflow. The
                    offending command is ignored, and has no other side
                    effect than to set the error flag.
GL_OUT_OF_MEMORY
                There is not enough memory left to execute the
                    command. The state of the GL is undefined, except for the
                    state of the error flags, after this error is
                    recorded.
*/

    // Note: glGetError() stall the pipeline - how bad it is ?
    GLint err = GL_NO_ERROR; // == 0x0
    for (err = glGetError(); GL_NO_ERROR != err; err = glGetError()) {
        const char *name = NULL;
        switch (err) {
            case GL_INVALID_ENUM:      name = "GL_INVALID_ENUM";      break;
            case GL_INVALID_VALUE:     name = "GL_INVALID_VALUE";     break;
            case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:     name = "GL_OUT_OF_MEMORY";     break;

            //case GL_STACK_OVERFLOW:    name = "GL_STACK_OVERFLOW";    break;

            default:
                name = "Unknown text for this GL_ERROR";
        }

        PRINTF("from %s: 0x%x (%s)\n", msg, err, name);

#ifdef S52_USE_ANDROID
        //g_assert(0);
#endif

    }
#endif
}

static int       _findCentInside  (guint npt, pt3 *v)
// return TRUE and centroid else FALSE
{
    PRINTF("DEBUG: point is outside polygone, heuristique used to find a pt inside\n");

    _dcin  = -1.0;
    _inSeg = FALSE;

    g_array_set_size(_vertexs, 0);
    g_array_set_size(_nvertex, 0);
    _g_ptr_array_clear(_tmpV);

    //gluTessProperty(_tcen, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);

    gluTessBeginPolygon(_tcin, NULL);

    gluTessBeginContour(_tcin);
    for (guint i=0; i<npt; ++i, ++v)
        gluTessVertex(_tcin, (GLdouble*)v, (void*)v);
    gluTessEndContour(_tcin);

    gluTessEndPolygon(_tcin);

    if (_dcin != -1.0) {
        g_array_append_val(_centroids, _pcin);
        return TRUE;
    }

    return FALSE;
}

static int       _getCentroidOpen (guint npt, pt3 *v)
// Open Poly - return TRUE and centroid else FALSE
{
    GLdouble ai   = 0.0;
    GLdouble atmp = 0.0;
    GLdouble xtmp = 0.0;
    GLdouble ytmp = 0.0;

    // need 3 pts at least
    if (npt<3) {
        PRINTF("DEGENERATED npt = %i\n", npt);
        g_assert(0);
        return FALSE;
    }

    // offset --coordinate are too big
    //double x = v[0].x;
    //double y = v[0].y;

    // compute area
    for (guint i=0, j=npt-1; i<npt; j=i++) {
        pt3 p1 = v[i];
        pt3 p2 = v[j];

        // same vertex
        if (p1.x==p2.x && p1.y==p2.y)
            continue;

        //PRINTF("%i->%i: %f, %f -- %f, %f\n", i,j, p1.x, p1.y, p2.x, p2.y);
        ai    =  p1.x * p2.y - p2.x * p1.y;
        atmp +=  ai;
        xtmp += (p2.x + p1.x) * ai;
        ytmp += (p2.y + p1.y) * ai;
    }

    // compute centroid
    if (atmp != 0.0) {
        pt3 pt = {0.0, 0.0, 0.0};

        //area = atmp / 2.0;

        // CW = 1, CCW = 2
        //ret (atmp >= 0.0) ? 1 : 2;

        //pt.x = (xtmp / (3.0 * atmp)) + x;
        //pt.y = (ytmp / (3.0 * atmp)) + y;
        pt.x = (xtmp / (3.0 * atmp));
        pt.y = (ytmp / (3.0 * atmp));

        //PRINTF("XY(%s): %f, %f, %i \n", (atmp>=0.0) ? "CW " : "CCW", p.x, p.y, npt);

        //if (TRUE == S57_isPtInside(npt, (double*)v, pt.x, pt.y, FALSE)) {
        if (TRUE == S57_isPtInside(npt, (double*)v, FALSE, pt.x, pt.y)) {
            g_array_append_val(_centroids, pt);

            return TRUE;
        }

        // use heuristique to find centroid
        if (1.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS)) {
            _findCentInside(npt, v);

            return TRUE;
        }

        return TRUE;
    } else {
        // FIXME: bizzard case of poly with no area!
        PRINTF("WARNING: no area (0.0)\n");
    }

    return FALSE;
}

static int       _getCentroidClose(guint npt, double *ppt)
// Close Poly - return TRUE and centroid else FALSE
{
    //GLdouble ai;
    GLdouble atmp = 0.0;
    GLdouble xtmp = 0.0;
    GLdouble ytmp = 0.0;
    double  *p    = ppt;

    // need 3 pts at least,
    //S57: the last one is the first one (so min 4)
    if (npt<4) {
        PRINTF("WARNING: degenerated poly [npt:%i]\n", npt);
        return FALSE;
    }

    // projected coordinate are just too big
    // to compute a tiny area
    double offx = p[0];
    double offy = p[1];

    // debug
    if ((p[0] != p[(npt-1) * 3]) || (p[1] != p[((npt-1) * 3)+1])) {
        PRINTF("WARNING: poly end points doesn't match\n");
        g_assert(0);
        return FALSE;
    }

    // compute area
    for (guint i=0; i<npt-1; ++i) {
        GLdouble ai = (p[0]-offx) * (p[4]-offy) - (p[3]-offx) * (p[1]-offy);
        atmp += ai;
        xtmp += ((p[3]-offx) + (p[0]-offx)) * ai;
        ytmp += ((p[4]-offy) + (p[1]-offy)) * ai;

        p += 3;
    }

    // compute centroid
    if (atmp != 0.0) {
        pt3 pt = {0.0, 0.0, 0.0};
        pt.x = (xtmp / (3.0 * atmp)) + offx;
        pt.y = (ytmp / (3.0 * atmp)) + offy;

        //PRINTF("XY(%s): %f, %f, %i \n", (atmp>=0.0) ? "CCW " : "CW", pt->x, pt->y, npt);

        if (TRUE == S57_isPtInside(npt, ppt, TRUE, pt.x, pt.y)) {
            g_array_append_val(_centroids, pt);
            //PRINTF("point is inside polygone\n");

            return TRUE;
        }

        // use heuristique to find centroid
        if (1.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS)) {
            _findCentInside(npt, (pt3*)ppt);

            return TRUE;
        }

    } else {
        // FIXME: bizzard case of poly with no area!
        PRINTF("WARNING: no area (0.0)\n");
    }

    return FALSE;
}

static void      _glMatrixMode(GLenum  mode)
{
#ifdef S52_USE_GL2
    //_mode = mode;
    switch(mode) {
    	case GL_MODELVIEW:  _crntMat = _mvm[_mvmTop]; break;
    	case GL_PROJECTION: _crntMat = _pjm[_pjmTop]; break;
        default:
            PRINTF("ERROR: invalid mode (%i)\n", mode);
            g_assert(0);
    }
#else
    glMatrixMode(mode);
#endif

    return;
}

static void      _glPushMatrix(int mode)
{
#ifdef S52_USE_GL2
    GLfloat *prevMat = NULL;

    //switch(_mode) {
    switch(mode) {
    	case GL_MODELVIEW:  _mvmTop += 1; break;
    	case GL_PROJECTION: _pjmTop += 1; break;
        default:
            PRINTF("ERROR: invalid mode (%i)\n", mode);
            g_assert(0);
    }

    if (MATRIX_STACK_MAX<=_mvmTop || MATRIX_STACK_MAX<=_pjmTop) {
        PRINTF("ERROR: matrix stack overflow\n");
        g_assert(0);
    }

    prevMat  = (GL_MODELVIEW == mode) ? _mvm[_mvmTop-1] : _pjm[_pjmTop-1];
    _crntMat = (GL_MODELVIEW == mode) ? _mvm[_mvmTop  ] : _pjm[_pjmTop  ];

    // Note: no mem obverlap
    memcpy(_crntMat, prevMat, sizeof(GLfloat) * 16);

#else
    (void)mode;
    glPushMatrix();
#endif

    return;
}

static void      _glPopMatrix(int mode)
{
#ifdef S52_USE_GL2
    //switch(_mode) {
    switch(mode) {
    	case GL_MODELVIEW:  _mvmTop -= 1; break;
    	case GL_PROJECTION: _pjmTop -= 1; break;
        default:
            PRINTF("ERROR: invalid mode (%i)\n", mode);
            g_assert(0);
    }

    if (_mvmTop<0 || _pjmTop<0) {
        PRINTF("ERROR: matrix stack underflow\n");
        g_assert(0);
    }

    // update _crntMat
    _crntMat = (GL_MODELVIEW == mode) ? _mvm[_mvmTop] : _pjm[_pjmTop];

    // optimisation
    //if (GL_MODELVIEW == _mode)
    //if (GL_MODELVIEW == mode)
    //    _identity_MODELVIEW = FALSE;

#else
    (void)mode;
    glPopMatrix();
#endif

    return;
}

static void      _glLoadIdentity(int mode)
{
#ifdef S52_USE_GL2
    //* debug - alway in GL_MODELVIEW mode in a GLcycle
    if (GL_MODELVIEW == mode) {
        g_assert(*_crntMat == *_mvm[_mvmTop]);
        ;
    } else {
        g_assert(*_crntMat == *_pjm[_pjmTop]);
        ;
    }
    //*/

    memset(_crntMat, 0, sizeof(GLfloat) * 16);
    _crntMat[0] = _crntMat[5] = _crntMat[10] = _crntMat[15] = 1.0;

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //if (GL_MODELVIEW == mode)
    //    _identity_MODELVIEW = TRUE;

#else
    (void)mode;
    glLoadIdentity();
#endif

    return;
}

static void      _glOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
{
#ifdef S52_USE_GL2
    float dx = right - left;
    float dy = top   - bottom;
    float dz = zFar  - zNear;

    // avoid division by zero
    float tx = (dx != 0.0) ? -(right + left)   / dx : 0.0;
    float ty = (dy != 0.0) ? -(top   + bottom) / dy : 0.0;
    float tz = (dz != 0.0) ? -(zFar  + zNear)  / dz : 0.0;

    // GLSL: NOT row major
    //GLfloat m[16] = {
    //2.0f / dx,  0,          0,          tx,
    //0,          2.0f / dy,  0,          ty,
    //0,          0,          -2.0f / dz, tz,
    //0,          0,          0,          1
    //};

    // Matrices in GLSL are column major !!
    GLfloat m[16] = {
        2.0f / dx,  0.0f,       0.0f,          0.0f,
        0.0f,       2.0f / dy,  0.0f,          0.0f,
        0.0f,       0.0f,      -2.0f / dz,     0.0f,
        tx,         ty,         tz,            1.0f
    };

    _multiply(_crntMat, m);

#else
    glOrtho(left, right, bottom, top, zNear, zFar);
#endif

    return;
}

static void      _glUniformMatrix4fv_uModelview(void)
// optimisation - reset flag
{
    _glLoadIdentity(GL_MODELVIEW);

#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);


    /*
    if (FALSE == _identity_MODELVIEW) {
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity(GL_MODELVIEW);

        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

        _identity_MODELVIEW = TRUE;
    } else {
        _identity_MODELVIEW_cnt++;
    }
    */
#endif

    return;
}

static GLint     _pushScaletoPixel(int scaleSym)
{
    double scalex = _scalex;
    double scaley = _scaley;

    if (TRUE == scaleSym) {
        scalex /= (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
        scaley /= (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0);
    }

    _glMatrixMode(GL_MODELVIEW);
    _glPushMatrix(GL_MODELVIEW);
    _glScaled(scalex, scaley, 1.0);

    return TRUE;
}

static GLint     _popScaletoPixel(void)
{
    //_glMatrixMode(GL_MODELVIEW);
    _glPopMatrix (GL_MODELVIEW);

    // ModelView Matrix will be send to GPU before next glDraw()
    //glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    return TRUE;
}

//#if 0
static GLint     _glMatrixDump(int mode)
// debug
{
#ifdef S52_USE_GL2
    float *m = (GL_MODELVIEW == mode) ? _mvm[_mvmTop] : _pjm[_pjmTop];
#else
    double m[16];
    glGetDoublev(mode, m);
#endif

    PRINTF("%f\t %f\t %f\t %f \n",m[ 0], m[ 1], m[ 2], m[ 3]);
    PRINTF("%f\t %f\t %f\t %f \n",m[ 4], m[ 5], m[ 6], m[ 7]);
    PRINTF("%f\t %f\t %f\t %f \n",m[ 8], m[ 9], m[10], m[11]);
    PRINTF("%f\t %f\t %f\t %f \n",m[12], m[13], m[14], m[15]);
    PRINTF("-------------------\n");

    return TRUE;
}
//#endif  // 0

static GLint     _glMatrixSet(VP vpcoord)
// push & reset matrix GL_PROJECTION & GL_MODELVIEW
{
#ifdef S52_USE_GV
    return TRUE;
#endif

    GLdouble  left   = 0.0;
    GLdouble  right  = 0.0;
    GLdouble  bottom = 0.0;
    GLdouble  top    = 0.0;
    GLdouble  znear  = 0.0;
    GLdouble  zfar   = 0.0;

    // from OpenGL correcness tip --use 'int' for predictability
    // point(0.5, 0.5) fill the same pixel as recti(0,0,1,1). Note that
    // vertice need to be place +1/2 to match RasterPos()
    //GLint left=0,   right=0,   bottom=0,   top=0,   znear=0,   zfar=0;

    switch (vpcoord) {
        case VP_PRJ:
            left   = _pmin.u,      right = _pmax.u,
            bottom = _pmin.v,      top   = _pmax.v,
            znear  = Z_CLIP_PLANE, zfar  = -Z_CLIP_PLANE;
            //PRINTF("DEBUG: set VP_PRJ\n");
            break;

        case VP_WIN:
            left   = _vp.x,       right = _vp.x + _vp.w,
            bottom = _vp.y,       top   = _vp.y + _vp.h;
            znear  = Z_CLIP_PLANE, zfar  = -Z_CLIP_PLANE;
            //PRINTF("DEBUG: set VP_WIN\n");
            break;
        default:
            PRINTF("ERROR: unknown viewport coodinate\n");
            g_assert(0);
            return FALSE;
    }

    if (0.0==left && 0.0==right && 0.0==bottom && 0.0==top) {
        PRINTF("WARNING: Viewport not set (%f,%f,%f,%f)\n",
               left, right, bottom, top);
        g_assert(0);
        return FALSE;
    }

    _glMatrixMode  (GL_PROJECTION);
    _glPushMatrix  (GL_PROJECTION);
    _glLoadIdentity(GL_PROJECTION);

    _glOrtho(left, right, bottom, top, znear, zfar);

    //*
    _glTranslated(  (left+right)/2.0,  (bottom+top)/2.0, 0.0);
    //_glRotated   (_north, 0.0, 0.0, 1.0);
    _glRotated   (_view.north, 0.0, 0.0, 1.0);
    _glTranslated( -(left+right)/2.0, -(bottom+top)/2.0, 0.0);
    //PRINTF("DEBUG: north:%f\n", _north);
    //*/

    _glMatrixMode  (GL_MODELVIEW);
    _glPushMatrix  (GL_MODELVIEW);
    _glLoadIdentity(GL_MODELVIEW);

#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

#ifdef S52_USE_GL1
    // GL1 read in matrix from GPU
    glGetDoublev(GL_MODELVIEW_MATRIX,  _mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, _pjm);
#endif

    return TRUE;
}

static GLint     _glMatrixDel(VP vpcoord)
// pop matrix GL_PROJECTION & GL_MODELVIEW
{
    // vpcoord not used, just there so that it match _glMatrixSet()
    (void) vpcoord;

    _glMatrixMode(GL_PROJECTION);
    _glPopMatrix (GL_PROJECTION);

    _glMatrixMode(GL_MODELVIEW);
    _glPopMatrix (GL_MODELVIEW);

#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

    return TRUE;
}


//-----------------------------------
//
// PROJECTION SECTION
//
//-----------------------------------

static int       _win2prj(double *x, double *y)
// convert coordinate: window --> projected
{
    GLint vp[4] = {_vp.x, _vp.y, _vp.w, _vp.h};

    //_glLoadIdentity(GL_MODELVIEW);

#ifdef S52_USE_GL2
    float u       = *x;
    float v       = *y;
    float dummy_z = 0.0;

    //* debug
    if (0 == _pjm[_pjmTop][0]) {
        PRINTF("WARNING: broken Projection Matrix\n");
        g_assert(0);
        return FALSE;
    }
    //*/

    // Note: this is CPU job - no need to send the matrix to the GPU

    if (GL_FALSE == _gluUnProject(u, v, dummy_z, _mvm[_mvmTop], _pjm[_pjmTop], vp, &u, &v, &dummy_z)) {
        PRINTF("WARNING: _gluUnProject faild: _mvmTop=%i, _pjmTop=%i\n", _mvmTop, _pjmTop);
        g_assert(0);
        return FALSE;
    }

    *x = u;
    *y = v;

#else

    // FIXME: should be useless (how/why need that) - read in matrix from GPU
    glGetDoublev(GL_MODELVIEW_MATRIX,  _mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, _pjm);

    GLdouble dummy_z = 0.0;
    if (GL_FALSE == gluUnProject(*x, *y, dummy_z, _mvm, _pjm, vp, x, y, &dummy_z)) {
        PRINTF("WARNING: gluUnProject faild\n");
        g_assert(0);
        return FALSE;
    }
#endif

    return TRUE;
}

static projXY    _prj2win(projXY p)
// convert coordinate: projected --> window (pixel)
{
    GLint vp[4] = {_vp.x,_vp.y,_vp.w,_vp.h};

    // make sure that _gluProject() has the right coordinate
    // but if call from 52_GL_prj2win() then matrix allready set so this is redundant
    //_glLoadIdentity(GL_MODELVIEW);

#ifdef S52_USE_GL2
    // FIXME: find a better way to catch non initialyse matrix
    if (0 == _pjm[_pjmTop]) {
        g_assert(0);
        return p;
    }

    float u       = p.u;
    float v       = p.v;
    float dummy_z = 0.0;
    if (GL_FALSE == _gluProject(u, v, dummy_z, _mvm[_mvmTop], _pjm[_pjmTop], vp, &u, &v, &dummy_z)) {
        PRINTF("WARNING: _gluProject() failed x/y: %f %f\n", p.u, p.v);
        _glMatrixDump(GL_MODELVIEW);
        _glMatrixDump(GL_PROJECTION);
        g_assert(0);
        return p;
    }
    p.u = u;
    p.v = v;

#else

    // FIXME: should be useless (how/why need that) - read in matrix from GPU
    glGetDoublev(GL_MODELVIEW_MATRIX,  _mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, _pjm);

    GLdouble dummy_z = 0.0;
    if (GL_FALSE == gluProject(p.u, p.v, dummy_z, _mvm, _pjm, vp, &p.u, &p.v, &dummy_z)) {
        PRINTF("WARNING: gluProject() failed x/y: %f %f\n", p.u, p.v);
        _glMatrixDump(GL_MODELVIEW);
        _glMatrixDump(GL_PROJECTION);
        g_assert(0);
        return p;
    }
#endif

    return p;
}

int        S52_GL_win2prj(double *x, double *y)
// convert coordinate: window --> projected
{
    // if symb OK imply that projection is OK
    if (FALSE == _symbCreated) {
       PRINTF("DEBUG: no symbol imply no projection\n");
       g_assert(0);
       return FALSE;
    }

    _glMatrixSet(VP_PRJ);

    int ret = _win2prj(x, y);

    _glMatrixDel(VP_PRJ);

    return ret;
}


int        S52_GL_prj2win(double *x, double *y)
// convert coordinate: projected --> window
{
    // if symbole OK imply that projection is OK
    if (FALSE == _symbCreated) {
        PRINTF("DEBUG: no symbol imply no projection\n");
        g_assert(0);
        return FALSE;
    }

    _glMatrixSet(VP_PRJ);

    {
        projXY uv = {*x, *y};
        uv = _prj2win(uv);
        *x = uv.u;
        *y = uv.v;
    }

    _glMatrixDel(VP_PRJ);

    return TRUE;
}

static void      _glLineStipple(GLint  factor,  GLushort  pattern)
{
#ifdef S52_USE_GL2
    // silence gcc warning
    (void)factor;
    (void)pattern;

    /*
    static int silent = FALSE;
    if (FALSE == silent) {
        PRINTF("FIXME: GL2 line stipple\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }
    //*/


#else
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(factor, pattern);
#endif

    return;
}

static void      _glLineWidth(GLfloat width)
{
    // FIXME: debug dotpich correction
    glLineWidth(width);

    // debug GLES2 blending - NOP!
    //glLineWidth(width + 0.5);

    return;
}

static void      _glPointSize(GLfloat size)
{
    // FIXME: test dotpich correction
#ifdef S52_USE_GL2
    //glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    //then 'gl_PointSize' in the shader becomme active
    glUniform1f(_uPointSize, size);
#else
    glPointSize(size);
#endif

    return;
}

static GLvoid    _DrawArrays_LINE_STRIP(guint npt, vertex_t *ppt)
{
    /*
    // debug - test move S52 layer on Z
    double *p = ppt;
    for (guint i=0; i<npt; ++i) {
        GLdouble z;
        projUV p;
        p.u = *ppt++;
        p.v = *ppt++;
        z   = *ppt++;
        //p += 2;
        // *p++ = (double) (_dprio /10000.0);
    }
    */

    if (npt < 2) {
        PRINTF("FIXME: npt < 2 (%i)\n", npt);
        g_assert(0);
        return;
    }

#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
    glDrawArrays(GL_LINE_STRIP, 0, npt);
    glDisableVertexAttribArray(_aPosition);
#else
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
    glDrawArrays(GL_LINE_STRIP, 0, npt);
#endif

    _checkError("_DrawArrays_LINE_STRIP() .. end");

    return;
}

static GLvoid    _DrawArrays_LINES(guint npt, vertex_t *ppt)
// this is used when VRM line style is alternate line style
// ie _normallinestyle == 'N'
{
    // debug
    if (npt < 2) {
        //PRINTF("FIXME: found wierd LINES (%i)\n", npt);
        return;
    }

    if (0 != (npt % 2)) {
        PRINTF("FIXME: found LINES not modulo 2 (%i)\n", npt);
        g_assert(0);
        return;
    }

#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
    glDrawArrays(GL_LINES, 0, npt);
    glDisableVertexAttribArray(_aPosition);
#else
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
    glDrawArrays(GL_LINES, 0, npt);
#endif

    _checkError("_DrawArrays_LINES() .. end");

    return;
}

#ifdef S52_USE_OPENGL_VBO
static int       _VBODrawArrays_AREA(S57_prim *prim)
// only called by _fillArea() --> _VBODraw_AREA()
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;   // dummy
    guint     vertNbr = 0;      // dummy
    guint     vboID   = 0;      // dummy

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

    for (guint i=0; i<primNbr; ++i) {
        GLint mode  = 0;
        GLint first = 0;
        GLint count = 0;
        //guint mode  = 0;
        //guint first = 0;
        //guint count = 0;

        S57_getPrimIdx(prim, i, &mode, &first, &count);

        /*
        // FIXME: AP & AC filter one another
        if ((S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER)) {
        if ((S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER)) {
            switch (mode) {
            //case GL_TRIANGLE_STRIP: _ntristrip += count; break;
            //case GL_TRIANGLE_FAN:   _ntrisfan  += count; break;
            //case GL_TRIANGLES:      _ntris     += count; ++_nCall; break;
            case GL_TRIANGLE_STRIP: _ntristrip++; break;
            case GL_TRIANGLE_FAN:   _ntrisfan++;  break;
            case GL_TRIANGLES:      _ntris++;     break;
            default:
                PRINTF("unkown: [0x%x]\n", mode);
                g_assert(0);
            }
        } else {
            glDrawArrays(mode, first, count);
        }
        */

        if (_TRANSLATE == mode) {
            PRINTF("FIXME: _TRANSLATE\n");
            g_assert(0);
        } else {
            glDrawArrays(mode, first, count);
        }
    }


    _checkError("_VBODrawArrays_AREA()");

    return TRUE;
}

static int       _VBOCreate(S57_prim *prim)
// return new vboID else FALSE.
// Note that vboID set save by the caller
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

    //if (GL_FALSE == glIsBuffer(vboID)) {
    if (0 == vboID) {
        glGenBuffers(1, &vboID);

        // glIsBuffer fail!
        //if (GL_FALSE == glIsBuffer(vboID)) {
        if (0 == vboID) {
            PRINTF("ERROR: glGenBuffers() fail\n");
            g_assert(0);
            return FALSE;
        }

        // bind VBO in order to use
        glBindBuffer(GL_ARRAY_BUFFER, vboID);

        // upload VBO data to GPU
        glBufferData(GL_ARRAY_BUFFER, vertNbr*sizeof(vertex_t)*3, (const void *)vert, GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        _checkError("_VBOCreate()");

    } else {
        PRINTF("ERROR: VBO allready set!\n");
        g_assert(0);
        return FALSE;
    }

    return vboID;
}

static int       _VBODraw_AREA(S57_prim *prim)
// run a VBO - only called by _fillArea()
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

    if (0 == vboID) {
        vboID = _VBOCreate(prim);
        S57_setPrimDList(prim, vboID);
    }

    // bind VBOs for vertex array of vertex coordinates
    glBindBuffer(GL_ARRAY_BUFFER, vboID);

#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
    _VBODrawArrays_AREA(prim);
    glDisableVertexAttribArray(_aPosition);
#else
    // set VertPtr to VBO
    glVertexPointer(3, GL_DBL_FLT, 0, 0);
    _VBODrawArrays_AREA(prim);
#endif

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _checkError("_VBODraw_AREA() -fini-");

    return TRUE;
}
#endif  // S52_USE_OPENGL_VBO

static double    _getWorldGridRef(S52_obj *obj, double *LLx, double *LLy, double *URx, double *URy, double *tileW, double *tileH)
// called by _GL1.i and _GL2.i
{
    //
    // Tile pattern to 'grided' extent
    //

    // pattern tile: 1 = 0.01 mm
    double tw = 0.0;  // tile width
    double th = 0.0;  // tile height
    double dx = 0.0;  // run length offset for STG pattern
    S52_PL_getAPTileDim(obj, &tw,  &th,  &dx);

    // convert tile unit (0.01mm) to pixel
    double tileWidthPix  = tw / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    double tileHeightPix = th / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));
    double stagOffsetPix = dx / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));

    // convert tile in pixel to world
    double w0 = tileWidthPix  * _scalex;
    double h0 = tileHeightPix * _scaley;

    // grid alignment
    double x1, y1;   // LL of region of area
    double x2, y2;   // UR of region of area

    S57_geo *geoData = S52_PL_getGeo(obj);
    S57_getExt(geoData, &x1, &y1, &x2, &y2);
    double xyz[6] = {x1, y1, 0.0, x2, y2, 0.0};
    if (FALSE == S57_geo2prj3dv(2, (double*)&xyz))
        return FALSE;

    x1  = xyz[0];
    y1  = xyz[1];
    x2  = xyz[3];
    y2  = xyz[4];

    x1  = floor(x1 / w0) * w0;
    //y1  = floor(y1 / (2*h0)) * (2*h0);
    if (TRUE == stagOffsetPix)
        y1 = floor(y1 / (2*h0)) * (2*h0);
    else
        y1 = floor(y1 / h0) * h0;

    // optimisation, resize extent grid to fit window
    if (x1 < _pmin.u)
        x1 += floor((_pmin.u-x1)   / w0) * w0;
    if (y1 < _pmin.v)
        y1 += floor((_pmin.v-y1)   / (2*h0)) * (2*h0);
    if (x2 > _pmax.u)
        x2 -= floor((x2 - _pmax.u) / w0) * w0;
    //if (y2 > _pmax.v)
    //    y2 -= floor((y2 - _pmax.v) / h0) * h0;
    if (y2 > _pmax.v)
        y2 -= floor((y2 - _pmax.v) / (2*h0)) * (2*h0);

    // cover completely
    x2 += w0;
    y2 += h0;

    //PRINTF("PIXEL: tileW:%f tileH:%f\n", tileWidthPix, tileHeightPix);

    *LLx   = x1;
    *LLy   = y1;
    *URx   = x2;
    *URy   = y2;
    *tileW = tileWidthPix;
    *tileH = tileHeightPix;

    return stagOffsetPix;
}

static int       _fillArea(S57_geo *geoData)
{
    S57_prim *prim = S57_getPrimGeo(geoData);
    if (NULL == prim) {
        prim = _tessd(_tobj, geoData);
    }

#ifdef S52_USE_OPENGL_VBO
    if (FALSE == _VBODraw_AREA(prim)) {
        PRINTF("DEBUG: _VBODraw_AREA() failed [%s]\n", S57_getName(geoData));
    }
#else
    _callDList(prim);
#endif

    return TRUE;
}


//---------------------------------------
//
// SYMBOLOGY INSTRUCTION RENDERER SECTION
//
//---------------------------------------

// FIXME: ship head up for safety perimeter

// S52_GL_PICK mode
typedef struct col {
    GLubyte r;
    GLubyte g;
    GLubyte b;
    // FIXME: use _fb_format (rgb/rgba) and EGL rgb/rgba
    GLubyte a;
} col;

typedef union cIdx {
    col   color;
    guint idx;
} cIdx;
static cIdx _cIdx;
// FIXME: try 1 x 1
//static cIdx _pixelsRead[1 * 1];  // buffer to collect pixels when in S52_GL_PICK mode
static cIdx _pixelsRead[8 * 8];  // buffer to collect pixels when in S52_GL_PICK mode

#if 0
// MSAA experiment does the blending now
static int       _setBlend(int blend)
// TRUE turn on blending if AA
{
    if (TRUE == (int) S52_MP_get(S52_MAR_ANTIALIAS)) {
        if (TRUE == blend) {
            glEnable(GL_BLEND);

#ifdef S52_USE_GL1
            glEnable(GL_LINE_SMOOTH);
            glEnable(GL_ALPHA_TEST);
#endif
        } else {
            glDisable(GL_BLEND);

#ifdef S52_USE_GL1
            glDisable(GL_LINE_SMOOTH);
            glDisable(GL_ALPHA_TEST);
#endif
        }
    }

    _checkError("_setBlend()");

    return TRUE;
}
#endif  // 0

static int       _glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    // debug
    //printf("_glColor4ub: set current cIdx R to : %X\n", _cIdx.color.r);

#ifdef S52_USE_GL2
    GLfloat alpha = (4 - (a - '0')) * TRNSP_FAC_GLES2;
    glUniform4f(_uColor, r/255.0, g/255.0, b/255.0, alpha);
#else
    GLbyte alpha = (4 - (a - '0')) * TRNSP_FAC;
    glColor4ub(r, g, b, alpha);
#endif

    _checkError("_glColor4ub()");

    return TRUE;
}

static GLubyte   _setFragColor(S52_Color *c)
// set color/highlight, trans, pen_w
// return transparancy/alpha
{
    if (S52_GL_PICK == _crnt_GL_cycle) {
        // opaque
        _glColor4ub(_cIdx.color.r, _cIdx.color.g, _cIdx.color.b, '0');
        return (GLubyte) 1;
    }

    // normal
    _glColor4ub(c->R, c->G, c->B, c->trans);

    // reset color if highlighting (pick / alarm / indication)
    // FIXME: red / yellow (danger / warning)
    if (TRUE == _doHighlight) {
        S52_Color *dnghlcol = S52_PL_getColor("DNGHL");
        _glColor4ub(dnghlcol->R, dnghlcol->G, dnghlcol->B, c->trans);
    }

    //* trans
    if (('0'!=c->trans) && (TRUE==(int) S52_MP_get(S52_MAR_ANTIALIAS))) {
        // FIXME: blending always ON
        glEnable(GL_BLEND);

#ifdef S52_USE_GL1
        glEnable(GL_ALPHA_TEST);
#endif
    }
    //*/

    // pen_w of SY
    // - AC, AP, TXT, doesn't have a pen_w
    // - LS, LC have there own pen_w
    if (0 != c->pen_w) {
        _glLineWidth(c->pen_w - '0');

        // FIXME: used by _DrawArrays_POINTS
        // move to the call
        //_glPointSize(c->pen_w - '0');
    }

    return c->trans;
}

static int       _glCallList(S52_DList *DListData)
// get color of each Display List then run it
{
    if (NULL == DListData) {
        PRINTF("WARNING: NULL DListData!\n");
        return FALSE;
    }

    _checkError("_glCallList(): -start-");

#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
    glEnableVertexAttribArray(_aPosition);
#endif

    // no face culling as symbol can be both CW,CCW when winding is ODD (for ISODNG)
    // Note: cull face is faster but on GLES2 Radeon HD, Tegra2, Adreno its not
    glDisable(GL_CULL_FACE);

    GLuint     lst = DListData->vboIds[0];
    S52_Color *col = DListData->colors;

    for (guint i=0; i<DListData->nbr; ++i, ++lst, ++col) {

        //GLubyte trans =
        _setFragColor(col);

#ifdef S52_USE_OPENGL_VBO
        GLuint vboId = DListData->vboIds[i];
        glBindBuffer(GL_ARRAY_BUFFER, vboId);         // for vertex coordinates

        // reset offset in VBO
#ifdef S52_USE_GL2
        glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
#else
        glVertexPointer(3, GL_DBL_FLT, 0, 0);
#endif

        {
            guint j     = 0;
            GLint mode  = 0;
            GLint first = 0;
            GLint count = 0;

            // same color but change in MODE
            while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {

                // debug: how can this be !?
                if (NULL == DListData->prim[i]) {
                    g_assert(0);
                    continue;
                }

                if (_TRANSLATE == mode) {
                    GArray *vert = S57_getPrimVertex(DListData->prim[i]);

                    vertex_t *v = (vertex_t*)vert->data;
                    vertex_t  x = v[first*3+0];
                    vertex_t  y = v[first*3+1];
                    vertex_t  z = v[first*3+2];

                    _glTranslated(x, y, z);
#ifdef S52_USE_GL2
                    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

                } else {
                    // debug - test filter at GL level instead of CmdWord level
                    if (S52_CMD_WRD_FILTER_SY & (int) S52_MP_get(S52_CMD_WRD_FILTER)) {
                        ++_nFrag;
                    } else {
                        /*
                        if (mode = GL_LINES) {
                            glLineWidth(col->pen_w - '0' + 1.0);
                            glUniform4f(_uColor, col->R/255.0, col->G/255.0, col->B/255.0, 0.5);
                            glDrawArrays(mode, first, count);
                            glLineWidth(col->pen_w - '0');
                            glUniform4f(_uColor, col->R/255.0, col->G/255.0, col->B/255.0, (4 - (col->trans - '0')) * TRNSP_FAC_GLES2);
                        }
                        //*/

                        // normal draw
                        glDrawArrays(mode, first, count);

                        /*
                        { // debug
                            char str[80];
                            SNPRINTF(str, 80, "_glCallList():glDrawArrays() mode:%i first:%i count:%i", mode, first, count);
                            //_checkError("_glCallList(): -glDrawArrays()-");
                            _checkError(str);
                        }
                        */
                    }
                }
                ++j;

            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);

#else   // S52_USE_OPENGL_VBO

        glVertexPointer(3, GL_DBL_FLT, 0, 0);              // last param is offset, not ptr
        if (TRUE == glIsList(lst)) {
            //++_nFrag;
            glCallList(lst);              // NOT in OpenGL ES SC
            //glCallLists(1, GL_UNSIGNED_INT, &lst);
        } else {
            PRINTF("WARNING: glIsList() failed\n");
            g_assert(0);
            return FALSE;
        }
#endif  // S52_USE_OPENGL_VBO

#ifdef S52_USE_GL1
        if ('0' != col->trans) {
            glDisable(GL_ALPHA_TEST);
        }
#endif

    }

#ifdef S52_USE_GL2
    glDisableVertexAttribArray(_aPosition);
#endif

    glEnable(GL_CULL_FACE);

    _checkError("_glCallList(): -end-");

    return TRUE;
}

static int       _computeCentroid(S57_geo *geoData)
// return centroids
// fill global array _centroid
{
#ifdef S52_USE_GV
    // FIXME: there is a bug, tesselator fail
    return _centroids;
#endif

    guint   npt = 0;
    double *ppt = NULL;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    if (npt < 4)
        return FALSE;

    // debug
    //GString *FIDNstr = S57_getAttVal(geoData, "FIDN");
    //if (0==strcmp("2135158891", FIDNstr->str)) {
    //    PRINTF("%s\n",FIDNstr->str);
    //}
    //if (0 ==  g_strcmp0("ISTZNE", S57_getName(geoData))) {
    //    PRINTF("ISTZNEA found\n");
    //}
    //if (0 ==  g_strcmp0("PRDARE", S57_getName(geoData))) {
    //    PRINTF("PRDARE found\n");
    //}


    //PRINTF("%s EXT %f,%f -- %f,%f\n", S57_getName(geoData), _pmin.u, _pmin.v, _pmax.u, _pmax.v);

    double x1,y1, x2,y2;
    S57_getExt(geoData, &x1, &y1, &x2, &y2);
    double xyz[6] = {x1, y1, 0.0, x2, y2, 0.0};
    if (FALSE == S57_geo2prj3dv(2, (double*)&xyz))
        return FALSE;

    x1 = xyz[0];
    y1 = xyz[1];
    x2 = xyz[3];
    y2 = xyz[4];

    // extent inside view, compute normal centroid, no clip
    if ((_pmin.u < x1) && (_pmin.v < y1) && (_pmax.u > x2) && (_pmax.v > y2)) {
        g_array_set_size(_centroids, 0);

        _getCentroidClose(npt, ppt);
        //PRINTF("no clip: %s\n", S57_getName(geoData));

        return TRUE;
    }

    // CSG - Computational Solid Geometry  (clip poly)
    {
        _g_ptr_array_clear(_tmpV);

        g_array_set_size(_centroids, 0);
        g_array_set_size(_vertexs,   0);
        g_array_set_size(_nvertex,   0);

        //gluTessProperty(_tcen, GLU_TESS_BOUNDARY_ONLY, GLU_TRUE);

        // debug
        //PRINTF("npt: %i\n", npt);

        gluTessBeginPolygon(_tcen, NULL);

        // place the area --CW (BUG: should be CCW!)
        gluTessBeginContour(_tcen);
        for (guint i=0; i<npt-1; ++i) {
            gluTessVertex(_tcen, (GLdouble*)ppt, (void*)ppt);
            ppt += 3;
        }
        gluTessEndContour(_tcen);

        // place the screen
        gluTessBeginContour(_tcen);
        {
            GLdouble d[4*3] = {
                _pmin.u, _pmin.v, 0.0,
                _pmax.u, _pmin.v, 0.0,
                _pmax.u, _pmax.v, 0.0,
                _pmin.u, _pmax.v, 0.0,
            };
            GLdouble *p = NULL;

            // CCW
            /*
            p = d;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            */

            // CW
            p = d + (3*3);
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p -= 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p -= 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p -= 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);

        }
        gluTessEndContour(_tcen);

        // finish
        gluTessEndPolygon(_tcen);

        // compute centroid
        {   // debug: land here is extent of area overlap but not the area itself
            //if (0 == _nvertex->len)
            //    PRINTF("not intersecting with screen .. !!\n");

            int offset = 0;
            for (guint i=0; i<_nvertex->len; ++i) {
                int npt = g_array_index(_nvertex, int, i);
                pt3 *p = &g_array_index(_vertexs, pt3, offset);
                _getCentroidOpen(npt-offset, p);
                offset = npt;
            }
        }
    }

    return TRUE;
}

static int       _getVesselVector(S52_obj *obj, double *course, double *speed)
// return TRUE and course, speed, else FALSE
{
    S57_geo *geo    = S52_PL_getGeo(obj);
    double   vecstb = S52_MP_get(S52_MAR_VECSTB);

    // ground
    if (1.0 == vecstb) {
        GString *sogspdstr = S57_getAttVal(geo, "sogspd");
        GString *cogcrsstr = S57_getAttVal(geo, "cogcrs");

        *speed  = (NULL == sogspdstr)? 0.0 : S52_atof(sogspdstr->str);
        *course = (NULL == cogcrsstr)? 0.0 : S52_atof(cogcrsstr->str);

        // if no speed then draw no vector
        if (0.0 == *speed)
            return FALSE;

        return TRUE;
    }

    // water
    if (2.0 == vecstb) {
        GString *stwspdstr = S57_getAttVal(geo, "stwspd");
        GString *ctwcrsstr = S57_getAttVal(geo, "ctwcrs");

        *speed  = (NULL == stwspdstr)? 0.0 : S52_atof(stwspdstr->str);
        *course = (NULL == ctwcrsstr)? 0.0 : S52_atof(ctwcrsstr->str);

        // if no speed then draw no vector
        if (0.0 == *speed)
            return FALSE;

        return TRUE;
    }

    // none - 0
    return FALSE;
}

static int       _renderSY_POINT_T(S52_obj *obj, double x, double y, double rotation)
{
    S52_DList *DListData = S52_PL_getDListData(obj);

    _glLoadIdentity(GL_MODELVIEW);

    _glTranslated(x, y, 0.0);
    _glScaled(1.0, -1.0, 1.0);
    _pushScaletoPixel(TRUE);
    _glRotated(rotation, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

    _glCallList(DListData);

    _popScaletoPixel();

    return TRUE;
}

static int       _renderSY_silhoutte(S52_obj *obj)
// ownship & vessel (AIS)
{
    S57_geo  *geo    = S52_PL_getGeo(obj);
    GLdouble  orient = S52_PL_getSYorient(obj);

    // debug
    //return TRUE;

    guint     npt = 0;
    GLdouble *ppt = NULL;
    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    S52_DList *DListData = S52_PL_getDListData(obj);

    // compute ship symbol size on screen
    // get offset
    //GString *shp_off_xstr = S57_getAttVal(geo, "_shp_off_x");
    GString *shp_off_ystr = S57_getAttVal(geo, "_shp_off_y");
    //double   shp_off_x    = (NULL == shp_off_xstr) ? 0.0 : S52_atof(shp_off_xstr->str);
    double   shp_off_y    = (NULL == shp_off_ystr) ? 0.0 : S52_atof(shp_off_ystr->str);

    // 1 - compute symbol size in pixel
    int width;  // breadth (beam)
    int height; // length
    S52_PL_getSYbbox(obj, &width, &height);

    double symLenPixel = height / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));
    double symBrdPixel = width  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));

    // 2 - compute ship's length in pixel
    GString *shpbrdstr = S57_getAttVal(geo, "shpbrd");
    GString *shplenstr = S57_getAttVal(geo, "shplen");
    double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
    double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

    double shpBrdPixel = shpbrd / _scalex;
    double shpLenPixel = shplen / _scaley;

    // > 10 mm draw to scale
    if (((shpLenPixel*_dotpitch_mm_y) >= SHIPS_OUTLINE_MM) && (TRUE==(int) S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {

        // 3 - compute stretch of symbol (ratio)
        double lenRatio = shpLenPixel / symLenPixel;
        double brdRatio = shpBrdPixel / symBrdPixel;

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
        _glTranslated(0.0, -shp_off_y, 0.0);
        _pushScaletoPixel(TRUE);

        // apply stretch
        // Note: no -Y
        _glScaled(brdRatio, lenRatio, 1.0);

        _glCallList(DListData);

        _popScaletoPixel();

    }

    return TRUE;
}

static int       _renderSY_CSYMB(S52_obj *obj)
// FIXME: use dotpitch for XY placement
{
    S57_geo *geoData = S52_PL_getGeo(obj);
    char    *attname = "$SCODE";

    GString *attval  =  S57_getAttVal(geoData, attname);
    if (NULL == attval) {
        PRINTF("DEBUG: no attval\n");
        return FALSE;
    }

    S52_DList *DListData = S52_PL_getDListData(obj);

    _glLoadIdentity(GL_MODELVIEW);

    // scale bar
    if (0==g_strcmp0(attval->str, "SCALEB10") ||
        0==g_strcmp0(attval->str, "SCALEB11") ) {
        int width;
        int height;
        S52_PL_getSYbbox(obj, &width, &height);

        // 1 - compute symbol size in pixel
        double scaleSymWPixel = width  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
        double scaleSymHPixel = height / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));

        // 2 - compute symbol length in pixel
        // 3 - scale screen size
        double scale1NMWinWPixel =     4.0 / _scalex; // not important since pen width will override
        double scale1NMWinHPixel =  1852.0 / _scaley;

        // 4 - compute stretch of symbol (ratio)
        double HRatio = scale1NMWinHPixel / scaleSymHPixel;
        double WRatio = scale1NMWinWPixel / scaleSymWPixel;

        // set geo
        double x = 10.0; // 3 mm from left
        double y = 10.0; // bottom justifier

        _win2prj(&x, &y);

        //PRINTF("DEBUG: SCALEB XY `%f %f\n", x, y);

        _glTranslated(x, y, 0.0);
        _glScaled(1.0, -1.0, 1.0);                 // flip Y
        _glRotated(_view.north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        _pushScaletoPixel(TRUE);

        if (_SCAMIN < 80000.0) {
            // scale bar 1 NM
            if (0 == g_strcmp0(attval->str, "SCALEB10")) {
                // apply stretch
                _glScaled(WRatio, HRatio, 1.0);
                _glCallList(DListData);
            }
        } else {
            // scale bar 10 NM
            if (0 == g_strcmp0(attval->str, "SCALEB11")) {
                // apply stretch
                _glScaled(WRatio, HRatio*10.0, 1.0);
                _glCallList(DListData);
            }
        }

        _popScaletoPixel();

        return TRUE;
    }

    //
    // FIXME: looklike as if something bellow is affecting LC() winding - maybe matrix rot +- north
    //        cannot reproduce the defect of (DATCOVR/M_COVR:CATCOV=2) - HO data limit __/__/__ - LC(HODATA01)


    //*
    // north arrow
    if (0 == g_strcmp0(attval->str, "NORTHAR1")) {
        double x   = 30;
        double y   = _vp.h - 40;
        double rot = 0.0;

        _win2prj(&x, &y);

        _renderSY_POINT_T(obj, x, y, rot);

        return TRUE;
    }

    // depth unit
    if (0 == g_strcmp0(attval->str, "UNITMTR1")) {
        // Note: S52 specs say: left corner, just beside the scalebar [what does that mean in XY]
        double x = 30;
        double y = 20;

        _win2prj(&x, &y);

        _renderSY_POINT_T(obj, x, y, _view.north);

        return TRUE;
    }
    //*/

    //*
    if (TRUE == (int) S52_MP_get(S52_MAR_DISP_CALIB)) {
        // check symbol physical size, should be 5mm by 5mm
        if (0 == g_strcmp0(attval->str, "CHKSYM01")) {
            // FIXME: use _dotpitch_ ..
            double x      = _vp.x + 50;
            double y      = _vp.y + 50;
            // Note: no S52_MP_get(S52_MAR_DOTPITCH_MM_X) because we need exactly 5mm
            double scalex = _scalex / (_dotpitch_mm_x * 100.0);
            double scaley = _scaley / (_dotpitch_mm_y * 100.0);

            _win2prj(&x, &y);

            _glTranslated(x, y, 0.0);
            _glScaled(scalex,  scaley, 1.0);
            _glRotated(-_view.north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

            _glCallList(DListData);

            return TRUE;
        }

        // symbol to be used for checking and adjusting the brightness and contrast controls
        if (0 == g_strcmp0(attval->str, "BLKADJ01")) {
            // FIXME: use _dotpitch_ ..
            // top left (witch is under CPU usage on Android)
            double x   = _vp.w - 50;
            double y   = _vp.h - 50;

            _win2prj(&x, &y);

            _renderSY_POINT_T(obj, x, y,  _view.north);

            return TRUE;
        }
    }
    //*/

    //*
    {   // C1 ed3.1: AA5C1ABO.000
        // LOWACC01 QUESMRK1 CHINFO11 CHINFO10 REFPNT02 QUAPOS01
        // CURSRA01 CURSRB01 CHINFO09 CHINFO08 INFORM01

        guint     npt     = 0;
        GLdouble *ppt     = NULL;
        if (TRUE == S57_getGeoData(geoData, 0, &npt, &ppt)) {

            _renderSY_POINT_T(obj, ppt[0], ppt[1], _view.north);

            return TRUE;
        }
    }
    //*/

    // debug --should not reach this point
    PRINTF("CSYMB symbol not rendere: %s\n", attval->str);
    //g_assert(0);

    return FALSE;
}

static int       _renderSY_ownshp(S52_obj *obj)
{
    S57_geo  *geoData = S52_PL_getGeo(obj);
    GLdouble  orient  = S52_PL_getSYorient(obj);

    guint     npt     = 0;
    GLdouble *ppt     = NULL;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
        _renderSY_silhoutte(obj);
        return TRUE;
    }

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP01")) {
        //GString *shpbrdstr = S57_getAttVal(geoData, "shpbrd");
        GString *shplenstr = S57_getAttVal(geoData, "shplen");
        //double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        //double shpBrdPixel = shpbrd / scalex;
        //double shpLenPixel = shplen / scaley;
        double shpLenPixel = shplen / _scaley;

        // 10 mm drawn circle if silhoutte to small OR no silhouette at all
        if ( ((shpLenPixel*_dotpitch_mm_y) < SHIPS_OUTLINE_MM) || (FALSE==(int) S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {
            _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
        }

        return TRUE;
    }

    // draw vector stabilization
    // FIXME: use S52_MAR_VECSTB to draw VECGND01 -OR- VECWTR01 (not both)
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND01") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR01") ) {
        // 1 or 2
        if (0.0 != S52_MP_get(S52_MAR_VECSTB)) {
            // compute symbol offset due to course and seep
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {

                double courseRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM  = S52_MP_get(S52_MAR_VECPER) * (speed /60.0);
                double veclenM   = veclenNM * NM_METER;
                double veclenMX  = veclenM  * cos(courseRAD);
                double veclenMY  = veclenM  * sin(courseRAD);

                //_renderSY_POINT_T(obj, ppt[0]+veclenMX+_ownshp_off_x, ppt[1]+veclenMY+_ownshp_off_y, course);
                //_renderSY_POINT_T(obj, ppt[0]+veclenMX+_ownshp_off_x, ppt[1]+veclenMY, course);
                _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);
            }
        }
        return TRUE;
    }

    //
    // FIXME: OSPSIX02, OSPONE02, pivot seem to be on the base of triangle (it should be at the top!)
    //

    // time marks on vector - 6 min
    if (0 == S52_PL_cmpCmdParam(obj, "OSPSIX02")) {
        // 1 or 2
        if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {
            // compute symbol offset of each 6 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                int    nmrk         = (int) (S52_MP_get(S52_MAR_VECPER) / 6.0);

                // don't draw the last OSPSIX if overright S52_MAR_VECSTB
                if (0.0!=S52_MP_get(S52_MAR_VECSTB) && 0==(int)S52_MP_get(S52_MAR_VECPER)%6)
                    --nmrk;

                for (int i=0; i<nmrk; ++i) {
                    double ptx = ppt[0] + veclenM6minX*(i+1);
                    double pty = ppt[1] + veclenM6minY*(i+1);

                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty+_ownshp_off_y, course);
                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty, course);
                    _renderSY_POINT_T(obj, ptx, pty, course);
                }
            }
        }
        return TRUE;
    }

    // time marks on vector - 1 min
    if (0 == S52_PL_cmpCmdParam(obj, "OSPONE02")) {
        if (1.0 == S52_MP_get(S52_MAR_VECMRK)) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                int    nmrk         = (int)S52_MP_get(S52_MAR_VECPER);

                for (int i=0; i<nmrk; ++i) {
                    // skip 6 min mark
                    if (0 == (i+1) % 6)
                        continue;

                    double ptx = ppt[0] + veclenM1minX*(i+1);
                    double pty = ppt[1] + veclenM1minY*(i+1);

                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty+_ownshp_off_y, course);
                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty, course);
                    _renderSY_POINT_T(obj, ptx, pty, course);
                }
            }
        }
        return TRUE;
    }

    PRINTF("ERROR: unknown 'ownshp' symbol\n");
    g_assert(0);

    return FALSE;
}

static int       _renderSY_vessel(S52_obj *obj)
// AIS & ARPA
{
    S57_geo  *geo       = S52_PL_getGeo(obj);
    guint     npt       = 0;
    GLdouble *ppt       = NULL;
    GString  *vestatstr = S57_getAttVal(geo, "vestat");  // vessel state
    GString  *vecstbstr = S57_getAttVal(geo, "vecstb");  // vector stabilize
    GString  *headngstr = S57_getAttVal(geo, "headng");

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

#ifdef S52_USE_SYM_AISSEL01
    // AIS selected: experimental, put selected symbol on target
    if ((0 == S52_PL_cmpCmdParam(obj, "AISSEL01")) &&
        // FIXME: why test vestat here?
        (NULL!=vestatstr                           &&
        ('1'==*vestatstr->str || '2'==*vestatstr->str || '3'==*vestatstr->str))
       ) {
        GString *_vessel_selectstr = S57_getAttVal(geo, "_vessel_select");
        if ((NULL!=_vessel_selectstr) && ('Y'== *_vessel_selectstr->str)) {
            _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);
        }
    }
#endif

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
        _renderSY_silhoutte(obj);
        return TRUE;
    }

#ifdef S52_USE_SYM_VESSEL_DNGHL
    // experimental: VESSEL close quarters situation; target red, skip the reste
    if (NULL!=vestatstr && '3'==*vestatstr->str) {
        GString *vesrcestr = S57_getAttVal(geo, "vesrce");
        GString *headngstr = S57_getAttVal(geo, "headng");
        double   headng    = (NULL==headngstr) ? 0.0 : S52_atof(headngstr->str);

        if (NULL!=vesrcestr && '2'==*vesrcestr->str) {
            if (0 == S52_PL_cmpCmdParam(obj, "aisves01")) {
                _renderSY_POINT_T(obj, ppt[0], ppt[1], headng);
            }
        }
        if (NULL!=vesrcestr && '1'==*vesrcestr->str) {
            if (0 == S52_PL_cmpCmdParam(obj, "arpatg01")) {
                _renderSY_POINT_T(obj, ppt[0], ppt[1], headng);
            }
        }
        return TRUE;
    }
#endif

    // draw vector stabilization
    // FIXME: NO VECT STAB if target sleeping - (NULL!=vestatstr && '2'==*vestatstr->str)
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND21") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR21") ) {
        // 1 or 2
        if (NULL!=vecstbstr && ('1'==*vecstbstr->str||'2'==*vecstbstr->str)) {
            // compute symbol offset due to course and speed
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double courseRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM  = S52_MP_get(S52_MAR_VECPER) * (speed /60.0);
                double veclenM   = veclenNM * NM_METER;
                double veclenMX  = veclenM  * cos(courseRAD);
                double veclenMY  = veclenM  * sin(courseRAD);

                // FIXME: why make this check again!!
                if ((0==S52_PL_cmpCmdParam(obj, "VECGND21")) && ('1'==*vecstbstr->str) ) {
                    _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);
                } else {
                    if ((0==S52_PL_cmpCmdParam(obj, "VECWTR21")) && ('2'==*vecstbstr->str) ) {
                        _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);
                    }
                }
            }
        }

        return TRUE;
    }

    //
    // FIXME: AISSIX01, AISONE01, pivot seem to be on the base of triangle (it should be at the top!)
    //

    // time marks on vector - 6 min
    if ((0 == S52_PL_cmpCmdParam(obj, "ARPSIX01")) ||
        (0 == S52_PL_cmpCmdParam(obj, "AISSIX01")) ){
        // 1 or 2
        if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {
            // compute symbol offset of each 6 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                int    nmrk         = (int)(S52_MP_get(S52_MAR_VECPER) / 6.0);

                // don't draw the last AISSIX if overright S52_MAR_VECSTB
                if (0.0!=S52_MP_get(S52_MAR_VECSTB) && 0==(int)S52_MP_get(S52_MAR_VECPER)%6)
                    --nmrk;

                for (int i=0; i<nmrk; ++i) {
                    double ptx = ppt[0] + veclenM6minX*(i+1);
                    double pty = ppt[1] + veclenM6minY*(i+1);

                    _renderSY_POINT_T(obj, ptx, pty, course);
                }
            }
        }
        return TRUE;
    }

    // time marks on vector - 1 min
    if ((0 == S52_PL_cmpCmdParam(obj, "ARPONE01")) ||
        (0 == S52_PL_cmpCmdParam(obj, "AISONE01"))  ) {
        if (1.0 == S52_MP_get(S52_MAR_VECMRK)) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                int    nmrk         = (int)S52_MP_get(S52_MAR_VECPER);

                for (int i=0; i<nmrk; ++i) {
                    // skip 6 min mark
                    if (0 == (i+1) % 6)
                        continue;

                    double ptx = ppt[0] + veclenM1minX*(i+1);
                    double pty = ppt[1] + veclenM1minY*(i+1);

                    _renderSY_POINT_T(obj, ptx, pty, course);
                }
            }
        }
        return TRUE;
    }

    // symbol ARPA
    if (0 == S52_PL_cmpCmdParam(obj, "ARPATG01")) {
        _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);
        return TRUE;
    }

    // symbol AIS (normal)
    if (0 == S52_PL_cmpCmdParam(obj, "AISVES01")) {
        GString *headngstr = S57_getAttVal(geo, "headng");
        double   headng    = (NULL==headngstr) ? 0.0 : S52_atof(headngstr->str);
        GString *shplenstr = S57_getAttVal(geo, "shplen");
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        double shpLenPixel = shplen / _scaley;

        // drawn VESSEL symbol
        // 1 - if silhoutte too small
        // 2 - OR no silhouette at all
        if ( ((shpLenPixel*_dotpitch_mm_y) < SHIPS_OUTLINE_MM) || (FALSE==(int) S52_MP_get(S52_MAR_SHIPS_OUTLINE)) ) {
            // 3 - AND active (ie not sleeping)
            if (NULL!=vestatstr && '1'==*vestatstr->str)
                _renderSY_POINT_T(obj, ppt[0], ppt[1], headng);
        }

        return TRUE;
    }

    // AIS sleeping, no heading: this symbol put a '?' beside the target
    if ((0 == S52_PL_cmpCmdParam(obj, "AISDEF01")) && (NULL == headngstr) ) {
        // drawn upright
        _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);

        return TRUE;
    }

    // AIS sleeping
    if ((0 == S52_PL_cmpCmdParam(obj, "AISSLP01")) && (NULL!=vestatstr && '2'==*vestatstr->str) ) {
        GString *headngstr = S57_getAttVal(geo, "headng");
        double   headng    = (NULL==headngstr) ? 0.0 : S52_atof(headngstr->str);

        _renderSY_POINT_T(obj, ppt[0], ppt[1], headng);

        return TRUE;
    }

    return TRUE;
}

static int       _renderSY_pastrk(S52_obj *obj)
{
    S57_geo  *geo = S52_PL_getGeo(obj);
    guint     npt = 0;
    GLdouble *ppt = NULL;

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    if (npt < 2)
        return FALSE;

    for (guint i=0; i<npt; ++i) {
        double x1 = ppt[ i   *3 + 0];
        double y1 = ppt[ i   *3 + 1];
        //double x2 = ppt[(i+1)*3 + 0];
        //double y2 = ppt[(i+1)*3 + 1];
        double x2 = 0.0;
        double y2 = 0.0;

        if (i == npt-1) {
            x2 = ppt[(i-1)*3 + 0];
            y2 = ppt[(i-1)*3 + 1];
        } else {
            x2 = ppt[(i+1)*3 + 0];
            y2 = ppt[(i+1)*3 + 1];
        }

        double segang = 90.0 - atan2(y2-y1, x2-x1) * RAD_TO_DEG;

        _renderSY_POINT_T(obj, x1, y1, segang);

        //PRINTF("SEGANG: %f\n", segang);
    }

    return TRUE;
}

// forward decl
static int       _renderTXTAA(S52_obj *obj, S52_Color *color, double x, double y, unsigned int bsize, unsigned int weight, const char *str);
static int       _renderSY_leglin(S52_obj *obj)
{
    S57_geo  *geo = S52_PL_getGeo(obj);
    guint     npt = 0;
    GLdouble *ppt = NULL;

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
        PRINTF("WARNING: LEGLIN with no geo\n");
        return FALSE;
    }

    // debug
    //if (npt != 2) {
    //    PRINTF("WARNING: LEGLIN with %i point\n", npt);
    //    return FALSE;
    //}

    // planned speed (box symbol)
    if (0 == S52_PL_cmpCmdParam(obj, "PLNSPD03") ||
        0 == S52_PL_cmpCmdParam(obj, "PLNSPD04") ) {
        GString *plnspdstr = S57_getAttVal(geo, "plnspd");
        double   plnspd    = (NULL==plnspdstr) ? 0.0 : S52_atof(plnspdstr->str);

        if (0.0 != plnspd) {
            // FIXME: strech the box to fit text
            // FIXME: place the box "close to the leg" (see S52 p. I-112)
            _renderSY_POINT_T(obj, ppt[0], ppt[1], _view.north);


            /* planned speed text
            {
                double offset_x = 10.0 * _scalex;
                double offset_y = 18.0 * _scaley;

                // draw speed text inside box
                // FIXME: compute offset from symbol's bbox
                // S52_PL_getSYbbox(S52_obj *obj, int *width, int *height);
                // FIXME: ajuste XY for rotation
                char s[80];
                SNPRINTF(s, 80, "%3.1f kt", plnspd);

                // FIXME: get color from TE & TX command word
                // -OR- get color from leglin (orange or yellow)
                S52_Color *color = S52_PL_getColor("CHBLK");

                _renderTXTAA(obj, color, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, s);
                //_renderTXTAA(obj, NULL, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, s);
            }
            //*/

        }
        return TRUE;
    }

    // crossline for planned position --distance tags
    if (0 == S52_PL_cmpCmdParam(obj, "PLNPOS02")) {

        PRINTF("FIXME: compute crossline for planned position --distance tags\n");

        _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);

        return TRUE;
    }

    // debug --should not reach that
    PRINTF("ERROR: unknown 'leglin' symbol\n");
    g_assert(0);

    return TRUE;
}

static int       _renderSY(S52_obj *obj)
// SYmbol
{
    // FIXME: second draw of the same Mariners' Object misplace centroid!
    // and fail to be cursor picked

#ifdef S52_USE_GV
    PRINTF("FIXME: point symbol not drawn\n");
    return FALSE;
#endif

    // debug - filter is also in _glCallList():glDrawArray()
    //if (S52_CMD_WRD_FILTER_SY & (int) S52_MP_get(S52_CMD_WRD_FILTER))
    //    return TRUE;

    // failsafe
    S57_geo *geoData = S52_PL_getGeo(obj);
    GLdouble orient  = S52_PL_getSYorient(obj);

    guint     npt = 0;
    GLdouble *ppt = NULL;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
        return FALSE;
    }

    if (S57_POINT_T == S57_getObjtype(geoData)) {

        if (0 == g_strcmp0(S57_getName(geoData), "ownshp")) {
            _renderSY_ownshp(obj);
            return TRUE;
        }

        if (0 == g_strcmp0(S57_getName(geoData), "$CSYMB")) {
            _renderSY_CSYMB(obj);
            return TRUE;
        }

        if (0 == g_strcmp0(S57_getName(geoData), "vessel")) {
            _renderSY_vessel(obj);
            return TRUE;
        }

        // clutter - skip rendering LOWACC01
        if ((0==S52_PL_cmpCmdParam(obj, "LOWACC01")) && (0.0==S52_MP_get(S52_MAR_QUAPNT01)))
            return TRUE;

        // experimental --turn buoy light
        if (0.0 != S52_MP_get(S52_MAR_ROT_BUOY_LIGHT)) {
            if (0 == g_strcmp0(S57_getName(geoData), "LIGHTS")) {
                S57_geo *other = S57_getTouchLIGHTS(geoData);
                // this light 'touch' a buoy
                if ((NULL!=other) && (0==g_strcmp0(S57_getName(other), "BOYLAT"))) {
                    // assume that light have a single color in List
                    double deg = S52_MP_get(S52_MAR_ROT_BUOY_LIGHT);

                    S52_DList *DListData = S52_PL_getDListData(obj);
                    S52_Color *colors    = DListData->colors;
                    if (0 == g_strcmp0(colors->colName, "LITRD"))
                        orient = deg + 90.0;
                    if (0 == g_strcmp0(colors->colName, "LITGN"))
                        orient = deg - 90.0;

                }
            }
        }

        // debug
        //if (0 == g_strcmp0(S57_getName(geoData), "BOYLAT")) {
        //    PRINTF("DEBUG: BOYLAT found\n");
        //    //g_assert(0);
        //}
        //if (0 == g_strcmp0(S57_getName(geoData), "BOYCAR")) {  // cardinal buoy
        //    PRINTF("DEBUG: BOYCAR found\n");
        //    //g_assert(0);
        //}


        // all other point sym
        // FIXME: chart rotation - some should not rotate, like cardinal buoy BOYCAR - check chart
        //_renderSY_POINT_T(obj, ppt[0], ppt[1], orient+_north);  // CIP wrong
        _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);           // CIP ok - buoy rotate

        return TRUE;
    }

    //debug - skip LINES_T & AREAS_T
    //return TRUE;

    // an SY command on a line object (ex light on power line)
    if (S57_LINES_T == S57_getObjtype(geoData)) {

        // computer 'center' of line
        double cView_x = (_pmax.u + _pmin.u) / 2.0;
        double cView_y = (_pmax.v + _pmin.v) / 2.0;

        guint     npt = 0;
        GLdouble *ppt = NULL;
        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
            return FALSE;
        }

        if (0 == g_strcmp0("ebline", S57_getName(geoData))) {
            if (0 == S52_PL_cmpCmdParam(obj, "EBLVRM11")) {
                _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
            } else {
                double orient = ATAN2TODEG(ppt);
                _renderSY_POINT_T(obj, ppt[3], ppt[4], orient);
            }

            //_setBlend(FALSE);
            return TRUE;
        }

        if (0 == g_strcmp0("vrmark", S57_getName(geoData))) {
            if (0 == S52_PL_cmpCmdParam(obj, "EBLVRM11")) {
                _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
            }

            //_setBlend(FALSE);
            return TRUE;
        }

        if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
            _renderSY_pastrk(obj);
            //_setBlend(FALSE);
            return TRUE;
        }

        if (0 == g_strcmp0("leglin", S57_getName(geoData))) {
            _renderSY_leglin(obj);
            //_setBlend(FALSE);
            return TRUE;
        }

        if (0 == g_strcmp0("clrlin", S57_getName(geoData))) {
            double orient = ATAN2TODEG(ppt);
            _renderSY_POINT_T(obj, ppt[3], ppt[4], orient);
            //_setBlend(FALSE);
            return TRUE;
        }

        // find segment's center point closess to view center
        double xmin = 0.0;
        double ymin = 0.0;
        double dmin = INFINITY;
        for (guint i=0; i<npt; ++i) {
            double x = (ppt[i*3+3] + ppt[i*3]  ) / 2.0;
            double y = (ppt[i*3+4] + ppt[i*3+1]) / 2.0;
            double d = sqrt(pow(x-cView_x, 2) + pow(y-cView_y, 2));

            if (dmin > d) {
                dmin = d;
                xmin = x;
                ymin = y;
            }
        }

        if (INFINITY != dmin) {
            //_renderSY_POINT_T(obj, xmin, ymin, orient+_north);
            _renderSY_POINT_T(obj, xmin, ymin, orient);
        }

        //_setBlend(FALSE);

        return TRUE;
    }

    //debug - skip AREAS_T (cost 30msec; from ~110ms to ~80ms on Estuaire du St-L CA279037.000)
    //return TRUE;

    if (S57_AREAS_T == S57_getObjtype(geoData)) {
        guint     npt = 0;
        GLdouble *ppt = NULL;
        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
            return FALSE;
        }

        // clutter - skip rendering LOWACC01
        if ((0==S52_PL_cmpCmdParam(obj, "LOWACC01")) && (0.0==S52_MP_get(S52_MAR_QUAPNT01)))
            return TRUE;

        if (S52_GL_PICK == _crnt_GL_cycle) {
            // fill area, because other draw might not fill area
            // case of SY();LS(); (ie no AC() fill)
            {
                S52_DList *DListData = S52_PL_getDListData(obj);
                S52_Color *col = DListData->colors;
                _setFragColor(col);

                _glUniformMatrix4fv_uModelview();

                // when in pick mode, fill the area
                _fillArea(geoData);
            }

            // centroid offset might put the symb outside the area
            if (TRUE == S57_hasCentroid(geoData)) {
                double x,y;
                while (TRUE == S57_getNextCentroid(geoData, &x, &y)) {
                    //_renderSY_POINT_T(obj, x, y, orient+_north);
                    _renderSY_POINT_T(obj, x, y, orient);
                }
            }
            return TRUE;
        }


        {   // normal draw, also fill centroid
            double offset_x;
            double offset_y;

            // corner case where scrolling set area is clipped by view
            double x1,y1,x2,y2;
            S57_getExt(geoData, &x1, &y1, &x2, &y2);
            if ((y1 < _gmin.v) || (y2 > _gmax.v) || (x1 < _gmin.u) || (x2 > _gmax.u)) {
                // reset centroid
                S57_newCentroid(geoData);
            }

            // debug - skip centroid (cost 30% CPU and no glDraw(); from 120ms to 80ms on Estuaire du St-L CA279037.000)
            if (TRUE == S57_hasCentroid(geoData)) {
                double x,y;
                while (TRUE == S57_getNextCentroid(geoData, &x, &y)) {
                    //_renderSY_POINT_T(obj, x, y, orient+_north);
                    _renderSY_POINT_T(obj, x, y, orient);
                }
                return TRUE;
            } else {
                _computeCentroid(geoData);
            }

            // compute offset
            if (0 < _centroids->len) {
                S52_PL_getPivotOffset(obj, &offset_x, &offset_y);

                // mm --> pixel
                offset_x /=  S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0;
                offset_y /=  S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0;

                // pixel --> PRJ coord
                // scale offset
                offset_x *= _scalex;
                offset_y *= _scaley;

                S57_newCentroid(geoData);
            }

            for (guint i=0; i<_centroids->len; ++i) {
                pt3 *pt = &g_array_index(_centroids, pt3, i);

                // debug
                //PRINTF("drawing centered at: %f/%f\n", pt->x, pt->y);

                // save centroid
                S57_addCentroid(geoData, pt->x, pt->y);

                // check if offset move the object outside pick region
                // that symbole 'Y' axis is down, so '-offsety'
                //_renderSY_POINT_T(obj, pt->x, pt->y, orient+_north);
                //_renderSY_POINT_T(obj, pt->x + offset_x, pt->y - offset_y, orient+_north);
                _renderSY_POINT_T(obj, pt->x + offset_x, pt->y - offset_y, orient);

                // display only one centroid
                if (0.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS))
                    return TRUE;
            }
        }

        return TRUE;
    }

    // should not be reach
    PRINTF("DEBUG: don't know how to draw this point symbol\n");
    //g_assert(0);

    return FALSE;
}

static int       _renderLS_LIGHTS05(S52_obj *obj)
{
    S57_geo *geoData   = S52_PL_getGeo(obj);
    GString *orientstr = S57_getAttVal(geoData, "ORIENT");
    GString *sectr1str = S57_getAttVal(geoData, "SECTR1");
    GString *sectr2str = S57_getAttVal(geoData, "SECTR2");
    double   leglenpix = 25.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);

    GLdouble *ppt = NULL;
    guint     npt = 0;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    /* debug
    {
        double orientA = -1;  // sync with 'no orient' value in getSYorient()
        double orientB = S52_PL_getSYorient(obj);
        if (NULL != orientstr) {
            orientA = S52_atof(orientstr->str);
        }
        if (orientA != orientB) {
            PRINTF("orientA != orientB A:%f, B:%f\n", orientA, orientB);
            g_assert(0);
        }

    }
    //*/

    //PRINTF("S1: %f, S2:%f\n", sectr1, sectr2);

    // this is part of CS
    if (TRUE == (int) S52_MP_get(S52_MAR_FULL_SECTORS)) {
        GString  *valnmrstr = S57_getAttVal(geoData, "VALNMR");
        if (NULL != valnmrstr) {
            //PRINTF("FIXME: compute leglen to scale (NM)\n");
            double x1, y1, x2, y2;
            pt3 pt, ptlen;
            double valnmr = S52_atof(valnmrstr->str);

            S57_getExt(geoData, &x1, &y1, &x2, &y2);

            // light position
            pt.x = x1;  // not used
            pt.y = y1;
            pt.z = 0.0;
            if (FALSE == S57_geo2prj3dv(1, (double*)&pt))
                return FALSE;
            // position of end of sector nominal range
            ptlen.x = x1; // not used
            ptlen.y = y1 + (valnmr / 60.0);
            ptlen.z = 0.0;
            if (FALSE == S57_geo2prj3dv(1, (double*)&ptlen))
                return FALSE;

            _glLoadIdentity(GL_MODELVIEW);

            {
                projUV p = {ptlen.x, ptlen.y};
                p   = _prj2win(p);
                leglenpix = p.v;
                p.u = pt.x;
                p.v = pt.y;
                p   = _prj2win(p);
                leglenpix += p.v;
            }
        }
    }

    if (NULL != orientstr) {
        double orient = S52_atof(orientstr->str);

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90.0-orient, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif
        {   // from sea side
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        _popScaletoPixel();

    }

    if (NULL != sectr1str) {
        double sectr1 = S52_atof(sectr1str->str);

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90.0-sectr1, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        {   // from sea side
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        _popScaletoPixel();

    }

    if (NULL != sectr2str) {
        double sectr2 = S52_atof(sectr2str->str);

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90.0-sectr2, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        {    // from sea side
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        _popScaletoPixel();

    }

    return TRUE;
}

static int       _renderLS_ownshp(S52_obj *obj)
{
    S57_geo *geo       = S52_PL_getGeo(obj);
    GString *headngstr = S57_getAttVal(geo, "headng");
    double   vecper    = S52_MP_get(S52_MAR_VECPER);

    GLdouble *ppt     = NULL;
    guint     npt     = 0;

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    // FIXME: 2 type of line for 3 line symbol - but one overdraw the other
    // pen_w:
    //   2px - vector
    //   1px - heading, beam brg

    S52_Color *col;
    char       style;   // L/S/T
    char       pen_w;
    S52_PL_getLSdata(obj, &pen_w, &style, &col);


    // draw heading line
    if ((NULL!=headngstr) && (TRUE==(int) S52_MP_get(S52_MAR_HEADNG_LINE)) && ('1'==pen_w)) {
        double orient = S52_PL_getSYorient(obj);

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient-90.0, 0.0, 0.0, 1.0);

#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        // OWNSHP Heading
        // FIXME: draw to the edge of the screen
        // FIXME: coord. sys. must be in meter
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {_view.rNM * NM_METER * 2.0, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
    }

    // beam bearing line
    if ((0.0!=S52_MP_get(S52_MAR_BEAM_BRG_NM)) && ('1'==pen_w)) {
        double orient    = S52_PL_getSYorient(obj);
        double beambrgNM = S52_MP_get(S52_MAR_BEAM_BRG_NM);

        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);

#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        {   // port
            pt3v pt[2] = {{0.0, 0.0, 0.0}, { beambrgNM * NM_METER, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        {   // starboard
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-beambrgNM * NM_METER, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
    }

    // vector
    if ((0.0!=vecper) && ('2'==pen_w)) {
        double course, speed;
        if (TRUE == _getVesselVector(obj, &course, &speed)) {
            double orientRAD = (90.0 - course) * DEG_TO_RAD;
            double veclenNM  = vecper   * (speed / 60.0);
            double veclenM   = veclenNM * NM_METER;
            double veclenMX  = veclenM  * cos(orientRAD);
            double veclenMY  = veclenM  * sin(orientRAD);
            pt3v   pt[2]     = {{0.0, 0.0, 0.0}, {veclenMX, veclenMY, 0.0}};

            _glLoadIdentity(GL_MODELVIEW);

            _glTranslated(ppt[0], ppt[1], 0.0);

#ifdef S52_USE_GL2
            glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
    }

    return TRUE;
}

static int       _renderLS_vessel(S52_obj *obj)
// draw heading & vector line, colour is set by the previously drawn symbol
{
    S57_geo *geo       = S52_PL_getGeo(obj);
    GString *headngstr = S57_getAttVal(geo, "headng");
    double   vecper    = S52_MP_get(S52_MAR_VECPER);

    // debug
    //return TRUE;

    // FIXME: for every cmd LS() for this object draw a heading line
    // so this mean that drawing the vector will also redraw the heading line
    // and drawing the heading line will also redraw the vector
    // The fix could be to use the line witdh!
    // S52_PL_getLSdata(obj, &pen_w, &style, &col);
    // or
    // glGet with argument GL_LINE_WIDTH
    // or (better)
    // put a flags in each object --this flags must be resetted at every drawLast()
    // or
    // overdraw every LS (current solution)
    // or
    // create a heading symbol!

    // heading line, AIS only
    GString *vesrcestr = S57_getAttVal(geo, "vesrce");
    if (NULL!=vesrcestr && '2'==*vesrcestr->str) {
        if ((NULL!=headngstr) && (TRUE==(int) S52_MP_get(S52_MAR_HEADNG_LINE)))  {
            GLdouble *ppt = NULL;
            guint     npt = 0;
            if (TRUE == S57_getGeoData(geo, 0, &npt, &ppt)) {
                double headng = S52_atof(headngstr->str);
                // draw a line 50mm in length
                pt3v pt[2] = {{0.0, 0.0, 0.0}, {50.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X), 0.0, 0.0}};

                _glLoadIdentity(GL_MODELVIEW);

                _glTranslated(ppt[0], ppt[1], ppt[2]);
                _glRotated(90.0 - headng, 0.0, 0.0, 1.0);
                _glScaled(1.0, -1.0, 1.0);

                _pushScaletoPixel(FALSE);

#ifdef S52_USE_GL2
                glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);

                _popScaletoPixel();


            }
        }
    }
    // vector
    if (0 != vecper) {
        double course, speed;
        if (TRUE == _getVesselVector(obj, &course, &speed)) {
            double orientRAD = (90.0 - course) * DEG_TO_RAD;
            double veclenNM  = vecper   * (speed / 60.0);
            double veclenM   = veclenNM * NM_METER;
            double veclenMX  = veclenM  * cos(orientRAD);
            double veclenMY  = veclenM  * sin(orientRAD);

            GLdouble *ppt    = NULL;
            guint     npt    = 0;

            if (TRUE == S57_getGeoData(geo, 0, &npt, &ppt)) {
                pt3v pt[2] = {{ppt[0], ppt[1], 0.0}, {ppt[0]+veclenMX, ppt[1]+veclenMY, 0.0}};

#ifdef S52_USE_GL2
#ifdef S52_USE_SYM_VESSEL_DNGHL
                // 0 - undefined, 1 - AIS active, 2 - AIS sleeping, 3 - AIS active, close quarter (red)
                GString *vestatstr = S57_getAttVal(geo, "vestat");
                if (NULL!=vestatstr && '3'==*vestatstr->str) {
                    double ppt[6] = {ppt[0], ppt[1], 0.0, ppt[0]+veclenMX, ppt[1]+veclenMY, 0.0};
                    _glLineWidth(3);
                    _renderLS_gl2('D', 2, ppt);
                 }
#endif  // S52_USE_SYM_VESSEL_DNGHL

                _glUniformMatrix4fv_uModelview();
                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);

                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindTexture(GL_TEXTURE_2D,  0);

                glUniform1f(_uTextOn, 0.0);
                glDisableVertexAttribArray(_aUV);
                glDisableVertexAttribArray(_aPosition);
#else
                //pt3v pt[2] = {{ppt[0], ppt[1], 0.0}, {ppt[0]+veclenMX, ppt[1]+veclenMY, 0.0}};
                //_glUniformMatrix4fv_uModelview();
                _glLoadIdentity(GL_MODELVIEW);
                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
#endif
            }
        }
    }

    return TRUE;
}

#ifdef S52_USE_AFGLOW
static int       _renderLS_afterglow(S52_obj *obj)
{
    if (0.0 == S52_MP_get(S52_MAR_DISP_AFTERGLOW))
        return TRUE;

    S57_geo   *geo = S52_PL_getGeo(obj);
    guint      pti = S57_getGeoSize(geo);
    GLdouble  *ppt = NULL;
    guint      npt = 0;

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return TRUE;

    if (0 == npt) {
        PRINTF("DEBUG: afterglow npt = 0, .. exit\n");
        g_assert(0);
        return TRUE;
    }
    if (0 == pti) {
        //PRINTF("DEBUG: afterglow pti = 0, .. exit\n");
        //g_assert(0);
        return TRUE;
    }

    {   // set color
        S52_Color *col;
        char       style;   // dummy
        char       pen_w;   // dummy
        S52_PL_getLSdata(obj, &pen_w, &style, &col);
        _setFragColor(col);
    }

    //_setBlend(TRUE);

    //_checkError("_renderLS_afterglow() .. beg");

    _glPointSize(7.0);


#ifdef S52_USE_GL2
    //float   maxAlpha   = 1.0;   // 0.0 - 1.0
    //float   maxAlpha   = 0.5;   // 0.0 - 1.0
    float   maxAlpha   = 0.3;   // 0.0 - 1.0
#else
    float   maxAlpha   = 50.0;   // 0.0 - 255.0
#endif

    float crntAlpha = 0.0;
    float dalpha    = maxAlpha / pti;

    g_array_set_size(_aftglwColorArr, 0);

#ifdef S52_USE_GL2
    // set point alpha
    for (guint i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, crntAlpha);
        crntAlpha += dalpha;
    }
    // convert an array of geo double (3) to float (3)
    _d2f(_tessWorkBuf_f, pti, ppt);

    //_checkError("_renderLS_afterglow() .. -0-");
    // turn ON after glow in shader
    glUniform1f(_uGlowOn, 1.0);

    // vertex array - fill vbo arrays
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0,  _tessWorkBuf_f->data);
    //_checkError("_renderLS_afterglow() .. -0.1-");

    // fill array with alpha
    glEnableVertexAttribArray(_aAlpha);
    glVertexAttribPointer    (_aAlpha, 1, GL_FLOAT, GL_FALSE, 0, _aftglwColorArr->data);
    //_checkError("_renderLS_afterglow() .. -1-");

#else  // S52_USE_GL2

    // fill color (alpha) array
    for (guint i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, col->R);
        g_array_append_val(_aftglwColorArr, col->G);
        g_array_append_val(_aftglwColorArr, col->B);
        unsigned char tmp = (unsigned char)crntAlpha;
        g_array_append_val(_aftglwColorArr, tmp);
        crntAlpha += dalpha;
    }

    // vertex array - fill vbo arrays
    glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwVertID);

    //glEnableClientState(GL_VERTEX_ARRAY);       // no need to activate vertex coords array - alway on
    glVertexPointer(3, GL_DBL_FLT, 0, 0);          // last param is offset, not ptr
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, 0);
    glBufferData(GL_ARRAY_BUFFER, pti*sizeof(vertex_t)*3, (const void *)ppt, GL_DYNAMIC_DRAW);

    // colors array
    //glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwColrID);
    glBufferData(GL_ARRAY_BUFFER, pti*sizeof(unsigned char)*4, (const void *)_aftglwColorArr->data, GL_DYNAMIC_DRAW);

#endif  // S52_USE_GL2

    // 3 - draw
    _glUniformMatrix4fv_uModelview();
    glDrawArrays(GL_POINTS, 0, pti);
    //_checkError("_renderLS_afterglow() .. -2-");

#ifdef S52_USE_GL2
    // 4 - done
    // turn OFF after glow
    glUniform1f(_uGlowOn, 0.0);
    glDisableVertexAttribArray(_aPosition);
    glDisableVertexAttribArray(_aAlpha);

#else  // S52_USE_GL2

    // deactivate color array
    glDisableClientState(GL_COLOR_ARRAY);

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#endif  // S52_USE_GL2

    _checkError("_renderLS_afterglow() .. end");

    return TRUE;
}
#endif  // S52_USE_AFGLOW

static int       _renderLS(S52_obj *obj)
// Line Style
{
#ifdef S52_USE_GV
    return FALSE;
#endif

    if (S52_CMD_WRD_FILTER_LS & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S52_Color *col;
    char       style;   // L/S/T
    char       pen_w;
    S52_PL_getLSdata(obj, &pen_w, &style, &col);
    _setFragColor(col);

    _glLineWidth(pen_w - '0');
    //_glLineWidth(pen_w - '0' + 0.1);  // WARNING: THIS +0.1 SLOW DOWN EVERYTHING
    //_glLineWidth(pen_w - '0' + 0.5);
    //_glLineWidth(pen_w - '0' + 0.375);
    //_glLineWidth(pen_w - '0' - 0.5);

    // debug
    //glLineWidth(3.5);

    //_setBlend(TRUE);

    // Assuming pixel size at 0.3 mm.
    // Note: we can draw coded depth line because
    // pattern start is not reset a each new lines in a strip.
    //glEnable(GL_LINE_STIPPLE);
    //glEnable(GL_LINE_SMOOTH);  // antialiase needed for line stippled

    switch (style) {

        case 'L': // SOLD --correct
            // but this stippling break up antialiase
            //glLineStipple(1, 0xFFFF);
            break;

        case 'S': // DASH (dash 3.6mm, space 1.8mm) --incorrect  (last space 1.8mm instead of 1.2mm)
            //glEnable(GL_LINE_STIPPLE);
            _glLineStipple(3, 0x7777);
            //glLineStipple(2, 0x9248);  // !!
            break;

        case 'T': // DOTT (dott 0.6mm, space 1.2mm) --correct
            //glEnable(GL_LINE_STIPPLE);
            _glLineStipple(1, 0xFFF0);
            _glPointSize(pen_w - '0');
            break;

        default:
            PRINTF("ERROR: invalid line style\n");
            g_assert(0);
            return FALSE;
    }

    {
        S57_geo  *geoData = S52_PL_getGeo(obj);

        if (S57_POINT_T == S57_getObjtype(geoData)) {
            if (0 == g_strcmp0("LIGHTS", S57_getName(geoData)))
                _renderLS_LIGHTS05(obj);
            else {
                if (0 == g_strcmp0("ownshp", S57_getName(geoData)))
                    _renderLS_ownshp(obj);
                else {
                    if (0 == g_strcmp0("vessel", S57_getName(geoData))) {
#ifdef S52_USE_SYM_VESSEL_DNGHL
                        // AIS close quarters
                        GString *vestatstr = S57_getAttVal(geoData, "vestat");
                        if (NULL!=vestatstr && '3'==*vestatstr->str) {
                            if (0 == g_strcmp0("DNGHL", col->colName))
                                _renderLS_vessel(obj);
                            else
                                // discard all other line not of DNGHL colour
                                return TRUE;
                        }
                        else
#endif  // S52_USE_SYM_VESSEL_DNGHL
                        {
                            // normal line
                            _renderLS_vessel(obj);
                        }
                    }
                }
            }
        }
        else
        {
            // S57_LINES_T, S57_AREAS_T

            // FIXME: case of pick AREA where the only commandword is LS()
            // FIX: one more call to fillarea()
            GLdouble *ppt     = NULL;
            guint     npt     = 0;
            S57_getGeoData(geoData, 0, &npt, &ppt);

            // get the current number of positon (this grow as GPS/AIS pos come in)
            if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
                npt = S57_getGeoSize(geoData);
            }

            if (0 == g_strcmp0("ownshp", S57_getName(geoData))) {
                // what symbol for ownshp of type line or area ?
                // when ownshp is a POINT_T type !!!
                PRINTF("DEBUG: ownshp obj of type LINES_T, AREAS_T\n");
                _renderLS_ownshp(obj);
                g_assert(0);
            } else {
#ifdef S52_USE_AFGLOW
                // afterglow
                if ((0 == g_strcmp0("afgves", S57_getName(geoData))) ||
                    (0 == g_strcmp0("afgshp", S57_getName(geoData)))
                   ) {
                    //PRINTF("DEBUG: XXXXXXXXXXXXXXX afgves\n");
                    _renderLS_afterglow(obj);
                }
                else
#endif
                {

#ifdef S52_USE_GL2
                    _renderLS_gl2(style, npt, ppt);
#else
                    //_glUniformMatrix4fv_uModelview();
                    _glLoadIdentity(GL_MODELVIEW);

                    _DrawArrays_LINE_STRIP(npt, (vertex_t *)ppt);
#endif
                }
            }

            // add point on thick line to round corner
//#ifdef S52_USE_GL2
            // Not usefull with MSAA
            //_d2f(_tessWorkBuf_f, npt, ppt);
            //_DrawArrays_POINTS(npt, (vertex_t *)_tessWorkBuf_f->data);
//#else
            // add point on thick line to round corner
            // BUG: dot showup on transparent line
            // FIX: don't draw dot on tranparent line!
            //if ((3 <= (pen_w -'0')) && ('0'==col->trans))  {
            //if ((3 <= (pen_w -'0')) && ('0'==col->trans) && (TRUE==(int)S52_MP_get(S52_MAR_DISP_RND_LN_END))) {
            //if ((3 <= (pen_w -'0')) && ('0'==col->trans) && (1.0==S52_MP_get(S52_MAR_DISP_RND_LN_END))) {
            //    _glPointSize(pen_w  - '0');
            //
            //    _DrawArrays_POINTS(npt, (vertex_t *)ppt);
            //}
//#endif

        }
    }


#ifdef S52_USE_GL1
    if ('S'==style || 'T'==style)
        glDisable(GL_LINE_STIPPLE);
#endif

    //_setBlend(FALSE);

    _checkError("_renderLS()");

    return TRUE;
}

// http://en.wikipedia.org/wiki/Cohen-Sutherland
static const int _RIGHT  = 8;  // 1000
static const int _TOP    = 4;  // 0100
static const int _LEFT   = 2;  // 0010
static const int _BOTTOM = 1;  // 0001
static const int _CENTER = 0;  // 0000

static int       _computeOutCode(double x, double y)
{
    int code = _CENTER;

    if (y > _pmax.v) code |= _TOP;         // above the clip window
    else
    if (y < _pmin.v) code |= _BOTTOM;      // below the clip window

    if (x > _pmax.u) code |= _RIGHT;       // to the right of clip window
    else
    if (x < _pmin.u) code |= _LEFT;        // to the left of clip window

    return code;
}

static int       _clipToView(double *x1, double *y1, double *x2, double *y2)
// TRUE if some part is inside of the view and the clipped line
// FIXME: symbol aligned to border (window coordinate),
//        should be aligned to a 'grid' (geo coordinate)
{
    int accept   = FALSE;
    int done     = FALSE;
    int outcode1 = _computeOutCode(*x1, *y1);
    int outcode2 = _computeOutCode(*x2, *y2);

    do {
        // check if logical 'or' is 0. Trivially accept and get out of loop
        if (!(outcode1 | outcode2)) {
            accept = TRUE;
            done   = TRUE;
        } else {
            // check if logical 'and' is not 0. Trivially reject and get out of loop
            if (outcode1 & outcode2) {
                done = TRUE;
            } else {
                // failed both tests, so calculate the line segment to clip
                // from an outside point to an intersection with clip edge
                double x, y;
                // At least one endpoint is outside the clip rectangle; pick it.
                int outcode = outcode1 ? outcode1: outcode2;
                // Now find the intersection point;
                // use formulas y = y0 + slope * (x - x0), x = x0 + (1/slope)* (y - y0)
                // point is above the clip rectangle
                if (outcode & _TOP) {
                    x = *x1 + (*x2 - *x1) * (_pmax.v - *y1)/(*y2 - *y1);
                    y = _pmax.v;
                } else {
                    // point is below the clip rectangle
                    if (outcode & _BOTTOM) {
                        x = *x1 + (*x2 - *x1) * (_pmin.v - *y1)/(*y2 - *y1);
                        y = _pmin.v;
                    } else {
                        // point is to the right of clip rectangle
                        if (outcode & _RIGHT) {
                            y = *y1 + (*y2 - *y1) * (_pmax.u - *x1)/(*x2 - *x1);
                            x = _pmax.u;
                        } else {
                            // point is to the left of clip rectangle
                            if (outcode & _LEFT) {
                                y = *y1 + (*y2 - *y1) * (_pmin.u - *x1)/(*x2 - *x1);
                                x = _pmin.u;
                            }
                        }
                    }
                }

                // Now we move outside point to intersection point to clip
                // and get ready for next pass.
                if (outcode == outcode1) {
                    *x1 = x;
                    *y1 = y;
                    outcode1 = _computeOutCode(*x1, *y1);
                } else {
                    *x2 = x;
                    *y2 = y;
                    outcode2 = _computeOutCode(*x2, *y2);
                }
            }
        }
    } while (!done);

    return accept;
}

int        S52_GL_movePoint(double *x, double *y, double angle, double dist_m)
// find new point fron X,Y at distance and angle
{
    *x -= cos(angle) * dist_m;
    *y -= sin(angle) * dist_m;

    return TRUE;
}

static int       _renderLCring(S52_obj *obj, guint ringNo, double symlen_wrld)
{
    g_array_set_size(_tmpWorkBuffer, 0);

    GLdouble *ppt = NULL;
    guint     npt = 0;
    S57_geo  *geo = S52_PL_getGeo(obj);
    if (FALSE == S57_getGeoData(geo, ringNo, &npt, &ppt))
        return FALSE;

    S52_DList *DListData = S52_PL_getDListData(obj);

    double off_x = ppt[0];
    double off_y = ppt[1];
    GLdouble x1,y1;
    GLdouble x2,y2;
    for (guint i=1; i<npt; ++i) {
        GLdouble z1,z2;
        // set coordinate
        x1 = ppt[0];
        y1 = ppt[1];
        z1 = ppt[2];
        ppt += 3;
        x2 = ppt[0];
        y2 = ppt[1];
        z2 = ppt[2];

        //////////////////////////////////////////////////////
        //
        // overlapping Line Complex (LC) suppression
        //
        //if (z1<0.0 && z2<0.0) {
        if (-S57_OVERLAP_GEO_Z==z1 && -S57_OVERLAP_GEO_Z==z2) {
            //PRINTF("NOTE: this line segment (%s) overlap a line segment with higher prioritity (Z=%f)\n", S57_getName(geo), z1);
            continue;
        }
        /////////////////////////////////////////////////////


        //*
        // do not draw the rest of leglin if arc drawn
        if (0 == g_strcmp0("leglin", S57_getName(geo))) {
            if (2.0==S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) {
                // shorten x1,y1 of wholin_dist of previous leglin
                GLdouble segangRAD  = atan2(y2-y1, x2-x1);
                S52_obj *objPrevLeg = S52_PL_getPrevLeg(obj);
                S57_geo *geoPrev    = S52_PL_getGeo(objPrevLeg);
                GString *prev_wholin_diststr = S57_getAttVal(geoPrev, "_wholin_dist");
                if (NULL != prev_wholin_diststr) {
                    double prev_wholin_dist = S52_atof(prev_wholin_diststr->str) * 1852;
                    S52_GL_movePoint(&x1, &y1, segangRAD + (180.0 * DEG_TO_RAD), prev_wholin_dist);
                }

                // shorten x2,y2 if there is a next curve
                S52_obj *objNextLeg = S52_PL_getNextLeg(obj);
                if (NULL != objNextLeg) {
                    GString *wholin_diststr = S57_getAttVal(geo, "_wholin_dist");
                    if (NULL != wholin_diststr) {
                        double wholin_dist = S52_atof(wholin_diststr->str) * 1852;
                        S52_GL_movePoint(&x2, &y2, segangRAD, wholin_dist);
                    }

                }
            }
        }
        //*/

        if (FALSE == _clipToView(&x1, &y1, &x2, &y2))
            continue;

        GLdouble seglen_wrld   = sqrt(pow((x1-off_x)-(x2-off_x), 2)  + pow((y1-off_y)-(y2-off_y), 2));
        GLdouble segang        = atan2(y2-y1, x2-x1);
        GLdouble symlen_wrld_x = cos(segang) * symlen_wrld;
        GLdouble symlen_wrld_y = sin(segang) * symlen_wrld;
        int      nsym          = (int) (seglen_wrld / symlen_wrld);

        segang *= RAD_TO_DEG;

        //PRINTF("segang: %f seglen: %f symlen:%f\n", segang, seglen, symlen);
        //PRINTF(">> x1: %f y1: %f \n",x1, y1);
        //PRINTF(">> x2: %f y2: %f \n",x2, y2);

        GLdouble offset_wrld_x = 0.0;
        GLdouble offset_wrld_y = 0.0;

        // draw symb's as long as it fit the line length
        for (int j=0; j<nsym; ++j) {
            _glLoadIdentity(GL_MODELVIEW);

            _glTranslated(x1+offset_wrld_x, y1+offset_wrld_y, 0.0);           // move coord sys. at symb pos.
            _glRotated(segang, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
            _glScaled(1.0, -1.0, 1.0);

            _pushScaletoPixel(TRUE);

            _glCallList(DListData);

            _popScaletoPixel();

            offset_wrld_x += symlen_wrld_x;
            offset_wrld_y += symlen_wrld_y;
        }

        // FIXME: need this because some 'Display List' reset blending
        // FIXME: some Complex Line (LC) symbol allway use blending (ie transparancy)
        // but now with GLES2 MSAA its all or nothing
        //_setBlend(TRUE);
        //if (TRUE == (int) S52_MP_get(S52_MAR_ANTIALIAS)) {
        //    glEnable(GL_BLEND);
        //}

        {   // complete the rest of the line
            pt3v pt[2] = {{x1+offset_wrld_x, y1+offset_wrld_y, z1}, {x2, y2, z2}};
            g_array_append_val(_tmpWorkBuffer, pt[0]);  // x1 y1 z1
            g_array_append_val(_tmpWorkBuffer, pt[1]);  // x2 y2 z2
        }
    }

    // set identity matrix
    _glUniformMatrix4fv_uModelview();

    // render all lines ending
    _DrawArrays_LINES(_tmpWorkBuffer->len, (vertex_t*)_tmpWorkBuffer->data);

    _checkError("_renderLCring()");

    return TRUE;
}

static int       _drawArc(S52_obj *objA, S52_obj *objB);  // forward decl
static int       _renderLC(S52_obj *obj)
// Line Complex (AREA, LINE)
{
    /*
        // FIXME: check invariant
        {   // invariant: just to be sure that things don't explode
            // the number of tile in pixel is proportional to the number
            // of tile visible in world coordinate
            GLdouble tileNbrX = (_vp[2] - _vp[0]) / tileWidthPix;
            GLdouble tileNbrY = (_vp[3] - _vp[1]) / tileHeightPix;
            GLdouble tileNbrU = (x2-x1) / w;
            GLdouble tileNbrV = (y2-y1) / h;
            // debug
            //PRINTF("TX: %f TY: %f TU: %f TV: %f\n", tileNbrX,tileNbrY,tileNbrU,tileNbrV);
            //PRINTF("WORLD: widht: %f height: %f tileW: %f tileH: %f\n", (x2-x1), (y2-y1), w, h);
            //PRINTF("PIXEL: widht: %i height: %i tileW: %f tileH: %f\n", (_vp[2] - _vp[0]), (_vp[3] - _vp[1]), tileWidthPix, tileHeightPix);
            if (tileNbrX + 4 < tileNbrU)
                g_assert(0);
            if (tileNbrY + 4 < tileNbrV)
                g_assert(0);
        }
    */


#ifdef S52_USE_GV
    return FALSE;
#endif


    if (S52_CMD_WRD_FILTER_LC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S57_geo *geo = S52_PL_getGeo(obj);
    // draw arc if this is a leglin
    if (0 == g_strcmp0("leglin", S57_getName(geo))) {
        // check if user want to display arc
        if ((2.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) || (3.0==S52_MP_get(S52_MAR_DISP_WHOLIN))) {
            S52_obj *objNextLeg = S52_PL_getNextLeg(obj);

            if (NULL != objNextLeg)
                _drawArc(obj, objNextLeg);
        }

        //* draw guard zone if highligthed
        // FIXME: what about arc!
        if (TRUE == S57_isHighlighted(geo)) {
            _glLoadIdentity(GL_MODELVIEW);

#ifdef S52_USE_GL2
            glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif
            _DrawArrays_LINE_STRIP(5, _hazardZone);
        }
        //*/
    }

    //* debug
    //if (0 == g_strcmp0("M_COVR", S57_getName(geo))) {
    //    PRINTF("DEBUG: M_COVR found, nRing=%i\n", S57_getRingNbr(geo));
    //}
    //if (0 == g_strcmp0("M_NSYS", S57_getName(geo))) {
    //    PRINTF("DEBUG: M_NSYS found, nRing=%i\n", S57_getRingNbr(geo));
    //}
    //if (0 == g_strcmp0("sclbdy", S57_getName(geo))) {
    //    PRINTF("DEBUG: sclbdy found, nRing=%i\n", S57_getRingNbr(geo));
    //    //g_assert(0);
    //}
    //*/

    // set pen color & size here because values might not
    // be set via call list --short line
    S52_DList *DListData = S52_PL_getDListData(obj);
    S52_Color *c = DListData->colors;
    _setFragColor(c);

    //GLdouble symlen_pixl = 0.0;
    //GLdouble symlen_wrld = 0.0;

    GLdouble symlen = 0.0;
    char     pen_w  = 0;
    S52_PL_getLCdata(obj, &symlen, &pen_w);
    _glLineWidth(pen_w - '0');
    //_glLineWidth(pen_w - '0' + 0.375);

    GLdouble symlen_pixl = symlen / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    GLdouble symlen_wrld = symlen_pixl * _scalex;

    guint rNbr = S57_getRingNbr(geo);
    for (guint i=0; i<rNbr; ++i) {
        _renderLCring(obj, i, symlen_wrld);
    }

    //_setBlend(FALSE);

    // debug
    //glEnable(GL_BLEND);

    _checkError("_renderLC()");

    return TRUE;
}

static int       _renderAC_NODATA_layer0(void)
// clear all buffer so that no artefact from S52_drawLast remain
{
    if (S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    // clear screen with color NODATA
    S52_Color *c = S52_PL_getColor("NODTA");

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

#ifdef S52_USE_RADAR
    if (1.0 == S52_MP_get(S52_MAR_DISP_NODATA_LAYER)) {
        glClearColor(c->R/255.0, c->G/255.0, c->B/255.0, 1.0);
    } else {
        glClearColor(0.0, 0.0, 0.0, 1.0);  // black
    }
#else
    glClearColor(c->R/255.0, c->G/255.0, c->B/255.0, 1.0);
    //glClearColor(c->R/255.0, c->G/255.0, c->B/255.0, 0.0); // debug Nexus/Adreno draw() frame
    //glClearColor(c->R/255.0, 0.0, c->B/255.0, 0.0);
    //glClearColor(1.0, 0.0, 0.0, 1.0);
#endif

#ifdef S52_USE_TEGRA2
    // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase)
    // define in gl2ext.h
    //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
    glClear(GL_COVERAGE_BUFFER_BIT_NV | GL_COLOR_BUFFER_BIT);
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    _checkError("_renderAC_NODATA_layer0()");

    return TRUE;
}

static int       _renderAC_LIGHTS05(S52_obj *obj)
// this code is specific to CS LIGHTS05
{
    S57_geo   *geoData   = S52_PL_getGeo(obj);
    GString   *sectr1str = S57_getAttVal(geoData, "SECTR1");
    GString   *sectr2str = S57_getAttVal(geoData, "SECTR2");

    if ((NULL!=sectr1str) && (NULL!=sectr2str)) {
        S52_Color *c         = S52_PL_getACdata(obj);
        S52_Color *black     = S52_PL_getColor("CHBLK");
        GLdouble   sectr1    = S52_atof(sectr1str->str);
        GLdouble   sectr2    = S52_atof(sectr2str->str);
        double     sweep     = (sectr1 > sectr2) ? sectr2-sectr1+360 : sectr2-sectr1;
        GString   *extradstr = S57_getAttVal(geoData, "extend_arc_radius");
        GLdouble   radius    = 0.0;
        GLint      loops     = 1;

        GLdouble  *ppt       = NULL;
        guint      npt       = 0;
        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
            return FALSE;

        if (NULL!=extradstr && 'Y'==*extradstr->str) {
            radius = 25.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 25 mm on xoom)
        } else {
            radius = 20.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 20 mm on xoom)
        }

        // NOTE: specs say unit, assume it mean pixel
#ifdef S52_USE_OPENGL_VBO
        _gluQuadricDrawStyle(_qobj, GLU_FILL);
#else
        gluQuadricDrawStyle(_qobj, GLU_FILL);
#endif

        // first pass - create VBO
        S52_DList *DList = S52_PL_getDListData(obj);
        if (NULL == DList) {
            DList = S52_PL_newDListData(obj);
            DList->nbr = 2;
        }

        //if (FALSE == glIsBuffer(DList->vboIds[0])) {
        // GL1 win32
        if (0 == DList->vboIds[0]) {
            DList->prim[0] = S57_initPrim(NULL);
            DList->prim[1] = S57_initPrim(NULL);

            DList->colors[0] = *black;
            DList->colors[1] = *c;

#ifdef S52_USE_OPENGL_VBO
            _diskPrimTmp = DList->prim[0];
            _gluPartialDisk(_qobj, radius, radius+4, sweep/2.0, loops, sectr1+180, sweep);
            DList->vboIds[0] = _VBOCreate(_diskPrimTmp);

            _diskPrimTmp = DList->prim[1];
            _gluPartialDisk(_qobj, radius+1, radius+3, sweep/2.0, loops, sectr1+180, sweep);
            DList->vboIds[1] = _VBOCreate(_diskPrimTmp);
#else
            // black sector
            DList->vboIds[0] = glGenLists(1);
            glNewList(DList->vboIds[0], GL_COMPILE);

            _diskPrimTmp = DList->prim[0];
            gluPartialDisk(_qobj, radius, radius+4, sweep/2.0, loops, sectr1+180, sweep);
            _DrawArrays(_diskPrimTmp);
            glEndList();

            // color sector
            DList->vboIds[1] = glGenLists(1);
            glNewList(DList->vboIds[1], GL_COMPILE);

            _diskPrimTmp = DList->prim[1];
            gluPartialDisk(_qobj, radius+1, radius+3, sweep/2.0, loops, sectr1+180, sweep);
            _DrawArrays(_diskPrimTmp);
            glEndList();
#endif
            _diskPrimTmp = NULL;
        }

        //_setBlend(TRUE);


        _glLoadIdentity(GL_MODELVIEW);

        _glTranslated(ppt[0], ppt[1], 0.0);
        _pushScaletoPixel(FALSE);

        // use VBO
        _glCallList(DList);

        _popScaletoPixel();

        //_setBlend(FALSE);
    }

    _checkError("_renderAC_LIGHTS05()");

    return TRUE;
}

static int       _renderAC_VRMEBL01(S52_obj *obj)
// this code is specific to CS VRMEBL
// CS create a fake command word and in making so
// create room to add a cmdDef/DList variable use
// for drawing a VRM
{
    GLdouble *ppt = NULL;
    guint     npt = 0;
    S57_geo  *geo = S52_PL_getGeo(obj);

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    S52_DList *DListData = S52_PL_getDListData(obj);
    if (NULL == DListData) {
        DListData = S52_PL_newDListData(obj);
        DListData->nbr       = 1;
        DListData->prim[0]   = S57_initPrim(NULL);
        DListData->colors[0] = *S52_PL_getColor("CURSR");
    }

    S52_Color *c = DListData->colors;
    _setFragColor(c);

    GLdouble slice  = 360.0;
    GLdouble loops  = 1.0;
    GLdouble radius = sqrt(pow((ppt[0]-ppt[3]), 2) + pow((ppt[1]-ppt[4]), 2));

    _diskPrimTmp = DListData->prim[0];
    S57_initPrim(_diskPrimTmp); //reset

#ifdef S52_USE_OPENGL_VBO
    _gluQuadricDrawStyle(_qobj, GLU_LINE);
    _gluDisk(_qobj, radius, radius+4, slice, loops);
#else
    gluQuadricDrawStyle(_qobj, GLU_LINE);
    gluDisk(_qobj, radius, radius+4, slice, loops);
#endif

    // the circle has a tickness of 2 pixels
    _glLineWidth(2);

    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     DList   = 0;

    if (FALSE == S57_getPrimData(_diskPrimTmp, &primNbr, &vert, &vertNbr, &DList))
        return FALSE;

    //_setBlend(TRUE);

    _glLoadIdentity(GL_MODELVIEW);

    _glTranslated(ppt[0], ppt[1], 0.0);

#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glVertexPointer(3, GL_DBL_FLT, 0, 0);              // last param is offset, not ptr
#endif

    GString *normallinestylestr = S57_getAttVal(geo, "_normallinestyle");
    if (NULL!=normallinestylestr && 'Y'==*normallinestylestr->str)
        _DrawArrays_LINE_STRIP(vertNbr, vert);
    else
        _DrawArrays_LINES(vertNbr, vert);

    _diskPrimTmp = NULL;


    //_setBlend(FALSE);

    _checkError("_renderAC_VRMEBL01()");

    return TRUE;
}

static int       _renderAC(S52_obj *obj)
// Area Color (also filter light sector)
{
    // debug - this filter also in _VBODrawArrays_AREA():glDraw()
    if (S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S52_Color *c   = S52_PL_getACdata(obj);
    S57_geo   *geo = S52_PL_getGeo(obj);

    // LIGHTS05
    if (S57_POINT_T == S57_getObjtype(geo)) {
        _renderAC_LIGHTS05(obj);
        return TRUE;
    }

    // VRM
    if ((S57_LINES_T==S57_getObjtype(geo)) && (0==g_strcmp0("vrmark", S57_getName(geo)))) {
        _renderAC_VRMEBL01(obj);
        return TRUE;
    }

    //* experimental: skip NODTA color - passthrough layer 0
    // FIXME: check if this break S52_MAR_DISP_NODATA_LAYER OFF
    // FIXME: check if HO can  put something on group 1 bellow group 2 NODTA
    //        that break pasthrough layer 0
    if (0 == g_strcmp0(c->colName, "NODTA")) {
        /* Note: UNSARE (unsurveyed area) on group 2
        if (S52_PRIO_GROUP1 != S52_PL_getDPRI(obj)) {
            // FIXME: skip return if group 2
            PRINTF("DEBUG: nodata on group 2\n");
            g_assert(0);
        }
        //*/
        return TRUE;
    }
    //*/


    _setFragColor(c);

    _glUniformMatrix4fv_uModelview();

    _fillArea(geo);

    _checkError("_renderAC()");

    return TRUE;
}

static int       _renderAP_NODATA_layer0(void)
{
    // debug - this filter also in _VBODrawArrays_AREA():glDraw()
    if (S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    typedef struct {vertex_t x,y;} pt2v;
    pt2v pt0, pt1, pt2, pt3;

    // double --> float if GLES2
    // FIXME: resize extent for chart rotation
    pt0.x = _pmin.u;
    pt0.y = _pmin.v;

    pt1.x = _pmin.u;
    pt1.y = _pmax.v;

    pt2.x = _pmax.u;
    pt2.y = _pmax.v;

    pt3.x = _pmax.u;
    pt3.y = _pmin.v;

    S52_Color *chgrd = S52_PL_getColor("CHGRD");  // grey, conspic
    _setFragColor(chgrd);

#ifdef S52_USE_GL2
    // draw using texture as a stencil -------------------
    // NODATA texture size: 32px x 32px  --> world
    vertex_t tile_x   = 32 * _scalex;
    vertex_t tile_y   = 32 * _scaley;
    int      n_tile_x = (pt0.x - pt2.x) / tile_x;
    int      n_tile_y = (pt0.y - pt2.y) / tile_y;

    // georeference to grid
    pt0.x = floorf(pt0.x / tile_x) * tile_x;
    pt0.y = floorf(pt0.y / tile_y) * tile_y;

    vertex_t ppt[4*3 + 4*2] = {
        pt0.x, pt0.y, 0.0,        0.0f,     0.0f,
        pt1.x, pt1.y, 0.0,        0.0f,     n_tile_y,
        pt2.x, pt2.y, 0.0,        n_tile_x, n_tile_y,
        pt3.x, pt3.y, 0.0,        n_tile_x, 0.0f
    };

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer(_aUV, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    // turn ON 'sampler2d'
    //glUniform1f(_uTextOn,       1.0);

    //*
    glUniform1f(_uPattOn,       1.0);
    glUniform1f(_uPattGridX,  pt0.x);
    glUniform1f(_uPattGridY,  pt0.y);
    glUniform1f(_uPattW,     tile_x);
    glUniform1f(_uPattH,     tile_y);
    //*/

    glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

    glFrontFace(GL_CW);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glFrontFace(GL_CCW);

    _checkError("_renderAP_NODATA_layer0 -4-");

    glBindTexture(GL_TEXTURE_2D,  0);

    //*
    glUniform1f(_uPattOn,    0.0);
    glUniform1f(_uPattGridX, 0.0);
    glUniform1f(_uPattGridY, 0.0);
    glUniform1f(_uPattW,     0.0);
    glUniform1f(_uPattH,     0.0);
    //*/

    // turn OFF 'sampler2d'
    //glUniform1f(_uTextOn,       0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

#else  // S52_USE_GL2


    // FIXME: stippling fail on Mesa3d as of 10.1.3 (bug 25280)

    glEnable(GL_POLYGON_STIPPLE);

    glPolygonStipple(_nodata_mask);

    {
        vertex_t ppt[4*3] = {
            pt0.x, pt0.y, 0.0,
            pt1.x, pt1.y, 0.0,
            pt2.x, pt2.y, 0.0,
            pt3.x, pt3.y, 0.0
        };

        glVertexPointer(3, GL_DBL_FLT, 0, ppt);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glDisable(GL_POLYGON_STIPPLE);

#endif  // S52_USE_GL2

    _checkError("_renderAP_NODATA_layer0 -end-");

    return FALSE;
}

static int       _renderAP(S52_obj *obj)
// Area Pattern
// NOTE: S52 define pattern rotation but doesn't use it in PLib, so not implemented.
{
    // debug - this filter also in _VBODrawArrays_AREA():glDraw()
    if (S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj))) {
        if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
            return TRUE;
    }


#ifdef S52_USE_GV
    return FALSE;
#endif


    //--------------------------------------------------------
    // don't pick pattern for now
    if (S52_GL_PICK == _crnt_GL_cycle) {
        return TRUE;
    }

    /*
    // when in pick mode, fill the area
    if (S52_GL_PICK == _crnt_GL_cycle) {
        S57_geo *geoData = S52_PL_getGeo(obj);
        S52_Color dummy;

        _setFragColor(&dummy);
        _fillArea(geoData);

        return TRUE;
    }
    //*/
    //--------------------------------------------------------
    //* experimental: skip NODATA03 pattern - passthrough layer 0
    // FIXME: check if this break S52_MAR_DISP_NODATA_LAYER OFF
    // FIXME: check if HO can  put something on group 1 bellow group 2 NODATA03
    //        that break pasthrough layer 0
    if (0 == S52_PL_cmpCmdParam(obj, "NODATA03")) {
        /* Note: UNSARE (unsurveyed area) on group 2
        if (S52_PRIO_GROUP1 != S52_PL_getDPRI(obj)) {
            // FIXME: skip return if group 2
            PRINTF("DEBUG: nodata on group 2\n");
            g_assert(0);
        }
        //*/
        return TRUE;
    }
    //*/

#ifdef S52_USE_GL2
    return _renderAP_gl2(obj);
#endif
#ifdef S52_USE_GL1
    return _renderAP_gl1(obj);
#endif
}

static int       _traceCS(S52_obj *obj)
// failsafe trap --should not get here
{
    if (0 == S52_PL_cmpCmdParam(obj, "DEPCNT02"))
        PRINTF("DEPCNT02\n");

    if (0 == S52_PL_cmpCmdParam(obj, "LIGHTS05"))
        PRINTF("LIGHTS05\n");

    PRINTF("WARNING: _traceCS()\n");
    g_assert(0);

    return TRUE;
}

static int       _traceOP(S52_obj *obj)
{
    (void) obj;

    // debug:
    //PRINTF("DEBUG: OVERRIDE PRIORITY: %s, TYPE: %s\n", S52_PL_getOBCL(obj), S52_PL_infoLUP(obj));

    return TRUE;
}

static int       _renderTXTAA(S52_obj *obj, S52_Color *color, double x, double y, unsigned int bsize, unsigned int weight, const char *str)
// render text in AA if Mar Param set
// Note: PLib C1 CHARS for TE() & TX() alway '15110' - ie style = 1 (alway), weigth = '5' (medium), width = 1 (alway), bsize = 10
// Note: weight is already converted from '4','5','6' to int 0,1,2
// Note: obj can be NULL
// Note: all text command pass here
{
    // FIXME: use 'bsize'
    //(void) bsize;

    if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    if (weight >= S52_MAX_FONT) {
        PRINTF("WARNING: weight(%i) >= S52_MAX_FONT(%i)\n", weight, S52_MAX_FONT);
        g_assert(0);
        return FALSE;
    }

    if (bsize >= S52_MAX_FONT) {
        //bsize -= 10;  // 0 - too small
        bsize -=  9;  // 1 - small


        //PRINTF("WARNING: bsize(%i) >= S52_MAX_FONT(%i) str:%s\n", bsize, S52_MAX_FONT, str);
        //return FALSE;
    }

    // FIXME: cursor pick
    if (S52_GL_PICK == _crnt_GL_cycle) {
        PRINTF("DEBUG: picking text not handled\n");
        return FALSE;
    }

    // debug - check logic (should this be a bug!)
    if (NULL == str) {
        PRINTF("DEBUG: warning NULL str\n");
        return FALSE;
    }

// ---- FREETYPE GL ----------------------------------------------------
#ifdef S52_USE_FREETYPE_GL
#ifdef S52_USE_GL2
    // static text
    guint  len    =   0;
    double strWpx = 0.0;
    double strHpx = 0.0;
    char   hjust  = '3';  // LEFT   (default)
    char   vjust  = '1';  // BOTTOM (default)

    if ((NULL!=obj) && (S52_GL_DRAW==_crnt_GL_cycle)) {
        GLuint vboID = S52_PL_getFreetypeGL_VBO(obj, &len, &strWpx, &strHpx, &hjust, &vjust);
        if (0 != vboID) {
        //if (GL_TRUE == glIsBuffer(vboID)) {
            // connect to data in VBO on GPU
            glBindBuffer(GL_ARRAY_BUFFER, vboID);
        } else {
            //_freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight, &strWpx, &strHpx);
            _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, bsize, &strWpx, &strHpx);
            if (0 == _freetype_gl_buffer->len)
                return TRUE;
            else
                len = _freetype_gl_buffer->len;

            glGenBuffers(1, &vboID);

            // glIsBuffer() fail!
            if (0 == vboID) {
                PRINTF("ERROR: glGenBuffers() fail\n");
                g_assert(0);
                return FALSE;
            }

            S52_PL_setFreetypeGL_VBO(obj, vboID, _freetype_gl_buffer->len, strWpx, strHpx);

            // bind VBOs for vertex array
            glBindBuffer(GL_ARRAY_BUFFER, vboID);      // for vertex coordinates
            // upload freetype_gl data to GPU
            glBufferData(GL_ARRAY_BUFFER,
                         _freetype_gl_buffer->len * sizeof(_freetype_gl_vertex_t),
                         (const void *)_freetype_gl_buffer->data,
                         GL_STATIC_DRAW);
        }
    }

    //
    // update dynamique str dim.
    //

    // dynamique text - layer 9
    if (S52_GL_LAST == _crnt_GL_cycle) {
        //_freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight, &strWpx, &strHpx);
        _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, bsize, &strWpx, &strHpx);
    }

    // lone text - S52_GL_drawStr() / S52_GL_drawStrWorld()
    if (S52_GL_NONE == _crnt_GL_cycle) {
        //_freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight, &strWpx, &strHpx);
        _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, bsize, &strWpx, &strHpx);
    }

    /* debug: draw all text sans just. Change color to DNGHL
    {
        S52_Color *c = S52_PL_getColor("DNGHL");   // danger conspic.
        _setFragColor(c);

        _renderTXTAA_gl2(x, y, NULL, len);
    }
    //*/

    // apply text justification
    _justifyTXTPos(strWpx, strHpx, hjust, vjust, &x, &y);
    //PRINTF("DEBUG: pos XY: %f/%f H/V: %c %c (%s)\n", x, y, hjust, vjust, str);

#ifdef S52_USE_TXT_SHADOW
    if (NULL != color) {
        S52_Color *c = S52_PL_getColor("UIBCK");  // opposite of CHBLK
        _setFragColor(c);

        // lower right - OK
        if ((S52_GL_LAST==_crnt_GL_cycle) || (S52_GL_NONE==_crnt_GL_cycle)) {
            // some MIO change age of target - need to resend the string
            _renderTXTAA_gl2(x+_scalex, y-_scaley, (GLfloat*)_freetype_gl_buffer->data, _freetype_gl_buffer->len);
        } else {
            _renderTXTAA_gl2(x+_scalex, y-_scaley, NULL, len);
        }
    }
#endif  // S52_USE_TXT_SHADOW

    if (NULL != color)
        _setFragColor(color);

    if ((S52_GL_LAST==_crnt_GL_cycle) || (S52_GL_NONE==_crnt_GL_cycle)) {
        // some MIO change age of target - need to resend the string
        _renderTXTAA_gl2(x, y, (GLfloat*)_freetype_gl_buffer->data, _freetype_gl_buffer->len);
    } else {
        _renderTXTAA_gl2(x, y, NULL, len);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

#endif  // S52_USE_GL2
#endif  // S52_USE_FREETYPE_GL

// ---- GLC -----------------------------------------------------------
#ifdef S52_USE_GLC
    if (TRUE == (int) S52_MP_get(S52_MAR_ANTIALIAS)) {

        _glMatrixSet(VP_WIN);

        projUV p = {x, y};
        p = _prj2win(p);

        glRasterPos3d((int)p.u, (int)p.v, 0.0);
        PRINTF("GLC:%s\n", str);

        glcRenderString(str);

        _glMatrixDel(VP_WIN);

        _checkError("_renderTXTAA() / POINT_T");

        return TRUE;
    }
#endif

// ---- COGL -----------------------------------------------------------
#ifdef S52_USE_COGL
    // debug - skip all cogl call
    //return TRUE;

    CoglColor color;

    cogl_color_set_from_4ub(&color, 200, 0, 0, 0);

    //CoglColor white;
    //cogl_color_set_from_4f (&white, 1.0, 1.0, 1.0, 0.5);
    //cogl_color_set_from_4f (&white, 1.0, 1.0, 1.0, 1.0);
    //cogl_color_premultiply (&white);


    //GString *s = g_string_new(str);
    //GString *s = g_string_new("test");
    //ClutterActor *_text = clutter_text_new();
    //clutter_text_set_text(CLUTTER_TEXT(_text), "test");
    //clutter_text_set_text(CLUTTER_TEXT(_text), str);
    //_PangoLayout = clutter_text_get_layout(CLUTTER_TEXT(_text));


    //pango_layout_set_text(_PangoLayout, s->str, s->len);
    pango_layout_set_text(_PangoLayout, "test", 4);
    //g_string_free(s, TRUE);

    projUV p = {x, y};
    p = _prj2win(p);
    //static int xxx = 0;
    //static int yyy = 0;
    cogl_pango_render_layout(_PangoLayout, p.u, p.v, &color, 0);
    //cogl_pango_render_layout(_PangoLayout, x, y, &color, 0);
    //cogl_pango_render_layout(_PangoLayout, xxx++, yyy++, &color, 0);
    //cogl_pango_render_layout(_PangoLayout, 0, 0, &color, 0);
    //cogl_pango_render_layout(_PangoLayout, 100, 100, &white, 0);
    //cogl_pango_render_layout(_PangoLayout, p.u, p.v, &white, 0);
#endif

// ---- FTGL -----------------------------------------------------------
#ifdef S52_USE_FTGL
    (void)obj;

    double n = _view.north;
    _view.north = 0.0;

    _setFragColor(color);

    //_setBlend(FALSE);


    projUV p = {x, y};
    p = _prj2win(p);

    _glMatrixSet(VP_WIN);    // new - set win coord

    //glRasterPos2i(p.u, p.v);  // round to pixels
    glRasterPos2d(p.u, p.v);

    if (NULL != _ftglFont[weight]) {
        //PRINTF("DEBUG: ftgl:X/Y/str: %f %f %s\n", p.u, p.v, str);
        ftglRenderFont(_ftglFont[weight], str, FTGL_RENDER_ALL);
    }

    _glMatrixDel(VP_WIN);

    _checkError("_renderTXTAA() / POINT_T");

    _view.north = n;
#endif // S52_USE_FTGL


    return TRUE;
}

static int       _renderTXT(S52_obj *obj)
// render TE or TX
// Note: NOT all text command pass here (ex: legends are system generated)
{
    if (0.0 == S52_MP_get(S52_MAR_SHOW_TEXT))
        return FALSE;

    // also in _renderTXTAA()
    if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    guint      npt   = 0;
    GLdouble  *ppt   = NULL;
    S57_geo *geoData = S52_PL_getGeo(obj);
    if (FALSE == S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    S52_Color   *color  = NULL;
    int          xoffs  = 0;
    int          yoffs  = 0;
    unsigned int bsize  = 0;
    unsigned int weight = 0;
    int          disIdx = 0;  // text view group
    const char  *str    = S52_PL_getEX(obj, &color, &xoffs, &yoffs, &bsize, &weight, &disIdx);
    //PRINTF("DEBUG: xoffs/yoffs/bsize/weight: %i/%i/%i/%i:%s\n", xoffs, yoffs, bsize, weight, str);

    // supress display of text
    if (FALSE == (int) S52_MP_getTextDisp(disIdx))
        return FALSE;

    if (NULL == str) {
        //PRINTF("DEBUG: NULL text string\n");
        return FALSE;
    }
    if (0 == strlen(str)) {
        PRINTF("DEBUG: no text!\n");
        return FALSE;
    }

    /* debug: draw all text sans just. Change color to DNGHL
    {
        S52_Color *c = S52_PL_getColor("DNGHL");   // danger conspic.
        _setFragColor(c);

        _renderTXTAA(obj, c, ppt[0], ppt[1], bsize, weight, str);
    }
    //*/


    // convert pivot point u/v offset to PRJ
    double uoffs = ((bsize * PICA * xoffs) / S52_MP_get(S52_MAR_DOTPITCH_MM_X)) * _scalex;
    double voffs = ((bsize * PICA * yoffs) / S52_MP_get(S52_MAR_DOTPITCH_MM_Y)) * _scaley;
    // debug
    //double uoffs = 0.0;
    //double voffs = 0.0;

    if (S57_POINT_T == S57_getObjtype(geoData)) {
        _renderTXTAA(obj, color, ppt[0]+uoffs, ppt[1]-voffs, bsize, weight, str);

        return TRUE;
    }

    if (S57_LINES_T == S57_getObjtype(geoData)) {
        if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
            // past track time
            for (guint i=0; i<npt; ++i) {
                int timeHH = ppt[i*3 + 2] / 100;
                int timeMM = ppt[i*3 + 2] - (timeHH * 100);

                char s[80];
                //SNPRINTF(s, 80, "t%04.f", ppt[i*3 + 2]);     // S52 PASTRK01 say prefix a 't'
                SNPRINTF(s, 80, "%02i:%02i", timeHH, timeMM);  // ISO say HH:MM

                _renderTXTAA(obj, color, ppt[i*3 + 0]+uoffs, ppt[i*3 + 1]-voffs, bsize, weight, s);
            }

            return TRUE;
        }

        if (0 == g_strcmp0("clrlin", S57_getName(geoData))) {
            double orient = ATAN2TODEG(ppt);
            double x = (ppt[3] + ppt[0]) / 2.0;
            double y = (ppt[4] + ppt[1]) / 2.0;

            //if (orient < 0.0) orient += 360.0;

            char s[80];
            SNPRINTF(s, 80, "%s %03.f", str, orient);

            _renderTXTAA(obj, color, x+uoffs, y-voffs, bsize, weight, s);

            return TRUE;
        }

        if (0 == g_strcmp0("leglin", S57_getName(geoData))) {
            // cog
            if (0 == S52_PL_cmpCmdParam(obj, "leglin")) {
                // TX(leglin,3,1,2,'15112',0,0,CHBLK,51)
                //double orient = ATAN2TODEG(ppt);
                double orient = S52_atof(str);

                // mid point of leg
                double x = (ppt[3] + ppt[0]) / 2.0;
                double y = (ppt[4] + ppt[1]) / 2.0;

                //if (orient < 0.0) orient += 360.0;

                char s[80];
                //SNPRINTF(s, 80, "%03.f cog", orient);
                SNPRINTF(s, 80, "%03.f deg", orient);

                //PRINTF("DEBUG: xoffs/yoffs/bsize/weight: %i/%i/%i/%i:%s\t%s\n", xoffs, yoffs, bsize, weight, str, s);

                _renderTXTAA(obj, color, x+uoffs, y-voffs, bsize, weight, s);
            }

            // planned speed
            if (0 == S52_PL_cmpCmdParam(obj, "plnspd")) {
                // TX(plnspd,1,2,2,'15110',0,0,CHBLK,51)
                GString *plnspdstr = S57_getAttVal(geoData, "plnspd");
                double   plnspd    = (NULL==plnspdstr) ? 0.0 : S52_atof(plnspdstr->str);

                if (0.0 != plnspd) {
                    double offset_x = 10.0 * _scalex;
                    double offset_y = 18.0 * _scaley;

                    // draw speed text inside box
                    // FIXME: compute offset from symbol's bbox
                    // S52_PL_getSYbbox(S52_obj *obj, int *width, int *height);
                    char s[80];
                    SNPRINTF(s, 80, "%3.1f kt", plnspd);

                    // FIXME: get color from leglin (orange or yellow) instead of black (not in S52)
                    //S52_Color *color = S52_PL_getColor("???");

                    //PRINTF("DEBUG: xoffs/yoffs/bsize/weight: %i/%i/%i/%i:%s\t%s\n", xoffs, yoffs, bsize, weight, str, s);
                    _renderTXTAA(obj, color, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, s);
                }
            }

            return TRUE;
        }


        {   // other text (ex bridge)
            double cView_x = (_pmax.u + _pmin.u) / 2.0;
            double cView_y = (_pmax.v + _pmin.v) / 2.0;
            double dmin    = INFINITY;
            double xmin, ymin;

            // find segment's center point closess to view center
            // FIXME: clip segments to view
            // clang -fsanitize=address choke here
            //for (guint i=0; i<npt; ++i) {
            for (guint i=0; i<npt-1; ++i) {
                double x = (ppt[i*3+3] + ppt[i*3+0]) / 2.0;
                double y = (ppt[i*3+4] + ppt[i*3+1]) / 2.0;
                double d = sqrt(pow(x-cView_x, 2) + pow(y-cView_y, 2));

                if (dmin > d) {
                    dmin = d;
                    xmin = x;
                    ymin = y;
                }
            }

            if (INFINITY != dmin) {
                _renderTXTAA(obj, color, xmin+uoffs, ymin-voffs, bsize, weight, str);
            }
        }

        return TRUE;
    }

    if (S57_AREAS_T == S57_getObjtype(geoData)) {

        _computeCentroid(geoData);

        for (guint i=0; i<_centroids->len; ++i) {
            pt3 *pt = &g_array_index(_centroids, pt3, i);

            _renderTXTAA(obj, color, pt->x+uoffs, pt->y-voffs, bsize, weight, str);
            //PRINTF("TEXT (%s): %f/%f\n", str, pt->x, pt->y);

            // only draw the first centroid
            if (0.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS))
                return TRUE;
        }

        return TRUE;
    }

    PRINTF("DEBUG: don't know how to draw this TEXT\n");

    g_assert(0);

    return FALSE;
}


//-----------------------------------
//
// SYMBOL CREATION SECTION
//
//-----------------------------------

static S57_prim *_parseHPGL(S52_vec *vecObj, S57_prim *vertex)
// Display List generator - use for VBO also
// VBO: collect all vectors for 1 colour/pen_w/trans
{
    // Assume: a width of 1 unit is 1 pixel.
    // WARNING: offet might need adjustment since bounding box doesn't include line tickness.
    // Note: Pattern upto PLib 3.2, use a line width of 1.
    // Note: transparency: '0'=0%(opaque), '1'=25%, '2'=50%, '3'=75%

    // FIXME: instruction EP (Edge Polygon), AA (Arc Angle) and SC (Symbol Call)
    //        are not used i PLib/Chart-1 3.1, so not implemented.

    GLenum    fillMode = GLU_SILHOUETTE;
    //GLenum    fillMode = GLU_FILL;
    S52_vCmd  vcmd     = S52_PL_getNextVOCmd(vecObj);
    int       CI       = FALSE;

    //vertex_t *fristCoord = NULL;

    // debug
    //if (0==strncmp("TSSJCT02", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}
    //if (0==strncmp("BOYLAT13", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}
    //if (0==strncmp("CHKSYM01", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}
    // Note: bbx = 7173, bby = 1445, pivot_x = 7371, pivot_y = 1657
    // SYMD   39 AISSIX01 V 07371 01657 00405 00214 07173 01445
    //if (0==strncmp("AISSIX01", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}

    /*
    // FIXME: CHRVID01, CHRVID02, CHDATD01 in PLib 4.0 draft as xyz coord. The code should handle
    // wrong input gracefully - xy; instead of xyz; the latter is not apparently in S52 4.0
    if (0==strncmp("CHRVID01", S52_PL_getVOname(vecObj), 8)) {
        PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
        return vertex;
    }
    if (0==strncmp("CHRVID02", S52_PL_getVOname(vecObj), 8)) {
        PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
        return vertex;
    }
    if (0==strncmp("CHDATD01", S52_PL_getVOname(vecObj), 8)) {
        PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
        return vertex;
    }
    //*/

    // debug - check if more than one NEW token - YES
    while (S52_VC_NEW == vcmd) {
        vcmd = S52_PL_getNextVOCmd(vecObj);
    }

    while ((S52_VC_NONE!=vcmd) && (S52_VC_NEW!=vcmd)) {

        switch (vcmd) {

            case S52_VC_NONE: break;
            case S52_VC_NEW:  break;

            /*
            case S52_VC_SW: { // this mean there is a change in pen width

#if !defined(S52_USE_OPENGL_VBO)
                // draw vertex with previous pen width
                GArray *v = S57_getPrimVertex(vertex);
                if (0 < v->len) {
                    char pen_w = S52_PL_getVOwidth(vecObj);
                    _DrawArrays(vertex);
                    S57_initPrim(vertex); //reset
                    _glLineWidth(pen_w - '0');
                    _glPointSize(pen_w - '0');
                }
#endif
                break;
            }
            //*/

            // NOTE: entering poly mode fill a circle (CI) when a CI command
            // is found between PM0 and PM2
            case S52_VC_PM: // poly mode PM0/PM2, fill disk when not in PM
                fillMode = (GLU_FILL==fillMode) ? GLU_SILHOUETTE : GLU_FILL;
                //fillMode = GLU_FILL;

                break;

            case S52_VC_CI: {  // circle --draw immediatly
                GLdouble  radius = S52_PL_getVOradius(vecObj);
                GArray   *vec    = S52_PL_getVOdata(vecObj);
                if (NULL == vec) {
                    PRINTF("ERROR: vector NULL\n");
                    g_assert(0);
                    continue;
                }
                vertex_t *data   = (vertex_t *)vec->data;
                GLint     slices = 32;
                GLint     loops  = 1;
                GLdouble  inner  = 0.0;        // radius

                GLdouble  outer  = radius;     // in 0.01mm unit
/*
#ifdef S52_USE_OPENGL_VBO
                // compose symb need translation at render-time
                // (or add offset everything afterward!)
                _glBegin(_TRANSLATE, vertex);
                S57_addPrimVertex(vertex, data);
                _glEnd(vertex);
#else
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslated(data[0], data[1], data[2]);
#endif
*/
                // FIXME: optimisation, draw a point instead of a filled disk
                // use fillMode & radius * dotpitch = pixel

#ifdef S52_USE_OPENGL_VBO
                // compose symb need translation at render-time
                // (or add offset everything afterward!)
                _glBegin(_TRANSLATE, vertex);
                S57_addPrimVertex(vertex, data);
                _glEnd(vertex);

                // pass 'vertex' via global '_diskPrimTmp' used by _gluDisk()
                _diskPrimTmp = vertex;

                if (GLU_FILL == fillMode) {
                    _gluQuadricDrawStyle(_qobj, GLU_FILL);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                } else {  //LINE
                    _gluQuadricDrawStyle(_qobj, GLU_LINE);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                }
                // finish with tmp pointer to buffer
                _diskPrimTmp = NULL;

#else
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslated(data[0], data[1], data[2]);

                if (GLU_FILL == fillMode) {
                    gluQuadricDrawStyle(_qobj, GLU_FILL);
                    gluDisk(_qobj, inner, outer, slices, loops);
                }
                // when in fill mode draw outline (antialias)
                gluQuadricDrawStyle(_qobj, GLU_SILHOUETTE);
                gluDisk(_qobj, inner, outer, slices, loops);
                glPopMatrix();
#endif


/*
#if !defined(S52_USE_OPENGL_VBO)
                // when in fill mode draw outline (antialias)
                gluQuadricDrawStyle(_qobj, GLU_SILHOUETTE);
                gluDisk(_qobj, inner, outer, slices, loops);
                glPopMatrix();
#endif
*/
                CI = TRUE;
                //continue;
                break;
            }

            case S52_VC_FP: { // fill poly immediatly
                //PRINTF("fill poly: start\n");
                GArray   *vec  = S52_PL_getVOdata(vecObj);
                if (NULL == vec) {
                    PRINTF("ERROR: vector NULL\n");
                    g_assert(0);
                    continue;
                }
                vertex_t *data = (vertex_t *)vec->data;

                // circle is already filled
                if (TRUE == CI) {
                    CI = FALSE;
                    break;
                }

                // remember first coord
                //fristCoord = data;
                _g_ptr_array_clear(_tmpV);

                // FIXME: check poly winding - to skip ODD for ISODGR01
                // ODD needed for symb. ISODGR01 + glDisable(GL_CULL_FACE);

                gluTessBeginPolygon(_tobj, vertex);
                gluTessBeginContour(_tobj);

#ifdef S52_USE_GL2
                _f2d(_tessWorkBuf_d, vec->len, data);
                double *dptr = (double*)_tessWorkBuf_d->data;
                for (guint i=0; i<_tessWorkBuf_d->len; ++i, dptr+=3) {
                    gluTessVertex(_tobj, (GLdouble*)dptr, (void*)dptr);
                    //PRINTF("x/y/z %f/%f/%f\n", dptr[0], dptr[1], dptr[2]);
                }
#else
                for (guint i=0; i<vec->len; ++i, data+=3) {
                    gluTessVertex(_tobj, (GLdouble*)data, (void*)data);
                    //PRINTF("x/y/z %f/%f/%f\n", d[0],d[1],d[2]);
                }
#endif
                gluTessEndContour(_tobj);
                gluTessEndPolygon(_tobj);

                _checkError("_parseHPGL()");

                // FIXME: draw line (trigger AA)
                //_DrawArrays_LINE_STRIP(vec->len, (GLdouble *)vec->data);

                // trick from NeHe
                // Draw smooth anti-aliased outline over polygons.
                // FIXME: is this code affect states in DList?
                // if so then valid for GL1.x only (not VBO code path)
                //glEnable( GL_BLEND );
                //glEnable( GL_LINE_SMOOTH );
                //glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
                //DrawObjects();
                //glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                //glDisable( GL_LINE_SMOOTH );
                //glDisable( GL_BLEND );

                break;
            }

            case S52_VC_PD:    // pen down
            case S52_VC_PU: {  // pen up
                /*
                // paranoia
                GArray *vec = S52_PL_getVOdata(vecObj);
                if (0 == vec->len) {
                    PRINTF("Vector Object Name: %s  Command: %c - NO VECTOR!\n", S52_PL_getVOname(vecObj), vcmd);
                    g_assert(0);
                    break;
                }
                */

                {
                    _glBegin(GL_LINES,  vertex);
                    while ((S52_VC_PD==vcmd) || (S52_VC_PU==vcmd)) {
                        GArray   *vec  = S52_PL_getVOdata(vecObj);
                        if (NULL == vec) {
                            PRINTF("ERROR: vector NULL\n");
                            g_assert(0);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                            continue;
                        }
                        vertex_t *data = (vertex_t *)vec->data;


                        // failsafe
                        if (0 == vec->len)
                            continue;

                        // POINTS
                        // draw points as a short line so one glDraw call
                        // to render all: points, lines, strip to save a lone glDraw point call
                        if (1 == vec->len) {
                            vertex_t data1[3] = {data[0]+20.0, data[1]+20.0, 0.0};
                            S57_addPrimVertex(vertex, data);
                            S57_addPrimVertex(vertex, data1);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                            continue;
                        }

                        // LINES
                        if (2 == vec->len) {
                            S57_addPrimVertex(vertex, data+0);
                            S57_addPrimVertex(vertex, data+3);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                            continue;
                        }

                        // split STRIP into LINES
                        for (guint i=1; i<vec->len; ++i, data+=3) {
                            S57_addPrimVertex(vertex, data+0);
                            S57_addPrimVertex(vertex, data+3);
                        }

                        vcmd = S52_PL_getNextVOCmd(vecObj);
                    }
                    _glEnd(vertex);

                    // vcmd is set, go to the outer while
                    continue;
                }

            }

            // should not get here since these command
            // have already been filtered out in _filterVector() & S52_PL_getNextVOCmd()
            case S52_VC_SP: // color
            case S52_VC_SW: // pen_w
            case S52_VC_ST: // transparancy
            case S52_VC_SC: // symbol call

            // not used in PLib --not implemented
            case S52_VC_AA: // arc

            default:
                PRINTF("ERROR: invalid vector command: (%c)\n", vcmd);
                g_assert(0);
                return vertex;
        }

        vcmd = S52_PL_getNextVOCmd(vecObj);

    } /* while */


#if !defined(S52_USE_OPENGL_VBO)
    _DrawArrays(vertex);
    S57_donePrim(vertex);
    vertex = NULL;
#endif

    _checkError("_parseHPGL()");

    return vertex;
}

static GLint     _buildPatternDL(gpointer key, gpointer value, gpointer data)
{
    // 'key' not used
    (void) key;
    // 'data' not used
    (void) data;

    S52_symDef *symDef = (S52_symDef*)value;
    S52_DList  *DL     = S52_PL_getDLData(symDef);

    //PRINTF("PATTERN: %s\n", (char*)key);

    // is this symbol need to be re-created (update PLib)
    if (FALSE == DL->create)
        return FALSE;

#ifdef S52_USE_OPENGL_VBO
#if !defined(S52_USE_GLSC2)
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        // NOTE: DL->nbr is 1 - all pattern in PLib have only one color
        // but this is not in S52 specs (check this)
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        PRINTF("TODO: debug this code path\n");
        g_assert(0);
        return FALSE;
    }

    // debug
    if (MAX_SUBLIST < DL->nbr) {
        PRINTF("ERROR: sublist overflow -1-\n");
        g_assert(0);
        return FALSE;
    }
#endif  // !S52_USE_GLSC2
#else   // S52_USE_OPENGL_VBO

#if !defined(S52_USE_GLSC1)
    // can't delete a display list - no garbage collector in GL SC
    // first delete DL
    //if (TRUE == glIsBuffer(DL->vboIds[0])) {
    // GL1 win32
    if (0 != DL->vboIds[0]) {
        glDeleteLists(DL->vboIds[0], DL->nbr);
        DL->vboIds[0] = 0;
    }
#endif

    // then create new one
    DL->vboIds[0] = glGenLists(DL->nbr);
    if (0 == DL->vboIds[0]) {
        PRINTF("ERROR: glGenLists() failed .. exiting\n");
        g_assert(0);
        return FALSE;
    }
    glNewList(DL->vboIds[0], GL_COMPILE);

#endif  // S52_USE_OPENGL_VBO

    S52_vec *vecObj = S52_PL_initVOCmd(symDef);

    // set default
    //_glLineWidth(1.0);

    if (NULL == DL->prim[0])
        DL->prim[0]  = S57_initPrim(NULL);
    else {
        PRINTF("ERROR: DL->prim[0] not NULL\n");
        g_assert(0);
        return FALSE;
    }

#ifdef S52_USE_OPENGL_VBO
    // using VBO we need to keep some info (mode, first, count)
    DL->prim[0]   = _parseHPGL(vecObj, DL->prim[0]);
    DL->vboIds[0] = _VBOCreate(DL->prim[0]);

    // set normal mode
    //glBindBuffer(GL_ARRAY_BUFFER, 0);

#else   // S52_USE_OPENGL_VBO

    DL->prim[0] = _parseHPGL(vecObj, DL->prim[0]);
    glEndList();

#endif  // S52_USE_OPENGL_VBO

    S52_PL_doneVOCmd(vecObj);

    _checkError("_buildPatternDL()");

    return 0;
}

static GLint     _buildSymbDL(gpointer key, gpointer value, gpointer data)
{
    // 'key' not used
    (void) key;
    // 'data' not used
    (void) data;

    S52_symDef *symDef = (S52_symDef*)value;
    S52_DList  *DL     = S52_PL_getDLData(symDef);

    // is this symbol need to be re-created (update PLib)
    if (FALSE == DL->create)
        return FALSE;

    // debug
    if (MAX_SUBLIST < DL->nbr) {
        PRINTF("ERROR: sublist overflow -0-\n");
        g_assert(0);
        return FALSE;
    }

#ifdef S52_USE_OPENGL_VBO
#if !defined(S52_USE_GLSC2)
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        PRINTF("TODO: debug this code path\n");
        g_assert(0);
        return FALSE;
    }
#endif  // !S52_USE_GLSC1

    //glGenBuffers(DL->nbr, &DL->vboIds[0]);

#else

#if !defined(S52_USE_GLSC1)
    // in ES SC: can't delete a display list --no garbage collector
    if (0 != DL->vboIds[0]) {
        glDeleteLists(DL->vboIds[0], DL->nbr);
    }
#endif

    // then create new one
    DL->vboIds[0] = glGenLists(DL->nbr);
    if (0 == DL->vboIds[0]) {
        PRINTF("ERROR: glGenLists() failed .. exiting\n");
        g_assert(0);
        return FALSE;
    }
#endif // S52_USE_OPENGL_VBO

    // reset line
    _glLineWidth(1.0);
    //_glPointSize(1.0);

    S52_vec *vecObj  = S52_PL_initVOCmd(symDef);

    for (guint i=0; i<DL->nbr; ++i) {
        if (NULL == DL->prim[i])
            DL->prim[i]  = S57_initPrim(NULL);
        else {
            PRINTF("ERROR: DL->prim[i] not NULL\n");
            g_assert(0);
            return FALSE;
        }
    }

#ifdef S52_USE_OPENGL_VBO
    for (guint i=0; i<DL->nbr; ++i) {
        // using VBO we need to keep some info (mode, first, count)
        DL->prim[i]   = _parseHPGL(vecObj, DL->prim[i]);
        DL->vboIds[i] = _VBOCreate(DL->prim[i]);
    }
    // return to normal mode
    //glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
    GLuint listIdx = DL->vboIds[0];
    for (guint i=0; i<DL->nbr; ++i) {

        glNewList(listIdx++, GL_COMPILE);

        DL->prim[i] = _parseHPGL(vecObj, DL->prim[i]);

        glEndList();
    }
#endif // S52_USE_OPENGL_VBO

    _checkError("_buildSymbDL()");

    S52_PL_doneVOCmd(vecObj);

    return 0; // 0 continue traversing
}

static GLint     _createSymb(void)
{
    if (TRUE == _symbCreated)
        return TRUE;

    // FIXME: what if the ViewPort is to small !?

    _glMatrixSet(VP_WIN);

    S52_PL_traverse(S52_SMB_PATT, _buildPatternDL);
    PRINTF("PATT symbol finish\n");

    S52_PL_traverse(S52_SMB_LINE, _buildSymbDL);
    PRINTF("LINE symbol finish\n");

    S52_PL_traverse(S52_SMB_SYMB, _buildSymbDL);
    PRINTF("SYMB symbol finish\n");

    _glMatrixDel(VP_WIN);

    _checkError("_createSymb()");

    _symbCreated = TRUE;

    PRINTF("DEBUG: PLib sym created ..\n");

    return TRUE;
}

int        S52_GL_isSupp(S52_obj *obj)
// TRUE if display of object is suppressed
// also collect stat
{
    // debug
    //if (0 == g_strcmp0("M_COVR", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: M_COVR FOUND\n");
    //}
    //if (0 == g_strcmp0("m_covr", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: m_covr FOUND\n");
    //}
    //if (0 == g_strcmp0("sclbdy", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: sclbdy FOUND\n");
    //}

    // debug: some HO set a scamin on DISPLAYBASE obj!?
    // Note: obj on BASE can't be set to OFF
    if (DISPLAYBASE == S52_PL_getDISC(obj)) {
        return FALSE;
    }

    if (S52_SUPP_ON == S52_PL_getObjToggleState(obj)) {
        ++_oclip;
        return TRUE;
    }

    // SCAMIN
    if (TRUE == (int) S52_MP_get(S52_MAR_SCAMIN)) {
        S57_geo *geo  = S52_PL_getGeo(obj);
        double scamin = S57_getScamin(geo);

        if (scamin < _SCAMIN) {
            ++_oclip;
            return TRUE;
        }
    }

    // FIXME: not great - test every obj
    if (0 == (int) S52_MP_get(S52_MAR_DISP_HODATA)) {
        if (0 == g_strcmp0("M_COVR", S52_PL_getOBCL(obj))) {
            //PRINTF("DEBUG: M_COVR FOUND\n");
            return TRUE;
        }
    }

    return FALSE;
}

int        S52_GL_isOFFview(S52_obj *obj)
// TRUE if object not in view
{
    // FIXME: handle this case like extent in VRMEBL!

    // FIXME: AIS + Vector / Heading, also beam bearing

    // debug
    //if (0 == g_strcmp0("pastrk", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: pastrk FOUND\n");
    //}
    //if (0 == g_strcmp0("M_COVR", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: M_COVR FOUND\n");
    //}
    //if (0 == g_strcmp0("m_covr", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: m_covr FOUND\n");
    //}
    //if (0 == g_strcmp0("sclbdy", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: sclbdy FOUND\n");
    //}
    //if (0 == g_strcmp0("PRDARE", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: PRDARE FOUND\n");
    //}


    // geo extent _gmin/max
    double x1,y1,x2,y2;
    S57_geo *geo = S52_PL_getGeo(obj);
    if (FALSE == S57_getExt(geo, &x1, &y1, &x2, &y2))
        return FALSE;

    // FIXME: look for trick to bailout if func fail
    //(TRUE==S57_getExt(geo, &x1, &y1, &x2, &y2)? TRUE : return FALSE;

    // S-N limits
    if ((y2 < _gmin.v) || (y1 > _gmax.v)) {
        ++_oclip;
        return TRUE;
    }

    // E-W limits
    if (_gmax.u < _gmin.u) {
        // anti-meridian E-W limits
        if ((x2 < _gmin.u) && (x1 > _gmax.u)) {
            ++_oclip;
            return TRUE;
        }
    } else {
        if ((x2 < _gmin.u) || (x1 > _gmax.u)) {
            ++_oclip;
            return TRUE;
        }
    }

    return FALSE;
}

#ifdef S52_USE_GL2
#if defined(S52_USE_RASTER) || defined(S52_USE_RADAR)
static int       _newTexture(S52_GL_ras *raster)
// copy and blend raster 'data' to alpha texture
// FIXME: test if the use of shader to blend rather than precomputing value here is faster
{
    double min   =  INFINITY;
    double max   = -INFINITY;
    guint npotX  = raster->w;
    guint npotY  = raster->h;
    float *dataf = (float*) raster->data;
    guint count  = raster->w * raster->h;

    // GLES2/XOOM ALPHA fail and if not POT
    struct rgba {unsigned char r,g,b,a;};
    guchar *texAlpha = g_new0(guchar, count * sizeof(struct rgba));
    struct rgba *texTmp   = (struct rgba*) texAlpha;

    float safe  = (float) S52_MP_get(S52_MAR_SAFETY_CONTOUR) * -1.0;  // change signe
    float deep  = (float) S52_MP_get(S52_MAR_DEEP_CONTOUR)   * -1.0;  // change signe

    // debug
    int nFTLMAX = 0;
    int nNoData = 0;
    for (guint i=0; i<count; ++i) {

        // debug - fill .tiff area with color
        //texTmp[i].a = 255;
        //continue;


        if (raster->nodata == dataf[i]) {
            ++nNoData;
            texTmp[i].a = 0;
            continue;
        }
        if (G_MAXFLOAT == dataf[i]) {
            ++nFTLMAX;

            texTmp[i].a = 0;
            continue;

            // debug
            //texTmp[i].a = 255;
            //continue;
        }

        min = MIN(dataf[i], min);
        max = MAX(dataf[i], max);

        // debug
        //texTmp[i].a = 255;
        //continue;

        if ((safe/2.0) <= dataf[i]) {
            texTmp[i].a = 0;
            continue;
        }
        // SAFETY CONTOUR
        if (safe <= dataf[i]) {
            texTmp[i].a = 255;
            continue;
        }
        // DEEP CONTOUR
        if (deep <= dataf[i]) {
            texTmp[i].a = 100;
            continue;
        }

        // debug
        //texTmp[i].a = 255;
        //continue;
    }

    raster->min      = min;
    raster->max      = max;
    raster->npotX    = npotX;
    raster->npotY    = npotY;
    raster->texAlpha = texAlpha;

    PRINTF("DEBUG: nFTLMAX=%i nNoData=%i count=%i\n", nFTLMAX, nNoData, count);

    return TRUE;
}

int        S52_GL_drawRaster(S52_GL_ras *raster)
{
    // bailout if not in view
    // FIXME: test anti-meridien
    //if ((raster->pext.E < _pmin.u) || (raster->pext.N < _pmin.v) || (raster->pext.W > _pmax.u) || (raster->pext.S > _pmax.v)) {
    if ((raster->gext.E < _gmin.u) || (raster->gext.N < _gmin.v) || (raster->gext.W > _gmax.u) || (raster->gext.S > _gmax.v)) {
        return TRUE;
    }

    if (0 == raster->texID) {
        if (TRUE == raster->isRADAR) {

            // no ALPHA in GLSC2
#if !defined(S52_USE_GLSC2)
            glGenTextures(1, &raster->texID);
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, raster->npotX, raster->npotY, 0, GL_ALPHA, GL_UNSIGNED_BYTE, raster->texAlpha);
            // modern way
            //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_ALPHA, raster->npotX, raster->npotY);
            //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raster->npotX, raster->npotY, GL_ALPHA, GL_UNSIGNED_BYTE, _fb_pixels);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glBindTexture(GL_TEXTURE_2D, 0);

            _checkError("S52_GL_drawRaster() -1.0-");
#endif  //!S52_USE_GLSC2

        } else {
            // raster / bathy
            _newTexture(raster);

            glGenTextures(1, &raster->texID);
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
#ifdef S52_USE_GLSC2
            // modern way
            glTexStorage2D (GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raster->npotX, raster->npotY, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY, 0, GL_RGBA, GL_UNSIGNED_BYTE, raster->texAlpha);
            // modern way
            //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY);
            //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raster->npotX, raster->npotY, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);

            //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY, 0, GL_RGBA, GL_FLOAT, raster->data);
            //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_FLOAT, raster->data);
            //glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, raster->npotX, raster->npotY, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, raster->data);
            //dword PackValues(byte x,byte y,byte z,byte w) {
            //    return (x<<24)+(y<<16)+(z<<8)+(w);
            //}
#endif
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glBindTexture(GL_TEXTURE_2D, 0);

            _checkError("S52_GL_drawRaster() -2.0-");

            // debug
            PRINTF("DEBUG: MIN=%f MAX=%f\n", raster->min, raster->max);
        }
    } else {
        // no ALPHA in GLSC2
#if !defined(S52_USE_GLSC2)
        // update RADAR
        if (TRUE == raster->isRADAR) {
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raster->npotX, raster->npotY, GL_ALPHA, GL_UNSIGNED_BYTE, raster->texAlpha);

            _checkError("S52_GL_drawRaster() -3.0-");
        }
#endif  // !S52_USE_GLSC2

    }

    // set radar extent
    if (TRUE == raster->isRADAR) {
        raster->pext.S = raster->cLat - raster->rNM * 1852.0;
        raster->pext.W = raster->cLng - raster->rNM * 1852.0;
        raster->pext.N = raster->cLat + raster->rNM * 1852.0;
        raster->pext.E = raster->cLng + raster->rNM * 1852.0;
    }

    // set colour
    if (TRUE == raster->isRADAR) {
        // "RADHI", "RADLO"
        S52_Color *radhi = S52_PL_getColor("RADHI");
        glUniform4f(_uColor, radhi->R/255.0, radhi->G/255.0, radhi->B/255.0, (4 - (radhi->trans - '0'))*TRNSP_FAC_GLES2);
    } else {
        S52_Color *dnghl = S52_PL_getColor("DNGHL");
        glUniform4f(_uColor, dnghl->R/255.0, dnghl->G/255.0, dnghl->B/255.0, (4 - (dnghl->trans - '0'))*TRNSP_FAC_GLES2);
    }

    // to fit an image in a POT texture
    //float fracX = (float)raster->w/(float)raster->potX;
    //float fracY = (float)raster->h/(float)raster->potY;
    float fracX = 1.0;
    float fracY = 1.0;
    vertex_t ppt[4*3 + 4*2] = {
        raster->pext.W, raster->pext.S, 0.0,        0.0f,  0.0f,
        raster->pext.E, raster->pext.S, 0.0,        fracX, 0.0f,
        raster->pext.E, raster->pext.N, 0.0,        fracX, fracY,
        raster->pext.W, raster->pext.N, 0.0,        0.0f,  fracY
    };

    // FIXME: need this for bathy
    glDisable(GL_CULL_FACE);

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    glUniform1f(_uTextOn, 1.0);
    glBindTexture(GL_TEXTURE_2D, raster->texID);

    _glUniformMatrix4fv_uModelview();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    _checkError("S52_GL_drawRaster() -4-");

    glBindTexture(GL_TEXTURE_2D,  0);

    glUniform1f(_uTextOn, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    // FIXME: need this for bathy
    glEnable(GL_CULL_FACE);

    return TRUE;
}
#endif  // S52_USE_RASTER S52_USE_RADAR
#endif  // S52_USE_GL2

#if 0
int        S52_GL_drawLIGHTS(S52_obj *obj)
// draw lights
{
    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
        	case S52_CMD_SIM_LN: _renderLS_LIGHTS05(obj); _ncmd++; break;   // LS
        	case S52_CMD_ARE_CO: _renderAC_LIGHTS05(obj); _ncmd++; break;   // AC

        	default: break;
        }
        cmdWrd = S52_PL_getCmdNext(obj);
    }

    return TRUE;
}
#endif  // 0

int        S52_GL_drawText(S52_obj *obj, gpointer user_data)
// TE&TX
{
    // quiet compiler
    (void)user_data;

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_TXT_TX:
            case S52_CMD_TXT_TE: _ncmd++; _renderTXT(obj); break;

            default: break;
        }
        cmdWrd = S52_PL_getCmdNext(obj);
    }

    // flag that all obj texts has been parsed
    S52_PL_setTextParsed(obj);

    return TRUE;
}

int        S52_GL_draw(S52_obj *obj, gpointer user_data)
// draw all
// later redraw only dirty region
// later redraw only Group 2 object
// later ...
{
    // quiet compiler
    (void)user_data;

    //------------------------------------------------------
    // debug
    //return TRUE;
    //if (0 == strcmp("HRBFAC", S52_PL_getOBCL(obj))) {
    //    PRINTF("HRBFAC found\n");
    //    //return;
    //}
    //if (0 == strcmp("ebline", S52_PL_getOBCL(obj))) {
    //    PRINTF("ebline found\n");
    //}
    //if (0 == strcmp("UNSARE", S52_PL_getOBCL(obj))) {
    //    PRINTF("UNSARE found\n");
    //    //return;
    //}
    //if (0 == strcmp("$CSYMB", S52_PL_getOBCL(obj))) {
    //    PRINTF("$CSYMB found\n");
    //    //return;
    //}
    //if (0 == g_strcmp0(S52_PL_getOBCL(obj), "pastrk")) {
    //    PRINTF("DEBUG: pastrk FOUND\n");
    //}
    //if (0 == g_strcmp0("M_COVR", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: M_COVR FOUND\n");
    //}
    //if (0 == g_strcmp0("m_covr", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: m_covr FOUND\n");
    //}
    //if (0 == g_strcmp0("sclbdy", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: sclbdy FOUND\n");
    //}
    //if (0 == g_strcmp0("clrlin", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: clrlin FOUND\n");
    //}
    //if (S52_GL_PICK == _crnt_GL_cycle) {
    //    S57_geo *geo = S52_PL_getGeo(obj);
    //    GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //    if (0==strcmp("2135161787", FIDNstr->str)) {
    //        PRINTF("%s\n", FIDNstr->str);
    //    }
    //}
    /*
    S57_geo *geo = S52_PL_getGeo(obj);
    //PRINTF("drawing geo ID: %i\n", S57_getGeoS57ID(geo));
    //if (2184==S57_getGeoS57ID(geo)) {
    //if (140 == S57_getGeoS57ID(geo)) {
    //if (103 == S57_getGeoS57ID(geo)) {  // Hawaii ISODNG
    if (567 == S57_getGeoS57ID(geo)) {
        PRINTF("found %i XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n", S57_getGeoS57ID(geo));
        S57_highlightON(geo);
    ////    return TRUE;
    }
    */
    //if (S52_GL_PICK == _crnt_GL_cycle) {
    //    if (0 == strcmp("PRDARE", S52_PL_getOBCL(obj))) {
    //        PRINTF("PRDARE found\n");
    //    }
    //}
    //------------------------------------------------------


    /* FIXME: check atomic for each obj
    // but _atomicAbort is local to S52.c!
    g_atomic_int_get(&_atomicAbort);
    if (TRUE == _atomicAbort) {
        PRINTF("abort drawing .. \n");
        _backtrace();
        g_atomic_int_set(&_atomicAbort, FALSE);
        return TRUE;
    }
    */

    if (S52_GL_PICK == _crnt_GL_cycle) {
        ++_cIdx.color.r;

        g_ptr_array_add(_objPick, obj);

        //S57_geo *geo = S52_PL_getGeo(obj);
        //PRINTF("DEBUG: %i - pick: %s:%i\n", _cIdx.color.r, S52_PL_getOBCL(obj), S57_getGeoS57ID(geo));
    }

    ++_nobj;

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {

        switch (cmdWrd) {
            /// text is parsed/rendered separetly now
            case S52_CMD_TXT_TX:
            case S52_CMD_TXT_TE: break;   // TE&TX

            case S52_CMD_SYM_PT: _renderSY(obj); _ncmd++; break;   // SY
            case S52_CMD_SIM_LN: _renderLS(obj); _ncmd++; break;   // LS
            case S52_CMD_COM_LN: _renderLC(obj); _ncmd++; break;   // LC
            case S52_CMD_ARE_CO: _renderAC(obj); _ncmd++; break;   // AC
            case S52_CMD_ARE_PA: _renderAP(obj); _ncmd++; break;   // AP

            // trap CS call that have not been resolve
            case S52_CMD_CND_SY: _traceCS(obj); break;   // CS
            // trap CS call that Override Priority
            case S52_CMD_OVR_PR: _traceOP(obj); break;

            case S52_CMD_NONE: break;

            default:
                PRINTF("ERROR: unknown command word: %i\n", cmdWrd);
                g_assert(0);
                break;
        }

        cmdWrd = S52_PL_getCmdNext(obj);
    }

    // Can cursor pick now use the journal in S52.c instead of the GPU?
    // NO, if extent is use then concave AREA and LINES can trigger false positive
    if (S52_GL_PICK == _crnt_GL_cycle) {
        if (2.0==S52_MP_get(S52_MAR_DISP_CRSR_PICK) || 3.0==S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
            // WARNING: some graphic card preffer RGB / BYTE .. YMMV
            // FIXME: optimisation: shader trick! .. put the pixels back in an array then back to main!!
            // FIXME: use _fb_format (rgb/rgba) and EGL rgb/rgba
            //glReadPixels(_vp[0], _vp[1], 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, &_pixelsRead);
            //glReadPixels(_vp[0], _vp[1], 8, 8, GL_RGB,  GL_UNSIGNED_BYTE, &_pixelsRead);
            //glReadPixels(_vp[0], _vp[1], 1, 1, GL_RGB,  GL_UNSIGNED_BYTE, &_pixelsRead);
            //glReadPixels(_vp[0], _vp[1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &_pixelsRead);
#ifdef S52_USE_GLSC2
            int bufSize = 1 * 1 * 4;
            _glReadnPixels(_vp.x, _vp.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bufSize, &_pixelsRead);
#else
            glReadPixels(_vp.x, _vp.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &_pixelsRead);
#endif
            _checkError("S52_GL_draw():glReadPixels()");

            if (_pixelsRead[0].color.r == _cIdx.color.r) {
                g_ptr_array_add(_objPick, obj);

                // debug
                PRINTF("DEBUG: pixel found (%i, %i): i=%i color=%X\n", _vp.x, _vp.y, 0, _cIdx.color.r);
            }
        }
    }

    return TRUE;
}

int        S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    if (S52_GL_PICK == _crnt_GL_cycle) {
        return FALSE;
    }

    //
    // FIXME: call _renderAC_NODATA_layer0() when drag - to erease line artefact
    //_renderAC_NODATA_layer0(); nop!
    // WRAP_S/T GL_REPEAT - nop!

    // set rotation (via _glMatrixSet())
    double northtmp = _view.north;
    _view.north = north;
    _glMatrixSet(VP_PRJ);

    glBindTexture(GL_TEXTURE_2D, _fb_pixels_id);

#ifdef S52_USE_GL2
     // turn ON 'sampler2d'
    glUniform1f(_uBlitOn, 1.0);

    GLfloat ppt[4*3 + 4*2] = {
        _pmin.u, _pmin.v, 0.0,   0.0 + scale_x + scale_z, 0.0 + scale_y + scale_z,
        _pmin.u, _pmax.v, 0.0,   0.0 + scale_x + scale_z, 1.0 + scale_y - scale_z,
        _pmax.u, _pmax.v, 0.0,   1.0 + scale_x - scale_z, 1.0 + scale_y - scale_z,
        _pmax.u, _pmin.v, 0.0,   1.0 + scale_x - scale_z, 0.0 + scale_y + scale_z
    };

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer    (_aUV,       2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    glFrontFace(GL_CW);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glFrontFace(GL_CCW);

    // turn OFF 'sampler2d'
    glUniform1f(_uBlitOn, 0.0);
    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

#else
    (void)scale_x;
    (void)scale_y;
    (void)scale_z;
#endif

    glBindTexture(GL_TEXTURE_2D, 0);

    _glMatrixDel(VP_PRJ);

    _view.north = northtmp;

    _checkError("S52_GL_drawBlit()");

    return TRUE;
}

int        S52_GL_drawStrWorld(double x, double y, char *str, unsigned int bsize, unsigned int weight)
// draw string in world coords
{
    S52_GL_cycle tmpCrntCycle = _crnt_GL_cycle;
    _crnt_GL_cycle = S52_GL_NONE;

    S52_Color *c = S52_PL_getColor("CHBLK");  // black
    _renderTXTAA(NULL, c, x, y, bsize, weight, str);

    _crnt_GL_cycle = tmpCrntCycle;

    return TRUE;
}

int        S52_GL_drawStrWin(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
// draw a string in window coords
{
    if (S52_GL_INIT == _crnt_GL_cycle) {
        PRINTF("WARNING: init GL first (draw)\n");
        return FALSE;
    }

    S52_Color *c = S52_PL_getColor(colorName);

#ifdef S52_USE_GL2
    S52_GL_win2prj(&pixels_x, &pixels_y);

    _glMatrixSet(VP_PRJ);
    //_renderTXTAA(NULL, c, pixels_x, pixels_y, bsize, 1, str);
    _renderTXTAA(NULL, c, pixels_x, pixels_y, bsize, bsize, str);  // FIXME: weight
    _glMatrixDel(VP_PRJ);

#else  // S52_USE_GL2
    glRasterPos2d(pixels_x, pixels_y);
    _checkError("S52_GL_drawStrWin()");

#ifdef S52_USE_FTGL
    if (NULL != _ftglFont[bsize]) {
        _setFragColor(c);
        ftglRenderFont(_ftglFont[bsize], str, FTGL_RENDER_ALL);
    }
#endif

#endif // S52_USE_GL2

    return TRUE;
}

static guchar   *_readFBPixels(void)
{
    if (S52_GL_PICK == _crnt_GL_cycle) {
        return NULL;
    }

    // debug
    //PRINTF("VP: %i, %i, %i, %i\n", _vp[0], _vp[1], _vp[2], _vp[3]);


#ifdef S52_USE_GL2
    glBindTexture(GL_TEXTURE_2D, _fb_pixels_id);

#ifdef S52_USE_GLSC2
    int bufSize =  _vp.w * _vp.h * 4;
    _glReadnPixels(_vp.x, _vp.y, _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, bufSize, _fb_pixels);

    _checkError("S52_GL_readFBPixels().. -0-");

    //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h);
    glTexStorage2D (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);

    _checkError("S52_GL_readFBPixels().. -1-");

#else  // S52_USE_GLSC2

#ifdef S52_USE_TEGRA2
    // Note: glCopyTexImage2D flip Y on TEGRA (Xoom)
    // Note: must be in sync with _fb_format

    // copy FB --> MEM
    // RGBA
    glReadPixels(_vp.x, _vp.y,  _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // copy MEM --> Texture
    // RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // modern way
    //glTexStorage2D (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h,);
    //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _vp.w, _vp.h,, 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);

#else   // S52_USE_TEGRA2
    // RGB
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, _vp.w, _vp.h, 0);
#endif  // S52_USE_TEGRA2
#endif  // S52_USE_GLSC2

    glBindTexture(GL_TEXTURE_2D, 0);

#else   // S52_USE_GL2
    // GL1
    glReadPixels(_vp.x, _vp.y,  _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
#endif  // S52_USE_GL2

    _checkError("S52_GL_readFBPixels().. -end-");

    return _fb_pixels;
}

static int       _drawFBPixels(void)
{
    if (S52_GL_PICK == _crnt_GL_cycle) {
        return FALSE;
    }

    // debug
    //PRINTF("VP: %i, %i, %i, %i\n", _vp[0], _vp[1], _vp[2], _vp[3]);

    // set rotation temporatly to 0.0 (via _glMatrixSet())
    double northtmp = _view.north;
    _view.north = 0.0;

    glBindTexture(GL_TEXTURE_2D, _fb_pixels_id);

#ifdef S52_USE_GL2
    _glMatrixSet(VP_PRJ);

    // turn ON 'sampler2d'
    glUniform1f(_uBlitOn, 1.0);

    GLfloat ppt[4*3 + 4*2] = {
        _pmin.u, _pmin.v, 0.0,   0.0, 0.0,
        _pmin.u, _pmax.v, 0.0,   0.0, 1.0,
        _pmax.u, _pmax.v, 0.0,   1.0, 1.0,
        _pmax.u, _pmin.v, 0.0,   1.0, 0.0
    };

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer    (_aUV,       2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    glFrontFace(GL_CW);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glFrontFace(GL_CCW);

    // turn OFF 'sampler2d'
    glUniform1f(_uBlitOn, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    _glMatrixDel(VP_PRJ);

#else   // S52_USE_GL2

    _glMatrixSet(VP_WIN);
    glRasterPos2i(0, 0);

    // parameter must be in sync with glReadPixels()
    // RGBA
    glDrawPixels(_vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // RGB
    //glDrawPixels(_vp[2], _vp[3], GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);

    _glMatrixDel(VP_WIN);

#endif  // S52_USE_GL2

    glBindTexture(GL_TEXTURE_2D, 0);

    _checkError("S52_GL_drawFBPixels() -fini-");

    _view.north = northtmp;

    return TRUE;
}

static double    _set_SCAMIN(void)
// used also by S52_GL_isSupp() and to set SCALEB10 or SCALEB11 (scale bar)
{
    _SCAMIN = (_scalex > _scaley) ? _scalex : _scaley;
    _SCAMIN *= 10000.0;

    return _SCAMIN;
}

static int       _doProjection(vp_t vp, double centerLat, double centerLon, double rangeDeg)
{
    pt3 NE = {0.0, 0.0, 0.0};  // Nort/East
    pt3 SW = {0.0, 0.0, 0.0};  // South/West

    NE.y = centerLat + rangeDeg;
    SW.y = centerLat - rangeDeg;
    NE.x = SW.x = centerLon;

    if (FALSE == S57_geo2prj3dv(1, (double*)&NE))
        return FALSE;
    if (FALSE == S57_geo2prj3dv(1, (double*)&SW))
        return FALSE;

    {
        // screen ratio
        double r = (double)vp.h / (double)vp.w;   // > 1 'h' dominant, < 1 'w' dominant
        //PRINTF("Viewport pixels (width x height): %i %i (r=%f)\n", w, h, r);
        double dy = NE.y - SW.y;
        //double dy = ABS(NE.y - SW.y);
        // assume h dominant (latitude), so range
        // fit on a landscape screen in latitude
        // FIXME: in portrait screen logitude is dominant
        // check if the ratio is the same on Xoom.
        double dx = dy / r;

        NE.x += (dx / 2.0);
        SW.x -= (dx / 2.0);
    }

    _pmin.u = SW.x;  // left
    _pmin.v = SW.y;  // bottom
    _pmax.u = NE.x;  // right
    _pmax.v = NE.y;  // top
    //PRINTF("PROJ MIN: %f %f  MAX: %f %f\n", _pmin.u, _pmin.v, _pmax.u, _pmax.v);

    // use to cull object base on there extent and view in deg
    _gmin.u = SW.x;
    _gmin.v = SW.y;
    _gmin   = S57_prj2geo(_gmin);

    _gmax.u = NE.x;
    _gmax.v = NE.y;
    _gmax   = S57_prj2geo(_gmax);

    // MPP - Meter Per Pixel
    _scalex = (_pmax.u - _pmin.u) / (double)vp.w;
    _scaley = (_pmax.v - _pmin.v) / (double)vp.h;

    _set_SCAMIN();

    return TRUE;
}

int        S52_GL_begin(S52_GL_cycle cycle)
{
    // GL sanity check before start of cycle
    _checkError("S52_GL_begin() -0-");

    if (S52_GL_NONE == cycle) {
        PRINTF("DEBUG: GL cycle out of sync\n");
        g_assert(0);
        return FALSE;
    }

    // Projection set in the DRAW cycle - hence need to be first
    // so that other calls depend on projection
    if (S52_GL_INIT==_crnt_GL_cycle && S52_GL_DRAW!=cycle) {
        // Note: land here when AIS start sending target before the main loop call Draw()
        PRINTF("WARNING: the very fist GL_begin must start a DRAW cycle\n");
        //g_assert(0);
        return FALSE;
    }
    _crnt_GL_cycle = cycle;

    S52_GL_init();

    // debug
    _drgare = 0;
    _depare = 0;
    _nAC    = 0;
    _nFrag  = 0;

    // stat
    _ntristrip = 0;
    _ntrisfan  = 0;
    _ntris     = 0;
    _nCall     = 0;
    _npoly     = 0;

    // test optimisation
    //_identity_MODELVIEW     = FALSE;
    //_identity_MODELVIEW_cnt = 0;

#ifdef S52_USE_COGL
    cogl_begin_gl();
#endif

#ifdef S52_USE_GL1
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    //glPushAttrib(GL_ENABLE_BIT);          // NOT in OpenGL ES SC
    //glDisable(GL_NORMALIZE);              // NOT in GLES2

    glAlphaFunc(GL_ALWAYS, 0);

    // LINE, GL_LINE_SMOOTH_HINT - NOT in GLES2
    glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
    //glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // POLY (not used)
    //glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);

    glShadeModel(GL_FLAT);                       // NOT in GLES2
    // draw both side
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // NOT in OpenGL ES SC
    glEnableClientState(GL_VERTEX_ARRAY);        // NOT in GLES2

    // draw to back buffer then swap
    glDrawBuffer(GL_BACK);
#endif

#ifdef S52_USE_GL2
    /*
     Returned by GetGraphicsResetStatus:

     GL_NO_ERROR               Indicates that the GL context has not been in a reset state since the last call.
     GL_GUILTY_CONTEXT_RESET   Indicates that a reset has been detected that is attributable to the current GL context.
     GL_INNOCENT_CONTEXT_RESET Indicates a reset has been detected that is not attributable to the current GL context.
     GL_UNKNOWN_CONTEXT_RESET  Indicates a detected graphics reset whose cause is unknown.

     Accepted by the <value> parameter of GetBooleanv, GetIntegerv, and GetFloatv:

        CONTEXT_ROBUST_ACCESS                           0x90F3
        RESET_NOTIFICATION_STRATEGY                     0x8256

     Returned by GetIntegerv and related simple queries when <value> is RESET_NOTIFICATION_STRATEGY :

        LOSE_CONTEXT_ON_RESET                           0x8252
        NO_RESET_NOTIFICATION                           0x8261

     Returned by GetError:

        CONTEXT_LOST                                    0x0507

    */

    /* FIXME: trigger reset to test this call
    if (NULL!=_glGetGraphicsResetStatus && TRUE==_GL_EXT_robustness) {
        GLenum ret = _glGetGraphicsResetStatus();
        if (GL_NO_ERROR != ret) {
            // FIXME: set S52_MAR_ERROR
            PRINTF("DEBUG: invalide GL context [0x%x].. need reset\n", ret);
            _checkError("S52_GL_begin() - GL2 glGetGraphicsResetStatus");
            g_assert(0);
            return FALSE;
        }
        _checkError("S52_GL_begin() - GL2 glGetGraphicsResetStatus");
    }
    */

    // FrontFaceDirection
    //glFrontFace(GL_CW);
    glFrontFace(GL_CCW);  // default

    //glCullFace(GL_BACK);  // default

    // FIXME: need to clock this more accuratly
    // Note: Mesa RadeonHD & Xoom (TEGRA2) Android 4.4.2 a bit slower with cull face!
    // also make little diff on Nexus with current timming method (not accurate)
    glEnable(GL_CULL_FACE);
    //glDisable(GL_CULL_FACE);

    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    //glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    //glDisable(GL_SAMPLE_COVERAGE);

    glActiveTexture(GL_TEXTURE0);  // default
    glUniform1i(_uSampler2d0, 0);  // default
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _checkError("S52_GL_begin() - GL2 EnableCap");
#endif  // S52_USE_GL2


    glEnable(GL_BLEND);

    // GL_FUNC_ADD, GL_FUNC_SUBTRACT, or GL_FUNC_REVERSE_SUBTRACT
    //glBlendEquation(GL_FUNC_ADD);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // transparency
    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    //glEnable(GL_MULTISAMPLE);
    //glDisable(GL_MULTISAMPLE_EXT);    //glSampleCoverage(1,  GL_FALSE);

    //glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    //glHint(GL_POINT_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);

    // depth OFF - not used
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindTexture(GL_TEXTURE_2D, 0);


    //_checkError("S52_GL_begin() -2-");


#if !defined(S52_USE_GLSC1)
    glDisable(GL_DITHER);                   // NOT in OpenGL ES SC
#endif

    // -------set matrix param ---------
    // do projection if draw() since the view is the same for all other mode
    if (S52_GL_DRAW == _crnt_GL_cycle) {
        // this will setup _pmin/_pmax, need a valide _vp
        glViewport(_vp.x, _vp.y, _vp.w, _vp.h);

        _doProjection(_vp, _view.cLat, _view.cLon, _view.rNM/60.0);
    }

    _glMatrixSet(VP_PRJ);

    // then create all S52 PLib symbol
    _createSymb();

    // ----- set cycle GL state --------------------------------------------------
    switch(_crnt_GL_cycle) {

    //-- picking ------------------------------------------------------------
    case S52_GL_PICK: {
        //if (S52_GL_PICK == _crnt_GL_cycle) {
        _cIdx.color.r = 0;
        _cIdx.color.g = 0;
        _cIdx.color.b = 0;
        _cIdx.color.a = 0;

        g_ptr_array_set_size(_objPick, 0);

        // make sure that _cIdx.color are not messed up
        //glDisable(GL_POINT_SMOOTH);
        //glDisable(GL_LINE_SMOOTH);     // NOT in GLES2
        //glDisable(GL_POLYGON_SMOOTH);  // NOT in "OpenGL ES SC"
        glDisable(GL_BLEND);

        // 2014OCT14 - optimisation no color ditherring
        glDisable(GL_DITHER);            // NOT in "OpenGL ES SC"

        //glDisable(GL_FOG);             // NOT in "OpenGL ES SC", NOT in GLES2
        //glDisable(GL_LIGHTING);        // NOT in GLES2
        //glDisable(GL_TEXTURE_1D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_TEXTURE_2D);      // fail on GLES2
        //glDisable(GL_TEXTURE_3D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_ALPHA_TEST);      // NOT in GLES2

        //glShadeModel(GL_FLAT);         // NOT in GLES2
    }
    break;

    //-- update background / layer 0-8 ------------------------------------------
    case S52_GL_DRAW: {
        //if (S52_GL_DRAW == _crnt_GL_cycle) {
        // CS DATCVR01: 2.2 - No data areas
        if (1.0 == S52_MP_get(S52_MAR_DISP_NODATA_LAYER)) {
            // fill display with 'NODTA' color
            _renderAC_NODATA_layer0();

            // fill with NODATA03 pattern
            _renderAP_NODATA_layer0();
        }

#ifdef S52_USE_RADAR
        if (0.0 == S52_MP_get(S52_MAR_DISP_NODATA_LAYER)) {
            // fill display with black color in RADAR mode
            _renderAC_NODATA_layer0();
        }
#endif  // S52_USE_RADAR
    }
    break;

    //-- update foreground / layer 9 / fast layer ------------------------------------------
    case S52_GL_LAST: {
        //if (S52_GL_LAST == _crnt_GL_cycle) {
        // user can draw on top of base
        // then call drawLast repeatdly
        if (TRUE == _fb_pixels_udp) {
#ifdef S52_USE_GL2
            _readFBPixels();
#else
            glDrawBuffer(GL_FRONT); // GL1
            _readFBPixels();
            glDrawBuffer(GL_BACK);  // GL1
#endif
            _fb_pixels_udp = FALSE;
        }

        // load FB that was filled with the previous draw() call
        _drawFBPixels();
    }
    break;

    case S52_GL_BLIT: break;  // bitblit cycle - blit FB of first pass

    case S52_GL_NONE:  // state between 2 cycles
    case S52_GL_INIT:  // state before first S52_GL_DRAW
    default: {
        PRINTF("DEBUG: invalide S52 GL cycle\n");
        g_assert(0);
        return FALSE;
    }
    }  // switch()

    _checkError("S52_GL_begin() - fini");

    return TRUE;
}

int        S52_GL_end(S52_GL_cycle cycle)
//
{
    if (cycle != _crnt_GL_cycle) {
        PRINTF("WARNING: S52_GL_mode out of sync\n");
        g_assert(0);
        return FALSE;
    }

    // read once, top object
    if (S52_GL_PICK==_crnt_GL_cycle && 1.0==S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
#ifdef S52_USE_GLSC2
        int bufSize = 1 * 1 * 4;
        _glReadnPixels(_vp.x, _vp.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bufSize, &_pixelsRead);
#else
        glReadPixels(_vp.x, _vp.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &_pixelsRead);
#endif
        _checkError("S52_GL_end():glReadPixels()");
    }

#ifdef S52_USE_GL1
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopAttrib();     // NOT in OpenGL ES SC
#endif

#ifdef S52_USE_COGL
    cogl_end_gl();
#endif

    _glMatrixDel(VP_PRJ);

    // texture of FB need update
    if (S52_GL_DRAW == _crnt_GL_cycle) {
        _fb_pixels_udp = TRUE;
    }

    /* debug - flush / finish and blit + swap
    if (S52_GL_DRAW == _crnt_GL_cycle) {
    //if (S52_GL_LAST == _crnt_GL_cycle) {
    //if (S52_GL_BLIT == _crnt_GL_cycle) {
        //glFlush();

        // EGL swap does a glFinish, if the call is make here
        // then the 80ish ms is spend here if not then sawp does the
        // Finish delay
        //glFinish();

        // EGL swap does a glFinish, if the call is make here
        // then the 80ish ms is spend here if not then sawp does the
        // Finish delay
        //glFinish();

        // debug Raster
        //    unsigned char pixels;
        //    S52_GL_drawRaster(&pixels);

        // debug - statistic
        PRINTF("TOTAL TESS STRIP = %i\n", _nStrips);
        PRINTF("TRIANGLE_STRIP   = %i\n", _ntristrip);
        PRINTF("TRIANGLE_FAN     = %i\n", _ntrisfan);
        //PRINTF("TRIANGLES ****   = %i, _nCall = %i\n", (int)_ntris/3, _nCall);
        PRINTF("TRIANGLES ****   = %i\n", _ntris);
        PRINTF("TOTAL POLY       = %i\n", _npoly);
    }
    //*/

#ifdef S52_USE_GL2
    //PRINTF("SKIP POIN_T glDrawArrays(): nFragment = %i\n", _nFrag);
    //PRINTF("SKIP identity = %i\n", _identity_MODELVIEW_cnt);
    //PRINTF("DEPARE = %i, TOTAL AC = %i\n", _depare, _nAC);
#endif

#ifdef S52_USE_GL2
#if !defined(S52_USE_GLSC2)
    //* test - try save glsl prog after drawing - FAIL on Intel
    // Note: this test to sae bin also in _GL2.i:_compShaderbin()
    static int silent = FALSE;
    if (FALSE == silent) {
        _saveShaderBin(_programObject);
        silent = TRUE;
    }
    //*/
#endif
#endif  // GL2

    _checkError("S52_GL_end() -fini-");

    _crnt_GL_cycle = S52_GL_NONE;

    return TRUE;
}

int        S52_GL_delDL(S52_obj *obj)
// delete the GL part of S57 geo object (Display List)
// S52_obj is use only by FREETYPE_GL
{
    S57_geo  *geoData = S52_PL_getGeo(obj);
    S57_prim *prim    = S57_getPrimGeo(geoData);

#ifdef S52_USE_GLSC1
    // SC can't delete a display list --no garbage collector
    return TRUE;
#endif

    // S57 have Display List / VBO
    if (NULL != prim) {
        guint     primNbr = 0;
        vertex_t *vert    = NULL;
        guint     vertNbr = 0;
        guint     vboID   = 0;

        if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
            return FALSE;

#ifdef S52_USE_OPENGL_VBO
#if !defined(S52_USE_GLSC2)
        // delete VBO when program terminated
        if (GL_TRUE == glIsBuffer(vboID)) {
            glDeleteBuffers(1, &vboID);
            vboID = 0;
            S57_setPrimDList(prim, vboID);
        }else {
            PRINTF("WARNING: ivalid PrimData VBO\n");
            g_assert(0);
            return FALSE;
        }

#ifdef S52_USE_FREETYPE_GL
        // delete text if any
        if (TRUE == S52_PL_hasText(obj)) {
            guint  len;
            double dummy;  // str W/H
            char   dum;    // just. H/V
            guint vboID = S52_PL_getFreetypeGL_VBO(obj, &len, &dummy, &dummy, &dum, &dum);
            if (GL_TRUE == glIsBuffer(vboID)) {
                glDeleteBuffers(1, &vboID);

                //len   = 0;
                //vboID = 0;
                //S52_PL_setFreetypeGL_VBO(obj, vboID, len);
                S52_PL_setFreetypeGL_VBO(obj, 0, 0, 0.0, 0.0);
            }
            /* debug
            else
            {
                PRINTF("WARNING: invalid FreetypeGL VBOID(%i)\n", vboID);
                //g_assert(0);
            }
            */
        }
#endif  // S52_USE_FREETYPE_GL
#endif  // !S52_USE_GLSC2

#else  // S52_USE_OPENGL_VBO
        // 'vboID' is in fact a DList here
        if (GL_TRUE == glIsList(vboID)) {
            glDeleteLists(vboID, 1);
            vboID = 0;
            S57_setPrimDList(prim, vboID);
        } else {
            //PRINTF("WARNING: invalid DL\n");
            //g_assert(0);
            return FALSE;
        }
#endif  // S52_USE_OPENGL_VBO
    }

    _checkError("S52_GL_delDL()");

    return TRUE;
}

int        S52_GL_delRaster(S52_GL_ras *raster, int texOnly)
{
    // texture
    if (FALSE == raster->isRADAR) {
        g_free(raster->texAlpha);
        raster->texAlpha = NULL;

        // src data
        if (TRUE != texOnly) {
            g_string_free(raster->fnameMerc, TRUE);
            raster->fnameMerc = NULL;
            g_free(raster->data);
            raster->data = NULL;
        }
    }

#if !defined(S52_USE_GLSC2)
    glDeleteTextures(1, &raster->texID);
#endif
    raster->texID = 0;

    _checkError("S52_GL_delRaster()");

    return TRUE;
}

static int       _contextValid(void)
// return TRUE if current GL context support S52 rendering
{
    if (TRUE == _ctxValidated)
        return TRUE;
    _ctxValidated = TRUE;

    GLint r=0,g=0,b=0,a=0,s=0,p=0;
    //GLboolean m=0;

    _checkError("_contextValid() -0-");

    //glGetBooleanv(GL_RGBA_MODE,    &m);    // not in "OpenGL ES SC" / GLES2
    glGetIntegerv(GL_RED_BITS,     &r);
    glGetIntegerv(GL_GREEN_BITS,   &g);
    glGetIntegerv(GL_BLUE_BITS,    &b);
    glGetIntegerv(GL_ALPHA_BITS,   &a);
    glGetIntegerv(GL_STENCIL_BITS, &s);
    glGetIntegerv(GL_DEPTH_BITS,   &p);
    PRINTF("BITS:r,g,b,a,stencil,depth: %d %d %d %d %d %d\n",r,g,b,a,s,p);
    // 16 bits:mode,r,g,b,a,s: 1 5 6 5 0 8
    // 24 bits:mode,r,g,b,a,s: 1 8 8 8 0 8

#ifdef S52_USE_GL1
    if (s <= 0)
        PRINTF("WARNING: no stencil buffer in cinfig for pattern on GL1\n");

    GLboolean d = FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &d);
    PRINTF("DEBUG: double buffer: %s\n", ((TRUE==d) ? "TRUE" : "FALSE"));
#endif



    // check EXT
    {
        const GLubyte *extensions = glGetString(GL_EXTENSIONS);
#ifdef S52_DEBUG
        {
            const GLubyte *vendor     = glGetString(GL_VENDOR);
            const GLubyte *renderer   = glGetString(GL_RENDERER);
            const GLubyte *version    = glGetString(GL_VERSION);
            const GLubyte *glslver    = glGetString(GL_SHADING_LANGUAGE_VERSION);
            PRINTF("Vendor:     %s\n", vendor);
            PRINTF("Renderer:   %s\n", renderer);
            PRINTF("Version:    %s\n", version);
            PRINTF("Shader:     %s\n", glslver);
            PRINTF("Extensions: %s\n", extensions);
        }
#endif

#ifdef S52_USE_GL2
        // FIXME: use macro SETGLEXTENSION(_GL_OES_texture_npot) ..

        // npot
        if (NULL != g_strrstr((const char *)extensions, "GL_OES_texture_npot")) {
            PRINTF("DEBUG: GL_OES_texture_npot OK\n");
            _GL_OES_texture_npot = TRUE;
        } else {
            PRINTF("DEBUG: GL_OES_texture_npot FAILED\n");
            _GL_OES_texture_npot = FALSE;
        }

        // marker
        if (NULL != g_strrstr((const char *)extensions, "GL_EXT_debug_marker")) {
            PRINTF("DEBUG: GL_EXT_debug_marker OK\n");
            _GL_EXT_debug_marker = TRUE;
        } else {
            PRINTF("DEBUG: GL_EXT_debug_marker FAILED\n");
            _GL_EXT_debug_marker = FALSE;
        }

        // point sprites
        if (NULL != g_strrstr((const char *)extensions, "GL_OES_point_sprite")) {
            PRINTF("DEBUG: GL_OES_point_sprite OK\n");
            _GL_OES_point_sprite = TRUE;
        } else {
            PRINTF("DEBUG: GL_OES_point_sprite FAILED\n");
            _GL_OES_point_sprite = FALSE;
        }

        // GL_ARB_get_program_binary
        if (NULL != g_strrstr((const char *)extensions, "GL_OES_get_program_binary")) {
        //if (NULL != g_strrstr((const char *)extensions, "get_program_binary")) {
            PRINTF("DEBUG: GL_OES_get_program_binary OK\n");
        } else {
            PRINTF("DEBUG: GL_OES_get_program_binary FAILED\n");
        }


        {   // GL_EXT_texture_storage
            const char *str = g_strrstr((const char *)extensions, "GL_EXT_texture_storage ");
            PRINTF("DEBUG: GL_EXT_texture_storage %s\n", (NULL==str)? "FAILED": "OK");
        }

        {   // GL_EXT_robustness - check for EXT/KHR/ARB
            const char *str = g_strrstr((const char *)extensions, "robustness ");
            PRINTF("DEBUG: GL_EXT/KHR/ARB_robustness %s\n", (NULL==str)? "FAILED": "OK");
            _GL_EXT_robustness = (NULL== str)? FALSE : TRUE;
        }
#endif  // S52_USE_GL2

    }

    _checkError("_contextValid()");

    return TRUE;
}

int        S52_GL_init(void)
// return TRUE on success
{
    if (!_doInit) {
        // debug
        //PRINTF("WARNING: S52 GL allready initialize!\n");
        return _doInit;
    }

    PRINTF("begin GL init..\n");

    /* juste checking
    if (sizeof(double) != sizeof(GLdouble)) {
        PRINTF("ERROR: sizeof(double) != sizeof(GLdouble)\n");
        g_assert(0);
        return FALSE;
    }
    */

    // GL sanity check before start of init
    _checkError("S52_GL_init() -0-");

    // check if this context (grahic card) can draw all of S52
    _contextValid();

    _initGLU();

#ifdef S52_USE_GLC
    _initGLC();
#endif

#ifdef S52_USE_FTGL
    _initFTGL();
#endif

#ifdef S52_USE_COGL
    _initCOGL();
#endif

#ifdef S52_USE_GL2
    _init_gl2();
#endif

    if (NULL == _objPick)
        _objPick = g_ptr_array_new();

    //_DEBUG = TRUE;

    // tmp buffer
    if (NULL == _tmpWorkBuffer)
        _tmpWorkBuffer = g_array_new(FALSE, FALSE, sizeof(vertex_t)*3);

#ifdef S52_USE_AFGLOW
#ifdef S52_USE_GL2
    if (NULL == _aftglwColorArr)
        _aftglwColorArr = g_array_new(FALSE, FALSE, sizeof(float));
#else
    if (NULL == _aftglwColorArr)
        _aftglwColorArr = g_array_new(FALSE, FALSE, sizeof(unsigned char));
#endif

    // init vbo for color
    if (0 == _vboIDaftglwColrID) {
        glGenBuffers(1, &_vboIDaftglwColrID);

        // glIsBuffer() faild but _vboIDaftglwColrID is valid
        if (0 == _vboIDaftglwColrID) {
            PRINTF("ERROR: glGenBuffers() fail\n");
            g_assert(0);
            return FALSE;
        }
    }

    // init VBO for vertex
    if (0 == _vboIDaftglwVertID) {
        glGenBuffers(1, &_vboIDaftglwVertID);

        // glIsBuffer() failed but _vboIDaftglwVertID is valid
        if (0 == _vboIDaftglwVertID) {
            PRINTF("ERROR: glGenBuffers() fail\n");
            g_assert(0);
            return FALSE;
        }
    }
#endif

    // ------------
    // setup mem buffer to save FB to
    glGenTextures(1, &_fb_pixels_id);
    glBindTexture  (GL_TEXTURE_2D, _fb_pixels_id);

#ifdef S52_USE_TEGRA2
    // Note: _fb_pixels must be in sync with _fb_format
    glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    // modern way
    //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h);
    //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#else
#ifdef S52_USE_GLSC2
    // modern way
    glTexStorage2D (GL_TEXTURE_2D, 0, GL_RGB, _vp.w, _vp.h);
#else
    // old way
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, _vp.w, _vp.h, 0);
    // modern way
    //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_RGB, _vp.w, _vp.h);
#endif
#endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture  (GL_TEXTURE_2D, 0);


    _doInit = FALSE;

    return TRUE;
}

int        S52_GL_done(void)
{
    if (TRUE == _doInit)
        return FALSE;

    _freeGLU();

    if (NULL != _fb_pixels) {
        g_free(_fb_pixels);
        _fb_pixels = NULL;
    }

    if (NULL != _objPick) {
        g_ptr_array_free(_objPick, TRUE);
        _objPick = NULL;
    }

    if (NULL != _tmpWorkBuffer) {
        g_array_free(_tmpWorkBuffer, TRUE);
        _tmpWorkBuffer = NULL;
    }

#ifdef S52_USE_AFGLOW
    if (NULL != _aftglwColorArr) {
        g_array_free(_aftglwColorArr, TRUE);
        _aftglwColorArr = NULL;
    }
#if !defined(S52_USE_GLSC2)
    if (0 != _vboIDaftglwColrID) {
        glDeleteBuffers(1, &_vboIDaftglwColrID);
        _vboIDaftglwColrID = 0;
    }
    if (0 != _vboIDaftglwVertID) {
        glDeleteBuffers(1, &_vboIDaftglwVertID);
        _vboIDaftglwVertID = 0;
    }
#endif

#ifdef S52_USE_GL2
    if (NULL != _tessWorkBuf_d) {
        g_array_free(_tessWorkBuf_d, TRUE);
        _tessWorkBuf_d = NULL;
    }
    if (NULL != _tessWorkBuf_f) {
        g_array_free(_tessWorkBuf_f, TRUE);
        _tessWorkBuf_f = NULL;
    }
#endif

    // done texture object
#if !defined(S52_USE_GLSC2)
    glDeleteTextures(1, &_nodata_mask_texID);
    glDeleteTextures(1, &_dottpa_mask_texID);
    glDeleteTextures(1, &_dashpa_mask_texID);
    glDeleteFramebuffers(1, &_fboID);
    glDeleteProgram(_programObject);
#endif

    _dashpa_mask_texID = 0;
    _dottpa_mask_texID = 0;
    _nodata_mask_texID = 0;
    _fboID             = 0;
    _programObject     = 0;


#ifdef S52_USE_FREETYPE_GL
    texture_font_delete(_freetype_gl_font[0]);
    texture_font_delete(_freetype_gl_font[1]);
    texture_font_delete(_freetype_gl_font[2]);
    texture_font_delete(_freetype_gl_font[3]);
    _freetype_gl_font[0] = NULL;
    _freetype_gl_font[1] = NULL;
    _freetype_gl_font[2] = NULL;
    _freetype_gl_font[3] = NULL;

    if (0 != _freetype_gl_textureID) {
#if !defined(S52_USE_GLSC2)
        glDeleteBuffers(1, &_freetype_gl_textureID);
#endif
        _freetype_gl_textureID = 0;
    }

    if (NULL != _freetype_gl_buffer) {
        g_array_free(_freetype_gl_buffer, TRUE);
        _freetype_gl_buffer = NULL;
    }
#endif  // S52_USE_FREETYPE_GL
#endif  // S52_USE_GL2



#ifdef S52_USE_FTGL
    if (NULL != _ftglFont[0])
        ftglDestroyFont(_ftglFont[0]);
    if (NULL != _ftglFont[1])
        ftglDestroyFont(_ftglFont[1]);
    if (NULL != _ftglFont[2])
        ftglDestroyFont(_ftglFont[2]);
    if (NULL != _ftglFont[3])
        ftglDestroyFont(_ftglFont[3]);

    _ftglFont[0] = NULL;
    _ftglFont[1] = NULL;
    _ftglFont[2] = NULL;
    _ftglFont[3] = NULL;
#endif

    _diskPrimTmp = S57_donePrim(_diskPrimTmp);

    if (NULL != _strPick) {
        g_string_free(_strPick, TRUE);
        _strPick = NULL;
    }

    _doInit = TRUE;

    return _doInit;
}

int        S52_GL_setDotPitch(int w, int h, int wmm, int hmm)
{
    _dotpitch_mm_x = (double)wmm / (double)w;
    _dotpitch_mm_y = (double)hmm / (double)h;

    PRINTF("DEBUG: DOTPITCH(mm): X = %f, Y = %f\n", _dotpitch_mm_x, _dotpitch_mm_y);

    S52_MP_set(S52_MAR_DOTPITCH_MM_X, _dotpitch_mm_x);
    S52_MP_set(S52_MAR_DOTPITCH_MM_Y, _dotpitch_mm_y);

    if (NULL != _fb_pixels) {
        PRINTF("DEBUG: _fb_pixels allready alloc\n");
        g_assert(0);
    }

    _fb_pixels_size = w * h * _fb_pixels_format;
    _fb_pixels      = g_new0(unsigned char, _fb_pixels_size);

    return TRUE;
}

int        S52_GL_setPRJView(double  s, double  w, double  n, double  e)
{
    _pmin.v = s;
    _pmin.u = w;
    _pmax.v = n;
    _pmax.u = e;

    return TRUE;
}

int        S52_GL_getPRJView(double *LLv, double *LLu, double *URv, double *URu)
{
    if (_doInit) {
        PRINTF("WARNING: S52 GL not initialize\n");
        return FALSE;
    }

    *LLu = _pmin.u;
    *LLv = _pmin.v;
    *URu = _pmax.u;
    *URv = _pmax.v;

    return TRUE;
}

int        S52_GL_setGEOView(double  s, double  w, double  n, double  e)
{
    _gmin.v = s;
    _gmin.u = w;
    _gmax.v = n;
    _gmax.u = e;

    return TRUE;
}

int        S52_GL_getGEOView(double *LLv, double *LLu, double *URv, double *URu)
{
    if (_doInit) {
        PRINTF("WARNING: S52 GL not initialize\n");
        return FALSE;
    }

    *LLu = _gmin.u;
    *LLv = _gmin.v;
    *URu = _gmax.u;
    *URv = _gmax.v;

    return TRUE;
}

int        S52_GL_setView(double centerLat, double centerLon, double rangeNM, double north)
{
    /*
    _centerLat = centerLat;
    _centerLon = centerLon;
    _rangeNM   = rangeNM;
    _north     = north;
    //*/

    //* update local var _view
    _view.cLat  = centerLat;
    _view.cLon  = centerLon;
    _view.rNM   = rangeNM;
    _view.north = north;
    //*/

    return TRUE;
}

int        S52_GL_getView(double *centerLat, double *centerLon, double *rangeNM, double *north)
{
    /*
    *centerLat = _centerLat;
    *centerLon = _centerLon;
    *rangeNM   = _rangeNM;
    *north     = _north;
    //*/

    //*
    *centerLat = _view.cLat;
    *centerLon = _view.cLon;
    *rangeNM   = _view.rNM;
    *north     = _view.north;
    //*/

    return TRUE;
}

int        S52_GL_setViewPort(int x, int y, int width, int height)
{
    // NOTE: width & height are in fact GLsizei, a pseudo unsigned int
    // it is a 'int32' that can't be negative

    _vp.x = x;
    _vp.y = y;
    _vp.w = width;
    _vp.h = height;

    guint sz = width * height * _fb_pixels_format;
    if (_fb_pixels_size < sz) {
        _fb_pixels_size = sz;
        //_fb_pixels      = g_renew(unsigned char, _fb_pixels, _fb_pixels_size);
        _fb_pixels      = g_renew(unsigned char, NULL, _fb_pixels_size);
        PRINTF("DEBUG: pixels buffer resized (%i)\n", _fb_pixels_size);
    }

    return TRUE;
}

int        S52_GL_getViewPort(int *x, int *y, int *width, int *height)
{
    //glGetIntegerv(GL_VIEWPORT, _vp);
    //_checkError("S52_GL_getViewPort()");

    *x      = _vp.x;
    *y      = _vp.y;
    *width  = _vp.w;
    *height = _vp.w;

    return TRUE;
}

int        S52_GL_setScissor(int x, int y, int width, int height)
// return TRUE;
// when w & h < 0, GL_INVALID_VALUE is generated
// so if either width or height is negative the turn of glDisable(GL_SCISSOR_TEST)
{
    //PRINTF("DEBUG: x/y/width/height: %i/%i/%i/%i\n", x, y, width, height);

    // NOTE: width & height are in fact GLsizei, a pseudo unsigned int
    // it is a 'int32' that can't be negative
    if (width<0 || height<0) {
        glDisable(GL_SCISSOR_TEST);
        return TRUE;
    }

    //
    // FIXME: extent box if chart rotation - will mess MIO & HO data limit!
    // FIX: skip scissor _north != 0, add doc
    if (0.0 != _view.north) {
        // FIXME: save S52_MAR_DISP_HODATA, reset when N = 0
        glDisable(GL_SCISSOR_TEST);
        return TRUE;
    }


    glEnable(GL_SCISSOR_TEST);

    /* +1 px to cover line witdh at edge
    if (x<=0 || y<=0) {
        if (0 < x)
            glScissor(x, 0, width+1,   height+1+y);
        if (0 < y)
            glScissor(0, y, width+1+x, height+1);
    } else
        glScissor(x-1, y-1, width+2, height+2);
    */

    glScissor(x-1, y-1, width+2, height+2);

    _checkError("S52_GL_setScisor().. -end-");

    return TRUE;
}

cchar     *S52_GL_getNameObjPick(void)

{
    if (S52_GL_NONE != _crnt_GL_cycle) {
        PRINTF("ERROR: inside a GL cycle\n");
        g_assert(0);
        return NULL;
    }

    if (0 == _objPick->len) {
        PRINTF("WARNING: no S57 object found\n");
        return NULL;
    }

    if (NULL == _strPick)
        _strPick = g_string_new("");

    const char *name = NULL;
    guint S57ID = 0;
    guint idx   = 0;

    if (1.0 == S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
        idx = _pixelsRead[0].color.r;

        // pick anything
        if (0 != idx)
            --idx;
    }

    //if (2.0 == S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
    if (2.0==S52_MP_get(S52_MAR_DISP_CRSR_PICK) || 3.0==S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {

        PRINTF("----------- PICK(%i) ---------------\n", _objPick->len);

        for (guint i=0; i<_objPick->len; ++i) {
            S52_obj *obj = (S52_obj*)g_ptr_array_index(_objPick, i);
            S57_geo *geo = S52_PL_getGeo(obj);

            name  = S57_getName(geo);
            //S57ID = S57_getGeoS57ID(geo);

            PRINTF("%i  : %s\n", i, name);
            PRINTF("LUP : %s\n", S52_PL_getCMDstr(obj));
            PRINTF("DPRI: %i\n", (int)S52_PL_getDPRI(obj));

            { // pull PLib exposition field: LXPO/PXPO/SXPO
                int nCmd = 0;
                S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);
                while (S52_CMD_NONE != cmdWrd) {
                    const char *cmdType = NULL;

                    switch (cmdWrd) {
                    case S52_CMD_SYM_PT: cmdType = "SXPO"; break;   // SY
                    case S52_CMD_COM_LN: cmdType = "LXPO"; break;   // LC
                    case S52_CMD_ARE_PA: cmdType = "PXPO"; break;   // AP

                    default: break;
                    }

                    //*
                    if (NULL != cmdType) {
                        //char  name[80];
                        const char *value = S52_PL_getCmdText(obj);
                        // debug
                        //PRINTF("%s%i: %s\n", cmdType, nCmd, value);

                        if (NULL !=  value) {
                            char name[80];
                            // insert in Att
                            SNPRINTF(name, 80, "%s%i", cmdType, nCmd);
                            S57_setAtt(geo, name, value);
                        }
                    }
                    //*/

                    cmdWrd = S52_PL_getCmdNext(obj);
                    ++nCmd;
                }
            }

            S57_dumpData(geo, FALSE);
            PRINTF("-----------\n");
        }

        // pick anything
        if (0 != _objPick->len)
            idx = _objPick->len-1;

    }

    // hightlight object at the top of the stack
    if (0==_objPick->len || _objPick->len<=idx) {
        return NULL;
    }

    S52_obj *objHighLight = (S52_obj *)g_ptr_array_index(_objPick, idx);
    S57_geo *geoHighLight = S52_PL_getGeo(objHighLight);
    S57_highlightON(geoHighLight);

    name  = S57_getName (geoHighLight);
    S57ID = S57_getGeoS57ID(geoHighLight);

    // Note: compile with S52_USE_C_AGGR_C_ASSO
#ifdef S52_USE_C_AGGR_C_ASSO
    if (3.0 == S52_MP_get(S52_MAR_DISP_CRSR_PICK)) {
        // debug
        GString *geo_refs = S57_getAttVal(geoHighLight, "_LNAM_REFS_GEO");
        if (NULL != geo_refs)
            PRINTF("DEBUG:geo:_LNAM_REFS_GEO = %s\n", geo_refs->str);

        // get relationship obj
        S57_geo *geoRel = S57_getRelationship(geoHighLight);
        if (NULL != geoRel) {
            GString *geoRelIDs   = NULL;
            GString *geoRel_refs = S57_getAttVal(geoRel, "_LNAM_REFS_GEO");
            if (NULL != geoRel_refs)
                PRINTF("DEBUG:geoRel:_LNAM_REFS_GEO = %s\n", geoRel_refs->str);

            // parse Refs
            gchar **splitRefs = g_strsplit_set(geoRel_refs->str, ",", 0);
            gchar **topRefs   = splitRefs;

            while (NULL != *splitRefs) {
                S57_geo *geoRelAssoc = NULL;

                sscanf(*splitRefs, "%p", (void**)&geoRelAssoc);
                S57_highlightON(geoRelAssoc);

                guint idAssoc = S57_getGeoS57ID(geoRelAssoc);

                if (NULL == geoRelIDs) {
                    geoRelIDs = g_string_new("");
                    g_string_printf(geoRelIDs, ":%i,%i", S57_getGeoS57ID(geoRel), idAssoc);
                } else {
                    g_string_append_printf(geoRelIDs, ",%i", idAssoc);
                }

                splitRefs++;
            }

            // if in a relation then append it to pick string
            g_string_printf(_strPick, "%s:%i%s", name, S57ID, geoRelIDs->str);

            g_string_free(geoRelIDs, TRUE);

            g_strfreev(topRefs);
        }
    }
#endif  // S52_USE_C_AGGR_C_ASSO

    g_string_printf(_strPick, "%s:%i", name, S57ID);

    return (const char *)_strPick->str;
}

#include "gdal.h"  // GDAL stuff to write .PNG
int        S52_GL_dumpS57IDPixels(const char *toFilename, S52_obj *obj, unsigned int width, unsigned int height)
// FIXME: width/height rounding error all over - fix: +0.5
// to test 2 PNG using Python Imaging Library (PIL):
// FIXME: move GDAL stuff to S52.c (and remove gdal.h)
{
    GDALDriverH  driver  = NULL;
    GDALDatasetH dataset = NULL;

    return_if_null(toFilename);

    GDALAllRegister();


    // if obj is NULL then dump the whole framebuffer
    guint x, y;
    if (NULL == obj) {
        x      = _vp.x;
        y      = _vp.y;
        width  = _vp.w;
        height = _vp.h;
    } else {
        if (width > _vp.w) {
            PRINTF("WARNING: dump width (%i) exceeded viewport width (%i)\n", width, _vp.w);
            return FALSE;
        }
        if (height> _vp.h) {
            PRINTF("WARNING: dump height (%i) exceeded viewport height (%i)\n", height, _vp.h);
            return FALSE;
        }

        // get extent to compute center in pixels coords
        double x1, y1, x2, y2;
        S57_geo  *geoData = S52_PL_getGeo(obj);
        S57_getExt(geoData, &x1, &y1, &x2, &y2);
        // position
        pt3 pt;
        pt.x = (x1+x2) / 2.0;  // lon center
        pt.y = (y1+y2) / 2.0;  // lat center
        pt.z = 0.0;
        if (FALSE == S57_geo2prj3dv(1, (double*)&pt))
            return FALSE;

        S52_GL_prj2win(&pt.x, &pt.y);

        // FIXME: what happen if the viewport change!!
        x = 0;
        y = 0;
        if ((pt.x - width/2 ) <      0) x = width/2;
        if ((x    + width   ) >  _vp.w) x = _vp.w - width;
        if ((pt.y - height/2) <      0) y = height/2;
        if ((y    + height  ) >  _vp.h) y = _vp.h - height;
    }

    driver = GDALGetDriverByName("GTiff");
    if (NULL == driver) {
        PRINTF("WARNING: fail to get GDAL driver\n");
        return FALSE;
    }

    /* debug
    char **papszMetadata = GDALGetMetadata(driver, NULL);
    int i = 0;
    while (NULL != papszMetadata[i]) {
        PRINTF("papszMetadata[%i] = %s\n", i, papszMetadata[i]);
        ++i;
    }
    //if( CSLFetchBoolean( papszMetadata, GDAL_DCAP_CREATE, FALSE ) )
    */

#ifdef S52_USE_ANDROID
    char *tmpTif = "/sdcard/s52droid/_tmp.tif";
#else
    char *tmpTif = "_tmp.tif";
#endif

    dataset = GDALCreate(driver, tmpTif, width, height, 3, GDT_Byte, NULL);
    if (NULL == dataset) {
        PRINTF("WARNING: fail to create GDAL data set\n");
        return FALSE;
    }
    GDALRasterBandH bandR = GDALGetRasterBand(dataset, 1);
    GDALRasterBandH bandG = GDALGetRasterBand(dataset, 2);
    GDALRasterBandH bandB = GDALGetRasterBand(dataset, 3);

    guint8 *pixels = g_new0(guint8, width * height * _fb_pixels_format);

    // get framebuffer pixels
#ifdef S52_USE_GL2
    // FIXME: arm adreno will break here
    // FIXME: must be in sync with _fb_format
    //glReadPixels(x, y, width, height, GL_RGB,  GL_UNSIGNED_BYTE, pixels);
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#ifdef S52_USE_GLSC2
    int bufSize =  _vp.w * _vp.h * 4;
    _glReadnPixels(_vp.x, _vp.y, _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, bufSize, _fb_pixels);
#else
    glReadPixels(_vp.x, _vp.y,  _vp.w, _vp.h, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
#endif  // S52_USE_GLSC2

#else
    glReadBuffer(GL_FRONT);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glReadBuffer(GL_BACK);
#endif

    _checkError("S52_GL_dumpS57IDPixels()");

    //FIXME: use something like glPixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    {   // flip vertically
        guint8 *flipbuf = g_new0(guint8, width * height * 3);
        for (guint i=0; i<height; ++i) {
            memcpy(flipbuf + ((height-1) - i) * width * 3,
                   pixels + (i * width * 3),
                   width * 3);
        }
        g_free(pixels);
        pixels = flipbuf;
    }

    // write to temp file
    GDALRasterIO(bandR, GF_Write, 0, 0, width, height, pixels+0, width, height, GDT_Byte, 3, 0 );
    GDALRasterIO(bandG, GF_Write, 0, 0, width, height, pixels+1, width, height, GDT_Byte, 3, 0 );
    GDALRasterIO(bandB, GF_Write, 0, 0, width, height, pixels+2, width, height, GDT_Byte, 3, 0 );

    GDALClose(dataset);
    g_free(pixels);

    GDALDatasetH dst_dataset;
    driver      = GDALGetDriverByName("PNG");
    dst_dataset = GDALCreateCopy(driver, toFilename, GDALOpen(tmpTif, GA_ReadOnly), FALSE, NULL, NULL, NULL);
    GDALClose(dst_dataset);

    return TRUE;
}

int        S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str)
{
    // FIXME: str not used yet (get font metric from a particular font system)
    (void)str;

    // scale offset
    //double uoffs  = ((PICA * *offset_x) / _dotpitch_mm_x) * _scalex;
    //double voffs  = ((PICA * *offset_y) / _dotpitch_mm_y) * _scaley;
    double uoffs  = ((PICA * *offset_x) / S52_MP_get(S52_MAR_DOTPITCH_MM_X)) * _scalex;
    double voffs  = ((PICA * *offset_y) / S52_MP_get(S52_MAR_DOTPITCH_MM_Y)) * _scaley;

    *offset_x = uoffs;
    *offset_y = voffs;

    return TRUE;
}

int        S52_GL_drawGraticule(void)
// FIXME: use S52_MAR_DISP_GRATICULE to get the user choice
{
    return_if_null(S57_getPrjStr());

    // delta lat  / 1852 = height in NM
    // delta long / 1852 = witdh  in NM
    double dlat = (_pmax.v - _pmin.v) / 3.0;
    double dlon = (_pmax.u - _pmin.u) / 3.0;

    //int remlat =  (int) _pmin.v % (int) dlat;
    //int remlon =  (int) _pmin.u % (int) dlon;

    char   str[80];
    S52_Color *black = S52_PL_getColor("CHBLK");
    _setFragColor(black);
    // FIXME: set black->lineW
    _glLineWidth(1.0);

    //_setBlend(TRUE);

    _glUniformMatrix4fv_uModelview();

    //
    // FIXME: small optimisation: collecte all segment to tmpWorkBuffer THEN call a draw
    //

    // ----  graticule S lat
    {
        double lat  = _pmin.v + dlat;
        double lon  = _pmin.u;
        pt3v ppt[2] = {{lon, lat, 0.0}, {_pmax.u, lat, 0.0}};
        projXY uv   = {lon, lat};

        uv = S57_prj2geo(uv);
        SNPRINTF(str, 80, "%07.4f deg %c", fabs(uv.v), (0.0<lat)?'N':'S');
        //_renderTXTAA(lon, lat, 1, 1, str);

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
    }

    // ---- graticule N lat
    {
        double lat  = _pmin.v + dlat + dlat;
        double lon  = _pmin.u;
        pt3v ppt[2] = {{lon, lat, 0.0}, {_pmax.u, lat, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SNPRINTF(str, 80, "%07.4f deg %c", fabs(uv.v), (0.0<lat)?'N':'S');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_renderTXTAA(lon, lat, 1, 1, str);
    }


    // ---- graticule W long
    {
        double lat  = _pmin.v;
        double lon  = _pmin.u + dlon;
        pt3v ppt[2] = {{lon, lat, 0.0}, {lon, _pmax.v, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SNPRINTF(str, 80, "%07.4f deg %c", fabs(uv.u), (0.0<lon)?'E':'W');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_renderTXTAA(lon, lat, 1, 1, str);
    }

    // ---- graticule E long
    {
        double lat  = _pmin.v;
        double lon  = _pmin.u + dlon + dlon;
        pt3v ppt[2] = {{lon, lat, 0.0}, {lon, _pmax.v, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SNPRINTF(str, 80, "%07.4f deg %c", fabs(uv.u), (0.0<lon)?'E':'W');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_renderTXTAA(lon, lat, 1, 1, str);
    }


    //printf("lat: %f, long: %f\n", lat, lon);
    //_setBlend(FALSE);

    _checkError("S52_GL_drawGraticule()");

    return TRUE;
}

int              _drawArc(S52_obj *objA, S52_obj *objB)
{
    S57_geo  *geoA          = S52_PL_getGeo(objA);
    S57_geo  *geoB          = S52_PL_getGeo(objB);
    GString  *wholinDiststr = S57_getAttVal(geoA, "_wholin_dist");
    double    wholinDist    = (NULL == wholinDiststr) ? 0.0 : S52_atof(wholinDiststr->str) * 1852.0;
    //int       CW            = TRUE;
    //int       revsweep      = FALSE;

    GLdouble *pptA = NULL;
    guint     nptA = 0;
    GLdouble *pptB = NULL;
    guint     nptB = 0;

    if (FALSE==S57_getGeoData(geoA, 0, &nptA, &pptA))
        return FALSE;
    if (FALSE==S57_getGeoData(geoB, 0, &nptB, &pptB))
        return FALSE;
    if (0.0 == wholinDist)
        return FALSE;

    double orientA = ATAN2TODEG(pptA);
    double orientB = ATAN2TODEG(pptB);

    if (orientA < 0.0) orientA += 360.0;
    if (orientB < 0.0) orientB += 360.0;

    // begining of curve - point of first prependical
    double x1 = pptA[3] - cos((90.0-orientA)*DEG_TO_RAD)*wholinDist;
    double y1 = pptA[4] - sin((90.0-orientA)*DEG_TO_RAD)*wholinDist;
    // end of first perpendicular
    double x2 = x1 + cos(((90.0-orientA)+90.0)*DEG_TO_RAD)*wholinDist;
    double y2 = y1 + sin(((90.0-orientA)+90.0)*DEG_TO_RAD)*wholinDist;


    // end of curve     - point of second perendicular
    double x3 = pptB[0] + cos((90.0-orientB)*DEG_TO_RAD)*wholinDist;
    double y3 = pptB[1] + sin((90.0-orientB)*DEG_TO_RAD)*wholinDist;

    // end of second perpendicular
    double x4 = x3 + cos(((90.0-orientB)+90.0)*DEG_TO_RAD)*wholinDist;
    double y4 = y3 + sin(((90.0-orientB)+90.0)*DEG_TO_RAD)*wholinDist;

    if ((orientB-orientA) < 0)
        orientB += 360.0;

    double sweep = orientB - orientA;

    if (sweep > 360.0)
        sweep -= 360.0;

    if (sweep > 180.0)
        sweep -= 360.0;

    // debug
    //PRINTF("SWEEP: %f\n", sweep);


    //
    // find intersection of perpendicular
    //
    // compute a1, b1, c1, where line joining points 1 and 2
    // is "a1 x  +  b1 y  +  c1  =  0".
    double a1 = y2 - y1;
    double b1 = x1 - x2;
    double c1 = x2 * y1 - x1 * y2;

    double a2 = y4 - y3;
    double b2 = x3 - x4;
    double c2 = x4 * y3 - x3 * y4;

    double denom = a1 * b2 - a2 * b1;

    // check if line intersec
    if (0 == denom)
        return FALSE;

    // compute intersection
    double xx   = (b1*c2 - b2*c1) / denom;
    double yy   = (a2*c1 - a1*c2) / denom;
    double dist = sqrt(pow(xx-x1, 2) + pow(yy-y1, 2));

    // compute LEGLIN symbol pixel size
    GLdouble symlen    = 0.0;
    char     dummy     = 0.0;
    S52_PL_getLCdata(objA, &symlen, &dummy);

    double symlen_pixl = symlen / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
    double symlen_wrld = symlen_pixl * _scalex;
    double symAngle    = atan2(symlen_wrld, dist) * RAD_TO_DEG;
    double nSym        = fabs(floor(sweep / symAngle));
    double crntAngle   = orientA + 90.0;

    if (sweep > 0.0)
        crntAngle += 180;

    crntAngle += (symAngle / 2.0);

    //
    // draw
    //
    //_setBlend(TRUE);

    S52_DList *DListData = S52_PL_getDListData(objA);
    S52_Color *color     = DListData->colors;
    _setFragColor(color);


    // draw arc
    if (2.0==S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) {
        //nSym  /= 2;
        for (int j=0; j<=nSym; ++j) {
            _glLoadIdentity(GL_MODELVIEW);

            _glTranslated(xx, yy, 0.0);

            _glRotated(90.0-crntAngle, 0.0, 0.0, 1.0);
            // FIXME: radius too long (maybe because of symb width!)
            _glTranslated(dist - (0.5*_scalex), 0.0, 0.0);
            _glScaled(1.0, -1.0, 1.0);
            _glRotated(-90.0, 0.0, 0.0, 1.0);    // why -90.0 and not +90.0 .. because of inverted axis (glScale!)

            _pushScaletoPixel(TRUE);

            _glCallList(DListData);

            _popScaletoPixel();

            // rotate
            if (0.0 < sweep)
                crntAngle += symAngle;
            else
                crntAngle -= symAngle;
        }
    }

    //_setBlend(FALSE);

    return TRUE;
}

#if 0
int        S52_GL_drawArc(S52_obj *objA, S52_obj *objB)
{
    return_if_null(objA);
    return_if_null(objB);

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(objA);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_COM_LN: _drawArc(objA, objB); break;

            default: break;
        }
        cmdWrd = S52_PL_getCmdNext(objA);
    }

    _checkError("S52_GL_drawArc()");

    return TRUE;
}
#endif  // 0

#if 0
int              _intersect(double x1, double y1, double x2, double y2,
                            double x3, double y3, double x4, double y4)
// TRUE if line segment intersect, else FALSE
// inspire from GEM III
{
    double Ax = x2-x1;
    double Bx = x3-x4;

    double Ay = y2-y1;
    double By = y3-y4;

    double Cx = x1-x3;
    double Cy = y1-y3;

    // alpha numerator
    double d  = By*Cx - Bx*Cy;
    // both denominator
    double f  = Ay*Bx - Ax*By;

    //é alpha tets
    if (f > 0) {
        if (d<0 || d>f)
            return FALSE;
    } else {
        if (d>0 || d<f)
            return FALSE;
    }

    // beta numerator
    double e = Ax*Cy - Ay*Cx;

    // beta tests
    if (f > 0) {
        if (e<0 || e>f)
            return FALSE;
    } else {
        if (e>0 || e<f)
            return FALSE;
    }

    return TRUE;
}
#endif

int        S52_GL_isHazard(int nxyz, double *xyz)
// TRUE if hazard found
{
    // Port
    //_intersect(x1, y1, x2, y2, x3, y3, x4, y4);
    // Starboard
    //_intersect(x1, y1, x2, y2, x3, y3, x4, y4);

#ifdef S52_USE_GL2
    _d2f(_tessWorkBuf_f, nxyz, xyz);
    memcpy(_hazardZone, _tessWorkBuf_f->data, sizeof(vertex_t) * nxyz * 3);
#else
    memcpy(_hazardZone, xyz, sizeof(vertex_t) * nxyz * 3);
#endif

    // highlight Hazard
    int found = FALSE;
    for (guint i=0; i<_objPick->len; ++i) {
        S52_obj  *obj = (S52_obj *)g_ptr_array_index(_objPick, i);
        S57_geo  *geo = S52_PL_getGeo(obj);

        GLdouble *ppt = NULL;
        guint     npt = 0;
        if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
            continue;

        for (guint j=0; j<npt; ++j) {
            if (TRUE == S57_isPtInside(nxyz, xyz, TRUE, ppt[j*3 + 0], ppt[j*3 + 1])) {
                S57_highlightON(geo);
                found = TRUE;
                break;
            }
        }
    }

    return found;
}


#ifdef S52_GL_TEST
//---------------------------
//
// MAIN SECTION
//
//---------------------------

int main(int argc, char** argv)
{
    return 1;
}
#endif  // S52_GL_TEST
