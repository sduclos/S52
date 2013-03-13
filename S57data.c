// S57gvgeo.c: interface to OpenEV S57 geo data
//
// Project:  OpENCview/OpenEV

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013 Sylvain Duclos sduclos@users.sourceforgue.net

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


#include "S57data.h"    // S57_geo

#include "S52utils.h"   // PRINTF(), S52_strlen()

#include <math.h>       // INFINITY
//#include <glib.h>       // GArray


#ifdef S52_USE_PROJ
#include <proj_api.h>   // projUV, projXY, projPJ
static projPJ      _pjsrc   = NULL;   // projection source
static projPJ      _pjdst   = NULL;   // projection destination
static int         _doInit  = TRUE;   // will set new src projection
static const char *_argssrc = "+proj=latlong +ellps=WGS84 +datum=WGS84";
//static const char *_argsdst = NULL;
#endif

// MAXINT-6 is how OGR tag an UNKNOWN value
// see gdal/ogr/ogrsf_frmts/s57/s57.h:126
// it is then turn into a string in gv_properties
#define EMPTY_NUMBER_MARKER "2147483641"  /* MAXINT-6 */

#define UNKNOWN  (1.0/0.0)   //HUGE_VAL   // INFINITY/NAN

// debug: current object's internal ID
//static int _id = 0;
static unsigned int _id = 1;  // start at 1, the number of object loaded

typedef struct _pt3 { double x,y,z; } pt3;
typedef struct _pt2 { double x,y;   } pt2;
//struct _pt3{ double x,y,z; } pt3;
//struct pt3 { double x,y,z; };

// assume: extent canonical: x1 < x2, y1 < y2
typedef struct _rect {
    double x1;        // W
    double y1;        // S
    double x2;        // E
    double y2;        // N
} _rect;

// data for glDrawArrays()
typedef struct _prim {
    int mode;
    int first;
    int count;
} _prim;

typedef struct _S57_prim {
    GArray *list;      // list of _prim in 'vertex'
    GArray *vertex;    // geographic coordinate (bouble or float for GLES2 since some go right in the GPU - ie line)
    guint   DList;     // display list of the above
} _S57_prim;

// S57 object geo data
typedef struct _S57_geo {
    guint        id;          // record id (debug)
    //guint        s52objID;       // optimisation: numeric value of OBCL string


    GString     *name;        // object name 6/8 + '\0'; used for S52 LUP
    S52_Obj_t    obj_t;       // used in CS
    //S57_Obj_t    obj_t;       // used in CS also

    _rect        rect;        // lat/lon extent of object
    gboolean     sup;         // geo sup - TRUE if outside view

    // length of geo data (POINT, LINE, AREA) currently in buffer
    guint        dataSize;        // max is 1, linexyznbr, ringxyznbr[0]

    // hold coordinate before and after projection
    geocoord    *pointxyz;    // point

    guint        linexyznbr;  // line
    geocoord    *linexyz;

    /*
    guint        linenbr;        // FIXME: multi-line: break a line, at position in MASK (attVal), into multi-line
    guint       *linexyznbr;     // sound good in theorie
    geocoord   **linexyz;
    */

    guint        ringnbr;       // area
    guint       *ringxyznbr;
    geocoord   **ringxyz;

    // hold tessalated geographic and projected coordinate of area
    // in a format suitable for OpenGL
    S57_prim    *prim;

    GData       *attribs;

    // for CS - object "touched" by this object
    union {
        S57_geo *TOPMAR; // break out objet class "touched"
        S57_geo *LIGHTS; // break out objet class "touched"
        S57_geo *DEPARE; // break out objet class "touched"
        S57_geo *DEPVAL; // break out objet class "touched"
    } touch;

    double       scamin;

#ifdef S52_USE_SUPP_LINE_OVERLAP
    GString     *rcidstr;     // optimisation point to rcid str value
    S57_geo     *link;        // experimetal, link to auxiliary
#endif

    // centroid - save current centroids of this object
    // optimisation mostly for layer 9 AREA
    guint   centroidIdx;
    GArray *centroid;

#ifdef S52_USE_WORLD
    S57_geo     *nextPoly;
#endif


} _S57_geo;

static GString *_attList = NULL;

static void   _string_free(gpointer data)
{
    g_string_free((GString*)data, TRUE);
}

static int    _doneGeoData(_S57_geo *geoData)
// delete the geo data it self
{
    // POINT
    if (NULL != geoData->pointxyz) {
        g_free((geocoord*)geoData->pointxyz);
        geoData->pointxyz = NULL;
    }

    // LINES
    if (NULL != geoData->linexyz) {
        g_free((geocoord*)geoData->linexyz);
        geoData->linexyz = NULL;
    }

    // AREAS
    if (NULL != geoData->ringxyz){
        unsigned int i;
        for(i = 0; i < geoData->ringnbr; ++i) {
            if (NULL != geoData->ringxyz[i])
                g_free((geocoord*)geoData->ringxyz[i]);
            geoData->ringxyz[i] = NULL;
        }
        g_free((geocoord*)geoData->ringxyz);
        geoData->ringxyz = NULL;
    }

    if (NULL != geoData->ringxyznbr) {
        g_free(geoData->ringxyznbr);
        geoData->ringxyznbr = NULL;
    }

    geoData->linexyznbr = 0;
    geoData->ringnbr    = 0;

    return TRUE;
}

int        S57_doneData   (_S57_geo *geoData, gpointer user_data)
{
    // quiet line overlap analysis that trigger a bunch of harmless warning
    if (NULL!=user_data && NULL==geoData)
        return FALSE;

#ifdef S52_USE_WORLD
    {
        S57_geo *geoDataNext = NULL;
        if (NULL != (geoDataNext = S57_getNextPoly(geoData))) {
            S57_doneData(geoDataNext, user_data);
        }
    }
#endif


#ifndef S52_USE_GV
    // data from OGR is a copy --so this need to be deleted
    _doneGeoData(geoData);
#endif

    S57_donePrimGeo(geoData);

    if (NULL != geoData->name)
        g_string_free(geoData->name, TRUE);

    if (NULL != geoData->attribs)
        g_datalist_clear(&geoData->attribs);

    if (NULL != geoData->centroid)
        g_array_free(geoData->centroid, TRUE);

    g_free(geoData);

    return TRUE;
}

