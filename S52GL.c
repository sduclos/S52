// S52GL.c: display S57 data using S52 symbology and OpenGL.
//
// Project:  OpENCview

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


// - S52GL.c : S52 OpenGL rendering front-end
//   - _GL1.i: GL1.x / GLSC1.0  - fixe-function pipeline
//   - _GL2.i: GL2.x / GLES2.x  - programmable/shader (GLSL)

#include "S52GL.h"

#include "S52MP.h"        // S52_MP_get/set()
#include "S52utils.h"     // PRINTF(), S52_atof(), S52_atoi(), S52_strlen()

#include <glib.h>
#include <glib/gstdio.h>  // g_file_test()

// compiled with -std=gnu99 instead of -std=c99 will define M_PI
#include <math.h>         // sin(), cos(), atan2(), pow(), sqrt(), floor(), INFINITY, M_PI


///////////////////////////////////////////////////////////////////
// state
static int          _doInit             = TRUE;    // initialize (but GL context --need main loop)
static int          _ctxValidated       = FALSE;   // validate GL context
static GPtrArray   *_objPick            = NULL;    // list of object picked
static GString     *_strPick            = NULL;    // hold temps val
static int          _doHighlight        = FALSE;   // TRUE then _objhighlight point to the object to hightlight
static S52_GL_cycle _crnt_GL_cycle      = S52_GL_NONE; // failsafe - keep cycle in sync between begin / end
static int          _identity_MODELVIEW = FALSE;   // TRUE then identity matrix for modelview is on GPU (optimisation for AC())
static int          _identity_MODELVIEW_cnt = 0;   // count saving
static GArray      *_tmpWorkBuffer      = NULL;    // tmp buffer

static double       _north     = 0.0;
static double       _rangeNM   = 0.0;
static double       _centerLat = 0.0;
static double       _centerLon = 0.0;

static double       _SCAMIN    = 1.0;  // screen scale
static double       _scalex    = 1.0;  // meter per pixel in X
static double       _scaley    = 1.0;  // meter per pixel in Y

// projected view
static projUV _pmin = { INFINITY,  INFINITY};
static projUV _pmax = {-INFINITY, -INFINITY};
// _pmin, _pmax convert to GEO for culling object with there extent is in deg
static projUV _gmin = { INFINITY,  INFINITY};
static projUV _gmax = {-INFINITY, -INFINITY};

// current ViewPort
//static GLuint _vp[4]; // x,y,width,height
static guint _vp[4]; // x,y,width,height

// GL_PROJECTION matrix
typedef enum _VP{
    VP_PRJ,         // projected coordinate
    VP_WIN,         // window coordinate
    VP_NUM          // number of coord. systems type
}VP;
#define Z_CLIP_PLANE 10000   // clipped beyon this plane

#define S52_MAX_FONT  4

// hold copy of FrameBuffer
static int            _fb_update      = TRUE;  // TRUE flag that the FB changed
static unsigned char *_fb_pixels      = NULL;
static unsigned int   _fb_pixels_size = 0;
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
static guint          _fb_texture_id  = 0;
#endif
#ifdef S52_USE_ADRENO
#define RGB           3
static int            _fb_format      = RGB;   // alpha blending done in shader
                                               // no need to read alpha from FB
#else
#define RGBA          4
static int            _fb_format      = RGBA;
//static int            _fb_format      = RGB ;  // NOTE: on TEGRA2 RGB (3) very slow
#endif


/////////////////////////////////////////////////////
//
// include the apropriate declaration/definition
//

// sanity checks
#if defined(S52_USE_GLES2) && !defined(S52_USE_GL2)
#define S52_USE_GL2  // this signal to load _GL2.i
#endif
#if defined(S52_USE_FREETYPE_GL) && (!defined(S52_USE_GL2) || !defined(S52_USE_GLES2))
#error "Need GL2 or GLES2 for Freetype GL"
#endif
#if defined(S52_USE_GL1) && defined(S52_USE_GL2)
#error "GL1 or GL2, not both"
#endif
#if !defined(S52_USE_GL1) && !defined(S52_USE_GL2)
#error "must define GL1 or GL2"
#endif


// GL1.x
#ifdef S52_USE_GL1
#include "_GL1.i"
#endif

// GL2.x, GLES2.x
#ifdef S52_USE_GL2
#include "_GL2.i"
#endif

// GL3.x, GLES3.x
#ifdef S52_USE_GL3
#include "_GL3.i"
#endif
/////////////////////////////////////////////////////


// debug - ATI
// glGetIntegerv(GL_ATI_meminfo, GLint *params);
//#define VBO_FREE_MEMORY_ATI                     0x87FB
//#define TEXTURE_FREE_MEMORY_ATI                 0x87FC
//#define RENDERBUFFER_FREE_MEMORY_ATI            0x87FD
/*
      param[0] - total memory free in the pool
      param[1] - largest available free block in the pool
      param[2] - total auxiliary memory free
      param[3] - largest auxiliary free block
*/

/*
///////////////////////////////////////////////////////////////////
//
// FONTS
//

#ifdef S52_USE_FTGL
#include <FTGL/ftgl.h>
static FTGLfont *_ftglFont[S52_MAX_FONT];
#endif // S52_USE_FTGL

#ifdef S52_USE_GLC
#include <GL/glc.h>
static GLint _GLCctx;
#endif  // S52_USE_GLC

#ifdef S52_USE_COGL
#include "cogl-pango/cogl-pango.h"
static PangoFontDescription *_PangoFontDesc  = NULL;
static PangoFontMap         *_PangoFontMap   = NULL;
static PangoContext         *_PangoCtx       = NULL;
static PangoLayout          *_PangoLayout    = NULL;
#endif  // S52_USE_COGL
*/

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
//static int     _debug = 0;
// debug
//static int _DEBUG = FALSE;
static guint _S57ID = 0;


// tessalated area stat
static guint   _ntris     = 0;     // area GL_TRIANGLES      count
static guint   _ntristrip = 0;     // area GL_TRIANGLE_STRIP count
static guint   _ntrisfan  = 0;     // area GL_TRIANGLE_FAN   count
static guint   _nCall     = 0;
static guint   _npoly     = 0;     // total polys


//////////////////////////////////////////////////////
//
// tessallation
// mingw specific, with gcc APIENTRY expand to nothing
//#ifdef _MINGW
//#define _CALLBACK __attribute__ ((__stdcall__))
//#define _CALLBACK
//#else
#define _CALLBACK
//#endif

#define void_cb_t GLvoid _CALLBACK

typedef void (_CALLBACK *f)    ();
typedef void (_CALLBACK *fint) (GLint);
typedef void (_CALLBACK *f2)   (GLint, void*);
typedef void (_CALLBACK *fp)   (void*);
typedef void (_CALLBACK *fpp)  (void*, void*);

static GLUtriangulatorObj *_tobj = NULL;
static GPtrArray          *_tmpV = NULL;  // place holder during tesssalation (GLUtriangulatorObj combineCallback)

// centroid
static GLUtriangulatorObj *_tcen       = NULL;     // GLU CSG
static GArray             *_vertexs    = NULL;
static GArray             *_nvertex    = NULL;     // list of nbr of vertex per poly in _vertexs
static GArray             *_centroids  = NULL;     // centroids of poly's in _vertexs

static GLUtriangulatorObj *_tcin       = NULL;
static GLboolean           _startEdge  = GL_TRUE;  // start inside edge --for heuristic of centroid in poly
static int                 _inSeg      = FALSE;    // next vertex will complete an edge

// display list / VBO
static   int  _symbCreated = FALSE;  // TRUE if PLib symb created (DList/VBO)

// same thing as in proj_api.h
#if !defined(S52_USE_PROJ)
typedef struct { double u, v; } projUV;
#define projXY projUV
#define RAD_TO_DEG    57.29577951308232
#define DEG_TO_RAD     0.0174532925199432958
#endif

#define ATAN2TODEG(xyz)   (90.0 - atan2(xyz[4]-xyz[1], xyz[3]-xyz[0]) * RAD_TO_DEG)

#define PICA   0.351

// NOTE: S52 pixels for symb are 0.3 mm
// this is the real dotpitch of the device
// as computed at init() time
static double _dotpitch_mm_x      = 0.3;  // will be overwright at init()
static double _dotpitch_mm_y      = 0.3;  // will be overwright at init()

#define MM2INCH 25.4

//#define SHIPS_OUTLINE_MM    10.0   // 10 mm
#define SHIPS_OUTLINE_MM     6.0   // 6 mm

// symbol twice as big (see _pushScaletoPixel())
#define STRETCH_SYM_FAC 2.0

typedef struct  pt3  { double   x,y,z; } pt3;
typedef struct  pt3v { vertex_t x,y,z; } pt3v;

// for centroid inside poly heuristic
static double _dcin;
static pt3    _pcin;

#define NM_METER 1852.0

// debug
static int _GL_BEGIN = FALSE;
#define CHECK_GL_BEGIN if (FALSE == _GL_BEGIN) {                     \
                           PRINTF("ERROR: not inside glBegin()\n"); \
                           g_assert(0);                             \
                       }

#define CHECK_GL_END   if (TRUE == _GL_BEGIN) {                         \
                           PRINTF("ERROR: already inside glBegin()\n"); \
                           g_assert(0);                                 \
                       }

#ifdef S52_USE_AFGLOW
// experimental: synthetic after glow
static GArray  *_aftglwColorArr    = NULL;
static GLuint   _vboIDaftglwVertID = 0;
static GLuint   _vboIDaftglwColrID = 0;
#endif


//-----------------------------------
//
// GLU: TESS, QUADRIC, ..
//
//-----------------------------------

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

        //PRINTF("from %s: 0x%4x:%s (%s)\n", msg, err, gluErrorString(err), name);
        PRINTF("from %s: 0x%x (%s)\n", msg, err, name);
        //g_assert(0);
    }
#endif
}

// FIXME: use GDestroyNotify() instead
static int       _g_ptr_array_clear(GPtrArray *arr)
// free vertex malloc'd during tess, keep array memory allocated
{
    for (guint i=0; i<arr->len; ++i)
        g_free(g_ptr_array_index(arr, i));
    g_ptr_array_set_size(arr, 0);

    return TRUE;
}

static int       _findCentInside  (guint npt, pt3 *v)
// return TRUE and centroid else FALSE
{
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

        if (TRUE == S57_isPtInside(npt, (double*)v, pt.x, pt.y, FALSE)) {
            g_array_append_val(_centroids, pt);

            return TRUE;
        }

        // use heuristique to find centroid
        if (1.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS)) {
            _findCentInside(npt, v);
            PRINTF("point is outside polygone\n");

            return TRUE;
        }

        return TRUE;
    }

    PRINTF("WARNING: no area (0.0)\n");

    return FALSE;
}

static int       _getCentroidClose(guint npt, double *ppt)
// Close Poly - return TRUE and centroid else FALSE
{
    GLdouble ai;
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
    }

    // compute area
    for (guint i=0; i<npt-1; ++i) {
        ai    =  (p[0]-offx) * (p[4]-offy) - (p[3]-offx) * (p[1]-offy);
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

        if (TRUE == S57_isPtInside(npt, ppt, pt.x, pt.y, TRUE)) {
            g_array_append_val(_centroids, pt);
            //PRINTF("point is inside polygone\n");

            return TRUE;
        }

        // use heuristique to find centroid
        if (1.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS)) {
            _findCentInside(npt, (pt3*)ppt);

            // debug
            //PRINTF("point is outside polygone, heuristique used to find a pt inside\n");

            return TRUE;
        }

    }

    return FALSE;
}


static int       _getMaxEdge(pt3 *p)
{
    double x = p[0].x - p[1].x;
    double y = p[0].y - p[1].y;
    //double d =  sqrt(pow(x, 2) + pow(y, 2));
    double d =  sqrt(x*x + y*y);

    if (_dcin < d) {
        _dcin = d;
        _pcin.x = (p[1].x + p[0].x) / 2.0;
        _pcin.y = (p[1].y + p[0].y) / 2.0;
    }

    return TRUE;
}

static void_cb_t _tessError(GLenum err)
{
    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";

    PRINTF("%s (%d)\n", str, err);

    //g_assert(0);
}

static void_cb_t _quadricError(GLenum err)
{

    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";

    PRINTF("QUADRIC ERROR:%s (%d) (%0x)\n", str, err, err);

    // FIXME
    //g_assert(0);
}

static void_cb_t _edgeFlag(GLboolean flag)
{
    _startEdge = (GL_FALSE == flag)? GL_TRUE : GL_FALSE;
}


static void_cb_t _combineCallback(GLdouble   coords[3],
                                  GLdouble  *vertex_data[4],
                                  GLfloat    weight[4],
                                  GLdouble **dataOut )
{
    // debug
    //PRINTF("%f %f\n", coords[0], coords[1]);

    // vertex_data not used
    (void) vertex_data;
    // weight      not used
    (void) weight;

    // optimisation: alloc once --keep pos. of current index
    pt3 *p = g_new(pt3, 1);
    p->x   = coords[0];
    p->y   = coords[1];
    p->z   = coords[2];
    *dataOut = (GLdouble*)p;

    // debug
    g_ptr_array_add(_tmpV, (gpointer) p);
    //g_ptr_array_add(tmpV, (GLdouble*) p);

}

#if 0
static void_cb_t _combineCallbackCen(void)
// debug
{
    g_assert(0);
}
#endif

static void_cb_t _glBegin(GLenum mode, S57_prim *prim)
{
    // Note: mode=10  is _QUADRIC_TRANSLATE, defined bellow as 0x000A
    S57_begPrim(prim, mode);
}

static void_cb_t _beginCen(GLenum data)
{
    // avoid "warning: unused parameter"
    (void) data;

    // debug  printf
    //PRINTF("%i\n", data);
}

static void_cb_t _beginCin(GLenum data)
{
    // avoid "warning: unused parameter"
    (void) data;

    // debug  printf
    //PRINTF("%i\n", data);
}

static void_cb_t _glEnd(S57_prim *prim)
{
    S57_endPrim(prim);

    return;
}

static void_cb_t _endCen(void)
{
    // debug
    //PRINTF("finish a poly\n");

    if (_vertexs->len > 0)
        g_array_append_val(_nvertex, _vertexs->len);
}

static void_cb_t _endCin(void)
{
    // debug
}

static void_cb_t _vertex3d(GLvoid *data, S57_prim *prim)
// double - use by the tesselator
{
    // cast to float after tess (double)
    double  *dptr = (double*)data;
    vertex_t d[3] = {dptr[0], dptr[1], dptr[2]};

    S57_addPrimVertex(prim, d);

    return;
}

static void_cb_t _vertex3f(GLvoid *data, S57_prim *prim)
// float - use by symb from _parseHPGL() (so vertex are from the PLib and allready in float)
// store float vertex of primitive other than AREA
{
    S57_addPrimVertex(prim, (vertex_t*)data);
}

static void_cb_t _vertexCen(GLvoid *data)
{
    pt3 *p = (pt3*) data;

    //PRINTF("%f %f\n", p->x, p->y);

    g_array_append_val(_vertexs, *p);
}

static void_cb_t _vertexCin(GLvoid *data)
{
    pt3 *p = (pt3*) data;
    static pt3 pt[2];

    if (_inSeg) {
        pt[1] = *p;
        _getMaxEdge(&pt[0]);
        pt[0] = pt[1];
    } else
        pt[0] = *p;

    _inSeg = (_startEdge)? TRUE : FALSE;
}

#if 0
static void      _dumpATImemInfo(GLenum glenum)
{
    GLint params[4] = {0,0,0,0};
    glGetIntegerv(glenum, params);

    PRINTF("---------------ATI mem info: %x -----------------\n", glenum);
    PRINTF("%i Kbyte:total memory free in the pool\n",            params[0]);
    PRINTF("%i Kbyte:largest available free block in the pool\n", params[1]);
    PRINTF("%i Kbyte:total auxiliary memory free\n",              params[2]);
    PRINTF("%i Kbyte:largest auxiliary free block\n",             params[3]);
}
#endif

////////////////////////////////////////////////////
//
// Quadric (by hand)
//

// Make it not a power of two to avoid cache thrashing on the chip
#ifdef S52_USE_OPENGL_VBO

#define CACHE_SIZE    240

#undef  PI
#define PI            3.14159265358979323846f

#define _QUADRIC_BEGIN_DATA   0x0001  // _glBegin()
#define _QUADRIC_END_DATA     0x0002  // _glEnd()
#define _QUADRIC_VERTEX_DATA  0x0003  // _vertex3d()
#define _QUADRIC_ERROR        0x0004  // _quadricError()

// valid GLenum for glDrawArrays mode is 0-9
// This is an attempt to signal VBO drawing func
// to glTranslate() (eg to put a '!' inside a circle)
#define _TRANSLATE    0x000A

typedef struct _GLUquadricObj {
    GLint style;
    f2    cb_begin;
    fp    cb_end;
    fpp   cb_vertex;
    fint  cb_error;
} _GLUquadricObj;

static _GLUquadricObj *_qobj = NULL;
#else
static  GLUquadricObj *_qobj = NULL;
#endif

static S57_prim       *_diskPrimTmp = NULL;

#ifdef S52_USE_OPENGL_VBO
static _GLUquadricObj *_gluNewQuadric(void)
{
    static _GLUquadricObj qobj;

    return &qobj;
}

static int       _gluQuadricCallback(_GLUquadricObj* qobj, GLenum which, f fn)
{
    switch (which) {
      case _QUADRIC_ERROR:
          qobj->cb_error  = (fint)fn;
           break;
      case _QUADRIC_BEGIN_DATA:
           qobj->cb_begin  = (f2)fn;
           break;
      case _QUADRIC_END_DATA:
           qobj->cb_end    = (fp)fn;
           break;
      case _QUADRIC_VERTEX_DATA:
           qobj->cb_vertex = (fpp)fn;
           break;
      default:
          PRINTF("gluQuadricError(qobj, GLU_INVALID_ENUM)\n");
          g_assert(0);
          return FALSE;
    }

    return TRUE;
}

static int       _gluDeleteQuadric(_GLUquadricObj* qobj)
{
    (void)qobj;

    //g_assert(0);

    return TRUE;
}

