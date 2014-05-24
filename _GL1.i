// _GL1.i: definition & declaration for S52GL1.x
//
// SD 2014MAY20


// NOTE: rubber band in GL1.x
// glEnable(GL_COLOR_LOGIC_OP);
// glLogicOp(GL_XOR);


#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include "GL/glext.h"

#include <GL/glu.h>

// alpha is 0.0 - 255.0 in GL1.x
#define TRNSP_FAC   255 * 0.25

// experiment OpenGL ES SC
// Note: ES SC has some GLES2.x and GL1.x features
#ifdef S52_USE_OPENGL_SC
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

#else   // S52_USE_OPENGL_SC

#define _glScaled            glScaled
#define _glRotated           glRotated
#define _glTranslated        glTranslated

static GLdouble _mvm[16];       // modelview  matrix used in _win2prj / _prj2win
static GLdouble _pjm[16];       // projection matrix used in _win2prj / _prj2win
#define GL_DBL_FLT          GL_DOUBLE

#endif  // S52_USE_OPENGL_SC


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
static double      _getGridRef(S52_obj *, double *, double *, double *, double *, double *, double *);
static int         _fillarea(S57_geo *);
static int         _glCallList(S52_DListData *);
static GLubyte     _glColor4ub(S52_Color *);
static int         _pushScaletoPixel(int);
static int         _popScaletoPixel(void);
static inline void _checkError(const char *);
////////////////////////////////////////////////////////

static int       _renderAP_NODATA_gl1(S52_obj *obj)
{
    S57_geo       *geoData   = S52_PL_getGeo(obj);
    S52_DListData *DListData = S52_PL_getDListData(obj);

    if (NULL != DListData) {
        S52_Color *col = DListData->colors;
        _glColor4ub(col);

        glEnable(GL_POLYGON_STIPPLE);
        glPolygonStipple(_nodata_mask);

        _fillarea(geoData);

        glDisable(GL_POLYGON_STIPPLE);

        return TRUE;
    }

    return FALSE;
}

static int       _renderAP_DRGARE_gl1(S52_obj *obj)
{
    if (TRUE != (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
        return TRUE;

    S57_geo       *geoData   = S52_PL_getGeo(obj);
    S52_DListData *DListData = S52_PL_getDListData(obj);

    if (NULL != DListData) {
        S52_Color *col = DListData->colors;


        _glColor4ub(col);

        glEnable(GL_POLYGON_STIPPLE);
        glPolygonStipple(_drgare_mask);

        _fillarea(geoData);

        glDisable(GL_POLYGON_STIPPLE);
        return TRUE;
    }

    return FALSE;
}

static int       _renderAP_gl1(S52_obj *obj)
{
    // broken on GL1
    return TRUE;

    //--------------------------------------------------------
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

    //--------------------------------------------------------

    //*
    // TODO: optimisation: if proven to be faster, compare S57 object number instead of string name
    if (0 == g_strcmp0("DRGARE", S52_PL_getOBCL(obj))) {

        if (TRUE == (int) S52_MP_get(S52_MAR_DISP_DRGARE_PATTERN))
            _renderAP_DRGARE_gl1(obj);

        //PRINTF("FIXME: \n");

        return TRUE;
    } else {
        // fill area with NODATA pattern
        if (0==g_strcmp0("UNSARE", S52_PL_getOBCL(obj)) ) {
            _renderAP_NODATA_gl1(obj);
            return TRUE;
        } else {
            // fill area with OVERSC01
            if (0==g_strcmp0("M_COVR", S52_PL_getOBCL(obj)) ) {
                //_renderAP_NODATA(obj);
                return TRUE;
            } else {
                // fill area with
                if (0==g_strcmp0("M_CSCL", S52_PL_getOBCL(obj)) ) {
                    //_renderAP_NODATA(obj);
                    return TRUE;
                } else {
                    // fill area with
                    if (0==g_strcmp0("M_QUAL", S52_PL_getOBCL(obj)) ) {
                        //_renderAP_NODATA(obj);
                        return TRUE;
                    }
                }
            }
        }
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
    S52_DListData *DListData = S52_PL_getDListData(obj);
    S57_geo *geoData = S52_PL_getGeo(obj);
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

    double x1, y1;   // LL of region of area
    double x2, y2;   // UR of region of area
    double tileWidthPix;
    double tileHeightPix;
    double stagOffsetPix = _getGridRef(obj, &x1, &y1, &x2, &y2, &tileWidthPix, &tileHeightPix);
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


    // NOTE: pattern that do not fit entirely inside an area
    // are displayed  (hence pattern are clipped) because ajacent area
    // filled with same pattern will complete the clipped pattern.
    // No test y+th<y2 and x+tw<x2 to check for the end of a row/collum.
    //d = 0.0;

    //
    // FIXME: GL1.5 texture
    //
    //*
    int npatt = 0;  // stat
    int stag  = 0;  // 0-1 true/false add dx for stagged pattern
    double ww = tileWidthPix  * _scalex;  // pattern width in world
    double hw = tileHeightPix * _scaley;  // pattern height in world
    double d  = stagOffsetPix * _scalex;  // stag offset in world

    glMatrixMode(GL_MODELVIEW);

    for (double y=y1; y<=y2; y+=ww) {
        glLoadIdentity();   // reset to screen origin
        glTranslated(x1 + (d*stag), y, 0.0);
        glScaled(1.0, -1.0, 1.0);

        _pushScaletoPixel(TRUE);
        for (double x=x1; x<x2; x+=hw) {
            _glCallList(DListData);
            glTranslated(ww, 0.0, 0.0);
            ++npatt;
        }
        _popScaletoPixel();
        stag = !stag;
    }

    // debug
    //char *name = S52_PL_getOBCL(obj);
    //PRINTF("nbr of tile (%s): %i-------------------\n", name, npatt);

    glDisable(GL_STENCIL_TEST);

    // this turn off blending from display list
    //_setBlend(FALSE);
    //*/

    _checkError("_renderAP()");

    return TRUE;
}