#ifdef S52_USE_PROJ
int        S57_initPROJ()
// NOTE: corrected for PROJ 4.6.0 ("datum=WGS84")
{
    if (FALSE == _doInit) return _doInit;

    const char *pj_ver = pj_get_release();
    if (NULL != pj_ver)
        PRINTF("PROJ4 VERSION: %s\n", pj_ver);

    // setup source projection
    if (!(_pjsrc = pj_init_plus(_argssrc))){
        PRINTF("error init src PROJ4\n");
        S57_donePROJ();
        return FALSE;
    }

    // FIXME: will need resetting for different projection
    _doInit = FALSE;

    if (NULL == _attList)
        _attList = g_string_new("");

    return TRUE;
}

int        S57_donePROJ()
{
    if (NULL != _pjsrc) pj_free(_pjsrc);
    if (NULL != _pjdst) pj_free(_pjdst);

    _pjsrc  = NULL;
    _pjdst  = NULL;
    _doInit = TRUE;

    if (NULL != _attList)
        g_string_free(_attList, TRUE);

    return TRUE;
}

int        S57_setMercPrj(double lat)
{
    //const char  *templ = "+proj=merc +lat_ts=%.6f +ellps=WGS84 +datum=WGS84 +unit=m";
    //const char  *templ = "+proj=merc +lat_ts=-50.0 +ellps=WGS84 +datum=WGS84 +unit=m";
    const char  *templ = "+proj=merc +lat_ts=0.0 +ellps=WGS84 +datum=WGS84 +unit=m";
    gchar *pjstr = g_strdup_printf(templ, lat);

    if (NULL != _pjdst) pj_free(_pjdst);

    if (!(_pjdst = pj_init_plus(templ))) {
        PRINTF("ERROR: init pjdst PROJ4 (lat:%f) [%s]\n", lat, pj_strerrno(pj_errno));
        g_assert(0);
        return FALSE;
    }

    g_free(pjstr);

    return TRUE;
}

projXY     S57_prj2geo(projUV uv)
// convert PROJ to geographic (LL)
{
    if (TRUE == _doInit) return uv;

    if (NULL == _pjdst)  return uv;

    uv = pj_inv(uv, _pjdst);
    if (0 != pj_errno) {
        PRINTF("x=%f y=%f %s\n", uv.u, uv.v, pj_strerrno(pj_errno));
        g_assert(0);
        return uv;
    }

    uv.u /= DEG_TO_RAD;
    uv.v /= DEG_TO_RAD;

    return uv;
}

int        S57_geo2prj3dv(guint npt, double *data)
// convert to XY 'in-place'
{
    return_if_null(data);

    unsigned int   i = 0;
    pt3 *pt = (pt3*)data;
    int ret = 0;

    if (TRUE == _doInit) S57_initPROJ();

    if (NULL == _pjdst) {
        PRINTF("ERROR: nothing to project to .. load a chart frist!\n");
        // debug
        g_assert(0);

        return FALSE;
    }

    // deg to rad --latlon
    for (i=0; i<npt; ++i, ++pt) {
        pt->x *= DEG_TO_RAD,
        pt->y *= DEG_TO_RAD;

        // debug
        //if (-10.0 == pt->z) {
        //    PRINTF("DEBUG: overlap found\n");
        //    //g_assert(0);
        //}
    }
    // reset to beginning
    pt = (pt3*)data;

    // rad to cartesian  --mercator
    ret = pj_transform(_pjsrc, _pjdst, npt, 3, &pt->x, &pt->y, &pt->z);
    if (0 != ret) {
        PRINTF("ERROR: in transform (%i): %s (%f,%f)\n", ret, pj_strerrno(pj_errno), pt->x, pt->y);
        g_assert(0);
        return FALSE;
    }
    return TRUE;
}

int        S57_geo2prj(_S57_geo *geoData)
{
    return_if_null(geoData);

    if (TRUE == _doInit) S57_initPROJ();

    guint i;
    guint nr = S57_getRingNbr(geoData);

    for (i=0; i<nr; ++i) {
        guint   npt;
        double *ppt;
        if (TRUE == S57_getGeoData(geoData, i, &npt, &ppt))
            if (FALSE == S57_geo2prj3dv(npt, ppt))
                return FALSE;
    }

    return TRUE;
}
#endif

S57_geo   *S57_setPOINT(geocoord *xyz)
{
    return_if_null(xyz);

    _S57_geo *geoData = g_new0(_S57_geo, 1);
    //_S57_geo *geoData = g_try_new0(_S57_geo, 1);
    if (NULL == geoData)
        g_assert(0);

    geoData->id       = _id++;
    geoData->obj_t    = POINT_T;
    geoData->pointxyz = xyz;

    geoData->rect.x1  =  INFINITY;
    geoData->rect.y1  =  INFINITY;
    geoData->rect.x2  = -INFINITY;
    geoData->rect.y2  = -INFINITY;

    geoData->scamin   =  INFINITY;

#ifdef S52_USE_WORLD
    geoData->nextPoly = NULL;
#endif

    return geoData;
}

// experimental
S57_geo   *S57_setGeoLine(_S57_geo *geoData, guint xyznbr, geocoord *xyz)
{
    return_if_null(geoData);

    geoData->obj_t      = LINES_T;  // because some Edge objet default to _META_T when no geo yet
    geoData->linexyznbr = xyznbr;
    geoData->linexyz    = xyz;

    return geoData;
}