static int       _gluPartialDisk(_GLUquadricObj* qobj,
                                 GLfloat innerRadius, GLfloat outerRadius,
                                 GLint slices, GLint loops, GLfloat startAngle, GLfloat sweepAngle)
{
    GLdouble sinCache[CACHE_SIZE];
    GLdouble cosCache[CACHE_SIZE];
    GLdouble angle;
    GLdouble vertex[3];

    if (slices < 2) slices = 2;

    if ((slices<2) || (loops<1) || (outerRadius<=0.0) || (innerRadius<0.0) || (innerRadius>outerRadius)) {
        //gluQuadricError(qobj, GLU_INVALID_VALUE);
        if ((NULL!=qobj) && (NULL!=qobj->cb_error)) {
            qobj->cb_error(GLU_INVALID_VALUE);
        }

        return FALSE;
    }

    if (slices >= CACHE_SIZE) slices = CACHE_SIZE - 1;

    if (sweepAngle < -360.0) sweepAngle = -360.0;
    if (sweepAngle >  360.0) sweepAngle =  360.0;

    if (sweepAngle <    0.0) {
        startAngle += sweepAngle;
        sweepAngle -= sweepAngle;
    }

    //if (sweepAngle == 360.0) slices2 = slices;
    //slices2 = slices + 1;

    GLdouble angleOffset = startAngle/180.0f*PI;
    for (int i=0; i<=slices; i++) {
        angle = angleOffset+((PI*sweepAngle)/180.0f)*i/slices;

        sinCache[i] = sin(angle);
        cosCache[i] = cos(angle);
    }

    if (GLU_FILL == qobj->style)
        qobj->cb_begin(GL_TRIANGLE_STRIP, _diskPrimTmp);
    else
        qobj->cb_begin(GL_LINE_STRIP, _diskPrimTmp);

    for (int j=0; j<loops; j++) {
        float deltaRadius = outerRadius - innerRadius;
        float radiusLow   = outerRadius - deltaRadius * ((GLfloat)(j+0)/loops);
        float radiusHigh  = outerRadius - deltaRadius * ((GLfloat)(j+1)/loops);

        for (int i=0; i<=slices; i++) {
            if (GLU_FILL == qobj->style) {
                vertex[0] = radiusLow * sinCache[i];
                vertex[1] = radiusLow * cosCache[i];
                vertex[2] = 0.0;
                qobj->cb_vertex(vertex, _diskPrimTmp);
                vertex[0] = radiusHigh * sinCache[i];
                vertex[1] = radiusHigh * cosCache[i];
                vertex[2] = 0.0;
                qobj->cb_vertex(vertex, _diskPrimTmp);
            } else {
                vertex[0] = outerRadius * sinCache[i];
                vertex[1] = outerRadius * cosCache[i];
                vertex[2] = 0.0;
                qobj->cb_vertex(vertex, _diskPrimTmp);
            }
        }
    }

    qobj->cb_end(_diskPrimTmp);

    return TRUE;
}

static int       _gluDisk(_GLUquadricObj* qobj, GLfloat innerRadius,
                          GLfloat outerRadius, GLint slices, GLint loops)
{
    _gluPartialDisk(qobj, innerRadius, outerRadius, slices, loops, 0.0, 360.0);

    return TRUE;
}

static int       _gluQuadricDrawStyle(_GLUquadricObj* qobj, GLint style)
{
    qobj->style = style;

    //g_assert(0);

    return TRUE;
}
#endif  // S52_USE_OPENGL_VBO


static GLint     _initGLU(void)
// initialize various GLU object
//
{

    ////////////////////////////////////////////////////////////////
    //
    // init tess stuff
    //
    {
        // hold vertex comming from GLU_TESS_COMBINE callback
        _tmpV = g_ptr_array_new();

        _tobj = gluNewTess();
        if (NULL == _tobj) {
            PRINTF("ERROR: gluNewTess() failed\n");
            return FALSE;
        }

        gluTessCallback(_tobj, GLU_TESS_BEGIN_DATA, (f)_glBegin);
        gluTessCallback(_tobj, GLU_TESS_END_DATA,   (f)_glEnd);
        gluTessCallback(_tobj, GLU_TESS_ERROR,      (f)_tessError);
        gluTessCallback(_tobj, GLU_TESS_VERTEX_DATA,(f)_vertex3d);
        gluTessCallback(_tobj, GLU_TESS_COMBINE,    (f)_combineCallback);

        // NOTE: _*NOT*_ NULL to trigger GL_TRIANGLES tessallation
        gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) _edgeFlag);
        //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) NULL);


        // no GL_LINE_LOOP
        gluTessProperty(_tobj, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);

        // ODD needed for symb. ISODGR01 + glDisable(GL_CULL_FACE);
        gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
        // default
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NEGATIVE);
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ABS_GEQ_TWO);

        //gluTessProperty(_tobj, GLU_TESS_TOLERANCE, 0.00001);
        //gluTessProperty(_tobj, GLU_TESS_TOLERANCE, 0.1);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tobj, 0.0, 0.0, 1.0);
        //gluTessNormal(_tobj, 0.0, 0.0, -1.0);  // OK for symb 5mm x 5mm square - fail all else
        //gluTessNormal(_tobj, 0.0, 0.0, 0.0);    // default

        // accumulate triangles strips
        //_vStrips = g_array_new(FALSE, FALSE, sizeof(vertex_t)*3);
        //_vFans   = g_array_new(FALSE, FALSE, sizeof(vertex_t)*3);
    }


    ///////////////////////////////////////////////////////////////
    //
    // centroid (CSG)
    //
    {
        _tcen = gluNewTess();
        if (NULL == _tcen) {
            PRINTF("ERROR: gluNewTess() failed\n");
            return FALSE;
        }
        _centroids = g_array_new(FALSE, FALSE, sizeof(double)*3);
        _vertexs   = g_array_new(FALSE, FALSE, sizeof(double)*3);
        _nvertex   = g_array_new(FALSE, FALSE, sizeof(int));

        //gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);
        //gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NEGATIVE);
        gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ABS_GEQ_TWO);
        gluTessProperty(_tcen, GLU_TESS_BOUNDARY_ONLY, GLU_TRUE);
        gluTessProperty(_tcen, GLU_TESS_TOLERANCE, 0.000001);

        gluTessCallback(_tcen, GLU_TESS_BEGIN,  (f)_beginCen);
        gluTessCallback(_tcen, GLU_TESS_END,    (f)_endCen);
        gluTessCallback(_tcen, GLU_TESS_VERTEX, (f)_vertexCen);
        gluTessCallback(_tcen, GLU_TESS_ERROR,  (f)_tessError);
        gluTessCallback(_tcen, GLU_TESS_COMBINE,(f)_combineCallback);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tcen, 0.0, 0.0, 1.0);

        //-----------------------------------------------------------

        _tcin = gluNewTess();
        if (NULL == _tcin) {
            PRINTF("ERROR: gluNewTess() failed\n");
            return FALSE;
        }

        //gluTessProperty(_tcin, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
        gluTessProperty(_tcin, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);
        gluTessProperty(_tcin, GLU_TESS_TOLERANCE, 0.000001);

        gluTessCallback(_tcin, GLU_TESS_BEGIN,     (f)_beginCin);
        gluTessCallback(_tcin, GLU_TESS_END,       (f)_endCin);
        gluTessCallback(_tcin, GLU_TESS_VERTEX,    (f)_vertexCin);
        gluTessCallback(_tcin, GLU_TESS_ERROR,     (f)_tessError);
        gluTessCallback(_tcin, GLU_TESS_COMBINE,   (f)_combineCallback);
        gluTessCallback(_tcin, GLU_TESS_EDGE_FLAG, (f)_edgeFlag);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tcin, 0.0, 0.0, 1.0);

    }

    ////////////////////////////////////////////////////////////////
    //
    // init quadric stuff
    //

    {
#ifdef S52_USE_OPENGL_VBO

        _qobj = _gluNewQuadric();
        _gluQuadricCallback(_qobj, _QUADRIC_BEGIN_DATA, (f)_glBegin);
        _gluQuadricCallback(_qobj, _QUADRIC_END_DATA,   (f)_glEnd);
        _gluQuadricCallback(_qobj, _QUADRIC_VERTEX_DATA,(f)_vertex3d);
        _gluQuadricCallback(_qobj, _QUADRIC_ERROR,      (f)_quadricError);
#else
        _qobj = gluNewQuadric();
        gluQuadricCallback (_qobj, GLU_ERROR,           (f)_quadricError);
#endif
        /*
         _tessError(0);

         str = gluGetString(GLU_VERSION);
         PRINTF("GLU version:%s\n",str);
         */
    }

    return TRUE;
}

static GLint     _freeGLU(void)
{

    //tess
    if (_tmpV) g_ptr_array_free(_tmpV, TRUE);
    //if (tmpV) g_ptr_array_unref(tmpV);
    if (_tobj) gluDeleteTess(_tobj);
#ifdef S52_USE_OPENGL_VBO
    if (_qobj) _gluDeleteQuadric(_qobj);
#else
    if (_qobj) gluDeleteQuadric(_qobj);
#endif
    _tmpV = NULL;
    _tobj = NULL;
    _qobj = NULL;

    if (_tcen) gluDeleteTess(_tcen);
    _tcen = NULL;
    if (_tcin) gluDeleteTess(_tcin);
    _tcin = NULL;
    if (_centroids) g_array_free(_centroids, TRUE);
    //if (_centroids) g_array_unref(_centroids);
    _centroids = NULL;
    if (_vertexs)   g_array_free(_vertexs, TRUE);
    //if (_vertexs)   g_array_unref(_vertexs);
    _vertexs = NULL;
    if (_nvertex)   g_array_free(_nvertex, TRUE);
    //if (_nvertex)   g_array_unref(_nvertex);
    _nvertex = NULL;

    return TRUE;
}

static S57_prim *_tessd(GLUtriangulatorObj *tobj, S57_geo *geoData)
// WARNING: not re-entrant (tmpV)
{
    guint     nr   = S57_getRingNbr(geoData);
    S57_prim *prim = S57_initPrimGeo(geoData);

    _g_ptr_array_clear(_tmpV);

    // NOTE: _*NOT*_ NULL to trigger GL_TRIANGLES tessallation
    //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) _edgeFlag);

    gluTessBeginPolygon(tobj, prim);
    for (guint i=0; i<nr; ++i) {
        guint     npt = 0;
        GLdouble *ppt = NULL;

        if (TRUE == S57_getGeoData(geoData, i, &npt, &ppt)) {
            gluTessBeginContour(tobj);
            for (guint j=0; j<npt-1; ++j, ppt+=3) {
                gluTessVertex(tobj, ppt, ppt);

                // debug
                //if (2186==S57_getGeoID(geoData)) {
                //    PRINTF("%i: %f, %f, %f\n", j, ppt[0], ppt[1], ppt[2]);
                //}
            }
            gluTessEndContour(tobj);
        }
    }
    gluTessEndPolygon(tobj);

    //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) NULL);

    return prim;
}

static double    _computeSCAMIN(void)

{
    return (_scalex > _scaley) ? _scalex : _scaley;
}

static void      _glMatrixMode(GLenum  mode)
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    _mode = mode;
    switch(mode) {
    	case GL_MODELVIEW:  _crntMat = _mvm[_mvmTop]; break;
    	case GL_PROJECTION: _crntMat = _pjm[_pjmTop]; break;
        default:
            PRINTF("_glMatrixMode()\n");
            g_assert(0);
    }
#else
    glMatrixMode(mode);
#endif

    return;
}

static void      _glPushMatrix(void)
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    GLfloat *prevMat = NULL;

    switch(_mode) {
    	case GL_MODELVIEW:  _mvmTop += 1; break;
    	case GL_PROJECTION: _pjmTop += 1; break;
        default:
            PRINTF("_glPushMatrix()\n");
            g_assert(0);
    }

    if (MATRIX_STACK_MAX<=_mvmTop || MATRIX_STACK_MAX<=_pjmTop) {
        PRINTF("ERROR: matrix stack overflow\n");
        g_assert(0);
    }

    prevMat  = (GL_MODELVIEW == _mode) ? _mvm[_mvmTop-1] : _pjm[_pjmTop-1];
    _crntMat = (GL_MODELVIEW == _mode) ? _mvm[_mvmTop  ] : _pjm[_pjmTop  ];

    // Note: no mem obverlap
    memcpy(_crntMat, prevMat, sizeof(float) * 16);

#else
   glPushMatrix();
#endif

    return;
}

static void      _glPopMatrix(void)
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    switch(_mode) {
    	case GL_MODELVIEW:  _mvmTop -= 1; break;
    	case GL_PROJECTION: _pjmTop -= 1; break;
        default:
            PRINTF("_glPopMatrix()\n");
            g_assert(0);
    }

    if (_mvmTop<0 || _pjmTop<0) {
        PRINTF("ERROR: matrix stack underflow\n");
        g_assert(0);
    }

    // optimisation
    if (GL_MODELVIEW == _mode)
        _identity_MODELVIEW = FALSE;

#else
   glPopMatrix();
#endif

    return;
}

static void      _glLoadIdentity(void)
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    memset(_crntMat, 0, sizeof(GLfloat) * 16);
    _crntMat[0] = _crntMat[5] = _crntMat[10] = _crntMat[15] = 1.0;

    // optimisation - reset flag
    if (GL_MODELVIEW == _mode)
        _identity_MODELVIEW = TRUE;

#else
    glLoadIdentity();
#endif

    return;
}

static void      _glOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    float dx = right - left;
    float dy = top   - bottom;
    float dz = zFar  - zNear;

    // avoid division by zero
    float tx = (dx != 0.0) ? -(right + left)   / dx : 0.0;
    float ty = (dy != 0.0) ? -(top   + bottom) / dy : 0.0;
    float tz = (dz != 0.0) ? -(zFar  + zNear)  / dz : 0.0;

    // GLSL ERROR: row major
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
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    if (FALSE == _identity_MODELVIEW) {
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

        _identity_MODELVIEW = TRUE;
    } else {
        _identity_MODELVIEW_cnt++;
    }
#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
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
    _glPushMatrix();
    _glScaled(scalex, scaley, 1.0);

    return TRUE;
}

static GLint     _popScaletoPixel(void)
{
    _glMatrixMode(GL_MODELVIEW);
    _glPopMatrix();

    // ModelView Matrix will be send to GPU before next glDraw
    //glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    return TRUE;
}



static GLint     _glMatrixSet(VP vpcoord)
// push & reset matrix GL_PROJECTION & GL_MODELVIEW
{
    /* */
    GLdouble  left   = 0.0;
    GLdouble  right  = 0.0;
    GLdouble  bottom = 0.0;
    GLdouble  top    = 0.0;
    GLdouble  znear  = 0.0;
    GLdouble  zfar   = 0.0;
    /* */

    // from OpenGL correcness tip --use 'int' for predictability
    // point(0.5, 0.5) fill the same pixel as recti(0,0,1,1). Note that
    // vertice need to be place +1/2 to match RasterPos()
    //GLint left=0,   right=0,   bottom=0,   top=0,   znear=0,   zfar=0;

    switch (vpcoord) {
        case VP_PRJ:
            left   = _pmin.u,      right = _pmax.u,
            bottom = _pmin.v,      top   = _pmax.v,
            znear  = Z_CLIP_PLANE, zfar  = -Z_CLIP_PLANE;
            break;

        /*
        case VP_PRJ_ZCLIP:
            left   = pmin.u,       right = pmax.u,
            bottom = pmin.v,       top   = pmax.v,
            znear  = Z_CLIP_PLANE, zfar  = 1 - Z_CLIP_PLANE;
            break;
        */
        case VP_WIN:
            left   = _vp[0],       right = _vp[0] + _vp[2],
            bottom = _vp[1],       top   = _vp[1] + _vp[3];
            znear  = Z_CLIP_PLANE, zfar  = -Z_CLIP_PLANE;
            break;
        default:
            PRINTF("ERROR: unknown viewport coodinate\n");
            g_assert(0);
            return FALSE;
    }

    if (0.0==left && 0.0==right && 0.0==bottom && 0.0==top) {
        PRINTF("ERROR: Viewport not set (%f,%f,%f,%f)\n",
               left, right, bottom, top);
        g_assert(0);
    }


    _glMatrixMode(GL_PROJECTION);
    _glPushMatrix();
    _glLoadIdentity();
    _glOrtho(left, right, bottom, top, znear, zfar);

    _glTranslated(  (left+right)/2.0,  (bottom+top)/2.0, 0.0);
    _glRotated   (_north, 0.0, 0.0, 1.0);
    _glTranslated( -(left+right)/2.0, -(bottom+top)/2.0, 0.0);

    _glMatrixMode(GL_MODELVIEW);
    _glPushMatrix();
    _glLoadIdentity();

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

    return TRUE;
}

static GLint     _glMatrixDel(VP vpcoord)
// pop matrix GL_PROJECTION & GL_MODELVIEW
{
    // vpcoord not used, just there so that it match _glMatrixSet()
    (void) vpcoord;

    _glMatrixMode(GL_PROJECTION);
    _glPopMatrix();

    _glMatrixMode(GL_MODELVIEW);
    _glPopMatrix();

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

    return TRUE;
}

#if 0
static GLint     _glMatrixDump(GLenum matrix)
// debug
{
    double m1[16];
    double m2[16];

    glGetDoublev(matrix, m1);

    memcpy(m2 ,m1, sizeof(m1));

    if (0 == memcmp(m2, m1, sizeof(m1))) {
        PRINTF("%f\t %f\t %f\t %f \n",m2[0], m2[1], m2[2], m2[3]);
        PRINTF("%f\t %f\t %f\t %f \n",m2[4], m2[5], m2[6], m2[7]);
        PRINTF("%f\t %f\t %f\t %f \n",m2[8], m2[9], m2[10],m2[11]);
        PRINTF("%f\t %f\t %f\t %f \n",m2[12],m2[13],m2[14],m2[15]);
        PRINTF("-------------------\n");
    }
    return 1;
}
#endif


//-----------------------------------
//
// PROJECTION SECTION
//
//-----------------------------------

static int       _win2prj(double *x, double *y)
// convert coordinate: window --> projected
{
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    float u       = *x;
    float v       = *y;
    float dummy_z = 0.0;
    if (GL_FALSE == _gluUnProject(u, v, dummy_z, _mvm[_mvmTop], _pjm[_pjmTop], (GLint*)_vp, &u, &v, &dummy_z)) {
        PRINTF("WARNING: UnProjection faild\n");

        g_assert(0);

        return FALSE;
    }
    *x = u;
    *y = v;

#else
    GLdouble dummy_z = 0.0;
    if (GL_FALSE == gluUnProject(*x, *y, dummy_z, _mvm, _pjm, (GLint*)_vp, x, y, &dummy_z)) {
        PRINTF("WARNING: UnProjection faild\n");
        g_assert(0);
        return FALSE;
    }
#endif

    return TRUE;
}


static projXY    _prj2win(projXY p)
// convert coordinate: projected --> window (pixel)
{
    // debug
    //PRINTF("_VP[]: %i,%i,%i,%i\n", _vp[0], _vp[1], _vp[2], _vp[3]);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // FIXME: find a better way -
    if (0 == _pjm[_pjmTop]) {
        g_assert(0);
        return p;
    }
    float u = p.u;
    float v = p.v;
    float dummy_z = 0.0;

    // make sure that _gluProject() has the right coordinate
    // but if call from 52_GL_prj2win() then matrix allready set so this is redundant
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    if (GL_FALSE == _gluProject(u, v, dummy_z, _mvm[_mvmTop], _pjm[_pjmTop], (GLint*)_vp, &u, &v, &dummy_z)) {
        PRINTF("ERROR\n");
        g_assert(0);
    }
    p.u = u;
    p.v = v;
#else
    GLdouble dummy_z = 0.0;
    if (GL_FALSE == gluProject(p.u, p.v, dummy_z, _mvm, _pjm, (GLint*)_vp, &p.u, &p.v, &dummy_z)) {
        PRINTF("ERROR\n");
        g_assert(0);
    }
#endif

    return p;
}

int        S52_GL_win2prj(double *x, double *y)
// convert coordinate: window --> projected
{
    _glMatrixSet(VP_PRJ);

    int ret = _win2prj(x, y);

    _glMatrixDel(VP_PRJ);

    return ret;
}

