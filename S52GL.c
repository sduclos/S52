// S52GL.c: display S57 data using S52 symbology and OpenGL.
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013  Sylvain Duclos sduclos@users.sourceforgue.net

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


// FIXME: split this file - 10 KLOC !
// Summary of functions calls to try isolated dependecys are at
// the end of this file.


    //glPixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    //glTexImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, texture.image);

#include "S52GL.h"
#include "S52MP.h"        // S52_MP_get/set()
#include "S52utils.h"     // PRINTF(), S52_atof(), S52_atoi(), S52_strlen()

#include <glib.h>
#include <glib/gstdio.h>  // g_file_test()

#include <string.h>       // strcmp(), bzero()

// compiled with -std=gnu99 instead of -std=c99 will define M_PI
#include <math.h>         // sin(), cos(), atan2(), pow(), sqrt(), floor(), INFINITY, M_PI

#include <dlfcn.h>


#ifndef S52_USE_GLES2
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
//#include "glext.h"
#endif


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


// experiment OpenGL ES SC
//#define S52_USE_OPENGL_SAFETY_CRITICAL
#ifdef S52_USE_OPENGL_SAFETY_CRITICAL

#include "GL/es_sc_gl.h"
typedef GLfloat GLdouble;
#define glScaled            glScalef
#define glRotated           glRotatef
#define glTranslated        glTranslatef
#define glVertex3d          glVertex3f
#define GL_COMPILE          0x1300
#define GL_UNSIGNED_INT     0x1405  // byte size of a dispaly list
#define GL_DBL_FLT          GL_FLOAT
#endif
#define GL_DBL_FLT          GL_DOUBLE

// NOTE: rubber band in GL 1.x
// glEnable(GL_COLOR_LOGIC_OP); glLogicOp(GL_XOR);


#ifdef S52_USE_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
//static GLeglImageOES test;

typedef double GLdouble;

// convert float to double for tess
static GArray *_tessWorkBuf_d  = NULL;
// for converting geo double to VBO float
static GArray *_tessWorkBuf_f  = NULL;

// debug
static GArray *_buf = NULL;

// glsl main
static GLint  _programObject  = 0;
static GLuint _vertexShader   = 0;
static GLuint _fragmentShader = 0;

// glsl uniform
static GLuint _uProjection = 0;
static GLuint _uModelview  = 0;
static GLuint _uColor      = 0;
static GLuint _uPointSize  = 0;
static GLuint _uSampler2d  = 0;
static GLuint _uBlitOn     = 0;
static GLuint _uGlowOn     = 0;
static GLuint _uStipOn     = 0;

static GLuint _uPattOn     = 0;
static GLuint _uPattOffX   = 0;
static GLuint _uPattOffY   = 0;
static GLuint _uPattX      = 0;
static GLuint _uPattY      = 0;
//static GLuint _uPattMaxX   = 0;
//static GLuint _uPattMaxY   = 0;

// glsl varying
static GLint _aPosition    = 0;
static GLint _aUV          = 0;
static GLint _aColor       = 0;
static GLint _aAlpha       = 0;

static GLuint _framebufferID       = 0;
static GLuint _colorRenderbufferID = 0;

static int    _resetVBOID  = FALSE;


#endif // S52_USE_GLES2


///////////////////////////////////////////////////////////////////
//
// FONTS  (test various libs)
//


#define S52_MAX_FONT  4

#ifdef S52_USE_FREETYPE_GL
#ifdef S52_USE_GLES2
#include "vector.h"
#include "texture-atlas.h"
#include "texture-font.h"

static texture_font_t  *_freetype_gl_font[S52_MAX_FONT] = {NULL,NULL,NULL,NULL};
static texture_atlas_t *_freetype_gl_atlas              = NULL;
static const char      *_freetype_gl_fontfilename       =
#ifdef S52_USE_ANDROID
    //"/system/fonts/Roboto-Regular.ttf";   // not official, could change place
    "/system/fonts/DroidSans.ttf";
#else
    "./Roboto-Regular.ttf";
#endif

typedef struct {
    GLfloat x, y, z;    // position
    GLfloat s, t;       // texture
} _freetype_gl_vertex_t;

// legend has no S52_obj, so this a place holder
static GLuint  _text_textureID = 0;
static GArray *_textWorkBuf    = NULL;

#define LF  '\r'   // Line Feed
#define TB  '\t'   // Tabulation
#define NL  '\n'   // New Line

#endif // S52_USE_GLES2
#endif // S52_USE_FREETYPE_GL


#ifdef S52_USE_FTGL
#include <FTGL/ftgl.h>
#ifdef _MINGW
static FTGLfont *_ftglFont[S52_MAX_FONT];
#else
//static FTFont   *_ftglFont[S52_MAX_FONT];
static FTGLfont *_ftglFont[S52_MAX_FONT];
#endif
#endif // S52_USE_FTGL


#ifdef S52_USE_GLC
#include <GL/glc.h>

//#define glcContext
//#ifdef _MINGW
//extern __declspec(dllimport) GLint __stdcall glcGenContext(void); //@0'
//#endif

/*
_ glcContext@4'
_glcResolution@4'
_glcEnable@4'
_glcRenderStyle@4'
_glcEnable@4'
_glcEnable@4'
_glcGenFontID@0'
_glcNewFontFromFamily@8'
_glcFontFace@8'
_glcFont@4'
_glcGetListi@8'
_glcGetFontc@8'
_glcGetFontFace@4'

_glcRenderString@4'
*/

static GLint _GLCctx;

#endif  // S52_USE_GLC


#ifdef S52_USE_COGL
//#include "cogl/cogl.h"
//#include "cogl/cogl-pango.h"
#include "cogl-pango/cogl-pango.h"
//#include "pango/pango.h"
static PangoFontDescription *_PangoFontDesc  = NULL;
static PangoFontMap         *_PangoFontMap   = NULL;
static PangoContext         *_PangoCtx       = NULL;
static PangoLayout          *_PangoLayout    = NULL;

//#include "clutter/clutter.h"
//static ClutterActor         *_text           = NULL;
//static ClutterActor         *_stage          = NULL;

//#include "clutter-gtk/clutter-gtk.h"
//static GtkClutterEmbed      *_stage          = NULL;
//static GtkWidget            *_stage          = NULL;
#endif  // S52_USE_COGL


#ifdef S52_USE_A3D
#define LOG_TAG "s52a3d"
#define LOG_DEBUG
#include "a3d/a3d_log.h"
#include "a3d/a3d_texfont.h"
#include "a3d/a3d_texstring.h"
static const char      *_a3d_font_file = "/data/data/nav.ecs.s52droid/files/whitrabt.tex.gz";
static a3d_texfont_t   *_a3d_font      = NULL;
static a3d_texstring_t *_a3d_str       = NULL;
#endif  // S52_USE_A3D



///////////////////////////////////////////////////////////////////
// statistique
static guint   _nobj  = 0;     // number of object drawn during lap
static guint   _ncmd  = 0;     // number of command drawn during lap
static guint   _oclip = 0;     // number of object clipped
//static guint   _nFrag = 0;     // number of pixel fragment (color switch)
//static int   _debug = 0;


///////////////////////////////////////////////////////////////////
// state
static int        _doInit        = TRUE;    // initialize (but GL context --need main loop)
static int        _ctxValidated  = FALSE;   // validate GL context
// FIXME: use the journal in S52.c instead of the GPU! - this simplify the GL code
static int        _doPick        = FALSE;   // TRUE inside curcor picking cycle
static GPtrArray *_objPick       = NULL;    // list of object picked
static char       _strPick[80];             // hold temps val

//////////////////////////////////////////////////////
// tessallation
// mingw specific, with gcc APIENTRY expand to nothing
#ifdef _MINGW
#define CALLBACK __attribute__ ((__stdcall__))
#else
#define CALLBACK
#endif

#ifdef S52_USE_ANDROID
#include "tesselator.h"
typedef GLUtesselator GLUtesselatorObj;
typedef GLUtesselator GLUtriangulatorObj;
#else
#include <GL/glu.h>     // tess, quadric,...
#endif


typedef void (CALLBACK *f) ();
typedef void (CALLBACK *fint) (GLint);
typedef void (CALLBACK *f2)  (GLint, void*);
typedef void (CALLBACK *fp)  (void*);
typedef void (CALLBACK *fpp) (void*, void*);

static GLUtriangulatorObj *_tobj = NULL;

static GPtrArray          *_tmpV = NULL;  // place holder during tesssalation (GLUtriangulatorObj)

// centroid
static GLUtriangulatorObj *_tcen       = NULL;     // GLU CSG
static GArray             *_vertexs    = NULL;
static GArray             *_nvertex    = NULL;     // list of nbr of vertex per poly in _vertexs
static GArray             *_centroids  = NULL;     // centroids of poly's in _vertexs

static GLUtriangulatorObj *_tcin       = NULL;
static GLboolean           _startEdge  = GL_TRUE;  // start inside edge --for heuristic of centroid in poly
static int                 _inSeg      = FALSE;    // next vertex will complete an edge

// display list
//static GLuint  _listIndex  = 0;         // GL display index (0==error)
static   int  _symbCreated = FALSE;  // TRUE symb display list created

// hack: first coordinate of polygon mode

// transparency factor
#ifdef S52_USE_GLES2
// alpha is 0.0 - 1.0
#define TRNSP_FAC_GLES2   0.25
#else
// alpha is 0.0 - 255.0
#define TRNSP_FAC   255 * 0.25
#endif

// same thing as in proj_api.h
#ifndef S52_USE_PROJ
typedef struct { double u, v; } projUV;
#define projXY projUV
#define RAD_TO_DEG    57.29577951308232
#define DEG_TO_RAD     0.0174532925199432958
#endif

#define ATAN2TODEG(xyz)   (90.0 - atan2(xyz[4]-xyz[1], xyz[3]-xyz[0]) * RAD_TO_DEG)

// in the begining was the universe .. it was good
static projUV _pmin = { INFINITY,  INFINITY};
static projUV _pmax = {-INFINITY, -INFINITY};
//static projUV _pmin = { HUGE,  HUGE};
//static projUV _pmax = {-HUGE, -HUGE};

// viewport of current openev gldraw signal
static GLuint _vp[4]; // x,y,width,height

typedef enum _VP{   // set GL_PROJECTION matrix to
    VP_PRJ,         // projected coordinate
    //VP_PRJ_ZCLIP,   // same but short depth volume for line clipping
    VP_WIN,         // window coordinate
    VP_NUM          // number of coord. systems type
}VP;

// line mask plane
#define Z_CLIP_PLANE 10000   // clipped beyon this plane

#define PICA   0.351

// DEPRECATED - Display List for font
//#define S52_MAX_FONT  3        // total number of font
//static gint _fontDList[S52_MAX_FONT];
//static gint _fontDListIdx = 0;


// NOTE: S52 pixels for symb are 0.3 mm
// this is the real dotpitch of the device
// as computed at init() time
static double _dotpitch_mm_x      = 0.3;  // will be overright at init()
static double _dotpitch_mm_y      = 0.3;  // will be overright at init()
static double _SCAMIN             = 0.0;  // screen scale
#define MM2INCH 25.4

// symbol twice as big (see _pushScaletoPixel())
#define STRETCH_SYM_FAC 2.0
//static double _scalePixelX = 1.0;
//static double _scalePixelY = 1.0;

//typedef GLdouble coor3d[3];
//typedef struct  pt3 { GLdouble x,y,z; }  pt3;
//typedef struct _pt2 { GLdouble x,y;   } _pt2;
typedef struct  pt3  { double   x,y,z; }  pt3;
typedef struct  pt3v { vertex_t x,y,z; }  pt3v;
typedef struct _pt2  { vertex_t x,y;   } _pt2;

// for centroid inside poly heuristic
static double _dcin;
static pt3    _pcin;

#define NM_METER 1852.0

//static double _north = 330.0;  // debug
static double _north     = 0.0;
static double _rangeNM   = 0.0;
static double _centerLat = 0.0;
static double _centerLon = 0.0;

//
//---- PATTERN -----------------------------------------------------------
//
// NOTE: 4 mask are drawn to fill the square made of 2 triangles (fan)
// NOTE: MSB 0x01, LSB 0xE0 - so it left most pixels is at 0x01
// and the right most pixel in a byte is at 0xE0
// 1 bit in _nodata_mask is 4 bytes (RGBA) in _rgba_nodata_mask (s0 x 8 bits x 4 )

//OVERSC01


//DRGARE01
static int   _drgare = 0;  // debug
static const GLubyte _drgare_mask[4*32] = {
    0x80, 0x80, 0x80, 0x80, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x08, 0x08, 0x08, 0x08, // 8
    0x00, 0x00, 0x00, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x80, 0x80, 0x80, 0x80, // 8
    0x00, 0x00, 0x00, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x08, 0x08, 0x08, 0x08, // 8
    0x00, 0x00, 0x00, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x00, 0x00, 0x00, 0x00  // 8
};

// NODATA03
static GLuint        _nodata_mask_texID = 0;
static GLubyte       _nodata_mask_rgba[4*32*8*4]; // 32 * 32 * 4   // 8bits * 4col * 32row * 4rgba
static const GLubyte _nodata_mask_bits[4*32] = {
    0xFE, 0x00, 0x00, 0x00, // 1
    0xFE, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x00, 0x00, 0xFE, 0x00, // 8
    0x00, 0x00, 0xFE, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0xFE, 0x00, 0x00, 0x00, // 8
    0xFE, 0x00, 0x00, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x00, 0x00, 0xFE, 0x00, // 8
    0x00, 0x00, 0xFE, 0x00, // 1
    0x00, 0x00, 0x00, 0x00, // 2
    0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, // 7
    0x00, 0x00, 0x00, 0x00  // 8
};

// line DOTT PAttern
// dotpitch 0.3:  dott = 2 * 0.3,  space = 4 * 0.3  (6  px wide, 32 /  6 = 5.333)
// 1100 0000 1100 0000 ...
// dotpitch 0.15: dott = 4 * 0.15, space = 8 * 0.3  (12 px wide, 32 / 12 = 2.666)
// llll 0000 0000 1111 0000 0000 1111 0000
static GLuint        _dottpa_mask_texID = 0;
static GLubyte       _dottpa_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
static const GLubyte _dottpa_mask_bits[4] = {     // 4 x 8 bits = 32 bits
    0x03, 0x03, 0x03, 0x03
//    0x00, 0x00, 0x00, 0x00
};


// OVERSC01
// vertical line at each 400 units (4 mm)
// 0.15mm dotpitch then 27 pixels == 4mm  (26.666..)
// 0.30mm dotpitch then 13 pixels == 4mm  (13.333..)
static GLuint        _oversc_mask_texID = 0;
static GLubyte       _oversc_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
//static const GLubyte _oversc_mask_bits[16] = {  // 4 x 8 bits = 32 bits
static const GLubyte _oversc_mask_bits[4] = {     // 4 x 8 bits = 32 bits
    0x01, 0x00, 0x00, 0x00
};

// other pattern are created using FBO
static GLuint        _frameBufferID  = 0;
static GLuint        _renderBufferID = 0;

//---- PATTERN -----------------------------------------------------------



// debug
//static int _DEBUG = FALSE; //

static int            _update_fb      = TRUE;
static unsigned char *_fb_pixels      = NULL;
static unsigned int   _fb_pixels_size = 0;
static GLuint         _fb_texture_id  = 0;

// debug
static int _GL_BEGIN = FALSE;
#define CHECK_GL_BEGIN if (TRUE != _GL_BEGIN) {                     \
                           PRINTF("ERROR: not inside glBegin()\n"); \
                           g_assert(0);                             \
                       }

#define CHECK_GL_END   if (TRUE == _GL_BEGIN) {                         \
                           PRINTF("ERROR: already inside glBegin()\n"); \
                           g_assert(0);                                 \
                       }

#ifdef S52_USE_GLES2
// FIXME: put into struct
//static    int      _debugMatrix = 1;
#define   MATRIX_STACK_MAX 8

// symbole not in GLES2
#define   GL_MODELVIEW    0x1700
#define   GL_PROJECTION   0x1701

/* QuadricDrawStyle */
#define   GLU_POINT                          100010
#define   GLU_LINE                           100011
#define   GLU_FILL                           100012
#define   GLU_SILHOUETTE                     100013
/* Boolean */
#define   GLU_FALSE                          0
#define   GLU_TRUE                           1

static    GLenum   _mode = GL_MODELVIEW;  // GL_MODELVIEW (initial) or GL_PROJECTION
static    GLfloat  _mvm[MATRIX_STACK_MAX][16];       // modelview matrix
static    GLfloat  _pjm[MATRIX_STACK_MAX][16];       // projection matrix
static    GLfloat *_crntMat;          // point to active matrix
static    int      _mvmTop = 0;       // point to stack top
static    int      _pjmTop = 0;       // point to stack top


#else   // S52_USE_GLES2

#ifdef S52_USE_OPENGL_SAFETY_CRITICAL
    // BUG: access to matrix is in integer
    // BUT gluProject() for OpenGL ES use GLfloat (make sens)
    // so how to get matrix in float!
static    GLint    _mvm[16];       // OpenGL ES SC
static    GLint    _pjm[16];       // OpenGL ES SC
#else
static    GLdouble _mvm[16];       // modelview matrix
static    GLdouble _pjm[16];       // projection matrix
#endif

#endif  // S52_USE_GLES2

static GLuint   _vboIDaftglwVertID = 0;
static GLuint   _vboIDaftglwColrID = 0;
static GArray  *_aftglwColorArr    = NULL;

static int      _highlight    = FALSE;
static S52_obj *_objhighlight = NULL;

// utils
static int       _f2d(GArray *tessWorkBuf_d, unsigned int npt, vertex_t *ppt)
// convert array of float (vertex_t with GLES2) to array of double
// as the tesselator work on double
{
    unsigned int i = 0;
    g_array_set_size(tessWorkBuf_d, 0);

    for (i=0; i<npt; i++) {
        double d[3] = {ppt[0], ppt[1], ppt[2]};
        g_array_append_val(tessWorkBuf_d, d);
        ppt += 3;
    }
    return TRUE;
}

static int       _d2f(GArray *tessWorkBuf_f, unsigned int npt, double *ppt)
// convert array of double to array of float
// geo to VBO
{
    unsigned int i = 0;
    g_array_set_size(tessWorkBuf_f, 0);

    for (i=0; i<npt; ++i) {
        //float f[3] = {ppt[0], ppt[1], ppt[2]};
        float f[3] = {ppt[0], ppt[1], 0.0};
        g_array_append_val(tessWorkBuf_f, f);
        ppt += 3;
    }
    return TRUE;
}


//-----------------------------------
//
// GLU: TESS, QUADRIC, ...  SECTION
//
//-----------------------------------

static
inline void      _checkError(const char *msg)
{
#ifdef S52_DEBUG

    //CHECK_GL_BEGIN;

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
            g_assert(0);
            //exit(0);
    }
#endif
}

// FIXME: use GDestroyNotify() instead
static int       _g_ptr_array_clear(GPtrArray *arr)
// free vertex malloc'd, keep array memory allocated
{
    for (guint i=0; i<arr->len; ++i)
        g_free(g_ptr_array_index(arr, i));
    g_ptr_array_set_size(arr, 0);

    return TRUE;
}

static int       _findCentInside(unsigned int npt, pt3 *v)
// return TRUE and centroid else FALSE
{
    _dcin  = -1.0;
    _inSeg = FALSE;

    g_array_set_size(_vertexs,   0);
    g_array_set_size(_nvertex,   0);

    //gluTessProperty(_tcen, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);

    gluTessBeginPolygon(_tcin, NULL);

    gluTessBeginContour(_tcin);
    for (guint i=0; i<npt; ++i, ++v)
        gluTessVertex(_tcin, (GLdouble*)v, (void*)v);
    gluTessEndContour(_tcin);

    gluTessEndPolygon(_tcin);

    _g_ptr_array_clear(_tmpV);

    if (_dcin != -1.0) {
        g_array_append_val(_centroids, _pcin);
        return TRUE;
    }

    return FALSE;
}

static int       _getCentroidOpen(guint npt, pt3 *v)
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
            //PRINTF("point is outside polygone, heuristique used to find an place inside\n");

            return TRUE;
        }

    }

    return FALSE;
}


static int       _getMaxEdge(pt3 *p)
{
    double x = p[0].x - p[1].x;
    double y = p[0].y - p[1].y;
    double d =  sqrt(pow(x, 2) + pow(y, 2));

    if (_dcin < d) {
        _dcin = d;
        //_pcin.x = p[0].x + (p[1].x - p[0].x) / 2.0;
        //_pcin.y = p[0].y + (p[1].y - p[0].y) / 2.0;
        _pcin.x = (p[1].x + p[0].x) / 2.0;
        _pcin.y = (p[1].y + p[0].y) / 2.0;
    }

    return TRUE;
}

static GLvoid CALLBACK   _tessError(GLenum err)
{
    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";

    PRINTF("%s (%d)\n", str, err);

    g_assert(0);
}

static GLvoid CALLBACK   _quadricError(GLenum err)
{

    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";

    PRINTF("QUADRIC ERROR:%s (%d) (%0x)\n", str, err, err);

    // FIXME
    g_assert(0);
}

static GLvoid CALLBACK   _edgeCin(GLboolean flag)
{
    _startEdge = (GL_FALSE == flag)? GL_TRUE : GL_FALSE;
}


static GLvoid CALLBACK   _combineCallback(GLdouble   coords[3],
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
// debug
static GLvoid CALLBACK   _combineCallbackCen(void)
{
    g_assert(0);
}
#endif

static GLvoid CALLBACK   _glBegin(GLenum data, S57_prim *prim)
{
    /*
    // debug
    if (_DEBUG) {
        //PRINTF("_glBegin() mode: %i\n", data);
        if (0 != _GLBEGIN)
            g_assert(0);
        _GLBEGIN = 1;
    }
    */

    S57_begPrim(prim, data);
}

static GLvoid CALLBACK   _beginCen(GLenum data)
{
    // avoid "warning: unused parameter"
    (void) data;

    // debug  printf
    //PRINTF("%i\n", data);
}

static GLvoid CALLBACK   _beginCin(GLenum data)
{
    // avoid "warning: unused parameter"
    (void) data;

    // debug  printf
    //PRINTF("%i\n", data);
}

static GLvoid CALLBACK   _glEnd(S57_prim *prim)
{
    /*
    // debug
    if (_DEBUG) {
        //PRINTF("_glEnd()\n");
        if (1 != _GLBEGIN)
            g_assert(0);
        _GLBEGIN = 0;
    }
    */

    S57_endPrim(prim);
}

static GLvoid CALLBACK   _endCen(void)
{
    // debug
    //PRINTF("finish a poly\n");

    if (_vertexs->len > 0)
        g_array_append_val(_nvertex, _vertexs->len);
}

static GLvoid CALLBACK   _endCin(void)
{
    // debug
}

static GLvoid CALLBACK   _vertex3d(GLvoid *data, S57_prim *prim)
// double
{
    //S57_addPrimVertex(prim, (vertex_t*)data);

#ifdef S52_USE_GLES2
    // cast to float after tess (double)
    double *dptr     = (double*)data;
    float dataf[3] = {0.0, 0.0, 0.0};
    dataf[0] = (float) dptr[0];
    dataf[1] = (float) dptr[1];
    dataf[2] = (float) dptr[2];

    S57_addPrimVertex(prim, (float*)dataf);
#else
    S57_addPrimVertex(prim, (double*)data);
#endif

}

static GLvoid CALLBACK   _vertex3f(GLvoid *data, S57_prim *prim)
// float
{
    S57_addPrimVertex(prim, (vertex_t*)data);
}

static GLvoid CALLBACK   _vertexCen(GLvoid *data)
{
    pt3 *p = (pt3*) data;

    //PRINTF("%f %f\n", p->x, p->y);

    g_array_append_val(_vertexs, *p);
}

static GLvoid CALLBACK   _vertexCin(GLvoid *data)
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

static void      _dumpATImemInfo(GLenum glenum)
{
    GLint params[4] = {0,0,0,0};
    //glGetIntegerv(GL_ATI_meminfo, GLint *params);
    //glGetIntegerv(VBO_FREE_MEMORY_ATI, params);
    glGetIntegerv(glenum, params);
    //#define VBO_FREE_MEMORY_ATI                     0x87FB
    //#define TEXTURE_FREE_MEMORY_ATI                 0x87FC
    //#define RENDERBUFFER_FREE_MEMORY_ATI            0x87FD
    /*
     param[0] - total memory free in the pool
     param[1] - largest available free block in the pool
     param[2] - total auxiliary memory free
     param[3] - largest auxiliary free block
     */
    PRINTF("---------------ATI mem info: %x -----------------\n", glenum);
    PRINTF("%i Kbyte:total memory free in the pool\n",            params[0]);
    PRINTF("%i Kbyte:largest available free block in the pool\n", params[1]);
    PRINTF("%i Kbyte:total auxiliary memory free\n",              params[2]);
    PRINTF("%i Kbyte:largest auxiliary free block\n",             params[3]);
}

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


// experiment
#define _QUADRIC_TRANSLATE    0x0010  // valid GLenum for glDrawArrays mode is 0-9
                                      // GL_POLYGON_STIPPLE_BIT	is also	0x00000010
                                      // This is an attempt to signal VBO drawing func
                                      // to glTranslate() (eg to put a '!' inside a circle)
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

    //g_assert(0);

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
          //gluQuadricError(qobj, GLU_INVALID_ENUM);
          g_assert(0);
          return FALSE;
    }

    //g_assert(0);

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

    if (NULL == _diskPrimTmp)
        g_assert(0);

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

    int i,j;

    GLdouble angleOffset = startAngle/180.0f*PI;
    for (i=0; i<=slices; i++) {
        angle = angleOffset+((PI*sweepAngle)/180.0f)*i/slices;

        sinCache[i] = sin(angle);
        cosCache[i] = cos(angle);
    }

    //glEnableClientState(GL_VERTEX_ARRAY);
    //glVertexPointer(3, GL_FLOAT, 0, vertex);

    if (GLU_FILL == qobj->style)
        qobj->cb_begin(GL_TRIANGLE_STRIP, _diskPrimTmp);
    else
        qobj->cb_begin(GL_LINE_STRIP, _diskPrimTmp);

    for (j=0; j<loops; j++) {
        float deltaRadius = outerRadius-innerRadius;
        float radiusLow   = outerRadius-deltaRadius*((GLfloat)j/loops);
        float radiusHigh  = outerRadius-deltaRadius*((GLfloat)(j+1)/loops);

        //int offset = 0;
        for (i = 0; i <= slices; i++) {

            if (GLU_FILL == qobj->style) {
                vertex[0] = radiusLow * sinCache[i];
                vertex[1] = radiusLow * cosCache[i];
                vertex[2] = 0.0;
                qobj->cb_vertex(vertex, _diskPrimTmp);
                vertex[0] = radiusHigh * sinCache[i];
                vertex[1] = radiusHigh * cosCache[i];
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

    //g_assert(0);

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
#endif //S52_USE_OPENGL_VBO


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
        //_tmpV = g_array_new(FALSE, FALSE, sizeof(coor3d));
        _tmpV = g_ptr_array_new();

        _tobj = gluNewTess();
        if (NULL == _tobj) {
            PRINTF("ERROR: gluNewTess() failed\n");
            return FALSE;
        }

        /*
        void tess_properties(GLUtesselator *tobj) {
            gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);
            gluTessCallback(_tobj, GLU_TESS_VERTEX,     (_GLUfuncptr)glVertex3dv);
            gluTessCallback(_tobj, GLU_TESS_BEGIN,      (_GLUfuncptr)beginCallback);
            gluTessCallback(_tobj, GLU_TESS_END,        (_GLUfuncptr)endCallback);
            gluTessCallback(_tobj, GLU_TESS_ERROR,      (_GLUfuncptr)errorCallback);
            gluTessCallback(_tobj, GLU_TESS_COMBINE,    (_GLUfuncptr)combineCallback);
            gluTessCallback(_tobj, GLU_TESS_VERTEX_DATA,(_GLUfuncptr)&vertexCallback);
        }
        */
        gluTessCallback(_tobj, GLU_TESS_BEGIN_DATA, (f)_glBegin);
        gluTessCallback(_tobj, GLU_TESS_END_DATA,   (f)_glEnd);
        gluTessCallback(_tobj, GLU_TESS_ERROR,      (f)_tessError);
        gluTessCallback(_tobj, GLU_TESS_VERTEX_DATA,(f)_vertex3d);
        gluTessCallback(_tobj, GLU_TESS_COMBINE,    (f)_combineCallback);

        // NOTE: _*NOT*_ NULL to trigger GL_TRIANGLES tessallation
        //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) glEdgeFlag);
        //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) _glEdgeFlag);
        gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) NULL);


        // no GL_LINE_LOOP
        gluTessProperty(_tobj, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);

        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
        // ODD needed for symb. ISODGR01
        gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NEGATIVE);

        //gluTessProperty(_tobj, GLU_TESS_TOLERANCE, 0.00001);
        //gluTessProperty(_tobj, GLU_TESS_TOLERANCE, 0.1);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tobj, 0.0, 0.0, 1.0);
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
        gluTessCallback(_tcin, GLU_TESS_EDGE_FLAG, (f)_edgeCin);

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
         str = glGetString(GL_VERSION);
         PRINTF("GL version:%s\n",str);
         */
    }

    //_dumpATImemInfo(VBO_FREE_MEMORY_ATI);

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

#ifdef S52_USE_GLC
static GLint     _initGLC(void)
{
    int   i;
    GLint font;
    static int first = FALSE;

    if (first)
        return TRUE;
    first = TRUE;

    _GLCctx = glcGenContext();

    glcContext(_GLCctx);

    // dot pitch mm to dpi
    //double dpi = MM2INCH / _dotpitch_mm_x;  // dot per inch
    double dpi = MM2INCH / _dotpitch_mm_y;  // dot per inch
    glcResolution(dpi);
    //glcResolution(dpi/10.0);
    //glcResolution(12.0);


    // speed very slow !!
    // and small font somewhat darker but less "fuzzy"
    //glcDisable(GLC_GL_OBJECTS);
    // speed ok
    glcEnable(GLC_GL_OBJECTS);

    //glcRenderStyle(GLC_TEXTURE);
    //glcRenderStyle(GLC_PIXMAP_QSO);
    //glcRenderStyle(GLC_BITMAP);
    //glcRenderStyle(GLC_LINE);
    //glcStringType(GLC_UTF8_QSO);

    // one or the other (same thing when MIPMAP on)
    glcEnable(GLC_HINTING_QSO);
    //glcDisable(GLC_HINTING_QSO);

    // with Arial make no diff
    // BIG DIFF: if positon rouded to and 'int' and no MIPMAP (less fuzzy)
    glcEnable(GLC_MIPMAP);     // << GOOD!
    //glcDisable(GLC_MIPMAP);    // << BAD!


    font = glcGenFontID();
    if (!glcNewFontFromFamily(font, "Arial")) {
    //if (!glcNewFontFromFamily(font, "Verdana")) {
    //if (!glcNewFontFromFamily(font, "Trebuchet")) {  // fail
    //if (!glcNewFontFromFamily(font, "trebuc")) {  // fail
    //if (!glcNewFontFromFamily(font, "DejaVu Sans")) {
        PRINTF("font failed\n");
        g_assert(0);
    }

    //if (!glcFontFace(font, "Bold")) {
    if (!glcFontFace(font, "Regular")) {
    //if (!glcFontFace(font, "Normal")) {
        PRINTF("font face failed\n");
        g_assert(0);
    }

    glcFont(font);

    int  count = glcGeti(GLC_CURRENT_FONT_COUNT);
    PRINTF("%d fonts were used :\n", count);

    for (i = 0; i < count; i++) {
        int font = glcGetListi(GLC_CURRENT_FONT_LIST, i);
        PRINTF("Font #%i : %s", font, (const char*) glcGetFontc(font, GLC_FAMILY));
        PRINTF("Face : %s\n", (const char*) glcGetFontFace(font));
    }

    // for bitmap and pixmap
    glcScale(13.0, 13.0);

    return TRUE;
}
#endif

