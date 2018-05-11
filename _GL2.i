// _GL2.i: definition & declaration for GL2.x, GLES2.x.
//         Link to libGL.so or libGLESv2.so.
//
// SD 2014MAY20
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2018 Sylvain Duclos sduclos@users.sourceforge.net

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


//
// Note: if this code is included it mean that S52_USE_GL2 is allready defined
//       to get GLES2 specific code (ex GLSL ES) define S52_USE_GLES2 also.
// Note: GL2 matrix stuff work with GL_DOUBLE normaly while GLES2 work only with GL_FLOAT

#include <strings.h>  // bzero()

// Note: GLES2 is a subset of GL2, so declaration in GLES2 header cover all GL2 decl use in the code
#ifdef S52_USE_GLSC2
#include <GLSC2/glsc2.h>
#include <GLSC2/glsc2ext.h>  //
#else  // S52_USE_GLSC2
#ifdef S52_USE_GL2
#include <GLES2/gl2.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>  // glReadnPixelsEXT / KHR, glTexStorage2DEXT
#endif  // S52_USE_GL2
#endif  // S52_USE_GLSC2

typedef double GLdouble;

#include "tesselator.h"  // will pull also: typedef void GLvoid;
typedef GLUtesselator GLUtesselatorObj;
typedef GLUtesselator GLUtriangulatorObj;

// hold copy of FrameBuffer
// Note: this is of no use in GL2 as tex stay in GPU
// only used when FB need to be dump to disk
static guint          _fb_pixels_id  = 0;     // texture ID
static unsigned char *_fb_pixels     = NULL;
static guint          _fb_pixels_sz  = 0;
static int            _fb_pixels_udp = TRUE;  // TRUE flag that the FB changed
#define _RGB           3
#define _RGBA          4
#ifdef S52_USE_ADRENO
static int            _fb_pixels_format = _RGB;   // alpha blending done in shader
#else
static int            _fb_pixels_format = _RGBA;
//static int            _fb_pixels_format = _RGB ;  // Note: on TEGRA2 RGB (3) very slow
#endif


////////////////////////////////////////////////////////
// forward decl
static double      _getWorldGridRef(S52_obj *, double *, double *, double *, double *, double *, double *);
static int         _fillArea(S57_geo *);
static void        _glMatrixMode(guint);
static void        _glLoadIdentity(int);
static void        _glUniformMatrix4fv_uModelview(void);
static int         _glMatrixSet(VP);
static int         _glMatrixDel(VP);
static int         _pushScaletoPixel(int);
static int         _popScaletoPixel(void);
static GLubyte     _setFragAttrib(const S52_Color *, gboolean);
static void        _glLineWidth(GLfloat);
static void        _glPointSize(GLfloat);
static inline void _checkError(const char *);
static GLvoid      _DrawArrays_LINE_STRIP(guint, vertex_t *);
static GLvoid      _DrawArrays_LINES(guint npt, vertex_t *ppt);
static inline void _glGenBuffers(GLuint *);
static int         _VBOCreate(S57_prim *);
static int         _glCallList(S52_DListData *, gboolean);

// debug - i965
//static int         _VBOCreate2_glCallList(S52_DListData *);
////////////////////////////////////////////////////////


// GL2 GPU Extension
//   GL_OES_texture_npot:
//   The npot extension for GLES2 is only about support of mipmaps and repeat/mirror wrap modes.
//   If you don't care about mipmaps and use only the clamp wrap mode, you can use npot textures.
//   It's part of the GLES2 spec.
static int _GL_OES_texture_npot = FALSE;
static int _GL_EXT_debug_marker = FALSE;
static int _GL_OES_point_sprite = FALSE;
static int _GL_EXT_robustness   = FALSE;
static int _GL_KHR_no_error     = FALSE;

// used to convert float to double for tesselator
static GArray *_tessWorkBuf_d = NULL;
// used to convert geo double to VBO float
static GArray *_tessWorkBuf_f = NULL;

// glsl main
static GLuint _programObject = 0;

// glsl uniform
static GLint _uProjection = 0;
static GLint _uModelview  = 0;
static GLint _uColor      = 0;
static GLint _uPointSize  = 0;
static GLint _uSampler2d  = 0;
//static GLint _uSampler2d1 = 0;
static GLint _uBlitOn     = 0;
static GLint _uTextOn     = 0;  // textured line and text from freetype-gl
static GLint _uGlowOn     = 0;
static GLint _uStipOn     = 0;
//static GLint _uScaleOn    = 0;

// test - optimisation
//static GLint _uPalOn      = 0;
//static GLint _uPalArray   = 0;

static GLint _uPattOn     = 0;
static GLint _uPattGridX  = 0;
static GLint _uPattGridY  = 0;
static GLint _uPattW      = 0;
static GLint _uPattH      = 0;

// glsl varying
static GLint _aPosition   = -2;
static GLint _aUV         = -2;
static GLint _aAlpha      = -2;

// alpha is 0.0 - 1.0
#define TRNSP_FAC_GLES2   0.25


//---- PATTERN GL2 / GLES2 -----------------------------------------------------------
//
// Note: 4 mask are drawn to fill the square made of 2 triangles (fan)
// Note: MSB 0x01, LSB 0xE0 - so it left most pixels is at 0x01 0x00
// and the right most pixel in a byte is at 0xE0
// 1 bit in _nodata_mask is 4 bytes (RGBA) in _rgba_nodata_mask (s0 x 8 bits x 4 )

/* FIXME:
+static cairo_always_inline cairo_bool_t
+_cairo_is_little_endian (void)
+{
+    static const int i = 1;
+    return *((char *) &i) == 0x01;
+}
+
*/