S57_geo   *S57_setLINES(guint xyznbr, geocoord *xyz)
{
    _S57_geo *geoData = g_new0(_S57_geo, 1);
    //_S57_geo *geoData = g_try_new0(_S57_geo, 1);
    if (NULL == geoData)
        g_assert(0);

    return_if_null(geoData);

    geoData->id         = _id++;
    geoData->obj_t      = LINES_T;
    geoData->linexyznbr = xyznbr;
    geoData->linexyz    = xyz;

    geoData->rect.x1 =  INFINITY;
    geoData->rect.y1 =  INFINITY;
    geoData->rect.x2 = -INFINITY;
    geoData->rect.y2 = -INFINITY;

    geoData->scamin  =  INFINITY;


#ifdef S52_USE_WORLD
    geoData->nextPoly = NULL;
#endif

    return geoData;
}

#if 0
S57_geo   *S57_setMLINE(guint nLineCount, guint *linexyznbr, geocoord **linexyz)
{
    _S57_geo *geoData = g_new0(_S57_geo, 1);
    //_S57_geo *geoData = g_try_new0(_S57_geo, 1);
    if (NULL == geoData)
        g_assert(0);

    geoData->id         = _id++;
    geoData->obj_t      = MLINE_T;
    geoData->linenbr    = nLineCount;
    geoData->linexyznbr = linexyznbr;
    geoData->linexyz    = linexyz;

#ifdef S52_USE_WORLD
    geoData->nextPoly = NULL;
#endif

    return geoData;
}
#endif

S57_geo   *S57_setAREAS(guint ringnbr, guint *ringxyznbr, geocoord **ringxyz)
{
    return_if_null(ringxyznbr);
    return_if_null(ringxyz);

    _S57_geo *geoData = g_new0(_S57_geo, 1);
    //_S57_geo *geoData = g_try_new0(_S57_geo, 1);
    if (NULL == geoData)
        g_assert(0);

    geoData->id         = _id++;
    geoData->obj_t      = AREAS_T;
    geoData->ringnbr    = ringnbr;
    geoData->ringxyznbr = ringxyznbr;
    geoData->ringxyz    = ringxyz;

    geoData->rect.x1 =  INFINITY;
    geoData->rect.y1 =  INFINITY;
    geoData->rect.x2 = -INFINITY;
    geoData->rect.y2 = -INFINITY;

    geoData->scamin  =  INFINITY;

#ifdef S52_USE_WORLD
    geoData->nextPoly = NULL;
#endif

    return geoData;
}

S57_geo   *S57_set_META()
{
    _S57_geo *geoData = g_new0(_S57_geo, 1);
    //_S57_geo *geoData = g_try_new0(_S57_geo, 1);
    if (NULL == geoData)
        g_assert(0);

    geoData->id       = _id++;
    geoData->obj_t    = _META_T;

    geoData->rect.x1 =  INFINITY;
    geoData->rect.y1 =  INFINITY;
    geoData->rect.x2 = -INFINITY;
    geoData->rect.y2 = -INFINITY;

    geoData->scamin  =  INFINITY;

#ifdef S52_USE_WORLD
    geoData->nextPoly = NULL;
#endif

    return geoData;
}

int        S57_setName(_S57_geo *geoData, const char *name)
// NOTE: this is a S57 object name .. UTF-16 or UTF-8
// use g_string to handle that
{
    // debug
    //if (NULL == name)
    //    g_assert(0);

    return_if_null(geoData);
    return_if_null(name);

    geoData->name = g_string_new(name);

    return TRUE;
}

char      *S57_getName(_S57_geo *geoData)
{
    return_if_null(geoData);
    return_if_null(geoData->name);

    return geoData->name->str;
}

guint      S57_getRingNbr(_S57_geo *geoData)
{
    return_if_null(geoData);

    // since this is used with S57_getGeoData
    // META object don't need to be projected for rendering
    switch (geoData->obj_t) {
        case POINT_T:
        case LINES_T:
            return 1;
        case AREAS_T:
            return geoData->ringnbr;
        default:
            return 0;
    }
}

int        S57_getGeoData(_S57_geo *geoData, guint ringNo, guint *npt, double **ppt)
// helper providing uniform access to geoData
// WARNING: npt is the allocated mem (capacity)
{
    return_if_null(geoData);

    if  (AREAS_T==geoData->obj_t && geoData->ringnbr<ringNo) {
        PRINTF("WARNING: invalid ring number requested! \n");
        *npt = 0;
        g_assert(0);
    }

    switch (geoData->obj_t) {
    	case _META_T: *npt = 0; break;        // meta geo stuff (ex: C_AGGR)

    	case POINT_T:
            if (NULL != geoData->pointxyz) {
                *npt = 1;
                *ppt = geoData->pointxyz;
            } else {
                *npt = 0;
            }
            break;

        case LINES_T:
            if (NULL != geoData->linexyz) {
                *npt = geoData->linexyznbr;
                *ppt = geoData->linexyz;
            } else {
                *npt = 0;
            }
            break;

        case AREAS_T:
            if (NULL != geoData->ringxyznbr) {
                *npt = geoData->ringxyznbr[ringNo];
                *ppt = geoData->ringxyz[ringNo];
            } else {
                *npt = 0;
            }
            //else
            //    PRINTF("ERROR: atempt to access a tessed area .. \n"
            //           "(there is no geo anymore!!)\n");

            // WARNING: check if begin/end vertex are the same
            // (OpenGIS: closed and simple ring)
            // If so shorten it to help trace tesss (in combineCB)
            /*
            {
                int     n = 3 * (*npt-1);
                double *p = *ppt;
                if (p[0] == p[n+0] &&
                    p[1] == p[n+1] &&
                    p[2] == p[n+2]) {
                    //--(*npt);
                    // *npt -= 1;          // <<< shorten
                } else {
                    PRINTF("not close/simple ring\n");
                    //PRINTF("    START: x=%f y=%f z=%f\n", p[0],p[1],p[2]);
                    //PRINTF("    END:   x=%f y=%f z=%f\n", p[n+0],p[n+1],p[n+2]);
                    //exit(0);
                }

            }
            */

            //if (geodata->ringnbr > 1) {
            //    PRINTF("WARNING!!! AREA_T ringnbr:%i only exterior ring used\n", geodata->ringnbr);
            //}
            break;
        default:
            PRINTF("ERROR: object type invalid (%i)\n", geoData->obj_t);
            g_assert(0);
            return FALSE;
    }

    if (*npt < geoData->dataSize) {
        PRINTF("ERROR: geo lenght greater then npt - internal error\n");
        g_assert(0);
        return FALSE;
    }

    if (0==*npt)
        return FALSE;
    else
        return TRUE;
}

