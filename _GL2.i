// _GL2.i: definition & declaration for GL2.x, GLES2.x.
//         Link to libGL.so or libGLESv2.so.
//
// SD 2014MAY20
//
// Note: if this code is included it mean that S52_USE_GL2 is allready defined
//       to get GLES2 specific code (ex GLSL ES) define S52_USE_GLES2 also.
// Note: GL2 matrix stuff work with GL_DOUBLE normaly while GLES2 work only with GL_FLOAT


// Note: GLES2 is a subset of GL2, so declaration in GLES2 header cover all GL2 decl use in the code
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
typedef double GLdouble;

#include "tesselator.h"
typedef GLUtesselator GLUtesselatorObj;
typedef GLUtesselator GLUtriangulatorObj;

////////////////////////////////////////////////////////
// forward decl
static double      _getGridRef(S52_obj *, double *, double *, double *, double *, double *, double *);
static int         _fillArea(S57_geo *);
static void        _glMatrixMode(guint);
static void        _glLoadIdentity(int);
static void        _glUniformMatrix4fv_uModelview(void);
static int         _glMatrixSet(VP);
static int         _glMatrixDel(VP);
static int         _pushScaletoPixel(int);
static int         _popScaletoPixel(void);
static GLubyte     _setFragColor(S52_Color *);
static void        _glLineWidth(GLfloat);
static void        _glPointSize(GLfloat);
static inline void _checkError(const char *);
static GLvoid      _DrawArrays_LINE_STRIP(guint, vertex_t *);  // debug pattern
////////////////////////////////////////////////////////


// used to convert float to double for tesselator
static GArray *_tessWorkBuf_d = NULL;
// used to convert geo double to VBO float
static GArray *_tessWorkBuf_f = NULL;

// glsl main
static GLint  _programObject  = 0;
static GLuint _vertexShader   = 0;
static GLuint _fragmentShader = 0;

// glsl uniform
static GLint _uProjection = 0;
static GLint _uModelview  = 0;
static GLint _uColor      = 0;
static GLint _uPointSize  = 0;
static GLint _uSampler2d  = 0;
static GLint _uBlitOn     = 0;
static GLint _uStipOn     = 0;
static GLint _uGlowOn     = 0;

static GLint _uPattOn     = 0;
static GLint _uPattGridX  = 0;
static GLint _uPattGridY  = 0;
static GLint _uPattW      = 0;
static GLint _uPattH      = 0;

// glsl varying
static GLint _aPosition    = 0;
static GLint _aUV          = 0;
static GLint _aAlpha       = 0;

// alpha is 0.0 - 1.0
#define TRNSP_FAC_GLES2   0.25


//---- PATTERN GL2 / GLES2 -----------------------------------------------------------
//
// NOTE: 4 mask are drawn to fill the square made of 2 triangles (fan)
// NOTE: MSB 0x01, LSB 0xE0 - so it left most pixels is at 0x01
// and the right most pixel in a byte is at 0xE0
// 1 bit in _nodata_mask is 4 bytes (RGBA) in _rgba_nodata_mask (s0 x 8 bits x 4 )

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

// DASH pattern
static GLuint        _dashpa_mask_texID = 0;
static GLubyte       _dashpa_mask_rgba[8*4*4];    // 32 * 4rgba = 8bits * 4col * 1row * 4rgba
static const GLubyte _dashpa_mask_bits[4] = {     // 4 x 8 bits = 32 bits
    0xFF, 0xF0, 0xFF, 0xF0
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
static texture_font_t  *_freetype_gl_font[S52_MAX_FONT] = {NULL,NULL,NULL,NULL};
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

    if (NULL == _freetype_gl_atlas) {
        _freetype_gl_atlas = texture_atlas_new(512, 512, 1);    // alpha only
        //_freetype_gl_atlas = texture_atlas_new(1024, 1024, 1);    // alpha only
    } else {
        PRINTF("WARNING: _init_freetype_gl() allready initialize\n");
        g_assert(0);
        return FALSE;
    }

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
        PRINTF("default TTF not found in s52.cfg\n");
        PRINTF("using hard-coded TTF filename: %s\n", _freetype_gl_fontfilename);
    } else {
       if (TRUE == g_file_test(TTFPath, G_FILE_TEST_EXISTS)) {
            _freetype_gl_fontfilename = TTFPath;
            PRINTF("default TTF found in s52.cfg (%s)\n", TTFPath);
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
    texture_font_load_glyphs(_freetype_gl_font[1], cache);
    texture_font_load_glyphs(_freetype_gl_font[2], cache);
    texture_font_load_glyphs(_freetype_gl_font[3], cache);

    if (0 == _freetype_gl_textureID)
        glGenBuffers(1, &_freetype_gl_textureID);

    // FIXME: glIsBuffer fail here
    //if (GL_FALSE == glIsBuffer(_ftgl_textureID)) {
    if (0 == _freetype_gl_textureID) {
        PRINTF("ERROR: glGenBuffers() fail\n");
        g_assert(0);
        return FALSE;
    }

    if (NULL == _freetype_gl_buffer) {
        _freetype_gl_buffer = g_array_new(FALSE, FALSE, sizeof(_freetype_gl_vertex_t));
    }

    return TRUE;
}

static GArray   *_fill_freetype_gl_buffer(GArray *ftglBuf, const char *str, unsigned int weight)
// fill buffer whit triangles strip
// experimental: smaller text size if second line
{
    int   pen_x = 0;
    int   pen_y = 0;
    int   nl    = FALSE;
    glong len   = g_utf8_strlen(str, -1);

    g_array_set_size(ftglBuf, 0);

    for (glong i=0; i<len; ++i) {
        gchar           *utfc  = g_utf8_offset_to_pointer(str, i);
        gunichar         unic  = g_utf8_get_char(utfc);
        texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], unic);
        if (NULL == glyph) {
            continue;
        }

        // experimental: smaller text size if second line
        if (NL == unic) {
            weight = (0<weight) ? weight-1 : weight;
            texture_glyph_t *glyph = texture_font_get_glyph(_freetype_gl_font[weight], 'A');
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
    }

    return ftglBuf;
}