int        S52_GL_prj2win(double *x, double *y)
// convert coordinate: projected --> windows
{
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
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // silence gcc warning
    (void)factor;
    (void)pattern;

    /*
    static int silent = FALSE;
    if (FALSE == silent) {
        PRINTF("FIXME: line stipple\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }
    */


#else
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(factor, pattern);
#endif

    return;
}

static void      _glPointSize(GLfloat size)
{
//#ifdef S52_USE_GLES2
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


    if (npt < 2)
        return;

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
    glDrawArrays(GL_LINE_STRIP, 0, npt);
    glDisableVertexAttribArray(_aPosition);
#else
    //glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
    glDrawArrays(GL_LINE_STRIP, 0, npt);
#endif


    _checkError("_DrawArrays_LINE_STRIP() .. end");

    return;
}

static GLvoid    _DrawArrays_LINES(guint npt, vertex_t *ppt)
// this is used when VRM line style is aternate line style
// ie _normallinestyle == 'N'
{
    if (0 != (npt % 2)) {
        PRINTF("FIXME: found LINES not modulo 2\n");
        g_assert(0);
    }

    if (npt < 2)
        return;

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
    glDrawArrays(GL_LINES, 0, npt);
    glDisableVertexAttribArray(_aPosition);
#else
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
    glDrawArrays(GL_LINES, 0, npt);
#endif

    _checkError("_DrawArrays_LINES() .. end");

    return;
}

#ifdef S52_USE_OPENGL_VBO
static int       _VBODrawArrays(S57_prim *prim)
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

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

        // debug - 10 call of count = 66 --> AC(),AP()
        //glDrawArrays(mode = GL_TRIANGLES, first = 0, count = 66)
        //if (GL_TRIANGLES==mode && 66==count)
        //    PRINTF("SKIP %i\n", _S57ID);
        //else
            glDrawArrays(mode, first, count);


        //
        //if (_QUADRIC_TRANSLATE == mode) {
        //    g_assert(1 == count);
        //    _glTranslated(vert[first+0], vert[first+1], vert[first+2]);
        //} else {
        //    glDrawArrays(mode, first, count);
        //}
    }


    _checkError("_VBODrawArrays()");

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

    if (GL_FALSE == glIsBuffer(vboID)) {
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
    }

    return vboID;
}

static int       _VBODraw(S57_prim *prim)
// run a VBO
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

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
    _VBODrawArrays(prim);
    glDisableVertexAttribArray(_aPosition);
#else
    // set VertPtr to VBO
    glVertexPointer(3, GL_DOUBLE, 0, 0);
    _VBODrawArrays(prim);
#endif

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _checkError("_VBODraw() -fini-");

    return TRUE;
}

#else // S52_USE_OPENGL_VBO

static int       _DrawArrays(S57_prim *prim)
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

    glVertexPointer(3, GL_DBL_FLT,  0, vert);

    for (guint i=0; i<primNbr; ++i) {
        GLint mode  = 0;
        GLint first = 0;
        GLint count = 0;

        S57_getPrimIdx(prim, i, &mode, &first, &count);

        glDrawArrays(mode, first, count);
        //PRINTF("i:%i mode:%i first:%i count:%i\n", i, mode, first, count);
    }

    _checkError("_DrawArrays()");

    return TRUE;
}

//static int       _createDList(S57_prim *prim)
static guint     _createDList(S57_prim *prim)
// create display list
{
    guint DList = 0;
    DList = glGenLists(1);
    if (0 == DList) {
        PRINTF("ERROR: glGenLists() failed\n");
        g_assert(0);
    }

    glNewList(DList, GL_COMPILE);

    _DrawArrays(prim);

    glEndList();

    S57_setPrimDList(prim, DList);

    _checkError("_createDList()");

    //return TRUE;
    return DList;
}

static int       _callDList(S57_prim *prim)
// run display list - create it first
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     DList   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &DList))
        return FALSE;

    // no glIsList() in "OpenGL ES SC"
    if (GL_FALSE == glIsList(DList)) {
        DList = _createDList(prim);
    }

    glCallList(DList);

    _checkError("_callDList()");

    return TRUE;
}
#endif // S52_USE_OPENGL_VBO

static double    _getGridRef(S52_obj *obj, double *LLx, double *LLy, double *URx, double *URy, double *tileW, double *tileH)
{
    //
    // Tile pattern to 'grided' extent
    //

    //double x , y;    // index
    double x1, y1;   // LL of region of area
    double x2, y2;   // UR of region of area

    // pattern tile: 1 = 0.01 mm
    double tw = 0.0;  // tile width
    double th = 0.0;  // tile height
    double dx = 0.0;  // run length offset for STG pattern
    S52_PL_getAPTileDim(obj, &tw,  &th,  &dx);

    // convert tile unit (0.01mm) to pixel
    double tileWidthPix  = tw  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    double tileHeightPix = th  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));
    double stagOffsetPix = dx  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    //double tileWidthPix  = tw  / (100.0 * _dotpitch_mm_x);
    //double tileHeightPix = th  / (100.0 * _dotpitch_mm_y);
    //double stagOffsetPix = dx  / (100.0 * _dotpitch_mm_x);

    // convert tile in pixel to world
    //double d0 = stagOffsetPix * _scalex;
    double w0 = tileWidthPix  * _scalex;
    double h0 = tileHeightPix * _scaley;

    // grid alignment
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
    y1  = floor(y1 / (2*h0)) * (2*h0);

    // optimisation, resize extent grid to fit window
    if (x1 < _pmin.u)
        x1 += floor((_pmin.u-x1) / w0) * w0;
    if (y1 < _pmin.v)
        y1 += floor((_pmin.v-y1) / (2*h0)) * (2*h0);
    if (x2 > _pmax.u)
        x2 -= floor((x2 - _pmax.u) / w0) * w0;
    if (y2 > _pmax.v)
        y2 -= floor((y2 - _pmax.v) / h0) * h0;

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

static int       _fillarea(S57_geo *geoData)
{
    _S57ID = S57_getGeoID(geoData);
    _npoly++;

    S57_prim *prim = S57_getPrimGeo(geoData);
    if (NULL == prim) {
        prim = _tessd(_tobj, geoData);
    }

    // debug - test optimization
    //if (0 == g_strcmp0("DEPARE", S57_getName(geoData))) {
    //    ++_depare;
    //    return TRUE;
    //}
    //++_nAC;

#ifdef S52_USE_OPENGL_VBO
    if (FALSE == _VBODraw(prim)) {
        PRINTF("DEBUG: _VBODraw() failed [%s]\n", S57_getName(geoData));
    }
#else
    //_createDList(prim);
    _callDList(prim);
#endif

    return TRUE;
}


//---------------------------------------
//
// SYMBOLOGY INSTRUCTION RENDERER SECTION
//
//---------------------------------------
typedef struct col {
    GLubyte r;
    GLubyte g;
    GLubyte b;
    GLubyte a;
} col;

// S52_GL_PICK mode
typedef union cIdx {
    col   color;
    guint idx;
} cIdx;
static cIdx _cIdx;
static cIdx _pixelsRead[8 * 8];  // buffer to collect pixels when in S52_GL_PICK mode

#if 0
static int       _setBlend(int blend)
// TRUE turn on blending if AA
{
    //static int blendstate = FALSE;
    if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
        if (TRUE == blend) {
            glEnable(GL_BLEND);

#if defined(S52_USE_GL1) || defined(S52_USE_GL1)
            glEnable(GL_LINE_SMOOTH);
            glEnable(GL_ALPHA_TEST);
#endif
        } else {
            glDisable(GL_BLEND);

#if defined(S52_USE_GL1) || defined(S52_USE_GL1)
            glDisable(GL_LINE_SMOOTH);
            glDisable(GL_ALPHA_TEST);
#endif
        }
    }

    _checkError("_setBlend()");

    return TRUE;
}
#endif

static GLubyte   _glColor4ub(S52_Color *c)
// return transparancy
{

#ifdef S52_DEBUG
    if (NULL == c) {
        PRINTF("ERROR: no color\n");
        g_assert(0);
        return 0;
    }
#endif

    if (S52_GL_PICK == _crnt_GL_cycle) {
        // debug
        //printf("_glColor4ub: set current cIdx R to : %X\n", _cIdx.color.r);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniform4f(_uColor, _cIdx.color.r/255.0, _cIdx.color.g/255.0, _cIdx.color.b/255.0, _cIdx.color.a);
#else
        glColor4ub(_cIdx.color.r, _cIdx.color.g, _cIdx.color.b, _cIdx.color.a);
#endif

        return (GLubyte) 1;
    }

    if ('0' != c->trans) {
        // FIXME: some symbol allway use blending
        // but now with GLES2 AA its all or nothing
        if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS))
            glEnable(GL_BLEND);

#ifdef S52_USE_GL1
        glEnable(GL_ALPHA_TEST);
#endif
    }

    if (TRUE == _doHighlight) {
        S52_Color *dnghlcol = S52_PL_getColor("DNGHL");
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniform4f(_uColor, dnghlcol->R/255.0, dnghlcol->G/255.0, dnghlcol->B/255.0, (4 - (c->trans - '0')) * TRNSP_FAC_GLES2);
#else
        glColor4ub(dnghlcol->R, dnghlcol->G, dnghlcol->B, (4 - (c->trans - '0')) * TRNSP_FAC);
#endif
    } else {
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniform4f(_uColor, c->R/255.0, c->G/255.0, c->B/255.0, (4 - (c->trans - '0')) * TRNSP_FAC_GLES2);
#else
        glColor4ub(c->R, c->G, c->B, (4 - (c->trans - '0'))*TRNSP_FAC);
#endif

        if (0 != c->pen_w) {  // AC, .. doesn't have en pen_w
            glLineWidth (c->pen_w - '0');
            _glPointSize(c->pen_w - '0');
        }
    }

    return c->trans;
}

static int       _glCallList(S52_DListData *DListData)
// get color of each Display List then run it
{
    if (NULL == DListData) {
        PRINTF("WARNING: NULL DListData!\n");
        return FALSE;
    }

//#ifdef S52_USE_GLES2
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

        GLubyte trans = _glColor4ub(col);

#ifdef  S52_USE_OPENGL_VBO
        GLuint vboId = DListData->vboIds[i];
        glBindBuffer(GL_ARRAY_BUFFER, vboId);         // for vertex coordinates

        // reset offset in VBO
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
#else
        glVertexPointer(3, GL_DOUBLE, 0, 0);
#endif

        {
            guint j     = 0;
            GLint mode  = 0;
            GLint first = 0;
            GLint count = 0;

            while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {
                //if (_QUADRIC_TRANSLATE == mode) {
                if (_TRANSLATE == mode) {
                    GArray *vert = S57_getPrimVertex(DListData->prim[i]);

                    vertex_t *v = (vertex_t*)vert->data;
                    vertex_t  x = v[first*3+0];
                    vertex_t  y = v[first*3+1];
                    vertex_t  z = v[first*3+2];

                    _glTranslated(x, y, z);
//#ifdef S52_USE_GLES2
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
                    }
                }
                ++j;

            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);

#else   // S52_USE_OPENGL_VBO

        glVertexPointer(3, GL_DOUBLE, 0, 0);              // last param is offset, not ptr
        if (TRUE == glIsList(lst)) {
            //++_nFrag;
            glCallList(lst);              // NOT in OpenGL ES SC
            //glCallLists(1, GL_UNSIGNED_INT, &lst);
        } else {
            PRINTF("WARNING: glIsList() failed\n");
            g_assert(0);
        }

#endif  // S52_USE_OPENGL_VBO

        if ('0' != trans) {
#ifdef S52_USE_GL1
            glDisable(GL_ALPHA_TEST);
#endif
        }
    }

