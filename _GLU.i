// _GLU.i: OpenGL Utility
//
// SD 2014MAY29


//////////////////////////////////////////////////////
//
// tessallation
// mingw specific, with gcc APIENTRY expand to nothing
#ifdef _MINGW
#define _CALLBACK __attribute__ ((__stdcall__))
//#define _CALLBACK
#else
#define _CALLBACK
#endif

#define void_cb_t GLvoid _CALLBACK

typedef void (_CALLBACK *f)    ();
typedef void (_CALLBACK *fint) (GLint);
typedef void (_CALLBACK *f2)   (GLint, void*);
typedef void (_CALLBACK *fp)   (void*);
typedef void (_CALLBACK *fpp)  (void*, void*);

// tesselator for area
static GLUtriangulatorObj *_tobj       = NULL;
static GPtrArray          *_tmpV       = NULL;     // place holder during tesssalation (GLUtriangulatorObj combineCallback)

// centroid
static GLUtriangulatorObj *_tcen       = NULL;     // GLU CSG - Computational Solid Geometry
static GArray             *_vertexs    = NULL;
static GArray             *_nvertex    = NULL;     // list of nbr of vertex per poly in _vertexs
static GArray             *_centroids  = NULL;     // centroids of poly's in _vertexs

// check if centroid is inside the poly
static GLUtriangulatorObj *_tcin       = NULL;
static GLboolean           _startEdge  = GL_TRUE;  // start inside edge --for heuristic of centroid in poly
static int                 _inSeg      = FALSE;    // next vertex will complete an edge

// HO Data Limit
static GLUtriangulatorObj *_tUnion     = NULL;

// experimental: centroid inside poly heuristic
static double _dcin;
static pt3    _pcin;


////////////////////////////////////////////////////
//
// Quadric (by hand to fill VBO)
//

#ifdef S52_USE_OPENGL_VBO

// Make it not a power of two to avoid cache thrashing on the chip
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
#define _TRANSLATE    0x000A  // mode=10, glTranslated()
#define _TRANSLATE0   0x000B  // mode=11, glLoadIdentity() + glTranslated()

typedef struct _GLUquadricObj {
    GLint style;
    f2    cb_begin;
    fp    cb_end;
    fpp   cb_vertex;
    fint  cb_error;
} _GLUquadricObj;

static _GLUquadricObj *_qobj = NULL;
#else   // S52_USE_OPENGL_VBO
static  GLUquadricObj *_qobj = NULL;
#endif  // S52_USE_OPENGL_VBO

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
          PRINTF("WARNING: gluQuadricError(qobj, GLU_INVALID_ENUM)\n");
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
    //GLdouble angle;
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
        //sweepAngle -= sweepAngle;
        sweepAngle = 0.0;
    }

    //if (sweepAngle == 360.0) slices2 = slices;
    //slices2 = slices + 1;

    GLdouble angleOffset = startAngle/180.0f*PI;
    for (int i=0; i<=slices; i++) {
        GLdouble angle = angleOffset+((PI*sweepAngle)/180.0f)*i/slices;

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

    return TRUE;
}
#endif  // S52_USE_OPENGL_VBO

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
#ifdef S52_DEBUG
    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";
    PRINTF("%s (%d)\n", str, err);
#endif

    g_assert(0);
}

static void_cb_t _quadricError(GLenum err)
{
#ifdef S52_DEBUG
    //const GLubyte *str = gluErrorString(err);
    const char *str = "FIXME: no gluErrorString(err)";
    PRINTF("%s (%d) (%0x)\n", str, err, err);
#endif

    g_assert(0);
}