#ifdef S52_USE_FTGL
static GLint     _initFTGL(void)
{
    //const char *file = "arial.ttf";
    //const char *file = "DejaVuSans.ttf";
    //const char *file = "Trebuchet_MS.ttf";
    //const gchar *file = "Waree.ttf";
    const gchar *file = "Waree.ttf";
    // from Navit
    //const char *file = "LiberationSans-Regular.ttf";


    if (FALSE == g_file_test(file, G_FILE_TEST_EXISTS)) {
        PRINTF("WARNING: font file not found (%s)\n", file);
        return FALSE;
    }

//#ifdef _MINGW
    _ftglFont[0] = ftglCreatePixmapFont(file);
    _ftglFont[1] = ftglCreatePixmapFont(file);
    _ftglFont[2] = ftglCreatePixmapFont(file);
    _ftglFont[3] = ftglCreatePixmapFont(file);

    ftglSetFontFaceSize(_ftglFont[0], 12, 12);
    ftglSetFontFaceSize(_ftglFont[1], 14, 14);
    ftglSetFontFaceSize(_ftglFont[2], 16, 16);
    ftglSetFontFaceSize(_ftglFont[3], 20, 20);

/*
#else
    _ftglFont[0] = new FTPixmapFont(file);
    _ftglFont[1] = new FTPixmapFont(file);
    _ftglFont[2] = new FTPixmapFont(file);
    _ftglFont[3] = new FTPixmapFont(file);

    _ftglFont[0]->FaceSize(12);
    _ftglFont[1]->FaceSize(14);
    _ftglFont[2]->FaceSize(16);
    _ftglFont[3]->FaceSize(20);
#endif
*/


    return TRUE;
}
#endif

#ifdef S52_USE_COGL
static int       _initCOGL(void)
{
    /* Setup a Pango font map and context */
    _PangoFontMap = cogl_pango_font_map_new();
    //_PangoFontMap = cogl_pango_font_map_new();

    cogl_pango_font_map_set_use_mipmapping (COGL_PANGO_FONT_MAP(_PangoFontMap), TRUE);

    _PangoCtx = cogl_pango_font_map_create_context (COGL_PANGO_FONT_MAP(_PangoFontMap));

    _PangoFontDesc = pango_font_description_new ();
    pango_font_description_set_family (_PangoFontDesc, "DroidSans");
    pango_font_description_set_size (_PangoFontDesc, 30 * PANGO_SCALE);

    /* Setup the "Hello Cogl" text */
    _PangoLayout = pango_layout_new (_PangoCtx);
    pango_layout_set_font_description (_PangoLayout, _PangoFontDesc);
    pango_layout_set_text (_PangoLayout, "Hello Cogl", -1);

    PangoRectangle hello_label_size;
    pango_layout_get_extents (_PangoLayout, NULL, &hello_label_size);
    int hello_label_width  = PANGO_PIXELS (hello_label_size.width);
    int hello_label_height = PANGO_PIXELS (hello_label_size.height);


    /*
    //_PangoFontDescr = pango_font_description_new();
    //_PangoFontDescr = pango_font_description_from_string("Helvetica 14");
    //pango_font_description_set_family(_PangoFontDescr, "Sans");
    //pango_font_description_set_size(_PangoFontDescr, 10 * PANGO_SCALE);

    _PangoFontMap = cogl_pango_font_map_new();
    //cogl_pango_font_map_set_resolution(COGL_PANGO_FONT_MAP(_PangoFontMap), 96.0);
    cogl_pango_font_map_set_resolution(COGL_PANGO_FONT_MAP(_PangoFontMap), 160.0);   // xoom

    _PangoCtx     = cogl_pango_font_map_create_context(COGL_PANGO_FONT_MAP(_PangoFontMap));
    //_PangoCtx     = pango_font_map_create_context(_PangoFontMap);

    //_text = clutter_text_new();

    //clutter_text_set_text (CLUTTER_TEXT (_text), "Hello, World!");
    //clutter_text_set_font_name (CLUTTER_TEXT (_text), "Sans 64px");

    //_PangoLayout  = clutter_actor_create_pango_layout(_text, "test123");
    _PangoLayout  = pango_layout_new(_PangoCtx);
    //_PangoCtx = clutter_actor_create_pango_context(_text);

    //pango_layout_set_font_description(_PangoLayout, _PangoFontDescr);

    cogl_pango_ensure_glyph_cache_for_layout(_PangoLayout);

    //clutter_container_add (CLUTTER_CONTAINER (_stage), _text, NULL);

    //clutter_actor_show(_stage);
    */


    return TRUE;
}
#endif

#ifdef S52_USE_A3D
static int       _initA3D(void)
{
    PRINTF("_initA3D() .. -0-\n");
	_a3d_font = a3d_texfont_new(_a3d_font_file);
    if(NULL == _a3d_font) {
        PRINTF("a3d_texfont_new() .. failed\n");
        return FALSE;
    }

    PRINTF("_initA3D() .. -1-\n");
	//_a3d_str = a3d_texstring_new(_a3d_font, 16, 48, A3D_TEXSTRING_TOP_LEFT, 1.0f, 1.0f, 0.235f, 1.0f);   // yellow
	//_a3d_str = a3d_texstring_new(_a3d_font, 16, 48, A3D_TEXSTRING_TOP_LEFT, 0.0f, 0.0f, 0.0f, 1.0f);      // black
	_a3d_str = a3d_texstring_new(_a3d_font, 10, 12, A3D_TEXSTRING_TOP_LEFT, 0.0f, 0.0f, 0.0f, 1.0f);      // black
    if(NULL == _a3d_str) {
        PRINTF("a3d_texstring_new() .. failed\n");
        return FALSE;
    }

    PRINTF("_initA3D() .. -2-\n");

    return TRUE;
}
#endif