//-----------------------------------------
//
// gles2 float Matrix stuff (by hand)
//

static void      _make_z_rot_matrix(GLfloat angle, GLfloat *m)
{
   float c = cos(angle * M_PI / 180.0);
   float s = sin(angle * M_PI / 180.0);

   memset(m, 0, sizeof(GLfloat) * 16);
   m[0] = m[5] = m[10] = m[15] = 1.0;

   m[0] =  c;
   m[1] =  s;
   m[4] = -s;
   m[5] =  c;
}

static void      _make_scale_matrix(GLfloat xs, GLfloat ys, GLfloat zs, GLfloat *m)
{
   memset(m, 0, sizeof(GLfloat) * 16);
   m[0]  = xs;
   m[5]  = ys;
   m[10] = zs;
   m[15] = 1.0;
}

static void      _multiply(GLfloat *m, GLfloat *n)
{
   GLfloat tmp[16];
   const GLfloat *row, *column;
   div_t d;

   for (int i = 0; i < 16; i++) {
      tmp[i] = 0;
      d      = div(i, 4);
      row    = n + d.quot * 4;
      column = m + d.rem;
      for (int j = 0; j < 4; j++)
          tmp[i] += row[j] * column[j * 4];
   }
   memcpy(m, &tmp, sizeof tmp);
}

//------------ NOT USED -----------------------------
#if 0
static void      _mul_matrix(GLfloat *prod, const GLfloat *a, const GLfloat *b)
{
#define A(row,col)  a[(col<<2)+row]
#define B(row,col)  b[(col<<2)+row]
#define P(row,col)  p[(col<<2)+row]
   GLfloat p[16];
   for (GLint i = 0; i < 4; i++) {
      const GLfloat ai0=A(i,0),  ai1=A(i,1),  ai2=A(i,2),  ai3=A(i,3);
      P(i,0) = ai0 * B(0,0) + ai1 * B(1,0) + ai2 * B(2,0) + ai3 * B(3,0);
      P(i,1) = ai0 * B(0,1) + ai1 * B(1,1) + ai2 * B(2,1) + ai3 * B(3,1);
      P(i,2) = ai0 * B(0,2) + ai1 * B(1,2) + ai2 * B(2,2) + ai3 * B(3,2);
      P(i,3) = ai0 * B(0,3) + ai1 * B(1,3) + ai2 * B(2,3) + ai3 * B(3,3);
   }
   memcpy(prod, p, sizeof(p));
#undef A
#undef B
#undef P
}
#endif
//------------ NOT USED -----------------------------


static void      __gluMultMatrixVecf(const GLfloat matrix[16], const GLfloat in[4], GLfloat out[4])
{
    for (int i=0; i<4; i++) {
        out[i] = in[0] * matrix[0*4+i] +
                 in[1] * matrix[1*4+i] +
                 in[2] * matrix[2*4+i] +
                 in[3] * matrix[3*4+i];
    }
}