//#ifdef S52_USE_GLES2
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
    //S57_getExtPRJ(geoData, &x1, &y1, &x2, &y2);
    S57_getExt(geoData, &x1, &y1, &x2, &y2);
    double xyz[6] = {x1, y1, 0.0, x2, y2, 0.0};
    if (FALSE == S57_geo2prj3dv(2, (double*)&xyz))
        return FALSE;

    x1  = xyz[0];
    y1  = xyz[1];
    x2  = xyz[3];
    y2  = xyz[4];

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
            GLdouble *p = d;

            // CCW
            /*
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            p += 3;
            gluTessVertex(_tcen, (GLdouble*)p, (void*)p);
            */

            // CW
            p = d+(3*3);
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

        //_g_ptr_array_clear(_tmpV);

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
    S52_DListData *DListData = S52_PL_getDListData(obj);

    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

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

    guint     npt = 0;
    GLdouble *ppt = NULL;

    // debug
    //return TRUE;

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    S52_DListData *DListData = S52_PL_getDListData(obj);

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
    if (((shpLenPixel*_dotpitch_mm_y) >= SHIPS_OUTLINE_MM) && (TRUE==S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {

        // 3 - compute stretch of symbol (ratio)
        double lenRatio = shpLenPixel / symLenPixel;
        double brdRatio = shpBrdPixel / symBrdPixel;

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
        _glTranslated(0.0, -shp_off_y, 0.0);
        _pushScaletoPixel(TRUE);

        // apply stretch
        _glScaled(brdRatio, lenRatio, 1.0);

        _glCallList(DListData);

        _popScaletoPixel();

    }

    return TRUE;
}


static int       _renderSY_CSYMB(S52_obj *obj)
{
    S57_geo *geoData = S52_PL_getGeo(obj);
    char    *attname = "$SCODE";

    GString *attval  =  S57_getAttVal(geoData, attname);
    if (NULL == attval) {
        PRINTF("DEBUG: no attval\n");
        return FALSE;
    }

    S52_DListData *DListData = S52_PL_getDListData(obj);

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


        _glMatrixSet(VP_PRJ);

        _win2prj(&x, &y);

        _glTranslated(x, y, 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        _pushScaletoPixel(TRUE);

        if (_SCAMIN < 80000.0) {
            // scale bar 1 NM
            if (0==g_strcmp0(attval->str, "SCALEB10")) {
                // apply stretch
                _glScaled(WRatio, HRatio, 1.0);
                _glCallList(DListData);
            }
        } else {
            // scale bar 10 NM
            if (0==g_strcmp0(attval->str, "SCALEB11")) {
                // apply stretch
                _glScaled(WRatio, HRatio*10.0, 1.0);
                _glCallList(DListData);
            }
        }

        _popScaletoPixel();

        _glMatrixDel(VP_PRJ);

        return TRUE;
    }


    // north arrow
    if (0==g_strcmp0(attval->str, "NORTHAR1")) {
        double x = 30;
        double y = _vp[3] - 40;
        double rotation = 0.0;

        _glMatrixSet(VP_PRJ);

        _win2prj(&x, &y);

        _renderSY_POINT_T(obj, x, y, rotation);

        _glMatrixDel(VP_PRJ);

        return TRUE;
    }

    // depth unit
    if (0==g_strcmp0(attval->str, "UNITMTR1")) {
        // Note: S52 specs say: left corner, just beside the scalebar [what does that mean in XY]
        double x = 30;
        double y = 20;

        _glMatrixSet(VP_PRJ);

        _win2prj(&x, &y);

        _renderSY_POINT_T(obj, x, y, _north);

        _glMatrixDel(VP_PRJ);

        return TRUE;
    }

    if (TRUE == S52_MP_get(S52_MAR_DISP_CALIB)) {
        // check symbol physical size, should be 5mm by 5mm
        if (0==g_strcmp0(attval->str, "CHKSYM01")) {
            // FIXME: use _dotpitch_ ..
            double x = _vp[0] + 50;
            double y = _vp[1] + 50;

            _glMatrixSet(VP_PRJ);
            _win2prj(&x, &y);

            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            _glTranslated(x, y, 0.0);
            _glScaled(_scalex / (_dotpitch_mm_x * 100.0),
                      //scaley / (_dotpitch_mm_y * 100.0),
                      _scaley / (_dotpitch_mm_x * 100.0),
                      1.0);

            _glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

            _glCallList(DListData);

            _glMatrixDel(VP_PRJ);

            return TRUE;
        }

        // symbol to be used for checking and adjusting the brightness and contrast controls
        if (0==g_strcmp0(attval->str, "BLKADJ01")) {
            // FIXME: use _dotpitch_ ..
            // top left (witch is under CPU usage on Android)
            double x = _vp[2] - 50;
            double y = _vp[3] - 50;

            _glMatrixSet(VP_PRJ);
            _win2prj(&x, &y);

            _renderSY_POINT_T(obj, x, y, _north);

            _glMatrixDel(VP_PRJ);

            return TRUE;
        }
    }

    {   // C1 ed3.1: AA5C1ABO.000
        // LOWACC01 QUESMRK1 CHINFO11 CHINFO10 REFPNT02 QUAPOS01
        // CURSRA01 CURSRB01 CHINFO09 CHINFO08 INFORM01

        guint     npt     = 0;
        GLdouble *ppt     = NULL;
        if (TRUE == S57_getGeoData(geoData, 0, &npt, &ppt)) {

            _renderSY_POINT_T(obj, ppt[0], ppt[1], _north);

            return TRUE;
        }
    }

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
        if ( ((shpLenPixel*_dotpitch_mm_y) < SHIPS_OUTLINE_MM) || (FALSE==S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {
            _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
        }

        return TRUE;
    }

    // draw vector stabilization
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND01") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR01") ) {
        double vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset due to course and seep
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {

                double courseRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM  = vecper   * (speed /60.0);
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

    // time marks on vector - 6 min
    if (0 == S52_PL_cmpCmdParam(obj, "OSPSIX02")) {
        double     vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset of each 6 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                int    nmrk         = (int) (vecper / 6.0);

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
        double     vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                int    nmrk         = vecper;

                for (int i=0; i<nmrk; ++i) {
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

    // debug
    //return TRUE;

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

#ifdef S52_USE_SYM_AISSEL01
    // AIS selected: experimental, put selected symbol on target
    if ((0 == S52_PL_cmpCmdParam(obj, "AISSEL01")) &&
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
    // FIXME: NO VECT STAB if target sleeping
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND21") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR21") ) {
        double     vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset due to course and speed
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double courseRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM  = vecper   * (speed /60.0);
                double veclenM   = veclenNM * NM_METER;
                double veclenMX  = veclenM  * cos(courseRAD);
                double veclenMY  = veclenM  * sin(courseRAD);

                if ((0==S52_PL_cmpCmdParam(obj, "VECGND21")) &&
                    (NULL!=vecstbstr && '1'==*vecstbstr->str)
                   ) {
                    _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);
                } else {
                    if ((0==S52_PL_cmpCmdParam(obj, "VECWTR21")) &&
                        (NULL!=vecstbstr && '2'==*vecstbstr->str)
                       ) {
                        _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);
                    }
                }
            }
        }

        return TRUE;
    }

    // time marks on vector - 6 min
    if ((0 == S52_PL_cmpCmdParam(obj, "ARPSIX01")) ||
        (0 == S52_PL_cmpCmdParam(obj, "AISSIX01")) ){
        double     vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset of each 6 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                int    nmrk         = vecper / 6.0;

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
        double     vecper = S52_MP_get(S52_MAR_VECPER);
        if (0.0 != vecper) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                int    nmrk         = vecper;

                for (int i=0; i<nmrk; ++i) {
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

        double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;
        double shpLenPixel = shplen / scaley;

        // drawn VESSEL symbol
        // 1 - if silhoutte too small
        // 2 - OR no silhouette at all
        if ( ((shpLenPixel*_dotpitch_mm_y) < SHIPS_OUTLINE_MM) || (FALSE==S52_MP_get(S52_MAR_SHIPS_OUTLINE)) ) {
            // 3 - AND active (ie not sleeping)
            if (NULL!=vestatstr && '1'==*vestatstr->str)
                _renderSY_POINT_T(obj, ppt[0], ppt[1], headng);
        }
        //double course, speed;
        //_getVector(obj, &course, &speed);

        //_renderSY_POINT_T(obj, ppt[0], ppt[1], headng);

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
        double x2 = ppt[(i+1)*3 + 0];
        double y2 = ppt[(i+1)*3 + 1];

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

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt)) {
        PRINTF("WARNING: LEGLIN with no geo\n");
        return FALSE;
    }

    if (npt != 2) {
        PRINTF("WARNING: LEGLIN with %i point\n", npt);
        return FALSE;
    }

    // planned speed (box symbol)
    if (0 == S52_PL_cmpCmdParam(obj, "PLNSPD03") ||
        0 == S52_PL_cmpCmdParam(obj, "PLNSPD04") ) {
        GString *plnspdstr = S57_getAttVal(geo, "plnspd");
        double   plnspd    = (NULL==plnspdstr) ? 0.0 : S52_atof(plnspdstr->str);

        if (0.0 != plnspd) {
            double offset_x = 10.0 * _scalex;
            double offset_y = 18.0 * _scaley;
            gchar str[80];

            // draw box --side effect, set color
            // FIXME: strech the box to fit text
            // FIXME: place the box "above close to the leg" (see S52 p. I-112)
            //_renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);
            _renderSY_POINT_T(obj, ppt[0], ppt[1], _north);

            // draw speed text inside box
            // FIXME: compute offset from symbol's bbox
            // S52_PL_getSYbbox(S52_obj *obj, int *width, int *height);
            // FIXME: ajuste XY for rotation
            SPRINTF(str, "%3.1f kt", plnspd);

            // FIXME: get color from TE & TX commad word
            S52_Color *color = S52_PL_getColor("CHBLK");

            //_renderTXTAA(obj, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, str);
            _renderTXTAA(obj, color, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, str);


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
    if (1 == isinf(orient)) {
        PRINTF("WARNING: no 'orient' for object: %s\n", S57_getName(geoData));
        return FALSE;
    }

    guint     npt = 0;
    GLdouble *ppt = NULL;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
        return FALSE;
    }

    if (POINT_T == S57_getObjtype(geoData)) {

        // SOUNDG
        /* S52_MAR_FONT_SOUNDG deprecated
        if (0 == g_strcmp0(S57_getName(geoData), "SOUNDG")) {
            if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                double    datum   = S52_MP_get(S52_MAR_DATUM_OFFSET);
                char      str[16] = {'\0'};
                double    soundg;

                guint     npt = 0;
                GLdouble *ppt = NULL;

                if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
                    return FALSE;
                }

                soundg = ppt[2] + datum;

                if (30.0 > soundg)
                    g_snprintf(str, 5, "%4.1f", soundg);
                else
                    g_snprintf(str, 5, "%4.f", soundg);

                S52_DListData *DListData = S52_PL_getDListData(obj);
                S52_Color *col = DListData->colors;

                _glColor4ub(col);

                return TRUE;
            }
        }
        */

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
                    S52_Color     *colors    = NULL;
                    double         deg       = S52_MP_get(S52_MAR_ROT_BUOY_LIGHT);

                    S52_DListData *DListData = S52_PL_getDListData(obj);
                    colors = DListData->colors;
                    if (0 == g_strcmp0(colors->colName, "LITRD"))
                        orient = deg + 90.0;
                    if (0 == g_strcmp0(colors->colName, "LITGN"))
                        orient = deg - 90.0;

                }
            }
        }

        // all other point sym
        _renderSY_POINT_T(obj, ppt[0], ppt[1], orient+_north);

        return TRUE;
    }

    //debug - skip LINES_T & AREAS_T
    //return TRUE;

    // an SY command on a line object (ex light on power line)
    if (LINES_T == S57_getObjtype(geoData)) {

        // computer 'center' of line
        double cView_x = (_pmax.u + _pmin.u) / 2.0;
        double cView_y = (_pmax.v + _pmin.v) / 2.0;
        double dmin    = INFINITY;
        double xmin, ymin;

        guint     npt = 0;
        GLdouble *ppt = NULL;
        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
            return FALSE;
        }

        if (0==g_strcmp0("ebline", S57_getName(geoData))) {

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
        //for (i=0; i<npt-1; ++i) {
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
            _renderSY_POINT_T(obj, xmin, ymin, orient);
        }

        //_setBlend(FALSE);

        return TRUE;
    }

    //debug - skip AREAS_T (cost 30msec; from ~110ms to ~80ms on Estuaire du St-L CA279037.000)
    //return TRUE;

    if (AREAS_T == S57_getObjtype(geoData)) {
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
                S52_DListData *DListData = S52_PL_getDListData(obj);
                S52_Color *col = DListData->colors;
                _glColor4ub(col);

                // when in pick mode, fill the area
                _fillarea(geoData);
            }

            // centroid offset might put the symb outside the area
            if (TRUE == S57_hasCentroid(geoData)) {
                double x,y;
                while (TRUE == S57_getNextCentroid(geoData, &x, &y))
                    _renderSY_POINT_T(obj, x, y, orient+_north);
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
                    _renderSY_POINT_T(obj, x, y, orient+_north);
                }
                return TRUE;
            } else {
                _computeCentroid(geoData);
            }

            // compute offset
            if (0 < _centroids->len) {
                S52_PL_getOffset(obj, &offset_x, &offset_y);

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

    //PRINTF("S1: %f, S2:%f\n", sectr1, sectr2);

    // this is part of CS
    if (TRUE == S52_MP_get(S52_MAR_FULL_SECTORS)) {
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
#ifndef S52_USE_GV
            if (FALSE == S57_geo2prj3dv(1, (double*)&pt))
                return FALSE;
#endif
            // position of end of sector nominal range
            ptlen.x = x1; // not used
            ptlen.y = y1 + (valnmr / 60.0);
            ptlen.z = 0.0;
#ifndef S52_USE_GV
            if (FALSE == S57_geo2prj3dv(1, (double*)&ptlen))
                return FALSE;
#endif
            {
                projUV p = {ptlen.x, ptlen.y};
                //p.u = ptlen.x;
                //p.v = ptlen.y;
                p   = _prj2win(p);
                leglenpix = p.v;
                p.u = pt.x;
                p.v = pt.y;
                p   = _prj2win(p);
                //leglenpix -= p.v;
                leglenpix += p.v;
            }
        }
    }

    if (NULL != orientstr) {
        double o = S52_atof(orientstr->str);

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90.0-o, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        _popScaletoPixel();

    }

    if (NULL != sectr1str) {
        double sectr1 = S52_atof(sectr1str->str);

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90.0-sectr1, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
        _popScaletoPixel();

    }

    if (NULL != sectr2str) {
        double sectr2 = S52_atof(sectr2str->str);

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glRotated(90-sectr2, 0.0, 0.0, 1.0);

        _pushScaletoPixel(FALSE);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        {
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
    if ((NULL!=headngstr) && (TRUE==S52_MP_get(S52_MAR_HEADNG_LINE)) && ('1'==pen_w)) {
        double orient = S52_PL_getSYorient(obj);

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient-90.0, 0.0, 0.0, 1.0);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

        // OWNSHP Heading
        // FIXME: draw to the edge of the screen
        // FIXME: coord. sys. must be in meter
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {_rangeNM * NM_METER * 2.0, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
        }
    }

    // beam bearing line
    if ((0.0!=S52_MP_get(S52_MAR_BEAM_BRG_NM)) && ('1'==pen_w)) {
        double orient    = S52_PL_getSYorient(obj);
        double beambrgNM = S52_MP_get(S52_MAR_BEAM_BRG_NM);

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);

//#ifdef S52_USE_GLES2
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
            //pt3v   pt[2]     = {{0.0, 0.0, 0.0}, {0.0, veclenM, 0.0}};

            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            _glTranslated(ppt[0], ppt[1], 0.0);

//#ifdef S52_USE_GLES2
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
        if ((NULL!=headngstr) && (TRUE==S52_MP_get(S52_MAR_HEADNG_LINE)))  {
            GLdouble *ppt = NULL;
            guint     npt = 0;
            if (TRUE == S57_getGeoData(geo, 0, &npt, &ppt)) {
                double headng = S52_atof(headngstr->str);
                // draw a line 50mm in length
                pt3v pt[2] = {{0.0, 0.0, 0.0}, {50.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X), 0.0, 0.0}};

                _glMatrixMode(GL_MODELVIEW);
                _glLoadIdentity();

                _glTranslated(ppt[0], ppt[1], ppt[2]);
                _glRotated(90.0 - headng, 0.0, 0.0, 1.0);
                _glScaled(1.0, -1.0, 1.0);

                _pushScaletoPixel(FALSE);

//#ifdef S52_USE_GLES2
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

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
                GString *vestatstr = S57_getAttVal(geo, "vestat");
                if (NULL!=vestatstr && '3'==*vestatstr->str) {
                    // FIXME: move to _renderLS_setPatDott()
                    float dx       = pt[0].x - pt[1].x;
                    float dy       = pt[0].y - pt[1].y;
                    float leglen_m = sqrt(dx*dx + dy*dy);                     // leg length in meter
                    float leglen_px= leglen_m  / _scalex;                      // leg length in pixel
                    float sym_n    = leglen_px / 32.0;  // 32 pixels (rgba)
                    float ptr[4] = {
                        0.0,   0.0,
                        sym_n, 1.0
                    };

                    glLineWidth(3);
                    glUniform1f(_uStipOn, 1.0);
                    glBindTexture(GL_TEXTURE_2D, _dashpa_mask_texID);
                    glEnableVertexAttribArray(_aUV);
                    glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 0, ptr);
                }

                _glUniformMatrix4fv_uModelview();
                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);

                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindTexture(GL_TEXTURE_2D,  0);
                // turn OFF stippling
                glUniform1f(_uStipOn, 0.0);

                glDisableVertexAttribArray(_aUV);
                glDisableVertexAttribArray(_aPosition);
#else
                _glUniformMatrix4fv_uModelview();
                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
#endif
            }
        }
    }

    return TRUE;
}

static int       _renderLS_afterglow(S52_obj *obj)
{
    (void)obj;

#ifdef S52_USE_AFGLOW

    S57_geo   *geo = S52_PL_getGeo(obj);
    GLdouble  *ppt = NULL;
    guint      npt = 0;

    if (0.0 == S52_MP_get(S52_MAR_DISP_AFTERGLOW))
        return TRUE;

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return TRUE;

    if (0 == npt)
        return TRUE;

    //{   // set color
        S52_Color *col;
        char       style;   // dummy
        char       pen_w;   // dummy
        S52_PL_getLSdata(obj, &pen_w, &style, &col);
        _glColor4ub(col);
    //}

    //_setBlend(TRUE);

    _checkError("_renderLS_afterglow() .. beg");

    _glUniformMatrix4fv_uModelview();
    _glPointSize(7.0);

    // fill color (alpha) array
    guint  pti = S57_getGeoSize(geo);
    if (0 == pti)
        return TRUE;

//#ifdef S52_USE_GLES2
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

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    for (guint i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, crntAlpha);
        crntAlpha += dalpha;
    }
    // convert an array of geo double (3) to float (3)
    _d2f(_tessWorkBuf_f, pti, ppt);

#else
    for (guint i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, col->R);
        g_array_append_val(_aftglwColorArr, col->G);
        g_array_append_val(_aftglwColorArr, col->B);
        unsigned char tmp = (unsigned char)crntAlpha;
        g_array_append_val(_aftglwColorArr, tmp);
        crntAlpha += dalpha;
    }
#endif

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    //_checkError("_renderLS_afterglow() .. -0-");

    // vertex array - fill vbo arrays
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0,  _tessWorkBuf_f->data);


    // turn ON after glow in shader
    glUniform1f(_uGlowOn, 1.0);

    //_checkError("_renderLS_afterglow() .. -0.1-");

    // fill array with alpha
    glEnableVertexAttribArray(_aAlpha);
    glVertexAttribPointer    (_aAlpha, 1, GL_FLOAT, GL_FALSE, 0, _aftglwColorArr->data);

    //_checkError("_renderLS_afterglow() .. -1-");

#else  // S52_USE_GLES2

    // vertex array - fill vbo arrays
    glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwVertID);

    //glEnableClientState(GL_VERTEX_ARRAY);       // no need to activate vertex coords array - alway on
    glVertexPointer(3, GL_DOUBLE, 0, 0);          // last param is offset, not ptr
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, 0);
    glBufferData(GL_ARRAY_BUFFER, pti*sizeof(vertex_t)*3, (const void *)ppt, GL_DYNAMIC_DRAW);

    // colors array
    glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwColrID);
    glBufferData(GL_ARRAY_BUFFER, pti*sizeof(unsigned char)*4, (const void *)_aftglwColorArr->data, GL_DYNAMIC_DRAW);

#endif  // S52_USE_GLES2


    // 3 - draw
    glDrawArrays(GL_POINTS, 0, pti);
    //_checkError("_renderLS_afterglow() .. -2-");

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // 4 - done
    // turn OFF after glow
    glUniform1f(_uGlowOn, 0.0);
    glDisableVertexAttribArray(_aPosition);
    glDisableVertexAttribArray(_aAlpha);

#else  // S52_USE_GLES2

    // deactivate color array
    glDisableClientState(GL_COLOR_ARRAY);

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#endif  // S52_USE_GLES2
#endif  // S52_USE_AFGLOW

    _checkError("_renderLS_afterglow() .. end");

    return TRUE;
}

static int       _renderLS(S52_obj *obj)
// Line Style
// FIXME: do overlapping line supression (need to find a test case - S52_MAR_SYMBOLIZED_BND)
// FIX: add clip plane in shader (GLES2)
{
#ifdef S52_USE_GV
    // FIXME
    return FALSE;
#endif

    if (S52_CMD_WRD_FILTER_LS & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S52_Color *col;
    char       style;   // L/S/T
    char       pen_w;
    S52_PL_getLSdata(obj, &pen_w, &style, &col);
    _glColor4ub(col);

    glLineWidth(pen_w - '0');
    //glLineWidth(pen_w - '0' + 0.1);  // WARNING: THIS +0.1 SLOW DOWN EVERYTHING
    //glLineWidth(pen_w - '0' + 0.5);
    //glLineWidth(pen_w - '0' + 0.375);
    //glLineWidth(pen_w - '0' - 0.5);

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
    }

    {
        S57_geo  *geoData = S52_PL_getGeo(obj);

        if (POINT_T == S57_getObjtype(geoData)) {
            if (0 == g_strcmp0("LIGHTS", S57_getName(geoData)))
                _renderLS_LIGHTS05(obj);
            else {
                if (0 == g_strcmp0("ownshp", S57_getName(geoData)))
                    _renderLS_ownshp(obj);
                else {
                    if (0 == g_strcmp0("vessel", S57_getName(geoData))) {
                        // AIS close quarters
                        GString *vestatstr = S57_getAttVal(geoData, "vestat");
                        if (NULL!=vestatstr && '3'==*vestatstr->str) {
                            if (0 == g_strcmp0("DNGHL", col->colName))
                                _renderLS_vessel(obj);
                            else
                                // discard all other line not of DNGHL colour
                                return TRUE;
                        } else {
                            // normal line
                            _renderLS_vessel(obj);
                        }
                    }
                }
            }
        }
        else
        {
            // LINES_T, AREAS_T

            // FIXME: case of pick AREA where the only commandword is LS()
            // FIX: one more call to fillarea()
            GLdouble *ppt     = NULL;
            guint     npt     = 0;
            S57_getGeoData(geoData, 0, &npt, &ppt);

            //*
            // get the current number of positon (this grow as GPS/AIS pos come in)
            if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
                npt = S57_getGeoSize(geoData);
            }

            if (0 == g_strcmp0("ownshp", S57_getName(geoData))) {
                // what symbol for ownshp of type line or area ?
                // when ownshp is a POINT_T type !!!
                PRINTF("DEBUG: ownshp obj of type LINES_T, AREAS_T!");
                _renderLS_ownshp(obj);
                g_assert(0);
            } else {
                if ((0 == g_strcmp0("afgves", S57_getName(geoData))) ||
                    (0 == g_strcmp0("afgshp", S57_getName(geoData)))
                   ) {
                    _renderLS_afterglow(obj);
                } else {

                    _glUniformMatrix4fv_uModelview();

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
                    _d2f(_tessWorkBuf_f, npt, ppt);

                    if (0 == g_strcmp0("leglin", S57_getName(geoData))) {
                        // FIXME: move to _renderLS_setPatDott()
                        glUniform1f(_uStipOn, 1.0);
                        glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);

                        float dx       = ppt[0] - ppt[3];
                        float dy       = ppt[1] - ppt[4];
                        float leglen_m = sqrt(dx*dx + dy*dy);   // leg length in meter
                        float leglen_px= leglen_m  / _scalex;   // leg length in pixel
                        float sym_n    = leglen_px / 32.0;      // number of symbol - 32 pixels (rgba)

                        float ptr[4] = {
                            0.0,   0.0,
                            sym_n, 1.0
                        };

                        glEnableVertexAttribArray(_aUV);
                        glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 0, ptr);

                        _DrawArrays_LINE_STRIP(npt, (vertex_t *)_tessWorkBuf_f->data);

                        glDisableVertexAttribArray(_aUV);

                        // turn OFF stippling
                        glBindTexture(GL_TEXTURE_2D,  0);
                        glUniform1f(_uStipOn, 0.0);

                    } else {
                        _DrawArrays_LINE_STRIP(npt, (vertex_t *)_tessWorkBuf_f->data);
                    }
#else
                    _DrawArrays_LINE_STRIP(npt, (vertex_t *)ppt);
#endif
                }
            }
            //*/



//#ifdef S52_USE_GLES2
//#ifdef S52_USE_GL2
            // Not usefull with AA
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


//#if !defined(S52_USE_GLES2)
#ifndef S52_USE_GL2
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
    // FIXME
    return FALSE;