S57_prim  *S57_initPrim(_S57_prim *prim)
// set/reset primitive holder
{
    if (NULL == prim) {
        S57_prim *p = g_new0(S57_prim, 1);
        //_S57_prim *p = g_try_new0(_S57_prim, 1);
        if (NULL == p)
            g_assert(0);

        p->list   = g_array_new(FALSE, FALSE, sizeof(_prim));
        p->vertex = g_array_new(FALSE, FALSE, sizeof(vertex_t)*3);

        return p;
    } else {
        g_array_set_size(prim->list,   0);
        g_array_set_size(prim->vertex, 0);

        return prim;
    }
}

S57_prim  *S57_donePrim(_S57_prim *prim)
{
    //return_if_null(prim);
    // some symbol (ex Mariners' Object) dont use primitive since
    // not in OpenGL retained mode .. so this warning is a false alarme
    if (NULL == prim)
        return NULL;

    if (NULL != prim->list)   g_array_free(prim->list,   TRUE);
    if (NULL != prim->vertex) g_array_free(prim->vertex, TRUE);
    //if (NULL != prim->list)   g_array_unref(prim->list);
    //if (NULL != prim->vertex) g_array_unref(prim->vertex);

    // failsafe
    prim->list   = NULL;
    prim->vertex = NULL;

    g_free(prim);

    return NULL;
}

S57_prim  *S57_initPrimGeo(_S57_geo *geoData)
{
    return_if_null(geoData);

    geoData->prim = S57_initPrim(geoData->prim);

    return geoData->prim;
}

S57_geo   *S57_donePrimGeo(_S57_geo *geoData)
{
    return_if_null(geoData);

    if (NULL != geoData->prim) {
        S57_donePrim(geoData->prim);

        //g_free(geoData->prim);

        geoData->prim = NULL;
    }

    return NULL;
}

int        S57_begPrim(_S57_prim *prim, int mode)
{
    struct _prim p;

    return_if_null(prim);

    p.mode  = mode;
    p.first = prim->vertex->len;

    g_array_append_val(prim->list, p);

    return TRUE;
}

int        S57_addPrimVertex(_S57_prim *prim, vertex_t *ptr)
// add one xyz coord (3 vertex_t)
{
    return_if_null(prim);
    return_if_null(ptr);

    //g_array_append_val(prim->vertex, *ptr);
    g_array_append_vals(prim->vertex, ptr, 1);

    return TRUE;
}

int        S57_endPrim(_S57_prim *prim)
{
    return_if_null(prim);

    struct _prim *p = &g_array_index(prim->list, struct _prim, prim->list->len-1);

    if (NULL == p)
        return FALSE;

    p->count = prim->vertex->len - p->first;

    return TRUE;
}

S57_prim  *S57_getPrimGeo(_S57_geo *geoData)
{
    return_if_null(geoData);

    return geoData->prim;
}

guint      S57_getPrimData(_S57_prim *prim, guint *primNbr, vertex_t **vert, guint *vertNbr, guint *vboID)
{
    return_if_null(prim);

    //return_if_null(prim->list);

    *primNbr =            prim->list->len;
    //*vert    = (double*)prim->vertex->data;
    *vert    = (vertex_t*)prim->vertex->data;
    //*vertNbr = (int)      prim->vertex->len;
    *vertNbr =            prim->vertex->len;
    *vboID   =            prim->DList;

    //return prim->DList;
    return TRUE;
}

GArray    *S57_getPrimVertex(_S57_prim *prim)
{
    return_if_null(prim);

    return prim->vertex;
}

S57_prim  *S57_setPrimSize(_S57_prim *prim, int sz)
{
    return_if_null(prim);

    g_array_set_size(prim->list,   sz);
    g_array_set_size(prim->vertex, sz);

    return prim;
}

int        S57_setPrimDList (_S57_prim *prim, guint DList)
{
    return_if_null(prim);

    prim->DList = DList;

    return TRUE;
}

int        S57_getPrimIdx(_S57_prim *prim, unsigned int i, int *mode, int *first, int *count)
{
    return_if_null(prim);

    if (i>=prim->list->len)
        return FALSE;

    struct _prim *p = &g_array_index(prim->list, struct _prim, i);

    if (NULL == p) {
        PRINTF("ERROR: no primitive at index: %i\n", i);
        return FALSE;
    }

    *mode  = p->mode;
    *first = p->first;
    *count = p->count;

    return TRUE;
}

int        S57_setExt(_S57_geo *geoData, double x1, double y1, double x2, double y2)
// assume: extent canonical
{
    return_if_null(geoData);

    //if (0 == g_strncasecmp(geoData->name->str, "M_COVR", 6)) {
    //    PRINTF("%s: %f, %f  UR: %f, %f\n", geoData->name->str, x1, y1, x2, y2);
    //}

    // FIXME: use abs()
    // check long
    x1 = (x1 < -180.0) ? 0.0 : (x1 > 180.0) ? 0.0 : x1;
    x2 = (x2 < -180.0) ? 0.0 : (x2 > 180.0) ? 0.0 : x2;
    // check lat
    y1 = (y1 < -90.0) ? 0.0 : (y1 > 90.0) ? 0.0 : y1;
    y2 = (y2 < -90.0) ? 0.0 : (y2 > 90.0) ? 0.0 : y2;

    geoData->rect.x1 = x1;
    geoData->rect.y1 = y1;
    geoData->rect.x2 = x2;
    geoData->rect.y2 = y2;

    return TRUE;
}

int        S57_getExt(_S57_geo *geoData, double *x1, double *y1, double *x2, double *y2)
// assume: extent canonical
{
    return_if_null(geoData);

    *x1 = geoData->rect.x1; // W
    *y1 = geoData->rect.y1; // S
    *x2 = geoData->rect.x2; // E
    *y2 = geoData->rect.y2; // N

    return TRUE;
}