// NODATA03
static GLuint        _nodata_mask_texID = 0;
static GLubyte       _nodata_mask_rgba[4*32*8*4]; // 32 * 32 * 4   // 8bits * 4col * 32row * 4rgba
static const GLubyte _nodata_mask_bits[4*32] = {
// Note: indianness
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

// Line Style DOTT PAttern
// dotpitch 0.3:  dott = 2 * 0.3,  space = 4 * 0.3  (6  px wide, 32 /  6 = 5.333)
// 1100 0000 1100 0000 ...
// dotpitch 0.15: dott = 4 * 0.15, space = 8 * 0.3  (12 px wide, 32 / 12 = 2.666)
// llll 0000 0000 1111 0000 0000 1111 0000
static GLuint        _dottpa_mask_texID = 0;
static GLubyte       _dottpa_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
static const GLubyte _dottpa_mask_bits[4] = {     // 4 x 8 bits = 32 bits
// Note: indianness
    //0x03, 0x03, 0x03, 0x03
  0x30, 0x30, 0x30, 0x30
};

// Line Style DASH pattern
static GLuint        _dashpa_mask_texID = 0;
static GLubyte       _dashpa_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
static const GLubyte _dashpa_mask_bits[4] = {     // 4 x 8 bits = 32 bits
// Note: indianness
    0xFF, 0xF0, 0xFF, 0xF0
    //0xFF, 0xFF, 0xFF, 0xEF  // --> right most pixel is OFF
};

// OVERSC01
// vertical line at each 400 units (4 mm)
// 0.15mm dotpitch then 27 pixels == 4mm  (26.666..)
// 0.30mm dotpitch then 13 pixels == 4mm  (13.333..)
//static GLuint        _oversc_mask_texID = 0;
//static GLubyte       _oversc_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
//static const GLubyte _oversc_mask_bits[16] = {  // 4 x 8 bits = 32 bits
//static const GLubyte _oversc_mask_bits[4] = {     // 4 x 8 bits = 32 bits
//    0x01, 0x00, 0x00, 0x00
//};

// other pattern are created using FBO
static GLuint        _fboID = 0;
//---- PATTERN GL2 / GLES2 -----------------------------------------------------------


//static int _debugMatrix = 1;
#define   MATRIX_STACK_MAX 8

// symbole not in GLES2 - but defined here to mimic GL2
#define   GL_MODELVIEW    0x1700
#define   GL_PROJECTION   0x1701

// QuadricDrawStyle
#define   GLU_POINT                          100010
#define   GLU_LINE                           100011
#define   GLU_FILL                           100012
#define   GLU_SILHOUETTE                     100013
// Boolean
#define   GLU_TRUE                           1
#define   GLU_FALSE                          0

// FIXME: current matrix dependency in GL2 code path need encapsulation - GL2ctx.crntMat
// capture GL2 ctx state in struct
//static    GLenum   _mode = GL_MODELVIEW;  // GL_MODELVIEW (initial) or GL_PROJECTION
//static    GLenum   _mode = GL_PROJECTION;  // GL_MODELVIEW (initial) or GL_PROJECTION
static    GLfloat  _mvm[MATRIX_STACK_MAX][16];       // modelview matrix
static    GLfloat  _pjm[MATRIX_STACK_MAX][16];       // projection matrix
static    GLfloat *_crntMat;          // point to active matrix
static    int      _mvmTop = 0;       // point to stack top
static    int      _pjmTop = 0;       // point to stack top
//static    int      _mvmTop = -1;       // point to stack top
//static    int      _pjmTop = -1;       // point to stack top

#include <wchar.h>
#include "vector.h"
#include "texture-atlas.h"
#include "texture-font.h"

static texture_atlas_t *_freetype_gl_atlas              = NULL;
static texture_font_t  *_freetype_gl_font[S52_MAX_FONT] = {0};
static const char      *_freetype_gl_fontfilename       =
#ifdef S52_USE_ANDROID
    //"/system/fonts/Roboto-Regular.ttf"   // not official, could change place
    "/system/fonts/DroidSans.ttf"
#else
    "./Roboto-Regular.ttf"
    //"./Waree.ttf"          // Thai glyph
    //"../../ttf/13947.ttf"  // Russian glyph
#endif
    ;  // editor efte get confuse if ';' at the end of the string

typedef struct {
    GLfloat x, y, z;    // position
    GLfloat s, t;       // texture
} _freetype_gl_vertex_t;

// legend has no S52_obj, so this a place holder
static GLuint  _freetype_gl_textureID = 0;
static GArray *_freetype_gl_buffer    = NULL;

#define LF  '\r'   // Line Feed
#define TB  '\t'   // Tabulation
#define NL  '\n'   // New Line


static int       _f2d(GArray *tessWorkBuf_d, guint npt, vertex_t *ppt)
// convert array of float (vertex_t with GLES2) to array of double
// as the tesselator work on double for OGR witch has coord. in double
{
    g_array_set_size(tessWorkBuf_d, 0);

    for (guint i=0; i<npt; i++) {
        double d[3] = {ppt[0], ppt[1], 0.0};  // flush S57_OVERLAP_GEO_Z
        g_array_append_val(tessWorkBuf_d, d);

        ppt += 3;
    }
    return TRUE;
}

static int       _d2f(GArray *tessWorkBuf_f, unsigned int npt, double *ppt)
// convert array of double to array of float, geo to VBO
{
    g_array_set_size(tessWorkBuf_f, 0);

    for (guint i=0; i<npt; ++i) {
        float f[3] = {ppt[0], ppt[1], 0.0};  // flush S57_OVERLAP_GEO_Z
        g_array_append_val(tessWorkBuf_f, f);
        ppt += 3;
    }
    return TRUE;
}

static int       _init_freetype_gl(void)
{
    const wchar_t   *cache    = L" !\"#$%&'()*+,-./0123456789:;<=>?"
                                L"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                                L"`abcdefghijklmnopqrstuvwxyz{|}~"
                                L"èàé";

    _checkError("_init_freetype_gl() -beg-");

    if (NULL == _freetype_gl_atlas) {
#ifdef S52_USE_GLSC2
        _freetype_gl_atlas = texture_atlas_new(512, 512, 3);    // GL_RGB in GLSC2 - code change in GLSL
#else
        _freetype_gl_atlas = texture_atlas_new(512, 512, 1);    // alpha only  - not in GLSC2
#endif
    } else {
        PRINTF("WARNING: _init_freetype_gl() allready initialize\n");
        g_assert(0);
        return FALSE;
    }

    _checkError("_init_freetype_gl() -0-");

    // FIXME: overkill!
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

    valueBuf TTFPath = {'\0'};
    if (FALSE == S52_utils_getConfig(CFG_TTF, TTFPath)) {
        PRINTF("NOTE: loading hard-coded TTF filename: %s\n", _freetype_gl_fontfilename);
    } else {
        if (TRUE == g_file_test(TTFPath, G_FILE_TEST_EXISTS)) {
            _freetype_gl_fontfilename = TTFPath;
            PRINTF("NOTE: loading TTF found in s52.cfg (%s)\n", TTFPath);
        } else {
            //FIXME: test this path
            PRINTF("WARNING: no TTF found (no text)\n", TTFPath);
            return FALSE;
        }
    }

    // 10 points default
    int basePtSz = 10 * (PICA / S52_MP_get(S52_MAR_DOTPITCH_MM_Y));

    //PRINTF("DEBUG: basePtSz = %i, dotp mm=%f\n", basePtSz, _dotpitch_mm_y);

    _freetype_gl_font[0] = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, basePtSz +  0);
    _freetype_gl_font[1] = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, basePtSz +  6);
    _freetype_gl_font[2] = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, basePtSz + 12);
    _freetype_gl_font[3] = texture_font_new(_freetype_gl_atlas, _freetype_gl_fontfilename, basePtSz + 18);

    if (NULL == _freetype_gl_font[0]) {
        PRINTF("WARNING: texture_font_new() failed\n");
        g_assert(0);
        return FALSE;
    }


    texture_font_load_glyphs(_freetype_gl_font[0], cache);
    _checkError("_init_freetype_gl() -1-");
    texture_font_load_glyphs(_freetype_gl_font[1], cache);
    texture_font_load_glyphs(_freetype_gl_font[2], cache);
    texture_font_load_glyphs(_freetype_gl_font[3], cache);

    _checkError("_init_freetype_gl() -2-");

    _glGenBuffers(&_freetype_gl_textureID);

    if (NULL == _freetype_gl_buffer) {
        _freetype_gl_buffer = g_array_new(FALSE, FALSE, sizeof(_freetype_gl_vertex_t));
    }

    return TRUE;
}

//static GArray   *_fill_freetype_gl_buffer(GArray *ftglBuf, const char *str, unsigned int weight, double *strWpx, double *strHpx)
static GArray   *_fill_freetype_gl_buffer(GArray *ftglBuf, const char *str, unsigned int bsize, double *strWpx, double *strHpx)
// fill buffer with triangles strip, W/H can be NULL
// experimental: smaller text size if second line
{
    int   pen_x = 0;
    int   pen_y = 0;
    int   nl    = FALSE;
    glong len   = g_utf8_strlen(str, -1);

    if (NULL!=strWpx && NULL!=strHpx) {
        *strWpx = 0.0;
        *strHpx = 0.0;
    }

    g_array_set_size(ftglBuf, 0);

    for (glong i=0; i<len; ++i) {
        gchar           *utfc  = g_utf8_offset_to_pointer(str, i);
        gunichar         unic  = g_utf8_get_char(utfc);
        //texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], unic);
        texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[bsize], unic);
        if (NULL == glyph) {
            PRINTF("DEBUG: NULL glyph %lc: \n", unic);
            continue;
        }

        // experimental: smaller text size if second line
        if (NL == unic) {
            //weight = (0<weight) ? weight-1 : weight;
            //texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], 'A');
            bsize = (0<bsize) ? bsize-1 : bsize;
            texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[bsize], 'A');
            pen_x =  0;
            pen_y = (NULL!=glyph) ? -(glyph->height+5) : 10 ;
            nl    = TRUE;

            continue;
        }

        // experimental: augmente kerning if second line
        if (TRUE == nl) {
            pen_x += texture_glyph_get_kerning(glyph, unic);
            pen_x++;
        }

        GLfloat x0 = pen_x + glyph->offset_x;
        GLfloat y0 = pen_y + glyph->offset_y;

        GLfloat x1 = x0    + glyph->width;
        GLfloat y1 = y0    - glyph->height;    // Y is down, so flip glyph
        //GLfloat y1 = y0    - (glyph->height+1);  // Y is down, so flip glyph
                                                 // +1 check this, some device clip the top row
        GLfloat s0 = glyph->s0;
        GLfloat t0 = glyph->t0;
        GLfloat s1 = glyph->s1;
        GLfloat t1 = glyph->t1;

        // debug
        //PRINTF("CHAR: x0,y0,x1,y1: %lc: %f %f %f %f\n", str[i],x0,y0,x1,y1);
        //PRINTF("CHAR: s0,t0,s1,t1: %lc: %f %f %f %f\n", str[i],s0,t0,s1,t1);

        GLfloat z0 = 0.0;
        _freetype_gl_vertex_t vertices[6] = {
            {x0,y0,z0,  s0,t0},
            {x0,y1,z0,  s0,t1},
            {x1,y1,z0,  s1,t1},

            {x0,y0,z0,  s0,t0},
            {x1,y1,z0,  s1,t1},
            {x1,y0,z0,  s1,t0}
        };

        ftglBuf = g_array_append_vals(ftglBuf, &vertices[0], 3);
        ftglBuf = g_array_append_vals(ftglBuf, &vertices[3], 3);

        pen_x += glyph->advance_x;
        pen_y += glyph->advance_y;

        // tally whole string size (what with NL)
        if (NULL!=strWpx && NULL!=strHpx) {
            *strWpx += glyph->width;
            *strHpx  = (*strHpx>glyph->height)? *strHpx : glyph->height;
        }
    }

    //PRINTF("DEBUG: h/w px: %f %f\n", h_px, w_px);

    return ftglBuf;
}

static int       _justifyTXTPos(double strWpx, double strHpx, char hjust, char vjust, double *x, double *y)
// apply H/V justification to position X/Y
// Note: see doc at struct _Text in S52PL.c:150 for interpretation
{
    double strWw = strWpx * _scalex;
    double strHw = strHpx * _scaley;
    double wx, wy;  // width
    double hx, hy;  // height

    // horizontal
    switch(hjust) {
    case '1':  // CENTRE
        wx = (cos(_view.north*DEG_TO_RAD) * strWw) / 2.0;
        wy = (sin(_view.north*DEG_TO_RAD) * strWw) / 2.0;
        break;
    case '2':  // RIGHT
        wx = cos(_view.north*DEG_TO_RAD) * strWw;
        wy = sin(_view.north*DEG_TO_RAD) * strWw;
        break;
    case '3':  // LEFT
        wx = 0.0;
        wy = 0.0;
        break;

    default:
        PRINTF("DEBUG: invalid hjust!\n");
        wx = 0.0;
        wy = 0.0;
        g_assert(0);
    }

    // vertical
    switch(vjust) {
    case '1':  // BOTTOM
        hx = 0.0;
        hy = 0.0;
        break;
    case '2':  // CENTRE
        hx = (cos(_view.north*DEG_TO_RAD) * strHw) / 2.0;
        hy = (sin(_view.north*DEG_TO_RAD) * strHw) / 2.0;
        break;
    case '3':  // TOP
        hx = cos(_view.north*DEG_TO_RAD) * strHw;
        hy = sin(_view.north*DEG_TO_RAD) * strHw;
        break;

    default:
        PRINTF("DEBUG: invalid vjust!\n");
        hx = 0.0;
        hy = 0.0;
        g_assert(0);
    }

    //PRINTF("DEBUG: px str W/H: %f/%f  scl X/Y: %f/%f\n", strWpx, strHpx, _scalex, _scaley);
    //PRINTF("DEBUG: w  str W/H: %f/%f  pos X/Y: %f/%f\n", strWw,  strHw,  *x, *y);

    // FIXME: do the math
    *x += +hx - wx;

    *y += -hy + wy;
    //*y += hy - wy;

    return TRUE;
}



//-----------------------------------------
//
// gles2 float Matrix stuff (by hand)
//

static void      __make_z_rot_matrix(GLfloat angle, GLfloat *m)
// called only by _glRotated()
{
   float c = cos(angle * M_PI / 180.0);
   float s = sin(angle * M_PI / 180.0);

   //m[10] = m[15] = 1.0f;

   m[0]  =  c;
   m[1]  =  s;
   m[4]  = -s;
   m[5]  =  c;
   m[10] = 1.0f;
   m[15] = 1.0f;
}

static void      __make_scale_matrix(GLfloat xs, GLfloat ys, GLfloat zs, GLfloat *m)
// called only by _glScaled()
{
   // caller allready done that
    //memset(m, 0, sizeof(GLfloat) * 16);

   m[0]  = xs;
   m[5]  = ys;
   m[10] = zs;
   m[15] = 1.0f;
}

static void      __gluMultMatrixVecf(const GLfloat matrix[16], const GLfloat in[4], GLfloat out[4])
// called by _gluProject() / _gluUnProject()
{
    for (guint i=0; i<4; i++) {
        out[i] = in[0] * matrix[0*4+i] +
                 in[1] * matrix[1*4+i] +
                 in[2] * matrix[2*4+i] +
                 in[3] * matrix[3*4+i];
    }
}

static GLint     _gluProject(GLfloat objx, GLfloat objy, GLfloat objz,
                             const GLfloat modelMatrix[16],
                             const GLfloat projMatrix[16],
                             const GLint   viewport[4],
                             GLfloat* winx, GLfloat* winy, GLfloat* winz)
{
    GLfloat in [4] = {objx, objy, objz, 1.0f};
    GLfloat out[4] = {0.0f};

    __gluMultMatrixVecf(modelMatrix, in,  out);
    __gluMultMatrixVecf(projMatrix,  out, in);

    if (0.0f == in[3])
        return GL_FALSE;

    in[0] /= in[3];
    in[1] /= in[3];
    in[2] /= in[3];

    // Map x, y and z to range 0-1
    in[0] = in[0] * 0.5f + 0.5f;
    in[1] = in[1] * 0.5f + 0.5f;
    in[2] = in[2] * 0.5f + 0.5f;

    // Map x,y to viewport
    in[0] = in[0] * viewport[2] + viewport[0];
    in[1] = in[1] * viewport[3] + viewport[1];

    *winx = in[0];
    *winy = in[1];
    *winz = in[2];

    return GL_TRUE;
}

static int       __gluInvertMatrixf(const GLfloat m[16], GLfloat invOut[16])
// called only by _gluUnProject()
{
    GLfloat inv[16] = {0.0f};
    GLfloat det;

    inv[0] =   m[5]*m[10]*m[15] - m[5] *m[11]*m[14] - m[9] *m[6]*m[15]
             + m[9]*m[7] *m[14] + m[13]*m[6] *m[11] - m[13]*m[7]*m[10];
    inv[4] = - m[4]*m[10]*m[15] + m[4] *m[11]*m[14] + m[8] *m[6]*m[15]
             - m[8]*m[7] *m[14] - m[12]*m[6] *m[11] + m[12]*m[7]*m[10];
    inv[8] =   m[4]*m[9] *m[15] - m[4] *m[11]*m[13] - m[8] *m[5]*m[15]
             + m[8]*m[7] *m[13] + m[12]*m[5] *m[11] - m[12]*m[7]*m[9];
    inv[12]= - m[4]*m[9] *m[14] + m[4] *m[10]*m[13] + m[8] *m[5]*m[14]
             - m[8]*m[6] *m[13] - m[12]*m[5] *m[10] + m[12]*m[6]*m[9];
    inv[1] = - m[1]*m[10]*m[15] + m[1] *m[11]*m[14] + m[9] *m[2]*m[15]
             - m[9]*m[3] *m[14] - m[13]*m[2] *m[11] + m[13]*m[3]*m[10];
    inv[5] =   m[0]*m[10]*m[15] - m[0] *m[11]*m[14] - m[8] *m[2]*m[15]
             + m[8]*m[3] *m[14] + m[12]*m[2] *m[11] - m[12]*m[3]*m[10];
    inv[9] = - m[0]*m[9] *m[15] + m[0] *m[11]*m[13] + m[8] *m[1]*m[15]
             - m[8]*m[3] *m[13] - m[12]*m[1] *m[11] + m[12]*m[3]*m[9];
    inv[13]=   m[0]*m[9] *m[14] - m[0] *m[10]*m[13] - m[8] *m[1]*m[14]
             + m[8]*m[2] *m[13] + m[12]*m[1] *m[10] - m[12]*m[2]*m[9];
    inv[2] =   m[1]*m[6] *m[15] - m[1] *m[7] *m[14] - m[5] *m[2]*m[15]
             + m[5]*m[3] *m[14] + m[13]*m[2] *m[7]  - m[13]*m[3]*m[6];
    inv[6] = - m[0]*m[6] *m[15] + m[0] *m[7] *m[14] + m[4] *m[2]*m[15]
             - m[4]*m[3] *m[14] - m[12]*m[2] *m[7]  + m[12]*m[3]*m[6];
    inv[10]=   m[0]*m[5] *m[15] - m[0] *m[7] *m[13] - m[4] *m[1]*m[15]
             + m[4]*m[3] *m[13] + m[12]*m[1] *m[7]  - m[12]*m[3]*m[5];
    inv[14]= - m[0]*m[5] *m[14] + m[0] *m[6] *m[13] + m[4] *m[1]*m[14]
             - m[4]*m[2] *m[13] - m[12]*m[1] *m[6]  + m[12]*m[2]*m[5];
    inv[3] = - m[1]*m[6] *m[11] + m[1] *m[7] *m[10] + m[5] *m[2]*m[11]
             - m[5]*m[3] *m[10] - m[9] *m[2] *m[7]  + m[9] *m[3]*m[6];
    inv[7] =   m[0]*m[6] *m[11] - m[0] *m[7] *m[10] - m[4] *m[2]*m[11]
             + m[4]*m[3] *m[10] + m[8] *m[2] *m[7]  - m[8] *m[3]*m[6];
    inv[11]= - m[0]*m[5] *m[11] + m[0] *m[7] *m[9]  + m[4] *m[1]*m[11]
             - m[4]*m[3] *m[9]  - m[8] *m[1] *m[7]  + m[8] *m[3]*m[5];
    inv[15]=   m[0]*m[5] *m[10] - m[0] *m[6] *m[9]  - m[4] *m[1]*m[10]
             + m[4]*m[2] *m[9]  + m[8] *m[1] *m[6]  - m[8] *m[2]*m[5];

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (0.0f == det) {
        PRINTF("WARNING: det = 0, fail\n");
        return GL_FALSE;
    }

    det=1.0f/det;

    for (int i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;

    return GL_TRUE;
}

static void      __gluMultMatricesf(const GLfloat a[16], const GLfloat b[16], GLfloat r[16])
// called only by _gluUnProject()
{
    for (guint i=0; i<4; ++i) {
        for (guint j=0; j<4; ++j) {
            r[i*4+j] = a[i*4+0] * b[0*4+j] +
                       a[i*4+1] * b[1*4+j] +
                       a[i*4+2] * b[2*4+j] +
                       a[i*4+3] * b[3*4+j];
        }
    }
}

static GLint     _gluUnProject(GLfloat winx, GLfloat winy, GLfloat winz,
                               const GLfloat modelMatrix[16],
                               const GLfloat projMatrix[16],
                               const GLint   viewport[4],
                               GLfloat* objx, GLfloat* objy, GLfloat* objz)
{
    GLfloat finalMatrix[16] = {0.0f};
    GLfloat in [4];
    GLfloat out[4];

    __gluMultMatricesf(modelMatrix, projMatrix, finalMatrix);
    if (GL_FALSE == __gluInvertMatrixf(finalMatrix, finalMatrix)) {
        PRINTF("WARNING: __gluInvertMatrixf() fail\n");
        g_assert(0);
        return GL_FALSE;
    }

    in[0] = winx;
    in[1] = winy;
    in[2] = winz;
    in[3] = 1.0f;

    // Map x and y from window coordinates
    in[0] = (in[0] - viewport[0]) / viewport[2];
    in[1] = (in[1] - viewport[1]) / viewport[3];

    // Map to range -1 to 1
    in[0] = in[0] * 2 - 1;
    in[1] = in[1] * 2 - 1;
    in[2] = in[2] * 2 - 1;

    __gluMultMatrixVecf(finalMatrix, in, out);
    if (0.0f == out[3]) {
        PRINTF("WARNING: __gluMultMatrixVecf() fail [out[3]=0]\n");
        g_assert(0);
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

static void      _multiply(GLfloat *m, GLfloat *n)
{
    GLfloat tmp[16] = {0.0f};

   for (guint i = 0; i < 16; i++) {
      div_t    d      = div(i, 4);
      GLfloat *row    = n + d.quot * 4;
      GLfloat *column = m + d.rem;
      for (int j = 0; j < 4; j++)
          tmp[i] += row[j] * column[j * 4];
   }
   memcpy(m, &tmp, sizeof tmp);
}
// gles2 float Matrix stuff (by hand)
//-----------------------------------------

static void      _glTranslated(double x, double y, double z)
{
    GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  (GLfloat) x, (GLfloat) y, (GLfloat) z, 1 };
    //GLfloat t[16] = {1, (GLfloat) x, (GLfloat) y, (GLfloat) z,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};

    _multiply(_crntMat, t);

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //    _identity_MODELVIEW = FALSE;

    return;
}

static void      _glScaled(double x, double y, double z)
{
    GLfloat m[16] = {0.0f};

    __make_scale_matrix((GLfloat) x, (GLfloat) y, (GLfloat) z, m);

    _multiply(_crntMat, m);

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //    _identity_MODELVIEW = FALSE;

    return;
}

static void      _glRotated(double angle, double x, double y, double z)
// rotate on Z
{
    GLfloat m[16] = {0.0f};
    // FIXME: handle only 0.0==x 0.0==y 1.0==z
    // silence warning for now
    (void)x;
    (void)y;
    (void)z;

    __make_z_rot_matrix((GLfloat) angle, m);

    _multiply(_crntMat, m);

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //    _identity_MODELVIEW = FALSE;

    return;
}

static int       _renderTXTAA_gl2(double x, double y, GLfloat *data, guint len)
// render VBO static text (ie no data) or dynamic text
{
    // FIXME: abort if no ttf (id check _freetype_gl_atlas->id)

    // Note: static text could also come from MIO's (ie S52_GL_LAST cycle)
    if ((NULL == data) &&
        ((S52_GL_DRAW==_crnt_GL_cycle) || (S52_GL_LAST==_crnt_GL_cycle))) {
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
        glEnableVertexAttribArray(_aPosition);
        glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), BUFFER_OFFSET(0));

        glEnableVertexAttribArray(_aUV);
        glVertexAttribPointer    (_aUV,       2, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), BUFFER_OFFSET(sizeof(float)*3));
    } else {
        // connect ftgl buffer coord data to GPU
        glEnableVertexAttribArray(_aPosition);
        glVertexAttribPointer    (_aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), data+0);

        glEnableVertexAttribArray(_aUV);
        glVertexAttribPointer    (_aUV,       2, GL_FLOAT, GL_FALSE, sizeof(_freetype_gl_vertex_t), data+3);
    }

    // turn ON 'sampler2d'
    //glUniform1f(_uTextOn, 1.0);
    glUniform1i(_uTextOn, 1);

    glBindTexture(GL_TEXTURE_2D, _freetype_gl_atlas->id);

    _glLoadIdentity(GL_MODELVIEW);

    _glTranslated(x, y, 0.0);
    // FIXME: rot then scale

    _pushScaletoPixel(FALSE);

    // horizontal text
    _glRotated(-_view.north, 0.0, 0.0, 1.0);

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    glDrawArrays(GL_TRIANGLES, 0, len);

    _popScaletoPixel();

    glBindTexture(GL_TEXTURE_2D, 0);
    //glUniform1f(_uTextOn, 0.0);
    glUniform1i(_uTextOn, 0);

    // disconnect buffer
    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    _checkError("_renderTXTAA_gl2() freetype-gl");

    return TRUE;
}

typedef unsigned char u8;

#if !defined(S52_USE_ANDROID)
#ifdef S52_USE_GLSC2
typedef void (GL_APIENTRYP PFNGLREADNPIXELSKHRPROC) (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei bufSize, void *data);
static PFNGLREADNPIXELSKHRPROC            _glReadnPixels            = NULL;
//typedef void (GL_APIENTRYP PFNGLTEXTURESTORAGE2DEXTPROC) (GLuint texture, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
static PFNGLTEXSTORAGE2DEXTPROC           _glTexStorage2DEXT        = NULL;
#else  // S52_USE_GLSC2
// Note: on Intel GL_PROGRAM_BINARY_LENGTH_OES is 0 - bailout
// https://bugs.freedesktop.org/show_bug.cgi?id=87516
// FIXME: trigger reset to test this call
//static PFNGLGETGRAPHICSRESETSTATUSEXTPROC _glGetGraphicsResetStatus = NULL;

//static PFNGLREADNPIXELSEXTPROC            _glReadnPixels            = NULL;  // EXT fail with mesa-git/master(2016AUG28)
static PFNGLPROGRAMPARAMETERIEXTPROC      _glProgramParameteriEXT   = NULL;
static PFNGLGETPROGRAMBINARYOESPROC       _glGetProgramBinaryOES    = NULL;
static PFNGLPROGRAMBINARYOESPROC          _glProgramBinaryOES       = NULL;
static PFNGLTEXSTORAGE2DEXTPROC           _glTexStorage2DEXT        = NULL;
#endif  // S52_USE_GLSC2

#ifdef S52_USE_EGL
static int       _loadProcEXT()
{
    typedef void (*proc)(void);
    extern proc eglGetProcAddress(const char *procname);

#ifdef S52_USE_GLSC2
    _glGetGraphicsResetStatus = (PFNGLGETGRAPHICSRESETSTATUSEXTPROC) eglGetProcAddress("glGetGraphicsResetStatusEXT");
    PRINTF("DEBUG: eglGetProcAddress(glGetGraphicsResetStatusEXT)       %s\n",     (NULL==_glGetGraphicsResetStatus)?"FAILED":"OK");
    if (NULL == _glGetGraphicsResetStatus) {
        _glGetGraphicsResetStatus = (PFNGLGETGRAPHICSRESETSTATUSKHRPROC) eglGetProcAddress("glGetGraphicsResetStatusKHR");
        PRINTF("DEBUG: eglGetProcAddress(glGetGraphicsResetStatusKHR)       %s\n",     (NULL==_glGetGraphicsResetStatus)?"FAILED":"OK");
    }

    _glReadnPixels =       (PFNGLREADNPIXELSKHRPROC)      eglGetProcAddress("glReadnPixelsKHR");
    PRINTF("DEBUG: eglGetProcAddress(glReadnPixelsKHR)       %s\n",     (NULL==_glReadnPixelsKHR)?"FAILED":"OK");

    _glTexStorage2DEXT =   (PFNGLTEXSTORAGE2DEXTPROC)     eglGetProcAddress("glTexStorage2DEXT");
    PRINTF("DEBUG: eglGetProcAddress(glTexStorage2DEXT)      %s\n",     (NULL==_glTexStorage2DEXT)?"FAILED":"OK");
#else
    /* need gles2.h 20180316
    _glGetGraphicsResetStatus = (PFNGLGETGRAPHICSRESETSTATUSEXTPROC) eglGetProcAddress("glGetGraphicsResetStatusEXT");
    PRINTF("DEBUG: eglGetProcAddress(glGetGraphicsResetStatusEXT)       %s\n",     (NULL==_glGetGraphicsResetStatus)?"FAILED":"OK");
    if (NULL == _glGetGraphicsResetStatus) {
        _glGetGraphicsResetStatus = (PFNGLGETGRAPHICSRESETSTATUSKHRPROC) eglGetProcAddress("glGetGraphicsResetStatusKHR");
        PRINTF("DEBUG: eglGetProcAddress(glGetGraphicsResetStatusKHR)       %s\n",     (NULL==_glGetGraphicsResetStatus)?"FAILED":"OK");
    }

    _glReadnPixels =       (PFNGLREADNPIXELSEXTPROC)      eglGetProcAddress("glReadnPixels");
    PRINTF("DEBUG: eglGetProcAddress(glReadnPixels)          %s\n",     (NULL==_glReadnPixels)?"FAILED":"OK");
    if (NULL == _glReadnPixels) {
        _glReadnPixels =       (PFNGLREADNPIXELSEXTPROC)      eglGetProcAddress("glReadnPixelsEXT");
        PRINTF("DEBUG: eglGetProcAddress(glReadnPixelsEXT)       %s\n",     (NULL==_glReadnPixels)?"FAILED":"OK");
    }
    if (NULL == _glReadnPixels) {
        _glReadnPixels =       (PFNGLREADNPIXELSKHRPROC)      eglGetProcAddress("glReadnPixelsKHR");
        PRINTF("DEBUG: eglGetProcAddress(glReadnPixelsKHR)       %s\n",     (NULL==_glReadnPixels)?"FAILED":"OK");
    }
    */
    _glProgramParameteriEXT = (PFNGLPROGRAMPARAMETERIEXTPROC)eglGetProcAddress("glProgramParameteriEXT");
    PRINTF("DEBUG: eglGetProcAddress(glProgramParameteriEXT) %s\n",     (NULL==_glProgramParameteriEXT)?"FAILED":"OK");

    _glGetProgramBinaryOES =  (PFNGLGETPROGRAMBINARYOESPROC) eglGetProcAddress("glGetProgramBinaryOES");
    PRINTF("DEBUG: eglGetProcAddress(glGetProgramBinaryOES)  %s\n",     (NULL==_glGetProgramBinaryOES)?"FAILED":"OK");

    _glProgramBinaryOES =     (PFNGLPROGRAMBINARYOESPROC)    eglGetProcAddress("glProgramBinaryOES");
    PRINTF("DEBUG: eglGetProcAddress(glProgramBinaryOES)     %s\n",     (NULL==_glProgramBinaryOES)?"FAILED":"OK");

    _glTexStorage2DEXT =      (PFNGLTEXSTORAGE2DEXTPROC)     eglGetProcAddress("glTexStorage2DEXT");
    PRINTF("DEBUG: eglGetProcAddress(glTexStorage2DEXT)      %s\n",     (NULL==_glTexStorage2DEXT)?"FAILED":"OK");
#endif

    return TRUE;
}
#endif  // S52_USE_EGL

#if !defined(S52_USE_GLSC2)
static int       _saveShaderBin(GLuint programObject)
// Save a GLSL shader bin into a file
{
    //FIXME: no OES

    // get the blob
    GLint nFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &nFormats);
    if (0 != nFormats) {
        //GLint formats[nFormats];
        GLenum formats[nFormats];
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, (GLint*)formats);
    } else {
        PRINTF("DEBUG: GL_NUM_PROGRAM_BINARY_FORMATS_OES failed nFormats=%i\n", nFormats);
    }

    GLsizei bufsize = 0;
    glGetProgramiv(programObject, GL_PROGRAM_BINARY_LENGTH_OES, &bufsize);
    _checkError("_saveShaderBin() -1-");
    if (0 == bufsize) {
        PRINTF("DEBUG: GL_PROGRAM_BINARY_LENGTH_OES failed [bufsize=%i]\n", bufsize);
        //g_assert(0);
        return FALSE;
    }

    GLsizei lenOut        = 0;
    u8      binary[bufsize];
    if (NULL != _glGetProgramBinaryOES) {
        // FIXME: compare bufsize / lenOut
        //_glGetProgramBinaryOES(programObject, bufsize, &lenOut, formats, binary);
        _glGetProgramBinaryOES(programObject, bufsize, &lenOut, 0, binary);
        //glGetProgramBinaryOES(programObject, bufsize, &lenOut, formats, binary);
        _checkError("_saveShaderBin():_glGetProgramBinaryOES() -2-");
    } else {
        g_assert(0);
        return FALSE;
    }

    // save the blob
    if (0 != lenOut) {
        FILE* fp = fopen("shader.bin", "wb");
        fwrite(binary, lenOut, 1, fp);
        fclose(fp);
    } else {
        PRINTF("DEBUG: glGetProgramBinary() FAILED [bufsize=%i, lenOut=%i]\n", bufsize, lenOut);
        //g_assert(0);
        return FALSE;
    }

    return TRUE;
}
#endif  // !S52_USE_GLSC2

#ifdef S52_USE_GLSC2
static GLuint    _loadShaderBin(void)
// Load a binary GLSL shader from a file
{
    FILE* fp = g_fopen("shader.bin", "rb");
    if (NULL == fp) {
        PRINTF("WARNING: 'shader.bin' loading failed\n");
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    GLint len = (GLint)ftell(fp);
    u8 binary[len];
    fseek(fp, 0, SEEK_SET);
    fread(binary, len, 1, fp);
    fclose(fp);

    _checkError("_loadShaderBin() -0-");
    GLint nFormats = 0;
#ifdef S52_USE_GLSC2
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &nFormats);
#else
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &nFormats);
#endif
    GLint binaryFormats[nFormats];
#ifdef S52_USE_GLSC2
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, binaryFormats);
#else
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, binaryFormats);
#endif
    _checkError("_loadShaderBin() -1-");

    GLuint progId = glCreateProgram();

    // FIXME: binaryFormat seem useless - maybe binaryFormats (with 's')
    GLenum binaryFormat = 0;
#ifdef S52_USE_GLSC2
    glProgramBinary(progId, binaryFormat, (const void *)binary, len);
#else
    if (NULL != _glProgramBinaryOES) {
        _glProgramBinaryOES(progId, binaryFormat, (const void *)binary, len);
        //_glProgramBinaryOES(progId, NULL, (const void *)binary, len);
    } else {
        return 0;
    }
#endif
    _checkError("_loadShaderBin() -2-");

    PRINTF("DEBUG: after loading 'shader.bin': binFormat:%i, binary:%s\n", binaryFormat, binary);

    GLint success = 0;
    glGetProgramiv(progId, GL_LINK_STATUS, &success);
    if (!success) {
        PRINTF("WARNING: 'shader.bin' linking failed\n");
        return 0;
    }
    _checkError("_loadShaderBin() -end-");

    return progId;
}
#endif  // !S52_USE_ANDROID
#endif  // S52_USE_GLSC2

#if !defined(S52_USE_GLSC2)
static GLuint    _loadShaderSrc(GLenum type, const char *shaderSrc)
{
    GLint compiled = GL_FALSE;

    GLuint shader = glCreateShader(type);
    if (0 == shader) {
        PRINTF("ERROR: glCreateShader() failed\n");
        _checkError("_loadShader()");
        g_assert(0);
        return FALSE;
    }

    glShaderSource(shader, 1, &shaderSrc, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (GL_FALSE == compiled) {
        int logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);  // length include '\0'

        PRINTF("ERROR: glCompileShader() fail\n");

        if (0 != logLen) {
            int  writeOut = 0;
            char log[logLen];
            glGetShaderInfoLog(shader, logLen, &writeOut, log);
            _checkError("_loadShader()");
            PRINTF("DEBUG: glCompileShader() log: %s\n", log);
        }
        g_assert(0);
        return FALSE;
    }

    return shader;
}

static GLuint    _compShaderSrc(GLuint programObject)
// compile vertex and fragment shader source
{
    PRINTF("DEBUG: building '_programObject'\n");
    programObject = glCreateProgram();
    if (0 == programObject) {
        PRINTF("ERROR: glCreateProgram() FAILED\n");
        g_assert(0);
        return 0;
    }

    _checkError("_compShaderSrc() -00-");

    // ----------------------------------------------------------------------
    PRINTF("DEBUG: GL_VERTEX_SHADER\n");

    #define STRINGIFY(STR) #STR

    // Note: if no #version directive will default to 1.10 (bad)
    // Note: no "es" in V1.00
    static const char vertSrc[] =
        "#version 100\n"
        //"#version 130\n"  // not supported
        //"#version 300 es\n"

        STRINGIFY(

        //precision lowp float;
        precision mediump float;
        //precision highp   float;

        precision mediump int;

        uniform mat4    uProjection;
        uniform mat4    uModelview;
        uniform float   uPointSize;

        uniform int     uPattOn;
        uniform float   uPattGridX;
        uniform float   uPattGridY;
        uniform float   uPattW;
        uniform float   uPattH;

        uniform float   uStipOn;
        //uniform float   uScaleOn;

        // optimisation test - color LUP
        uniform int     uPalOn;
        //uniform vec4    uPalArray[63];  // 63 - S52_COL_NUM

        attribute vec4  aPosition;
        attribute float aAlpha;
        attribute vec2  aUV;

        varying vec2    v_texCoord;
        varying float   v_alpha;
        varying vec4    v_color;
        varying float   v_dist;

        void main(void)
        {
            vec4     pos = aPosition;
            v_alpha      = aAlpha;
            gl_PointSize = uPointSize;

            if (1 == uPattOn) {
                //v_texCoord.x = (aPosition.x - uPattGridX) / uPattW;
                //v_texCoord.y = (aPosition.y - uPattGridY) / uPattH;
                v_texCoord.x = (uPattGridX - aPosition.x) / uPattW;
                v_texCoord.y = (uPattGridY - aPosition.y) / uPattH;
            }

            /*
            else if (0 <= uPalOn) {
                // optimisation test -> uPalOn = aPosition.z;
                //v_color = uPalArray[uPalOn];

                //v_texCoord = mod(gl_Position.xy, vec2(32.0, 0.0));
                //v_texCoord = mod(gl_Position.xy, vec2(32.0, 1.0));
                //v_texCoord.x = mod(gl_Position.xy, vec2(32.0, 1.0)).x;
                //v_texCoord.y = mod(gl_Position.xy, vec2(1.0, 32.0)).y;
                //v_texCoord = mod(gl_Position.xy, vec2(32.0, 1.0)) * aUV;
                //v_texCoord = mod(gl_Position.xy, vec2(32.0, 1.0)) + aUV;
                //v_texCoord = mod(gl_Position.xy, vec2(32.0, 32.0));
                //v_texCoord = (uProjection * uModelview * vec4(aUV,1.0,1.0) * vec4(320000.0/2.0,320000.0/2.0,1.0,1.0)).xy;
                //v_texCoord = vec2(mod(length(aUV)*(10.0), 32.0), 0.0);
                ;
            }
            */

            else if (0.0 < uStipOn) {
                v_dist     = pos.z;
                pos.z      = 0.0;
                v_texCoord = aUV;
            } else {
                v_texCoord = aUV;
            }

            gl_Position  = uProjection * uModelview * pos;
        });

    GLuint vertexShader = _loadShaderSrc(GL_VERTEX_SHADER, vertSrc);

    // ----------------------------------------------------------------------

    PRINTF("DEBUG: GL_FRAGMENT_SHADER\n");

    static const char fragSrc[] =
        "#version 100\n"

        STRINGIFY(

        //precision lowp float;
        precision mediump float;
        //precision highp   float;

        uniform sampler2D uSampler2d;

        uniform int   uBlitOn;
        uniform int   uTextOn;
        uniform int   uPattOn;
        uniform int   uGlowOn;
        uniform float uStipOn;

        uniform vec4  uColor;

        //uniform float uScaleOn;

        //uniform int   uPalOn;

        varying vec2  v_texCoord;
        varying float v_alpha;
        varying float v_dist;

        const float pixelsPerPattern = 32.0;

        void main(void) {

            if (1 == uBlitOn) {
                gl_FragColor = texture2D(uSampler2d, v_texCoord);
                //gl_FragColor.rgb = texture2D(uSampler2d, v_texCoord).rgb;
                //gl_FragColor.a = texture2D(uSampler2d1, v_texCoord).a;
                //gl_FragColor = texture2D(uSampler2d1, v_texCoord);
            } else if (1 == uTextOn) {
                //gl_FragColor = texture2D(uSampler2d, v_texCoord);
                //vec4 _sample = texture2D(uSampler2d, v_texCoord);
//#ifdef S52_USE_GLSC2
                // GLSC2 has no GL_ALPHA in freetype-gl/texture_atlas_new() use GL_RED
                // gl_FragColor.a   = texture2D(uSampler2d, v_texCoord).r;
//#else
                gl_FragColor.a   = texture2D(uSampler2d, v_texCoord).a;
//#endif
                gl_FragColor.rgb = uColor.rgb;
            } else if (1 == uPattOn) {
                gl_FragColor = texture2D(uSampler2d, v_texCoord);
                gl_FragColor.rgb = uColor.rgb;
            })

#ifdef S52_USE_AFGLOW
        STRINGIFY(
            else if (1 == uGlowOn) {
                float dist = distance(vec2(0.5,0.5), gl_PointCoord);
                if (0.5 < dist) {
                    discard;
                } else {
                    gl_FragColor   = uColor;
                    gl_FragColor.a = v_alpha;
                }
            })
#endif
        STRINGIFY(
            else if (0.0 < uStipOn) {
                vec2 tex_n = v_texCoord;
                //tex_n.x   *= v_dist / uScaleOn / 32.0;
                tex_n.x   *= v_dist / uStipOn / pixelsPerPattern;
                //tex_n.x   *= v_dist;
                //tex_n.y   *= v_dist / uScaleOn / 32.0;
                gl_FragColor.a   = texture2D(uSampler2d, tex_n).a;
                gl_FragColor.rgb = uColor.rgb;
                //gl_FragColor.a = 1.0;
                //gl_FragColor = vec4(v_dist, 0.0, 0.0, 1.0);

                /* FIXME: stippled graticule fail whem rotating
                // will fail on NW-SE line segment
                gl_FragColor = uColor;
                int mody = int(mod(gl_FragCoord.y, 16.0));
                int modx = int(mod(gl_FragCoord.x, 16.0));
                if (1 == uStipOn) {
                    if (((16-modx)>mody && mody>(8-modx)) || (mody>(24-modx)))
                        discard;
                } else { // 2
                    //int result = int(mod(gl_FragCoord.x + gl_FragCoord.y, 2.0));
                    int result = int(mod(gl_FragCoord.x + gl_FragCoord.y, 4.0));
                    bool dis = result == 0;
                    //if (dis)
                    if (!dis)
                        discard;
                }
                 */
            } else {

                //
                // normal case last!
                //

                gl_FragColor = uColor;
            }
        })

        ;  // this signal the string end

    GLuint fragmentShader = _loadShaderSrc(GL_FRAGMENT_SHADER, fragSrc);


    // ----------------------------------------------------------------------

    if ((0==programObject) || (0==vertexShader) || (0==fragmentShader)) {
        PRINTF("ERROR: problem loading shaders and/or creating program\n");
        g_assert(0);
        return 0;
    }
    _checkError("_compShaderSrc() -0-");

    if (TRUE != glIsShader(vertexShader)) {
        PRINTF("ERROR: glIsShader(vertexShader) failed\n");
        g_assert(0);
        return 0;
    }
    if (TRUE != glIsShader(fragmentShader)) {
        PRINTF("ERROR: glIsShader(fragmentShader) failed\n");
        g_assert(0);
        return 0;
    }

    _checkError("_compShaderSrc() -1-");

    glAttachShader(programObject, vertexShader);
    glAttachShader(programObject, fragmentShader);
    _checkError("_compShaderSrc() -1.1-");

#if !defined(S52_USE_ANDROID)
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
    if (NULL != _glProgramParameteriEXT) {
        _glProgramParameteriEXT(programObject, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        _checkError("_compShaderSrc() -1.2-");
    }
#endif  // !S52_USE_ANDROID

    glLinkProgram(programObject);
    GLint linked = GL_FALSE;
    glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
    if (GL_TRUE == linked){
        // Note: migth need a draw() call before saving
        // to instanciate the prog (see S52_GL_end())
        _saveShaderBin(programObject);
        ;
    } else {
        GLsizei length;
        GLchar  infoLog[2048];

        glGetProgramInfoLog(programObject,  2048, &length, infoLog);
        PRINTF("DEBUG: problem linking program:%s", infoLog);

        g_assert(0);
        return 0;
    }

    glUseProgram(programObject);

    _checkError("_compShaderSrc() -2-");

    return programObject;
}
#endif  // !S52_USE_GLSC2

static GLuint    _bindAttrib(GLuint programObject)
{
    //load all attributes
    _aPosition   = glGetAttribLocation(programObject, "aPosition");
    //_aPosition2  = glGetAttribLocation(programObject, "aPosition2");
    _aUV         = glGetAttribLocation(programObject, "aUV");
    _aAlpha      = glGetAttribLocation(programObject, "aAlpha");

    return programObject;
}

static GLuint    _bindUnifrom(GLuint programObject)
{
    _uProjection = glGetUniformLocation(programObject, "uProjection");
    _uModelview  = glGetUniformLocation(programObject, "uModelview");
    _uColor      = glGetUniformLocation(programObject, "uColor");
    _uPointSize  = glGetUniformLocation(programObject, "uPointSize");
    _uSampler2d  = glGetUniformLocation(programObject, "uSampler2d");

    _uBlitOn     = glGetUniformLocation(programObject, "uBlitOn");
    _uTextOn     = glGetUniformLocation(programObject, "uTextOn");
    _uGlowOn     = glGetUniformLocation(programObject, "uGlowOn");
    _uStipOn     = glGetUniformLocation(programObject, "uStipOn");
    //_uScaleOn    = glGetUniformLocation(programObject, "uScaleOn");

    // optimisation test
    //_uPalOn      = glGetUniformLocation(programObject, "uPalOn");
    //_uPalArray   = glGetUniformLocation(programObject, "uPalArray");

    _uColor      = glGetUniformLocation(programObject, "uColor");

    _uPattOn     = glGetUniformLocation(programObject, "uPattOn");
    _uPattGridX  = glGetUniformLocation(programObject, "uPattGridX");
    _uPattGridY  = glGetUniformLocation(programObject, "uPattGridY");
    _uPattW      = glGetUniformLocation(programObject, "uPattW");
    _uPattH      = glGetUniformLocation(programObject, "uPattH");

    return programObject;
}

static int       _clearColor(void)
{
    //clear FB ALPHA before use
    //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0, 0.0, 0.0, 0.0);

#ifdef S52_USE_TEGRA2
    // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase), define in gl2ext.h
    //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
    glClear(GL_COLOR_BUFFER_BIT | GL_COVERAGE_BUFFER_BIT_NV);
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    _checkError("_clearColor() -1-");

    return TRUE;
}

static int       _1024bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
// make a RGBA texture from 32x32 bitmask (a la glPolygonStipple() in OpenGL 1.x)
{
    //memset(rgba_mask, 0, 4*32*8*4);
    bzero(rgba_mask, 4*32*8*4);

    for (int i=0; i<(4*32); ++i) {
        if (mask[i] & (1<<0)) {
            //rgba_mask[(i*4*8)+0] = 0;  // R
            //rgba_mask[(i*4*8)+1] = 0;  // G
            //rgba_mask[(i*4*8)+2] = 0;  // B
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

    return TRUE;
}

static int       _32bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
// make a RGBA texture from 32x1 bitmask (a la glLineStipple() in OpenGL 1.x)
{
    //memset(rgba_mask, 0, 8*4*4);  // 32x4B (rgda)
    bzero(rgba_mask, 8*4*4);  // 32x4B (rgda)

    for (int i=0; i<4; ++i) {     // 4 bytes
        if (mask[i] & (1<<0)) {
            //rgba_mask[(i*4*8)+0] = 0;   // R
            //rgba_mask[(i*4*8)+1] = 0;   // G
            //rgba_mask[(i*4*8)+2] = 0;   // B
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

    return TRUE;
}

static GLuint    _initAPtexStore(GLsizei w, GLsizei h, GLvoid *data)
// alloc GPU storage & load texture
{
    // FIXME: send GL_APHA instead of RGBA - convertion of bits to bytes instead of RGBA
    // FIXME: GLSC2 doesn't have GL_ALPHA nor glTexStorage2D()
    GLuint maskTexID = 0;
    glGenTextures(1, &maskTexID);
    glBindTexture(GL_TEXTURE_2D, maskTexID);

#ifdef S52_USE_GLSC2
    // modern way
    glTexStorage2D (GL_TEXTURE_2D, 0, GL_RGB, w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
    // Note: GL_RGBA is needed for:
    // - Vendor: Tungsten Graphics, Inc. - Renderer: Mesa DRI Intel(R) 965GM x86/MMX/SSE2
    // - Vendor: Qualcomm                - Renderer: Adreno (TM) 320
    //if (NULL == data)
    //    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, data);
    //else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // modern way
    //_glTexStorage2DEXT (GL_TEXTURE_2D, 0, GL_RGBA8_OES, w, h);
    //_checkError("_renderTexure() -000-");
    //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#endif

    //_checkError("_initAPtexStore() -00-");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture  (GL_TEXTURE_2D, 0);

    _checkError("_initAPtexStore() -0-");

    return maskTexID;
}

static int       _initAPTexture(void)
{
    if (0 != _nodata_mask_texID)
        return TRUE;

    _checkError("_initTexture -00-");

    // fill _rgba_nodata_mask - expand bitmask to a RGBA buffer
    // that will acte as a stencil in the fragment shader
    _1024bitMask2RGBATex(_nodata_mask_bits, _nodata_mask_rgba);
    _32bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);
    _32bitMask2RGBATex  (_dashpa_mask_bits, _dashpa_mask_rgba);

    // nodata pattern
    _nodata_mask_texID = _initAPtexStore(32, 32, _nodata_mask_rgba);

    // dott pattern
    _dottpa_mask_texID = _initAPtexStore(32, 1, _dottpa_mask_rgba);

    // dash pattern
    _dashpa_mask_texID = _initAPtexStore(32, 1, _dashpa_mask_rgba);

    // setup mem buffer to save FB to
    _fb_pixels_id      = _initAPtexStore(_vp.w, _vp.h, NULL);

    // debug
    //_fb_pixels_id      = _initAPtexStore(0, 0, NULL);
    //glGenTextures(1, &_fb_pixels_id);

    return TRUE;
}

static int       _init_gl2(void)
{
    //if (TRUE == glIsProgram(_programObject)) {
    if (0 != _programObject) {
        PRINTF("DEBUG: _programObject valid not re-init\n");
        return TRUE;
    }

    PRINTF("NOTE: begin GL2/GLSL init ..\n");

    if (NULL == _tessWorkBuf_d) {
        //_tessWorkBuf_d = g_array_new(FALSE, FALSE, sizeof(double)*3);
        _tessWorkBuf_d = g_array_new(FALSE, FALSE, sizeof(GLdouble)*3);
    }
    if (NULL == _tessWorkBuf_f) {
        //_tessWorkBuf_f = g_array_new(FALSE, FALSE, sizeof(float)*3);
        _tessWorkBuf_f = g_array_new(FALSE, FALSE, sizeof(GLfloat)*3);
    }

    _init_freetype_gl();

    _initAPTexture();

#if !defined(S52_USE_ANDROID)
#ifdef S52_USE_EGL
    _loadProcEXT();
#endif
#endif  // !S52_USE_ANDROID

#ifdef S52_USE_GLSC2
    _programObject = _loadShaderBin();
    if (0 == _programObject) {
        PRINTF("WARNING: GLSC2/GLSL _loadShaderBin() failed .. \n");
        g_assert(0);
        return FALSE;
    }
#else  // S52_USE_GLSC2
    _programObject = _compShaderSrc(_programObject);
    if (0 == _programObject) {
        PRINTF("WARNING: GL2/GLSL init .. failed\n");
        g_assert(0);
        return FALSE;
    }
#endif  // S52_USE_GLSC2

    _bindAttrib (_programObject);
    _bindUnifrom(_programObject);

    //  init matrix stack
    //memset(_mvm, 0, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);
    //memset(_pjm, 0, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);
    bzero(_mvm, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);
    bzero(_pjm, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);

    //  init matrix
    _glMatrixMode  (GL_PROJECTION);
    _glLoadIdentity(GL_PROJECTION);

    _glMatrixMode  (GL_MODELVIEW);
    _glLoadIdentity(GL_MODELVIEW);

    //clear FB ALPHA before use, also put blue but doen't show up unless startup bug
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    //glClearColor(0, 0, 1, 1);     // blue
    //glClearColor(1.0, 0.0, 0.0, 1.0);     // red
    //glClearColor(1.0, 0.0, 0.0, 0.0);     // red
    glClearColor(0.0, 0.0, 0.0, 0.0);

    _clearColor();

    // turn OFF rgb lookup
    //glUniform1i(_uPalOn, -1);

    return TRUE;
}

static int       _renderTile(S52_DListData *DListData)
{
    _checkError("_renderTile() -0-");

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    glEnableVertexAttribArray(_aPosition);

    for (guint i=0; i<DListData->nbr; ++i) {
        guint j     = 0;
        GLint mode  = 0;
        GLint first = 0;
        GLint count = 0;

        GArray *vert = S57_getPrimVertex(DListData->prim[i]);
        vertex_t *v = (vertex_t*)vert->data;

        glVertexAttribPointer(_aPosition, 3, GL_FLOAT, GL_FALSE, 0, v);

        while (TRUE == S57_getPrimIdx(DListData->prim[i], j, &mode, &first, &count)) {
            /*
            if (_TRANSLATE == mode) {
                PRINTF("FIXME: handle _TRANSLATE for Tile!\n");
                g_assert(0);
            } else {
                glDrawArrays(mode, first, count);
            }
            */

            glDrawArrays(mode, first, count);
            ++j;
        }
    }

    glDisableVertexAttribArray(_aPosition);

    _checkError("_renderTile() -1-");

    return TRUE;
}

#if 0
static int       _getAPTileDim(S52_DListData *DListData, double *w, double *h)
// debug
{
    vertex_t min_x =  INFINITY;
    vertex_t min_y =  INFINITY;
    vertex_t max_x = -INFINITY;
    vertex_t max_y = -INFINITY;

    for (guint i=0; i<DListData->nbr; ++i) {
        GArray *vert = S57_getPrimVertex(DListData->prim[i]);
        vertex_t *v = (vertex_t*)vert->data;

        for (guint j=0; j<vert->len; j+=3) {
            vertex_t x = v[j+0];
            vertex_t y = v[j+1];
            //vertex_t z = v[j+2];
            min_x = (x<min_x) ? x : min_x;
            min_y = (y<min_y) ? y : min_y;
            max_x = (x>max_x) ? x : max_x;
            max_y = (y>max_y) ? y : max_y;
        }
    }

    //*w = (max_x - min_x)/2.0;
    //*h = (max_y - min_y)/2.0;
    *w = (max_x - min_x) / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
    *h = (max_y - min_y) / (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0);

    return TRUE;
}
#endif  // 0

static int       _bindFBO(GLuint mask_texID)
// Frame Buffer Object
{
    _checkError("_initFBO() -00-");

    if (0 == _fboID) {
        glGenFramebuffers (1, &_fboID);
    }

    _checkError("_initFBO() -0-");

    glBindFramebuffer     (GL_FRAMEBUFFER, _fboID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_texID, 0);

    _checkError("_initFBO() -1-");

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        PRINTF("WARNING: glCheckFramebufferStatus() fail, status: %i\n", status);

        //*
        switch(status)
        {
        case GL_FRAMEBUFFER_UNSUPPORTED:
            PRINTF("Framebuffer object format is unsupported by the video hardware. (GL_FRAMEBUFFER_UNSUPPORTED)(FBO - 820)\n");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            PRINTF("Incomplete attachment. (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)(FBO - 820)\n");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            PRINTF("Incomplete missing attachment. (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT)(FBO - 820)\n");
            break;

// Not in GL
//            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
//                PRINTF("Incomplete dimensions. (GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT)(FBO - 820)");
//                break;

/*
        case GL_FRAMEBUFFER_INCOMPLETE_FORMATS:
            PRINTF("Incomplete formats. (GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT)(FBO - 820)");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            PRINTF("Incomplete draw buffer. (GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT)(FBO - 820)");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            PRINTF("Incomplete read buffer. (GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT)(FBO - 820)");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            PRINTF("Incomplete multisample buffer. (GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT)(FBO - 820)");
            break;
//*/

        default:
            PRINTF("ERROR: Some video driver error or programming error occured. Framebuffer object status is invalid. (FBO - 823)");
            break;
        }

        g_assert(0);

        return FALSE;
    }

    _checkError("_initFBO() -end-");

    // debug - test to get rid of artefact at start up
    //glDeleteFramebuffers(1, &fboID);
    //fboID = 0;


    return TRUE;
}

static int       _fixDPI_glScaled(void)
{
    ////////////////////////////////////////////////////////////////
    // DPI tweak for texture/pattern
    //
    // FIXME: scale found by trial and error
    // FIXME: dotpitch should do in normal case and user override via
    // S52_MAR_DOTPITCH_MM_X/Y (will scale symb also!)
    //

#ifdef S52_USE_ANDROID
#ifdef S52_USE_TEGRA2
    // Xoom - S52_MAR_DOTPITCH_MM set to 0.3
    double DPIfac = 8.0;
#endif

#ifdef S52_USE_ADRENO
    // Nexus 7 (2013) - 323ppi landscape -
    //double fac = 1.0;

    // Nexus 7 (2013) - 323ppi landscape - S52_MAR_DOTPITCH_MM set to 0.2
    double DPIfac = 4.0;
#endif
#endif  // S52_USE_ANDROID

    ////////////////////////////////////////////////////////////////

    // S52 symb unit = 0.01mm
    double scaleX = 1.0 / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0);
    double scaleY = 1.0 / (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0);
    // debug
    //double scaleX = 1.0 / (S52_MP_get(S52_MAR_DOTPITCH_MM_X) * 100.0 * DPIfac);
    //double scaleY = 1.0 / (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) * 100.0 * DPIfac);

    _glScaled(scaleX, scaleY, 1.0);

    return TRUE;
}

static int       _minPOT(int value)
// min POT greater than 'value' - simplyfie texture handling
// compare to _nearestPOT() but use more GPU memory
{
    int i = 1;

    if (value <= 0) return -1;      // Error!

    for (;;) {
        if (value == 0) return i;
        value >>= 1;
        i *= 2;
    }
}

static int       _renderTexure(S52_obj *obj, double tileWpx, double tileHpx, double stagOffsetPix)
{
    GLsizei w = ceil(tileWpx);
    GLsizei h = ceil(tileHpx);

    if (FALSE == _GL_OES_texture_npot) {
        w = _minPOT(w);
        h = _minPOT(h);
    }

    if (0.0 != stagOffsetPix) {
        w *= 2;
        h *= 2;
    }

    _checkError("_renderTexure() -0000-");

    GLuint mask_texID = _initAPtexStore(w, h, NULL);

    _bindFBO(mask_texID);

    // save texture mask ID when everythings check ok
    S52_PL_setAPtexID(obj, mask_texID);

    //_clearColor();

    // render to texture -----------------------------------------

    _glMatrixSet(VP_WIN);

    /* debug - draw X and Y axis + diagonal
    {
        _glLineWidth(1.0);

        // Note: line in X / Y need to start at +1 to show up
        // FIXME: why -X !!
        pt3v lineW[2] = {{0.0, 1.0, 0.0}, {  w, 1.0, 0.0}};  // left
        pt3v lineH[2] = {{1.0, 0.0, 0.0}, {1.0,   h, 0.0}};  // down
        //pt3v lineD[2] = {{5.0, 5.0, 0.0}, {  w,   h, 0.0}};  // diag

        _DrawArrays_LINE_STRIP(2, (vertex_t*)lineW);
        _DrawArrays_LINE_STRIP(2, (vertex_t*)lineH);
        //_DrawArrays_LINE_STRIP(2, (vertex_t*)lineD);  // diag
    }
    //*/

    {   // set line/point width
        double   dummy = 0.0;
        char     pen_w = '1';
        S52_PL_getLCdata(obj, &dummy, &pen_w);


        _glLineWidth(pen_w - '0' + 1.0);  // must enlarge line glsl sampler

        _checkError("_renderTexure() -0-");

        _glPointSize(pen_w - '0' + 1.0);  // sampler + AA soften pixel, so need enhencing a bit
    }

    _checkError("_renderTexure() -1-");

    // debug - locate drgare 2 dot symb
    //_glTranslated(0.0, 0.0, 0.0);
    //_glTranslated(0.0, 4.0, 0.0);
    //_glTranslated(4.0, 4.0, 0.0);
    //_glTranslated(4.0, 0.0, 0.0);

    _fixDPI_glScaled();

    S52_DListData *DListData = S52_PL_getDListData(obj);
    _renderTile(DListData);

    if (0.0 != stagOffsetPix) {
        _glLoadIdentity(GL_MODELVIEW);

        if (TRUE == _GL_OES_texture_npot) {
            //_glTranslated((tileWpx/2.0) + stagOffsetPix, tileHpx + (tileHpx/2.0), 0.0);
            _glTranslated((tileWpx/2.0) + stagOffsetPix, tileHpx, 0.0);
        } else {
            _glTranslated((w/2.0) + stagOffsetPix, (h/2.0), 0.0);
        }

        _fixDPI_glScaled();

        _renderTile(DListData);
    }

    _glMatrixDel(VP_WIN);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    _checkError("_renderTexure() -2-");

    return mask_texID;
}

static int       _renderAP_gl2(S52_obj *obj)
{
    // debug
    //if (0 == S52_PL_cmpCmdParam(obj, "NODATA03")) {
    //    PRINTF("DEBUG: nodata03 on group 2\n");
    //}
    //if (0 == S52_PL_cmpCmdParam(obj, "DIAMOND1")) {
    //    PRINTF("DEBUG: DIAMOND1 found\n");
    //}
    //if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj))) {
    //    PRINTF("DEBUG: DRGARE found\n");
    //}

    // FIXME: rename LLwx
    //double x1=0.0, y1=0.0;   // LL of region of area in world
    //double x2=0.0, y2=0.0;   // UR of region of area in world
    double LLx=0.0, LLy=0.0;   // LL of region of area in world
    double URx=0.0, URy=0.0;   // UR of region of area in world
    double tileWpx = 0.0;
    double tileHpx = 0.0;
    double stagOffsetPix = _getWorldGridRef(obj, &LLx, &LLy, &URx, &URy, &tileWpx, &tileHpx);
    double tileWw = tileWpx * _scalex;
    double tileHw = tileHpx * _scaley;

    //PRINTF("DEBUG: %s: grid x1:%f y1:%f Ww:%f Hw:%f sop:%f\n", S52_PL_getOBCL(obj), LLx, LLy, tileWw, tileHw, stagOffsetPix);

    GLuint mask_texID = S52_PL_getAPtexID(obj);
    if (0 == mask_texID) {
        // FIXME: refactor into init/build/draw APtex
        // - initAPTex
        // - initAPFBO
        // - setAPtexID

        // draw tex
        if (TRUE == glIsEnabled(GL_SCISSOR_TEST)) {
            // scissor box interfere with texture creation
            glDisable(GL_SCISSOR_TEST);
            mask_texID = _renderTexure(obj, tileWpx, tileHpx, stagOffsetPix);
            glEnable(GL_SCISSOR_TEST);
        } else {
            mask_texID = _renderTexure(obj, tileWpx, tileHpx, stagOffsetPix);
        }
    }

    S52_DListData *DListData = S52_PL_getDListData(obj);
    _setFragAttrib(DListData->colors, S57_getHighlight(S52_PL_getGeo(obj)));

    // debug - red conspic
    //glUniform4f(_uColor, 1.0, 0.0, 0.0, 0.0);

    _glUniformMatrix4fv_uModelview();

    //glUniform1f(_uPattOn,    1.0);
    glUniform1i(_uPattOn,       1);
    // make no diff on mesa if it is 0.0 but not on Xoom (tegra2)
    glUniform1f(_uPattGridX, LLx);
    glUniform1f(_uPattGridY, LLy);
    glUniform1f(_uPattW,     tileWw);        // tile width in world
    glUniform1f(_uPattH,     tileHw);        // tile height in world

    glBindTexture(GL_TEXTURE_2D, mask_texID);

    _fillArea(S52_PL_getGeo(obj));

    glBindTexture(GL_TEXTURE_2D, 0);

    //glUniform1f(_uPattOn,    0.0);
    glUniform1i(_uPattOn,    0);
    glUniform1f(_uPattGridX, 0.0);
    glUniform1f(_uPattGridY, 0.0);
    glUniform1f(_uPattW,     0.0);
    glUniform1f(_uPattH,     0.0);

    _checkError("_renderAP_gl2() -2-");

    return TRUE;
}

static int       _renderLS_gl2(S52_obj *obj, char style, guint npt, double *ppt)
{
    // debug - i965 DRI3 bug, force DRI2 LIBGL_DRI3_DISABLE=1 (fail in gdb)
    //S57ID = 891, name = SBDARE GB5X01NE.000 - bug mesa dri
    /* S57ID = 5756 FSHZNE GB4X0000.000
    if (NULL!=obj && (891==S57_getS57ID(S52_PL_getGeo(obj)))) {
        //PRINTF("DEBUG: FSHZNE found\n");
        PRINTF("DEBUG: SBDARE found\n");
        //S52_utils_gdbBreakPoint();
        //g_assert(0);
    }
    //*/

    //* debug - skip big geo not solid that break DRI3 in gdb
    if (('L'!=style) && (5056<=npt*16)) {
        npt = 316;
        return TRUE;
    }
    //*/

    // convert to float
    _d2f(_tessWorkBuf_f, npt, ppt);
    vertex_t *v = (vertex_t *)_tessWorkBuf_f->data;

    _glUniformMatrix4fv_uModelview();

    /* debug - force immediat rendering of all line in solid line style
    _DrawArrays_LINE_STRIP(npt, v);
    return TRUE;
    //*/

    //* Note: must be in scope when calling _glCallList()
    float texUV[4] = {
        0.0, 0.0,
        1.0, 1.0
        //32.0, 1.0
    };
    //*/

    switch (style) {

        // SOLD
        case 'L': //_DrawArrays_LINE_STRIP(npt, v);
                  //return TRUE;
                  break;

        // DASH (dash 3.6mm, space 1.8mm)
        case 'S': glBindTexture(GL_TEXTURE_2D, _dashpa_mask_texID);
                  glUniform1f(_uStipOn, _scalex);

                  glEnableVertexAttribArray(_aUV);
                  glVertexAttribPointer(_aUV, 2, GL_FLOAT, GL_FALSE, 0, texUV);

                  // debug
                  //if (NULL != obj) {
                  //    S57_setHighlight(S52_PL_getGeo(obj), TRUE);
                  //}

                  break;

        // DOTT (dott 0.6mm, space 1.2mm)
        case 'T': glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);
                  glUniform1f(_uStipOn, _scalex);

                  glEnableVertexAttribArray(_aUV);
                  glVertexAttribPointer(_aUV, 2, GL_FLOAT, GL_FALSE, 0, texUV);

                  break;

        default:
            PRINTF("WARNING: invalid line style\n");
            g_assert(0);
            return FALSE;
    }

    if (NULL != obj) {
        S52_DListData *DListData = S52_PL_getDListData(obj);
        if (NULL == DListData) {
            DListData = S52_PL_newDListData(obj);
            DListData->create    =  FALSE;
            DListData->nbr       =  1;
            DListData->prim[0]   = S57_initPrim(NULL);
            DListData->crntPalID = (int)S52_MP_get(S52_MAR_COLOR_PALETTE);

            {
                S52_Color *col;
                char       styleDummy;   // L/S/T - use by normal command word LS()
                char       pen_w;
                S52_PL_getLSdata(obj, &pen_w, &styleDummy, &col);
                DListData->colors[0] = *col;
                DListData->colors[0].fragAtt.pen_w = pen_w;
                DListData->colors[0].fragAtt.trans = '0';  // 0% - no transparency
            }

            // fill prim
            if ('L' == style) {
                S57_begPrim(DListData->prim[0], GL_LINE_STRIP);
                for (guint i=0; i<npt; ++i, v+=3) {
                    S57_addPrimVertex(DListData->prim[0], v);
                }

            //*
            } else {
                // FIXME: plit VBO in 256 vertexs for i965 DRI3!
                S57_begPrim(DListData->prim[0], GL_LINES);
                for (guint i=0; i<npt-1; ++i, v+=3) {
                    // put dist in Z v[2]=v[5] = dist (ie sqrt(dx*dx + dy*dy)])
                    float dx = v[0] - v[3];
                    float dy = v[1] - v[4];
                    float dist = sqrt(dx*dx + dy*dy);

                    // dist / Z
                    v[2] = v[5] = dist;

                    // add p1
                    S57_addPrimVertex(DListData->prim[0], v);

                    // add p2
                    S57_addPrimVertex(DListData->prim[0], v+3);
                }
            }
            //*/

            S57_endPrim(DListData->prim[0]);
            DListData->vboIds[0] = _VBOCreate(DListData->prim[0]);

            // debug - DRI3 intel i965
            //_VBOCreate2_glCallList(DListData);
        }

        _glCallList(DListData, S57_getHighlight(S52_PL_getGeo(obj)));

    } else {
        // draw graticule

        // flush FB
        //_clearColor();

        // 1 - compute line legs length
        // 2 - compute numger of repeate
        // 4.a - send tex_n array for  GL_LINES to vertex shader
        // 4.b - send _scalex to uniforme
        // 5 - in vertex shader:
        //     5.1 - compute tex_n - (dist  / _scalex) / 32 <-- flat throughout one line
        //     5.2 - in frag shader:
        //           5.3 - apply tex_n to UV.x *= tex_n

        float dx = v[0] - v[3];
        float dy = v[1] - v[4];
        float dist = sqrt(dx*dx + dy*dy);

        // dist / Z
        v[2] = v[5] = dist;

        // immediate mode
        g_assert(2 == npt);
        _DrawArrays_LINES(npt, v);
    }


    glDisableVertexAttribArray(_aUV);

    glBindTexture(GL_TEXTURE_2D, 0);

    glUniform1f(_uStipOn, 0.0);

    return TRUE;
}

/*
static int       _renderLS_gl2(char style, guint npt, double *ppt)
{
    _d2f(_tessWorkBuf_f, npt, ppt);

    switch (style) {

        case 'L': // SOLD --correct
            // but this stippling break up antialiase
            //glLineStipple(1, 0xFFFF);
            _glUniformMatrix4fv_uModelview();
            _DrawArrays_LINE_STRIP(npt, (vertex_t *)_tessWorkBuf_f->data);

            return TRUE;

        case 'S': // DASH (dash 3.6mm, space 1.8mm) --incorrect  (last space 1.8mm instead of 1.2mm)
            //glEnable(GL_LINE_STIPPLE);
            //_glLineStipple(3, 0x7777);
            //glLineStipple(2, 0x9248);  // !!
            glBindTexture(GL_TEXTURE_2D, _dashpa_mask_texID);
            // debug
            //return TRUE;
            break;

        case 'T': // DOTT (dott 0.6mm, space 1.2mm) --correct
            //glEnable(GL_LINE_STIPPLE);
            //_glLineStipple(1, 0xFFF0);
            //_glPointSize(pen_w - '0');
            glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);
            break;

        default:
            PRINTF("WARNING: invalid line style\n");
            g_assert(0);
            return FALSE;
    }

    // FIXME: use GL_POINTS if DOTT
    glUniform1f(_uTextOn, 1.0);
    _glUniformMatrix4fv_uModelview();

    float *v = (float *)_tessWorkBuf_f->data;
    for (guint i=1; i<npt; ++i, ppt+=3, v+=3) {
        float dx       = ppt[0] - ppt[3];
        float dy       = ppt[1] - ppt[4];
        float leglen_m = sqrt(dx*dx + dy*dy);   // leg length in meter
        float leglen_px= leglen_m  / _scalex;   // leg length in pixel
        float tex_n    = leglen_px / 32.0;      // number of texture pattern - 1D 32x1 pixels

        float ptr[4] = {
            0.0,   0.0,
            tex_n, 1.0
        };

        glEnableVertexAttribArray(_aUV);
        glVertexAttribPointer    (_aUV, 2, GL_FLOAT, GL_FALSE, 0, ptr);

        //_DrawArrays_LINE_STRIP(npt, (vertex_t *)_tessWorkBuf_f->data);
        _DrawArrays_LINE_STRIP(2, v);
    }

    glDisableVertexAttribArray(_aUV);
    glBindTexture(GL_TEXTURE_2D,  0);
    glUniform1f(_uTextOn, 0.0);

    return TRUE;
}
*/