#endif


    if (S52_CMD_WRD_FILTER_LC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    GLdouble       symlen_pixl = 0.0;
    GLdouble       symlen_wrld = 0.0;
    GLdouble       x1,y1,z1,  x2,y2,z2;
    S57_geo       *geo = S52_PL_getGeo(obj);

    // draw arc if this is a leglin
    if (0 == g_strcmp0("leglin", S57_getName(geo))) {
        // check if user want to display arc
        if ((2.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) || (3.0==S52_MP_get(S52_MAR_DISP_WHOLIN))) {
            S52_obj *objNextLeg = S52_PL_getNextLeg(obj);

            if (NULL != objNextLeg)
                _drawArc(obj, objNextLeg);
        }
    }

    GLdouble *ppt = NULL;
    guint     npt = 0;
    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    // set pen color & size here because values might not
    // be set via call list --short line
    S52_DListData *DListData = S52_PL_getDListData(obj);
    S52_Color *c = DListData->colors;
    _glColor4ub(c);

    GLdouble symlen = 0.0;
    char     pen_w  = 0;
    S52_PL_getLCdata(obj, &symlen, &pen_w);
    glLineWidth(pen_w - '0');
    //glLineWidth(pen_w - '0' + 0.375);

    symlen_pixl = symlen / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    symlen_wrld = symlen_pixl * _scalex;

    g_array_set_size(_tmpWorkBuffer, 0);

    double off_x = ppt[0];
    double off_y = ppt[1];
    for (guint i=1; i<npt; ++i) {
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
        // overlapping Line Complex (LC) supression
        //
        if (z1<0.0 && z2<0.0) {
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
            // reset origine
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

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
        // but now with GLES2 AA its all or nothing
        //_setBlend(TRUE);
        //if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
        //    glEnable(GL_BLEND);
        //}

        {   // complete the rest of the line
            pt3v pt[2] = {{x1+offset_wrld_x, y1+offset_wrld_y, 0.0}, {x2, y2, 0.0}};
            g_array_append_val(_tmpWorkBuffer, pt[0]);
            g_array_append_val(_tmpWorkBuffer, pt[1]);
        }
    }

    // set identity matrix
    _glUniformMatrix4fv_uModelview();

    // render all lines ending
    _DrawArrays_LINES(_tmpWorkBuffer->len, (vertex_t*)_tmpWorkBuffer->data);

    //_setBlend(FALSE);

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
        GLdouble  *ppt       = NULL;
        guint      npt       = 0;
        S52_Color *c         = S52_PL_getACdata(obj);
        S52_Color *black     = S52_PL_getColor("CHBLK");
        GLdouble   sectr1    = (NULL == sectr1str) ? 0.0 : S52_atof(sectr1str->str);
        GLdouble   sectr2    = (NULL == sectr2str) ? 0.0 : S52_atof(sectr2str->str);
        double     sweep     = (sectr1 > sectr2) ? sectr2-sectr1+360 : sectr2-sectr1;
        //GLdouble   startAng  = (sectr1 + 180 > 360.0) ? sectr1 : sectr1 + 180;
        GString   *extradstr = S57_getAttVal(geoData, "extend_arc_radius");
        GLdouble   radius    = 0.0;
        GLint      loops     = 1;
        projUV     p         = {0.0, 0.0};
        //double     z         = 0.0;

        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
            return FALSE;

        //_glMatrixMode(GL_MODELVIEW);
        //_glLoadIdentity();

        p.u = ppt[0];
        p.v = ppt[1];
        p = _prj2win(p);

        if (NULL!=extradstr && 'Y'==*extradstr->str) {
            radius = 25.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 25 mm on xoom)
        } else {
            radius = 20.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 20 mm on xoom)
        }

        //_setBlend(TRUE);


        _glTranslated(ppt[0], ppt[1], 0.0);
        _pushScaletoPixel(FALSE);

        // NOTE: specs say unit, assume it mean pixel
#ifdef S52_USE_OPENGL_VBO
        _gluQuadricDrawStyle(_qobj, GLU_FILL);
#else
        gluQuadricDrawStyle(_qobj, GLU_FILL);
#endif

        // first pass - create VBO
        S52_DListData *DList = S52_PL_getDListData(obj);
        if (NULL == DList) {
            DList = S52_PL_newDListData(obj);
            DList->nbr = 2;
        }
        if (FALSE == glIsBuffer(DList->vboIds[0])) {
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
            //gluPartialDisk(_qobj, radius, radius+4, slice, loops, sectr1+180, sweep);
            gluPartialDisk(_qobj, radius, radius+4, sweep/2.0, loops, sectr1+180, sweep);
            _DrawArrays(_diskPrimTmp);
            glEndList();

            // color sector
            DList->vboIds[1] = glGenLists(1);
            glNewList(DList->vboIds[1], GL_COMPILE);

            _diskPrimTmp = DList->prim[1];
            //gluPartialDisk(_qobj, radius+1, radius+3, slice, loops, sectr1+180, sweep);
            gluPartialDisk(_qobj, radius+1, radius+3, sweep/2.0, loops, sectr1+180, sweep);
            _DrawArrays(_diskPrimTmp);
            glEndList();
#endif
            _diskPrimTmp = NULL;
        }

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

    if (0 != g_strcmp0("vrmark", S57_getName(geo))) {
        PRINTF("ERROR: not a 'vrmark' (%s)\n", S57_getName(geo));
        g_assert(0);
    }

    S52_DListData *DListData = S52_PL_getDListData(obj);
    if (NULL == DListData) {
        DListData = S52_PL_newDListData(obj);
        DListData->nbr       = 1;
        DListData->prim[0]   = S57_initPrim(NULL);
        DListData->colors[0] = *S52_PL_getColor("CURSR");
    }

    //_setBlend(TRUE);

    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    _glTranslated(ppt[0], ppt[1], 0.0);
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glVertexPointer(3, GL_DOUBLE, 0, 0);              // last param is offset, not ptr
#endif

    S52_Color *c = DListData->colors;
    _glColor4ub(c);

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
    glLineWidth(2);

    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     DList   = 0;

    if (FALSE == S57_getPrimData(_diskPrimTmp, &primNbr, &vert, &vertNbr, &DList))
        return FALSE;


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
    // debug - this filter also in _VBODrawArrays():glDraw()
    if (S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S52_Color *c   = S52_PL_getACdata(obj);
    S57_geo   *geo = S52_PL_getGeo(obj);

    // LIGHTS05
    if (POINT_T == S57_getObjtype(geo)) {
        _renderAC_LIGHTS05(obj);
        return TRUE;
    }

    // VRM
    if ((LINES_T==S57_getObjtype(geo)) && (0==g_strcmp0("vrmark", S57_getName(geo)))) {
        _renderAC_VRMEBL01(obj);
        return TRUE;
    }

    _glColor4ub(c);

#if !defined(S52_USE_GV)
    _glUniformMatrix4fv_uModelview();
#endif

    _fillarea(geo);

    _checkError("_renderAC()");

    return TRUE;
}

static int       _renderAP_NODATA_layer0(void)
{
    // debug - this filter also in _VBODrawArrays():glDraw()
    if (S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    typedef struct {vertex_t x,y;} pt2v;
    pt2v pt0, pt1, pt2, pt3;

    // double --> float
    pt0.x = _pmin.u;
    pt0.y = _pmin.v;

    pt1.x = _pmin.u;
    pt1.y = _pmax.v;

    pt2.x = _pmax.u;
    pt2.y = _pmax.v;

    pt3.x = _pmax.u;
    pt3.y = _pmin.v;

    S52_Color *chgrd = S52_PL_getColor("CHGRD");  // grey, conspic
    _glColor4ub(chgrd);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // draw using texture as a stencil -------------------

    vertex_t tile_x   = 32 * _scalex;
    vertex_t tile_y   = 32 * _scaley;
    int      n_tile_x = (_pmin.u - _pmax.u) / tile_x;
    int      n_tile_y = (_pmin.v - _pmax.v) / tile_y;

    //*
    vertex_t ppt[4*3 + 4*2] = {
        pt0.x, pt0.y, 0.0,        0.0f,     0.0f,
        pt1.x, pt1.y, 0.0,        0.0f,     n_tile_y,
        pt2.x, pt2.y, 0.0,        n_tile_x, n_tile_y,
        pt3.x, pt3.y, 0.0,        n_tile_x, 0.0f
    };
    //*/

    /*
    vertex_t ppt1[4*2] = {
        0.0f,     0.0f,
        0.0f,     n_tile_y,
        n_tile_x, n_tile_y,
        n_tile_x, 0.0f
    };
    //*/

    /*
    vertex_t ppt[4*3 + 4*2] = {
        pt0.x, pt0.y, 0.0,        pt0.x, pt0.y,
        pt1.x, pt1.y, 0.0,        pt1.x, pt1.y,
        pt2.x, pt2.y, 0.0,        pt2.x, pt2.y,
        pt3.x, pt3.y, 0.0,        pt3.x, pt3.y
    };
    //*/

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer(_aUV, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);
    //glVertexAttribPointer(_aUV, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    // turn ON 'sampler2d'
    glUniform1f(_uPattOn,       1.0);
    glUniform1f(_uPattGridX,  pt0.x);
    glUniform1f(_uPattGridY,  pt0.y);
    glUniform1f(_uPattW,     tile_x);
    glUniform1f(_uPattH,     tile_y);

    glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

    glFrontFace(GL_CW);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    //glDrawArrays(GL_TRIANGLES, 0, 3);

    glFrontFace(GL_CCW);

    _checkError("_renderAP_NODATA_layer0 -4-");

    glBindTexture(GL_TEXTURE_2D,  0);

    glUniform1f(_uPattOn,    0.0);
    glUniform1f(_uPattGridX, 0.0);
    glUniform1f(_uPattGridY, 0.0);
    glUniform1f(_uPattW,     0.0);
    glUniform1f(_uPattH,     0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

#else  // S52_USE_GLES2

    glEnable(GL_POLYGON_STIPPLE);

    //glPolygonStipple(_nodata_mask);

    // FIXME: could bind buffer - why? or why not?
    {
        vertex_t ppt[4*3] = {pt0.x, pt0.y, 0.0, pt1.x, pt1.y, 0.0, pt2.x, pt2.y, 0.0, pt3.x, pt3.y, 0.0};
        glVertexPointer(3, GL_DBL_FLT, 0, ppt);
        glDrawArrays(GL_QUADS, 0, 4);
    }

    glDisable(GL_POLYGON_STIPPLE);

#endif  // S52_USE_GLES2

    _checkError("_renderAP_NODATA_layer0 -end-");

    return FALSE;
}

static int       _renderAP(S52_obj *obj)
// Area Pattern
// NOTE: S52 define pattern rotation but doesn't use it in PLib, so not implemented.
{
    // debug - this filter also in _VBODrawArrays():glDraw()
    if (S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj))) {
        if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
            return TRUE;
    }


#ifdef S52_USE_GV
    // FIXME
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

        _glColor4ub(&dummy);
        _fillarea(geoData);

        return TRUE;
    }
    //*/
    //--------------------------------------------------------

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
    //PRINTF("OVERRIDE PRIORITY: %s, TYPE: %s\n", S52_PL_getOBCL(obj), S52_PL_infoLUP(obj));

    //S52_PL_cmpCmdParam(obj, NULL);

    return TRUE;
}

static int       _renderTXTAA(S52_obj *obj, S52_Color *color, double x, double y, unsigned int bsize, unsigned int weight, const char *str)
// render text in AA if Mar Param set
// Note: PLib C1 CHARS for TE() & TX() alway '15110' - ie style = 1 (alway), weigth = '5' (medium), width = 1 (alway), bsize = 10
// Note: weight is already converted from '4','5','6' to int 0,1,2
// Note: obj can be NULL
{
    // TODO: use 'bsize'
    (void) bsize;

    g_assert(NULL != color);

    if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    if (weight >= S52_MAX_FONT) {
        PRINTF("ERROR: weight(%i) >= S52_MAX_FONT(%i)\n", weight, S52_MAX_FONT);
        return FALSE;
    }

    //if (bsize >= S52_MAX_FONT) {
    //    PRINTF("ERROR: bsize(%i) >= S52_MAX_FONT(%i)\n", bsize, S52_MAX_FONT);
    //    return FALSE;
    //}

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

#ifdef S52_USE_FREETYPE_GL
//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // static text
    guint len = 0;
    if ((S52_GL_DRAW==_crnt_GL_cycle) && (NULL!=obj)) {
        GLuint vboID = S52_PL_getFreetypeGL_VBO(obj, &len);
        if (GL_TRUE == glIsBuffer(vboID)) {
            // connect to data in VBO GPU
            glBindBuffer(GL_ARRAY_BUFFER, vboID);
        } else {
            _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight);
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

            S52_PL_setFreetypeGL_VBO(obj, vboID, _freetype_gl_buffer->len);

            // bind VBOs for vertex array
            glBindBuffer(GL_ARRAY_BUFFER, vboID);      // for vertex coordinates
            // upload freetype_gl data to GPU
            glBufferData(GL_ARRAY_BUFFER,
                         _freetype_gl_buffer->len * sizeof(_freetype_gl_vertex_t),
                         (const void *)_freetype_gl_buffer->data,
                         GL_STATIC_DRAW);
        }
    }

    // dynamique text
    if (S52_GL_LAST == _crnt_GL_cycle) {
        _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight);
    }

    // lone text
    if (S52_GL_NONE == _crnt_GL_cycle) {
        _freetype_gl_buffer = _fill_freetype_gl_buffer(_freetype_gl_buffer, str, weight);
    }

#ifdef S52_USE_TXT_SHADOW
    {
        S52_Color *c = S52_PL_getColor("UIBCK");   // opposite of CHBLK
        _glColor4ub(c);

        // lower right - OK
        if ((S52_GL_LAST==_crnt_GL_cycle) || (S52_GL_NONE==_crnt_GL_cycle)) {
            // some MIO change age of target - need to resend the string
            _renderTXTAA_gl2(x+_scalex, y-_scaley, (GLfloat*)_freetype_gl_buffer->data, _freetype_gl_buffer->len);
        } else {
            _renderTXTAA_gl2(x+_scalex, y-_scaley, NULL, len);
        }
    }
#endif  // S52_USE_TXT_SHADOW

    _glColor4ub(color);

    if ((S52_GL_LAST==_crnt_GL_cycle) || (S52_GL_NONE==_crnt_GL_cycle)) {
        // some MIO change age of target - need to resend the string
        _renderTXTAA_gl2(x, y, (GLfloat*)_freetype_gl_buffer->data, _freetype_gl_buffer->len);
    } else {
        _renderTXTAA_gl2(x, y, NULL, len);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

#endif  // S52_USE_GLES2
#endif  // S52_USE_FREETYPE_GL

#ifdef S52_USE_GLC
    if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
        projUV p = {x, y};

        //_glMatrixMode(GL_MODELVIEW);
        //_glLoadIdentity();

        p = _prj2win(p);

        _glMatrixSet(VP_WIN);

        glRasterPos3d((int)p.u, (int)p.v, 0.0);
        PRINTF("GLC:%s\n", str);

        glcRenderString(str);

        _glMatrixDel(VP_WIN);

        _checkError("_renderTXTAA() / POINT_T");

        return TRUE;
    }
#endif

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

#ifdef S52_USE_FTGL
    (void)obj;

    double n = _north;
    _north = 0.0;

    _glColor4ub(color);

    //_setBlend(FALSE);
    _glMatrixSet(VP_WIN);

    projUV p = {x, y};
    p = _prj2win(p);

    glRasterPos2i(p.u, p.v);

    // debug
    //PRINTF("ftgl:%s\n", str);

    if (NULL != _ftglFont[weight])
        ftglRenderFont(_ftglFont[weight], str, FTGL_RENDER_ALL);

    _glMatrixDel(VP_WIN);

    _checkError("_renderTXTAA() / POINT_T");

    _north = n;
#endif // S52_USE_FTGL


    return TRUE;
}