S52_Obj_t  S57_getObjtype(_S57_geo *geoData)
//S57_Obj_t  S57_getObjtype(_S57_geo *geoData)
{
    if (NULL == geoData)
        return _META_T;

    return geoData->obj_t;
}

#if 0
// return the number of attributes.
static void   _countItems(GQuark key_id, gpointer data, gpointer user_data)
{
    const gchar *attName  = g_quark_to_string(key_id);
    if (6 == strlen(attName)){
        int *cnt = (int*)user_data;
        *cnt = *cnt + 1;
    }
}

int        S57_getNumAtt(S57_geo *geoData)
{
    int cnt = 0;
    g_datalist_foreach(&geoData->attribs, _countItems, &cnt);
    return cnt;
}


struct _qwerty {
    int currentIdx;
    char featureName[20];
    char **name;
    char **value;
};

static void   _getAttValues(GQuark key_id, gpointer data, gpointer user_data)
{
    struct _qwerty *attData = (struct _qwerty*)user_data;

    GString     *attValue = (GString*) data;
    const gchar *attName  = g_quark_to_string(key_id);

    if (6 == S52_strlen(attName)){
        strcpy(attData->value[attData->currentIdx], attValue->str);
        strcpy(attData->name [attData->currentIdx], attName );
        PRINTF("inserting %s %s %d", attName, attValue->str, attData->currentIdx);
        attData->currentIdx += 1;
    } else {
        ;//      PRINTF("sjov Att: %s  = %s \n",attName, attValue->str);

    }
}

// recommend you count number of attributes in advance, to allocate the
// propper amount of **. each char *name should be allocated 7 and the char
// *val 20 ????
int        S57_getAttributes(_S57_geo *geoData, char **name, char **val)
{
  struct _qwerty tmp;

  tmp.currentIdx = 0;
  tmp.name       = name;
  tmp.value      = val;

  g_datalist_foreach(&geoData->attribs, _getAttValues,  &tmp);
  //  strcpy(name[tmp.currentIdx], "x");
  //  strcpy(val[tmp.currentIdx], "y");
  return tmp.currentIdx;
}
#endif

GString   *S57_getAttVal(_S57_geo *geoData, const char *att_name)
// return attribute string value or NULL if:
//      1- attribute name abscent
//      2- its a mandatory attribute but its value is not define (EMPTY_NUMBER_MARKER)
{
    return_if_null(geoData);
    return_if_null(att_name);

    static int silent = FALSE;

    //GString *att = (GString*) g_datalist_get_data(&geoData->attribs, att_name);
    //GString *att = (GString*) g_dataset_id_get_data(&geoData->attribs, g_quark_try_string(att_name));
    GQuark   q   = g_quark_from_string(att_name);
    //GQuark   q   = g_quark_from_static_string(att_name);
    GString *att = (GString*) g_datalist_id_get_data(&geoData->attribs, q);

    if (NULL!=att && (0==g_strcmp0(att->str, EMPTY_NUMBER_MARKER))) {
        //PRINTF("NOTE: mandatory attribute (%s) with ommited value\n", att_name);
        return NULL;
    }

    // display this NOTE once (because of to many warning)
    if (!silent && NULL!=att && 0==att->len) {
        PRINTF("NOTE: attribute (%s) has no value [obj:%s]\n", att_name, geoData->name->str);
        PRINTF("      (this msg will not repeat)\n");
        silent = TRUE;
        return NULL;
    }

    return att;
}

GData     *S57_setAtt(_S57_geo *geoData, const char *name, const char *val)
{
    return_if_null(geoData);
    return_if_null(name);
    return_if_null(val);

    // debug
    //PRINTF("OBJ: %s, ATT_NAME: %s, ATT_VAL: %s\n", S57_getName(geoData), name, val);
    //if (0==g_strcasecmp(S57_getName(geoData), "marfea")) {
    //    PRINTF("XXX SKIP   OBJ: %s, ATT_NAME: %s, ATT_VAL: %s\n", S57_getName(geoData), name, val);
    //    return NULL;
    //}
    /*
    if (NULL == val) {
        g_assert(0);
        return NULL;
    }
    */

    GQuark   qname = g_quark_from_string(name);
    GString *value = g_string_new(val);

    if (NULL == geoData->attribs)
        g_datalist_init(&geoData->attribs);

    // FIXME: maybe move up this SCAMIN test ?
    //if (0==g_strcmp0(name, "SCAMIN") && NULL!=val) {
    if (0==g_strcmp0(name, "SCAMIN")) {
         S57_setScamin(geoData, S52_atof(val));
         //geoData->scamin = S52_atof(val);

         // debug
         //if (0==g_strcmp0(S57_getName(geoData), "DEPCNT"))
         //   PRINTF("scamin depcnt: %s\n", val);
    }

#ifdef S52_USE_SUPP_LINE_OVERLAP
    if ((0==S52_strncmp(S57_getName(geoData), "Edge", 4)) && (0==S52_strncmp(name, "RCID", 4))) {
         geoData->rcidstr = value;
     }
#endif

    g_datalist_id_set_data_full(&geoData->attribs, qname, value, _string_free);

    return geoData->attribs;
}

#if 0
int        S57_setTouch(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    geo->touch = touch;

    return TRUE;
}

S57_geo   *S57_getTouch(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch;
}
#endif

int        S57_setTouchTOPMAR(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    geo->touch.TOPMAR = touch;

    return TRUE;
}

S57_geo   *S57_getTouchTOPMAR(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.TOPMAR;
}

int        S57_setTouchLIGHTS(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    geo->touch.LIGHTS = touch;

    return TRUE;
}

S57_geo   *S57_getTouchLIGHTS(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.LIGHTS;
}

int        S57_setTouchDEPARE(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    geo->touch.DEPARE = touch;

    return TRUE;
}

S57_geo   *S57_getTouchDEPARE(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.DEPARE;
}

int        S57_setTouchDEPVAL(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    geo->touch.DEPVAL = touch;

    return TRUE;
}

