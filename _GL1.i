// _GL1.i: definition & declaration for S52GL1.x. Link to libGL.so.
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




// Note: rubber band in GL1.x
// glEnable(GL_COLOR_LOGIC_OP);
// glLogicOp(GL_XOR);


#include <GL/gl.h>
#include "GL/glext.h"
#include <GL/glu.h>

// add missing def for MINGW
#ifdef _MINGW
#define GL_ARRAY_BUFFER                   0x8892
#define GL_STATIC_DRAW                    0x88E4
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
#endif

// alpha is 0.0 - 255.0 in GL1.x
#define TRNSP_FAC   255 * 0.25

// experiment OpenGL ES SC
// Note: ES SC has some GLES2.x and GL1.x features
#ifdef S52_USE_GLSC1
#include "GL/es_sc_gl.h"
typedef GLfloat GLdouble;
#define glScaled            glScalef
#define glRotated           glRotatef
#define glTranslated        glTranslatef
#define GL_COMPILE          0x1300
#define GL_UNSIGNED_INT     0x1405  // byte size of a dispaly list
// WARNING: matrix is in integer (GLint)
static GLint    _mvm[16];       // OpenGL ES SC
static GLint    _pjm[16];       // OpenGL ES SC
#define GL_DBL_FLT          GL_FLOAT

#else   // S52_USE_GLSC1

#define _glScaled           glScaled
#define _glRotated          glRotated
#define _glTranslated       glTranslated

static GLdouble _mvm[16];   // modelview  matrix used in _win2prj / _prj2win
static GLdouble _pjm[16];   // projection matrix used in _win2prj / _prj2win
#define GL_DBL_FLT          GL_DOUBLE

#endif  // S52_USE_GLSC1


//---- PATTERN GL1 -----------------------------------------------------------
// DRGARE01
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