static int       _renderTXT(S52_obj *obj)
// render TE or TX
// Note: all text pass here
{
    if (0.0 == S52_MP_get(S52_MAR_SHOW_TEXT))
        return FALSE;

    // also in _renderTXTAA()
    if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    guint      npt    = 0;
    GLdouble  *ppt    = NULL;
    S57_geo *geoData = S52_PL_getGeo(obj);
    if (FALSE == S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    S52_Color   *color  = NULL;
    int          xoffs  = 0;
    int          yoffs  = 0;
    unsigned int bsize  = 0;
    unsigned int weight = 0;
    int          disIdx = 0;      // text view group
    const char  *str = S52_PL_getEX(obj, &color, &xoffs, &yoffs, &bsize, &weight, &disIdx);

    // debug
    //PRINTF("xoffs/yoffs/bsize/weight: %i/%i/%i/%i:%s\n", xoffs, yoffs, bsize, weight, str);

    if (NULL == str) {
        //PRINTF("NULL text string\n");
        return FALSE;
    }
    if (0 == strlen(str)) {
        PRINTF("no text!\n");
        return FALSE;
    }

    // supress display of text
    if (FALSE == S52_MP_getTextDisp(disIdx))
        return FALSE;

    // convert offset to PRJ
    double uoffs  = ((10 * PICA * xoffs) / S52_MP_get(S52_MAR_DOTPITCH_MM_X)) * _scalex;
    double voffs  = ((10 * PICA * yoffs) / S52_MP_get(S52_MAR_DOTPITCH_MM_Y)) * _scaley;

    //PRINTF("uoffs/voffs: %f/%f %s\n", uoffs, voffs, str);

    if (POINT_T == S57_getObjtype(geoData)) {
        _renderTXTAA(obj, color, ppt[0]+uoffs, ppt[1]-voffs, bsize, weight, str);

        return TRUE;
    }

    if (LINES_T == S57_getObjtype(geoData)) {
        if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
            // past track time
            for (guint i=0; i<npt; ++i) {
                gchar s[80];
                int timeHH = ppt[i*3 + 2] / 100;
                int timeMM = ppt[i*3 + 2] - (timeHH * 100);
                //SPRINTF(s, "t%04.f", ppt[i*3 + 2]);     // S52 PASTRK01 say frefix a 't'
                SPRINTF(s, "%02i:%02i", timeHH, timeMM);  // ISO say HH:MM

                //_renderTXTAA(obj, ppt[i*3 + 0], ppt[i*3 + 1], bsize, weight, s);
                //_renderTXTAA(obj, ppt[i*3 + 0]+uoffs, ppt[i*3 + 1]-voffs, bsize, weight, s);
                _renderTXTAA(obj, color, ppt[i*3 + 0]+uoffs, ppt[i*3 + 1]-voffs, bsize, weight, s);
            }

            return TRUE;
        }

        if (0 == g_strcmp0("clrlin", S57_getName(geoData))) {
            double orient = ATAN2TODEG(ppt);
            double x = (ppt[3] + ppt[0]) / 2.0;
            double y = (ppt[4] + ppt[1]) / 2.0;

            gchar s[80];

            if (orient < 0) orient += 360.0;

            SPRINTF(s, "%s %03.f", str, orient);

            //_renderTXTAA(obj, x, y, bsize, weight, s);
            //_renderTXTAA(obj, x+uoffs, y-voffs, bsize, weight, s);
            _renderTXTAA(obj, color, x+uoffs, y-voffs, bsize, weight, s);

            return TRUE;
        }

        if (0 == g_strcmp0("leglin", S57_getName(geoData))) {
            double orient = ATAN2TODEG(ppt);
            // mid point of leg
            double x = (ppt[3] + ppt[0]) / 2.0;
            double y = (ppt[4] + ppt[1]) / 2.0;

            gchar s[80];

            if (orient < 0) orient += 360.0;

            SPRINTF(s, "%03.f cog", orient);

            //_renderTXTAA(obj, x, y, bsize, weight, s);
            //_renderTXTAA(obj, x+uoffs, y-voffs, bsize, weight, s);
            _renderTXTAA(obj, color, x+uoffs, y-voffs, bsize, weight, s);

            return TRUE;
        }


        {   // other text (ex bridge)
            double cView_x = (_pmax.u + _pmin.u) / 2.0;
            double cView_y = (_pmax.v + _pmin.v) / 2.0;
            double dmin    = INFINITY;
            double xmin, ymin;

            // find segment's center point closess to view center
            // FIXME: clip segments to view
            for (guint i=0; i<npt; ++i) {
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
                //_renderTXTAA(obj, xmin+uoffs, ymin-voffs, bsize, weight, str);
                _renderTXTAA(obj, color, xmin+uoffs, ymin-voffs, bsize, weight, str);
            }
        }

        return TRUE;
    }

    if (AREAS_T == S57_getObjtype(geoData)) {

        _computeCentroid(geoData);

        for (guint i=0; i<_centroids->len; ++i) {
            pt3 *pt = &g_array_index(_centroids, pt3, i);

            //_renderTXTAA(obj, pt->x+uoffs, pt->y-voffs, bsize, weight, str);
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
// VBO: collect all vectors for 1 colour
{
    // Assume: a width of 1 unit is 1 pixel.
    // WARNING: offet might need adjustment since bounding box
    // doesn't include line tickness.
    // Note: Pattern upto PLib 3.2, use a line width of 1.
    // Note: transparency: 0=0%(opaque), 1=25%, 2=50%, 3=75%

    // FIXME: instruction EP (Edge Polygon), AA (Arc Angle) and SC (Symbol Call)
    //        are not used in current PLib/Chart-1, so not implemented.

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

    // skip first token if it's S52_VC_NEW
    //if (S52_VC_NEW == vcmd)

    // debug - check if more than one NEW token - YES
    while (S52_VC_NEW == vcmd) {
        vcmd = S52_PL_getNextVOCmd(vecObj);
        //g_assert(0);
    }

    while ((S52_VC_NONE!=vcmd) && (S52_VC_NEW!=vcmd)) {

        switch (vcmd) {

            case S52_VC_NONE: break; //continue;

            case S52_VC_NEW:  break;

            case S52_VC_SW: { // this mean there is a change in pen width
#if !defined(S52_USE_OPENGL_VBO)
                // draw vertex with previous pen width
                GArray *v = S57_getPrimVertex(vertex);
                if (0 < v->len) {
                    char pen_w = S52_PL_getVOwidth(vecObj);
                    _DrawArrays(vertex);
                    S57_initPrim(vertex); //reset
                    glLineWidth(pen_w - '0');
                    //glLineWidth(pen_w - '0' - 0.5);
                    _glPointSize(pen_w - '0');
                }
#endif
                break;
            }

            // NOTE: entering poly mode fill a circle (CI) when a CI command
            // is found between PM0 and PM2
            case S52_VC_PM: // poly mode PM0/PM2, fill disk when not in PM
                fillMode = (GLU_FILL==fillMode) ? GLU_SILHOUETTE : GLU_FILL;
                //fillMode = GLU_FILL;

                break;

            case S52_VC_CI: {  // circle --draw immediatly
                GLdouble  radius = S52_PL_getVOradius(vecObj);
                GArray   *vec    = S52_PL_getVOdata(vecObj);
                vertex_t *data   = (vertex_t *)vec->data;
                GLint     slices = 32;
                GLint     loops  = 1;
                GLdouble  inner  = 0.0;        // radius

                GLdouble  outer  = radius;     // in 0.01mm unit

                // pass 'vertex' via global '_diskPrimTmp' used by _gluDisk()
                _diskPrimTmp = vertex;

#ifdef  S52_USE_OPENGL_VBO
                // compose symb need translation at render-time
                // (or add offset everything afterward!)
                //_glBegin(_QUADRIC_TRANSLATE, vertex);
                _glBegin(_TRANSLATE, vertex);
                _vertex3f(data, vertex);
                _glEnd(vertex);
#else
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslated(data[0], data[1], data[2]);
#endif

#ifdef  S52_USE_OPENGL_VBO
                if (GLU_FILL == fillMode) {
                    // FIXME: optimisation, draw a point instead of a filled disk
                    _gluQuadricDrawStyle(_qobj, GLU_FILL);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                } else {  //LINE
                    _gluQuadricDrawStyle(_qobj, GLU_LINE);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                }
#else
                if (GLU_FILL == fillMode) {
                    // FIXME: optimisation, draw a point instead of a filled disk
                    gluQuadricDrawStyle(_qobj, GLU_FILL);
                    gluDisk(_qobj, inner, outer, slices, loops);
                }
#endif
                // finish with tmp buffer
                _diskPrimTmp = NULL;


#if !defined( S52_USE_OPENGL_VBO)
                // when in fill mode draw outline (antialias)
                gluQuadricDrawStyle(_qobj, GLU_SILHOUETTE);
                //gluQuadricDrawStyle(_qobj, GLU_LINE);
                gluDisk(_qobj, inner, outer, slices, loops);
                glPopMatrix();
#endif

                CI = TRUE;
                //continue;
                break;
            }

            case S52_VC_FP: { // fill poly immediatly
                //PRINTF("fill poly: start\n");
                GArray   *vec  = S52_PL_getVOdata(vecObj);
                vertex_t *data = (vertex_t *)vec->data;

                // circle is already filled
                if (TRUE == CI) {
                    CI = FALSE;
                    break;
                }

                // remember first coord
                //fristCoord = data;
                _g_ptr_array_clear(_tmpV);

                gluTessBeginPolygon(_tobj, vertex);
                gluTessBeginContour(_tobj);
//#ifdef S52_USE_GLES2
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
                //glEnable( GL_BLEND );
                //glEnable( GL_LINE_SMOOTH );
                //glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
                //DrawObjects();
                //glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                //glDisable( GL_LINE_SMOOTH );
                //glDisable( GL_BLEND );


                //S57_initPrim(vertex); //reset
                //continue;
                break;
            }

            case S52_VC_PD:    // pen down
            case S52_VC_PU: {  // pen up
                GArray   *vec  = S52_PL_getVOdata(vecObj);
                //vertex_t *data = (vertex_t *)vec->data;

                // paranoi
                if (0 == vec->len) {
                    PRINTF("Vector Object Name: %s  Command: %c - NO VECTOR!\n", S52_PL_getVOname(vecObj), vcmd);
                    g_assert(0);
                    break;
                }

                //*
                // split STRIP into LINES
                //if (1 < vec->len)
                {
                    _glBegin(GL_LINES,  vertex);
                    while ((S52_VC_PD==vcmd) || (S52_VC_PU==vcmd)) {
                        GArray   *vec  = S52_PL_getVOdata(vecObj);
                        vertex_t *data = (vertex_t *)vec->data;

                        // failsafe
                        if (0 == vec->len)
                            continue;

                        // POINTS
                        //if (1 == vec->len)
                        //    break;
                        // draw points as a short line so one glDraw call
                        // to render all: points, lines, strip to save a lone glDraw point call
                        if (1 == vec->len) {
                            vertex_t data1[3] = {data[0]+20.0, data[1]+20.0, 0.0};
                            _vertex3f(data, vertex);
                            _vertex3f(data1, vertex);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                            continue;
                        }

                        // LINES
                        if (2 == vec->len) {
                            _vertex3f(data+0, vertex);
                            //data+=3;
                            _vertex3f(data+3, vertex);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                            continue;
                        }

                        // split STRIP into LINES
                        //for (guint i=0; i<vec->len-1; ++i, data+=3) {
                        for (guint i=0; (i+1)<vec->len; ++i, data+=3) {
                            _vertex3f(data+0, vertex);
                            _vertex3f(data+3, vertex);
                        }
                        vcmd = S52_PL_getNextVOCmd(vecObj);
                    }
                    _glEnd(vertex);

                    // vcmd is set, go to the outer while
                    continue;
                }
                /*
                else {
                    _glBegin(GL_POINTS, vertex);
                    while ((S52_VC_PD==vcmd) || (S52_VC_PU==vcmd)) {
                        GArray   *vec  = S52_PL_getVOdata(vecObj);
                        vertex_t *data = (vertex_t *)vec->data;

                        // not POINTS
                        if (1 != vec->len)
                            break;

                        _vertex3f(data, vertex);
                        vcmd = S52_PL_getNextVOCmd(vecObj);
                    }
                    _glEnd(vertex);
                    continue;
                }
                //*/

                /*
                // accumulate LINES segment
                if (2 == vec->len) {
                    _glBegin(GL_LINES, vertex);

                    _vertex3f(data, vertex);
                    data+=3;
                    _vertex3f(data, vertex);

                    vcmd = S52_PL_getNextVOCmd(vecObj);
                    while ((S52_VC_PD==vcmd) || (S52_VC_PU==vcmd)) {
                        GArray   *vec  = S52_PL_getVOdata(vecObj);
                        vertex_t *data = (vertex_t *)vec->data;

                        // LINES
                        if (2 == vec->len) {
                            _vertex3f(data, vertex);
                            data+=3;
                            _vertex3f(data, vertex);
                            vcmd = S52_PL_getNextVOCmd(vecObj);
                        } else {
                            break;  // not LINES, break out of the inner while
                        }
                    }
                    _glEnd(vertex);

                    // vcmd is set, go to the outer while
                    continue;
                }
                //*/

                /*
                if (1 == vec->len) {
                    _glBegin(GL_POINTS,     vertex);
                } else {
                    _glBegin(GL_LINE_STRIP, vertex);
                }
                //*/

/*
//#ifdef  S52_USE_GLES2
#ifdef S52_USE_GL2
                for (guint i=0; i<vec->len; ++i, data+=3) {
                    _vertex3f(data, vertex);
                }
#else
                for (guint i=0; i<vec->len; ++i, data+=3) {
                    _vertex3d(data, vertex);
                }
#endif
                // close loop by inserting last pen position
                // before entering polygon mode
                // this is to fix OBSTRN11 line on it side
                // other symbole (ex BOYLAT) have extra vector to close the loop
                if (NULL != fristCoord) {
                    unsigned int idx = (vec->len-1) * 3;
                    if (fristCoord[0] == data[idx] && fristCoord[1] == data[idx + 1]) {
                        PRINTF("WARNING: same coord (%7.1f,%7.1f): %s\n", fristCoord[0], fristCoord[1], S52_PL_getVOname(vecObj));
                    } else {
                        _vertex3f(fristCoord, vertex);
                        fristCoord = NULL;
                    }
                }

                _glEnd(vertex);

                //_DrawArrays(vertex);
                //S57_initPrim(vertex); //reset
                //continue;
                break;
*/
            }

            // should not get here since these command
            // have already been filtered out in _filterVector()
            case S52_VC_ST: // transparancy
            case S52_VC_SP: // color
            case S52_VC_SC: // symbol call

            // not used in PLib --not implemented
            case S52_VC_AA: // arc

            default:
                PRINTF("ERROR: invalid vector command: (%c)\n", vcmd);
                g_assert(0);
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

    S52_cmdDef    *cmdDef = (S52_cmdDef*)value;
    S52_DListData *DL     = S52_PL_getDLData(cmdDef);

    //PRINTF("PATTERN: %s\n", (char*)key);

    // is this symbol need to be re-created (update PLib)
    if (FALSE == DL->create)
        return FALSE;

#ifdef  S52_USE_OPENGL_VBO
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        // NOTE: DL->nbr is 1 - all pattern in PLib have only one color
        // but this is not in S52 specs (check this)
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        PRINTF("TODO: debug this code path\n");
        g_assert(0);
    }

    // debug
    if (MAX_SUBLIST < DL->nbr) {
        PRINTF("ERROR: sublist overflow -1-\n");
        g_assert(0);
    }

#else   // S52_USE_OPENGL_VBO

#ifndef S52_USE_OPENGL_SC
    // can't delete a display list - no garbage collector in GL SC
    // first delete DL
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        glDeleteLists(DL->vboIds[0], DL->nbr);
        DL->vboIds[0] = 0;
    }
#endif

    // then create new one
    DL->vboIds[0] = glGenLists(DL->nbr);
    if (0 == DL->vboIds[0]) {
        PRINTF("ERROR: glGenLists() failed .. exiting\n");
        g_assert(0);
    }
    glNewList(DL->vboIds[0], GL_COMPILE);

#endif  // S52_USE_OPENGL_VBO

    S52_vec *vecObj = S52_PL_initVOCmd(cmdDef);

    // set default
    //glLineWidth(1.0);

    if (NULL == DL->prim[0])
        DL->prim[0]  = S57_initPrim(NULL);
    else {
        PRINTF("ERROR: DL->prim[0] not NULL\n");
        g_assert(0);
    }

#ifdef  S52_USE_OPENGL_VBO
    // using VBO we need to keep some info (mode, first, count)
    DL->prim[0]   = _parseHPGL(vecObj, DL->prim[0]);
    DL->vboIds[0] = _VBOCreate(DL->prim[0]);

    // set normal mode
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#else   // S52_USE_OPENGL_VBO

    DL->prim[0] = _parseHPGL(vecObj, DL->prim[0]);
    glEndList();

#endif  // S52_USE_OPENGL_VBO

    {   // save pen width
        char pen_w = S52_PL_getVOwidth(vecObj);
        S52_PL_setLCdata((S52_cmdDef*)value, pen_w);
    }


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

    S52_cmdDef    *cmdDef = (S52_cmdDef*)value;
    S52_DListData *DL     = S52_PL_getDLData(cmdDef);

    // is this symbol need to be re-created (update PLib)
    if (FALSE == DL->create)
        return FALSE;

    // debug
    if (MAX_SUBLIST < DL->nbr) {
        PRINTF("ERROR: sublist overflow -0-\n");
        g_assert(0);
    }

#ifdef  S52_USE_OPENGL_VBO
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        PRINTF("TODO: debug this code path\n");
        g_assert(0);
    }

    //glGenBuffers(DL->nbr, &DL->vboIds[0]);

#else

#ifndef S52_USE_OPENGL_SC
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
    glLineWidth(1.0);
    //_glPointSize(1.0);

    S52_vec *vecObj  = S52_PL_initVOCmd(cmdDef);

    for (guint i=0; i<DL->nbr; ++i) {
        if (NULL == DL->prim[i])
            DL->prim[i]  = S57_initPrim(NULL);
        else {
            PRINTF("ERROR: DL->prim[i] not NULL\n");
            g_assert(0);
        }
    }

#ifdef  S52_USE_OPENGL_VBO
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


    {   // save some data for later
        char pen_w = S52_PL_getVOwidth(vecObj);
        S52_PL_setLCdata((S52_cmdDef*)value, pen_w);
    }

    _checkError("_buildSymbDL()");

    S52_PL_doneVOCmd(vecObj);

    return 0; // 0 continue traversing
}

static GLint     _createSymb(void)
// WARNING: this must be done from inside the main loop!
{

    if (TRUE == _symbCreated)
        return TRUE;

    // FIXME: what if the screen is to small !?
    //glGetIntegerv(GL_VIEWPORT, _vp);

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

    PRINTF("INFO: PLib sym created ..\n");

    return TRUE;
}

int        S52_GL_isSupp(S52_obj *obj)
// TRUE if display of object is suppressed
// also collect stat
{
    if (S52_SUP_ON == S52_PL_getToggleState(obj)) {
        ++_oclip;
        return TRUE;
    }

    // SCAMIN
    if (TRUE == S52_MP_get(S52_MAR_SCAMIN)) {
        S57_geo *geo  = S52_PL_getGeo(obj);
        double scamin = S57_getScamin(geo);

        if (scamin < _SCAMIN) {
            ++_oclip;
            return TRUE;
        }
    }

    return FALSE;
}

int        S52_GL_isOFFscreen(S52_obj *obj)
// TRUE if object not in view
{
    /*
    // debug: CHKSYM01 land here because it is on layer 8, other use layer 9
    S57_geo *geo  = S52_PL_getGeo(obj);
    if (0 == g_strcmp0("$CSYMB", S57_getName(geo))) {

        // this is just to quiet this the PRINTF msg
        // as CHKSYM01/BLKADJ01 pass here (this is normal)
        GString *attval = S57_getAttVal(geo, "$SCODE");
        if (0 == g_strcmp0(attval->str, "CHKSYM01"))
            return FALSE;
        if (0 == g_strcmp0(attval->str, "BLKADJ01"))
            return FALSE;

        PRINTF("%s:%s\n", S57_getName(geo), attval->str);

        return FALSE;
    }
    */

    {   // geo extent _gmin/max
        double x1,y1,x2,y2;
        S57_geo *geo = S52_PL_getGeo(obj);
        S57_getExt(geo, &x1, &y1, &x2, &y2);

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
    }

    return FALSE;
}

#if 0
static guint     _minPOT(guint value)
// min POT greater than 'value' - simplyfie texture handling
// compare to _nearestPOT() but use more GPU memory
{
    int i = 1;

    if (value == 0) return -1;      // Error!

    for (;;) {
        if (value == 0) return i;
        value >>= 1;
        i *= 2;
    }
}
#endif

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
static int       _newTexture(S52_GL_ras *raster)
// copy and blend raster 'data' to alpha texture
// FIXME: test if the use of hader to blend rather than precomputing value here is faster
{
    //guint potX  = _minPOT(raster->w);
    //guint potY  = _minPOT(raster->h);
    guint npotX = raster->w;
    guint npotY = raster->h;
    //potX = npotX;
    //potY = npotY;

    float min  =  INFINITY;
    float max  = -INFINITY;

    float safe = S52_MP_get(S52_MAR_SAFETY_CONTOUR) * -1.0;  // change signe
    //unsigned char safe = 100;

    float *dataf = (float*) raster->data;
    //unsigned char *datab = (unsigned char *) raster->data;

    guint count = raster->w * raster->h;
    unsigned char *texAlpha = g_new0(unsigned char, count * 4);
    struct rgba {unsigned char r,g,b,a;};
    struct rgba *texTmp   = (struct rgba*) texAlpha;

    for (guint i=0; i<count; ++i) {
        //int Yline = i/raster->w;
        //int k     = i - Yline*raster->w;
        //int ii    = Yline*potX + k;

        //if (raster->nodata == dataf[i]) {
        if (-9999 == dataf[i]) {
            //texTmp[ii] = 0;
            texTmp[i].a = 0;
            continue;
        }

        min = MIN(dataf[i], min);
        max = MAX(dataf[i], max);
        //min = MIN(datab[i], min);
        //max = MAX(datab[i], max);

        if ((safe/2.0) <= dataf[i]) {
        //if ((safe/2.0) <= datab[i]) {
            // debug
            //texTmp[Yline*potX + k] = 100;

            // OK
            //texTmp[ii] = 0;
            texTmp[i].a = 0;
            continue;
        }
        if (safe <= dataf[i]) {
        //if (safe <= datab[i]) {
            //texTmp[ii] = 255;
            texTmp[i].a = 255;
            continue;
        }
        if ((safe-2.0) <= dataf[i]) {
        //if ((safe-2.0) <= datab[i]) {
            //texTmp[ii] = 100;
            texTmp[i].a = 100;
            continue;
        }
    }

    raster->min      = min;
    raster->max      = max;
    raster->npotX    = npotX;
    raster->npotY    = npotY;
    raster->texAlpha = texAlpha;

    return TRUE;
}

int        S52_GL_drawRaster(S52_GL_ras *raster)
{
    // bailout if not in view
    if ((raster->E < _pmin.u) || (raster->S < _pmin.v) || (raster->W > _pmax.u) || (raster->N > _pmax.v)) {
        return TRUE;
    }

    if (0 == raster->texID) {
        if (TRUE == raster->isRADAR) {
            glGenTextures(1, &raster->texID);
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, raster->npotX, raster->npotY, 0, GL_ALPHA, GL_UNSIGNED_BYTE, raster->texAlpha);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glBindTexture(GL_TEXTURE_2D, 0);

            _checkError("S52_GL_drawRaster() -1.0-");
        } else {
            // raster
            _newTexture(raster);

            glGenTextures(1, &raster->texID);
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
            //glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, raster->potX, raster->potY, 0, GL_ALPHA, GL_UNSIGNED_BYTE, raster->texAlpha);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY, 0, GL_RGBA, GL_UNSIGNED_BYTE, raster->texAlpha);

            //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, raster->npotX, raster->npotY, 0, GL_RGBA, GL_FLOAT, raster->data);
            //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_FLOAT, raster->data);
            //glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, raster->npotX, raster->npotY, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, raster->data);
            //dword PackValues(byte x,byte y,byte z,byte w) {
            //    return (x<<24)+(y<<16)+(z<<8)+(w);
            //}

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
        // update RADAR
        if (TRUE == raster->isRADAR) {
            glBindTexture(GL_TEXTURE_2D, raster->texID);

            // GLES2/XOOM ALPHA fail and if not POT
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, raster->npotX, raster->npotY, GL_ALPHA, GL_UNSIGNED_BYTE, raster->texAlpha);

            _checkError("S52_GL_drawRaster() -3.0-");
        }
    }

    // set radar extent
    if (TRUE == raster->isRADAR) {
        raster->S = raster->cLat - raster->rNM * 1852.0;
        raster->W = raster->cLng - raster->rNM * 1852.0;
        raster->N = raster->cLat + raster->rNM * 1852.0;
        raster->E = raster->cLng + raster->rNM * 1852.0;
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

    _glUniformMatrix4fv_uModelview();

    // to fit an image in a POT texture
    //float fracX = (float)raster->w/(float)raster->potX;
    //float fracY = (float)raster->h/(float)raster->potY;
    float fracX = 1.0;
    float fracY = 1.0;
    vertex_t ppt[4*3 + 4*2] = {
        raster->W, raster->S, 0.0,        0.0f,  0.0f,
        raster->E, raster->S, 0.0,        fracX, 0.0f,
        raster->E, raster->N, 0.0,        fracX, fracY,
        raster->W, raster->N, 0.0,        0.0f,  fracY
    };

    // FIXME: need this for bathy
    glDisable(GL_CULL_FACE);

    glEnableVertexAttribArray(_aUV);
    glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &ppt[3]);

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    glUniform1f(_uStipOn, 1.0);
    glBindTexture(GL_TEXTURE_2D, raster->texID);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    _checkError("S52_GL_drawRaster() -4-");

    glBindTexture(GL_TEXTURE_2D,  0);

    glUniform1f(_uStipOn, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    // FIXME: need this for bathy
    glEnable(GL_CULL_FACE);

    return TRUE;
}
#endif  // S52_USE_GLES2


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
    //if (0 == strcmp("UNSARE", S52_PL_getOBCL(obj))) {
    //    PRINTF("UNSARE found\n");
    //    //return;
    //}
    //if (0 == strcmp("$CSYMB", S52_PL_getOBCL(obj))) {
    //    PRINTF("UNSARE found\n");
    //    //return;
    //}
    //if (S52_GL_PICK == _crnt_GL_cycle) {
    //    S57_geo *geo = S52_PL_getGeo(obj);
    //    GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //    if (0==strcmp("2135161787", FIDNstr->str)) {
    //        PRINTF("%s\n", FIDNstr->str);
    //    }
    //}
    //S57_geo *geo = S52_PL_getGeo(obj);
    //PRINTF("drawing geo ID: %i\n", S57_getGeoID(geo));
    //if (2184==S57_getGeoID(geo)) {
    //if (899 == S57_getGeoID(geo)) {
    //if (103 == S57_getGeoID(geo)) {  // Hawaii ISODNG
    //    PRINTF("found %i XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n", S57_getGeoID(geo));
    ////    return TRUE;
    //}
    //if (S52_GL_PICK == _crnt_GL_cycle) {
    //    if (0 == strcmp("PRDARE", S52_PL_getOBCL(obj))) {
    //        PRINTF("PRDARE found\n");
    //    }
    //}
    //------------------------------------------------------


    if (S52_GL_PICK == _crnt_GL_cycle) {
        ++_cIdx.color.r;
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
        // FIXME: optimisation - read only once all draw to get the top obj
        // FIXME: copy to texture then test pixels!
        // FB --> TEX
        //glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 8, 8, 0);
        // TEX --> MEM
        // gl...
        // -OR-
        // a shader trick! .. put the pixels back in an array then back to main!!


        // WARNING: some graphic card preffer RGB / BYTE .. YMMV
        //glReadPixels(_vp[0], _vp[1], 8, 8, GL_RGB, GL_UNSIGNED_BYTE, &_pixelsRead);
        glReadPixels(_vp[0], _vp[1], 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, &_pixelsRead);

        _checkError("S52_GL_draw():glReadPixels()");

        for (int i=0; i<(8*8); ++i) {
            if (_pixelsRead[i].color.r == _cIdx.color.r) {
                g_ptr_array_add(_objPick, obj);

                // debug
                PRINTF("pixel found (%i, %i): i=%i color=%X\n", _vp[0], _vp[1], i, _cIdx.color.r);
                break;
            }
        }

        // debug - dump pixel
        /*
        for (int i=7; i>=0; --i) {
            int j = i*8;
            printf("%i |%X %X %X %X %X %X %X %X|\n", i,
                   _pixelsRead[j+0].color.r, _pixelsRead[j+1].color.r, _pixelsRead[j+2].color.r,
                   _pixelsRead[j+3].color.r, _pixelsRead[j+4].color.r, _pixelsRead[j+5].color.r,
                   _pixelsRead[j+6].color.r, _pixelsRead[j+7].color.r;
        }
        printf("finish with object: cIdx: %X\n", _cIdx.idx);
        printf("----------------------------\n");
        */
    }


    return TRUE;
}

static int       _doProjection(double centerLat, double centerLon, double rangeDeg)
{
    //if (isnan(centerLat) || isnan(centerLon) || isnan(rangeDeg))
    //    return FALSE;

    pt3 NE = {0.0, 0.0, 0.0};  // Nort/East
    pt3 SW = {0.0, 0.0, 0.0};  // South/West

    NE.y = centerLat + rangeDeg;
    SW.y = centerLat - rangeDeg;
    NE.x = SW.x = centerLon;

#ifdef S52_USE_PROJ
    if (FALSE == S57_geo2prj3dv(1, (double*)&NE))
        return FALSE;
    if (FALSE == S57_geo2prj3dv(1, (double*)&SW))
        return FALSE;
#endif

    {
        // get drawing area in pixel
        // assume round pixel for now
        int w = _vp[2];  // width  (pixels)
        int h = _vp[3];  // hieght (pixels)
        // screen ratio
        double r = (double)h / (double)w;   // > 1 'h' dominant, < 1 'w' dominant
        //PRINTF("Viewport pixels (width x height): %i %i (r=%f)\n", w, h, r);
        double dy = NE.y - SW.y;
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
    _scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
    _scaley = (_pmax.v - _pmin.v) / (double)_vp[3];

    return TRUE;
}

int        S52_GL_begin(S52_GL_cycle cycle)
{
    CHECK_GL_END;
    _GL_BEGIN = TRUE;

    // GL sanity check before start of init
    _checkError("S52_GL_begin() -0-");

    if (S52_GL_NONE != _crnt_GL_cycle) {
        PRINTF("WARNING: S52_GL_cycle out of sync\n");
        g_assert(0);
    }
    _crnt_GL_cycle = cycle;

    // optimisation
    _identity_MODELVIEW = FALSE;
    _identity_MODELVIEW_cnt = 0;

    S52_GL_init();

//#if !defined(S52_USE_GLES2)
#ifndef S52_USE_GL2

    // FALSE: enable GLSL gl_PointCood
    glEnable(GL_POINT_SPRITE);
#endif

    // ATI
    //_dumpATImemInfo(VBO_FREE_MEMORY_ATI);          // 0x87FB
    //_dumpATImemInfo(TEXTURE_FREE_MEMORY_ATI);      // 0x87FC
    //_dumpATImemInfo(RENDERBUFFER_FREE_MEMORY_ATI); // 0x87FD

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


#ifdef S52_USE_TEGRA2
    // Nvidia - GetIntegerv
    // GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX          0x9047
    // GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX    0x9048
    // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX  0x9049
    // GPU_MEMORY_INFO_EVICTION_COUNT_NVX            0x904A
    // GPU_MEMORY_INFO_EVICTED_MEMORY_NVX            0x904B
#endif

#ifdef S52_USE_COGL
    cogl_begin_gl();
#endif

//#if !defined(S52_USE_GLES2)
#ifndef S52_USE_GL2
    glPushAttrib(GL_ALL_ATTRIB_BITS);
#endif

    glEnable(GL_BLEND);

    // GL_FUNC_ADD, GL_FUNC_SUBTRACT, or GL_FUNC_REVERSE_SUBTRACT
    //glBlendEquation(GL_FUNC_ADD);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // transparency
    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    //glSampleCoverage(1,  GL_FALSE);


    //glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    //glHint(GL_POINT_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);


    // FIXME: GLES2 or GL2
//#if !defined(S52_USE_GLES2)
#ifndef S52_USE_GL2
    glAlphaFunc(GL_ALWAYS, 0);

    // LINE, GL_LINE_SMOOTH_HINT - NOT in GLES2
    glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
    //glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // POLY (not used)
    //glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);
#endif


    // picking or rendering cycle
    if (S52_GL_PICK == _crnt_GL_cycle) {
        _cIdx.color.r = 0;
        _cIdx.color.g = 0;
        _cIdx.color.b = 0;
        _cIdx.color.a = 0;

        g_ptr_array_set_size(_objPick, 0);

        // make sure that color are not messed up
        //glDisable(GL_POINT_SMOOTH);

        //glDisable(GL_LINE_SMOOTH);     // NOT in GLES2
        //glDisable(GL_POLYGON_SMOOTH);  // NOT in "OpenGL ES SC"
        glDisable(GL_BLEND);
        //glDisable(GL_DITHER);          // NOT in "OpenGL ES SC"
        //glDisable(GL_FOG);             // NOT in "OpenGL ES SC", NOT in GLES2
        //glDisable(GL_LIGHTING);        // NOT in GLES2
        //glDisable(GL_TEXTURE_1D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_TEXTURE_2D);      // fail on GLES2
        //glDisable(GL_TEXTURE_3D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_ALPHA_TEST);      // NOT in GLES2

        //glShadeModel(GL_FLAT);         // NOT in GLES2

    } else {
        if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
            glEnable(GL_BLEND);

#ifdef S52_USE_GL1
            // NOTE: point smoothing is ugly with point pattern
            //glEnable(GL_POINT_SMOOTH);

            glEnable(GL_LINE_SMOOTH);
            glEnable(GL_ALPHA_TEST);
#endif

        } else {
            // need to explicitly disable since
            // OpenGL state stay the same from draw to draw
            glDisable(GL_BLEND);

#ifdef S52_USE_GL1
            glDisable(GL_POINT_SMOOTH);
            glDisable(GL_LINE_SMOOTH);
            glDisable(GL_ALPHA_TEST);
#endif
        }
    }

    //_checkError("S52_GL_begin() -2-");

    //glPushAttrib(GL_ENABLE_BIT);          // NOT in OpenGL ES SC
    //glDisable(GL_NORMALIZE);              // NOT in GLES2

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);


#if !defined(S52_USE_OPENGL_SC)
    glDisable(GL_DITHER);                   // NOT in OpenGL ES SC
#endif

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    // FrontFaceDirection
    //glFrontFace(GL_CW);
    glFrontFace(GL_CCW);  // default

    //glCullFace(GL_BACK);  // default

    // FIXME: need to clock this more accuratly
    // Note: Mesa RadeonHD & Xoom (TEGRA2) Android 4.4.2 a bit slower with cull face!
    // also make little diff on Nexus with current timming method (not accurate)
    glEnable(GL_CULL_FACE);
    //glDisable(GL_CULL_FACE);

    // EnableCap
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);

    _checkError("S52_GL_begin() - EnableCap");