S57_geo   *S57_getTouchDEPVAL(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.DEPVAL;
}

double     S57_setScamin(_S57_geo *geo, double scamin)
{
    return_if_null(geo);

    geo->scamin = scamin;

    return geo->scamin;
}

double     S57_getScamin(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->scamin;
}

double     S57_resetScamin(_S57_geo *geo)
// reset scamin from att val
{
    return_if_null(geo);


    if (NULL == geo->attribs)
        g_datalist_init(&geo->attribs);

    GString *valstr = S57_getAttVal(geo, "SCAMIN");

    //double val = (NULL==valstr) ? INFINITY : S52_atof(valstr->str);
    double val = (NULL==valstr) ? UNKNOWN : S52_atof(valstr->str);

    geo->scamin = val;

    return geo->scamin;
}

static void   _printAtt(GQuark key_id, gpointer data, gpointer user_data)
{
    // 'user_data' not used
    (void) user_data;

    const gchar *attName  = g_quark_to_string(key_id);
    GString     *attValue = (GString*) data;

    // print only S57 attrib - assuming that OGR att are not 6 char in lenght!!
    if (6 == S52_strlen(attName))
        PRINTF("\t%s : %s\n", attName, (char*)attValue->str);
}

gboolean   S57_setSup(_S57_geo *geoData, gboolean sup)
{
    return_if_null(geoData);

    geoData->sup = sup;

    return geoData->sup;
}

gboolean   S57_getSup(_S57_geo *geoData)
{
    return_if_null(geoData);

    return geoData->sup;
}


int        S57_dumpData(_S57_geo *geoData, int dumpCoords)
// debug
// if dumpCoords is TRUE dump all coordinates else dump extent
{
    return_if_null(geoData);


    PRINTF("S57_ID : %i\n", geoData->id);
    PRINTF("NAME   : %s\n", geoData->name->str);

    g_datalist_foreach(&geoData->attribs, _printAtt, NULL);

    {    // print coordinate
        guint     npt = 0;
        geocoord *ppt;

        S57_getGeoData(geoData, 0, &npt, &ppt);

        switch (geoData->obj_t) {
            case _META_T:  PRINTF("_META_T (%i)\n", npt); break;
            case POINT_T:  PRINTF("POINT_T (%i)\n", npt); break;
            case LINES_T:  PRINTF("LINES_T (%i)\n", npt); break;
            case AREAS_T:  PRINTF("AREAS_T (%i)\n", npt); break;
            default:
                PRINTF("WARNING: invalid object type; %i\n", geoData->obj_t);
        }

        if (TRUE == dumpCoords) {
            unsigned int i   = 0;
            guint     npt = 0;
            geocoord *ppt;

            if (FALSE==S57_getGeoData(geoData, 0, &npt, &ppt))
                return FALSE;

            for (i=0; i<npt; ++i) {
                PRINTF("\t\t(%f, %f, %f)\n", ppt[0], ppt[1], ppt[2]);
                ppt += 3;
            }
        } else {
            // dump extent
            PRINTF("EXTENT: %f, %f  --  %f, %f\n",
                   geoData->rect.y1, geoData->rect.x1, geoData->rect.y2, geoData->rect.x2);

        }
    }

    return TRUE;
}

static void   _getAtt(GQuark key_id, gpointer data, gpointer user_data)
{

    const gchar *attName  = g_quark_to_string(key_id);
    GString     *attValue = (GString*) data;
    GString     *attList  = (GString*) user_data;

    // filter out OGR internal S57 info
    if (0 == g_strcmp0("MASK",      attName)) return;
    if (0 == g_strcmp0("USAG",      attName)) return;
    if (0 == g_strcmp0("ORNT",      attName)) return;
    if (0 == g_strcmp0("NAME_RCNM", attName)) return;
    if (0 == g_strcmp0("NAME_RCID", attName)) return;

    // save S57 attribute + system attribute (ex vessel name - AIS)
    if (0 != attList->len)
        g_string_append(attList, ",");

    g_string_append(attList, attName);
    g_string_append_c(attList, ':');
    g_string_append(attList, attValue->str);

    // replace '\t' to ' ',
    if (0 == g_strcmp0("_vessel_label", attName)) {
        for (guint i=0; i<attList->len; ++i) {
            if ('\n' == attList->str[i]) {
                attList->str[i] = ' ';
                //g_string_insert_c(attList, i, SLASH);
                return;
            }
        }
    }
    //PRINTF("\t%s : %s\n", attName, (char*)attValue->str);

    return;
}


const char *S57_getAtt(_S57_geo *geoData)
{
    return_if_null(geoData);


    PRINTF("S57_ID : %i\n", geoData->id);
    PRINTF("NAME   : %s\n", geoData->name->str);

    g_string_set_size(_attList, 0);
    g_string_printf(_attList, "%i", geoData->id);

    g_datalist_foreach(&geoData->attribs, _getAtt, _attList);



    return _attList->str;
}

#if 0
void       S57_getGeoWindowBoundary(double lat, double lng, double scale, int width, int height, double *latMin, double *latMax, double *lngMin, double *lngMax)
{

  S57_initPROJ();

  {
    projUV pc1, pc2;   // pixel center

    pc1.v = lat;
    pc1.u = lng;

    pc1 = S57_geo2prj(pc1); // mercator center in meters

    // lower right
    pc2.u = (width/2. +0.5)*scale + pc1.u;
    pc2.v = (height/2. +0.5)*scale + pc1.v;
    pc2 = S57_prj2geo(pc2);
    *lngMax = pc2.u;
    *latMax = pc2.v;
    // upper left
    pc2.u = -((width/2.)*scale +0.5) + pc1.u;
    pc2.v = -((height/2.)*scale +0.5) + pc1.v;
    pc2 = S57_prj2geo(pc2);
    *lngMin = pc2.u;
    *latMin = pc2.v;
  }

  PRINTF("lat/lng: %lf/%lf scale: %lf, w/h: %d/%d lat: %lf/%lf lng: %lf/%lf\n", lat, lng, scale, width, height, *latMin, *latMax, *lngMin, *lngMax);

  //S57_donePROJ();

}
#endif