static int       __gluInvertMatrixf(const GLfloat m[16], GLfloat invOut[16])
{
    GLfloat inv[16], det;

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
    if (0.0 == det) {
        PRINTF("WARNING: det = 0, fail\n");
        return GL_FALSE;
    }

    det=1.0f/det;

    for (int i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;

    return GL_TRUE;
}

static void      __gluMultMatricesf(const GLfloat a[16], const GLfloat b[16], GLfloat r[16])
{
    for (int i=0; i<4; ++i) {
        for (int j=0; j<4; ++j) {
            r[i*4+j] = a[i*4+0] * b[0*4+j] +
                       a[i*4+1] * b[1*4+j] +
                       a[i*4+2] * b[2*4+j] +
                       a[i*4+3] * b[3*4+j];
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
    GLfloat out[4] = {0.0, 0.0, 0.0, 0.0};

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
                               const GLint   viewport[4],
                               GLfloat* objx, GLfloat* objy, GLfloat* objz)
{
    GLfloat finalMatrix[16];
    GLfloat in [4];
    GLfloat out[4];

    __gluMultMatricesf(modelMatrix, projMatrix, finalMatrix);
    if (GL_FALSE == __gluInvertMatrixf(finalMatrix, finalMatrix)) {
        PRINTF("WARNING: __gluInvertMatrixf() fail\n");
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
    if (0.0 == out[3]) {
        PRINTF("WARNING: __gluMultMatrixVecf() fail [out[3]=0]\n");
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
    GLfloat m[16];

    _make_scale_matrix((GLfloat) x, (GLfloat) y, (GLfloat) z, m);

    _multiply(_crntMat, m);

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //    _identity_MODELVIEW = FALSE;

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

    // optimisation - reset flag
    //if (GL_MODELVIEW == _mode)
    //    _identity_MODELVIEW = FALSE;

    return;
}

static int       _renderTXTAA_gl2(double x, double y, GLfloat *data, guint len)
// render VBO static text (ie no data) or dynamic text
{
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
    glUniform1f(_uStipOn, 1.0);

    glBindTexture(GL_TEXTURE_2D, _freetype_gl_atlas->id);

    //_glMatrixMode  (GL_MODELVIEW);
    _glLoadIdentity(GL_MODELVIEW);

    _glTranslated(x, y, 0.0);

    _pushScaletoPixel(FALSE);

    // horizontal text
    _glRotated(-_north, 0.0, 0.0, 1.0);

    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    glDrawArrays(GL_TRIANGLES, 0, len);

    _popScaletoPixel();

    glBindTexture(GL_TEXTURE_2D, 0);
    glUniform1f(_uStipOn, 0.0);

    // disconnect buffer
    glDisableVertexAttribArray(_aUV);
    glDisableVertexAttribArray(_aPosition);

    _checkError("_renderTXTAA_gl2() freetype-gl");

    return TRUE;
}

static GLuint    _loadShader(GLenum type, const char *shaderSrc)
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
            PRINTF("glCompileShader() log: %s\n", log);
        }
        g_assert(0);
        return FALSE;
    }

    return shader;
}

static int       _1024bitMask2RGBATex(const GLubyte *mask, GLubyte *rgba_mask)
// make a RGBA texture from 32x32 bitmask
{
    memset(rgba_mask, 0, 4*32*8*4);

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
// make a RGBA texture from 32x1 bitmask (those used by glPolygonStipple() in OpenGL 1.x)
{
    memset(rgba_mask, 0, 8*4*4);  // 32x4B (rgda)
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

static int       _initTexture(void)
{
    if (0 != _nodata_mask_texID)
        return TRUE;

    // load texture on GPU ----------------------------------

    // FIXME: send GL_APHA instead of RGBA - skip convertion of bits to RGBA
    // fill _rgba_nodata_mask - expand bitmask to a RGBA buffer
    // that will acte as a stencil in the fragment shader
    _1024bitMask2RGBATex(_nodata_mask_bits, _nodata_mask_rgba);
    _32bitMask2RGBATex  (_dottpa_mask_bits, _dottpa_mask_rgba);
    _32bitMask2RGBATex  (_dashpa_mask_bits, _dashpa_mask_rgba);

    glGenTextures(1, &_nodata_mask_texID);
    glGenTextures(1, &_dottpa_mask_texID);
    glGenTextures(1, &_dashpa_mask_texID);
    //glGenTextures(1,     &_aa_mask_texID);

    _checkError("_renderAP_NODATA_layer0 -0-");

    // ------------
    // nodata pattern
    glBindTexture(GL_TEXTURE_2D, _nodata_mask_texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, _nodata_mask_rgba);

    // ------------
    // dott pattern
    glBindTexture(GL_TEXTURE_2D, _dottpa_mask_texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, _dottpa_mask_rgba);

    // ------------
    // dash pattern
    glBindTexture(GL_TEXTURE_2D, _dashpa_mask_texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, _dashpa_mask_rgba);

    _checkError("_renderAP_NODATA_layer0 -1-");

    return TRUE;
}

static int       _init_gl2(void)
{
    PRINTF("begin GLSL init ..\n");

    if (TRUE == glIsProgram(_programObject)) {
        PRINTF("DEBUG: _programObject valid not re-init\n");
        return TRUE;
    }

    if (NULL == _tessWorkBuf_d)
        _tessWorkBuf_d = g_array_new(FALSE, FALSE, sizeof(double)*3);
    if (NULL == _tessWorkBuf_f)
        _tessWorkBuf_f = g_array_new(FALSE, FALSE, sizeof(float)*3);

    if (FALSE == glIsProgram(_programObject)) {
        GLint linked = GL_FALSE;

#ifdef S52_USE_FREETYPE_GL
        _init_freetype_gl();
#endif

        _initTexture();

        // ----------------------------------------------------------------------

        PRINTF("DEBUG: building '_programObject'\n");
        _programObject = glCreateProgram();
        if (0 == _programObject) {
            PRINTF("ERROR: glCreateProgram() FAILED\n");
            g_assert(0);
            return FALSE;
        }

        // ----------------------------------------------------------------------
        PRINTF("DEBUG: GL_VERTEX_SHADER\n");

        static const char vertSrc[] =
//#if (defined(S52_USE_GL2) || defined(S52_USE_GLES2))
#ifdef S52_USE_GLES2
            //"precision lowp float;                                          \n"
            "precision mediump float;                                       \n"
            //"precision highp   float;                                       \n"
#endif
            "uniform   mat4  uProjection;                                   \n"
            "uniform   mat4  uModelview;                                    \n"
            "uniform   float uPointSize;                                    \n"
            "uniform   float uPattOn;                                       \n"
            "uniform   float uPattGridX;                                    \n"
            "uniform   float uPattGridY;                                    \n"
            "uniform   float uPattW;                                        \n"
            "uniform   float uPattH;                                        \n"

            "attribute vec2  aUV;                                           \n"
            "attribute vec4  aPosition;                                     \n"
            "attribute float aAlpha;                                        \n"

            "varying   vec2  v_texCoord;                                    \n"
            "varying   vec4  v_acolor;                                      \n"
            "varying   float v_pattOn;                                      \n"
            "varying   float v_alpha;                                       \n"

            "void main(void)                                                \n"
            "{                                                              \n"
            // version 130 (not in mesa v 100)
            //"  gl_Position = ftransform(); \n"
            //"  vec4 TexCoord = gl_MultiTexCoord0; \n"

            "    v_alpha      = aAlpha;                                     \n"
            "    gl_PointSize = uPointSize;                                 \n"
            "    gl_Position  = uProjection * uModelview * aPosition;       \n"
            "    if (1.0 == uPattOn) {                                      \n"
            "        v_texCoord.x = (aPosition.x - uPattGridX) / uPattW;    \n"
            "        v_texCoord.y = (aPosition.y - uPattGridY) / uPattH;    \n"
            "    } else {                                                   \n"
            "        v_texCoord = aUV;                                      \n"
            "    }                                                          \n"
            "}                                                              \n";

        _vertexShader = _loadShader(GL_VERTEX_SHADER, vertSrc);

        // ----------------------------------------------------------------------

        PRINTF("DEBUG: GL_FRAGMENT_SHADER\n");

/*
#ifdef S52_USE_TEGRA2
        // FIXME: does this really help with blending on a TEGRA2
#define BLENDFUNC #pragma profilepragma blendoperation(gl_FragColor, GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
#else
#define BLENDFUNC
#endif
*/


/*
//#if (defined(S52_USE_GL2) && defined(S52_USE_MESA3D))
#ifdef S52_USE_GL2
//#ifdef S52_USE_MESA3D         // to get gl_PointCoord when s52_use_afterglow
            "#version 120                               \n"
//            "#version 100                               \n"
#endif
*/

        static const char fragSrc[] =
#ifdef S52_USE_MESA3D         // to get gl_PointCoord when s52_use_afterglow
            //"#version 120                               \n"
            //"#version 100                               \n"
#endif

#ifdef S52_USE_GLES2
            //"precision lowp float;                      \n"
            "precision mediump float;                   \n"
#endif
            "uniform sampler2D uSampler2d;              \n"
            "uniform float     uFlatOn;                 \n"
            "uniform float     uBlitOn;                 \n"
            "uniform float     uStipOn;                 \n"
            "uniform float     uPattOn;                 \n"
            "uniform float     uGlowOn;                 \n"

            //"uniform float     uFxAAOn;                 \n"

            "uniform vec4      uColor;                  \n"

            "varying vec2      v_texCoord;              \n"
            "varying float     v_alpha;                 \n"

            // NOTE: if else if ... doesn't seem to slow things down
            "void main(void)                            \n"
            "{                                          \n"
            "    if (1.0 == uBlitOn) {                  \n"
            "        gl_FragColor = texture2D(uSampler2d, v_texCoord);               \n"
            "    } else {                                                            \n"
            // Note: uStipOn and uPattOn same - diff might be usefull later on ..
            "        if (1.0 == uStipOn) {                                           \n"
            "            gl_FragColor = texture2D(uSampler2d, v_texCoord);           \n"
            "            gl_FragColor.rgb = uColor.rgb;                              \n"
            "        } else {                                                        \n"
            "            if (1.0 == uPattOn) {                                       \n"
            "                gl_FragColor = texture2D(uSampler2d, v_texCoord);       \n"
            "                gl_FragColor.rgb = uColor.rgb;                          \n"
            "            } else {                                                    \n"
#ifdef S52_USE_AFGLOW
            "                if (0.0 < uGlowOn) {                                    \n"
            "                    float dist = distance(vec2(0.5,0.5), gl_PointCoord);\n"
            "                    if (0.5 < dist) {                                   \n"
            "                        discard;                                        \n"
            "                    } else {                                            \n"
            "                        gl_FragColor   = uColor;                        \n"
            "                        gl_FragColor.a = v_alpha;                       \n"
            "                    }                                                   \n"
            "                } else                                                  \n"
#endif
            "                {                          \n"
            "                    gl_FragColor = uColor; \n"
            "                }                          \n"
            "            }                              \n"
            "        }                                  \n"
            "    }                                      \n"
            "}                                          \n";

        _fragmentShader = _loadShader(GL_FRAGMENT_SHADER, fragSrc);


        // ----------------------------------------------------------------------

        if ((0==_programObject) || (0==_vertexShader) || (0==_fragmentShader)) {
            PRINTF("ERROR: problem loading shaders and/or creating program\n");
            g_assert(0);
            return FALSE;
        }
        _checkError("_init_es2() -0-");

        if (TRUE != glIsShader(_vertexShader)) {
            PRINTF("ERROR: glIsShader(_vertexShader) failed\n");
            g_assert(0);
            return FALSE;
        }
        if (TRUE != glIsShader(_fragmentShader)) {
            PRINTF("ERROR: glIsShader(_fragmentShader) failed\n");
            g_assert(0);
            return FALSE;
        }

        _checkError("_init_es2() -1-");

        glAttachShader(_programObject, _vertexShader);
        glAttachShader(_programObject, _fragmentShader);
        glLinkProgram (_programObject);
        glGetProgramiv(_programObject, GL_LINK_STATUS, &linked);
        if (GL_FALSE == linked){
            GLsizei length;
            GLchar  infoLog[2048];

            glGetProgramInfoLog(_programObject,  2048, &length, infoLog);
            PRINTF("problem linking program:%s", infoLog);


            g_assert(0);
            return FALSE;
        }

        _checkError("_init_es2() -2-");

        //use the program
        glUseProgram(_programObject);


        _checkError("_init_es2() -3-");
    }


    //load all attributes
    //FIXME: move to bindShaderAttrib();
    _aPosition   = glGetAttribLocation(_programObject, "aPosition");
    _aUV         = glGetAttribLocation(_programObject, "aUV");
    _aAlpha      = glGetAttribLocation(_programObject, "aAlpha");

    //FIXME: move to bindShaderUnifrom();
    _uProjection = glGetUniformLocation(_programObject, "uProjection");
    _uModelview  = glGetUniformLocation(_programObject, "uModelview");
    _uColor      = glGetUniformLocation(_programObject, "uColor");
    _uPointSize  = glGetUniformLocation(_programObject, "uPointSize");
    _uSampler2d  = glGetUniformLocation(_programObject, "uSampler2d");

    _uBlitOn     = glGetUniformLocation(_programObject, "uBlitOn");
    _uStipOn     = glGetUniformLocation(_programObject, "uStipOn");
    _uGlowOn     = glGetUniformLocation(_programObject, "uGlowOn");

    _uPattOn     = glGetUniformLocation(_programObject, "uPattOn");
    _uPattGridX  = glGetUniformLocation(_programObject, "uPattGridX");
    _uPattGridY  = glGetUniformLocation(_programObject, "uPattGridY");
    _uPattW      = glGetUniformLocation(_programObject, "uPattW");
    _uPattH      = glGetUniformLocation(_programObject, "uPattH");


    //  init matrix stack
    memset(_mvm, 0, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);
    memset(_pjm, 0, sizeof(GLfloat) * 16 * MATRIX_STACK_MAX);

    //  init matrix
    _glMatrixMode  (GL_PROJECTION);
    _glLoadIdentity(GL_PROJECTION);

    _glMatrixMode  (GL_MODELVIEW);
    _glLoadIdentity(GL_MODELVIEW);

    //clear FB ALPHA before use, also put blue but doen't show up unless startup bug
    //glClearColor(0, 0, 1, 1);     // blue
    glClearColor(1.0, 0.0, 0.0, 1.0);     // red
    //glClearColor(1.0, 0.0, 0.0, 0.0);     // red
    //glClearColor(0.0, 0.0, 0.0, 0.0);

#ifdef S52_USE_TEGRA2
    // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase), define in gl2ext.h
    //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
    glClear(GL_COLOR_BUFFER_BIT | GL_COVERAGE_BUFFER_BIT_NV);
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    // setup mem buffer to save FB to
    glGenTextures(1, &_fb_texture_id);
    glBindTexture  (GL_TEXTURE_2D, _fb_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    //_checkError("_init_es2() -4-");

#ifdef S52_USE_TEGRA2
    // Note: _fb_pixels must be in sync with _fb_format

    // RGBA
    //glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGBA, _vp[2], _vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, _fb_pixels);
    glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGBA, _vp.w, _vp.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    // RGB
    //glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGB,  _vp[2], _vp[3], 0, GL_RGB,  GL_UNSIGNED_BYTE, _fb_pixels);
    //glTexImage2D   (GL_TEXTURE_2D, 0, GL_RGB,  _vp[2], _vp[3], 0, GL_RGB,  GL_UNSIGNED_BYTE, 0);

    //glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, _vp[2], _vp[3], 0);
    //glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  0, 0, _vp[2], _vp[3], 0);
#else
    //glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, _vp[2], _vp[3], 0);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, _vp.w, _vp.h, 0);
#endif

    //_checkError("_init_es2() -5-");

    glBindTexture  (GL_TEXTURE_2D, 0);

    _checkError("_init_es2() -fini-");

    return TRUE;
}

static int       _renderTile(S52_DList *DListData)
{
    glUniformMatrix4fv(_uModelview,  1, GL_FALSE, _mvm[_mvmTop]);

    glEnableVertexAttribArray(_aPosition);

    for (guint i=0; i<DListData->nbr; ++i) {
        guint j     = 0;
        GLint mode  = 0;
        GLint first = 0;
        GLint count = 0;

        // debug: how can this be !?
        if (NULL == DListData->prim[i])
            continue;

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

    _checkError("_renderTile() -0-");

    return TRUE;
}

static int       _initFBO(GLuint mask_texID)
{
    if (0 == _fboID) {
        glGenFramebuffers (1, &_fboID);
    }

    glBindFramebuffer     (GL_FRAMEBUFFER, _fboID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_texID, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        PRINTF("ERROR: glCheckFramebufferStatus() fail, status: %i\n", status);

        //*
        switch(status)
        {
        case GL_FRAMEBUFFER_UNSUPPORTED:
            PRINTF("Framebuffer object format is unsupported by the video hardware. (GL_FRAMEBUFFER_UNSUPPORTED)(FBO - 820)");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            PRINTF("Incomplete attachment. (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)(FBO - 820)");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            PRINTF("Incomplete missing attachment. (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT)(FBO - 820)");
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
            PRINTF("Some video driver error or programming error occured. Framebuffer object status is invalid. (FBO - 823)");
            break;
        }

        return FALSE;
    }

    _checkError("_setFBO()");

    return TRUE;
}

static int       _set_glScaled(void)
{
    // sailsafe
    double scaleX = _dotpitch_mm_x;
    double scaleY = _dotpitch_mm_y;


    ////////////////////////////////////////////////////////////////
    //
    // FIXME: scale found by trial and error
    //        FIME: should get pixel Resolution programmaticaly
    //

    // FIXME: why -Y? (flip Y !!)
#ifdef S52_USE_TEGRA2
    // Xoom - S52_MAR_DOTPITCH_MM set to 0.3
    scaleX = S52_MP_get(S52_MAR_DOTPITCH_MM_X)/ 8.0;
    scaleY = S52_MP_get(S52_MAR_DOTPITCH_MM_Y)/-8.0;
#endif

#ifdef S52_USE_ADRENO
    // Nexus 7 (2013) - 323ppi landscape -
    //scaleX = S52_MP_get(S52_MAR_DOTPITCH_MM_X)/ 1.0;
    //scaleY = S52_MP_get(S52_MAR_DOTPITCH_MM_Y)/-1.0;

    // Nexus 7 (2013) - 323ppi landscape - S52_MAR_DOTPITCH_MM set to 0.2
    scaleX = S52_MP_get(S52_MAR_DOTPITCH_MM_X)/ 4.0;  // 4 or 5 OK
    scaleY = S52_MP_get(S52_MAR_DOTPITCH_MM_Y)/-4.0;  // 4 or 5 OK
#endif

#ifdef S52_USE_MESA3D
    scaleX = S52_MP_get(S52_MAR_DOTPITCH_MM_X)/ 8.0;
    scaleY = S52_MP_get(S52_MAR_DOTPITCH_MM_Y)/-8.0;
#endif

    _glScaled(scaleX, scaleY, 1.0);

    ////////////////////////////////////////////////////////////////

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

#if 0
static int       next_power_of_two(int v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    return v;
}

static int       is_power_of_two(guint v)
{
    if (v == 0)
        return TRUE;

    return (v & (v-1)) == 0;
}

static guint     g_nearest_pow(gint num)
// lifted from glib garray.c
/* Returns the smallest power of 2 greater than n, or n if
 * such power does not fit in a guint
 */
{
  guint n = 1;

  //while (n<num && n>0)
  while (0<n && n<num)
    n <<= 1;

  return n ? n : num;
}
#endif

static int       _renderTexure(S52_obj *obj, double tileWpx, double tileHpx, double stagOffsetPix)
{
    GLuint mask_texID = 0;

    glGenTextures(1, &mask_texID);
    glBindTexture(GL_TEXTURE_2D, mask_texID);

    // GL_OES_texture_npot
    // The npot extension for GLES2 is only about support of mipmaps and repeat/mirror wrap modes.
    // If you don't care about mipmaps and use only the clamp wrap mode, you can use npot textures.
    // It's part of the GLES2 spec.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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

    // NOTE: GL_RGBA is needed for:
    // - Vendor: Tungsten Graphics, Inc. - Renderer: Mesa DRI Intel(R) 965GM x86/MMX/SSE2
    // - Vendor: Qualcomm                - Renderer: Adreno (TM) 320
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w,   h,   0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


    _initFBO(mask_texID);

    // save texture mask ID when everythings check ok
    S52_PL_setAPtexID(obj, mask_texID);

    // Clear Color ------------------------------------------------
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0, 0.0, 0.0, 0.0);

#ifdef S52_USE_TEGRA2
    // xoom specific - clear FB to reset Tegra 2 CSAA (anti-aliase), define in gl2ext.h
    //int GL_COVERAGE_BUFFER_BIT_NV = 0x8000;
    glClear(GL_COLOR_BUFFER_BIT | GL_COVERAGE_BUFFER_BIT_NV);
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    // set color alpha
    glUniform4f(_uColor, 0.0, 0.0, 0.0, 1.0);

    _checkError("_setTexture() -1-");


    // render to texture -----------------------------------------

    {   // set line/point width
        double   dummy = 0.0;
        char     pen_w = '1';
        S52_PL_getLCdata(obj, &dummy, &pen_w);

        //_glLineWidth(pen_w - '0');
        _glLineWidth(pen_w - '0' + 1.0);  // must enlarge line glsl sampler
        _glPointSize(pen_w - '0' + 1.0);  // sampler + AA soften pixel, so need enhencing a bit
    }

    /* debug - draw X and Y axis
    {
        // Note: line in X / Y need to start at +1 to show up
#ifdef S52_USE_TEGRA2
        pt3v lineW[2] = {{1.0, 1.0, 0.0}, {potW,  1.0, 0.0}};
        pt3v lineH[2] = {{1.0, 1.0, 0.0}, { 1.0, potH, 0.0}};
#else
        pt3v lineW[2] = {{1.0, 1.0, 0.0}, {tileWpx,     1.0, 0.0}};
        pt3v lineH[2] = {{1.0, 1.0, 0.0}, {    1.0, tileHpx, 0.0}};
#endif

        _DrawArrays_LINE_STRIP(2, (vertex_t*)lineW);
        _DrawArrays_LINE_STRIP(2, (vertex_t*)lineH);
    }
    //*/

    /* debug
    double tw = 0.0;  // tile width
    double th = 0.0;  // tile height
    double dx = 0.0;  // run length offset for STG pattern
    S52_PL_getAPTileDim(obj, &tw,  &th,  &dx);

    PRINTF("Tile   : %6.1f x %6.1f\n", tw,        th       );
    PRINTF("pivot  : %6.1f x %6.1f\n", px,        py       );
    PRINTF("bbox   : %6.1f x %6.1f\n", bbx,       bby      );
    PRINTF("off    : %6.1f x %6.1f\n", (px-bbx),  (py-bby) );
    PRINTF("off  px: %6.1f x %6.1f\n", offsetXpx, offsetYpx);
    PRINTF("Tile px: %6.1f x %6.1f\n", tileWpx,   tileHpx  );

    PRCARE  : tex: 32 x 32,   frac: 0.591071 x 0.592593
    Tile    :  500.0 x  500.0
    pivot   :  750.0 x  750.0
    bboxOrig:  750.0 x  250.0
    offset  :    0.0 x  500.0
    offsetpx:    0.0 x   19.0
    Tile px :   18.9 x   19.0

    PATD   55DIAMOND1VLINCON0000000000022500225002250043130112500093
    DEPARE  : tex: 128 x 256, frac: 0.664955 x 0.638963
    Tile    : 2250.0 x 4313.0
    pivot   : 2250.0 x 2250.0
    bboxOrig: 1125.0 x   93.0
    offset  : 1125.0 x 2157.0
    offsetpx:   42.6 x   81.8
    Tile px :   85.1 x  163.6

    DRGARE  : tex: 16 x 16,   frac: 0.827500 x 0.829630
    Tile    :  350.0 x  350.0
    pivot   : 1500.0 x 1500.0
    bboxOrig: 1500.0 x 1300.0
    offset  :    0.0 x  200.0
    offsetpx:    0.0 x    7.6
    Tile px :   13.2 x   13.3
    */
    _glMatrixSet(VP_WIN);


    _glTranslated(tileWpx/2.0 - 0.0, tileHpx/2.0 - 0.0, 0.0);
    //_glTranslated(tileWpx/2.0 - 3.0, tileHpx/2.0 - 3.0, 0.0);
    //_glTranslated(tileWpx/2.0 - 5.0, tileHpx/2.0 - 5.0, 0.0);
    //_glTranslated(tileWpx/2.0 - 10.0, tileHpx/2.0 - 10.0, 0.0);



    _set_glScaled();

    S52_DList *DListData = S52_PL_getDListData(obj);
    _renderTile(DListData);

    if (0.0 != stagOffsetPix) {
        _glLoadIdentity(GL_MODELVIEW);

        if (TRUE == _GL_OES_texture_npot) {
            _glTranslated(tileWpx + stagOffsetPix, tileHpx + (tileHpx/2.0), 0.0);
        } else {
            _glTranslated((w/2.0) + stagOffsetPix, (h/2.0), 0.0);
        }

        _set_glScaled();

        _renderTile(DListData);
    }

    _glMatrixDel(VP_WIN);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // debug - test to get rid of artefact at start up
    //glDeleteFramebuffers(1, &_fboID);
    //_fboID = 0;

    _checkError("_setTexture() -1-");

    return mask_texID;
}

static int       _renderAP_gl2(S52_obj *obj)
{
    S52_DList *DListData = S52_PL_getDListData(obj);

    double x1, y1;   // LL of region of area in world
    double x2, y2;   // UR of region of area in world
    double tileWpx;
    double tileHpx;
    double stagOffsetPix = _getGridRef(obj, &x1, &y1, &x2, &y2, &tileWpx, &tileHpx);
    double tileWw = tileWpx * _scalex;
    double tileHw = tileHpx * _scaley;

    GLuint mask_texID = S52_PL_getAPtexID(obj);
    if (0 == mask_texID) {
        if (TRUE == glIsEnabled(GL_SCISSOR_TEST)) {
            // scissor box interfere with texture creation
            glDisable(GL_SCISSOR_TEST);
            mask_texID = _renderTexure(obj, tileWpx, tileHpx, stagOffsetPix);
            glEnable(GL_SCISSOR_TEST);
        } else {
            mask_texID = _renderTexure(obj, tileWpx, tileHpx, stagOffsetPix);
        }
    }

    _setFragColor(DListData->colors);

    // debug - red conspic
    //glUniform4f(_uColor, 1.0, 0.0, 0.0, 0.0);

    _glUniformMatrix4fv_uModelview();

    glUniform1f(_uPattOn,    1.0);
    // make no diff on MESA/gallium if it is 0.0 but not on Xoom (tegra2)
    glUniform1f(_uPattGridX, x1);
    glUniform1f(_uPattGridY, y1);

    glUniform1f(_uPattW,     tileWw);        // tile width in world
    glUniform1f(_uPattH,     tileHw);        // tile height in world

    glBindTexture(GL_TEXTURE_2D, mask_texID);

    _fillArea(S52_PL_getGeo(obj));

    glBindTexture(GL_TEXTURE_2D, 0);

    glUniform1f(_uPattOn,    0.0);
    glUniform1f(_uPattGridX, 0.0);
    glUniform1f(_uPattGridY, 0.0);
    glUniform1f(_uPattW,     0.0);
    glUniform1f(_uPattH,     0.0);

    _checkError("_renderAP_es2() -2-");

    return TRUE;
}