#else
    glShadeModel(GL_FLAT);                       // NOT in GLES2
    // draw both side
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // NOT in OpenGL ES SC
    glEnableClientState(GL_VERTEX_ARRAY);
#endif



    // -------------------- F I X M E (cleanup this mess) --------
    // FIX: check if this can be done when realize() is called
    //
    // now that the main loop and projection are up ..
    //

    // check if an initial call S52_GL_setViewPort()
    //if (0.0==_vp[0] && 0.0==_vp[1] && 0.0==_vp[2] && 0.0==_vp[3]) {
    if (0==_vp[0] && 0==_vp[1] && 0==_vp[2] && 0==_vp[3]) {

        // a viewport cant only have positive, but GL ask for int
        glGetIntegerv(GL_VIEWPORT, (GLint*)_vp);
        PRINTF("WARNING: Viewport default to (%i,%i,%i,%i)\n",
               _vp[0], _vp[1], _vp[2], _vp[3]);
    }

    glViewport(_vp[0], _vp[1], _vp[2], _vp[3]);

    // set projection
    // FIXME: check if viewing nowhere (occur at init time)
    //if (INFINITY==_pmin.u || INFINITY==_pmin.v || -INFINITY==_pmax.u || -INFINITY==_pmax.v) {
    //    PRINTF("ERROR: view extent not set\n");
    //    return FALSE;
    //}

    // do projection if draw() since the view is the same for all other mode
    if (S52_GL_DRAW == _crnt_GL_cycle) {
        // this will setup _pmin/_pmax, need a valide _vp
        _doProjection(_centerLat, _centerLon, _rangeNM/60.0);
    }

    // then create all PLib symbol
    _createSymb();

    // _createSymb() might reset line width
    // FIXME: linewidth sould be set *before* rendering any line
    glLineWidth(1.0);

    // -------------------------------------------------------


    _SCAMIN = _computeSCAMIN() * 10000.0;

    if (_fb_pixels_size < (_vp[2] * _vp[3] * _fb_format) ) {
        PRINTF("ERROR: pixels buffer overflow: fb_pixels_size=%i, VP=%i \n", _fb_pixels_size, (_vp[2] * _vp[3] * _fb_format));
        // NOTE: since the assert() is removed in the release, draw last can
        // still be called (but does notting) if _fb_pixels is NULL
        g_free(_fb_pixels);
        _fb_pixels = NULL;

        g_assert(0);
    }

#ifndef S52_USE_GV
    // GV set the matrix it self
    // also don't erase GV stuff with NODATA
    /*
    if (TRUE != drawLast) {
        _glMatrixSet(VP_PRJ);
    } else {
        double northtmp = _north;
        _north = 0.0;
        _glMatrixSet(VP_PRJ);
        _north = northtmp;
    }
    */
    _glMatrixSet(VP_PRJ);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glActiveTexture(GL_TEXTURE0);
#else
    glGetDoublev(GL_MODELVIEW_MATRIX,  _mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, _pjm);
#endif

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    //-- update background ------------------------------------------
    if (S52_GL_DRAW == _crnt_GL_cycle) {
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
#endif

#if !defined(S52_USE_RADAR)
    } else {
        if (S52_GL_LAST == _crnt_GL_cycle) {
            // user can draw on top of base
            // then call drawLast repeatdly
            if (TRUE == _fb_update) {
                S52_GL_readFBPixels();
                _fb_update = FALSE;
            }

            // load FB that was filled with the previous draw() call
            S52_GL_drawFBPixels();
        }
#endif
    }
    //---------------------------------------------------------------

#endif  // S52_USE_GV

    _checkError("S52_GL_begin() - fini");

    return TRUE;
}

int        S52_GL_end(S52_GL_cycle cycle)
//
{
    CHECK_GL_BEGIN;

    if (cycle != _crnt_GL_cycle) {
        PRINTF("WARNING: S52_GL_mode out of sync\n");
        g_assert(0);
    }
    _crnt_GL_cycle = cycle;


//#if !defined(S52_USE_GLES2)
#ifndef S52_USE_GL2
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopAttrib();     // NOT in OpenGL ES SC
#endif

#ifdef S52_USE_COGL
    cogl_end_gl();
#endif

    _glMatrixDel(VP_PRJ);

    // texture of FB need update
    if (S52_GL_DRAW == _crnt_GL_cycle) {
        _fb_update = TRUE;
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

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    //PRINTF("SKIP POIN_T glDrawArrays(): nFragment = %i\n", _nFrag);
    //PRINTF("SKIP identity = %i\n", _identity_MODELVIEW_cnt);
    //PRINTF("DEPARE = %i, TOTAL AC = %i\n", _depare, _nAC);
#endif

    _crnt_GL_cycle = S52_GL_NONE;

    _GL_BEGIN = FALSE;

    _checkError("S52_GL_end() -fini-");

    return TRUE;
}

int        S52_GL_del(S52_obj *obj)
// delete geo Display List associate to an object
{
    S57_geo  *geoData = S52_PL_getGeo(obj);
    S57_prim *prim    = S57_getPrimGeo(geoData);

#ifdef S52_USE_OPENGL_SC
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
        // delete VBO when program terminated
        if (GL_TRUE == glIsBuffer(vboID))
            glDeleteBuffers(1, &vboID);

        vboID = 0;
        S57_setPrimDList(prim, vboID);

#ifdef S52_USE_FREETYPE_GL
        {   // delete text
            guint len;
            guint vboID = S52_PL_getFreetypeGL_VBO(obj, &len);
            if (GL_TRUE == glIsBuffer(vboID))
                glDeleteBuffers(1, &vboID);

            len   = 0;
            vboID = 0;
            S52_PL_setFreetypeGL_VBO(obj, vboID, len);
        }
#endif

#else  // S52_USE_OPENGL_VBO
        // 'vboID' is in fact a DList here
        if (GL_TRUE == glIsList(vboID)) {
            glDeleteLists(vboID, 1);
        } else {
            PRINTF("WARNING: ivalid DL\n");
            g_assert(0);
        }
#endif  // S52_USE_OPENGL_VBO
    }

    _checkError("S52_GL_del()");

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

    glDeleteTextures(1, &raster->texID);
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

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    {
        const GLubyte *vendor     = glGetString(GL_VENDOR);
        const GLubyte *renderer   = glGetString(GL_RENDERER);
        const GLubyte *version    = glGetString(GL_VERSION);
        const GLubyte *slglver    = glGetString(GL_SHADING_LANGUAGE_VERSION);
        const GLubyte *extensions = glGetString(GL_EXTENSIONS);
        if (NULL == extensions) {
            PRINTF("ERROR: glGetString(GL_EXTENSIONS) is NULL\n");
            g_assert(0);
        }

        PRINTF("Vendor:     %s\n", vendor);
        PRINTF("Renderer:   %s\n", renderer);
        PRINTF("Version:    %s\n", version);
        PRINTF("Shader:     %s\n", slglver);
        PRINTF("Extensions: %s\n", extensions);

        // npot
        if (NULL != g_strrstr((const char *)extensions, "GL_OES_texture_npot"))
            PRINTF("DEBUG: GL_OES_texture_npot OK\n");
        else
            PRINTF("DEBUG: GL_OES_texture_npot FAILED\n");


        // marker
        if (NULL != g_strrstr((const char *)extensions, "GL_EXT_debug_marker"))
            PRINTF("DEBUG: GL_EXT_debug_marker OK\n");
        else
            PRINTF("DEBUG: GL_EXT_debug_marker FAILED\n");

    }
#else
    if (s <= 0)
        PRINTF("WARNING: no stencil buffer for pattern in GL1\n");

    GLboolean d = FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &d);
    PRINTF("double buffer: %s\n", ((TRUE==d) ? "TRUE" : "FALSE"));