static const GLubyte _nodata_mask[4*32] = {
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

//---- PATTERN GL1 -----------------------------------------------------------

////////////////////////////////////////////////////////
// forward decl
static double      _getWorldGridRef(S52_obj *, double *, double *, double *, double *, double *, double *);
static int         _fillArea(S57_geo *);
static int         _glCallList(S52_DListData *);
static GLubyte     _setFragAttrib(S52_Color *, gboolean);
static int         _pushScaletoPixel(int);
static int         _popScaletoPixel(void);
static inline void _checkError(const char *);
////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////
//
// FONTS
//

#ifdef S52_USE_GLC
#include <GL/glc.h>
static GLint     _GLCctx;
static GLint     _initGLC(void)
{
    GLint font;
    static int first = FALSE;

    if (first)
        return TRUE;
    first = TRUE;

    _GLCctx = glcGenContext();

    glcContext(_GLCctx);

    // dot pitch mm to dpi
    //double dpi = MM2INCH / _dotpitch_mm_y;  // dot per inch
    double dpi = MM2INCH * _dotpitch_mm_y;  // dot per inch
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

    for (int i = 0; i < count; i++) {
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
#include <FTGL/ftgl.h>
static FTGLfont *_ftglFont[S52_MAX_FONT];
static GLint     _initFTGL(void)
{
    //const char *file = "arial.ttf";
    //const char *file = "DejaVuSans.ttf";
    //const char *file = "Trebuchet_MS.ttf";
    const gchar *file = "Waree.ttf";
    // from Navit
    //const char *file = "LiberationSans-Regular.ttf";


    if (FALSE == g_file_test(file, G_FILE_TEST_EXISTS)) {
        PRINTF("WARNING: font file not found (%s)\n", file);
        return FALSE;
    }

    _ftglFont[0] = ftglCreatePixmapFont(file);
    _ftglFont[1] = ftglCreatePixmapFont(file);
    _ftglFont[2] = ftglCreatePixmapFont(file);
    _ftglFont[3] = ftglCreatePixmapFont(file);

    //ftglSetFontFaceSize(_ftglFont[0], 12, 12);
    //ftglSetFontFaceSize(_ftglFont[1], 14, 14);
    //ftglSetFontFaceSize(_ftglFont[2], 16, 16);
    //ftglSetFontFaceSize(_ftglFont[3], 20, 20);

    //*
    // dot pitch mm to dpi
    //int dpi = 72;
    int dpi = MM2INCH * _dotpitch_mm_y;

    //int basePtSz = 12.0 / (S52_MP_get(S52_MAR_DOTPITCH_MM_Y) / 0.3);
    int basePtSz = 10 * (PICA / S52_MP_get(S52_MAR_DOTPITCH_MM_Y));

    // FIXME: check return -> TRUE
    ftglSetFontFaceSize(_ftglFont[0], basePtSz + 0, dpi);
    ftglSetFontFaceSize(_ftglFont[1], basePtSz + 2, dpi);
    ftglSetFontFaceSize(_ftglFont[2], basePtSz + 4, dpi);
    ftglSetFontFaceSize(_ftglFont[3], basePtSz + 8, dpi);
    //*/

    /*
    // dpi has no effect!
    //int dpi = 72;
    int dpi = 400;
    ftglSetFontFaceSize(_ftglFont[0], 12, dpi);
    ftglSetFontFaceSize(_ftglFont[1], 14, dpi);
    ftglSetFontFaceSize(_ftglFont[2], 16, dpi);
    ftglSetFontFaceSize(_ftglFont[3], 20, dpi);
    //*/

    return TRUE;
}
#endif

#ifdef S52_USE_COGL
#include "cogl-pango/cogl-pango.h"
static PangoFontDescription *_PangoFontDesc  = NULL;
static PangoFontMap         *_PangoFontMap   = NULL;
static PangoContext         *_PangoCtx       = NULL;
static PangoLayout          *_PangoLayout    = NULL;
static int       _initCOGL(void)
{
    // Setup a Pango font map and context
    _PangoFontMap = cogl_pango_font_map_new();

    cogl_pango_font_map_set_use_mipmapping(COGL_PANGO_FONT_MAP(_PangoFontMap), TRUE);

    _PangoCtx = cogl_pango_font_map_create_context(COGL_PANGO_FONT_MAP(_PangoFontMap));

    _PangoFontDesc = pango_font_description_new();
    pango_font_description_set_family(_PangoFontDesc, "DroidSans");
    pango_font_description_set_size(_PangoFontDesc, 30 * PANGO_SCALE);

    // Setup the "Hello Cogl" text
    _PangoLayout = pango_layout_new(_PangoCtx);
    pango_layout_set_font_description(_PangoLayout, _PangoFontDesc);
    pango_layout_set_text(_PangoLayout, "Hello Cogl", -1);

    PangoRectangle hello_label_size;
    pango_layout_get_extents(_PangoLayout, NULL, &hello_label_size);
    int hello_label_width  = PANGO_PIXELS(hello_label_size.width);
    int hello_label_height = PANGO_PIXELS(hello_label_size.height);

    return TRUE;
}
#endif

#ifdef S52_USE_OPENGL_VBO
static int       _DrawArrays(S57_prim *prim)
{
    guint     primNbr = 0;
    vertex_t *vert    = NULL;
    guint     vertNbr = 0;      // dummy
    guint     vboID   = 0;      // dummy

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

static guint     _createDList(S57_prim *prim)
// create display list
{
    guint DList = 0;
    DList = glGenLists(1);
    if (0 == DList) {
        PRINTF("WARNING: glGenLists() failed\n");
        g_assert(0);
        return FALSE;
    }

    glNewList(DList, GL_COMPILE);

    _DrawArrays(prim);

    glEndList();

    S57_setPrimDList(prim, DList);

    _checkError("_createDList()");

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
#endif  // !S52_USE_OPENGL_VBO

static int       _renderAP_mask_gl1(S52_obj *obj, const GLubyte *mask)
{
    S57_geo       *geo       = S52_PL_getGeo(obj);
    S52_DListData *DListData = S52_PL_getDListData(obj);

    if (NULL != DListData) {
        S52_Color *col = DListData->colors;
        _setFragAttrib(col, S57_getHighlight(geo));

        glEnable(GL_POLYGON_STIPPLE);
        //glPolygonStipple(_drgare_mask);
        glPolygonStipple(mask);

        _fillArea(geo);

        glDisable(GL_POLYGON_STIPPLE);

        return TRUE;
    }

    return FALSE;
}
static int       _renderAP_gl1(S52_obj *obj)
{
    // broken on GL1
    //return TRUE;

    //--------------------------------------------------------
    // debug - U pattern
    //if (0 != g_strcmp0("M_QUAL", S52_PL_getOBCL(obj)) ) {
    ////    //_renderAP_NODATA(obj);
    //    return TRUE;
    //}
    //char *name = S52_PL_getOBCL(obj);
    //PRINTF("%s: ----------------\n", name);
    //if (0==g_strcmp0("M_QUAL", S52_PL_getOBCL(obj)) ) {
    //    PRINTF("M_QUAL found\n");
    //}
    //return 1;

    //--------------------------------------------------------

    //*
    // optimisation: if proven to be faster, compare S57 object number instead of string name
    if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj))) {

        if (TRUE == (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN)) {
            //_renderAP_DRGARE_gl1(obj);
            _renderAP_mask_gl1(obj, _drgare_mask);
        }

        //PRINTF("FIXME: \n");

        return TRUE;
    } else {
        // fill area with NODATA pattern
        if (0==g_strcmp0("UNSARE", S52_PL_getOBCL(obj)) ) {
            //_renderAP_NODATA_gl1(obj);
            _renderAP_mask_gl1(obj, _nodata_mask);
            return TRUE;
        }
        /*
        else {
            // fill area with OVERSC01
            if (0==g_strcmp0("M_COVR", S52_PL_getOBCL(obj)) ) {
                //_renderAP_NODATA(obj);
                return TRUE;
            } else {
                // fill area with
                if (0==g_strcmp0("M_CSCL", S52_PL_getOBCL(obj)) ) {
                    //_renderAP_NODATA(obj);
                    return TRUE;
                }
            }
        }
        */
    }

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
        S57_geo *geo = S52_PL_getGeo(obj);
        _fillArea(geo);

        // setup stencil to clip pattern
        // all color to pass stencil filter
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

        // clip pattern pixel that lie outside of poly --clip if != 1
        glStencilFunc(GL_EQUAL, 0x1, 0x1);

        // freeze stencil state
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }

    double x1, y1;   // LL of region of area
    double x2, y2;   // UR of region of area
    double tileWidthPix;
    double tileHeightPix;
    double stagOffsetPix = _getWorldGridRef(obj, &x1, &y1, &x2, &y2, &tileWidthPix, &tileHeightPix);
    //PRINTF("PIXEL: tileW:%f tileH:%f\n", tileWidthPix, tileHeightPix);

    /*
    {   // invariant: just to be sure that things don't explode
        // the number of tile in pixel is proportional to the number
        // of tile visible in world coordinate
        GLdouble tileNbrX = (_vp[2] - _vp[0]) / tileWidthPix;
        GLdouble tileNbrY = (_vp[3] - _vp[1]) / tileHeightPix;
        GLdouble tileNbrX = _vp[2] / tileWidthPix;
        GLdouble tileNbrY = _vp[3] / tileHeightPix;
        GLdouble tileNbrU = (x2-x1) / w;
        GLdouble tileNbrV = (y2-y1) / h;

        // debug
        //PRINTF("TILE nbr: Pix X=%f Y=%f (X*Y=%f) World U=%f V=%f\n", tileNbrX,tileNbrY,tileNbrX*tileNbrY,tileNbrU,tileNbrV);
        //PRINTF("WORLD: width: %f height: %f tileW: %f tileH: %f\n", (x2-x1), (y2-y1), w, h);
        //PRINTF("PIXEL: width: %i height: %i tileW: %f tileH: %f\n", (_vp[2] - _vp[0]), (_vp[3] - _vp[1]), tileWidthPix, tileHeightPix);

        if (tileNbrX + 4 < tileNbrU)
            g_assert(0);
        if (tileNbrY + 4 < tileNbrV)
            g_assert(0);
    }
    */


    // Note: pattern that do not fit entirely inside an area
    // are displayed  (hence pattern are clipped) because ajacent area
    // filled with same pattern will complete the clipped pattern.
    // No test y+th<y2 and x+tw<x2 to check for the end of a row/collum.

    int npatt = 0;  // stat
    int stag  = 0;  // 0-1 true/false add dx for stagged pattern
    double ww = tileWidthPix  * _scalex;  // pattern width in world
    double hw = tileHeightPix * _scaley;  // pattern height in world
    double d  = stagOffsetPix * _scalex;  // stag offset in world

    S52_DListData *DListData = S52_PL_getDListData(obj);

    glMatrixMode(GL_MODELVIEW);

    for (double y=y1; y<=y2; y+=hw) {
        glLoadIdentity();   // reset to screen origin
        glTranslated(x1 + (d*stag), y, 0.0);
        glScaled(1.0, -1.0, 1.0);

        for (double x=x1; x<x2; x+=ww) {
            _pushScaletoPixel(TRUE);
            _glCallList(DListData);
            _popScaletoPixel();
            glTranslated(ww, 0.0, 0.0);
            ++npatt;
        }
        stag = !stag;
    }

    // debug
    //char *name = S52_PL_getOBCL(obj);
    //PRINTF("nbr of tile (%s): %i-------------------\n", name, npatt);

    glDisable(GL_STENCIL_TEST);

    // this turn off blending from display list
    //_setBlend(FALSE);

    _checkError("_renderAP()");

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