#ifdef S52_USE_FREETYPE_GL
static int       _init_freetype_gl(void)
{
    const wchar_t   *cache    = L" !\"#$%&'()*+,-./0123456789:;<=>?"
                                L"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                                L"`abcdefghijklmnopqrstuvwxyz{|}~";

    //_freetype_gl_atlas = texture_atlas_new(512, 512, 3);  // RGB
    if (NULL == _freetype_gl_atlas) {
        _freetype_gl_atlas = texture_atlas_new(512, 512, 1);    // alpha only
    }

    //if (NULL != _freetype_gl_font) {
    //    texture_font_delete(_freetype_gl_font);
    //    _freetype_gl_font = NULL;
    //}
    if (NULL != _freetype_gl_font[0]) {
        texture_font_delete(_freetype_gl_font[0]);
        texture_font_delete(_freetype_gl_font[1]);
        texture_font_delete(_freetype_gl_font[2]);
        texture_font_delete(_freetype_gl_font[3]);
        _freetype_gl_font[0] = NULL;
        _freetype_gl_font[1] = NULL;
        _freetype_gl_font[2] = NULL;
        _freetype_gl_font[3] = NULL;
    }

    //_freetype_gl_font  = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, 20);
    _freetype_gl_font[0]  = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, 12);
    _freetype_gl_font[1]  = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, 18);
    _freetype_gl_font[2]  = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, 24);
    _freetype_gl_font[3]  = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, 30);
    if (NULL == _freetype_gl_font[0]) {
        PRINTF("WARNING: texture_font_new() failed\n");
        g_assert(0);
        return FALSE;
    }

    //texture_font_load_glyphs(_freetype_gl_font, cache);
    texture_font_load_glyphs(_freetype_gl_font[0], cache);
    texture_font_load_glyphs(_freetype_gl_font[1], cache);
    texture_font_load_glyphs(_freetype_gl_font[2], cache);
    texture_font_load_glyphs(_freetype_gl_font[3], cache);


    // PL module save VBO ID (a GLuint) as a unsigned int
    // this check document that
    //g_assert(sizeof(GLuint) == sizeof(unsigned int));

    if (0 == _text_textureID)
        glGenBuffers(1, &_text_textureID);

    if (NULL == _textWorkBuf)
        _textWorkBuf = g_array_new(FALSE, FALSE, sizeof(_freetype_gl_vertex_t));

    return TRUE;
}
#endif

static GLint     _tessd(GLUtriangulatorObj *tobj, S57_geo *geoData)
// WARNING: not re-entrant (tmpV)
{
    guint i, j;
    guint     nr   = S57_getRingNbr(geoData);
    S57_prim *prim = S57_initPrimGeo(geoData);

    // debug
    //if (2186==S57_getGeoID(geoData)) {
    //    S57_dumpData(geoData);
    //    _DEBUG = TRUE;
    //} else {
    //    _DEBUG = FALSE;
    //}

    // assume vertex place holder existe and is empty
    g_assert(NULL != _tmpV);
    g_assert(0    == _tmpV->len);

    // debug
    //PRINTF("npt: %i\n", nr);

    gluTessBeginPolygon(tobj, prim);
    for (i=0; i<nr; ++i) {
        guint  npt;
        //pt3   *ppt;
        GLdouble *ppt;

        // typecast from pt3* to double*
        //if (TRUE==S57_getGeoData(geoData, i, &npt, (double**)&ppt)) {
        if (TRUE==S57_getGeoData(geoData, i, &npt, &ppt)) {
            gluTessBeginContour(tobj);
            //for (j=0; j<npt; ++j, ++ppt)
            for (j=0; j<npt-1; ++j, ppt+=3) {
                //gluTessVertex(tobj, (GLdouble*)ppt, (void*)ppt);
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

    _g_ptr_array_clear(_tmpV);

    return TRUE;
}

static double    _computeSCAMIN(void)
// FIXME: this is a BUG get SCAMIN from charts
{
    double scalex = (_pmax.u - _pmin.u) / (double)_vp[2] * 10000.0;
    double scaley = (_pmax.v - _pmin.v) / (double)_vp[3] * 10000.0;


    return (scalex > scaley) ? scalex : scaley;
}


//-----------------------------------------
//
// gles2 float Matrix stuff (by hand)
//

#ifdef S52_USE_GLES2

//static GLint   _u_matrix  = -1;
//static GLfloat _view_rotx = 0.0;
//static GLfloat _view_roty = 0.0;


static void      _make_z_rot_matrix(GLfloat angle, GLfloat *m)
{
   float c = cos(angle * M_PI / 180.0);
   float s = sin(angle * M_PI / 180.0);

   int i;
   for (i = 0; i < 16; i++)
      m[i] = 0.0;

   m[0] = m[5] = m[10] = m[15] = 1.0;

   m[0] =  c;
   m[1] =  s;
   m[4] = -s;
   m[5] =  c;
}

static void      _make_scale_matrix(GLfloat xs, GLfloat ys, GLfloat zs, GLfloat *m)
{
   int i;
   for (i = 0; i < 16; i++)
      m[i] = 0.0;

   // FIXME: bzero()

   m[0]  = xs;
   m[5]  = ys;
   m[10] = zs;
   m[15] = 1.0;
}

#if 0
static void      _mul_matrix(GLfloat *prod, const GLfloat *a, const GLfloat *b)
{
#define A(row,col)  a[(col<<2)+row]
#define B(row,col)  b[(col<<2)+row]
#define P(row,col)  p[(col<<2)+row]
   GLfloat p[16];
   GLint i;
   for (i = 0; i < 4; i++) {
      const GLfloat ai0=A(i,0),  ai1=A(i,1),  ai2=A(i,2),  ai3=A(i,3);
      P(i,0) = ai0 * B(0,0) + ai1 * B(1,0) + ai2 * B(2,0) + ai3 * B(3,0);
      P(i,1) = ai0 * B(0,1) + ai1 * B(1,1) + ai2 * B(2,1) + ai3 * B(3,1);
      P(i,2) = ai0 * B(0,2) + ai1 * B(1,2) + ai2 * B(2,2) + ai3 * B(3,2);
      P(i,3) = ai0 * B(0,3) + ai1 * B(1,3) + ai2 * B(2,3) + ai3 * B(3,3);
   }
   memcpy(prod, p, sizeof(p));
#undef A
#undef B
#undef PROD
}
#endif

static void      _multiply(GLfloat *m, GLfloat *n)
{
   GLfloat tmp[16];
   const GLfloat *row, *column;
   div_t d;
   int i, j;

   for (i = 0; i < 16; i++) {
      tmp[i] = 0;
      d      = div(i, 4);
      row    = n + d.quot * 4;
      column = m + d.rem;
      for (j = 0; j < 4; j++)
          tmp[i] += row[j] * column[j * 4];
   }
   memcpy(m, &tmp, sizeof tmp);
}

static void      _glMatrixMode(GLenum  mode)
{
    _mode = mode;
    switch(mode) {
    	case GL_MODELVIEW:  _crntMat = _mvm[_mvmTop]; break;
    	case GL_PROJECTION: _crntMat = _pjm[_pjmTop]; break;
    	default: g_assert(0);
    }

    return;
}

static void      _glPushMatrix(void)
{
    GLfloat *lastMat = (GL_MODELVIEW == _mode) ? _mvm[_mvmTop] : _pjm[_pjmTop];

    switch(_mode) {
    	case GL_MODELVIEW:  _mvmTop += 1; break;
    	case GL_PROJECTION: _pjmTop += 1; break;
    	default: g_assert(0);
    }
    if (MATRIX_STACK_MAX<=_mvmTop || MATRIX_STACK_MAX<=_pjmTop) {
        PRINTF("ERROR: matrix stack overflow\n");
        g_assert(0);
    }

    _crntMat = (GL_MODELVIEW == _mode) ? _mvm[_mvmTop] : _pjm[_pjmTop];

    {   // duplicate mat
        int i;
        GLfloat *ptr = _crntMat;
        for (i=0; i<16; ++i)
            *ptr++ = *lastMat++;

        // FIXME:
        //memcpy(prod, p, sizeof(p));
    }

    return;
}

static void      _glPopMatrix(void)
{
    switch(_mode) {
    	case GL_MODELVIEW:  _mvmTop -= 1; break;
    	case GL_PROJECTION: _pjmTop -= 1; break;
    	default: g_assert(0);
    }

    if (_mvmTop<0 || _pjmTop<0) {
        PRINTF("ERROR: matrix stack underflow\n");
        g_assert(0);
    }

    return;
}

static void      _glTranslated(double x, double y, double z)
{

    GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  (GLfloat) x, (GLfloat) y, (GLfloat) z, 1 };
    //GLfloat t[16] = {1, (GLfloat) x, (GLfloat) y, (GLfloat) z,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};

    _multiply(_crntMat, t);

    return;
}

static void      _glScaled(double x, double y, double z)
{
    // debug
    //return;

    GLfloat m[16];

    _make_scale_matrix((GLfloat) x, (GLfloat) y, (GLfloat) z, m);

    _multiply(_crntMat, m);

    return;
}

static void      _glRotated(double angle, double x, double y, double z)
// rotate on Z
{
    GLfloat m[16];
    // FIXME: handle only 0.0==x 0.0==y 1.0==z
    // silence warning for now
    (void)x;
    (void)y;
    (void)z;

    _make_z_rot_matrix((GLfloat) angle, m);

    _multiply(_crntMat, m);

    return;
}

static void      _glLoadIdentity(void)
{
    int i;
    GLfloat *ptr = _crntMat;
    for (i=0; i<16; ++i)
        *ptr++ = 0.0;

    // FIXME
    // bzero(_crntMat, sizeof(GLfloat) * 16);

    _crntMat[0] = _crntMat[5] = _crntMat[10] = _crntMat[15] = 1.0;

    return;
}

static void      _glOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
{
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

    return;
}

static void      __gluMultMatrixVecf(const GLfloat matrix[16], const GLfloat in[4], GLfloat out[4])
{
    int i;

    for (i=0; i<4; i++) {
        out[i] = in[0] * matrix[0*4+i] +
                 in[1] * matrix[1*4+i] +
                 in[2] * matrix[2*4+i] +
                 in[3] * matrix[3*4+i];
    }
}

static int       __gluInvertMatrixf(const GLfloat m[16], GLfloat invOut[16])
{
    GLfloat inv[16], det;
    int i;

    inv[0] =   m[5]*m[10]*m[15] - m[5] *m[11]*m[14] - m[9]*m[6]*m[15]
             + m[9]*m[7] *m[14] + m[13]*m[6] *m[11] - m[13]*m[7]*m[10];
    inv[4] = - m[4]*m[10]*m[15] + m[4] *m[11]*m[14] + m[8]*m[6]*m[15]
             - m[8]*m[7] *m[14] - m[12]*m[6] *m[11] + m[12]*m[7]*m[10];
    inv[8] =   m[4]*m[9] *m[15] - m[4] *m[11]*m[13] - m[8]*m[5]*m[15]
             + m[8]*m[7] *m[13] + m[12]*m[5] *m[11] - m[12]*m[7]*m[9];
    inv[12]= - m[4]*m[9] *m[14] + m[4] *m[10]*m[13] + m[8]*m[5]*m[14]
             - m[8]*m[6] *m[13] - m[12]*m[5] *m[10] + m[12]*m[6]*m[9];
    inv[1] = - m[1]*m[10]*m[15] + m[1] *m[11]*m[14] + m[9]*m[2]*m[15]
             - m[9]*m[3] *m[14] - m[13]*m[2] *m[11] + m[13]*m[3]*m[10];
    inv[5] =   m[0]*m[10]*m[15] - m[0] *m[11]*m[14] - m[8]*m[2]*m[15]
             + m[8]*m[3] *m[14] + m[12]*m[2] *m[11] - m[12]*m[3]*m[10];
    inv[9] = - m[0]*m[9] *m[15] + m[0] *m[11]*m[13] + m[8]*m[1]*m[15]
             - m[8]*m[3] *m[13] - m[12]*m[1] *m[11] + m[12]*m[3]*m[9];
    inv[13]=   m[0]*m[9] *m[14] - m[0] *m[10]*m[13] - m[8]*m[1]*m[14]
             + m[8]*m[2] *m[13] + m[12]*m[1] *m[10] - m[12]*m[2]*m[9];
    inv[2] =   m[1]*m[6] *m[15] - m[1] *m[7] *m[14] - m[5]*m[2]*m[15]
             + m[5]*m[3] *m[14] + m[13]*m[2] *m[7]  - m[13]*m[3]*m[6];
    inv[6] = - m[0]*m[6] *m[15] + m[0] *m[7] *m[14] + m[4]*m[2]*m[15]
             - m[4]*m[3] *m[14] - m[12]*m[2] *m[7]  + m[12]*m[3]*m[6];
    inv[10]=   m[0]*m[5] *m[15] - m[0] *m[7] *m[13] - m[4]*m[1]*m[15]
             + m[4]*m[3] *m[13] + m[12]*m[1] *m[7]  - m[12]*m[3]*m[5];
    inv[14]= - m[0]*m[5] *m[14] + m[0] *m[6] *m[13] + m[4]*m[1]*m[14]
             - m[4]*m[2] *m[13] - m[12]*m[1] *m[6]  + m[12]*m[2]*m[5];
    inv[3] = - m[1]*m[6] *m[11] + m[1] *m[7] *m[10] + m[5]*m[2]*m[11]
             - m[5]*m[3] *m[10] - m[9] *m[2] *m[7]  + m[9]*m[3]*m[6];
    inv[7] =   m[0]*m[6] *m[11] - m[0] *m[7] *m[10] - m[4]*m[2]*m[11]
             + m[4]*m[3] *m[10] + m[8] *m[2] *m[7]  - m[8]*m[3]*m[6];
    inv[11]= - m[0]*m[5] *m[11] + m[0] *m[7] *m[9]  + m[4]*m[1]*m[11]
             - m[4]*m[3] *m[9]  - m[8] *m[1] *m[7]  + m[8]*m[3]*m[5];
    inv[15]=   m[0]*m[5] *m[10] - m[0] *m[6] *m[9]  - m[4]*m[1]*m[10]
             + m[4]*m[2] *m[9]  + m[8] *m[1] *m[6]  - m[8]*m[2]*m[5];

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det == 0)
        return GL_FALSE;

    det=1.0f/det;

    for (i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;

    return GL_TRUE;
}

static void      __gluMultMatricesf(const GLfloat a[16], const GLfloat b[16], GLfloat r[16])
{
    for (int i=0; i<4; ++i) {
        for (int j=0; j<4; ++j) {
            r[i*4+j] = a[i*4+0]*b[0*4+j] +
                       a[i*4+1]*b[1*4+j] +
                       a[i*4+2]*b[2*4+j] +
                       a[i*4+3]*b[3*4+j];
        }
    }
}


static GLint     _gluProject(GLfloat objx, GLfloat objy, GLfloat objz,
                             const GLfloat modelMatrix[16],
                             const GLfloat projMatrix[16],
                             const GLint   viewport[4],
                             GLfloat* winx, GLfloat* winy, GLfloat* winz)
{
    GLfloat in [4] = {objx, objy, objz, 1.0};
    GLfloat out[4];

    //in[0] = objx;
    //in[1] = objy;
    //in[2] = objz;
    //in[3] = 1.0;

    __gluMultMatrixVecf(modelMatrix, in,  out);
    __gluMultMatrixVecf(projMatrix,  out, in);

    if (0.0 == in[3])
        return GL_FALSE;

    in[0] /= in[3];
    in[1] /= in[3];
    in[2] /= in[3];

    /* Map x, y and z to range 0-1 */
    in[0] = in[0] * 0.5f + 0.5f;
    in[1] = in[1] * 0.5f + 0.5f;
    in[2] = in[2] * 0.5f + 0.5f;

    /* Map x,y to viewport */
    in[0] = in[0] * viewport[2] + viewport[0];
    in[1] = in[1] * viewport[3] + viewport[1];

    *winx = in[0];
    *winy = in[1];
    *winz = in[2];

    return GL_TRUE;
}

static GLint     _gluUnProject(GLfloat winx, GLfloat winy, GLfloat winz,
                               const GLfloat modelMatrix[16],
                               const GLfloat projMatrix[16],
                               const GLint viewport[4],
                               GLfloat* objx, GLfloat* objy, GLfloat* objz)
{
    GLfloat finalMatrix[16];
    GLfloat in [4];
    GLfloat out[4];

    __gluMultMatricesf(modelMatrix, projMatrix, finalMatrix);
    if (!__gluInvertMatrixf(finalMatrix, finalMatrix)) {
        return GL_FALSE;
    }

    in[0]=winx;
    in[1]=winy;
    in[2]=winz;
    in[3]=1.0;

    /* Map x and y from window coordinates */
    in[0] = (in[0] - viewport[0]) / viewport[2];
    in[1] = (in[1] - viewport[1]) / viewport[3];

    /* Map to range -1 to 1 */
    in[0] = in[0] * 2 - 1;
    in[1] = in[1] * 2 - 1;
    in[2] = in[2] * 2 - 1;

    __gluMultMatrixVecf(finalMatrix, in, out);
    if (out[3] == 0.0) {
        return GL_FALSE;
    }

    out[0] /= out[3];
    out[1] /= out[3];
    out[2] /= out[3];
    *objx = out[0];
    *objy = out[1];
    *objz = out[2];

    return GL_TRUE;
}

#endif
//-----------------------------------------



static GLint     _pushScaletoPixel(int scaleSym)
{

    double scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
    double scaley = (_pmax.v - _pmin.v) / (double)_vp[3];

    if (TRUE == scaleSym) {
        scalex /= (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
        scaley /= (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0);
    }

#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glPushMatrix();
    _glScaled(scalex, scaley, 1.0);
#else
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glScaled(scalex, scaley, 1.0);
#endif

    _checkError("_pushScaletoPixel(TRUE)");

    return TRUE;
}

static GLint     _popScaletoPixel(void)
{
#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glPopMatrix();
    // send to shader previous matrix
    //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
#endif
    _checkError("_popScaletoPixel()");

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
            near   = Z_CLIP_PLANE, far   = 1 - Z_CLIP_PLANE;
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


#ifdef S52_USE_GLES2
    _glMatrixMode(GL_PROJECTION);
    _glPushMatrix();
    _glLoadIdentity();
    _glOrtho(left, right, bottom, top, znear, zfar);
    //gluOrtho2D(left, right, bottom, top);

    _glTranslated(  (left+right)/2.0,  (bottom+top)/2.0, 0.0);
    _glRotated   (_north, 0.0, 0.0, 1.0);
    _glTranslated( -(left+right)/2.0, -(bottom+top)/2.0, 0.0);

    _glMatrixMode(GL_MODELVIEW);
    _glPushMatrix();
    _glLoadIdentity();

    _checkError("_glMatrixSet() -0-");

    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(left, right, bottom, top, znear, zfar);
    //gluOrtho2D(left, right, bottom, top);

    glTranslated(  (left+right)/2.0,  (bottom+top)/2.0, 0.0);
    glRotated   (_north, 0.0, 0.0, 1.0);
    glTranslated( -(left+right)/2.0, -(bottom+top)/2.0, 0.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    _checkError("_glMatrixSet() -0-");

#endif

    _checkError("_glMatrixSet() -fini-");

    return TRUE;
}

static GLint     _glMatrixDel(VP vpcoord)
// pop matrix GL_PROJECTION & GL_MODELVIEW
{
    // vpcoord not used, just there so that it match _glMatrixSet()
    (void) vpcoord;

#ifdef S52_USE_GLES2
    _glMatrixMode(GL_PROJECTION);
    _glPopMatrix();

    _checkError("_glMatrixDel() -1-");

    _glMatrixMode(GL_MODELVIEW);
    _glPopMatrix();

    glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
#endif

    _checkError("_glMatrixDel() -2-");

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
#ifdef S52_USE_GLES2
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
/*
#ifdef S52_USE_OPENGL_SAFETY_CRITICAL
    // BUG: access to matrix is in integer
    // BUT gluProject() for OpenGL ES use GLfloat (make sens)
    // so how to get matrix in float!
    //GLfloat mvm[16];       // OpenGL ES SC
    //GLfloat pjm[16];       // OpenGL ES SC
    GLint  mvm[16];       // OpenGL ES SC
    GLint  pjm[16];       // OpenGL ES SC

    glGetIntegerv(GL_MODELVIEW_MATRIX, mvm);   // OpenGL ES SC
    glGetIntegerv(GL_PROJECTION_MATRIX, pjm);  // OpenGL ES SC
#else
    GLdouble mvm[16];       // modelview matrix
    GLdouble pjm[16];       // projection matrix
    glGetDoublev(GL_MODELVIEW_MATRIX, mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, pjm);
#endif
*/

    // FIXME: get MODELVIEW_MATRIX & PROJECTION_MATRIX once per draw
    // less opengl call, less chance to mix up matrix in _prj2win()

    // FIXME: find a better way -
    if (0 == _pjm[0])
        return p;

    // debug
    //PRINTF("_VP[]: %i,%i,%i,%i\n",_vp[0],_vp[1],_vp[2],_vp[3]);

#ifdef S52_USE_GLES2
    float u = p.u;
    float v = p.v;
    float dummy_z = 0.0;

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
    // FIXME: find a better way -
    if (0 == _pjm[0]) {
        g_assert(0);
        return FALSE;
    }

    _glMatrixSet(VP_PRJ);

    int ret = _win2prj(x, y);

    _glMatrixDel(VP_PRJ);

    return ret;
}

//int        S52_GL_prj2win(double *x, double *y, double *z)
int        S52_GL_prj2win(double *x, double *y)
// convert coordinate: projected --> windows
{

    projXY uv = {*x, *y};

    uv = _prj2win(uv);

    *x = uv.u;
    *y = uv.v;
    //*z = 0.0;

    return TRUE;
}

static void      _glLineStipple(GLint  factor,  GLushort  pattern)
{
#ifdef S52_USE_GLES2
    // silence warning
    (void)factor;
    (void)pattern;

    // turn ON stippling
    //glUniform1f(_uStipOn, 1.0);

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
#ifdef S52_USE_GLES2
    //glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    //then 'gl_PointSize' in the shader becomme active
    glUniform1f(_uPointSize, size);
#else
    glPointSize(size);
#endif

    return;
}

#ifndef S52_USE_GLES2
static GLvoid    _DrawArrays_QUADS(guint npt, vertex_t *ppt)
{
    if (npt != 4)
        return;

    glVertexPointer(3, GL_DBL_FLT, 0, ppt);

    glDrawArrays(GL_QUADS, 0, npt);

    _checkError("_DrawArrays_QUADS() end");

    return;
}

static GLvoid    _DrawArrays_TRIANGLE_FAN(guint npt, vertex_t *ppt)
{
    if (npt != 4)
        return;


    //glEnableClientState(GL_VERTEX_ARRAY);

    glVertexPointer(3, GL_DBL_FLT, 0, ppt);

    glDrawArrays(GL_TRIANGLE_FAN, 0, npt);
    glDisableVertexAttribArray(_aPosition);

    _checkError("_DrawArrays_TRIANGLE_FAN() end");

    return;
}
#endif  // !S52_USE_GLES2

static GLvoid    _DrawArrays_LINE_STRIP(guint npt, vertex_t *ppt)
{
    /*
    // debug - test move S52 layer on Z
    guint i = 0;
    double *p = ppt;
    for (i=0; i<npt; ++i) {
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

#ifdef S52_USE_GLES2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
#else
    //glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
#endif

    glDrawArrays(GL_LINE_STRIP, 0, npt);
    glDisableVertexAttribArray(_aPosition);

    _checkError("_DrawArrays_LINE_STRIP() .. end");

    return;
}

static GLvoid    _DrawArrays_LINES(guint npt, vertex_t *ppt)
// this is used when VRM line style is aternate line style
// ie _normallinestyle == 'N'
{
    if (npt < 2)
        return;

#ifdef S52_USE_GLES2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0, ppt);
#else
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_DBL_FLT, 0, ppt);
#endif

    glDrawArrays(GL_LINES, 0, npt);

    glDisableVertexAttribArray(_aPosition);

    _checkError("_DrawArrays_LINES() .. end");

    return;
}

#ifndef S52_USE_GLES2
static GLvoid    _DrawArrays_POINTS(guint npt, vertex_t *ppt)
{
    if (npt == 0)
        return;

    glVertexPointer(3, GL_DBL_FLT, 0, ppt);

    glDrawArrays(GL_POINTS, 0, npt);
    glDisableVertexAttribArray(_aPosition);

    _checkError("_DrawArrays_POINTS() -end-");

    return;
}
#endif

#ifdef S52_USE_OPENGL_VBO
static int       _VBODrawArrays(S57_prim *prim)
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

#ifndef S52_USE_GLES2
    glVertexPointer(3, GL_DBL_FLT,  0, 0);
#endif

    for (guint i=0; i<primNbr; ++i) {
        GLint mode  = 0;
        GLint first = 0;
        GLint count = 0;

        S57_getPrimIdx(prim, i, &mode, &first, &count);

        // debug
        //PRINTF("i:%i mode:%i first:%i count:%i\n", i, mode, first, count);
        //if (i == 57) {
        //    PRINTF("57 FOUND!\n");
        //}

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
// create a VBO, return vboID
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     vboID   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &vboID))
        return FALSE;

    if (GL_FALSE == glIsBuffer(vboID)) {
        glGenBuffers(1, &vboID);

        if (0 == vboID) {
            PRINTF("ERROR: glGenBuffers() fail \n");
            g_assert(0);
        }

        // bind VBO in order to use
        glBindBuffer(GL_ARRAY_BUFFER, vboID);

        // upload data to VBO
        glBufferData(GL_ARRAY_BUFFER, vertNbr*sizeof(vertex_t)*3, (const void *)vert, GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        PRINTF("ERROR: VBO allready set!\n");
        g_assert(0);
    }

    _checkError("_VBOCreate()");

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

    if (GL_FALSE == glIsBuffer(vboID)) {
        vboID = _VBOCreate(prim);
        S57_setPrimDList(prim, vboID);
    }

    // bind VBOs for vertex array
    glBindBuffer(GL_ARRAY_BUFFER, vboID);      // for vertex coordinates

#ifdef S52_USE_GLES2
    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
#else
    //glVertexPointer(3, GL_DOUBLE, 0, 0);          // need to reset offset in VBO
                                                  // (maybe a fglrx quirk)
#endif

    _VBODrawArrays(prim);

    glDisableVertexAttribArray(_aPosition);

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _checkError("_VBODraw() -fini-");

    return TRUE;
}

static int       _VBOvalidate(S52_DListData *DListData)
{
    if (FALSE == glIsBuffer(DListData->vboIds[0])) {
        guint i = 0;
        for (i=0; i<DListData->nbr; ++i) {
            DListData->vboIds[i] = _VBOCreate(DListData->prim[i]);
            if (FALSE == glIsBuffer(DListData->vboIds[i]))
                return FALSE;
        }
        // return to normal mode
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

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

    guint i = 0;
    for (i=0; i<primNbr; ++i) {
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

static int       _createDList(S57_prim *prim)
// create or run display list
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;
    guint     DList   = 0;

    if (FALSE == S57_getPrimData(prim, &primNbr, &vert, &vertNbr, &DList))
        return FALSE;

    // no glIsList() in "OpenGL ES SC"
    if (GL_FALSE == glIsList(DList)) {
    //if (0 == DList) {
        DList = glGenLists(1);
        if (0 == DList) {
            PRINTF("ERROR: glGenLists() failed\n");
            g_assert(0);
        }

        glNewList(DList, GL_COMPILE);

        _checkError("_createDList()");


        _DrawArrays(prim);

        glEndList();

        S57_setPrimDList(prim, DList);
    }

    if (GL_TRUE == glIsList(DList)) {
        glCallList(DList);
        //glCallLists(1, GL_UNSIGNED_INT, &DList);
    }

    _checkError("_createDList()");

    return TRUE;
}
#endif // S52_USE_OPENGL_VBO

static int       _fillarea(S57_geo *geoData)
{
    // debug
    //return 1;
    //if (2186 == S57_getGeoID(geoData)) {
    //    PRINTF("found!\n");
    //    return TRUE;
    //}
    // LNDARE
    //if (557 == S57_getGeoID(geoData)) {
    //    PRINTF("LNDARE found!\n");
    //    return TRUE;
    //}


    S57_prim *prim = S57_getPrimGeo(geoData);
    if (NULL == prim) {
        _tessd(_tobj, geoData);
        prim = S57_getPrimGeo(geoData);
    }


#ifdef S52_USE_OPENGL_VBO
    _VBODraw(prim);
#else
    _createDList(prim);
#endif

    return TRUE;
}


//---------------------------------------
//
// SYMBOLOGY INSTRUCTION RENDERER SECTION
//
//---------------------------------------
    struct col {
        GLubyte r;
        GLubyte g;
        GLubyte b;
        GLubyte a;
    } col;
    //struct col {
    //    GLbyte r;
    //    GLbyte g;
    //    GLbyte b;
    //    GLbyte a;
    //} col;

union cIdx {
    struct col color;
    unsigned int idx;

};

static union cIdx _cIdx, _read[81];

static int       _setBlend(int blend)
// TRUE turn on blending if AA
{
    //static int blendstate = FALSE;
    if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
        if (TRUE == blend) {
            glEnable(GL_BLEND);

#ifdef S52_USE_GLES2
#else
            glEnable(GL_LINE_SMOOTH);
            //glEnable(GL_ALPHA_TEST);
#endif
        } else {
            glDisable(GL_BLEND);

#ifdef S52_USE_GLES2
#else
            glDisable(GL_LINE_SMOOTH);
            //glDisable(GL_ALPHA_TEST);
#endif
        }
    }

    _checkError("_setBlend()");

    return TRUE;
}

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

    _checkError("_glColor4ub() -0-");

    if (TRUE == _doPick) {
        // debug
        //printf("_glColor4ub: set current cIdx R to : %X\n", _cIdx.color.r);

#ifdef S52_USE_GLES2
        glUniform4f(_uColor, _cIdx.color.r/255.0, _cIdx.color.g/255.0, _cIdx.color.b/255.0, _cIdx.color.a);
#else
        glColor4ub(_cIdx.color.r, _cIdx.color.g, _cIdx.color.b, _cIdx.color.a);
#endif

        _checkError("_glColor4ub() -1-");

        return (GLubyte) 1;

    }

    if ('0' != c->trans) {
        // FIXME: some symbol allway use blending
        // but now with GLES2 AA its all or nothing
        if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS))
            glEnable(GL_BLEND);

#ifdef S52_USE_GLES2
#else
        glEnable(GL_ALPHA_TEST);
#endif
    }

    if (TRUE == _highlight) {
        S52_Color *dnghlcol = S52_PL_getColor("DNGHL");
#ifdef S52_USE_GLES2
        glUniform4f(_uColor, dnghlcol->R/255.0, dnghlcol->G/255.0, dnghlcol->B/255.0, (4 - (c->trans - '0')) * TRNSP_FAC_GLES2);
#else
        glColor4ub(dnghlcol->R, dnghlcol->G, dnghlcol->B, (4 - (c->trans - '0')) * TRNSP_FAC);
#endif
    } else {
#ifdef S52_USE_GLES2
        glUniform4f(_uColor, c->R/255.0, c->G/255.0, c->B/255.0, (4 - (c->trans - '0')) * TRNSP_FAC_GLES2);
#else
        glColor4ub(c->R, c->G, c->B, (4 - (c->trans - '0'))*TRNSP_FAC);
        //glColor4ub(255, 0, 0, 255);
#endif
        if (0 != c->pen_w) {  // AC, .. doesn't have en pen_w
            glLineWidth (c->pen_w - '0');
            _glPointSize(c->pen_w - '0');
        }
    }

    // check if PL init OK
    //if (0 == c->trans) {
    //    PRINTF("ERROR: color trans not set!\n");
    //    g_assert(0);
    //}

    _checkError("_glColor4ub() -last-");

    return c->trans;
}

static int       _glCallList(S52_DListData *DListData)
// get color of each Display List then run it
{
    unsigned int i = 0;

    if (NULL == DListData)
        return FALSE;

#ifdef S52_USE_GLES2
    // set shader matrix before draw
    //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#endif

    GLuint     lst = DListData->vboIds[0];
    S52_Color *col = DListData->colors;

    for (i=0; i<DListData->nbr; ++i, ++lst, ++col) {

        GLubyte trans = _glColor4ub(col);

#ifdef  S52_USE_OPENGL_VBO
        GLuint vboId = DListData->vboIds[i];

        if (FALSE == glIsBuffer(vboId)) {
            PRINTF("ERROR: invalid VBO!\n");
            g_assert(0);
            return FALSE;
        }

        glBindBuffer(GL_ARRAY_BUFFER, vboId);         // for vertex coordinates

#ifdef S52_USE_GLES2
        glEnableVertexAttribArray(_aPosition);
        glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);
#else
        glVertexPointer(3, GL_DOUBLE, 0, 0);          // need to reset offset in VBO
                                                      // (maybe a fglrx quirk)
#endif

        {
            guint j     = 0;
            GLint mode  = 0;
            GLint first = 0;
            GLint count = 0;

            while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {
                if (_QUADRIC_TRANSLATE == mode) {
                    GArray *vert = S57_getPrimVertex(DListData->prim[i]);

                    vertex_t *v = (vertex_t*)vert->data;
                    vertex_t  x = v[first*3+0];
                    vertex_t  y = v[first*3+1];
                    vertex_t  z = v[first*3+2];

#ifdef S52_USE_GLES2
                    _glTranslated(x, y, z);
                    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
                    glTranslated(x, y, z);
#endif

                } else {
                    // XOOM crash here (but not at cold start)
                    glDrawArrays(mode, first, count);
                }
                ++j;

            }
        }

        // switch back to normal pointer operation
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisableVertexAttribArray(_aPosition);

#else
        //glEnableClientState(GL_VERTEX_ARRAY);             // activate vertex coords array
        glVertexPointer(3, GL_DOUBLE, 0, 0);              // last param is offset, not ptr
        if (TRUE == glIsList(lst)) {
            //++_nFrag;
            glCallList(lst);              // NOT in OpenGL ES SC
            //glCallLists(1, GL_UNSIGNED_INT, &lst);
        } else
            g_assert(0);
#endif

        if ('0' != trans) {
#ifdef S52_USE_GLES2
#else
            glDisable(GL_ALPHA_TEST);
#endif
        }
    }

    _checkError("_glCallList()");

    return TRUE;
}

#if 0
static int       _parseTEX(S52_obj *obj)
// parse TE or TX
{
    /*
    S52_Color *col    = NULL;
    int        xoffs  = 0;
    int        yoffs  = 0;
    int        bsize  = 0;
    int        weight = 0;
    char      *str    = NULL;;

    str = S52_PL_getEX(obj, &col, &xoffs, &yoffs, &bsize, &weight);
    //PRINTF("xoffs/yoffs/bsize/weight: %i/%i/%i/%i\n", xoffs, yoffs, bsize, weight);
    if (NULL == str) {
        //PRINTF("NULL text string\n");
        return FALSE;
    }
    */

    return TRUE;
}
#endif

//static S52_CmdL *_renderTX(S52_obj *obj)
// TeXt
//{
//    if (NULL == instruc->text)
//        instruc->text = S52_PL_parseTX(instruc->geoData, cmd);
//
//    _renderTEXT(obj);
//
//    return cmd;
//}

//static S52_CmdL *_renderTE(S52_obj *obj)
// TExt formatted
//{
//    if (NULL == instruc->text)
//        instruc->text = S52_PL_parseTE(instruc->geoData, cmd);
//
//    _renderTEXT(instruc, cmd);
//
//    return cmd;
//}

//static GArray   *_computeCentroid(S57_geo *geoData)
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

        //return _centroids;
        return TRUE;
    }

    // CSG - Computational Solid Geometry  (clip poly)
    {   // assume vertex place holder existe and is empty
        g_assert(NULL != _tmpV);
        g_assert(0    == _tmpV->len);

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

        _g_ptr_array_clear(_tmpV);

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

    //return _centroids;
    return TRUE;
}

//static int       _getOwnshpAtt(S52_obj *obj, double *course, double *speed, double *period)
static int       _getVesselVector(S52_obj *obj, double *course, double *speed)
// return TRUE and course, speed, else FALSE
{
    //S57_geo *geo       = S52_PL_getGeo(obj);
    //GString *vecstbstr = S57_getAttVal(geo, "vecstb");
    // NOTE: ownship doesn't have a vecstb attribute, rather it take the value
    // from S52_MAR_VECSTB
    //gint     vecstb    = (NULL == vecstbstr) ? (int) S52_MP_get(S52_MAR_VECSTB) : S52_atoi(vecstbstr->str);


    /*
    {
        GString *sogspdstr = S57_getAttVal(geo, "sogspd");
        GString *stwspdstr = S57_getAttVal(geo, "stwspd");
        double   sogspd    = 0.0;
        double   stwspd    = 0.0;

        GString *cogcrsstr = S57_getAttVal(geo, "cogcrs");
        GString *ctwcrsstr = S57_getAttVal(geo, "ctwcrs");
        double   cogcrs    = 0.0;
        double   ctwcrs    = 0.0;
        //double   speed     = 0.0;


        if ((NULL==sogspdstr) && (NULL==stwspdstr))
            return FALSE;

        if ((NULL==cogcrsstr) && (NULL==ctwcrsstr))
            return FALSE;

        sogspd = (NULL == sogspdstr)? 0.0 : S52_atof(sogspdstr->str);
        stwspd = (NULL == stwspdstr)? 0.0 : S52_atof(stwspdstr->str);

        *speed = (sogspd > stwspd)? sogspd : stwspd;

        cogcrs = (NULL == cogcrsstr)? 0.0 : S52_atof(cogcrsstr->str);
        ctwcrs = (NULL == ctwcrsstr)? 0.0 : S52_atof(ctwcrsstr->str);

        *course= (cogcrs > ctwcrs)? cogcrs : ctwcrs;

        // *period= vecper;

        return TRUE;
    }
    */


    // OWNSHP
    /*
    if (0.0 == S52_MP_get(S52_MAR_VECSTB))
        return FALSE;

    if (1.0 == S52_MP_get(S52_MAR_VECSTB)) {
        GString *sogspdstr = S57_getAttVal(geo, "sogspd");
        GString *cogcrsstr = S57_getAttVal(geo, "cogcrs");

        *speed  = (NULL == sogspdstr)? 0.0 : S52_atof(sogspdstr->str);
        *course = (NULL == cogcrsstr)? 0.0 : S52_atof(cogcrsstr->str);

    } else {
        GString *stwspdstr = S57_getAttVal(geo, "stwspd");
        GString *ctwcrsstr = S57_getAttVal(geo, "ctwcrs");

        *speed  = (NULL == stwspdstr)? 0.0 : S52_atof(stwspdstr->str);
        *course = (NULL == ctwcrsstr)? 0.0 : S52_atof(ctwcrsstr->str);
    }
    */

    /*
    // none
    if (0 == vecstb)
        return FALSE;

    // ground
    if (1 == vecstb) {
        GString *sogspdstr = S57_getAttVal(geo, "sogspd");
        GString *cogcrsstr = S57_getAttVal(geo, "cogcrs");

        *speed  = (NULL == sogspdstr)? 0.0 : S52_atof(sogspdstr->str);
        *course = (NULL == cogcrsstr)? 0.0 : S52_atof(cogcrsstr->str);

        return TRUE;
    }

    // water
    if (2 == vecstb) {
        GString *stwspdstr = S57_getAttVal(geo, "stwspd");
        GString *ctwcrsstr = S57_getAttVal(geo, "ctwcrs");

        *speed  = (NULL == stwspdstr)? 0.0 : S52_atof(stwspdstr->str);
        *course = (NULL == ctwcrsstr)? 0.0 : S52_atof(ctwcrsstr->str);

        return TRUE;
    }
    */

    S57_geo *geo       = S52_PL_getGeo(obj);
    //GString *overGroundstr = S57_getAttVal(geo, "_overGround");
    //int      overGround    = (NULL == overGroundstr)? TRUE : S52_atoi(overGroundstr->str);
    //GString *vecstbstr = S57_getAttVal(geo, "vecstb");
    //gint     vecstb    = (NULL == vecstbstr) ? 0 : S52_atoi(vecstbstr->str);

    // ground
    //if (1 == vecstb) {
        GString *sogspdstr = S57_getAttVal(geo, "sogspd");
        GString *cogcrsstr = S57_getAttVal(geo, "cogcrs");

        *speed  = (NULL == sogspdstr)? 0.0 : S52_atof(sogspdstr->str);
        *course = (NULL == cogcrsstr)? 0.0 : S52_atof(cogcrsstr->str);

        // if no speed then draw no vector
        if (0.0 == *speed)
            return FALSE;

        return TRUE;
    //}

    // water
    //if (2 == vecstb) {
    //    GString *stwspdstr = S57_getAttVal(geo, "stwspd");
    //    GString *ctwcrsstr = S57_getAttVal(geo, "ctwcrs");
    //
    //    *speed  = (NULL == stwspdstr)? 0.0 : S52_atof(stwspdstr->str);
    //    *course = (NULL == ctwcrsstr)? 0.0 : S52_atof(ctwcrsstr->str);
    //
    //    return TRUE;
    //}

    //return FALSE;
}

static int       _renderSY_POINT_T(S52_obj *obj, double x, double y, double rotation)
{
    S52_DListData *DListData = S52_PL_getDListData(obj);
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
        return FALSE;
/*
    S52_DListData *DListData = S52_PL_getDListData(obj);
    if (NULL == DListData)
        return FALSE;
    if (FALSE == glIsBuffer(DListData->vboIds[0])) {
        guint i = 0;
        for (i=0; i<DListData->nbr; ++i) {
            DListData->vboIds[i] = _VBOCreate(DListData->prim[i]);
        }
        // return to normal mode
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
*/
    // debug
    //S57_geo *geoData   = S52_PL_getGeo(obj);
    //if (581 == S57_getGeoID(geoData)) {
    //    PRINTF("marfea found\n");
    //}
    //if (32 == S57_getGeoID(geoData)) {
    //    PRINTF("BOYLAT found\n");
    //}
    //if (8 == S57_getGeoID(geoData)) {
    //    PRINTF("BCNSPP found return\n");
    //    return TRUE;
    //}
    // bailout after the first 'n' draw
    //int n = 4;
    //if (n < _debugMatrix++) return TRUE;


#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    _glTranslated(x, y, 0.0);
    _glScaled(1.0, -1.0, 1.0);

    //_glRotated(rotation, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

    _pushScaletoPixel(TRUE);
    //_pushScaletoPixel(FALSE);

    _glRotated(rotation, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslated(x, y, 0.0);
    glScaled(1.0, -1.0, 1.0);

    //glRotated(rotation, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

    _pushScaletoPixel(TRUE);
    //_pushScaletoPixel(FALSE);

    glRotated(rotation, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
#endif


    _glCallList(DListData);

    _popScaletoPixel();

    return TRUE;
}

static int       _renderSY_silhoutte(S52_obj *obj)
// ownship & vessel (AIS)
{
    S57_geo       *geo       = S52_PL_getGeo(obj);
    GLdouble       orient    = S52_PL_getSYorient(obj);

    guint     npt = 0;
    GLdouble *ppt = NULL;

    // debug
    //return TRUE;

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    S52_DListData *DListData = S52_PL_getDListData(obj);
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
        return FALSE;

/*
    if (NULL == DListData)
        return FALSE;
    if (FALSE == glIsBuffer(DListData->vboIds[0])) {
        guint i = 0;
        for (i=0; i<DListData->nbr; ++i) {
            DListData->vboIds[i] = _VBOCreate(DListData->prim[i]);
        }
        // return to normal mode
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
*/

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

    //double symLenPixel = height / (100.0 * _dotpitch_mm_y);
    //double symBrdPixel = width  / (100.0 * _dotpitch_mm_x);
    double symLenPixel = height / (100.0 * 0.3);
    double symBrdPixel = width  / (100.0 * 0.3);

    // 2 - compute ship's length in pixel
    double scalex      = (_pmax.u - _pmin.u) / (double)_vp[2] ;
    double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;

    GString *shpbrdstr = S57_getAttVal(geo, "shpbrd");
    GString *shplenstr = S57_getAttVal(geo, "shplen");
    double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
    double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

    double shpBrdPixel = shpbrd / scalex;
    double shpLenPixel = shplen / scaley;

    // > 10 mm draw to scale
    //if (((shpLenPixel*_dotpitch_mm_y)>10.0) && (TRUE==S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {
    //if ((shpLenPixel*_dotpitch_mm_y) > 10.0) {
    if ((shpLenPixel*_dotpitch_mm_y) >= 6.0) {

        // 3 - compute stretch of symbol (ratio)
        double lenRatio = shpLenPixel / symLenPixel;
        double brdRatio = shpBrdPixel / symBrdPixel;

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        // FIXME: this work only for projected coodinate in meter
        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
        _glTranslated(0.0, -shp_off_y, 0.0);

        _pushScaletoPixel(TRUE);

        // apply stretch
        _glScaled(brdRatio, lenRatio, 1.0);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // FIXME: this work only for projected coodinate in meter
        glTranslated(ppt[0], ppt[1], 0.0);
        glScaled(1.0, -1.0, 1.0);
        glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
        glTranslated(0.0, -shp_off_y, 0.0);

        _pushScaletoPixel(TRUE);

        // apply stretch
        glScaled(brdRatio, lenRatio, 1.0);
#endif

        _glCallList(DListData);

        _popScaletoPixel();

    }

    //_SHIPS_OUTLINE_DRAWN = TRUE;

    _checkError("_renderSilhoutte() / POINT_T");

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
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData))) {
        PRINTF("DEBUG: no DListData\n");
        return FALSE;
    }

    // scale bar
    if (0==g_strcmp0(attval->str, "SCALEB10") ||
        0==g_strcmp0(attval->str, "SCALEB11") ) {
        int width;
        int height;
        S52_PL_getSYbbox(obj, &width, &height);

        // 1 - compute symbol size in pixel
        //double scaleSymWPixel = width  / (100.0 * _dotpitch_mm_x);
        //double scaleSymHPixel = height / (100.0 * _dotpitch_mm_y);
        double scaleSymWPixel = width  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
        double scaleSymHPixel = height / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));

        // 2 - compute symbol length in pixel
        double pixel_size_x   = (_pmax.u - _pmin.u) / (double)_vp[2] ;
        double pixel_size_y   = (_pmax.v - _pmin.v) / (double)_vp[3] ;

        // 3 - scale screen size
        double scale1NMWinWPixel =     4.0 / pixel_size_x; // not important since pen width will override
        double scale1NMWinHPixel =  1852.0 / pixel_size_y;

        // 3 - compute stretch of symbol (ratio)
        //double HRatio = scaleSymHPixel / pixel_size_x;
        //double WRatio = scaleSymWPixel / pixel_size_y;
        double HRatio = scale1NMWinHPixel / scaleSymHPixel;
        double WRatio = scale1NMWinWPixel / scaleSymWPixel;

        // set geo
        double x = 10.0; // 3 mm from left
        double y = 10.0; // bottom justifier


        _glMatrixSet(VP_PRJ);

        //S52_GL_win2prj(&x, &y);
        _win2prj(&x, &y);

#ifdef S52_USE_GLES2
        _glTranslated(x, y, 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
#else
        glTranslated(x, y, 0.0);
        glScaled(1.0, -1.0, 1.0);
        glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
#endif
        _pushScaletoPixel(TRUE);



        if (_SCAMIN < 80000.0) {
            // scale bar 1 NM
            if (0==g_strcmp0(attval->str, "SCALEB10")) {
                // apply stretch
#ifdef S52_USE_GLES2
                _glScaled(WRatio, HRatio, 1.0);
#else
                glScaled(WRatio, HRatio, 1.0);
#endif
                _glCallList(DListData);
            }
        } else {
            // scale bar 10 NM
            if (0==g_strcmp0(attval->str, "SCALEB11")) {
                // apply stretch
#ifdef S52_USE_GLES2
                _glScaled(WRatio, HRatio*10.0, 1.0);
#else
                glScaled(WRatio, HRatio*10.0, 1.0);
#endif

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

        //S52_GL_win2prj(&x, &y);
        _win2prj(&x, &y);


        _renderSY_POINT_T(obj, x, y, rotation);

        _glMatrixDel(VP_PRJ);

        return TRUE;
    }

    // depth unit
    if (0==g_strcmp0(attval->str, "UNITMTR1")) {
        // S52 specs say: left corner, just inside the scalebar [what does that mean - deside]
        double x = 30;
        double y = 20;

        _glMatrixSet(VP_PRJ);

        _win2prj(&x, &y);

        _renderSY_POINT_T(obj, x, y, _north);

        _glMatrixDel(VP_PRJ);

        return TRUE;
    }

    if (TRUE == S52_MP_get(S52_MAR_DISP_CALIB)) {
        // check symbol should be 5mm by 5mm
        if (0==g_strcmp0(attval->str, "CHKSYM01")) {
            double      x = 40;
            double      y = 50;
            double scalex = (_pmax.u - _pmin.u) / (double) _vp[2];
            double scaley = (_pmax.v - _pmin.v) / (double) _vp[3];

            _glMatrixSet(VP_PRJ);
            _win2prj(&x, &y);

#ifdef S52_USE_GLES2
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            _glTranslated(x, y, 0.0);
            _glScaled(scalex / (_dotpitch_mm_x * 100.0),
                      //scaley / (_dotpitch_mm_y * 100.0),
                      scaley / (_dotpitch_mm_x * 100.0),
                      1.0);

            _glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
#else
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glTranslated(x, y, 0.0);
            glScaled(scalex / (_dotpitch_mm_x * 100.0),
                     //scaley / (_dotpitch_mm_y * 100.0),
                     scaley / (_dotpitch_mm_x * 100.0),
                     1.0);

            glRotated(_north, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
#endif
            _glCallList(DListData);

            _glMatrixDel(VP_PRJ);

            return TRUE;
        }

        // symbol to be used for checking and adjusting the brightness and contrast controls
        if (0==g_strcmp0(attval->str, "BLKADJ01")) {
            //double x = 50;
            double x = 1230;
            //double y = 100;
            double y = 710;

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

    // get offset
    //GString *_ownshp_off_xstr = S57_getAttVal(geoData, "_ownshp_off_x");
    //GString *_ownshp_off_ystr = S57_getAttVal(geoData, "_ownshp_off_y");
    //double   _ownshp_off_x    = (NULL == _ownshp_off_xstr) ? 0.0 : S52_atof(_ownshp_off_xstr->str);
    //double   _ownshp_off_y    = (NULL == _ownshp_off_ystr) ? 0.0 : S52_atof(_ownshp_off_ystr->str);

/*
    // > 10 mm draw to scale
    if (TRUE==S52_MP_get(S52_MAR_SHIPS_OUTLINE) && (0==S52_PL_cmpCmdParam(obj, "OWNSHP05"))) {
        // compute ship symbol size on screen
        int width;  // breadth (beam)
        int height; // length
        S52_PL_getSYbbox(obj, &width, &height);

        // 1 - compute symbol size in pixel
        double symLenPixel = (height/10.0) * _dotpitch_mm_y;
        double symBrdPixel = (width /10.0) * _dotpitch_mm_x;

        // 2 - compute ship's length in pixel
        double scalex      = (_pmax.u - _pmin.u) / (double)_vp[2] ;
        double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;

        GString *shpbrdstr = S57_getAttVal(geoData, "shpbrd");
        GString *shplenstr = S57_getAttVal(geoData, "shplen");
        double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        double shpBrdPixel = shpbrd / scalex;
        double shpLenPixel = shplen / scaley;

        if ((shpLenPixel*_dotpitch_mm_y) > 10.0) {

            // 3 - compute stretch of symbol (ratio)
            double lenRatio = shpLenPixel / symLenPixel;
            double brdRatio = shpBrdPixel / symBrdPixel;

            // FIXME: this work only for projected coodinate in meter
            _glTranslated(ppt[0], ppt[1], 0.0);
            _glScaled(1.0, -1.0, 1.0);
            _glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

            //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
            _glTranslated(0.0, -_ownshp_off_y, 0.0);

            _pushScaletoPixel();

            // apply stretch
            _glScaled(brdRatio, lenRatio, 1.0);


            _glCallList(DListData);

            _popScaletoPixel();


            _SHIPS_OUTLINE_DRAWN = TRUE;

            _checkError("_renderSY() / POINT_T");
        }
        return TRUE;
    }

    // < 10 mm draw cercle
    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP01")) {
        if (FALSE == _SHIPS_OUTLINE_DRAWN) {

            //_renderSY_POINT_T(obj, ppt[0]+_ownshp_off_x, ppt[1]+_ownshp_off_y, orient);
            //_renderSY_POINT_T(obj, ppt[0]+_ownshp_off_x, ppt[1], orient);
            _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);

            _SHIPS_OUTLINE_DRAWN = TRUE;

            _checkError("_renderSY() / POINT_T");
        }
        return TRUE;
    }
*/

    /*
    //if ((0==S52_PL_cmpCmdParam(obj, "OWNSHP01")) || (0==S52_PL_cmpCmdParam(obj, "OWNSHP05"))) {
    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
        // compute ship symbol size on screen
        int width;  // breadth (beam)
        int height; // length
        S52_PL_getSYbbox(obj, &width, &height);

        // 1 - compute symbol size in pixel
        double symLenPixel = (height/10.0) * _dotpitch_mm_y;
        double symBrdPixel = (width /10.0) * _dotpitch_mm_x;

        // 2 - compute ship's length in pixel
        double scalex      = (_pmax.u - _pmin.u) / (double)_vp[2] ;
        double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;

        GString *shpbrdstr = S57_getAttVal(geoData, "shpbrd");
        GString *shplenstr = S57_getAttVal(geoData, "shplen");
        double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        double shpBrdPixel = shpbrd / scalex;
        double shpLenPixel = shplen / scaley;

        // > 10 mm draw to scale
        if ((shpLenPixel*_dotpitch_mm_y) > 10.0) {
            //if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
                // 3 - compute stretch of symbol (ratio)
                double lenRatio = shpLenPixel / symLenPixel;
                double brdRatio = shpBrdPixel / symBrdPixel;

                // FIXME: this work only for projected coodinate in meter
                _glTranslated(ppt[0], ppt[1], 0.0);
                _glScaled(1.0, -1.0, 1.0);
                _glRotated(orient, 0.0, 0.0, 1.0);    // rotate coord sys. on Z

                //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
                _glTranslated(0.0, -_ownshp_off_y, 0.0);

                _pushScaletoPixel();

                // apply stretch
                _glScaled(brdRatio, lenRatio, 1.0);


                _glCallList(DListData);

                _popScaletoPixel();

            //}
            //else {
            //    // < 10 mm draw cercle
            //    _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
            //}
        }
        //else {
        //    // < 10 mm draw cercle
        //    if (((shpLenPixel*_dotpitch_mm_y) < 10.0) && (0 == S52_PL_cmpCmdParam(obj, "OWNSHP01")))
        //        _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
        //}


        //_SHIPS_OUTLINE_DRAWN = TRUE;

        _checkError("_renderSY() / POINT_T");

        return TRUE;
    }
    */

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
        _renderSY_silhoutte(obj);
        return TRUE;
    }

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP01")) {
        // compute ship symbol size on screen
        //int width;  // breadth (beam)
        //int height; // length
        //S52_PL_getSYbbox(obj, &width, &height);

        // 1 - compute symbol size in pixel
        //double symLenPixel = (height/10.0) * _dotpitch_mm_y;
        //double symBrdPixel = (width /10.0) * _dotpitch_mm_x;

        // 2 - compute ship's length in pixel
        //double scalex      = (_pmax.u - _pmin.u) / (double)_vp[2] ;
        double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;

        //GString *shpbrdstr = S57_getAttVal(geoData, "shpbrd");
        GString *shplenstr = S57_getAttVal(geoData, "shplen");
        //double   shpbrd    = (NULL==shpbrdstr) ? 0.0 : S52_atof(shpbrdstr->str);
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        //double shpBrdPixel = shpbrd / scalex;
        double shpLenPixel = shplen / scaley;

        // drawn circle if silhoutte to small OR no silhouette at all
        //if ( ((shpLenPixel*_dotpitch_mm_y) < 10.0) || (FALSE==S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {
        if ( ((shpLenPixel*_dotpitch_mm_y) < 6.0) || (FALSE==S52_MP_get(S52_MAR_SHIPS_OUTLINE))) {
            _renderSY_POINT_T(obj, ppt[0], ppt[1], orient);
        }

        return TRUE;
    }

    // draw vector stabilization
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND01") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR01") ) {
        //double period = S52_MP_get(S52_MAR_VECPER);
        gint     vecper = (int) S52_MP_get(S52_MAR_VECPER);

        //if (0.0 != period) {
        if (0 != vecper) {
            // compute symbol offset due to course and seep
            //double course, speed, period;
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {

                double courseRAD = (90.0 - course)*DEG_TO_RAD;
                //double veclenNM  = period   * (speed /60.0);
                double veclenNM  = vecper   * (speed /60.0);
                double veclenM   = veclenNM * NM_METER;
                double veclenMX  = veclenM  * cos(courseRAD);
                double veclenMY  = veclenM  * sin(courseRAD);

                //_renderSY_POINT_T(obj, ppt[0]+veclenMX+_ownshp_off_x, ppt[1]+veclenMY+_ownshp_off_y, course);
                //_renderSY_POINT_T(obj, ppt[0]+veclenMX+_ownshp_off_x, ppt[1]+veclenMY, course);
                _renderSY_POINT_T(obj, ppt[0]+veclenMX, ppt[1]+veclenMY, course);

                _checkError("_renderSY() / POINT_T");
            }
        }
        return TRUE;
    }

    // time marks on vector - 6 min
    if (0 == S52_PL_cmpCmdParam(obj, "OSPSIX02")) {
        //double period = S52_MP_get(S52_MAR_VECPER);
        gint     vecper = (int) S52_MP_get(S52_MAR_VECPER);

        //if (0.0 != period) {
        if (0 != vecper) {
            // compute symbol offset of each 6 min mark
            //double course, speed, period;
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                //int    nmrk         = (int) period / 6.0;
                int    nmrk         = (int) (vecper / 6.0);
                int    i            = 0;

                for (i=0; i<nmrk; ++i) {
                    double ptx = ppt[0] + veclenM6minX*(i+1);
                    double pty = ppt[1] + veclenM6minY*(i+1);

                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty+_ownshp_off_y, course);
                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty, course);
                    _renderSY_POINT_T(obj, ptx, pty, course);

                    _checkError("_renderSY() / POINT_T");
                }
            }
        }
        return TRUE;
    }

    // time marks on vector - 1 min
    if (0 == S52_PL_cmpCmdParam(obj, "OSPONE02")) {
        //double period = S52_MP_get(S52_MAR_VECPER);
        gint     vecper = (int) S52_MP_get(S52_MAR_VECPER);

        //if (0.0 != period) {
        if (0 != vecper) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                //int    nmrk         = (int) period;
                int    nmrk         = vecper;
                int    i            = 0;

                for (i=0; i<nmrk; ++i) {
                    double ptx = ppt[0] + veclenM1minX*(i+1);
                    double pty = ppt[1] + veclenM1minY*(i+1);

                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty+_ownshp_off_y, course);
                    //_renderSY_POINT_T(obj, ptx+_ownshp_off_x, pty, course);
                    _renderSY_POINT_T(obj, ptx, pty, course);

                    _checkError("_renderSY() / POINT_T");
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
    GString  *vestatstr = S57_getAttVal(geo, "vestat");
    GString  *vecstbstr = S57_getAttVal(geo, "vecstb");
    GString  *headngstr = S57_getAttVal(geo, "headng");

    // debug
    //return TRUE;

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

#ifdef S52_USE_SYM_AISSEL01
    // AIS selected: experimental, put selected symbol on taget
    if ((0 == S52_PL_cmpCmdParam(obj, "AISSEL01")) &&
        (NULL!=vestatstr                           &&
        ('1'==*vestatstr->str || '2'==*vestatstr->str ))
       ) {
        GString *_vessel_selectstr = S57_getAttVal(geo, "_vessel_select");
        if ((NULL!=_vessel_selectstr) && ('Y'== *_vessel_selectstr->str)) {
            _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);
        }
        //return TRUE;
    }
#endif

    if (0 == S52_PL_cmpCmdParam(obj, "OWNSHP05")) {
        _renderSY_silhoutte(obj);
        return TRUE;
    }

    // draw vector stabilization
    if (0 == S52_PL_cmpCmdParam(obj, "VECGND21") ||
        0 == S52_PL_cmpCmdParam(obj, "VECWTR21") ) {
        gint     vecper    = (int) S52_MP_get(S52_MAR_VECPER);

        // FIXME: NO VECT STAB if target sleeping

        if (0 != vecper) {
            // compute symbol offset due to course and speed
            //double course, speed, period;
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
        gint     vecper    = S52_MP_get(S52_MAR_VECPER);

        if (0 != vecper) {
            // compute symbol offset of each 6 min mark
            //double course, speed, period;
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM6min = (speed / 60.0) * 6.0;
                double veclenM6min  = veclenNM6min * NM_METER;
                double veclenM6minX = veclenM6min  * cos(orientRAD);
                double veclenM6minY = veclenM6min  * sin(orientRAD);
                int    nmrk         = (int) (vecper/6);
                int    i            = 0;

                for (i=0; i<nmrk; ++i) {
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
        gint     vecper = S52_MP_get(S52_MAR_VECPER);

        if (0 != vecper) {
            // compute symbol offset of each 1 min mark
            double course, speed;
            if (TRUE == _getVesselVector(obj, &course, &speed)) {
                double orientRAD    = (90.0 - course)*DEG_TO_RAD;
                double veclenNM1min = speed / 60.0;
                double veclenM1min  = veclenNM1min * NM_METER;
                double veclenM1minX = veclenM1min  * cos(orientRAD);
                double veclenM1minY = veclenM1min  * sin(orientRAD);
                int    nmrk         = vecper;
                int    i            = 0;

                for (i=0; i<nmrk; ++i) {
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

        double scaley      = (_pmax.v - _pmin.v) / (double)_vp[3] ;

        GString *shplenstr = S57_getAttVal(geo, "shplen");
        double   shplen    = (NULL==shplenstr) ? 0.0 : S52_atof(shplenstr->str);

        double shpLenPixel = shplen / scaley;

        // drawn VESSEL symbol
        // 1 - if silhoutte too small
        // 2 - OR no silhouette at all
        if ( ((shpLenPixel*_dotpitch_mm_y) < 10.0) || (FALSE==S52_MP_get(S52_MAR_SHIPS_OUTLINE)) ) {
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
    if ((0 == S52_PL_cmpCmdParam(obj, "AISDEF01")) &&
        (NULL == headngstr)
       ) {
        // drawn upright
        _renderSY_POINT_T(obj, ppt[0], ppt[1], 0.0);

        return TRUE;
    }

    // AIS sleeping
    if ((0 == S52_PL_cmpCmdParam(obj, "AISSLP01")) &&
        (NULL!=vestatstr && '2'==*vestatstr->str)
       ) {
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

static int       _drawTextAA(S52_obj *obj, double x, double y, unsigned int bsize, unsigned int weight, const char *str);
static int       _renderSY_leglin(S52_obj *obj)
{
    S57_geo  *geo = S52_PL_getGeo(obj);
    guint     npt = 0;
    GLdouble *ppt = NULL;

    //if (0.0 == timetags)
    //    return FALSE;

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
            double scalex = (_pmax.u - _pmin.u) / (double)_vp[2] ;
            double scaley = (_pmax.v - _pmin.v) / (double)_vp[3] ;
            double offset_x = 10.0 * scalex;
            double offset_y = 18.0 * scaley;
            //double offset_x = 100.0;
            //double offset_y = 100.0;
            //double offset_x = 10.0 * scalex * cos(_north* DEG_TO_RAD);
            //double offset_y = 18.0 * scaley * sin(_north* DEG_TO_RAD);
            //double offset_x = (10.0 * cos(_north* DEG_TO_RAD)) + (100.0 );
            //double offset_y = (18.0 * sin(_north* DEG_TO_RAD)) + (100.0 );
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
            _drawTextAA(obj, ppt[0]+offset_x, ppt[1]-offset_y, 0, 0, str);


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

    if (S52_CMD_WRD_FILTER_SY & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    // bebug - filter out all but ..
    //if (0 != S52_PL_cmpCmdParam(obj, "BOYLAT23") &&
    //    0 != S52_PL_cmpCmdParam(obj, "BOYLAT14")) {
    //    return TRUE;
    //}
    //if (0 != strcmp(S57_getName(geoData), "SWPARE"))
    //    return TRUE;
    //if (0 == strcmp(S57_getName(geoData), "waypnt")) {
    //    PRINTF("%s\n", S57_getName(geoData));
    //}

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
                if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
                    return FALSE;

                S52_Color *col = DListData->colors;
                _glColor4ub(col);

                //_drawTextAA(ppt[0], ppt[1], 1, 1, str);

                //PRINTF("SOUNDG: %s, %f %f\n", str, ppt[2], datum);

                //_setBlend(FALSE);
                return TRUE;
            }
        }

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
                    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
                        return FALSE;

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

    // an SY command on a line object (ex light on power line)
    if (LINES_T == S57_getObjtype(geoData)) {
        unsigned int i;

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
        for (i=0; i<npt; ++i) {
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

    if (AREAS_T == S57_getObjtype(geoData)) {
        guint     npt = 0;
        GLdouble *ppt = NULL;
        if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt)) {
            return FALSE;
        }

        // debug
        //if (0 ==  g_strcmp0("MAGVAR", S57_getName(geoData), 6)) {
        //    PRINTF("MAGVAR found\n");
        //}
        //if (0 ==  g_strcmp0("M_NSYS", S57_getName(geoData), 6)) {
        //    PRINTF("M_NSYS found\n");
        //}
        //if (0 ==  g_strcmp0("ACHARE", S57_getName(geoData), 6)) {
        //    PRINTF("ACHARE found\n");
        //    //return TRUE;
        //}
        //if (0 ==  g_strcmp0("TSSLPT", S57_getName(geoData), 6)) {
        //    PRINTF("TSSLPT found\n");
        //}

        // clutter - skip rendering LOWACC01
        // this test might be useless in real life
        if ((0==S52_PL_cmpCmdParam(obj, "LOWACC01")) && (0.0==S52_MP_get(S52_MAR_QUAPNT01)))
            return TRUE;

        if (TRUE == _doPick) {
            double x;
            double y;

            // fill area, because other draw might not fill area
            // case of SY();LS(); (ie no AC() fill)
            {
                S52_DListData *DListData = S52_PL_getDListData(obj);
                if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
                    return FALSE;

                S52_Color *col = DListData->colors;
                _glColor4ub(col);

                // when in pick mode, fill the area
                _fillarea(geoData);
            }

            // centroid offset might put the symb outside the area
            while (TRUE == S57_getNextCentroid(geoData, &x, &y))
                _renderSY_POINT_T(obj, x, y, orient+_north);

            return TRUE;
        }


        {   // normal draw, also fill centroid
            double offset_x;
            double offset_y;
            _computeCentroid(geoData);

            // compute offset
            if (0 < _centroids->len) {
                //*
                S52_PL_getOffset(obj, &offset_x, &offset_y);

                // mm --> pixel
                offset_x /=  S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0;
                offset_y /=  S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0;

                {   // pixel --> PRJ coord
                    // scale offset
                    double scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
                    double scaley = (_pmax.v - _pmin.v) / (double)_vp[3];

                    offset_x *= scalex;
                    offset_y *= scaley;
                }
                //*/

                S57_newCentroid(geoData);
            }

            for (guint i=0; i<_centroids->len; ++i) {
                pt3 *pt = &g_array_index(_centroids, pt3, i);
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

    PRINTF("BUG: don't know how to draw this point symbol\n");
    //g_assert(0);

    _checkError("_renderSY()");

    return FALSE;
}

static int       _renderLS_LIGHTS05(S52_obj *obj)
{
    S57_geo *geoData   = S52_PL_getGeo(obj);
    GString *orientstr = S57_getAttVal(geoData, "ORIENT");
    GString *sectr1str = S57_getAttVal(geoData, "SECTR1");
    //double   sectr1    = (NULL==sectr1str) ? 0.0 : S52_atof(sectr1str->str);
    GString *sectr2str = S57_getAttVal(geoData, "SECTR2");
    //double   sectr2    = (NULL==sectr2str) ? 0.0 : S52_atof(sectr2str->str);

    //double   leglenpix = 25.0 / _dotpitch_mm_x;
    double   leglenpix = 25.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);
    projUV   p;                  // = {ppt[0], ppt[1]};
    double   z         = 0.0;


    GLdouble *ppt = NULL;
    guint     npt = 0;
    if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    //PRINTF("S1: %f, S2:%f\n", sectr1, sectr2);

#ifdef S52_USE_GLES2
    // make sure that _prj2win() has the right coordinate
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();
#endif

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
            p.u = ptlen.x;
            p.v = ptlen.y;
            //z = 0.0;
            p   = _prj2win(p);
            leglenpix = p.v;
            p.u = pt.x;
            p.v = pt.y;
            //z = 0.0;
            p   = _prj2win(p);
            //leglenpix -= p.v;
            leglenpix += p.v;

        }
    }

    //p.u = ppt[0];
    //p.v = ppt[1];
    //p = _prj2win(p);
    // debug
    //PRINTF("POSITION: lat: %f lon: %f pixX: %f pixY: %f\n", ppt[1], ppt[0], p.v, p.u);

    if (NULL != orientstr) {
        double o = S52_atof(orientstr->str);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        //_glTranslated(p.u, p.v, z);
        _glTranslated(ppt[0], ppt[1], z);
        //_glRotated(o+90.0, 0.0, 0.0, 1.0);
        _glRotated(90.0-o, 0.0, 0.0, 1.0);
        //_glRotated(40.0, 0.0, 0.0, 1.0);
        //_glScaled(1.0, -1.0, 1.0);
        //_glRotated(o+90.0-_north, 0.0, 0.0, 1.0);

        //_pushScaletoPixel(TRUE);
        _pushScaletoPixel(FALSE);

        //_glRotated(90.0-o, 0.0, 0.0, 1.0);

        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], z);
        glRotated(90.0-o, 0.0, 0.0, 1.0);
        _pushScaletoPixel(FALSE);
#endif
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
        _popScaletoPixel();

    }

    if (NULL != sectr1str) {
        double sectr1 = S52_atof(sectr1str->str);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        //_glTranslated(p.u, p.v, z);
        _glTranslated(ppt[0], ppt[1], z);
        //_glScaled(1.0, -1.0, 1.0);
        _glRotated(90.0-sectr1, 0.0, 0.0, 1.0);
        //_glRotated(sectr1+90.0-_north, 0.0, 0.0, 1.0);

        //_pushScaletoPixel(TRUE);
        _pushScaletoPixel(FALSE);
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], z);
        glRotated(90.0-sectr1, 0.0, 0.0, 1.0);
        _pushScaletoPixel(FALSE);
#endif
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
        _popScaletoPixel();

        // debug
        //PRINTF("ANGLE: %f, LEGLENPIX: %f\n", sectr1, leglenpix);
    }

    if (NULL != sectr2str) {
        double sectr2 = S52_atof(sectr2str->str);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        //_glTranslated(p.u, p.v, z);
        _glTranslated(ppt[0], ppt[1], z);
        //_glScaled(1.0, -1.0, 1.0);
        _glRotated(90-sectr2, 0.0, 0.0, 1.0);
        //_glRotated(sectr2+90.0-_north, 0.0, 0.0, 1.0);

        //_pushScaletoPixel(TRUE);
        _pushScaletoPixel(FALSE);
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], z);
        glRotated(90-sectr2, 0.0, 0.0, 1.0);
        _pushScaletoPixel(FALSE);
#endif
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-leglenpix, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
        _popScaletoPixel();

    }

    return TRUE;
}

static int       _renderLS_ownshp(S52_obj *obj)
{
    S57_geo *geo       = S52_PL_getGeo(obj);
    GString *headngstr = S57_getAttVal(geo, "headng");
    double   period    = S52_MP_get(S52_MAR_VECPER);

    GLdouble *ppt     = NULL;
    guint     npt     = 0;

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    //glEnable(GL_BLEND);

    // get offset
    //GString *_ownshp_off_xstr = S57_getAttVal(geo, "_ownshp_off_x");
    //GString *_ownshp_off_ystr = S57_getAttVal(geo, "_ownshp_off_y");
    //double   _ownshp_off_x    = (NULL == _ownshp_off_xstr) ? 0.0 : S52_atof(_ownshp_off_xstr->str);
    //double   _ownshp_off_y    = (NULL == _ownshp_off_ystr) ? 0.0 : S52_atof(_ownshp_off_ystr->str);

    //if ((NULL!=headngstr) && (FALSE==_SHIPS_OUTLINE_DRAWN)) {
    if ((NULL != headngstr) && (TRUE==S52_MP_get(S52_MAR_HEADNG_LINE))) {
        double orient = S52_PL_getSYorient(obj);

#ifdef S52_USE_GLES2
        // heading line
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        //_glTranslated(ppt[0]-_ownshp_off_x, ppt[1]-_ownshp_off_y, 0.0);
        //_glTranslated(ppt[0]+_ownshp_off_x, ppt[1], 0.0);
        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient-90.0, 0.0, 0.0, 1.0);

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);
        //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], 0.0);
        glScaled(1.0, -1.0, 1.0);
        glRotated(orient-90.0, 0.0, 0.0, 1.0);
#endif

        //*
        {
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {_rangeNM * NM_METER * 2.0, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
        //*/
        /*
        glBegin(GL_LINES);
            glVertex3d(0.0, 0.0, 0.0);
            // FIXME: draw to the edge of the screen
            // FIXME: coord. sys. must be in meter
            //glVertex3d(2.0 * NM_METER, 0.0, 0.0);
            glVertex3d(_rangeNM * NM_METER * 2.0, 0.0, 0.0);
        glEnd();
        //*/
    }

    // beam bearing line
    if (0.0 != S52_MP_get(S52_MAR_BEAM_BRG_NM)) {
        double orient    = S52_PL_getSYorient(obj);
        double beambrgNM = S52_MP_get(S52_MAR_BEAM_BRG_NM);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        //_glTranslated(ppt[0]+_ownshp_off_x, ppt[1]+_ownshp_off_y, 0.0);
        //_glTranslated(ppt[0]+_ownshp_off_x, ppt[1], 0.0);
        _glTranslated(ppt[0], ppt[1], 0.0);
        _glScaled(1.0, -1.0, 1.0);
        _glRotated(orient, 0.0, 0.0, 1.0);

        //_glTranslated(-_ownshp_off_x, -_ownshp_off_y, 0.0);

        //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], 0.0);
        glScaled(1.0, -1.0, 1.0);
        glRotated(orient, 0.0, 0.0, 1.0);
#endif
        {   // port
            pt3v pt[2] = {{0.0, 0.0, 0.0}, { beambrgNM * NM_METER, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
        {   // starboard
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {-beambrgNM * NM_METER, 0.0, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
    }

    // vector
    if (0 != period) {
        double course, speed;
        if (TRUE == _getVesselVector(obj, &course, &speed)) {
            double orientRAD = (90.0 - course) * DEG_TO_RAD;
            double veclenNM  = period   * (speed / 60.0);
            double veclenM   = veclenNM * NM_METER;
            double veclenMX  = veclenM  * cos(orientRAD);
            double veclenMY  = veclenM  * sin(orientRAD);
            pt3v   pt[2]     = {{0.0, 0.0, 0.0}, {veclenMX, veclenMY, 0.0}};
            //pt3v   pt[2]     = {{0.0, 0.0, 0.0}, {0.0, veclenM, 0.0}};

#ifdef S52_USE_GLES2
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            //_glTranslated(ppt[0]+_ownshp_off_x, ppt[1]+_ownshp_off_y, 0.0);
            //_glTranslated(ppt[0]+_ownshp_off_x, ppt[1], 0.0);
            _glTranslated(ppt[0], ppt[1], 0.0);

            //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
            glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glTranslated(ppt[0], ppt[1], 0.0);
#endif

            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
    }

    return TRUE;
}

static int       _renderLS_vessel(S52_obj *obj)
{
    S57_geo *geo       = S52_PL_getGeo(obj);
    GString *headngstr = S57_getAttVal(geo, "headng");
    //double   period    = S52_MP_get(S52_MAR_VECPER);
    double   vecper    = S52_MP_get(S52_MAR_VECPER);
    //GString *vecperstr = S57_getAttVal(geo, "vecper");
    //int      vecper    = (NULL==vecperstr) ? 0 : S52_atoi(vecperstr->str);

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

    //if (NULL!=headngstr && 1.0==S52_MP_get(S52_MAR_HEADNG_LINE))  {
    if (NULL!=headngstr && TRUE==S52_MP_get(S52_MAR_HEADNG_LINE))  {
        GLdouble *ppt     = NULL;
        guint     npt     = 0;
        //double    orient  = S52_PL_getSYorient(obj);

        if (TRUE == S57_getGeoData(geo, 0, &npt, &ppt)) {
            double headng = S52_atof(headngstr->str);
            // draw a line 50mm in length
            //pt3v pt[2] = {{0.0, 0.0, 0.0}, {50.0 / _dotpitch_mm_x, 0.0, 0.0}};
            pt3v pt[2] = {{0.0, 0.0, 0.0}, {50.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X), 0.0, 0.0}};

#ifdef S52_USE_GLES2
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            _glTranslated(ppt[0], ppt[1], ppt[2]);
            //_glScaled(1.0, -1.0, 1.0);
            //_glRotated(headng-90.0, 0.0, 0.0, 1.0);
            _glRotated(90.0 - headng, 0.0, 0.0, 1.0);
            _glScaled(1.0, -1.0, 1.0);

            _pushScaletoPixel(FALSE);

            //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
            glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glTranslated(ppt[0], ppt[1], ppt[2]);
            glRotated(90.0 - headng, 0.0, 0.0, 1.0);
            glScaled(1.0, -1.0, 1.0);

            _pushScaletoPixel(FALSE);
#endif
            _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);

            _popScaletoPixel();


        }
    }

    if (0 != vecper) {
        //double course, speed, period;
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

#ifdef S52_USE_GLES2
                _glMatrixMode(GL_MODELVIEW);
                _glLoadIdentity();

                //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
                glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
#endif
                _DrawArrays_LINE_STRIP(2, (vertex_t*)pt);
            }
        }
    }

    return TRUE;
}

//static int       _renderLS_afglow(S52_obj *obj)
static int       _renderLS_afterglow(S52_obj *obj)
{
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


#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    //glPointSize(pen_w);
    //glPointSize(10.0);
    //glPointSize(8.0);
    //_glPointSize(7.0);
    _glPointSize(4.0);
    //_glPointSize(24.0);
#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    //glPointSize(pen_w);
    //glPointSize(10.0);
    //glPointSize(8.0);
    //glPointSize(7.0);
    glPointSize(4.0);
#endif

    // fill color (alpha) array
    guint  i   = 0;
    guint  pti = S57_getGeoSize(geo);
    if (0 == pti)
        return TRUE;

    if (pti > npt) {
        PRINTF("ERROR: buffer overflow\n");
        exit(0);
        return FALSE;
    }

#ifdef S52_USE_GLES2
    //float   maxAlpha   = 1.0;   // 0.0 - 1.0
    //float   maxAlpha   = 0.5;   // 0.0 - 1.0
    float   maxAlpha   = 0.3;   // 0.0 - 1.0
#else
    float   maxAlpha   = 50.0;   // 0.0 - 255.0
#endif

    float crntAlpha = 0.0;
    float dalpha    = maxAlpha / pti;

#ifdef S52_USE_GLES2
    if (NULL == _aftglwColorArr)
        _aftglwColorArr = g_array_sized_new(FALSE, TRUE, sizeof(GLfloat), npt);

    g_array_set_size(_aftglwColorArr, 0);
    for (i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, crntAlpha);
        crntAlpha += dalpha;
    }
    // convert geo double (3) to float (3)
    _d2f(_tessWorkBuf_f, pti, ppt);
#else
    g_array_set_size(_aftglwColorArr, 0);
    for (i=0; i<pti; ++i) {
        g_array_append_val(_aftglwColorArr, col->R);
        g_array_append_val(_aftglwColorArr, col->G);
        g_array_append_val(_aftglwColorArr, col->B);
        unsigned char tmp = (unsigned char)crntAlpha;
        g_array_append_val(_aftglwColorArr, tmp);
        crntAlpha += dalpha;
    }
#endif

#ifdef S52_USE_GLES2
    // init VBO
    // useless since geo change all the time
    /*
    if (0 == _vboIDaftglwVertID) {
        glGenBuffers(1, &_vboIDaftglwVertID);
        glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwVertID);
        glBufferData(GL_ARRAY_BUFFER, npt * sizeof(GLfloat) * 3, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    */

    // init vbo for color
    // optimisation - fill VBO once with fixe alpha
    /*
    if (0 == _vboIDaftglwColrID) {
        glGenBuffers(1, &_vboIDaftglwColrID);
        glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwColrID);
        glBufferData(GL_ARRAY_BUFFER, npt * sizeof(GLfloat), NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    //*/

    // debug
    //if (228 == pti) {
    //    PRINTF("228\n");
    //}
    // FIXME: when pti = 229, glDrawArrays() call fail
    if (229 == pti) {
        PRINTF("pti == 229\n");
    }



    //glBindBuffer(GL_ARRAY_BUFFER, 0);

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    // vertex array - fill vbo arrays
    //glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwVertID);
    //glBufferSubData(GL_ARRAY_BUFFER, 0, _tessWorkBuf_f->len, _tessWorkBuf_f->data);
    glEnableVertexAttribArray(_aPosition);
    //glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0,  0);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 0,  _tessWorkBuf_f->data);
    //glVertexAttribPointer    (_aPosition, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 3,  _tessWorkBuf_f->data);
    //glVertexAttribPointer    (_aPosition, 4, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 3,  _tessWorkBuf_f->data);

    // turn ON after glow in shader
    glUniform1f(_uGlowOn, 1.0);

    // fill array with alpha
    //glBindBuffer(GL_ARRAY_BUFFER, _vboIDaftglwColrID);
    //glBufferSubData(GL_ARRAY_BUFFER, 0, _aftglwColorArr->len, _aftglwColorArr->data);
    glEnableVertexAttribArray(_aAlpha);
    //glVertexAttribPointer    (_aAlpha, 1, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribPointer    (_aAlpha, 1, GL_FLOAT, GL_FALSE, 0, _aftglwColorArr->data);
#else
    // init vbo for color
    if (0 == _vboIDaftglwColrID)
        glGenBuffers(1, &_vboIDaftglwColrID);

    // init VBO
    if (0 == _vboIDaftglwVertID)
        glGenBuffers(1, &_vboIDaftglwVertID);

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
#endif


    //_checkError("_renderLS_afterglow(): before glDrawArrays()");

    // 3 - draw
    // FIXME: when pti = 229, this call fail
    glDrawArrays(GL_POINTS, 0, pti);
    glDisableVertexAttribArray(_aPosition);


    // 4 - done
#ifdef S52_USE_GLES2
    // turn OFF after glow
    glUniform1f(_uGlowOn, 0.0);
    glDisableVertexAttribArray(_aPosition);
    glDisableVertexAttribArray(_aAlpha);
#else
    // deactivate color array
    glDisableClientState(GL_COLOR_ARRAY);

    // bind with 0 - switch back to normal pointer operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif

    //PRINTF("glDrawArrays() .. fini\n");
    _checkError("_renderLS_afterglow()");

    return TRUE;
}

static int       _renderLS(S52_obj *obj)
// Line Style
// FIXME: put in VBO!
{
    S52_Color *col;
    char       style;   // L/S/T
    char       pen_w;

#ifdef S52_USE_GV
    // FIXME
    return FALSE;
#endif

    _checkError("_renderLS() -0-");

    // debug
    //S57_geo  *geoData = S52_PL_getGeo(obj);
    //if (0 != g_strcmp0("leglin", S57_getName(geoData), 6)) {
    //    return TRUE;
    //}

    if (S52_CMD_WRD_FILTER_LS & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    S52_PL_getLSdata(obj, &pen_w, &style, &col);
    glLineWidth(pen_w - '0');
    //glLineWidth(pen_w - '0' + 0.1);  // WARNING: THIS +0.1 SLOW DOWN EVERYTHING
    //glLineWidth(pen_w - '0' + 0.5);
    //glLineWidth(pen_w - '0' + 0.375);
    //glLineWidth(pen_w - '0' - 0.5);
    //glLineWidth(1.0);

    //glColor4ub(col->R, col->G, col->B, 255);
    //glColor4ub(col->R, col->G, col->B, (4 - (col->trans - '0'))*TRNSP_FAC);
    //GLubyte trans = _glColor4ub(col);

    _glColor4ub(col);

    //_setBlend(TRUE);


    // debug
    //glLineWidth(5);
    //glColor3ub(255, 0, 0);
    //S57_geo   *geoData = S52_PL_getGeo(obj);
    //PRINTF("%s\n", S57_getName(geoData));

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

        //*  commented for debug
        if (POINT_T == S57_getObjtype(geoData)) {
            //if (0 == g_ascii_strncasecmp("LIGHTS", S57_getName(geoData), 6))
            if (0 == g_strcmp0("LIGHTS", S57_getName(geoData)))
                _renderLS_LIGHTS05(obj);
            else {
                //if (0 == g_ascii_strncasecmp("ownshp", S57_getName(geoData), 6))
                if (0 == g_strcmp0("ownshp", S57_getName(geoData)))
                    _renderLS_ownshp(obj);
                else {
                    if (0 == g_strcmp0("vessel", S57_getName(geoData)))
                        _renderLS_vessel(obj);
                }
            }
        }
        else
        //*/
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

            if (0 == g_strcmp0("ownshp", S57_getName(geoData)))
                _renderLS_ownshp(obj);
            else {
                if ((0 == g_strcmp0("afgves", S57_getName(geoData))) ||
                    (0 == g_strcmp0("afgshp", S57_getName(geoData)))
                   ) {
                    _renderLS_afterglow(obj);
                } else {

#ifdef S52_USE_GLES2
                    _glMatrixMode(GL_MODELVIEW);
                    _glLoadIdentity();
                    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
                    _d2f(_tessWorkBuf_f, npt, ppt);

                    // FIXME: move to _renderLS_setPatDott()
                    if (0 == g_strcmp0("leglin", S57_getName(geoData))) {
                        glUniform1f(_uStipOn, 1.0);
                        glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);

                        //float sym_x_px = 32 * 0.3 / _dotpitch_mm_x;
                        //float sym_x_mm = 32 * 0.3;
                        float dx       = ppt[0] - ppt[3];
                        float dy       = ppt[1] - ppt[4];
                        float leglen_m = sqrt(dx*dx + dy*dy);                     // leg length in meter
                        float px_m_x   = (_pmax.u - _pmin.u) / (double)_vp[2] ;   // scale X
                        //float px_m_y   = (_pmax.v - _pmin.v) / (double)_vp[3] ;
                        float leglen_px= leglen_m  / px_m_x;                      // leg length in pixel
                        //float sym_n    = leg_d_px / sym_x_px;
                        float sym_n    = leglen_px / 32.0;  // 32 pixels (rgba)
                        float ptr[4] = {
                            0.0,   0.0,
                            sym_n, 1.0
                        };

                        glEnableVertexAttribArray(_aUV);
                        glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 0, ptr);
                    }

                    _DrawArrays_LINE_STRIP(npt, (vertex_t *)_tessWorkBuf_f->data);
#else
                    glMatrixMode(GL_MODELVIEW);
                    glLoadIdentity();
                    _DrawArrays_LINE_STRIP(npt, (vertex_t *)ppt);
#endif
                }
            }
            //*/



#ifdef S52_USE_GLES2
            // Not usefull with AA
            //_d2f(_tessWorkBuf_f, npt, ppt);
            //_DrawArrays_POINTS(npt, (vertex_t *)_tessWorkBuf_f->data);
#else
            // add point on thick line to round corner
            // BUG: dot showup on transparent line
            // FIX: don't draw dot on tranparent line!
            //if ((3 <= (pen_w -'0')) && ('0'==col->trans))  {
            //if ((3 <= (pen_w -'0')) && ('0'==col->trans) && (TRUE==(int)S52_MP_get(S52_MAR_DISP_RND_LN_END))) {
            if ((3 <= (pen_w -'0')) && ('0'==col->trans) && (1.0==S52_MP_get(S52_MAR_DISP_RND_LN_END))) {
                _glPointSize(pen_w  - '0');

                _DrawArrays_POINTS(npt, (vertex_t *)ppt);
            }
#endif
        }
    }


#ifdef S52_USE_GLES2
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D,  0);
    // turn OFF stippling
    glUniform1f(_uStipOn, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

#else
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
{
    //int accept   = TRUE;
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

static int       _drawArc(S52_obj *objA, S52_obj *objB);
static int       _renderLC(S52_obj *obj)
// Line Complex (AREA, LINE)
{
    GLdouble       symlen      = 0.0;
    GLdouble       symlen_pixl = 0.0;
    GLdouble       symlen_wrld = 0.0;
    GLdouble       x1,y1,z1,  x2,y2;
    //GLdouble     z2;
    S57_geo       *geo = S52_PL_getGeo(obj);
    char           pen_w;
    S52_Color     *c;
    double         scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
    //double         scaley = (_pmax.v - _pmin.v) / (double)_vp[3];

    GLdouble *ppt = NULL;
    guint     npt = 0;

#ifdef S52_USE_GV
    // FIXME
    return FALSE;
#endif

    if (S52_CMD_WRD_FILTER_LC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    // draw arc if this is a leglin
    if (0 == g_strcmp0("leglin", S57_getName(geo))) {
        // check if user want to display arc
        if ((2.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) || (3.0==S52_MP_get(S52_MAR_DISP_WHOLIN))) {
            S52_obj *objNextLeg = S52_PL_getNextLeg(obj);

            if (NULL != objNextLeg)
                _drawArc(obj, objNextLeg);
        }
    }

    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt))
        return FALSE;

    // debug - Mariner
    //npt = S57_getGeoSize(geo);

    // set pen color & size here because values might not
    // be set via call list --short line
    S52_DListData *DListData = S52_PL_getDListData(obj);
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
        return FALSE;
/*
    DListData = S52_PL_getDListData(obj);
    if (NULL == DListData)
        return FALSE;
    if (FALSE == glIsBuffer(DListData->vboIds[0])) {
        guint i = 0;
        for (i=0; i<DListData->nbr; ++i) {
            DListData->vboIds[i] = _VBOCreate(DListData->prim[i]);
        }
        // return to normal mode
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
*/

    c = DListData->colors;
    _glColor4ub(c);

    S52_PL_getLCdata(obj, &symlen, &pen_w);
    glLineWidth(pen_w - '0');
    //glLineWidth(pen_w - '0' - 0.5);
    //glLineWidth(1);

    //symlen_pixl = symlen / (_dotpitch_mm_x * 100.0);
    symlen_pixl = symlen / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
    //symlen_pixl = symlen / (0.3 * 100.0);
    //symlen_pixl = symlen / (0.15 * 100.0);
    symlen_wrld = symlen_pixl * scalex;

    //{   // debug
    //    char  *name = S57_getName(geo);
    //    PRINTF("%s: _dotpitch_mm_x=%f, symlen_pixl=%f, symlen_wrld=%f\n", name, _dotpitch_mm_x, symlen_pixl, symlen_wrld);
    //}

    double off_x = ppt[0];
    double off_y = ppt[1];
    //double run   = 0.0;

    // debug
    //if (0 == g_strcmp0("HRBARE", S57_getName(geo))) {
    //    PRINTF("DEBUG: found HRBARE\n");
    //}
    //if (0 == g_strcmp0("PILBOP", S57_getName(geo))) {
    //    PRINTF("DEBUG: found PILBOP\n");
    //}

    for (guint i=0; i<npt-1; ++i) {
        // set coordinate
        x1 = ppt[0];
        y1 = ppt[1];
        z1 = ppt[2];
        ppt += 3;
        x2 = ppt[0];
        y2 = ppt[1];
        //z2 = ppt[2];

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

        /*
        {//is this segment on screen (visible)
            double x1, y1, x2, y2;
            S57_getExtPRJ(geo, &x1, &y1, &x2, &y2);
            // BUG: line crossing the screen get regected!
            if ((_pmin.u < x1) && (_pmin.v < y1) && (_pmax.u > x2) && (_pmax.v > y2)) {
                //PRINTF("no clip: %s\n", S57_getName(geo));
                ;
            } else {
                // PRINTF("outside view shouldn't get here (%s)\n", S57_getName(geo));
                continue;
            }
        }
        */

        // FIXME: symbol aligned to border (window coordinate),
        //        should be aligned to a 'grid' (geo coordinate)
        if (FALSE == _clipToView(&x1, &y1, &x2, &y2))
            continue;

        /*
        {//debug: this segment should be on screen (visible)
            //double x1, y1, x2, y2;
            //S57_getExtPRJ(geo, &x1, &y1, &x2, &y2);
            if ((x1 < _pmin.u) || (y1 < _pmin.v) || (_pmax.u < x2) || ( _pmax.v < y2)) {
                PRINTF("outside view, shouldn't get here (%s)\n", S57_getName(geo));
                g_assert(0);
            }
        }
        */

        _highlight = FALSE;
        //if ((-10.0==z1) && (-10.0==z2)) {
        if (z1 < 0.0) {
            //PRINTF("NOTE: this line segment (%s) overlap a line segment with higher prioritity (Z=%f)\n", S57_getName(geo), z1);
            _highlight = TRUE;
            continue;
        }



        GLdouble seglen_wrld = sqrt(pow((x1-off_x)-(x2-off_x), 2)  + pow((y1-off_y)-(y2-off_y), 2));
        //GLdouble seglen_pixl = seglen_wrld / scalex;
        //GLdouble segang = atan2((y2-off_y)-(y1-off_y), (x2-off_x)-(x1-off_x)) * RAD_TO_DEG;
        //GLdouble segang = atan2(y2-y1, x2-x1) * RAD_TO_DEG;
        GLdouble segang = atan2(y2-y1, x2-x1);

        //PRINTF("segang: %f seglen: %f symlen:%f\n", segang, seglen, symlen);
        //PRINTF(">> x1: %f y1: %f \n",x1, y1);
        //PRINTF(">> x2: %f y2: %f \n",x2, y2);
        //GLdouble symlen_pixl_x = cos(segang) * symlen_pixl;
        //GLdouble symlen_pixl_y = sin(segang) * symlen_pixl;
        GLdouble symlen_wrld_x = cos(segang) * symlen_wrld;
        GLdouble symlen_wrld_y = sin(segang) * symlen_wrld;


        int nsym = (int) (seglen_wrld / symlen_wrld);
        segang *= RAD_TO_DEG;

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

        //_setBlend(TRUE);

        //GLdouble offset_pixl_x = 0.0;
        //GLdouble offset_pixl_y = 0.0;
        GLdouble offset_wrld_x = 0.0;
        GLdouble offset_wrld_y = 0.0;

        int j = 0;
        // draw symb's as long as it fit the line length
        //for (run=0.0; run<(seglen_pix-symlen_pix); run+=symlen_pix) {
        for (j=0; j<nsym; ++j) {

#ifdef S52_USE_GLES2
            // reset origine
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            //_glTranslated(x1+offset_pix_x, y1+offset_pix_y, 0.0);           // move coord sys. at symb pos.
            _glTranslated(x1+offset_wrld_x, y1+offset_wrld_y, 0.0);           // move coord sys. at symb pos.
            _glRotated(segang, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
            _glScaled(1.0, -1.0, 1.0);
#else
            // reset origine
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            //_glTranslated(x1+offset_pix_x, y1+offset_pix_y, 0.0);           // move coord sys. at symb pos.
            glTranslated(x1+offset_wrld_x, y1+offset_wrld_y, 0.0);           // move coord sys. at symb pos.
            glRotated(segang, 0.0, 0.0, 1.0);    // rotate coord sys. on Z
            glScaled(1.0, -1.0, 1.0);

#endif
            _pushScaletoPixel(TRUE);

            _glCallList(DListData);

            _popScaletoPixel();

            //offset_pixl_x += (symlen_pixl_x * scalex);
            //offset_pixl_y += (symlen_pixl_y * scaley);
            offset_wrld_x += symlen_wrld_x;
            offset_wrld_y += symlen_wrld_y;
        }


        // FIXME: need this because some 'Display List' reset blending
        _setBlend(TRUE);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
#endif
        {   // complete the rest of the line
            pt3v pt[2] = {{x1+offset_wrld_x, y1+offset_wrld_y, 0.0}, {x2, y2, 0.0}};
            _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt);
        }
    }

    _highlight = FALSE;

    //_setBlend(FALSE);

    _checkError("_renderLC()");

    return TRUE;
}

static int       _1024bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
// make a RGBA texture from 32x32 bitmask (those used by glPolygonStipple() in OpenGL 1.x)
{
    int i=0;

    //bzero(rgba_mask, 4*32*8*4);
    memset(rgba_mask, 0, 4*32*8*4);
    for (i=0; i<(4*32); ++i) {
        if (0 != mask[i]) {
            if (mask[i] & (1<<0)) {
                //rgba_mask[(i*4*8)+0] = 255;  // R
                //rgba_mask[(i*4*8)+1] = 255;  // G
                //rgba_mask[(i*4*8)+2] = 255;  // B
                rgba_mask[(i*4*8)+3] = 255;   // A
            }
            if (mask[i] & (1<<1)) rgba_mask[(i*4*8)+ 7] = 255;  // A
            if (mask[i] & (1<<2)) rgba_mask[(i*4*8)+11] = 255;  // A
            if (mask[i] & (1<<3)) rgba_mask[(i*4*8)+15] = 255;  // A
            if (mask[i] & (1<<4)) rgba_mask[(i*4*8)+19] = 255;  // A
            if (mask[i] & (1<<5)) rgba_mask[(i*4*8)+23] = 255;  // A
            if (mask[i] & (1<<6)) rgba_mask[(i*4*8)+27] = 255;  // A
            if (mask[i] & (1<<7)) rgba_mask[(i*4*8)+31] = 255;  // A
        }
    }

    return TRUE;
}

static int       _32bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
//static int       _64bitMask2RGBAText(const GLubyte *mask, GLubyte *rgba_mask)
// make a RGBA texture from 32x32 bitmask (those used by glPolygonStipple() in OpenGL 1.x)
{
    int i=0;
    //bzero(rgba_mask, 8*4*4);      // 32
    //bzero(rgba_mask, 4*8*4*2);  // 64 * 4 (rgba)
    memset(rgba_mask, 0, 8*4*4);
    for (i=0; i<(8*1); ++i) {
    //for (i=0; i<(8*1*2); ++i) { // 16 bytes * 8 = 64 bits
        if (0 != mask[i]) {
            if (mask[i] & (1<<0)) {
                //rgba_mask[(i*4*8)+0] = 255;  // R
                //rgba_mask[(i*4*8)+1] = 255;  // G
                //rgba_mask[(i*4*8)+2] = 255;  // B
                rgba_mask[(i*4*8)+3] = 255;   // A
            }
            if (mask[i] & (1<<1)) rgba_mask[(i*4*8)+ 7] = 255;  // A
            if (mask[i] & (1<<2)) rgba_mask[(i*4*8)+11] = 255;  // A
            if (mask[i] & (1<<3)) rgba_mask[(i*4*8)+15] = 255;  // A
            if (mask[i] & (1<<4)) rgba_mask[(i*4*8)+19] = 255;  // A
            if (mask[i] & (1<<5)) rgba_mask[(i*4*8)+23] = 255;  // A
            if (mask[i] & (1<<6)) rgba_mask[(i*4*8)+27] = 255;  // A
            if (mask[i] & (1<<7)) rgba_mask[(i*4*8)+31] = 255;  // A
        }
    }

    return TRUE;
}

static int       _renderAC_NODATA_layer0(void)
// clear all buffer so that no artefact from S52_drawLast remain
{
    // clear screen with color NODATA
    S52_Color *c = S52_PL_getColor("NODTA");

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    glClearColor(c->R/255.0, c->G/255.0, c->B/255.0, 1.0);
    //glClearColor(c->R/255.0, c->G/255.0, c->B/255.0, 0.0);
    //glClearColor(c->R/255.0, 0.0, c->B/255.0, 0.0);
    //glClearColor(1.0, 0.0, 0.0, 1.0);

    //glDrawBuffer(GL_FRONT|GL_BACK);
    //glDrawBuffer(GL_BACK);

#ifdef S52_USE_TEGRA2
    // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase)
    // define in gl2ext.h
    //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
    glClear(GL_COVERAGE_BUFFER_BIT_NV | GL_COLOR_BUFFER_BIT);
#else
    glClear(GL_COLOR_BUFFER_BIT);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif

    //glDrawBuffer(GL_BACK);

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

        p.u = ppt[0];
        p.v = ppt[1];
        p = _prj2win(p);

        if (NULL!=extradstr && 'Y'==*extradstr->str) {
            //radius = 25.0 / _dotpitch_mm_x;    // (25 mm)
            radius = 25.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 25 mm on xoom)
        } else {
            //radius = 20.0 / _dotpitch_mm_x;    // (20 mm)
            radius = 20.0 / S52_MP_get(S52_MAR_DOTPITCH_MM_X);    // (not 20 mm on xoom)
        }

        //_setBlend(TRUE);

#ifdef S52_USE_GLES2
        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();

        _glTranslated(ppt[0], ppt[1], 0.0);
#else
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glTranslated(ppt[0], ppt[1], 0.0);
#endif

        _pushScaletoPixel(FALSE);

        // NOTE: specs say unit, assume it mean pixel
#ifdef S52_USE_OPENGL_VBO
        _gluQuadricDrawStyle(_qobj, GLU_FILL);
#else
        gluQuadricDrawStyle(_qobj, GLU_FILL);
#endif
        //_gluQuadricDrawStyle(_qobj, GLU_LINE);


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
            //DList->colors[2] = *black;
            //DList->colors[3] = *c;

#ifdef S52_USE_OPENGL_VBO
            //glGenBuffers(2, &DList->vboIds[0]);
            //glBindBuffer(GL_ARRAY_BUFFER, DList->vboIds[0]);
            _diskPrimTmp = DList->prim[0];
            _gluPartialDisk(_qobj, radius, radius+4, sweep, loops, sectr1+180, sweep);
            DList->vboIds[0] = _VBOCreate(_diskPrimTmp);

            //glBindBuffer(GL_ARRAY_BUFFER, DList->vboIds[1]);
            _diskPrimTmp = DList->prim[1];
            _gluPartialDisk(_qobj, radius+1, radius+3, sweep, loops, sectr1+180, sweep);
            DList->vboIds[1] = _VBOCreate(_diskPrimTmp);

            // set normal mode
            //glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
            // black sector
            DList->vboIds[0] = glGenLists(1);
            glNewList(DList->vboIds[0], GL_COMPILE);

            _diskPrimTmp = DList->prim[0];
            //_gluPartialDisk(_qobj, radius, radius+4, sweep, loops, sectr1+180, sweep);
            gluPartialDisk(_qobj, radius, radius+4, sweep, loops, sectr1+180, sweep);
            _DrawArrays(_diskPrimTmp);
            glEndList();

            // color sector
            DList->vboIds[1] = glGenLists(1);
            glNewList(DList->vboIds[1], GL_COMPILE);

            _diskPrimTmp = DList->prim[1];
            //_gluPartialDisk(_qobj, radius+1, radius+3, sweep, loops, sectr1+180, sweep);
            gluPartialDisk(_qobj, radius+1, radius+3, sweep, loops, sectr1+180, sweep);
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
        //S52_Color *cursr = S52_PL_getColor("CURSR");
        DListData = S52_PL_newDListData(obj);
        DListData->nbr       = 1;
        DListData->prim[0]   = S57_initPrim(NULL);
        DListData->colors[0] = *S52_PL_getColor("CURSR");
        //DListData->colors[0] = *cursr;
    }

    //_setBlend(TRUE);

#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    _glTranslated(ppt[0], ppt[1], 0.0);

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslated(ppt[0], ppt[1], 0.0);

    //glEnableClientState(GL_VERTEX_ARRAY);
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
    S52_Color *c   = S52_PL_getACdata(obj);
    S57_geo   *geo = S52_PL_getGeo(obj);

    if (S52_CMD_WRD_FILTER_AC & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

    // debug
    //if (557 == S57_getGeoID(geo)) {
    //    PRINTF("%s\n", S57_getName(geo));
    //}
    //if (467 == S57_getGeoID(geo)) {
    //    PRINTF("%s\n", S57_getName(geo));
    //    S52_PL_highlightON(obj);
    //}

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


#ifndef S52_USE_GV
    // don't reset matrix if used in GV
#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    // FIXME: BUG BUG BUG
    // THIS SHOULD NOT BE HERE .. BUT MIXUP AREA DRAW
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
#endif

    _fillarea(geo);

    _checkError("_renderAC()");

    return TRUE;
}

static int       _renderAP_NODATA(S52_obj *obj)
{
    S57_geo       *geoData   = S52_PL_getGeo(obj);
    S52_DListData *DListData = S52_PL_getDListData(obj);

    if (NULL != DListData) {
        S52_Color *col = DListData->colors;


        _glColor4ub(col);

#ifdef S52_USE_GLES2
#else
        glEnable(GL_POLYGON_STIPPLE);
        //glPolygonStipple(_nodata_mask);
#endif

        _fillarea(geoData);

#ifdef S52_USE_GLES2
        //glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
        glDisable(GL_POLYGON_STIPPLE);
#endif
        return TRUE;
    }

    return FALSE;
}

static int       _renderAP_NODATA_layer0(void)
{
    //_pt2 pt0, pt1, pt2, pt3, pt4;
    _pt2 pt0, pt1, pt2, pt3;

    // double --> float
    pt0.x = _pmin.u;
    pt0.y = _pmin.v;

    pt1.x = _pmin.u;
    pt1.y = _pmax.v;

    pt2.x = _pmax.u;
    pt2.y = _pmax.v;

    pt3.x = _pmax.u;
    pt3.y = _pmin.v;

    S52_Color *chgrd = S52_PL_getColor("CHGRD");


#ifdef S52_USE_GLES2
    glUniform4f(_uColor, chgrd->R/255.0, chgrd->G/255.0, chgrd->B/255.0, (4 - (chgrd->trans - '0'))*TRNSP_FAC_GLES2);

    vertex_t tile_x = 32 * ((_pmax.u - _pmin.u) / (vertex_t)_vp[2]);
    vertex_t tile_y = 32 * ((_pmax.v - _pmin.v) / (vertex_t)_vp[3]);

    int      n_tile_x = (_pmin.u - _pmax.u) / tile_x;
    //vertex_t remain_x = (_pmin.u - _pmax.u) - (tile_x * n_tile_x);
    int      n_tile_y = (_pmin.v - _pmax.v) / tile_y;
    //vertex_t remain_y = (_pmin.v - _pmax.v) - (tile_y * n_tile_y);


    // FIXME: this code fail to render dotted line (solid line only)
    // if move to S52_GL_init_GLES2()
    if (0 == _nodata_mask_texID) {

        // load texture on GPU ----------------------------------

        //_nodata_mask_texID = 0;
        //_dottpa_mask_texID = 0;

        // fill _rgba_nodata_mask - expand bitmask to a RGBA buffer
        // that will acte as a stencil in the fragment shader
        _1024bitMask2RGBATex(_nodata_mask_bits, _nodata_mask_rgba);
        //_64bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);
        _32bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);

        glGenTextures(1, &_nodata_mask_texID);
        glGenTextures(1, &_dottpa_mask_texID);

        _checkError("_renderAP_NODATA_layer0 -0-");

        // ------------
        // nodata pattern
        glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        // FIXME: maybe there is a way to expand the mask to rgba here
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, _nodata_mask_rgba);

        // ------------
        // dott pattern
        glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, _dottpa_mask_rgba);

        glBindTexture(GL_TEXTURE_2D, 0);
        _checkError("_renderAP_NODATA_layer0 -0.1-");

        // setup FBO  ----------------------------------

        glGenFramebuffers (1, &_frameBufferID);
        glGenRenderbuffers(1, &_renderBufferID);
    }


    _checkError("_renderAP_NODATA_layer0 -1-");

    glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

    // draw using texture as a stencil -------------------
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
    _checkError("_renderAP_NODATA_layer0 -2-");

    glEnableVertexAttribArray(_aPosition);
    glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), ppt);

    _checkError("_renderAP_NODATA_layer0 -3-");

    // turn ON 'sampler2d'
    glUniform1f(_uPattOn,   1.0);
    glUniform1f(_uPattOffX, pt0.x);
    glUniform1f(_uPattOffY, pt0.y);
    glUniform1f(_uPattX,    tile_x);
    glUniform1f(_uPattY,    tile_y);
    //glUniform1f(_uPattMaxX, 1.0);
    //glUniform1f(_uPattMaxY, 1.0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    //glDrawArrays(GL_TRIANGLES, 0, 3);

    _checkError("_renderAP_NODATA_layer0 -4-");

    glBindTexture(GL_TEXTURE_2D,  0);

    glUniform1f(_uPattOn,   0.0);
    glUniform1f(_uPattOffX, 0.0);
    glUniform1f(_uPattOffY, 0.0);
    glUniform1f(_uPattX,    0.0);
    glUniform1f(_uPattY,    0.0);
    //glUniform1f(_uPattMaxX, 0.0);
    //glUniform1f(_uPattMaxY, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

#else
    glEnable(GL_POLYGON_STIPPLE);
    glColor4ub(chgrd->R, chgrd->G, chgrd->B, (4 - (chgrd->trans - '0'))*TRNSP_FAC);

    //glPolygonStipple(_nodata_mask);

    // FIXME: could bind buffer - why? or why not?
    {
        vertex_t ppt[4*3] = {pt0.x, pt0.y, 0.0, pt1.x, pt1.y, 0.0, pt2.x, pt2.y, 0.0, pt3.x, pt3.y, 0.0};
        _DrawArrays_QUADS(4, ppt);
    }

    glDisable(GL_POLYGON_STIPPLE);

#endif

    _checkError("_renderAP_NODATA_layer0 -end-");

    return FALSE;
}

static int       _renderAP_DRGARE(S52_obj *obj)
{
    if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
        return TRUE;

    S57_geo       *geoData   = S52_PL_getGeo(obj);
    S52_DListData *DListData = S52_PL_getDListData(obj);

    if (NULL != DListData) {
        S52_Color *col = DListData->colors;


        _glColor4ub(col);

#ifdef S52_USE_GLES2
#else
        glEnable(GL_POLYGON_STIPPLE);
        glPolygonStipple(_drgare_mask);
#endif

        _fillarea(geoData);

#ifdef S52_USE_GLES2
#else
        glDisable(GL_POLYGON_STIPPLE);
#endif
        return TRUE;
    }

    return FALSE;
}


static int       _renderAP(S52_obj *obj)
// Area Pattern
{
    if (S52_CMD_WRD_FILTER_AP & (int) S52_MP_get(S52_CMD_WRD_FILTER))
        return TRUE;

#ifdef S52_USE_GV
    // FIXME
    return FALSE;
#endif


    // don't pick pattern for now
    if (TRUE == _doPick)
        return TRUE;

    // when in pick mode, fill the area
    /*
    if (TRUE == _doPick) {
        S57_geo *geoData = S52_PL_getGeo(obj);
        S52_Color dummy;

        _glColor4ub(&dummy);
        _fillarea(geoData);

        return TRUE;
    }
    //*/

    // debug timming
    //return TRUE;

    // debug
    /*
    if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj), 6)) {
        //if (_drgare++ > 4)
        //    return TRUE;
        PRINTF("DRGARE found\n");

        //return TRUE;
    }
    //*/
    /*
    if (0 == g_strcmp0("PRCARE", S52_PL_getOBCL(obj), 6)) {
        //if (_drgare++ > 4)
        //    return TRUE;
        PRINTF("PRCARE found\n");
        //return TRUE;
    }
    //*/
    /*
    if (0 == S52_PL_cmpCmdParam(obj, "DIAMOND1")) {
        // debug timming
        //if (_drgare++ > 4)
        //    return TRUE;
        //g_assert_not_reached();
        //return TRUE;
        //PRINTF("pattern DIAMOND1 found\n");
    }
    */

    /*
    if (0 == S52_PL_cmpCmdParam(obj, "TSSJCT02c")) {
        // debug timming
        //if (_drgare++ > 4)
        //    return TRUE;
        PRINTF("TSSJCT02 found\n");
        //return TRUE;
    }
    //*/

    // debug - U pattern
    //if (0 != g_strcmp0("M_QUAL", S52_PL_getOBCL(obj), 6) ) {
    ////    //_renderAP_NODATA(obj);
    //    return TRUE;
    //}
    //char *name = S52_PL_getOBCL(obj);
    //PRINTF("%s: ----------------\n", name);
    //if (0==g_strcmp0("M_QUAL", S52_PL_getOBCL(obj), 6) ) {
    //    PRINTF("M_QUAL found\n");
    //}
    //return 1;



    // FIXME: get the name of the pattern instead
    // optimisation --experimental
    // TODO: optimisation: if proven to be faster, compare S57 object number instead of string name
#ifdef S52_USE_GLES2
    if (0 == g_ascii_strncasecmp("DRGARE", S52_PL_getOBCL(obj), 6)) {
        if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
            return TRUE;
    }
#else
    if (0 == g_ascii_strncasecmp("DRGARE", S52_PL_getOBCL(obj), 6)) {
        if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
            return TRUE;

        _renderAP_DRGARE(obj);
        return TRUE;
    } else {
        // fill area with NODATA pattern
        if (0==g_strcmp0("UNSARE", S52_PL_getOBCL(obj), 6) ) {
            //_renderAP_NODATA(obj);
            return TRUE;
        } else {
            // fill area with OVERSC01
            if (0==g_strcmp0("M_COVR", S52_PL_getOBCL(obj), 6) ) {
                //_renderAP_NODATA(obj);
                return TRUE;
            } else {
                // fill area with
                if (0==g_strcmp0("M_CSCL", S52_PL_getOBCL(obj), 6) ) {
                    //_renderAP_NODATA(obj);
                    return TRUE;
                } else {
                    // fill area with
                    if (0==g_strcmp0("M_QUAL", S52_PL_getOBCL(obj), 6) ) {
                        //_renderAP_NODATA(obj);
                        return TRUE;
                    }
                }
            }
        }
    }
#endif

    S52_DListData *DListData = S52_PL_getDListData(obj);
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData))) {
        glDisable(GL_STENCIL_TEST);
        return FALSE;
    }

    S57_geo *geoData = S52_PL_getGeo(obj);

#ifndef S52_USE_GLES2
    // Bec      pattern stencil
    // 550 msec   on      on
    // 480 msec   on      off
    // 360 msec   off     on

    // Bec tuning for GL in X (module loaded: DRI glx GLcore)
    // msec AA  pat sten
    // 130  off --  --
    // 280  on  --  --
    // 440  off on  on
    // 620  on  on  on
    // 345  off on  off
    // 535  on  on  off
    // 130  off off off
    // 280  on  off off

    // Bec tuning for GL in fglrx (ATI driver with all fancy switched to OFF)
    // msec AA  pat sten
    //  11  off --  --
    //  13  on  --  --
    // 120  off on  on
    // 140  on  on  on
    // 120  off on  off
    // 140  on  on  off
    //  12  off off on
    //  14  on  off on
    //  12  off off off
    //  14  on  off off

    //*
    {   // setup stencil
        glEnable(GL_STENCIL_TEST);

        glClear(GL_STENCIL_BUFFER_BIT);
        // debug:flush all
        //glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        glStencilFunc(GL_ALWAYS, 0x1, 0x1);
        glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

        // treate color as transparent
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);

        // fill stencil
        _fillarea(geoData);

        // setup stencil to clip pattern
        // all color to pass stencil filter
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

        // clip pattern pixel that lie outside of poly --clip if != 1
        glStencilFunc(GL_EQUAL, 0x1, 0x1);

        // freeze stencil state
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }
    //*/
#endif

    //
    // Tile pattern to 'grided' extent
    //

    // NOTE: S52 define pattern rotation but doesn't use that in specs.
    //GLdouble x , y;    // index
    GLdouble x1, y1;   // LL of region of area
    GLdouble x2, y2;   // UR of region of area

    // pattern tile 1 = 0.01 mm
    GLdouble tw = 0.0; // width
    GLdouble th = 0.0; // height
    GLdouble dx = 0.0; // run length offset for STG pattern
    S52_PL_getAPTileDim(obj, &tw,  &th,  &dx);

    GLdouble bbx = 0.0;
    GLdouble bby = 0.0;
    GLdouble px  = 0.0;  // pivot
    GLdouble py  = 0.0;  // pivot
    S52_PL_getAPTilePos(obj, &bbx, &bby, &px, &py);

    // convert tile unit (0.01mm) to pixel
    GLdouble tileWidthPix  = tw  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    GLdouble tileHeightPix = th  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));
    GLdouble stagOffsetPix = dx  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));

    GLdouble bbxPix        = bbx / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    GLdouble bbyPix        = bby / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));
    GLdouble pivotxPix     = px  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_X));
    GLdouble pivotyPix     = py  / (100.0 * S52_MP_get(S52_MAR_DOTPITCH_MM_Y));

    // convert tile in pixel to world
    GLdouble w = tileWidthPix  * ((_pmax.u - _pmin.u) / (GLdouble)_vp[2]);
    GLdouble h = tileHeightPix * ((_pmax.v - _pmin.v) / (GLdouble)_vp[3]);
    //GLdouble d = stagOffsetPix * ((_pmax.u - _pmin.u) / (GLdouble)_vp[2]);

    // grid alignment
    //S57_getExtPRJ(geoData, &x1, &y1, &x2, &y2);
    S57_getExt(geoData, &x1, &y1, &x2, &y2);
    double xyz[6] = {x1, y1, 0.0, x2, y2, 0.0};
    if (FALSE == S57_geo2prj3dv(2, (double*)&xyz))
        return FALSE;

    x1  = xyz[0];
    y1  = xyz[1];
    x2  = xyz[3];
    y2  = xyz[4];

    x1  = floor(x1 / w) * w;
    y1  = floor(y1 / (2*h)) * (2*h);

    // optimisation, resize extent grid to fit window
    if (x1 < _pmin.u)
        x1 += floor((_pmin.u-x1) / w) * w;
    if (y1 < _pmin.v)
        y1 += floor((_pmin.v-y1) / (2*h)) * (2*h);
    if (x2 > _pmax.u)
        x2 -= floor((x2 - _pmax.u) / w) * w;
    if (y2 > _pmax.v)
        y2 -= floor((y2 - _pmax.v) / h) * h;

    // cover completely
    x2 += w;
    y2 += h;

    //PRINTF("PIXEL: tileW:%f tileH:%f\n", tileWidthPix, tileHeightPix);

    {   // invariant: just to be sure that things don't explode
        // the number of tile in pixel is proportional to the number
        // of tile visible in world coordinate
        //GLdouble tileNbrX = (_vp[2] - _vp[0]) / tileWidthPix;
        //GLdouble tileNbrY = (_vp[3] - _vp[1]) / tileHeightPix;
        //GLdouble tileNbrX = _vp[2] / tileWidthPix;
        //GLdouble tileNbrY = _vp[3] / tileHeightPix;
        //GLdouble tileNbrU = (x2-x1) / w;
        //GLdouble tileNbrV = (y2-y1) / h;
        // debug
        //PRINTF("TILE nbr: Pix X=%f Y=%f (X*Y=%f) World U=%f V=%f\n", tileNbrX,tileNbrY,tileNbrX*tileNbrY,tileNbrU,tileNbrV);
        //PRINTF("WORLD: width: %f height: %f tileW: %f tileH: %f\n", (x2-x1), (y2-y1), w, h);
        //PRINTF("PIXEL: width: %i height: %i tileW: %f tileH: %f\n", (_vp[2] - _vp[0]), (_vp[3] - _vp[1]), tileWidthPix, tileHeightPix);
        //if (tileNbrX + 4 < tileNbrU)
        //    g_assert(0);
        //if (tileNbrY + 4 < tileNbrV)
        //    g_assert(0);
    }

#ifdef S52_USE_GLES2
    // *BUG* *BUG* *BUG*: can't tile subtexture in shader (or couldn't find the trick)
    // BUT in GLES3 texture can be of any size, so tiling should be strait foward then.
    // As for GLES2, tile are POT (power-of-two) and so are the pattern after scaling.
    // So there is a little discrepancy between S52 pattern size and GLES2
    // (but it has zero impact, as far as I can tell, on the meaning of the pattern)
    {
        int x = 0;
        int y = 0;

        if (0.0 == stagOffsetPix) {
            x = ceil(tileWidthPix );
            y = ceil(tileHeightPix);
        } else {
            x = ceil(tileWidthPix  + stagOffsetPix);
            y = ceil(tileHeightPix * 2);
        }
        x = (x<=8)?8:(x<=16)?16:(x<=32)?32:(x<=64)?64:(x<=128)?128:(x<=256)?256:512;
        y = (y<=8)?8:(y<=16)?16:(y<=32)?32:(y<=64)?64:(y<=128)?128:(y<=256)?256:512;

        GLuint mask_texID = S52_PL_getAPtexID(obj);
        if (0 == mask_texID) {
            glGenTextures(1, &mask_texID);
            glBindTexture(GL_TEXTURE_2D, mask_texID);
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            _checkError("_renderAP() -0.0-");

            glBindFramebuffer     (GL_FRAMEBUFFER, _frameBufferID);
            _checkError("_renderAP() -0.3-");

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_texID, 0);
            _checkError("_renderAP() -0.31-");

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                PRINTF("ERROR: glCheckFramebufferStatus() fail: %s\n", S52_PL_getOBCL(obj));
                g_assert(0);
            } else {
                PRINTF("OK: glCheckFramebufferStatus(): %s, tile: %i x %i\n", S52_PL_getOBCL(obj), x, y);
                PRINTF("pixels w/h: %f x %f, d: %f\n", tileWidthPix, tileHeightPix, stagOffsetPix);
            }

            // save texture mask ID
            S52_PL_setAPtexID(obj, mask_texID);

            _checkError("_renderAP() -0.32-");

            // ------------------------------------------------
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(0.0, 0.0, 0.0, 0.0);

#ifdef S52_USE_TEGRA2
            // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase)
            // define in gl2ext.h:
            //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
            glClear(GL_COLOR_BUFFER_BIT | GL_COVERAGE_BUFFER_BIT_NV);
            //glClear(GL_COLOR_BUFFER_BIT);
#else
            glClear(GL_COLOR_BUFFER_BIT);
#endif
            _checkError("_renderAP() -0.4-");
            // ------------------------------------------------


            {   // set line/point width
                GLdouble dummy = 0.0;
                char     pen_w = '1';
                S52_PL_getLCdata(obj, &dummy, &pen_w);
                glLineWidth(pen_w - '0');
                //glLineWidth(3.0);
                //glLineWidth(1.0);

                //_glPointSize(pen_w - '0');
                //_glPointSize(pen_w - '0' + 0.5); // sampler + AA soften pixel, so need enhencing a bit
                _glPointSize(pen_w - '0' + 1.0); // sampler + AA soften pixel, so need enhencing a bit
                //_glPointSize(3.0);
            }

            // render to texture

            _checkError("_renderAP() -0.1-");

            _glMatrixSet(VP_WIN);
            if (0.0 == stagOffsetPix) {
                // move to center
                _glTranslated(tileWidthPix/2.0, tileHeightPix/2.0, 0.0);
                // then flip on Y
                _glScaled(1.0, -1.0, 1.0);

                // Translated() can't have an 'offset' of 0 (matrix goes nuts)
                GLdouble offsetx = pivotxPix - bbxPix - (tileWidthPix  / 2.0) + 1.0;
                GLdouble offsety = pivotyPix - bbyPix - (tileHeightPix / 2.0) + 1.0;
                //if (0.0 == offsetx) offsetx = 1.0; //g_assert(0);
                //if (0.0 == offsety) offsety = 1.0; //g_assert(0);
                if (0.0 == offsetx) g_assert(0);
                if (0.0 == offsety) g_assert(0);

                // move - the 5.0 was found by trial and error .. could be improve
                // to fill the gap in pattern TSSJCT02 (traffic separation scheme crossing)
                //_glTranslated(offsetx, offsety, 0.0);
                _glTranslated(offsetx + (tileWidthPix/2.0) - 5.0, offsety - (tileHeightPix/2.0) + 5.0, 0.0);

                // scale
                // FIXME: why 0.03 and not 0.3 (ie S52_MAR_DOTPITCH_MM_ X/Y)
                // FIXME: use _pushScaletoPixel() on _glMatrixSet(VP_PRJ) !!
                //_glScaled(0.03, 0.03, 1.0);
                _glScaled(0.03/(tileWidthPix/x), 0.03/(tileHeightPix/y), 1.0);

            } else {
                _glTranslated(x/4.0, y/4.0, 0.0);
                _glScaled(0.05, -0.05, 1.0);
            }


            glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

            glUniform4f(_uColor, 0.0, 0.0, 0.0, 1.0);

            glEnableVertexAttribArray(_aPosition);

            // 1st row (bottom)
            // 1st LL
            for (unsigned int i=0; i<DListData->nbr; ++i) {
                guint j     = 0;
                GLint mode  = 0;
                GLint first = 0;
                GLint count = 0;

                GArray *vert = S57_getPrimVertex(DListData->prim[i]);
                vertex_t *v = (vertex_t*)vert->data;

                glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, v);
                _checkError("_renderAP() -10-");

                while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {
                    if (_QUADRIC_TRANSLATE == mode) {
                        PRINTF("FIXME: handle _QUADRIC_TRANSLATE\n");
                    } else {
                        glDrawArrays(mode, first, count);
                    }
                    ++j;
                }
            }

            _checkError("_renderAP() -0-");

            // 2nd row (top up right)
            //* stag ON
            if (0.0 != stagOffsetPix) {
                _glLoadIdentity();

                _glTranslated((x/4.0)*3.0, (y/4.0)*3.0, 0.0);

                //_glScaled(0.03, -0.03, 1.0);
                _glScaled(0.05, -0.05, 1.0);

                glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

                for (unsigned int i=0; i<DListData->nbr; ++i) {
                    guint j     = 0;
                    GLint mode  = 0;
                    GLint first = 0;
                    GLint count = 0;

                    GArray *vert = S57_getPrimVertex(DListData->prim[i]);
                    vertex_t *v = (vertex_t*)vert->data;

                    glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, v);

                    while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {
                        if (_QUADRIC_TRANSLATE == mode) {
                            PRINTF("FIXME: handle _QUADRIC_TRANSLATE\n");
                        } else {
                            glDrawArrays(mode, first, count);
                        }
                        ++j;
                    }
                }
            }
            //*/


            /* debug - draw X and Y axis
            {
                _glLoadIdentity();

                glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

                //pt3v pt1[2] = {{0.0, 0.0, 0.0}, {  x, 0.0, 0.0}};
                //pt3v pt2[2] = {{0.0, 1.0, 0.0}, {1.0,   y, 0.0}};
                pt3v pt1[2] = {{0.0, 0.0, 0.0}, {tileWidthPix,           0.0, 0.0}};
                pt3v pt2[2] = {{0.0, 1.0, 0.0}, {1.0,          tileHeightPix, 0.0}};

                _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt1);
                _DrawArrays_LINE_STRIP(2, (vertex_t*)&pt2);
            }
            //*/



            _glMatrixDel(VP_WIN);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            //glGenerateMipmap(GL_TEXTURE_2D); // not usefull
            glBindTexture(GL_TEXTURE_2D, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisableVertexAttribArray(_aPosition);
            //*/

            _checkError("_renderAP() -1-");

        }


        _glColor4ub(DListData->colors);
        //glUniform4f(_uColor, 1.0, 0.0, 0.0, 0.0); // red

        _glMatrixMode(GL_MODELVIEW);
        _glLoadIdentity();
        glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

        glUniform1f(_uPattOn,   1.0);
        glUniform1f(_uPattOffX, x1);
        glUniform1f(_uPattOffY, y1);

        // test
        //x = x * ((_pmax.u - _pmin.u) / (GLdouble)_vp[2]);
        //y = y * ((_pmax.v - _pmin.v) / (GLdouble)_vp[3]);

        if (0.0 == stagOffsetPix) {
            // this strech the texture

            glUniform1f(_uPattX,    w);        // tile width in world
            glUniform1f(_uPattY,    h);        // tile height in world
            //glUniform1f(_uPattX,    x);
            //glUniform1f(_uPattY,    y);
            //glUniform1f(_uPattX,    x * ((_pmax.u - _pmin.u) / (GLdouble)_vp[2]));
            //glUniform1f(_uPattY,    y * ((_pmax.v - _pmin.v) / (GLdouble)_vp[3]));
            //glUniform1f(_uPattMaxX, tileWidthPix  / x);
            //glUniform1f(_uPattMaxY, tileHeightPix / y);
            //glUniform1f(_uPattMaxX, 1.0);
            //glUniform1f(_uPattMaxY, 1.0);

        } else {
            //glUniform1f(_uPattX,    w + dx);
            //glUniform1f(_uPattY,    h * 2 );
            glUniform1f(_uPattX,    x * ((_pmax.u - _pmin.u) / (GLdouble)_vp[2]));
            glUniform1f(_uPattY,    y * ((_pmax.v - _pmin.v) / (GLdouble)_vp[3]));
            //glUniform1f(_uPattMaxX, 1.0);
            //glUniform1f(_uPattMaxY, 1.0);
        }

        glBindTexture(GL_TEXTURE_2D, mask_texID);

        _fillarea(geoData);

        glBindTexture(GL_TEXTURE_2D, 0);

        glUniform1f(_uPattOn,   0.0);
        glUniform1f(_uPattOffX, 0.0);
        glUniform1f(_uPattOffY, 0.0);
        glUniform1f(_uPattX,    0.0);
        glUniform1f(_uPattY,    0.0);
//        glUniform1f(_uPattMaxX, 0.0);
//        glUniform1f(_uPattMaxY, 0.0);

        _checkError("_renderAP() -2-");
    }

#else  // S52_USE_GLES2

    // NOTE: pattern that do not fit entirely inside an area
    // are displayed  (hence pattern are clipped) because ajacent area
    // filled with same pattern will complete the clipped pattern.
    // No test y+th<y2 and x+tw<x2 to check for the end of a row/collum.
    //d = 0.0;

    glMatrixMode(GL_MODELVIEW);

    //*
    int npatt = 0;  // stat
    int stag  = 0; // 0-1 true/false add dx for stagged pattern
    for (double y=y1; y<=y2; y+=h) {
        glLoadIdentity();   // reset to screen origin
        glTranslated(x1 + (d*stag), y, 0.0);
        glScaled(1.0, -1.0, 1.0);

        _pushScaletoPixel(TRUE);
        for (double x=x1; x<x2; x+=w) {
            _glCallList(DListData);
            //glTranslated(tw/(100.0*_dotpitch_mm_x), 0.0, 0.0);
            glTranslated(tw, 0.0, 0.0);
            ++npatt;
        }
        _popScaletoPixel();
        stag = !stag;
    }
    //*/

    // debug
    //char *name = S52_PL_getOBCL(obj);
    //PRINTF("nbr of tile (%s): %i-------------------\n", name, npatt);

    glDisable(GL_STENCIL_TEST);

    // this turn off blending from display list
    //_setBlend(FALSE);

#endif  // S52_USE_GLES2

    _checkError("_renderAP()");

    return TRUE;
}

static int       _traceCS(S52_obj *obj)
// failsafe trap --should not get here
{
    if (0 == S52_PL_cmpCmdParam(obj, "DEPCNT02"))
        PRINTF("DEPCNT02\n");

    if (0 == S52_PL_cmpCmdParam(obj, "LIGHTS05"))
        PRINTF("LIGHTS05\n");

    g_assert(0);

    return TRUE;
}

static int       _traceOP(S52_obj *obj)
{
    // debug:
    //PRINTF("OVERRIDE PRIORITY: %s, TYPE: %s\n", S52_PL_getOBCL(obj), S52_PL_infoLUP(obj));

    // avoid "warning: unused parameter"
    (void) obj;

    //S52_PL_cmpCmdParam(obj, NULL);

    return TRUE;
}

#ifdef S52_USE_FREETYPE_GL
//static GArray   *_fillFtglBuf(texture_font_t *font, GArray *buf, const char *str)
static GArray   *_fillFtglBuf(GArray *buf, const char *str, unsigned int weight)
// experimental: smaller text size if second line
// could translate into a TX command word to be added in the PLib
{
    int pen_x = 0;
    int pen_y = 0;
    int space = FALSE;

    size_t i   = 0;
    size_t len = S52_strlen(str);

    g_array_set_size(buf, 0);

    //for (i=0; i<wcslen(str); ++i) {
    for (i=0; i<len; ++i) {
        // experimental: smaller text size if second line
        if (NL == str[i]) {
        //if (TB == str[i]) {
            weight = (0<weight) ? weight-1 : weight;
            texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], 'A');
            pen_x =  0;
            pen_y = (NULL!=glyph) ? -(glyph->height+5) : 10 ;
            space = TRUE;
            ++i;
        }


        texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], str[i]);
        if (NULL != glyph) {
            // experimental: augmente kerning if second line
            if (pen_x > 0) {
                pen_x += texture_glyph_get_kerning(glyph, str[i-1]);
                pen_x += (TRUE==space) ? 1 : 0;
            }

            GLfloat x0 = pen_x + glyph->offset_x;
            GLfloat y0 = pen_y + glyph->offset_y;

            GLfloat x1 = x0    + glyph->width;
            GLfloat y1 = y0    - glyph->height;  // Y is down, so flip glyph

            GLfloat s0 = glyph->s0;
            GLfloat t0 = glyph->t0;
            GLfloat s1 = glyph->s1;
            GLfloat t1 = glyph->t1;

            //PRINTF("CHAR: x0,y0,x1,y1: %lc: %f %f %f %f\n", str[i],x0,y0,x1,y1);
            //PRINTF("CHAR: s0,t0,s1,t1: %lc: %f %f %f %f\n", str[i],s0,t0,s1,t1);

            GLfloat z0 = 0.0;
            _freetype_gl_vertex_t vertices[4] = {
                {x0,y0,z0,  s0,t0},
                {x0,y1,z0,  s0,t1},
                {x1,y1,z0,  s1,t1},
                {x1,y0,z0,  s1,t0}
            };
            buf = g_array_append_vals(buf, vertices, 4);

            pen_x += glyph->advance_x;
            pen_y += glyph->advance_y;
        }
    }

    return buf;
}

static int       _sendFtglBuf(GArray *buf)
// connect ftgl coord data to GPU
{
        glEnableVertexAttribArray(_aPosition);
        glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), buf->data);

        GLfloat *d = (GLfloat*)buf->data;
        d += 3;
        glEnableVertexAttribArray(_aUV);
        glVertexAttribPointer    (_aUV,       2, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), d);

        _checkError("_bindFtglBuf()  -fini-");

        return TRUE;
}
#endif

static int       _drawTextAA(S52_obj *obj, double x, double y, unsigned int bsize, unsigned int weight, const char *str)
// render text in AA if Mar Param set
// NOTE: obj is only used if S52_USE_FREETYPE_GL is defined
// NOTE: PLib C1 CHARS for TE() & TX() alway '15110' - ie style = 1 (alway), weigth = '5' (medium), width = 1 (alway), bsize = 10
// NOTE: weight is already converted from '4','5','6' to int 0,1,2
{
    // TODO: use 'bsize'
    (void) bsize;

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

    // debug
    //return FALSE;


#ifdef S52_USE_FREETYPE_GL
    (void)obj;

    // debug - check logic (should this be a bug!)
    if (NULL == str) {
        PRINTF("DEBUG: warning NULL str\n");
        return FALSE;
    }



    if (NULL == _buf) {
        _buf = g_array_new(FALSE, FALSE, sizeof(_freetype_gl_vertex_t));
    }

    // OPTIMISATION: not all need to be refilled as only VESSEL name
    // change over time (time tag). To re-fill each time is the worst case
    // so the FIX should speed up things a bit
    //_fillFtglBuf(_buf, str);
    //_fillFtglBuf(_freetype_gl_font[weight], _buf, str);
    _fillFtglBuf(_buf, str, weight);
    _sendFtglBuf(_buf);

    // turn ON 'sampler2d'
    glUniform1f(_uStipOn, 1.0);

    glBindTexture(GL_TEXTURE_2D, _freetype_gl_atlas->id);

    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();


    _glTranslated(x, y, 0.0);

    _pushScaletoPixel(FALSE);

    _glRotated   (-_north, 0.0, 0.0, 1.0);

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    //PRINTF("x:%f, x:%f, str:%s\n", x, y, str);

    // FIXME: check for '\n' and shorten line
    // BETTER: at a second TX() for second text row (2x sock traffic!)

    //for (guint i=0; i<S52_strlen(str); ++i) {
    //gint len = g_utf8_strlen(str, -1) - 1;
    gint len = g_utf8_strlen(str, -1);
    for (gint i=0; i<len; ++i) {
        glDrawArrays(GL_TRIANGLE_FAN, i*4, 4);
    }

    _popScaletoPixel();

    glBindTexture(GL_TEXTURE_2D,  0);
    glUniform1f(_uStipOn, 0.0);

    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    /*  huge mem usage
    if (NULL != obj) {
        GLuint  vID = 0;
        GArray *buf = S52_PL_getFtglBuf(obj, &vID);

        if (0 != vID) {
            glDeleteBuffers(1, &vID);
            S52_PL_setFtglBuf(obj, buf, 0);
        }
    }
    //*/

    _checkError("_drawTextAA() freetype-gl");

#endif

#ifdef S52_USE_GLC
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {

        // WIN coord. for raster (bitmap and pixmap)

        projUV p = {0.0, 0.0};
        p.u = x;
        p.v = y;
        p = _prj2win(p);

        _glMatrixSet(VP_WIN);

        glRasterPos3d((int)p.u, (int)p.v, 0.0);
        PRINTF("GLC:%s\n", str);

        glcRenderString(str);

        _glMatrixDel(VP_WIN);

        _checkError("_drawTextAA() / POINT_T");

        return TRUE;


        // glcResolution


        /*
        // PRJ coord.
        // debug: redondant --just to be certain
        _setBlend(FALSE);

        glEnable(GL_TEXTURE_2D);

        _glTranslated(x, y, 1000.0);
        //_glTranslated((int)x, (int)y, 1.0);
        //_glTranslated(x, y, 1.0);
        _pushScaletoPixel();

        //_glScaled(70.0, 70.0, 1.0);
        //_glScaled(12.0, 12.0, 1.0);
        _glScaled(11.0, 11.0, 1.0);
        //_glScaled(10.0, 10.0, 1.0);
        //_glScaled(100.0, 100.0, 1.0);
        //_glRotated(90.0 - 168.0, 0.0, 0.0, 1.0);
        glcRenderString(str);

        _popScaletoPixel();

        glDisable(GL_TEXTURE_2D);

        _checkError("_drawTextAA() / POINT_T");

        return TRUE;
        */
    }
    _checkError("_drawTextAA() / POINT_T");

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

#ifdef S52_USE_A3D
    a3d_texstring_printf(_a3d_str, "%s", str);
    //a3d_texstring_printf(_a3d_str, "%s", "test");

    _glMatrixSet(VP_PRJ);

    //PRINTF("x:%f, y:%f\n", x, y);
    projUV p = {x, y};
    p = _prj2win(p);
    //PRINTF("u:%f, v:%f\n", p.u, p.v);

    _glMatrixDel(VP_PRJ);

    a3d_texstring_draw(_a3d_str, p.u, _vp[3]-p.v , _vp[2], _vp[3]);
    //a3d_texstring_draw(_a3d_str, 100.0f, 100.0f, _vp[2], _vp[3]);

#endif

#ifdef S52_USE_FTGL
    (void)obj;

    // FIXME: why the need to un-rotate here !
    double n = _north;
    _north = 0.0;

    //_glMatrixMode(GL_TEXTURE);
    //_glPushMatrix();
    //_glLoadIdentity();

    //_setBlend(FALSE);
    _glMatrixSet(VP_WIN);

    projUV p = {x, y};
    p = _prj2win(p);

    //glRasterPos2d(p.u, p.v);
    glRasterPos2i(p.u, p.v);
    //glRasterPos3i(p.u, p.v, _north);

    // debug
    //PRINTF("ftgl:%s\n", str);

    if (NULL != _ftglFont[weight]) {
#ifdef _MINGW
        ftglRenderFont(_ftglFont[weight], str, FTGL_RENDER_ALL);
#else
        ftglRenderFont(_ftglFont[weight], str, FTGL_RENDER_ALL);
        //_ftglFont[weight]->Render(str);
        //if (_ftglFont[weight]->Error())
        //    g_assert(0);
#endif
    }
    //else
    //    g_assert(0);

    //_glMatrixMode(GL_TEXTURE);
    //_glPopMatrix();

    _glMatrixDel(VP_WIN);

    _checkError("_drawTextAA() / POINT_T");

    _north = n;

#else // S52_USE_FTGL


    // fallback on bitmap font

    //glPushAttrib(GL_LIST_BIT);

    //_glMatrixMode(GL_MODELVIEW);
    //_glLoadIdentity();


    //glListBase(_fontDList[weight]);
    //glListBase(_fontDList[2]);

    //glRasterPos3f(ppt[0]+uoffs, ppt[1]-voffs, 0.0);
    //glRasterPos2d(ppt[0]+uoffs, ppt[1]-voffs);

    //glCallLists(S52_strlen(str), GL_UNSIGNED_BYTE, (GLubyte*)str);
    //PRINTF("%i: %s\n", S52_PL_getLUCM(obj), str);

    //glListBase(_fontDList[1]);

    //_glMatrixMode(GL_TEXTURE);
    //_glLoadIdentity();


    //_glTranslated(ppt[0], ppt[1], 0.0);
    //glRasterPos3f(ppt[0]+uoffs, ppt[1]-voffs, 0.0);

    //glRasterPos2d(x, y);
    //_glScaled(100.0, 100.0, 0.0);

    //glCallLists(S52_strlen(str), GL_UNSIGNED_BYTE, (GLubyte*)str);

    _checkError("_drawTextAA() / POINT_T");
#endif


    return TRUE;
}

static int       _drawText(S52_obj *obj)
// render TE or TX
{
    S52_Color   *c      = NULL;
    int          xoffs  = 0;
    int          yoffs  = 0;
    unsigned int bsize  = 0;
    unsigned int weight = 0;
    int          disIdx = 0;      // text view group
    const char  *str    = NULL;

    guint      npt    = 0;
    GLdouble  *ppt    = NULL;

    if (0.0 == S52_MP_get(S52_MAR_SHOW_TEXT))
        return FALSE;

    S57_geo *geoData = S52_PL_getGeo(obj);
    if (FALSE == S57_getGeoData(geoData, 0, &npt, &ppt))
        return FALSE;

    str = S52_PL_getEX(obj, &c, &xoffs, &yoffs, &bsize, &weight, &disIdx);

    // debug
    //str = "X";
    //c   = S52_PL_getColor("DNGHL");

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

    // Text Important / Other
    // supress display of text above user selected level
    if (FALSE == S52_MP_getTextDisp(disIdx))
        return FALSE;

    // debug
    /*
    GString *FIDNstr = S57_getAttVal(geoData, "FIDN");
    PRINTF("NAME:%s FIDN:%s\n", S57_getName(geoData), FIDNstr->str);
    if (0==strcmp("84740", FIDNstr->str)) {
        PRINTF("%s\n",FIDNstr->str);
    }
    //*/

    /*
    {   // hack: more than one text at CA579016.000 (lower left range)
        // supress this text if NOT related to an other object
        // BUG: this hack doen't work since buoy 'touch' lights
        if (NULL != S57_getTouchLIGHTS(geoData)) {
            PRINTF("--- %s\n", S57_getName(geoData));
            //return FALSE;
        }
    }
    */

    // convert offset to PRJ
    double scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
    double scaley = (_pmax.v - _pmin.v) / (double)_vp[3];
    double uoffs  = ((10 * PICA * xoffs) / _dotpitch_mm_x) * scalex;
    double voffs  = ((10 * PICA * yoffs) / _dotpitch_mm_y) * scaley;
    //PRINTF("uoffs/voffs: %f/%f %s\n", uoffs, voffs, str);

#ifndef S52_USE_COGL
    // debug
    //glColor4ub(255, 0, 0, 255); // red
    //glUniform4f(_uColor, 0.0, 0.0, 1.0, 0.5); // GLES2

    _glColor4ub(c);
#endif

    if (POINT_T == S57_getObjtype(geoData)) {
        _drawTextAA(obj, ppt[0]+uoffs, ppt[1]-voffs, bsize, weight, str);

        return TRUE;
    }

    if (LINES_T == S57_getObjtype(geoData)) {
        if (0 == g_strcmp0("pastrk", S57_getName(geoData))) {
            // past track time
            unsigned int i = 0;

            for (i=0; i<npt; ++i) {
                /*
                struct xyt {
                    double x;
                    double y;
                    GDate  d;
                };
                struct xyt *x = (struct xyt *) &(ppt[i*3]);

                if (TRUE == g_date_valid(&x->d)) {
                    GTimeVal datetime;
                    GDate date;
                    g_time_val_from_iso8601("2010-01-01T01:01:02", &datetime);

                    g_date_clear(&date, 1);
                    g_date_set_time_val(&date, &datetime);

                    gchar s[80];
                    //g_date_strftime(s, 80,"%Y%m%d %T", &x->d);
                    g_date_strftime(s, 80,"%F %T", &date);

                    _drawTextAA(x->x, x->y, bsize, weight, s);
                }
                */
                gchar s[80];
                int timeHH = ppt[i*3 + 2] / 100;
                int timeMM = ppt[i*3 + 2] - (timeHH * 100);
                //SPRINTF(s, "t%04.f", ppt[i*3 + 2]);     // S52 PASTRK01 say frefix a 't'
                SPRINTF(s, "%02i:%02i", timeHH, timeMM);  // ISO say HH:MM

                _drawTextAA(obj, ppt[i*3 + 0], ppt[i*3 + 1], bsize, weight, s);

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

            _drawTextAA(obj, x, y, bsize, weight, s);

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

            _drawTextAA(obj, x, y, bsize, weight, s);

            return TRUE;
        }


        {   // other text (ex bridge)
            unsigned int i = 0;
            double cView_x = (_pmax.u + _pmin.u) / 2.0;
            double cView_y = (_pmax.v + _pmin.v) / 2.0;
            double dmin    = INFINITY;
            double xmin, ymin;

            // find segment's center point closess to view center
            // FIXME: clip segments to view
            //for (i=0; i<npt-1; ++i) {
            for (i=0; i<npt; ++i) {
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
                _drawTextAA(obj, xmin+uoffs, ymin-voffs, bsize, weight, str);
            }
        }

        return TRUE;
    }

    if (AREAS_T == S57_getObjtype(geoData)) {

        _computeCentroid(geoData);

        for (guint i=0; i<_centroids->len; ++i) {
            pt3 *pt = &g_array_index(_centroids, pt3, i);

            _drawTextAA(obj, pt->x+uoffs, pt->y-voffs, bsize, weight, str);
            //PRINTF("TEXT (%s): %f/%f\n", str, pt->x, pt->y);

            // only draw the first centroid
            if (0.0 == S52_MP_get(S52_MAR_DISP_CENTROIDS))
                return TRUE;
        }

        return TRUE;
    }

    PRINTF("NOTE: don't know how to draw this TEXT\n");
    g_assert(0);

    return FALSE;
}


//-----------------------------------
//
// SYMBOL CREATION SECTION
//
//-----------------------------------

//static GLint     _renderHPGL(gpointer key_NOT_USED, gpointer value, gpointer data_NOT_USED)
//static GLint     _renderHPGL(S52_cmdDef *cmdDef, S52_vec *vecObj)
//static S52_vCmd  _renderHPGL(S52_cmdDef *cmdDef, S52_vec *vecObj)
//static S52_vCmd  _renderHPGL(S52_vec *vecObj)
//static S52_vCmd  _parseHPGL(S52_vec *vecObj)
static S57_prim *_parseHPGL(S52_vec *vecObj, S57_prim *vertex)
// Display List generator - use for VBO also
{
    // Select pen Width unit = 0.32 mm.
    // Assume: a width of 1 unit is 1 pixel.
    // WARNING: offet might need adjustment since bounding box
    // doesn't include line tickness but moment all pattern,
    // upto PLib 3.2, use a line width of 1.

    // BUG: instruction EP (Edge Polygon), AA (Arc Angle) and SC (Symbol Call)
    //      are not used in current PLib/Chart-1, so not implemented.

    // NOTE: transparency: 0=0%(opaque), 1=25%, 2=50%, 3=75%


    GLenum    fillMode = GLU_SILHOUETTE;
    //GLenum    fillMode = GLU_FILL;
    S52_vCmd  vcmd     = S52_PL_getVOCmd(vecObj);
    int       CI       = FALSE;

    vertex_t *fristCoord = NULL;

    // debug
    //if (0==strncmp("TSSJCT02", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}
    //if (0==strncmp("BOYLAT13", S52_PL_getVOname(vecObj), 8)) {
    //    PRINTF("Vector Object Name: %s  Command: %c\n", S52_PL_getVOname(vecObj), vcmd);
    //}


    // skip first token if it's S52_VC_NEW
    //if (S52_VC_NEW == vcmd)
    while (S52_VC_NEW == vcmd)
        vcmd = S52_PL_getVOCmd(vecObj);

    // debug - check if more than one NEW token
    if (S52_VC_NEW == vcmd)
        g_assert(0);

    while ((S52_VC_NONE!=vcmd) && (S52_VC_NEW!=vcmd)) {

        switch (vcmd) {

            case S52_VC_NONE: break; //continue;

            case S52_VC_NEW:  break;

            case S52_VC_SW: { // this mean there is a change in pen width
                char pen_w = S52_PL_getVOwidth(vecObj);

                // draw vertex with previous pen width
                GArray *v = S57_getPrimVertex(vertex);
                if (0 < v->len) {
#ifdef  S52_USE_OPENGL_VBO
                    //_loadVBObuffer(vertex);
                    //S57_initPrim(vertex); //reset
#else
                    _DrawArrays(vertex);
                    S57_initPrim(vertex); //reset
#endif
                }

                glLineWidth(pen_w - '0');
                //glLineWidth(pen_w - '0' - 0.5);
                _glPointSize(pen_w - '0');

                //continue;
                break;
            }

            // NOTE: entering poly mode fill a circle (CI) when a CI command
            // is found between PM0 and PM2
            case S52_VC_PM: // poly mode PM0/PM2, fill disk when not in PM
                fillMode = (GLU_FILL==fillMode) ? GLU_SILHOUETTE : GLU_FILL;
                //fillMode = GLU_FILL;
                //continue;
                break;

            case S52_VC_CI: {  // circle --draw immediatly
                GLdouble  radius   = S52_PL_getVOradius(vecObj);
                GArray   *vec      = S52_PL_getVOdata(vecObj);
                //GLdouble *d       = (GLdouble *)vec->data;
                vertex_t *data     = (vertex_t *)vec->data;
                GLint     slices   = 32;
                GLint     loops    = 1;
                GLdouble  inner    = 0.0;        // radius

                GLdouble  outer    = radius;     // in 0.01mm unit

                // pass 'vertex' via global '_diskPrimTmp' used by _gluDisk()
                _diskPrimTmp = vertex;

#ifdef  S52_USE_OPENGL_VBO
                // compose symb need translation at render-time
                // (or add offset everything afterward!)
                _glBegin(_QUADRIC_TRANSLATE, vertex);
                _vertex3f(data, vertex);
                _glEnd(vertex);
#else
                _glMatrixMode(GL_MODELVIEW);
                _glPushMatrix();
                _glTranslated(data[0], data[1], data[2]);
#endif

#ifdef  S52_USE_OPENGL_VBO
                if (GLU_FILL == fillMode) {
                    // FIXME: optimisation, draw a point instead
                    // of a filled disk
                    _gluQuadricDrawStyle(_qobj, GLU_FILL);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                } else {  //LINE
                    _gluQuadricDrawStyle(_qobj, GLU_LINE);
                    _gluDisk(_qobj, inner, outer, slices, loops);
                }
#else
                if (GLU_FILL == fillMode) {
                    // FIXME: optimisation, draw a point instead
                    // of a filled disk
                    gluQuadricDrawStyle(_qobj, GLU_FILL);
                    gluDisk(_qobj, inner, outer, slices, loops);
                }
#endif
                // finish with tmp buffer
                _diskPrimTmp = NULL;


#ifdef  S52_USE_OPENGL_VBO
#else
                // when in fill mode draw outline (antialias)
                gluQuadricDrawStyle(_qobj, GLU_SILHOUETTE);
                //gluQuadricDrawStyle(_qobj, GLU_LINE);
                gluDisk(_qobj, inner, outer, slices, loops);
                _glPopMatrix();
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
                fristCoord = data;

                gluTessBeginPolygon(_tobj, vertex);
                gluTessBeginContour(_tobj);
#ifdef S52_USE_GLES2
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

                _g_ptr_array_clear(_tmpV);

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
                vertex_t *data = (vertex_t *)vec->data;

                if (1 == vec->len) {
                    _glBegin(GL_POINTS,     vertex);
                } else
                    _glBegin(GL_LINE_STRIP, vertex);

#ifdef  S52_USE_GLES2
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

        vcmd = S52_PL_getVOCmd(vecObj);

    } /* while */


#ifdef  S52_USE_OPENGL_VBO
    //_VBOLoadBuffer(vertex);
#else
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
    //if (0 != DL->vboIds[0]) {
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        // NOTE: DL->nbr is 1 - all pattern in PLib have only one color
        // but this is not in S52 specs (check this)
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        // TODO: debug this code path
        g_assert(0);
    }

    //glGenBuffers(DL->nbr, &DL->vboIds[0]);

    // debug
    if (MAX_SUBLIST < DL->nbr) {
        PRINTF("FATAL ERROR: sublist overflow -1-\n");
        g_assert(0);
    }

#else

#ifndef S52_USE_OPENGL_SAFETY_CRITICAL
    // can't delete a display list - no garbage collector in GL SC
    // first delete DL
    //if (0 != DL->vboIds[0]) {
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        glDeleteLists(DL->vboIds[0], DL->nbr);
        DL->vboIds[0] = 0;
    }
#endif

    // then create new one
    DL->vboIds[0] = glGenLists(DL->nbr);
    if (0 == DL->vboIds[0]) {
        PRINTF("FATAL ERROR: glGenLists() failed .. exiting\n");
        g_assert(0);
    }
    //glNewList(DL->start, GL_COMPILE);
    glNewList(DL->vboIds[0], GL_COMPILE);
#endif

    S52_vec *vecObj = S52_PL_initVOCmd(cmdDef);

    // set default
    //glLineWidth(1.0);

    if (NULL == DL->prim[0])
        DL->prim[0]  = S57_initPrim(NULL);
    else
        g_assert(0);


#ifdef  S52_USE_OPENGL_VBO

    // bind VBO in order to use
    //glBindBuffer(GL_ARRAY_BUFFER, DL->vboIds[0]);

    //vcmd = _parseHPGL(vecObj);
    // using VBO we need to keep some info (mode, first, count)
    DL->prim[0] = _parseHPGL(vecObj, DL->prim[0]);
    //_VBOLoadBuffer(DL->prim[0]);
    DL->vboIds[0] = _VBOCreate(DL->prim[0]);

    // set normal mode
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#else

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
        PRINTF("FATAL ERROR: sublist overflow -0-\n");
        g_assert(0);
    }

#ifdef  S52_USE_OPENGL_VBO
    if (TRUE == glIsBuffer(DL->vboIds[0])) {
        glDeleteBuffers(DL->nbr, &DL->vboIds[0]);

        // TODO: debug this code path
        g_assert(0);
    }

    //glGenBuffers(DL->nbr, &DL->vboIds[0]);

#else

#ifndef S52_USE_OPENGL_SAFETY_CRITICAL
    // in ES SC: can't delete a display list --no garbage collector
    if (0 != DL->vboIds[0]) {
        glDeleteLists(DL->vboIds[0], DL->nbr);
    }
#endif

    // then create new one
    DL->vboIds[0] = glGenLists(DL->nbr);
    if (0 == DL->vboIds[0]) {
        PRINTF("FATAL ERROR: glGenLists() failed .. exiting\n");
        g_assert(0);
        return FALSE;
    }
#endif // S52_USE_OPENGL_VBO

    // reset line
    glLineWidth(1.0);
    _glPointSize(1.0);

    S52_vec *vecObj  = S52_PL_initVOCmd(cmdDef);

    guint i = 0;
    for (i=0; i<DL->nbr; ++i) {
        if (NULL == DL->prim[i])
            DL->prim[i]  = S57_initPrim(NULL);
        else
            g_assert(0);
    }

#ifdef  S52_USE_OPENGL_VBO
    for (i=0; i<DL->nbr; ++i) {
        //glBindBuffer(GL_ARRAY_BUFFER, DL->vboIds[i]);
        // using VBO we need to keep some info (mode, first, count)
        DL->prim[i]   = _parseHPGL(vecObj, DL->prim[i]);
        //_VBOLoadBuffer(DL->prim[i]);
        DL->vboIds[i] = _VBOCreate(DL->prim[i]);
    }
    // return to normal mode
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#else
    //GLuint   listIdx = DL->start;
    GLuint listIdx = DL->vboIds[0];
    for (i=0; i<DL->nbr; ++i) {

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
    //PRINTF("PATT symbol finish\n");

    //_setBlend(TRUE);

    S52_PL_traverse(S52_SMB_LINE, _buildSymbDL);
    //PRINTF("LINE symbol finish\n");

    S52_PL_traverse(S52_SMB_SYMB, _buildSymbDL);
    //PRINTF("SYMB symbol finish\n");

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
    // debug
    //S57_geo *geo = S52_PL_getGeo(obj);
    //GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //if (0==strcmp("2135161787", FIDNstr->str)) {
    //    PRINTF("%s\n", FIDNstr->str);
    //}

    // filter on extent is just to small
    // to pick point object
    //if (TRUE == _doPick) {
    //    return TRUE;
    //}

    // debug: CHKSYM01 land here because it is on layer 8, other use layer 9
    S57_geo *geo  = S52_PL_getGeo(obj);
    if (0 == g_strcmp0("$CSYMB", S57_getName(geo))) {

        // this is just to quiet this msg as CHKSYM01/BLKADJ01 pass here (this is normal)
        GString *attval = S57_getAttVal(geo, "$SCODE");
        if (0 == g_strcmp0(attval->str, "CHKSYM01"))
            return FALSE;
        if (0 == g_strcmp0(attval->str, "BLKADJ01"))
            return FALSE;

        PRINTF("%s:%s\n", S57_getName(geo), attval->str);
        return FALSE;
    }

    // clip all other object outside view
    {
        double x1,y1,x2,y2;

        S57_geo *geo = S52_PL_getGeo(obj);
        //S57_getExtPRJ(geo, &x1, &y1, &x2, &y2);
        S57_getExt(geo, &x1, &y1, &x2, &y2);
        double xyz[6] = {x1, y1, 0.0, x2, y2, 0.0};
        if (FALSE == S57_geo2prj3dv(2, (double*)&xyz))
            return FALSE;

        x1  = xyz[0];
        y1  = xyz[1];
        x2  = xyz[3];
        y2  = xyz[4];

        if ((x2 < _pmin.u) || (y2 < _pmin.v) || (x1 > _pmax.u) || (y1 > _pmax.v)) {
            ++_oclip;
            return TRUE;
        }
    }

    return FALSE;
}

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

//int        S52_GL_drawText(S52_obj *obj)
int        S52_GL_drawText(S52_obj *obj, gpointer user_data)
// TE&TX
{
    // quiet compiler
    (void)user_data;

    // debug
    //S57_geo *geoData   = S52_PL_getGeo(obj);
    //if (2861 == S57_getGeoID(geoData)) {
    //    PRINTF("bridge found\n");
    //}

    // optimisation - shortcut all code
    //if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
    //    return TRUE;

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_TXT_TX:
            case S52_CMD_TXT_TE: _ncmd++; _drawText(obj); break;

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
    //if (TRUE == _doPick) {
    //    S57_geo *geo = S52_PL_getGeo(obj);
    //    GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //    if (0==strcmp("2135161787", FIDNstr->str)) {
    //        PRINTF("%s\n", FIDNstr->str);
    //    }
    //}
    //S57_geo *geo = S52_PL_getGeo(obj);
    //PRINTF("drawing geo ID: %i\n", S57_getGeoID(geo));
    //if (2184==S57_getGeoID(geo)) {
    //if (2186==S57_getGeoID(geo)) {
    //    PRINTF("found %i\n", S57_getGeoID(geo));
    //    return TRUE;
    //}
    if (TRUE == _doPick) {
        if (0 == strcmp("PRDARE", S52_PL_getOBCL(obj))) {
            PRINTF("PRDARE found\n");
        }
    }
    //------------------------------------------------------



    if (TRUE == _doPick) {
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

            // this is a trap call for CS that have not been resolve
            case S52_CMD_CND_SY: _traceCS(obj); break;   // CS
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
    if (TRUE == _doPick) {

        // WARNING: some graphic card preffer RGB / BYTE .. YMMV
        glReadPixels(_vp[0], _vp[1], 9, 9, GL_RGBA, GL_UNSIGNED_BYTE, &_read);
        //glReadPixels(_vp[0], _vp[1], 9, 9, GL_RGBA, GL_BYTE, &_read);
        //glReadPixels(_vp[0], _vp[1], 9, 9, GL_RGB, GL_UNSIGNED_BYTE, &_read);

        for (int i=0; i<81; ++i) {
            if (_read[i].color.r == _cIdx.color.r) {
                g_ptr_array_add(_objPick, obj);

                // debug
                PRINTF("pixel found (%i, %i): i=%i color=%X\n", _vp[0], _vp[1], i, _cIdx.color.r);
                break;
            }
        }

        _checkError("S52_GL_draw():glReadPixels()");


        // debug - dump pixel
        for (int i=8; i>=0; --i) {
            int j = i*9;
            printf("%i |%X %X %X %X %X %X %X %X %X|\n", i,
                   _read[j+0].color.r, _read[j+1].color.r, _read[j+2].color.r,
                   _read[j+3].color.r, _read[j+4].color.r, _read[j+5].color.r,
                   _read[j+6].color.r, _read[j+7].color.r, _read[j+8].color.r);
        }
        printf("finish with object: cIdx: %X\n", _cIdx.idx);
        printf("----------------------------\n");
    }


    return TRUE;
}

static int       _contextValid(void)
// return TRUE if current GL context support S52 rendering
{
    if (TRUE == _ctxValidated)
        return TRUE;
    _ctxValidated = TRUE;

    GLint r=0,g=0,b=0,a=0,s=0,p=0;
    //GLboolean m=0,d=0;             // bool true if in RGBA mode

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

#ifdef S52_USE_GLES2
    // shader replace the need for a stencil buffer
#else
    if (s <= 0)
        PRINTF("WARNING: no stencil buffer for pattern!\n");
#endif

    // not in GLES2
    //glGetBooleanv(GL_DOUBLEBUFFER,&d);
    //PRINTF("double buffer: %s\n", ((TRUE==d) ? "TRUE" : "FALSE"));


    {
        const GLubyte *vendor     = glGetString(GL_VENDOR);
        const GLubyte *renderer   = glGetString(GL_RENDERER);
        //const GLubyte *extensions = glGetString(GL_EXTENSIONS);
        const GLubyte *version    = glGetString(GL_VERSION);

        PRINTF("\nVendor:%s\nRenderer:%s\nVersion:%s\n", vendor, renderer, version);
    }


    _checkError("_contextValid()");

    return TRUE;
}

#if 0
static int       _stopTimer(int damage)
// strop timer and print stat
{
    //gdouble sec = g_timer_elapsed(_timer, NULL);
    ////g_print("%.0f msec (%i obj / %i cmd / %i FIXME frag / %i objects clipped)\n",
    ////        sec * 1000, _nobj, _ncmd, _nFrag, _oclip);

    //g_print("%.0f msec (%s)\n", sec * 1000, damage ? "chart" : "mariner");
    //printf("%.0f msec (%s)\n", sec * 1000, damage ? "chart" : "mariner");
    //g_print("(%i obj / %i cmd / %i FIXME frag / %i objects clipped)\n",
    //        _nobj, _ncmd, _nFrag, _oclip);

    return TRUE;
}
#endif

static int       _doProjection(double centerLat, double centerLon, double rangeDeg)
{
    //if (isnan(centerLat) || isnan(centerLon) || isnan(rangeDeg))
    //    return FALSE;

    //projUV Tm, Bm;     // Top  / Bottom (North/south) mercator
    //double Lm = 0.0,
    //       Rm = 0.0;   // Left / Right  (West/East)   mercator
    //double Hm = 0.0;   // requested mercator height of screen
    //double Wm = 0.0;   // mercator width of screen to fit screen pixel ratio (w/h)


    pt3 NE = {0.0, 0.0, 0.0};  // Nort/East
    pt3 SW = {0.0, 0.0, 0.0};  // South/West
    NE.y = centerLat + rangeDeg;
    SW.y = centerLat - rangeDeg;
    NE.x = SW.x = centerLon;

#ifndef S52_USE_GV
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

        // ratio
        double r = (double)h / (double)w;   // > 1 'h' dominant, < 1 'w' dominant
        //r = (double)w / (double)h;   // > 1 'h' dominant, < 1 'w' dominant
        //PRINTF("Viewport pixels (width x height): %i %i (r=%f)\n", w, h, r);

        double dy = NE.y - SW.y;
        // assume h dominant (latitude), so range fit on screen in latitude
        double dx = dy / r;

        NE.x += (dx / 2.0);
        SW.x -= (dx / 2.0);
    }

    _pmin.u = SW.x;  // left
    _pmin.v = SW.y;  // bottom
    _pmax.u = NE.x;  // right
    _pmax.v = NE.y;  // top

    //PRINTF("PROJ MIN: %f %f  MAX: %f %f\n", _pmin.u, _pmin.v, _pmax.u, _pmax.v);

    return TRUE;
}


int        S52_GL_begin(int cursorPick, int drawLast)
//int        S52_GL_begin(int cursorPick)
{
    CHECK_GL_END;
    //CHECK_GL_BEGIN;
    _GL_BEGIN = TRUE;

    //static int saveAttrib;

    //_checkError("S52_GL_begin() -0-");

    //_dumpATImemInfo(VBO_FREE_MEMORY_ATI);          // 0x87FB
    //_dumpATImemInfo(TEXTURE_FREE_MEMORY_ATI);      // 0x87FC
    //_dumpATImemInfo(RENDERBUFFER_FREE_MEMORY_ATI); // 0x87FD

    // debug
    _drgare = 0;

#ifdef S52_USE_COGL
    cogl_begin_gl();
#endif

#if (defined S52_USE_GLES2 || defined S52_USE_OPENGL_SAFETY_CRITICAL)
#else
    glPushAttrib(GL_ALL_ATTRIB_BITS);
#endif

    // check if this context (grahic card) can draw all of S52
    _contextValid();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    //glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    //glHint(GL_POINT_SMOOTH_HINT, GL_DONT_CARE);
    //glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);


#ifdef  S52_USE_GLES2
#else

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
    if (TRUE == cursorPick) {
        _doPick   = TRUE;
        //_cIdx.idx = 0;
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
        //glDisable(GL_FOG);             // NOT in "OpenGL ES SC"
        //glDisable(GL_LIGHTING);        // NOT in GLES2
        //glDisable(GL_TEXTURE_1D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_TEXTURE_2D);      // fail on GLES2
        //glDisable(GL_TEXTURE_3D);      // NOT in "OpenGL ES SC"
        //glDisable(GL_ALPHA_TEST);      // NOT in GLES2

        //glShadeModel(GL_FLAT);

    } else {
        if (TRUE == S52_MP_get(S52_MAR_ANTIALIAS)) {
            glEnable(GL_BLEND);
            // NOTE: point smoothing is ugly with point pattern
            //glEnable(GL_POINT_SMOOTH);

#ifdef S52_USE_GLES2
#else
            glEnable(GL_LINE_SMOOTH);
            glEnable(GL_ALPHA_TEST);
#endif

        } else {
            // need to explicitly disable since
            // OpenGL state stay the same from draw to draw
            glDisable(GL_BLEND);
            //glDisable(GL_POINT_SMOOTH);

#ifdef S52_USE_GLES2
#else
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

#ifndef S52_USE_OPENGL_SAFETY_CRITICAL
    glDisable(GL_DITHER);                   // NOT in OpenGL ES SC
#endif

#ifdef S52_USE_GLES2
#else
    glShadeModel(GL_FLAT);
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

    // FIXME: hack
    if (TRUE != cursorPick) {
        // this will setup _pmin/_pmax, need a valide _vp
        _doProjection(_centerLat, _centerLon, _rangeNM/60.0);
    }

    // then create all symbol
    _createSymb();

    // _createSymb() might reset line width
    glLineWidth(1.0);

    // -------------------------------------------------------



    _SCAMIN = _computeSCAMIN();

    if (_fb_pixels_size < (_vp[2] * _vp[3] * 4) ) {
        PRINTF("ERROR: pixels buffer overflow: fb_pixels_size=%i, VP=%i \n", _fb_pixels_size, (_vp[2] * _vp[3] * 4));
        // NOTE: since the assert() is removed in the release, draw last can
        // still be called (but does notting) if _fb_pixels is NULL
        g_free(_fb_pixels);
        _fb_pixels = NULL;

        g_assert(0);
        exit(0);
    }

// GV set the matrix it self
// also don't erase GV stuff with NODTA
#ifndef S52_USE_GV
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

#ifdef S52_USE_GLES2
#else
    glGetDoublev(GL_MODELVIEW_MATRIX,  _mvm);
    glGetDoublev(GL_PROJECTION_MATRIX, _pjm);
#endif

    // make sure nothing is bound (crash fglrx at DrawArray())
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    //-- update background ------------------------------------------
    //
    if (TRUE != drawLast) {
        // CS DATCVR01: 2.2 - No data areas
        if (1.0 == S52_MP_get(S52_MAR_DISP_NODATA_LAYER)) {
            // fill display with 'NODTA' color
            _renderAC_NODATA_layer0();

            // fill with NODATA03 pattern
            _renderAP_NODATA_layer0();
        }
    } else {

        if (TRUE == _update_fb) {
            S52_GL_readFBPixels();
        }

        // load FB that was filled with the previous draw() call
        S52_GL_drawFBPixels();
        //S52_GL_drawBlit(0.0, 0.0, 0.0, 0.0);
        _update_fb = FALSE;
    }
    //---------------------------------------------------------------

#endif  // S52_USE_GV


    _checkError("S52_GL_begin() - fini");

    return TRUE;
}

int        S52_GL_end(int drawLast)
//
{
    CHECK_GL_BEGIN;

#ifdef S52_USE_GLES2
    // at this point all object VBO's have been reset to 0
    // so signal the next _app() cycle to skip VBO re-ini
    _resetVBOID = FALSE;

    //* test - use GLES2 FBO instead of PBuffer
    //if (FALSE == drawLast) {
    //    // return to normal FBO
    //    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    //}
#else
    glDisableClientState(GL_VERTEX_ARRAY);
#endif

#ifndef S52_USE_GV
    _glMatrixDel(VP_PRJ);
#endif



#if (defined S52_USE_GLES2 || defined S52_USE_OPENGL_SAFETY_CRITICAL)
#else
    glPopAttrib();     // NOT in OpenGL ES SC
#endif

    // this seem to loop i
    //_checkError("S52_GL_end()");

    // end picking (return to normal or stay normal)
    _doPick = FALSE;

    // flag that the FB changed
    // and _fb_pixels need updating
    if (FALSE == drawLast)
        _update_fb = TRUE;

#ifdef S52_USE_COGL
    cogl_end_gl();
#endif


    //CHECK_GL_BEGIN;
    _GL_BEGIN = FALSE;

    if (NULL != _objhighlight) {
        S52_PL_highlightOFF(_objhighlight);
        _objhighlight = NULL;
    }

    // hang xoom if no drawFB!
    //if (TRUE == drawLast) {
    //    //glFlush();
    //    glFinish();
    //}

    return TRUE;
}

int        S52_GL_del(S52_obj *obj)
// delete geo Display List associate to an object
{
    S57_geo  *geoData = S52_PL_getGeo(obj);
    S57_prim *prim    = S57_getPrimGeo(geoData);

// SC can't delete a display list --no garbage collector
#ifndef S52_USE_OPENGL_SAFETY_CRITICAL
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

        // delete text
        // ???
#else
        // 'vboID' is in fact a DList here
        if (GL_TRUE == glIsList(vboID)) {
            glDeleteLists(vboID, 1);
        } else {
            PRINTF("WARNING: ivalid DL\n");
            g_assert(0);
        }
#endif


    }
#endif

    _checkError("S52_GL_del()");

    return TRUE;
}

#ifdef S52_USE_GLES2
static GLuint    _loadShader(GLenum type, const char *shaderSrc)
{
    GLuint shader;
    GLint  compiled;

    shader = glCreateShader(type);
    if (0 == shader) {
        PRINTF("ERROR: glCreateShader() failed\n");
        _checkError("_loadShader()");
        g_assert(0);
        return FALSE;
    }

    glShaderSource(shader, 1, &shaderSrc, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    // Check if compilation succeeded
    if(0 == compiled) {
		// An error happened, first retrieve the length of the log message
        int logLen   = 0;
        int writeOut = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);

		// Allocate enough space for the message and retrieve it
		char log[256];
        glGetShaderInfoLog(shader, logLen, &writeOut, log);

        PRINTF("ERROR: glCompileShader() fail: %s\n", log);

        _checkError("_loadShader()");

        g_assert(0);
        return FALSE;
    }

    return shader;
}

int        S52_GL_resetVBOID(void)
{
    return _resetVBOID;
}

int        S52_GL_init_GLES2(void)
{
    GLint  linked;
    const char* vShaderStr =
        "uniform   mat4  uProjection;  \n"
        "uniform   mat4  uModelview;   \n"
//        "uniform   vec4  uColor;       \n"
        "uniform   float uPointSize;   \n"
        "uniform   float uStipOn;      \n"
        "uniform   float uBlitOn;      \n"
        "                              \n"
        "uniform   float uPattOn;      \n"
        "uniform   float uPattOffX;    \n"
        "uniform   float uPattOffY;    \n"
        "uniform   float uPattX;       \n"
        "uniform   float uPattY;       \n"
        "                              \n"
        "attribute vec2  aUV;          \n"
        "attribute vec4  aPosition;    \n"
        "attribute vec4  aColor;       \n"
        "attribute float aAlpha;       \n"
        "                              \n"
//        "varying   vec4  v_color;      \n"
        "varying   vec4  v_acolor;     \n"
        "varying   vec2  v_texCoord;   \n"
//        "varying   vec4  v_texCoord;   \n"
        "varying   float v_pattOn;     \n"
        "varying   float v_alpha;      \n"
//        "varying   float v_cnt;        \n"
        "                              \n"
//        "varying   float v_tmp;        \n"
//        " vec4     tmp4d;              \n"
        "                              \n"
//        "float     alpha = 0.0;        \n"
//        "vec4  aUV4;                   \n"
        "void main(void)               \n"
        "{                             \n"
        //        "   v_color      = uColor;     \n"
        "                              \n"
        " v_alpha     = aAlpha;        \n"
//        "   v_acolor      = aColor;     \n"
//        "   v_tmp        = uProjection * vec4(aUV.x, aUV.y, 0.0, 1.0);  \n"
//        "   v_tmp        = uProjection * aUV;  \n"
//        "                              \n"
//        "   if (1.0 == uStipOn) {        \n"
//        "     v_texCoord = aUV.xy;      \n"
//        "     v_texCoord = aUV;      \n"
//        "   }                           \n"
        "                               \n"
        "                               \n"
        "                               \n"
        "   gl_Position  = uProjection * uModelview * aPosition;  \n"
//        "   v_texCoord   = uProjection * uModelview * aUV;  \n"
        "   if (1.0 == uPattOn) {          \n"
//        "       v_texCoord.x = aPosition.x; \n"
//        "       v_texCoord.y = aPosition.y; \n"
//        "       v_texCoord.x = aPosition.x / uPattX; \n"
//        "       v_texCoord.y = aPosition.y / uPattY; \n"
        "       v_texCoord.x = (aPosition.x - uPattOffX) / uPattX; \n"
        "       v_texCoord.y = (aPosition.y - uPattOffY) / uPattY; \n"
        "   } else {                       \n"
        "     v_texCoord = aUV;            \n"
        "   }                              \n"
        "   gl_PointSize = uPointSize;     \n"
        "}                                 \n";


//        "#ifdef GL_ES                  \n"
//        "precision highp float;        \n"
//        "#endif                        \n"
//        "                              \n"
    const char* fShaderStr =
        "#pragma profilepragma blendoperation(gl_FragColor, GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)\n"
//        "#pragma profilepragma blendoperation(gl_FragColor, GL_FUNC_ADD, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)\n"
        "precision mediump float;      \n"
        "                              \n"
        "uniform sampler2D uSampler2d; \n"
//        "uniform sampler2D uCircle2d; \n"
        "uniform float     uBlitOn;    \n"
        "uniform float     uPattOn;    \n"
        "uniform float     uPattX;     \n"
        "uniform float     uPattY;     \n"
//        "uniform float     uPattMaxX;  \n"
//        "uniform float     uPattMaxY;  \n"
        "uniform float     uGlowOn;    \n"
        "uniform float     uStipOn;    \n"
        "uniform vec4      uColor;     \n"
        "                              \n"
        "varying vec2  v_texCoord;     \n"
//        "varying float v_pattOn;       \n"
        "varying float v_alpha;        \n"
//        "varying float v_cnt;          \n"
//        "varying vec2  v_coords;       \n"
//        "varying float v_tmp;          \n"
        "                              \n"
        "vec2 coords;                  \n"
        "void main(void)               \n"
        "{                             \n"
        "                              \n"
        "    if (1.0 == uStipOn) {     \n"
        "       gl_FragColor = texture2D(uSampler2d, v_texCoord); \n"
        "       gl_FragColor.rgb = uColor.rgb;                    \n"
        "    } else {                  \n"
        "                              \n"
        "                              \n"
        "                              \n"
        "    if (1.0 == uBlitOn) {     \n"
        "       gl_FragColor = texture2D(uSampler2d, v_texCoord);\n"
        "    }                               \n"
        "    else                            \n"
        "    {                               \n"
            "    if (1.0 == uPattOn) {          \n"
//            "       coords.x = v_texCoord.x;                      \n"
//            "       coords.y = v_texCoord.y;                      \n"
//            "       coords.x = v_texCoord.x * uPattMaxX;                      \n"
//            "       coords.y = v_texCoord.y * uPattMaxY;                      \n"
//            "       coords.x = clamp(v_texCoord.x, 0.0, uPattMaxX);                      \n"
//            "       coords.y = clamp(v_texCoord.y, 0.0, uPattMaxY);                      \n"
//            "       coords.x = v_texCoord.x / uPattMaxX;                      \n"
//            "       coords.y = v_texCoord.y / uPattMaxY;                      \n"
//            "       if (0.5 < fract(coords.x)) {coords.x -= 0.5;}          \n"
//            "       if (0.5 < fract(coords.y)) {coords.y -= 0.5;}          \n"
//            "       if (uPattMaxX < fract(v_texCoord.x)) {discard;}          \n"
//            "       if (uPattMaxY < fract(v_texCoord.y)) {discard;}          \n"
            "       gl_FragColor = texture2D(uSampler2d, v_texCoord);\n"
//            "       gl_FragColor = texture2D(uSampler2d, coords);\n"
            "       gl_FragColor.rgb = uColor.rgb; \n"
// debug
//            "       gl_FragColor = vec4( v_texCoord.x, v_texCoord.y, 0.0, 1.0 );  \n"
            "    }                                 \n"
            "    else                              \n"
            "    {                                 \n"
                "    if (0.0 < uGlowOn) {          \n"
//                "       if (0.5 < (gl_PointCoord.s + gl_PointCoord.t)) {      \n"
//                "       if (1.0 > sqrt((gl_PointCoord.x-0.5)*(gl_PointCoord.x-0.5) + (gl_PointCoord.y-0.5)*(gl_PointCoord.y-0.5))) {      \n"
                "       if (0.5 > sqrt((gl_PointCoord.x-0.5)*(gl_PointCoord.x-0.5) + (gl_PointCoord.y-0.5)*(gl_PointCoord.y-0.5))) {      \n"
//                    "       gl_FragColor   = v_color;   \n"
                    "       gl_FragColor   = uColor;    \n"
                    "       gl_FragColor.a = v_alpha;   \n"
                    "   }                               \n"
                    "      else {                       \n"
                    "       discard;                    \n"
                    "   }                               \n"
                "    }                                  \n"
                "    else                               \n"
                "    {                                  \n"
//                "       gl_FragColor = v_color;      \n"
                "       gl_FragColor = uColor;      \n"
                "    }                              \n"
            "    }                               \n"
            "}                                   \n"
            "}                                   \n"
        "}                                   \n";


    //============ J U N K ==============================
    //        "       x = gl_FragCoord.x;    \n"
    //        "       d++;                   \n"
    //        "       d = (int)(x / 10.0);   \n"
    //        "       y = d * 10.0;          \n"
    //        "       x = x - y;             \n"
    //        "       if (pos.x>20.0) {      \n"
    //        "       if (d>50) {            \n"
    //        "          discard;            \n"
    //        "       }                      \n"
    //        "       if ((x>0.0) && (x<10.0) && (d>100)) {   \n"
    //        "       if ((x<10.0) && (d>100)) {              \n"
    //        "       if (d>100) {           \n"
    //        "          d=0;                \n"
    //        "          discard;            \n"
    //        "       } else {               \n"
    //        "          gl_FragColor = uColor; \n"
    //        "       }                      \n"
    //        "    if (i<10)) discard;       \n"
    //
    //============ J U N K ==============================



    PRINTF("begin ..\n");

    if (TRUE == glIsProgram(_programObject)) {
        PRINTF("DEBUG: _programObject valid not re-init\n");
        return TRUE;
    }


    if (FALSE == glIsProgram(_programObject)) {

        PRINTF("DEBUG: re-building '_programObject'\n");
        _resetVBOID = TRUE;

        _programObject  = glCreateProgram();
        PRINTF("GL_VERTEX_SHADER\n");
        _vertexShader   = _loadShader(GL_VERTEX_SHADER,   vShaderStr);
        PRINTF("GL_FRAGMENT_SHADER\n");
        _fragmentShader = _loadShader(GL_FRAGMENT_SHADER, fShaderStr);

        if ((0==_programObject) || (0==_vertexShader) || (0==_fragmentShader)) {
            PRINTF("problem loading shaders and/or creating program!");
            g_assert(0);
            exit(0);
            return FALSE;
        }
        _checkError("S52_GL_initGLES2() -0-");

        if (TRUE != glIsShader(_vertexShader))
            exit(0);
        if (TRUE != glIsShader(_fragmentShader))
            exit(0);

        _checkError("S52_GL_initGLES2() -1-");

        glAttachShader(_programObject, _vertexShader);
        glAttachShader(_programObject, _fragmentShader);
        glLinkProgram (_programObject);
        glGetProgramiv(_programObject, GL_LINK_STATUS, &linked);
        if(!linked){
            PRINTF("problem linking program!");
            g_assert(0);
            exit(0);
            return FALSE;
        }

        _checkError("S52_GL_initGLES2() -2-");

        //use the program
        glUseProgram(_programObject);


/*
        // load texture on GPU ----------------------------------

        _nodata_mask_texID = 0;
        _dottpa_mask_texID = 0;

        // fill _rgba_nodata_mask - expand bitmask to a RGBA buffer
        // that will acte as a stencil in the fragment shader
        _1024bitMask2RGBATex(_nodata_mask_bits, _nodata_mask_rgba);
        //_64bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);
        _32bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);

        glGenTextures(1, &_nodata_mask_texID);
        glGenTextures(1, &_dottpa_mask_texID);

        _checkError("_renderAP_NODATA_layer0 -0-");

        // ------------
        // nodata pattern
        glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        // FIXME: maybe there is a way to expand the mask to rgba here
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, _nodata_mask_rgba);

        // ------------
        // dott pattern
        glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, _dottpa_mask_rgba);

        glBindTexture(GL_TEXTURE_2D, 0);
        _checkError("_renderAP_NODATA_layer0 -0.1-");

        // setup FBO  ----------------------------------

        glGenFramebuffers (1, &_frameBufferID);
        glGenRenderbuffers(1, &_renderBufferID);
*/

#ifdef S52_USE_FREETYPE_GL
        _text_textureID    = 0;
        _init_freetype_gl();
#endif

        _checkError("S52_GL_initGLES2() -3-");
    }


    //load all attributes
    //FIXME: move to bindShaderAttrib();
    _aPosition   = glGetAttribLocation(_programObject, "aPosition");
    _aColor      = glGetAttribLocation(_programObject, "aColor");
    _aUV         = glGetAttribLocation(_programObject, "aUV");
    _aAlpha      = glGetAttribLocation(_programObject, "aAlpha");

    //FIXME: move to bindShaderUnifrom();
    _uProjection = glGetUniformLocation(_programObject, "uProjection");
    _uModelview  = glGetUniformLocation(_programObject, "uModelview");
    _uColor      = glGetUniformLocation(_programObject, "uColor");
    _uPointSize  = glGetUniformLocation(_programObject, "uPointSize");
    _uSampler2d  = glGetUniformLocation(_programObject, "uSampler2d");
    _uBlitOn     = glGetUniformLocation(_programObject, "uBlitOn");
    _uGlowOn     = glGetUniformLocation(_programObject, "uGlowOn");
    _uStipOn     = glGetUniformLocation(_programObject, "uStipOn");

    _uPattOn     = glGetUniformLocation(_programObject, "uPattOn");
    _uPattOffX   = glGetUniformLocation(_programObject, "uPattOffX");
    _uPattOffY   = glGetUniformLocation(_programObject, "uPattOffY");
    _uPattX      = glGetUniformLocation(_programObject, "uPattX");
    _uPattY      = glGetUniformLocation(_programObject, "uPattY");
//    _uPattMaxX   = glGetUniformLocation(_programObject, "uPattMaxX");
//    _uPattMaxY   = glGetUniformLocation(_programObject, "uPattMaxY");

    //clear FB ALPHA before use, also put blue but doen"t show
    //glClearColor(0, 0, 1, 1);     // blue
    glClearColor(1, 0, 0, 1);     // red
    glClear(GL_COLOR_BUFFER_BIT);

    // case of Android restarting - maybe useless
    //if (0 != _fb_texture_id) {
    //    glDeleteBuffers(1, &_fb_texture_id);
    //    _fb_texture_id = 0;
    //}

    glGenBuffers(1, &_fb_texture_id);
    glBindTexture  (GL_TEXTURE_2D, _fb_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGBA, _vp[2], _vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    glBindTexture  (GL_TEXTURE_2D, 0);


    _checkError("S52_GL_initGLES2() -fini-");

    return TRUE;
}
#endif  // S52_USE_GLES2

int        S52_GL_init(void)
// return TRUE on success
{
    if (!_doInit) {
        PRINTF("WARNING: S52 GL allready initialize!\n");
        return _doInit;
    }

    // debug
    //_contextValid();

    // juste checking
    g_assert(sizeof(double) == sizeof(GLdouble));

    _checkError("S52_GL_init() -0-");

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

#ifdef S52_USE_A3D
    _initA3D();
#endif


#ifdef S52_USE_GLES2
    S52_GL_init_GLES2();

    // FIXME: first init_GLES2 no VBO created
    // yet and also object have not been initialize
    // so this break at _GL_del()
    // For now just skip VBO deletion at first init()
    //_resetVBOID = FALSE;

#endif



    if (NULL == _objPick)
        _objPick = g_ptr_array_new();

    _doInit = FALSE;

    // signal to build display list
    _symbCreated = FALSE;

    // signal to validate GL context
    _ctxValidated = FALSE;

    //_DEBUG = TRUE;

#ifdef S52_USE_GLES2
    if (NULL == _tessWorkBuf_d)
        _tessWorkBuf_d = g_array_new(FALSE, FALSE, sizeof(double)*3);
    if (NULL == _tessWorkBuf_f)
        _tessWorkBuf_f = g_array_new(FALSE, FALSE, sizeof(float)*3);
#else
    if (NULL == _aftglwColorArr)
        _aftglwColorArr = g_array_new(FALSE, FALSE, sizeof(unsigned char));
#endif

    //return _doInit;
    return TRUE;;
}

int        S52_GL_setDotPitch(int w, int h, int wmm, int hmm)
{
    _dotpitch_mm_x = (double)wmm / (double)w;
    _dotpitch_mm_y = (double)hmm / (double)h;

    S52_MP_set(S52_MAR_DOTPITCH_MM_X, _dotpitch_mm_x);
    S52_MP_set(S52_MAR_DOTPITCH_MM_Y, _dotpitch_mm_y);

    // nop, this give a 0.41 dotpitch
    //double dotpitch_mm = sqrt(_dotpitch_mm_x*_dotpitch_mm_x + _dotpitch_mm_y*_dotpitch_mm_y);
    //_dotpitch_mm_x = dotpitch_mm;
    //_dotpitch_mm_y = dotpitch_mm;
    //double diag_mm = sqrt(hmm*hmm + wmm*wmm);
    //_dotpitch_mm_x = diag_mm / w;
    //_dotpitch_mm_y = diag_mm / h;
    //X = 0.376281, Y = 0.470351

    // debug
    PRINTF("DOTPITCH: X = %f, Y = %f\n", _dotpitch_mm_x, _dotpitch_mm_y);
    // debug:
    // 1 screen: X = 0.293750, Y = 0.293945
    // 2 screen: X = 0.264844, Y = 0.264648

    _fb_pixels_size = w * h * 4;
    _fb_pixels      = g_new0(unsigned char, _fb_pixels_size);


    // debug glRatio
    //_dotpitch_mm_x = 0.34;
    //_dotpitch_mm_y = 0.34;

    //_dotpitch_mm_x = 0.335;
    //_dotpitch_mm_y = 0.335;


    //_dotpitch_mm_x = 0.33;
    //_dotpitch_mm_y = 0.33;

    // S52 say:
    //_dotpitch_mm_x = 0.32;
    //_dotpitch_mm_y = 0.32;


    //_dotpitch_mm_x = 0.30;
    //_dotpitch_mm_y = 0.30;

    return TRUE;
}

#if 0
/* DEPRECATED
 int        S52_GL_setFontDL
 (int fontDL)
{
    if (_doInit) {
        PRINTF("ERROR: S52 GL not initialize\n");
        return FALSE;
    }

// this test is done when calling any Display List
//#ifndef S52_USE_OPENGL_SAFETY_CRITICAL
//    if (!glIsList(fontDL)) {
//        PRINTF("ERROR: not a display list\n");
//        return FALSE;
//    }
//#endif

    // debug
    //PRINTF("NOTE: font Display List = %i\n", fontDL);
    if (S52_MAX_FONT > _fontDListIdx)
        _fontDList[_fontDListIdx++] = fontDL;

    return TRUE;
}
*/
#endif

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
        //g_ptr_array_unref(_objPick);
        _objPick = NULL;
    }

    if (NULL != _aftglwColorArr) {
        g_array_free(_aftglwColorArr, TRUE);
        _aftglwColorArr = NULL;
    }

    /*
    if (0 != _vboIDaftglwVertID) {
        glDeleteBuffers(1, &_vboIDaftglwVertID);
        _vboIDaftglwVertID = 0;
    }
    if (0 != _vboIDaftglwVertID) {
        glDeleteBuffers(1, &_vboIDaftglwVertID);
        _vboIDaftglwVertID = 0;
    }
    */


#ifdef S52_USE_GLES2
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

    glDeleteFramebuffers (1, &_framebufferID);
    _framebufferID = 0;

    if (glIsRenderbuffer(_colorRenderbufferID)) {
        glDeleteRenderbuffers(1, &_colorRenderbufferID);
        _colorRenderbufferID = 0;
    }

#ifdef S52_USE_FREETYPE_GL
    //texture_font_delete(_freetype_gl_font);
    //_freetype_gl_font = NULL;
    texture_font_delete(_freetype_gl_font[0]);
    texture_font_delete(_freetype_gl_font[1]);
    texture_font_delete(_freetype_gl_font[2]);
    texture_font_delete(_freetype_gl_font[3]);
    _freetype_gl_font[0] = NULL;
    _freetype_gl_font[1] = NULL;
    _freetype_gl_font[2] = NULL;
    _freetype_gl_font[3] = NULL;

    if (0 != _text_textureID) {
        glDeleteBuffers(1, &_text_textureID);
        _text_textureID = 0;
    }
    if (NULL != _textWorkBuf) {
        g_array_free(_textWorkBuf, TRUE);
        _textWorkBuf = NULL;
    }
#endif

#endif  // S52_USE_GLES2



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

    _diskPrimTmp = S57_donePrim(_diskPrimTmp);

    _doInit = TRUE;

    return _doInit;
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

int        S52_GL_setPRJView(double  s, double  w, double  n, double  e)
{
    _pmin.v = s;
    _pmin.u = w;
    _pmax.v = n;
    _pmax.u = e;

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

    if (_fb_pixels_size < (_vp[2] * _vp[3] * 4) ) {
        //PRINTF("ERROR: pixels buffer overflow: fb_pixels_size=%i, VP=%i \n", _fb_pixels_size, (_vp[2] * _vp[3] * 4));
        // NOTE: since the assert() is removed in the release, draw last can
        // still be called (but does notting) if _fb_pixels is NULL
        //g_free(_fb_pixels);
        //_fb_pixels = NULL;
        //g_assert(0);
        //exit(0);

        //#define             g_renew(struct_type, mem, n_structs)
        //_fb_pixels      = g_new0(unsigned char, _fb_pixels_size);
        _fb_pixels_size = _vp[2] * _vp[3] * 4;
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

char      *S52_GL_getNameObjPick(void)
{
    if (_objPick->len > 0) {
        char        *name  = NULL;
        unsigned int S57ID = 0;

        PRINTF("----------- PICK(%i) ---------------\n", _objPick->len);

        for (guint i=0; i<_objPick->len; ++i) {
            S52_obj *obj = (S52_obj*)g_ptr_array_index(_objPick, i);
            S57_geo *geo = S52_PL_getGeo(obj);

            name  = S57_getName(geo);
            S57ID = S57_getGeoID(geo);

            PRINTF("%i  : %s\n", i, name);
            PRINTF("LUP : %s\n", S52_PL_getCMD(obj));
            PRINTF("DPRI: %i\n", (int)S52_PL_getDPRI(obj));
            S57_dumpData(geo, FALSE);
            PRINTF("-----------\n");
        }

        // hightlight object at the top
        _objhighlight = (S52_obj*)g_ptr_array_index(_objPick, _objPick->len-1);
        S52_PL_highlightON(_objhighlight);

        SPRINTF(_strPick, "%s:%i", name, S57ID);

    } else {
        PRINTF("NOTE: no S57 object\n");
    }

    return _strPick;
}

#if 0
//int        S52_GL_setOWNSHP(double breadth, double length)
int        S52_GL_setOWNSHP(S52_obj *obj, double heading)
{
    _shpbrd = breadth;
    _shplen = length;

    if (breadth < 1.0) _shpbrd = 1.0;
    if (length  < 1.0) _shplen = 1.0;

    return TRUE;
}
#endif

guchar    *S52_GL_readFBPixels(void)
{
    if (TRUE==_doPick || NULL==_fb_pixels)
        return FALSE;

    // copy framebuffer
    //glReadBuffer(GL_FRONT);

    // Wierd Planet: Clutter need GL_UNSIGNED_BYTE for read() and GL_BYTE for write(), GL_RGBA
    // Note also that in normal OpenGL this setup accumulate layer 9!!
    // Also FTGL rendering is lost
    //
    // this leak on Radeon HD 3450 (fglrx)
    glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);  // leak
    //glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGBA, GL_BYTE, _fb_pixels);             // leak less (but slower)
    //glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);   // leak
    //glReadPixels(_vp[0], _vp[1], _vp[2], _vp[3], GL_RGB, GL_BYTE, _fb_pixels);            // leak + slow

    //glReadBuffer(GL_BACK);
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    _checkError("S52_GL_readFBPixels()");

    //_update_fb = FALSE;

    return _fb_pixels;
}

#include "gdal.h"  // GDAL stuff to write .PNG

int        S52_GL_dumpS57IDPixels(const char *toFilename, S52_obj *obj, unsigned int width, unsigned int height)
// FIXME: width/height rounding error all over - fix: +0.5
// to test 2 PNG using Python Imaging Library (PIL):
{
    GDALDriverH   driver  = NULL;
    GDALDatasetH  dataset = NULL;

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
        //x = p.u - width/2;
        if ((p.u - width/2)  <      0) x = width/2;
        if ((x   + width  )  > _vp[2]) x = _vp[2] - width;
        //y = p.v - height/2;
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


    dataset = GDALCreate(driver, "_temp.tif", width, height, 3, GDT_Byte, NULL);
    if (NULL == dataset) {
        PRINTF("WARNING: fail to create GDAL data set\n");
        return FALSE;
    }
    GDALRasterBandH bandR = GDALGetRasterBand(dataset, 1);
    GDALRasterBandH bandG = GDALGetRasterBand(dataset, 2);
    GDALRasterBandH bandB = GDALGetRasterBand(dataset, 3);

    // get framebuffer pixels
    guint8 *pixels = g_new0(guint8, width * height * 3);
    //guint x = p.u-((int)width  / 2.0 );
    //guint y = p.v-((int)height / 2.0 );

#ifdef S52_USE_GLES2
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
#else
    glReadBuffer(GL_FRONT);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glReadBuffer(GL_BACK);
#endif


    //*
    // flip vertically
    {
        guint8 *flipbuf = g_new0(guint8, width * height * 3);
        unsigned int i = 0;
        for (i=0; i<height; ++i) {
            memcpy(flipbuf + ((height-1) - i) * width * 3,
                   pixels + (i * width * 3),
                   width * 3);
        }
        g_free(pixels);
        pixels = flipbuf;
    }
    //*/
    // write to temp file
    GDALRasterIO(bandR, GF_Write, 0, 0, width, height, pixels+0, width, height, GDT_Byte, 3, 0 );
    GDALRasterIO(bandG, GF_Write, 0, 0, width, height, pixels+1, width, height, GDT_Byte, 3, 0 );
    GDALRasterIO(bandB, GF_Write, 0, 0, width, height, pixels+2, width, height, GDT_Byte, 3, 0 );

    GDALClose(dataset);
    g_free(pixels);

    //gdal.GetDriverByName('PNG').CreateCopy(self.file.get_text(), gdal.Open('_temp.tif'),TRUE)
    GDALDatasetH dst_dataset;
    driver      = GDALGetDriverByName("PNG");
    dst_dataset = GDALCreateCopy(driver, toFilename, GDALOpen("_temp.tif", GA_ReadOnly), FALSE, NULL, NULL, NULL);
    GDALClose(dst_dataset);

    return TRUE;
}

int        S52_GL_drawFBPixels(void)
{
    if (TRUE==_doPick || NULL==_fb_pixels)
        return FALSE;

    // debug
    //PRINTF("DEBUG: S52_GL_drawFBPixels() .. XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    //return TRUE;

    // set rotation temporatly to 0.0  (MatrixSet)
    double north = _north;
    _north = 0.0;


#ifdef S52_USE_GLES2

    _glMatrixSet(VP_PRJ);

    glBindTexture(GL_TEXTURE_2D, _fb_texture_id);
    if (TRUE == _update_fb) {
        //PRINTF("DEBUG:glTexImage2D()\n");
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _vp[2], _vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    }

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

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // turn OFF 'sampler2d'
    glUniform1f(_uBlitOn, 0.0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    _glMatrixDel(VP_PRJ);

#else


    _glMatrixSet(VP_WIN);
    glRasterPos2i(0, 0);

    // parameter must be in sync with glReadPixels()
    glDrawPixels(_vp[2], _vp[3], GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    //glDrawPixels(_vp[2], _vp[3], GL_RGBA, GL_BYTE,         _fb_pixels);
    //glDrawPixels(_vp[2], _vp[3], GL_RGB, GL_UNSIGNED_BYTE, _fb_pixels);
    //glDrawPixels(_vp[2], _vp[3], GL_RGB, GL_BYTE,          _fb_pixels);

    _glMatrixDel(VP_WIN);
#endif

    _north = north;

    _checkError("S52_GL_drawFBPixels() -fini-");

    return TRUE;
}

int        S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north)
{
    if (TRUE==_doPick || NULL==_fb_pixels)
        return FALSE;

    // set rotation temporatly to 0.0  (MatrixSet)
    double northtmp = _north;

    _north = north;

    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    //glClear(GL_COLOR_BUFFER_BIT);

#ifdef S52_USE_GLES2

    _glMatrixSet(VP_PRJ);

    // FIXME: clip artefac from old FB
    //_renderAC_NODATA_layer0();


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

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // turn OFF 'sampler2d'
    glUniform1f(_uBlitOn, 0.0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

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

int        S52_GL_drawStr(double x, double y, char *str, unsigned int bsize, unsigned int weight)
// draw string in world coords
{
    // optimisation - shortcut all code
    //if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
    //    return TRUE;

    S52_Color *c = S52_PL_getColor("CHBLK");
    _glColor4ub(c);

    //_glMatrixSet(VP_WIN);
    _drawTextAA(NULL, x, y, bsize, weight, str);
    //_glMatrixDel(VP_WIN);

    _checkError("S52_GL_drawStr()");

    return TRUE;
}

int        S52_GL_drawStrWin(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)
// draw a string in window coords
{
    // optimisation - shortcut all code
    //if (S52_CMD_WRD_FILTER_TX & (int) S52_MP_get(S52_CMD_WRD_FILTER))
    //    return TRUE;

    S52_Color *c = S52_PL_getColor(colorName);

    _GL_BEGIN = TRUE;

    _glColor4ub(c);

#ifdef S52_USE_GLES2
    S52_GL_win2prj(&pixels_x, &pixels_y);

    _glMatrixSet(VP_PRJ);
    _drawTextAA(NULL, pixels_x, pixels_y, bsize, 1, str);
    _glMatrixDel(VP_PRJ);

#else
    glRasterPos2d(pixels_x, pixels_y);
#endif

#ifdef S52_USE_FTGL
    if (NULL != _ftglFont[bsize]) {
#ifdef _MINGW
        ftglRenderFont(_ftglFont[bsize], str, FTGL_RENDER_ALL);
#else
        ftglRenderFont(_ftglFont[bsize], str, FTGL_RENDER_ALL);
        //_ftglFont[bsize]->Render(str);
#endif
    }
#endif


    _checkError("S52_GL_drawStrWin()");

    _GL_BEGIN = FALSE;

    return TRUE;
}

int        S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str)
{
    // FIXME: str not used yet (get font metric from a particular font system)
    (void)str;

    // scale offset
    double scalex = (_pmax.u - _pmin.u) / (double)_vp[2];
    double scaley = (_pmax.v - _pmin.v) / (double)_vp[3];

    double uoffs  = ((PICA * *offset_x) / _dotpitch_mm_x) * scalex;
    double voffs  = ((PICA * *offset_y) / _dotpitch_mm_y) * scaley;

    *offset_x = uoffs;
    *offset_y = voffs;

    return TRUE;
}

int        S52_GL_drawGraticule(void)
{
    // delta lat  / 1852 = height in NM
    // delta long / 1852 = witdh  in NM
    double dlat = (_pmax.v - _pmin.v) / 3.0;
    double dlon = (_pmax.u - _pmin.u) / 3.0;

    //int remlat =  (int) _pmin.v % (int) dlat;
    //int remlon =  (int) _pmin.u % (int) dlon;

    char   str[80];
    S52_Color *black = S52_PL_getColor("CHBLK");

    //_setBlend(TRUE);

#ifdef S52_USE_GLES2
    _glMatrixMode(GL_MODELVIEW);
    _glLoadIdentity();

    //glUniformMatrix4fv(_uProjection, 1, GL_FALSE, _pjm[_pjmTop]);
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);
    glUniform4f(_uColor, black->R/255.0, black->G/255.0, black->B/255.0, 1.0);
#else
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glLineWidth(1.0);
    glColor4ub(black->R, black->G, black->B, (4 - (black->trans - '0'))*TRNSP_FAC);
#endif

    // ----  graticule S lat
    {
        double lat  = _pmin.v + dlat;
        double lon  = _pmin.u;
        pt3v ppt[2] = {{lon, lat, 0.0}, {_pmax.u, lat, 0.0}};
        projXY uv   = {lon, lat};

        uv = S57_prj2geo(uv);
        SPRINTF(str, "%07.4f° %c", fabs(uv.v), (0.0<lat)?'N':'S');
        //_drawTextAA(lon, lat, 1, 1, str);

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
    }

    // ---- graticule N lat
    {
        double lat  = _pmin.v + dlat + dlat;
        double lon  = _pmin.u;
        pt3v ppt[2] = {{lon, lat, 0.0}, {_pmax.u, lat, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SPRINTF(str, "%07.4f° %c", fabs(uv.v), (0.0<lat)?'N':'S');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_drawTextAA(lon, lat, 1, 1, str);
    }


    // ---- graticule W long
    {
        double lat  = _pmin.v;
        double lon  = _pmin.u + dlon;
        pt3v ppt[2] = {{lon, lat, 0.0}, {lon, _pmax.v, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SPRINTF(str, "%07.4f° %c", fabs(uv.u), (0.0<lon)?'E':'W');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_drawTextAA(lon, lat, 1, 1, str);
    }

    // ---- graticule E long
    {
        double lat  = _pmin.v;
        double lon  = _pmin.u + dlon + dlon;
        pt3v ppt[2] = {{lon, lat, 0.0}, {lon, _pmax.v, 0.0}};
        projXY uv   = {lon, lat};
        uv = S57_prj2geo(uv);
        SPRINTF(str, "%07.4f° %c", fabs(uv.u), (0.0<lon)?'E':'W');

        _DrawArrays_LINE_STRIP(2, (vertex_t *)ppt);
        //_drawTextAA(lon, lat, 1, 1, str);
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

    //double orientA = 90.0 - atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;
    //double orientB = 90.0 - atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
    //double orientA = atan2(pptA[3]-pptA[0], pptA[4]-pptA[1]) * RAD_TO_DEG;
    //double orientB = atan2(pptB[3]-pptB[0], pptB[4]-pptB[1]) * RAD_TO_DEG;
    //double orientA = 90.0 - atan2(pptA[4]-pptA[1], pptA[3]-pptA[0]) * RAD_TO_DEG;
    //double orientB = 90.0 - atan2(pptB[4]-pptB[1], pptB[3]-pptB[0]) * RAD_TO_DEG;
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


/*
    // begining of curve - point of first prependical
    double x1 = pptA[3] - cos(orientA*DEG_TO_RAD)*wholinDist;
    double y1 = pptA[4] - sin(orientA*DEG_TO_RAD)*wholinDist;
    // end of first perpendicular
    double x2 = x1 + cos((orientA+90.0)*DEG_TO_RAD)*wholinDist;
    double y2 = y1 + sin((orientA+90.0)*DEG_TO_RAD)*wholinDist;

    // end of curve     - point of second perendicular
    double x3 = pptB[0] + cos(orientB*DEG_TO_RAD)*wholinDist;
    double y3 = pptB[1] + sin(orientB*DEG_TO_RAD)*wholinDist;

    // end of second perpendicular
    double x4 = x3 + cos((orientB+90.0)*DEG_TO_RAD)*wholinDist;
    double y4 = y3 + sin((orientB+90.0)*DEG_TO_RAD)*wholinDist;
*/

    if ((orientB-orientA) < 0)
        orientB += 360.0;

    double sweep = orientB - orientA;

    if (sweep > 360.0)
        sweep -= 360.0;

    if (sweep > 180.0)
        sweep -= 360.0;

    // A = 315, B = 45
    //if (orientA > orientB &&  (orientA - orientB) > 180.0) {
    //    sweep = 360.0 - orientA + orientB;
    //}


    /*
    if (sweep <   0.0) {
        CW     = FALSE;
        //sweep += 360.0;
        //sweep += (180.0 - 45.0);
        sweep = -sweep;
    } else {
        CW     = TRUE;
        //sweep  = 180.0 - sweep;
        //sweep  = (180.0 - 45.0) - sweep;
    }
    */

    // debug
    //PRINTF("SWEEP: %f\n", sweep);


    // slope
    //double mAA = (y2-y1)/(x2-x1);
    //double mBB = (y4-y3)/(x4-x3);

    //
    // find intersection of perpendicular
    //
    // compute a1, b1, c1, where line joining points 1 and 2
    // is "a1 x  +  b1 y  +  c1  =  0".
    double a1 = y2 - y1;
    double b1 = x1 - x2;
    double c1 = x2 * y1 - x1 * y2;

    //double r3 = a1 * x3 + b1 * y3 + c1;
    //double r4 = a1 * x4 + b1 * y4 + c1;
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
    double   scalex    = (_pmax.u - _pmin.u) / (double)_vp[2];
    GLdouble symlen    = 0.0;
    char     dummy     = 0.0;
    S52_PL_getLCdata(objA, &symlen, &dummy);

    //double symlen_pixl = symlen / (_dotpitch_mm_x * 100.0);
    double symlen_pixl = symlen / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
    double symlen_wrld = symlen_pixl * scalex;
    double symAngle    = atan2(symlen_wrld, dist) * RAD_TO_DEG;
    double nSym        = abs(floor(sweep / symAngle));
    double crntAngle   = orientA + 90.0;

    if (sweep > 0.0)
        crntAngle += 180;

    /*
    if (TRUE == CW) {
        if (TRUE == revsweep)
            //crntAngle = orientB - 90.0;
            crntAngle = orientB + 180.0;
        else
            //crntAngle = orientA + 90.0;
            crntAngle = orientA + 180.0;    // NW -> NE
    } else {
        if (TRUE == revsweep)
            crntAngle = orientB;
        else
            crntAngle = orientA + 180;      //  NW -> SW
    }
    */

    //if (((sweep<0.0) && (FALSE==revsweep)) ||
    //if (sweep > 180.0) {
    //    revsweep = TRUE;
    //    sweep -= 360.0;
    //}

    //if (((sweep<0.0) && (FALSE==revsweep)) ||
    //    ((orientA>orientB) && ((orientA-orientB) > 180.0))
    //   )
    //{
    //    crntAngle += 180.0;
    //}

    /*
    if ( ((sweep<0.0) && (TRUE==revsweep)) || (orientA > orientB))
    {
        crntAngle += 180.0;
    }
    */
    // fine tuning: put back the symbol apparent angle
    /*
    if (TRUE == CW) {
        if (TRUE == revsweep)
            crntAngle -= (symAngle / 4.0);
        else
            crntAngle -= (symAngle / 2.0);
    } else {
        if (TRUE == revsweep)
            crntAngle -= (symAngle / 4.0);
        else
            crntAngle -= (symAngle / 2.0);
    }
    */


    crntAngle += (symAngle / 2.0);

    //
    // draw
    //
    //_setBlend(TRUE);

    S52_DListData *DListData = S52_PL_getDListData(objA);
    if ((NULL==DListData) || (FALSE==_VBOvalidate(DListData)))
        return FALSE;

/*
    S52_DListData *DListData = S52_PL_getDListData(objA);
    if (NULL == DListData)
        return FALSE;
    if (FALSE == glIsBuffer(DListData->vboIds[0])) {
        guint i = 0;
        for (i=0; i<DListData->nbr; ++i) {
            DListData->vboIds[i] = _VBOCreate(DListData->prim[i]);
        }
        // return to normal mode
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
*/

    S52_Color *color = DListData->colors;
    _glColor4ub(color);


    // draw arc
    if (2.0==S52_MP_get(S52_MAR_DISP_WHOLIN) || 3.0==S52_MP_get(S52_MAR_DISP_WHOLIN)) {
        int j = 0;
        //nSym  /= 2;
        for (j=0; j<=nSym; ++j) {
#ifdef S52_USE_GLES2
            _glMatrixMode(GL_MODELVIEW);
            _glLoadIdentity();

            _glTranslated(xx, yy, 0.0);

            _glRotated(90.0-crntAngle, 0.0, 0.0, 1.0);
            // FIXME: radius too long (maybe because of symb width!)
            //_glTranslated(dist, 0.0, 0.0);
            //_glTranslated(dist - (1*scalex), 0.0, 0.0);
            _glTranslated(dist - (0.5*scalex), 0.0, 0.0);
            _glScaled(1.0, -1.0, 1.0);
            _glRotated(-90.0, 0.0, 0.0, 1.0);    // why -90.0 and not +90.0 .. because of inverted axis (glScale!)
                                                // or atan2() !!

#else
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glTranslated(xx, yy, 0.0);

            glRotated(90.0-crntAngle, 0.0, 0.0, 1.0);
            // FIXME: radius too long (maybe because of symb width!)
            //_glTranslated(dist, 0.0, 0.0);
            //_glTranslated(dist - (1*scalex), 0.0, 0.0);
            glTranslated(dist - (0.5*scalex), 0.0, 0.0);
            glScaled(1.0, -1.0, 1.0);
            glRotated(-90.0, 0.0, 0.0, 1.0);    // why -90.0 and not +90.0 .. because of inverted axis (glScale!)
                                                // or atan2() !!
#endif
            _pushScaletoPixel(TRUE);

            _glCallList(DListData);

            _popScaletoPixel();

            // rotate
            /*
            if (TRUE == CW) {
                if (TRUE == revsweep)
                    crntAngle -= symAngle;
                else
                    crntAngle += symAngle;
            } else {
                if (TRUE == revsweep)
                    crntAngle += symAngle;
                else
                    crntAngle -= symAngle;
            }
            */

            /*
            if (TRUE == CW)
                crntAngle += symAngle;
            else
                crntAngle -= symAngle;
            */
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
            //case S52_CMD_SIM_LN: _renderLS(objA);      break;   // LS

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
#endif


#if 0
// Summary of functions calls to try isolated dependecys

/*
S52_GL_init
    _initGLU(void)
    _initGLC(void)
    _initFTGL(void)
    _initCOGL(void)
    _initA3D(void)

    // GLES2
    _init_freetype_gl(void)
    S52_GL_init_GLES2(void)
        _loadShader(GLenum type, const char *shaderSrc)
    _1024bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
    _32bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)

S52_GL_getPRJView(double *LLv, double *LLu, double *URv, double *URu)
S52_GL_setPRJView(double  s, double  w, double  n, double  e)
S52_GL_setView(double centerLat, double centerLon, double rangeNM, double north)
S52_GL_setViewPort(int x, int y, int width, int height)
S52_GL_getViewPort(int *x, int *y, int *width, int *height)
S52_GL_setDotPitch(int w, int h, int wmm, int hmm)
S52_GL_setFontDL(int fontDL)

// --- CULL ----
S52_GL_isSupp(S52_obj *obj)
S52_GL_isOFFscreen(S52_obj *obj)


// --- DRAW ----
S52_GL_begin(int cursorPick, int drawLast)
    _contextValid(void)
    _doProjection(double centerLat, double centerLon, double rangeDeg)
    _createSymb(void)
        _glMatrixSet(VP_WIN);
        _glMatrixDel(VP_WIN);
        S52_PL_traverse(S52_SMB_PATT, _buildPatternDL);
            _buildPatternDL(gpointer key, gpointer value, gpointer data)
                _parseHPGL(S52_vec *vecObj, S57_prim *vertex)
                    _f2d(GArray *tessWorkBuf_d, unsigned int npt, vertex_t *ppt)
        S52_PL_traverse(S52_SMB_LINE, _buildSymbDL);
            _buildSymbDL(gpointer key, gpointer value, gpointer data)
                _parseHPGL(S52_vec *vecObj, S57_prim *vertex)
                    _f2d(GArray *tessWorkBuf_d, unsigned int npt, vertex_t *ppt)
        S52_PL_traverse(S52_SMB_SYMB, _buildSymbDL);
            _buildSymbDL(gpointer key, gpointer value, gpointer data)
                _parseHPGL(S52_vec *vecObj, S57_prim *vertex)
                    _f2d(GArray *tessWorkBuf_d, unsigned int npt, vertex_t *ppt)
    _renderAC_NODATA_layer0(void)
    _renderAP_NODATA_layer0(void)
    S52_GL_readFBPixels();
    S52_GL_drawFBPixels();
    S52_GL_drawBlit(double scale_x, double scale_y, double scale_z, double north)


S52_GL_draw(S52_obj *obj, gpointer user_data)
    _renderSY(S52_obj *obj)
        _renderSY_POINT_T(S52_obj *obj, double x, double y, double rotation)
        _renderSY_silhoutte(S52_obj *obj)
        _renderSY_CSYMB(S52_obj *obj)
        _renderSY_ownshp(S52_obj *obj)
        _renderSY_vessel(S52_obj *obj)
        _renderSY_pastrk(S52_obj *obj)
        _renderSY_leglin(S52_obj *obj)
    _renderLS(S52_obj *obj)
        _renderLS_LIGHTS05(S52_obj *obj)
        _renderLS_ownshp(S52_obj *obj)
        _renderLS_vessel(S52_obj *obj)
        _renderLS_afterglow(S52_obj *obj)
        _glColor4ub(S52_Color *c)
        _glLineStipple(GLint  factor,  GLushort  pattern)
        _glPointSize(GLfloat size)
        _DrawArrays_LINE_STRIP(guint npt, vertex_t *ppt)
        _DrawArrays_POINTS(guint npt, vertex_t *ppt)
        _d2f(GArray *tessWorkBuf_f, unsigned int npt, double *ppt)
    _renderLC(S52_obj *obj)
    _renderAC(S52_obj *obj)
        _renderAC_LIGHTS05(S52_obj *obj)
        _renderAC_VRMEBL01(S52_obj *obj)
        _fillarea(S57_geo *geoData)
    _renderAP(S52_obj *obj)
        _renderAP_DRGARE(S52_obj *obj)
        _renderAP_NODATA(S52_obj *obj)
    _traceCS(S52_obj *obj)
    _traceOP(S52_obj *obj)
    S52_GL_drawArc(S52_obj *objA, S52_obj *objB)
        _drawArc(S52_obj *objA, S52_obj *objB);
    S52_GL_drawLIGHTS(S52_obj *obj)
    S52_GL_drawGraticule(void)
    S52_GL_getStrOffset(double *offset_x, double *offset_y, const char *str)
    S52_GL_drawStr(double x, double y, char *str, unsigned int bsize, unsigned int weight)
    S52_GL_drawStrWin(double pixels_x, double pixels_y, const char *colorName, unsigned int bsize, const char *str)

S52_GL_drawText(S52_obj *obj, gpointer user_data)
    _drawText(S52_obj *obj)
        _drawTextAA(S52_obj *obj, double x, double y, unsigned int bsize, unsigned int weight, const char *str)
            _fillFtglBuf(texture_font_t *font, GArray *buf, const char *str)
            _sendFtglBuf(GArray *buf)

S52_GL_end(int drawLast)

S52_GL_done(void)
    _freeGLU(void)


_isPtInside(int npt, pt3 *v, double x, double y, int close)
_findCentInside(unsigned int npt, pt3 *v)
_computeArea(int npt, pt3 *v)
_getCentroid(int npt, double *ppt, _pt2 *pt)
_getMaxEdge(pt3 *p)
_edgeCin(GLboolean flag)
_glBegin(GLenum data, S57_prim *prim)
_beginCen(GLenum data)
_beginCin(GLenum data)
_glEnd(S57_prim *prim)
_endCen(void)
_endCin(void)
_vertexCen(GLvoid *data)
_vertexCin(GLvoid *data)
_dumpATImemInfo(GLenum glenum)


// --- bypass the need for libGLU.so, mandatory for GLES ---
// move to S52GLES2utils.h or S52GLES2Math.h or S52Math.c

_combineCallback(GLdouble   coords[3],
_combineCallbackCen(void)
_vertex3d(GLvoid *data, S57_prim *prim)
_vertex3f(GLvoid *data, S57_prim *prim)
_tessError(GLenum err)
_tessd(GLUtriangulatorObj *tobj, S57_geo *geoData)
_quadricError(GLenum err)
_gluNewQuadric(void)
_gluQuadricCallback(_GLUquadricObj* qobj, GLenum which, f fn)
_gluDeleteQuadric(_GLUquadricObj* qobj)
_gluPartialDisk(_GLUquadricObj* qobj,
_gluDisk(_GLUquadricObj* qobj, GLfloat innerRadius,
_gluQuadricDrawStyle(_GLUquadricObj* qobj, GLint style)

// matrix stuff
// FIXME: use OpenGL Mathematics (GLM)

S52_GL_win2prj(double *x, double *y)
    _win2prj(double *x, double *y)
        _gluUnProject(GLfloat winx, GLfloat winy, GLfloat winz,

S52_GL_prj2win(double *x, double *y)
    _prj2win(projXY p)
        _gluProject

_make_z_rot_matrix(GLfloat angle, GLfloat *m)
_make_scale_matrix(GLfloat xs, GLfloat ys, GLfloat zs, GLfloat *m)
_mul_matrix(GLfloat *prod, const GLfloat *a, const GLfloat *b)
_multiply(GLfloat *m, GLfloat *n)
_glMatrixSet(VP vpcoord)
_glMatrixDel(VP vpcoord)
_glMatrixDump(GLenum matrix)
_glMatrixMode(GLenum  mode)
_glPushMatrix(void)
_glPopMatrix(void)
_glTranslated(double x, double y, double z)
_glScaled(double x, double y, double z)
_glRotated(double angle, double x, double y, double z)
_glLoadIdentity(void)
_glOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
__gluMultMatrixVecf(const GLfloat matrix[16], const GLfloat in[4], GLfloat out[4])
__gluInvertMatrixf(const GLfloat m[16], GLfloat invOut[16])
__gluMultMatricesf(const GLfloat a[16], const GLfloat b[16], GLfloat r[16])
_pushScaletoPixel(int scaleSym)
_popScaletoPixel(void)


// --- bypass the need for libGLU.so, mandatory for GLES ---

_DrawArrays_QUADS(guint npt, vertex_t *ppt)
_DrawArrays_TRIANGLE_FAN(guint npt, vertex_t *ppt)
_DrawArrays_LINES(guint npt, vertex_t *ppt)
_VBODrawArrays(S57_prim *prim)
_VBOCreate(S57_prim *prim)
_VBODraw(S57_prim *prim)
_VBOvalidate(S52_DListData *DListData)
_VBOLoadBuffer(S57_prim *prim)
_DrawArrays(S57_prim *prim)
_createDList(S57_prim *prim)
_setBlend(int blend)
_glCallList(S52_DListData *DListData)
_parseTEX(S52_obj *obj)
_computeCentroid(S57_geo *geoData)
_getVesselVector(S52_obj *obj, double *course, double *speed)
_computeSCAMIN(void)


_computeOutCode(double x, double y)
_clipToView(double *x1, double *y1, double *x2, double *y2)


_stopTimer(int damage)

S52_GL_movePoint(double *x, double *y, double angle, double dist_m)
S52_GL_del(S52_obj *obj)
S52_GL_resetVBOID(void)
S52_GL_getNameObjPick(void)
S52_GL_setOWNSHP(S52_obj *obj, double heading)
S52_GL_readFBPixels(void)
S52_GL_dumpS57IDPixels(const char *toFilename, S52_obj *obj, unsigned int width, unsigned int height)



*/
#endif