#ifdef S52_USE_SUPP_LINE_OVERLAP
int        S57_sameChainNode(_S57_geo *geoA, _S57_geo *geoB)
{

    return_if_null(geoA);
    return_if_null(geoB);

    unsigned int i = 0;

    pt3 *pa   = (pt3 *)geoA->linexyz;
    pt3 *pb   = (pt3 *)geoB->linexyz;
    pt3 *bend = (pt3 *)&geoB->linexyz[(geoB->linexyznbr-1)*3];
    //pt3 *bend = geoB->linexyz[geoB->linexyznbr-b-1];

    // FIXME: what if a chain-node has the same point
    // at both end of the chain!!
    /*
    if ((pb->x == bend->x) && (pb->y == bend->y))
        g_assert(0);

    // first point match
    if ((pa->x == pb->x) && (pa->y == pb->y))
        reverse = FALSE;
    else {
        // last point match
        if ((pa->x == bend->x) && (pa->y == bend->y))
            reverse = TRUE;
        else
            //no match
            return FALSE;
    }
    */

    // can't be the same if not same lenght
    if (geoA->linexyznbr != geoB->linexyznbr)
        return FALSE;

    if ((pa->x == pb->x) && (pa->y == pb->y)) {
        pt3 *ptA = (pt3 *)geoA->linexyz;
        pt3 *ptB = (pt3 *)geoB->linexyz;

        for (i=0; i<geoA->linexyznbr; ++i) {
            if ((ptA->x != ptB->x) || (ptA->x != ptB->x))
                break;

            ++ptA;
            ++ptB;
        }
    }

    // reach the end, then the're the same
    if (i+1 >= geoA->linexyznbr)
        return TRUE;

    if ((pa->x == bend->x) && (pa->y == bend->y)) {
        pt3 *ptA = (pt3 *)geoA->linexyz;
        pt3 *ptB = (pt3 *)(geoB->linexyz + (geoB->linexyznbr-1)*3);

        for (i=0; i<geoA->linexyznbr; ++i) {
            if ((ptA->x != ptB->x) || (ptA->x != ptB->x))
                break;

            ++ptA;
            --ptB;
        }
    }
    // reach the end, then the're the same
    if (i+1 >= geoA->linexyznbr)
        return TRUE;

    return FALSE;
    /*
    for (a=0; a<geoA->linexyznbr; ++a) {
        for (b=0; b<geoB->linexyznbr; ++b) {
            pt3 *ptB;
            pt3 *ptA = (pt3 *)&geoA->linexyz[a];
            if (TRUE == reverse)
                ptB = (pt3 *)&geoB->linexyz[(geoB->linexyznbr-b-1)*3];
            else
                ptB = (pt3 *)&geoB->linexyz[b];

            if ((ptA->x!=ptB->x) || (ptA->y!=ptB->y))
                return FALSE;
        }
    }
    */

    //return TRUE;
}

S57_geo   *S57_getGeoLink(_S57_geo *geoData)
{
    return_if_null(geoData);

    return geoData->link;
}

S57_geo   *S57_setGeoLink(_S57_geo *geoData, _S57_geo *link)
{
    return_if_null(geoData);

    geoData->link = link;

    return geoData;
}
#endif

#ifdef S52_USE_WORLD
S57_geo   *S57_setNextPoly(_S57_geo *geoData, _S57_geo *nextPoly)
{
    return_if_null(geoData);

    if (NULL != geoData->nextPoly)
        nextPoly->nextPoly = geoData->nextPoly;

    geoData->nextPoly = nextPoly;

    return geoData;
}


S57_geo   *S57_getNextPoly(_S57_geo *geoData)
{
    return_if_null(geoData);

    return geoData->nextPoly;
}

S57_geo   *S57_delNextPoly(_S57_geo *geoData)
// unlink Poly chain
{
    return_if_null(geoData);

    while (NULL != geoData) {
        S57_geo *nextPoly = geoData->nextPoly;
        geoData->nextPoly = NULL;
        geoData = nextPoly;
    }

    return NULL;
}
#endif

unsigned int S57_getGeoID(S57_geo *geoData)
{
    return_if_null(geoData);

    return  geoData->id;
}

int        S57_isPtInside(int npt, double *xyz, double x, double y, int close)
// return TRUE if inside else FALSE
{
    int i, j, c = 0;
    pt3 *v = (pt3 *)xyz;

    return_if_null(xyz);

    if (TRUE == close) {
        for (i=0; i<npt-1; ++i) {
            pt3 p1 = v[i];
            pt3 p2 = v[i+1];

            if ( ((p1.y>y) != (p2.y>y)) &&
                (x < (p2.x-p1.x) * (y-p1.y) / (p2.y-p1.y) + p1.x) )
                c = !c;
        }
    } else {
        for (i=0, j=npt-1; i<npt; j=i++) {
            pt3 p1 = v[i];
            pt3 p2 = v[j];

            if ( ((p1.y>y) != (p2.y>y)) &&
                (x < (p2.x-p1.x) * (y-p1.y) / (p2.y-p1.y) + p1.x) )
                c = !c;
        }
    }

    // debug
    //PRINTF("npt: %i inside: %s\n", npt, (c==1)?"TRUE":"FALSE");

    return c;
}

int        S57_touch(_S57_geo *geoA, _S57_geo *geoB)
// TRUE if A touch B else FALSE
{
    //unsigned int i;
    unsigned int  nptA;
    double       *pptA;
    unsigned int  nptB;
    double       *pptB;


    return_if_null(geoA);
    return_if_null(geoB);

    if (FALSE == S57_getGeoData(geoA, 0, &nptA, &pptA))
        return FALSE;

    if (FALSE == S57_getGeoData(geoB, 0, &nptB, &pptB))
        return FALSE;

    // FIXME: work only for point in poly not point in line
    if (LINES_T == S57_getObjtype(geoB)) {
        PRINTF("FIXME: geoB is a LINES_T .. this algo break on that type\n");
        return FALSE;
    }

    for (guint i=0; i<nptA; ++i, pptA+=3) {
        if (TRUE == S57_isPtInside(nptB, pptB, pptA[0], pptA[1], TRUE))
            return TRUE;
    }

    return FALSE;
}