static void_cb_t _edgeFlag(GLboolean flag)
{
    _startEdge = (GL_FALSE == flag)? GL_TRUE : GL_FALSE;

    // debug
    //PRINTF("%i\n", flag);
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

static void_cb_t _glBegin(GLenum mode, S57_prim *prim)
{
    S57_begPrim(prim, mode);
}

static void_cb_t _beginCen(GLenum data)
{
    // quiet compiler
    (void) data;

    // debug
    //PRINTF("%i\n", data);
}

static void_cb_t _beginCin(GLenum data)
{
    /* from gl.h
     #define GL_LINE_LOOP       0x0002
     #define GL_TRIANGLES       0x0004
     #define GL_TRIANGLE_STRIP  0x0005
     #define GL_TRIANGLE_FAN    0x0006
    */

    // quiet compiler
    (void) data;

    // debug
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

static void_cb_t _vertexCen(GLvoid *data)
{
    pt3 *p = (pt3*) data;

    // debug
    //PRINTF("%f %f\n", p->x, p->y);

    g_array_append_val(_vertexs, *p);
}

static void_cb_t _vertexUnion(GLvoid *data)
{
    pt3 *p = (pt3*) data;

    //if (TRUE == _startEdge) {
        // debug
        //PRINTF("%f %f\n", p->x, p->y);

        g_array_append_val(_vertexs, *p);
    //}
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

static GLint     _initGLU(void)
// initialize various GLU object
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
            PRINTF("WARNING: gluNewTess() failed\n");
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

        // Note: tolerance not implemented in libtess
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
            PRINTF("WARNING: gluNewTess() failed\n");
            return FALSE;
        }
        _centroids = g_array_new(FALSE, FALSE, sizeof(double)*3);
        _vertexs   = g_array_new(FALSE, FALSE, sizeof(double)*3);
        _nvertex   = g_array_new(FALSE, FALSE, sizeof(int));

        //gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);
        //gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NEGATIVE);
        gluTessProperty(_tcen, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ABS_GEQ_TWO);

        gluTessProperty(_tcen, GLU_TESS_BOUNDARY_ONLY, GLU_TRUE);

        // Note: tolerance not implemented in libtess
        //gluTessProperty(_tcen, GLU_TESS_TOLERANCE, 0.000001);

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
            PRINTF("WARNING: gluNewTess() failed\n");
            return FALSE;
        }

        // default
        //gluTessProperty(_tobj, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);

        gluTessProperty(_tcin, GLU_TESS_BOUNDARY_ONLY, GLU_FALSE);

        // Note: tolerance not implemented in libtess
        //gluTessProperty(_tcin, GLU_TESS_TOLERANCE, 0.000001);

        gluTessCallback(_tcin, GLU_TESS_BEGIN,     (f)_beginCin);
        gluTessCallback(_tcin, GLU_TESS_END,       (f)_endCin);
        gluTessCallback(_tcin, GLU_TESS_VERTEX,    (f)_vertexCin);
        gluTessCallback(_tcin, GLU_TESS_ERROR,     (f)_tessError);
        gluTessCallback(_tcin, GLU_TESS_COMBINE,   (f)_combineCallback);
        gluTessCallback(_tcin, GLU_TESS_EDGE_FLAG, (f)_edgeFlag);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tcin, 0.0, 0.0, 1.0);

        //-----------------------------------------------------------

        _tUnion = gluNewTess();
        if (NULL == _tUnion) {
            PRINTF("WARNING: gluNewTess() failed\n");
            return FALSE;
        }

        gluTessProperty(_tUnion, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
        //gluTessProperty(_tUnion, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_POSITIVE);

        gluTessProperty(_tUnion, GLU_TESS_BOUNDARY_ONLY, GLU_TRUE);

        // use _vertexs to hold Union
        gluTessCallback(_tUnion, GLU_TESS_BEGIN,     (f)_beginCin);  // do nothing
        gluTessCallback(_tUnion, GLU_TESS_END,       (f)_endCin);    // do nothing
        gluTessCallback(_tUnion, GLU_TESS_VERTEX,    (f)_vertexUnion);  // fill _vertexs
        gluTessCallback(_tUnion, GLU_TESS_ERROR,     (f)_tessError);
        gluTessCallback(_tUnion, GLU_TESS_COMBINE,   (f)_combineCallback);

        // NOTE: _*NOT*_ NULL to trigger GL_TRIANGLES tessallation
        //gluTessCallback(_tUnion, GLU_TESS_EDGE_FLAG, (f)_edgeFlag);
        gluTessCallback(_tUnion, GLU_TESS_EDGE_FLAG, (f)NULL);

        // set poly in x-y plane normal is Z (for performance)
        gluTessNormal(_tUnion, 0.0, 0.0, 1.0);
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

         //_tessError(0);
         //str = gluGetString(GLU_VERSION);
         //PRINTF("GLU version:%s\n",str);
    }

    return TRUE;
}

static GLint     _freeGLU(void)
{
    //tess
    if (_tmpV) g_ptr_array_free(_tmpV, TRUE);
    if (_tobj) gluDeleteTess(_tobj);

#ifdef S52_USE_OPENGL_VBO
    if (_qobj) _gluDeleteQuadric(_qobj);
#else
    if (_qobj)  gluDeleteQuadric(_qobj);
#endif
    _tmpV = NULL;
    _tobj = NULL;
    _qobj = NULL;

    if (_tcen) gluDeleteTess(_tcen);
    _tcen = NULL;
    if (_tcin) gluDeleteTess(_tcin);
    _tcin = NULL;
    if (_centroids) g_array_free(_centroids, TRUE);
    _centroids = NULL;
    if (_vertexs)   g_array_free(_vertexs,   TRUE);
    _vertexs = NULL;
    if (_nvertex)   g_array_free(_nvertex,   TRUE);
    _nvertex = NULL;

    return TRUE;
}

static int       _g_ptr_array_clear(GPtrArray *arr)
// free vertex malloc'd during tess, keep array memory allocated
{
    for (guint i=0; i<arr->len; ++i)
        g_free(g_ptr_array_index(arr, i));
    g_ptr_array_set_size(arr, 0);

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
                ppt[2] = 0.0;  // delete possible S57_OVERLAP_GEO_Z
                gluTessVertex(tobj, ppt, ppt);
            }
            gluTessEndContour(tobj);
        }
    }
    gluTessEndPolygon(tobj);

    //gluTessCallback(_tobj, GLU_TESS_EDGE_FLAG,  (f) NULL);

    return prim;
}

void      S52_GLU_begUnion(void)
{
    _g_ptr_array_clear(_tmpV);
    g_array_set_size(_vertexs, 0);

    gluTessBeginPolygon(_tUnion, NULL);

    return;
}

void      S52_GLU_addUnion(S57_geo *geo)
{
    guint   npt = 0;
    double *ppt = NULL;
    if (TRUE == S57_getGeoData(geo, 0, &npt, &ppt)) {
        gluTessBeginContour(_tUnion);
        //ppt += npt*3 - 3;
        //for (guint i=npt; i>0; --i, ppt-=3) {  // CCW
        for (guint i=0; i<npt; ++i, ppt+=3) {  // CW
            ppt[2] = 0.0;  // delete possible S57_OVERLAP_GEO_Z
            gluTessVertex(_tUnion, (GLdouble*)ppt, (void*)ppt);
            //PRINTF("x/y/z %f/%f/%f\n", d[0],d[1],d[2]);
        }
        gluTessEndContour(_tUnion);
    }

    return;
}

void      S52_GLU_endUnion(guint *npt, double **ppt)
{
    gluTessEndPolygon(_tUnion);

    *npt = _vertexs->len;
    *ppt = (double*)_vertexs->data;

    return;
}