#endif

    _checkError("_contextValid()");

    return TRUE;
}

int        S52_GL_init(void)
// return TRUE on success
{
    if (!_doInit) {
        //PRINTF("WARNING: S52 GL allready initialize!\n");
        return _doInit;
    }

    // juste checking
    if (sizeof(double) != sizeof(GLdouble)) {
        PRINTF("ERROR: sizeof(double) != sizeof(GLdouble)\n");
        g_assert(0);
    }

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

    // signal to (re)build S52 symbol
    //_symbCreated = FALSE;

    //_DEBUG = TRUE;

    // tmp buffer
    if (NULL == _tmpWorkBuffer)
        _tmpWorkBuffer = g_array_new(FALSE, FALSE, sizeof(vertex_t)*3);

#ifdef S52_USE_AFGLOW
    if (NULL == _aftglwColorArr)
        _aftglwColorArr = g_array_new(FALSE, FALSE, sizeof(unsigned char));

    // init vbo for color
    if (0 == _vboIDaftglwColrID)
        glGenBuffers(1, &_vboIDaftglwColrID);

    if (GL_FALSE == glIsBuffer(_vboIDaftglwColrID)) {
        PRINTF("ERROR: glGenBuffers() fail\n");
        g_assert(0);
        return FALSE;
    }

    // init VBO for vertex
    if (0 == _vboIDaftglwVertID)
        glGenBuffers(1, &_vboIDaftglwVertID);

    if (GL_FALSE == glIsBuffer(_vboIDaftglwVertID)) {
        PRINTF("ERROR: glGenBuffers() fail\n");
        g_assert(0);
        return FALSE;
    }
#endif

    _doInit = FALSE;

    return TRUE;
}

int        S52_GL_setDotPitch(int w, int h, int wmm, int hmm)
{
    _dotpitch_mm_x = (double)wmm / (double)w;
    _dotpitch_mm_y = (double)hmm / (double)h;

    // debug
    PRINTF("DOTPITCH(mm): X = %f, Y = %f\n", _dotpitch_mm_x, _dotpitch_mm_y);

    S52_MP_set(S52_MAR_DOTPITCH_MM_X, _dotpitch_mm_x);
    S52_MP_set(S52_MAR_DOTPITCH_MM_Y, _dotpitch_mm_y);

    _fb_pixels_size = w * h * _fb_format;
    _fb_pixels      = g_new0(unsigned char, _fb_pixels_size);

    return TRUE;
}

int        S52_GL_done(void)
{
    if (_doInit) return _doInit;

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
    if (0 != _vboIDaftglwColrID) {
        glDeleteBuffers(1, &_vboIDaftglwColrID);
        _vboIDaftglwColrID = 0;
    }
    if (0 != _vboIDaftglwVertID) {
        glDeleteBuffers(1, &_vboIDaftglwVertID);
        _vboIDaftglwVertID = 0;
    }
#endif

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    if (NULL != _tessWorkBuf_d) {
        g_array_free(_tessWorkBuf_d, TRUE);
        _tessWorkBuf_d = NULL;
    }
    if (NULL != _tessWorkBuf_f) {
        g_array_free(_tessWorkBuf_f, TRUE);
        _tessWorkBuf_f = NULL;
    }

    // done texture object
    glDeleteTextures(1, &_nodata_mask_texID);
    _nodata_mask_texID = 0;
    glDeleteProgram(_programObject);
    _programObject = 0;

    glDeleteFramebuffers(1, &_fboID);
    _fboID = 0;


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
        glDeleteBuffers(1, &_freetype_gl_textureID);
        _freetype_gl_textureID = 0;
    }
    if (NULL != _freetype_gl_buffer) {
        g_array_free(_freetype_gl_buffer, TRUE);
        _freetype_gl_buffer = NULL;
    }
#endif
#endif  // S52_USE_GLES2



#ifdef S52_USE_FTGL
#ifdef _MINGW
    if (NULL != _ftglFont[0])
        ftglDestroyFont(_ftglFont[0]);
    if (NULL != _ftglFont[1])
        ftglDestroyFont(_ftglFont[1]);
    if (NULL != _ftglFont[2])
        ftglDestroyFont(_ftglFont[2]);
    if (NULL != _ftglFont[3])
        ftglDestroyFont(_ftglFont[3]);
    _ftglFont = {NULL, NULL, NULL, NULL};
#endif
#endif

    _diskPrimTmp = S57_donePrim(_diskPrimTmp);

    if (NULL != _strPick) {
        g_string_free(_strPick, TRUE);
        _strPick = NULL;
    }

    _doInit = TRUE;

    return _doInit;
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
        PRINTF("ERROR: S52 GL not initialize\n");
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
        PRINTF("ERROR: S52 GL not initialize\n");
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
    _centerLat = centerLat;
    _centerLon = centerLon;
    _rangeNM   = rangeNM;
    _north     = north;

    return TRUE;
}

int        S52_GL_setViewPort(int x, int y, int width, int height)
{
    // NOTE: width & height are in fact GLsizei, a pseudo unsigned int
    // it is a 'int' that can't be negative

    // debug
    //glViewport(x, y, width, height);
    //glGetIntegerv(GL_VIEWPORT, _vp);
    //PRINTF("VP: %i, %i, %i, %i\n", _vp[0], _vp[1], _vp[2], _vp[3]);
    //_checkError("S52_GL_setViewPort()");

    _vp[0] = x;
    _vp[1] = y;
    _vp[2] = width;
    _vp[3] = height;

    if (_fb_pixels_size < (_vp[2] * _vp[3] * _fb_format) ) {
        _fb_pixels_size =  _vp[2] * _vp[3] * _fb_format;
        _fb_pixels      = g_renew(unsigned char, _fb_pixels, _fb_pixels_size);
        PRINTF("pixels buffer resized (%i)\n", _fb_pixels_size);
    }

    return TRUE;
}

int        S52_GL_getViewPort(int *x, int *y, int *width, int *height)
{
    //glGetIntegerv(GL_VIEWPORT, _vp);
    //_checkError("S52_GL_getViewPort()");

    *x      = _vp[0];
    *y      = _vp[1];
    *width  = _vp[2];
    *height = _vp[3];

    return TRUE;
}


const
char      *S52_GL_getNameObjPick(void)
{
    if (0 == _objPick->len) {
        PRINTF("WARNING: no S57 object found\n");
        return NULL;
    }

    if (NULL == _strPick)
        _strPick = g_string_new("");

    const    char *name  = NULL;
    unsigned int   S57ID = 0;

    PRINTF("----------- PICK(%i) ---------------\n", _objPick->len);

    for (guint i=0; i<_objPick->len; ++i) {
        S52_obj *obj = (S52_obj*)g_ptr_array_index(_objPick, i);
        S57_geo *geo = S52_PL_getGeo(obj);

        name  = S57_getName(geo);
        S57ID = S57_getGeoID(geo);

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
                    char  name[80];
                    const char *value = S52_PL_getCmdText(obj);

                    // debug
                    PRINTF("%s%i: %s\n", cmdType, nCmd, value);

                    // insert in Att
                    SPRINTF(name, "%s%i", cmdType, nCmd);
                    S57_setAtt(geo, name, value);
                    cmdType = NULL;
                }
                //*/

                cmdWrd = S52_PL_getCmdNext(obj);
                ++nCmd;
            }
        }

        S57_dumpData(geo, FALSE);
        PRINTF("-----------\n");
    }

    //SPRINTF(_strPick, "%s:%i", name, S57ID);
    g_string_printf(_strPick, "%s:%i", name, S57ID);

    //*
    // hightlight object at the top of the stack
    S52_obj *objhighlight = (S52_obj *)g_ptr_array_index(_objPick, _objPick->len-1);

    S57_geo *geo  = S52_PL_getGeo(objhighlight);
    S57_highlightON(geo);

#ifdef S52_USE_C_AGGR_C_ASSO
    // debug
    GString *geo_refs = S57_getAttVal(geo, "_LNAM_REFS_GEO");
    if (NULL != geo_refs)
        PRINTF("DEBUG:geo:_LNAM_REFS_GEO = %s\n", geo_refs->str);

    // get relationship obj
    S57_geo *geoRel = S57_getRelationship(geo);
    if (NULL != geoRel) {
        GString *geoRelIDs   = NULL;
        GString *geoRel_refs = S57_getAttVal(geoRel, "_LNAM_REFS_GEO");
        if (NULL != geoRel_refs)
            PRINTF("DEBUG:geoRel:_LNAM_REFS_GEO = %s\n", geoRel_refs->str);

        // parse Refs
        gchar  **splitRefs = g_strsplit_set(geoRel_refs->str, ",", 0);
        gchar  **topRefs   = splitRefs;

        while (NULL != *splitRefs) {
            S57_geo *geoRelAssoc = NULL;

            sscanf(*splitRefs, "%p", (void**)&geoRelAssoc);
            S57_highlightON(geoRelAssoc);

            guint idAssoc = S57_getGeoID(geoRelAssoc);

            if (NULL == geoRelIDs) {
                geoRelIDs = g_string_new("");
                g_string_printf(geoRelIDs, ":%i,%i", S57_getGeoID(geoRel), idAssoc);
            } else {
                g_string_append_printf(geoRelIDs, ",%i", idAssoc);
            }

            splitRefs++;
        }

        // if in a relation then append it to pick string
        //SPRINTF(_strPick, "%s:%i%s", name, S57ID, geoRelIDs->str);
        g_string_printf(_strPick, "%s:%i%s", name, S57ID, geoRelIDs->str);

        g_string_free(geoRelIDs, TRUE);

        g_strfreev(topRefs);
    }
#endif  // S52_USE_C_AGGR_C_ASSO

    return (const char *)_strPick->str;
}

guchar    *S52_GL_readFBPixels(void)
{
    if (S52_GL_PICK==_crnt_GL_cycle || NULL==_fb_pixels) {
        return NULL;
    }

    // debug
    //PRINTF("VP: %i, %i, %i, %i\n", _vp[0], _vp[1], _vp[2], _vp[3]);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    glBindTexture(GL_TEXTURE_2D, _fb_texture_id);
#endif

#ifdef S52_USE_TEGRA2
    // must be in sync with _fb_format

    //
    // copy FB --> MEM
    // RGBA
    glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // RGB
    //glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);

    // copy MEM --> Texture
    // RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _vp[2], _vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // RGB
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _vp[2], _vp[3], 0, GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _vp[2], _vp[3], 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

#else
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, _vp[2], _vp[3], 0);
#endif

    glBindTexture(GL_TEXTURE_2D, 0);

    _checkError("S52_GL_readFBPixels().. -end-");

    return _fb_pixels;
}

int        S52_GL_drawFBPixels(void)
{
    if (S52_GL_PICK==_crnt_GL_cycle || NULL==_fb_pixels) {
        return FALSE;
    }

    // set rotation temporatly to 0.0  (MatrixSet)
    double north = _north;
    _north = 0.0;

    // debug
    //PRINTF("VP: %i, %i, %i, %i\n", _vp[0], _vp[1], _vp[2], _vp[3]);

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2

    _glMatrixSet(VP_PRJ);

    glBindTexture(GL_TEXTURE_2D, _fb_texture_id);

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

    glBindTexture(GL_TEXTURE_2D, 0);

    _glMatrixDel(VP_PRJ);

#else   // S52_USE_GLES2

    _glMatrixSet(VP_WIN);
    glRasterPos2i(0, 0);

    // parameter must be in sync with glReadPixels()
    // RGBA
    glDrawPixels(_vp[2], _vp[3], GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    // RGB
    //glDrawPixels(_vp[2], _vp[3], GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);

    _glMatrixDel(VP_WIN);

#endif  // S52_USE_GLES2

    _north = north;

    _checkError("S52_GL_drawFBPixels() -fini-");

    return TRUE;
}

int        S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    if (S52_GL_PICK==_crnt_GL_cycle || NULL==_fb_pixels) {
        return FALSE;
    }

    // set rotation temporatly to 0.0  (MatrixSet)
    double northtmp = _north;

    _north = north;

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2

    _glMatrixSet(VP_PRJ);

    glBindTexture(GL_TEXTURE_2D, _fb_texture_id);

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

    glBindTexture(GL_TEXTURE_2D, 0);

    _glMatrixDel(VP_PRJ);

#else
    (void)scale_x;
    (void)scale_y;
    (void)scale_z;
#endif

    _north = northtmp;

    _checkError("S52_GL_drawBlit()");

    return TRUE;
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
        x      = _vp[0];
        y      = _vp[1];
        width  = _vp[2];
        height = _vp[3];
    } else {
        if (width > _vp[2]) {
            PRINTF("WARNING: dump width (%i) exceeded viewport width (%i)\n", width, _vp[2]);
            return FALSE;
        }
        if (height> _vp[3]) {
            PRINTF("WARNING: dump height (%i) exceeded viewport height (%i)\n", height, _vp[3]);
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

        projUV p = {pt.x, pt.y};
        p = _prj2win(p);

        // FIXME: what happen if the viewport change!!
        if ((p.u - width/2 ) <      0) x = width/2;
        if ((x   + width   ) > _vp[2]) x = _vp[2] - width;
        if ((p.v - height/2) <      0) y = height/2;
        if ((y   + height  ) > _vp[3]) y = _vp[3] - height;
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


    dataset = GDALCreate(driver, "/sdcard/s52droid/_temp.tif", width, height, 3, GDT_Byte, NULL);
    if (NULL == dataset) {
        PRINTF("WARNING: fail to create GDAL data set\n");
        return FALSE;
    }
    GDALRasterBandH bandR = GDALGetRasterBand(dataset, 1);
    GDALRasterBandH bandG = GDALGetRasterBand(dataset, 2);
    GDALRasterBandH bandB = GDALGetRasterBand(dataset, 3);

    // get framebuffer pixels
    //guint8 *pixels = g_new0(guint8, width * height * 3); // RGB
    guint8 *pixels = g_new0(guint8, width * height * 4);  // RGBA

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    //glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
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

    //gdal.GetDriverByName('PNG').CreateCopy(self.file.get_text(), gdal.Open('_temp.tif'),TRUE)
    GDALDatasetH dst_dataset;
    driver      = GDALGetDriverByName("PNG");
    dst_dataset = GDALCreateCopy(driver, toFilename, GDALOpen("/sdcard/s52droid/_temp.tif", GA_ReadOnly), FALSE, NULL, NULL, NULL);
    GDALClose(dst_dataset);

    return TRUE;
}

int        S52_GL_drawStr(double x, double y, char *str, unsigned int bsize, unsigned int weight)
// draw string in world coords
{
    S52_Color *c = S52_PL_getColor("CHBLK");  // black
    _renderTXTAA(NULL, c, x, y, bsize, weight, str);

    return TRUE;
}

int        S52_GL_drawStrWin(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
// draw a string in window coords
{
    S52_Color *c = S52_PL_getColor(colorName);

    _GL_BEGIN = TRUE;

//#ifdef S52_USE_GLES2
#ifdef S52_USE_GL2
    S52_GL_win2prj(&pixels_x, &pixels_y);

    _glMatrixSet(VP_PRJ);
    _renderTXTAA(NULL, c, pixels_x, pixels_y, bsize, 1, str);
    _glMatrixDel(VP_PRJ);

#else
    glRasterPos2d(pixels_x, pixels_y);
    _checkError("S52_GL_drawStrWin()");
#endif

#ifdef S52_USE_FTGL
    if (NULL != _ftglFont[bsize]) {
        _glColor4ub(c);

#ifdef _MINGW
        ftglRenderFont(_ftglFont[bsize], str, FTGL_RENDER_ALL);
#else
        ftglRenderFont(_ftglFont[bsize], str, FTGL_RENDER_ALL);
        //_ftglFont[bsize]->Render(str);
#endif
    }
#endif

    _GL_BEGIN = FALSE;

    return TRUE;
}

int        S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str)
{
    // FIXME: str not used yet (get font metric from a particular font system)
    (void)str;

    // scale offset
    double uoffs  = ((PICA * *offset_x) / _dotpitch_mm_x) * _scalex;
    double voffs  = ((PICA * *offset_y) / _dotpitch_mm_y) * _scaley;

    *offset_x = uoffs;
    *offset_y = voffs;

    return TRUE;
}

int        S52_GL_drawGraticule(void)
// FIXME: use S52_MAR_DISP_GRATICULE to get the user choice
{
    // delta lat  / 1852 = height in NM
    // delta long / 1852 = witdh  in NM
    double dlat = (_pmax.v - _pmin.v) / 3.0;
    double dlon = (_pmax.u - _pmin.u) / 3.0;

    //int remlat =  (int) _pmin.v % (int) dlat;
    //int remlon =  (int) _pmin.u % (int) dlon;

    char   str[80];
    S52_Color *black = S52_PL_getColor("CHBLK");
    _glColor4ub(black);

    //_setBlend(TRUE);

    _glUniformMatrix4fv_uModelview();
    glLineWidth(1.0);

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
        SPRINTF(str, "%07.4f deg %c", fabs(uv.v), (0.0<lat)?'N':'S');
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
        SPRINTF(str, "%07.4f deg %c", fabs(uv.v), (0.0<lat)?'N':'S');

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
        SPRINTF(str, "%07.4f deg %c", fabs(uv.u), (0.0<lon)?'E':'W');

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
        SPRINTF(str, "%07.4f deg %c", fabs(uv.u), (0.0<lon)?'E':'W');

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
    double nSym        = abs(floor(sweep / symAngle));
    double crntAngle   = orientA + 90.0;

    if (sweep > 0.0)
        crntAngle += 180;

    crntAngle += (symAngle / 2.0);

    //
    // draw
    //
    //_setBlend(TRUE);

    S52_DListData *DListData = S52_PL_getDListData(objA);
    //if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
    //    return FALSE;

    S52_Color *color = DListData->colors;
    _glColor4ub(color);


    // draw arc
    if (2.0==S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) {
        //nSym  /= 2;
        for (int j=0; j<=nSym; ++j) {
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

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