guint      S57_getGeoSize(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->dataSize;
}

guint      S57_setGeoSize(_S57_geo *geo, guint size)
{
    return_if_null(geo);

    if ((POINT_T==geo->obj_t) && (size > 1))                  g_assert(0);
    if ((LINES_T==geo->obj_t) && (size > geo->linexyznbr))    g_assert(0);
    if ((AREAS_T==geo->obj_t) && (size > geo->ringxyznbr[0])) g_assert(0);

    if (_META_T == geo->obj_t) {
        PRINTF("ERROR: object type invalid (%i)\n", geo->obj_t);
        g_assert(0);
        return FALSE;
    }

    return geo->dataSize = size;
}

int        S57_newCentroid(_S57_geo *geo)
{
    return_if_null(geo);

    // case where an object has multiple centroid (concave poly)
    if (NULL == geo->centroid)
        geo->centroid = g_array_new(FALSE, FALSE, sizeof(pt2));
    else
        geo->centroid = g_array_set_size(geo->centroid, 0);

    geo->centroidIdx = 0;

    return TRUE;
}

int        S57_addCentroid(_S57_geo *geo, double  x, double  y)
{
    return_if_null(geo);

    //geo->x = x;
    //geo->y = y;

    pt2 pt = {x, y};

    g_array_append_val(geo->centroid, pt);

    return TRUE;
}

int        S57_getNextCentroid(_S57_geo *geo, double *x, double *y)
{
    return_if_null(geo);
    return_if_null(geo->centroid);

    //*x = geo->x;
    //*y = geo->y;

    if (geo->centroidIdx < geo->centroid->len) {
        pt2 pt = g_array_index(geo->centroid, pt2, geo->centroidIdx);
        *x = pt.x;
        *y = pt.y;

        ++geo->centroidIdx;
        return TRUE;

    }


    return FALSE;
    //return TRUE;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
int        S57_markOverlapGeo(_S57_geo *geo, _S57_geo *geoEdge)
// mark coordinates in geo that match the chaine-node in geoEdge
{
    int     next = 0;
    guint   i,j;

    return_if_null(geo);
    return_if_null(geoEdge);

    guint   npt = 0;
    double *ppt;
    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
        g_assert(0);
        return FALSE;
    }

    guint   nptEdge = 0;
    double *pptEdge;
    if (FALSE == S57_getGeoData(geoEdge, 0, &nptEdge, &pptEdge)) {
        PRINTF("DEBUG: nptEdge: %i\n", nptEdge);
        g_assert(0);
        return FALSE;
    }

    // debug
    //if (0 == g_strcmp0("HRBARE", S57_getName(geo))) {
    //    PRINTF("DEBUG: found HRBARE, nptEdge: %i\n", nptEdge);
    //}

    // search ppt for first pptEdge
    for(i=0; i<npt; ++i) {
        //if (ppt[i*3] == pptEdge[i*3] && ppt[i*3+1] == pptEdge[i*3+1]) {
        if (ppt[i*3] == pptEdge[0] && ppt[i*3+1] == pptEdge[1]) {

            if (i == (npt-1)) {
                if (ppt[(i-1)*3] == pptEdge[3] && ppt[(i-1)*3+1] == pptEdge[4]) {
                    // next if backward
                    next = -1;
                    break;
                }
                //PRINTF("ERROR: at the end\n");
                //g_assert(0);
            }

            // FIXME: rollover
            // find if folowing point is ahead
            if (ppt[(i+1)*3] == pptEdge[3] && ppt[(i+1)*3+1] == pptEdge[4]) {
                // next is ahead
                next = 1;
                break;
            } else {
                // FIXME: start at end
                if (0==i) {
                    i = npt - 1;
                    //PRINTF("ERROR: edge mismatch\n");
                    //    g_assert(0);
                    //return FALSE;
                }
                // FIXME: rollover
                if (ppt[(i-1)*3] == pptEdge[3] && ppt[(i-1)*3+1] == pptEdge[4]) {
                    // next if backward
                    next = -1;
                    break;
                }
                // this could be due to an innir ring!
                //else {
                //    PRINTF("ERROR: edge mismatch\n");
                //    g_assert(0);
                //}
            }
        }
    }

    // FIXME: no starting point in edge match any ppt!?
    // could it be a rounding problem
    // could be that this edge is part of an inner ring of a poly
    // and S57_getGeoData() only return outer ring
    if (0 == next) {
        //GString *rcidEdgestr = S57_getAttVal(geoEdge, "RCID");
        //S57_geo *geoEdgeLinkto = geoEdge->link;
        //PRINTF("WARNING: this edge (ID:%s link to %s) starting point doesn't match any point in this geo (%s)\n",
        //       rcidEdgestr->str, S57_getName(geoEdgeLinkto), S57_getName(geo));

        /*
        // debug: check if it is a precision problem (rounding)
        guint i = 0;
        double minLat = 90.0;
        for(i=0; i<npt; ++i) {
            // find the minimum
            double tmp = ppt[i*3] - pptEdge[0];
            if (minLat > fabs(tmp))
                minLat = tmp;
        }
        //g_assert(0);
        PRINTF("minLat = %f\n", minLat);
        */

        return FALSE;
    }

    if (-1 == next) {
        int tmp = (i+1) - nptEdge;
        if ( tmp < 0)
            g_assert(0);
    }

    if (1 == next) {
        if (nptEdge + i > npt)
            g_assert(0);
    }

    //* debug - test if this -10.0 confuse the tesselator
    // move vertex to clip plane
    for (j=0; j<nptEdge; ++j) {
        ppt[i*3 + 2] = -10.0;
        i += next;
    }
    //*/

    return TRUE;
}

GString   *S57_getRCIDstr(S57_geo *geo)
{
    return_if_null(geo);

    return geo->rcidstr;
}

#endif


#if 0
int main(int argc, char** argv)
{

   return 1;
}
#endif
