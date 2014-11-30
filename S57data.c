// S57data.c: interface to S57 geo data
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


#include "S57data.h"    // S57_geo
#include "S52utils.h"   // PRINTF(), S52_strlen()

#include <math.h>       // INFINITY

#ifdef S52_USE_PROJ
static projPJ      _pjsrc   = NULL;   // projection source
static projPJ      _pjdst   = NULL;   // projection destination
static char       *_pjstr   = NULL;
static int         _doInit  = TRUE;   // will set new src projection
static const char *_argssrc = "+proj=latlong +ellps=WGS84 +datum=WGS84";
//static const char *_argsdst = "+proj=merc +ellps=WGS84 +datum=WGS84 +unit=m +no_defs";
// Note: ../../../FWTools/FWTools-2.0.6/bin/gdalwarp -t_srs "+proj=merc +ellps=WGS84 +datum=WGS84 +unit=m +no_defs" 46307260_LOD2.tif 46307260_LOD2.merc.tif
#endif

// MAXINT-6 is how OGR tag an UNKNOWN value
// see gdal/ogr/ogrsf_frmts/s57/s57.h:126
// it is then turn into a string in gv_properties
//#define EMPTY_NUMBER_MARKER "2147483641"  /* MAXINT-6 */

#define UNKNOWN  (1.0/0.0)   //HUGE_VAL   // INFINITY/NAN

// debug: current object's internal ID
static unsigned int _id = 1;  // start at 1, the number of object loaded

typedef struct _pt3 { double x,y,z; } pt3;
typedef struct _pt2 { double x,y;   } pt2;

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
    //guint mode;
    //guint first;
    //guint count;
} _prim;

typedef struct _S57_prim {
    GArray *list;      // list of _prim in 'vertex'
    GArray *vertex;    // XYZ geographic coordinate (bouble or float for GLES2 since some go right in the GPU - ie line)
    guint   DList;     // display list of the above
} _S57_prim;

// S57 object geo data
typedef struct _S57_geo {
    guint        id;          // record id (debug)
    //guint        s52objID;       // optimisation: numeric value of OBCL string


    GString     *name;        // object name 6/8 + '\0'; used for S52 LUP
    //S52_Obj_t    obj_t;       // used in CS
    S57_Obj_t    obj_t;       // used in CS

    _rect        rect;        // lat/lon extent of object
    gboolean     supp;        // geo suppression - TRUE if outside view

    // length of geo data (POINT, LINE, AREA) currently in buffer
    //guint        dataSize;        // max is 1, linexyznbr, ringxyznbr[0]
    guint        geoSize;        // max is 1, linexyznbr, ringxyznbr[0]

    // hold coordinate before and after projection
    geocoord    *pointxyz;    // point

    guint        linexyznbr;  // line
    geocoord    *linexyz;

    guint        ringnbr;     // area
    guint       *ringxyznbr;
    geocoord   **ringxyz;

    // hold tessalated geographic and projected coordinate of area
    // in a format suitable for OpenGL
    S57_prim    *prim;

    GData       *attribs;

#ifdef S52_USE_C_AGGR_C_ASSO
    // point to the S57 relationship object C_AGGR / C_ASSO this S57_geo belong
    S57_geo     *relation;
#endif

    // for CS - object "touched" by this object
    union {
        S57_geo *TOPMAR; // break out objet class "touched"
        S57_geo *LIGHTS; // break out objet class "touched"
        S57_geo *DEPARE; // break out objet class "touched"
        S57_geo *DEPVAL; // break out objet class "touched"
    } touch;

    double       scamin;

#ifdef S52_USE_SUPP_LINE_OVERLAP
    GString     *rcidstr;     // optimisation: point to Att RCID str value
    S57_geo     *link;        // experimetal, link to auxiliary
    S57_AW_t     origAW;      // debug - original Area Winding, CW: area < 0,  CCW: area > 0
#endif

    // centroid - save current centroids of this object
    // optimisation mostly for layer 9 AREA
    guint        centroidIdx;
    GArray      *centroid;

#ifdef S52_USE_WORLD
    S57_geo     *nextPoly;
#endif

    gboolean     highlight;  // highlight this geo object (cursor pick / hazard - experimental)

    gboolean     hazard;     // TRUE if a Safety Contour / hazard - use by leglin and GUARDZONE

} _S57_geo;

static GString *_attList = NULL;

static int    _doneGeoData(_S57_geo *geo)
// delete the geo data it self - data from OGR is a copy
{
#ifdef S52_USE_GV
    // not GV geo data
    return TRUE;
#endif

    // POINT
    if (NULL != geo->pointxyz) {
        g_free((geocoord*)geo->pointxyz);
        geo->pointxyz = NULL;
    }

    // LINES
    if (NULL != geo->linexyz) {
        g_free((geocoord*)geo->linexyz);
        geo->linexyz = NULL;
    }

    // AREAS
    if (NULL != geo->ringxyz){
        unsigned int i;
        for(i = 0; i < geo->ringnbr; ++i) {
            if (NULL != geo->ringxyz[i])
                g_free((geocoord*)geo->ringxyz[i]);
            geo->ringxyz[i] = NULL;
        }
        g_free((geocoord*)geo->ringxyz);
        geo->ringxyz = NULL;
    }

    if (NULL != geo->ringxyznbr) {
        g_free(geo->ringxyznbr);
        geo->ringxyznbr = NULL;
    }

    geo->linexyznbr = 0;
    geo->ringnbr    = 0;

    return TRUE;
}

int        S57_doneData   (_S57_geo *geo, gpointer user_data)
{
    // quiet line overlap analysis that trigger a bunch of harmless warning
    if (NULL!=user_data && NULL==geo)
        return FALSE;

#ifdef S52_USE_WORLD
    {
        S57_geo *geoNext = NULL;
        if (NULL != (geoNext = S57_getNextPoly(geo))) {
            S57_doneData(geoNext, user_data);
        }
    }
#endif


    _doneGeoData(geo);

    S57_donePrimGeo(geo);

    if (NULL != geo->name)
        g_string_free(geo->name, TRUE);

    if (NULL != geo->attribs)
        g_datalist_clear(&geo->attribs);

    if (NULL != geo->centroid)
        g_array_free(geo->centroid, TRUE);

    g_free(geo);

    return TRUE;
}

int        S57_initPROJ()
// NOTE: corrected for PROJ 4.6.0 ("datum=WGS84")
{
    if (FALSE == _doInit)
        return FALSE;

#ifdef S52_USE_PROJ
    const char *pj_ver = pj_get_release();
    if (NULL != pj_ver)
        PRINTF("PROJ4 VERSION: %s\n", pj_ver);

    // setup source projection
    if (!(_pjsrc = pj_init_plus(_argssrc))){
        PRINTF("error init src PROJ4\n");
        S57_donePROJ();
        return FALSE;
    }
#endif

    if (NULL == _attList)
        _attList = g_string_new("");

    // FIXME: will need resetting for different projection
    _doInit = FALSE;

    return TRUE;
}

int        S57_donePROJ()
{
#ifdef S52_USE_PROJ
    if (NULL != _pjsrc) pj_free(_pjsrc);
    if (NULL != _pjdst) pj_free(_pjdst);
#endif

    _pjsrc  = NULL;
    _pjdst  = NULL;
    _doInit = TRUE;

    if (NULL != _attList)
        g_string_free(_attList, TRUE);

    if (NULL != _pjstr)
        g_free(_pjstr);
    _pjstr = NULL;

    return TRUE;
}

int        S57_setMercPrj(double lat, double lon)
{
    // From: http://trac.osgeo.org/proj/wiki/GenParms (and other link from that page)
    // Note: For merc, PROJ.4 does not support a latitude of natural origin other than the equator (lat_0=0).
    // Note: true scale using the +lat_ts parameter, which is the latitude at which the scale is 1.
    // Note: +lon_wrap=180.0 convert clamp [-180..180] to clamp [0..360]


/* http://en.wikipedia.org/wiki/Web_Mercator
PROJCS["WGS 84 / Pseudo-Mercator",
    GEOGCS["WGS 84",
        DATUM["WGS_1984",
            SPHEROID["WGS 84",6378137,298.257223563,
                AUTHORITY["EPSG","7030"]],
            AUTHORITY["EPSG","6326"]],
        PRIMEM["Greenwich",0,
            AUTHORITY["EPSG","8901"]],
        UNIT["degree",0.0174532925199433,
            AUTHORITY["EPSG","9122"]],
        AUTHORITY["EPSG","4326"]],
    PROJECTION["Mercator_1SP"],
    PARAMETER["central_meridian",0],
    PARAMETER["scale_factor",1],
    PARAMETER["false_easting",0],
    PARAMETER["false_northing",0],
    UNIT["metre",1,
        AUTHORITY["EPSG","9001"]],
    AXIS["X",EAST],
    AXIS["Y",NORTH],
    EXTENSION["PROJ4","+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext +no_defs"],
    AUTHORITY["EPSG","3857"]]
*/

    const char *templ = "+proj=merc +lat_ts=%.6f +lon_0=%.6f +ellps=WGS84 +datum=WGS84 +unit=m";

    if (NULL != _pjstr) {
        PRINTF("WARNING: Merc projection allready set\n");
        return FALSE;
    }

    _pjstr = g_strdup_printf(templ, lat, lon);
    PRINTF("DEBUG: lat:%f, lon:%f [%s]\n", lat, lon, _pjstr);

#ifdef S52_USE_PROJ
    if (NULL != _pjdst)
        pj_free(_pjdst);

    if (!(_pjdst = pj_init_plus(_pjstr))) {
        PRINTF("ERROR: init pjdst PROJ4 (lat:%f) [%s]\n", lat, pj_strerrno(pj_errno));
        g_assert(0);
        return FALSE;
    }
#endif

    return TRUE;
}

GCPTR      S57_getPrjStr(void)
{
    return _pjstr;
}

projXY     S57_prj2geo(projUV uv)
// convert PROJ to geographic (LL)
{
    if (TRUE == _doInit) return uv;
    if (NULL == _pjdst)  return uv;

#ifdef S52_USE_PROJ
    uv = pj_inv(uv, _pjdst);
    if (0 != pj_errno) {
        PRINTF("ERROR: x=%f y=%f %s\n", uv.u, uv.v, pj_strerrno(pj_errno));
        g_assert(0);
        return uv;
    }

    uv.u /= DEG_TO_RAD;
    uv.v /= DEG_TO_RAD;
#endif

    return uv;
}

int        S57_geo2prj3dv(guint npt, double *data)
// convert to XY 'in-place'
{
#ifdef S52_USE_GV
    return TRUE;
#endif

    return_if_null(data);

    pt3 *pt = (pt3*)data;

    if (TRUE == _doInit) {
        S57_initPROJ();
    }

    if (NULL == _pjdst) {
        PRINTF("WARNING: nothing to project to .. load a chart first!\n");
        return FALSE;
    }

#ifdef S52_USE_PROJ
    // deg to rad --latlon
    for (guint i=0; i<npt; ++i, ++pt) {
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
    int ret = pj_transform(_pjsrc, _pjdst, npt, 3, &pt->x, &pt->y, &pt->z);
    if (0 != ret) {
        PRINTF("WARNING: in transform (%i): %s (%f,%f)\n", ret, pj_strerrno(pj_errno), pt->x, pt->y);
        g_assert(0);
        return FALSE;
    }
#endif

    return TRUE;
}

int        S57_geo2prj(_S57_geo *geo)
{
    return_if_null(geo);

    if (TRUE == _doInit)
        S57_initPROJ();

#ifdef S52_USE_PROJ
    guint nr = S57_getRingNbr(geo);
    for (guint i=0; i<nr; ++i) {
        guint   npt;
        double *ppt;
        if (TRUE == S57_getGeoData(geo, i, &npt, &ppt))
            if (FALSE == S57_geo2prj3dv(npt, ppt))
                return FALSE;
    }
#endif  // S52_USE_PROJ

    return TRUE;
}

S57_geo   *S57_setPOINT(geocoord *xyz)
{
    return_if_null(xyz);

    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->id       = _id++;
    geo->obj_t    = POINT_T;
    geo->pointxyz = xyz;

    geo->rect.x1  =  INFINITY;
    geo->rect.y1  =  INFINITY;
    geo->rect.x2  = -INFINITY;
    geo->rect.y2  = -INFINITY;

    geo->scamin   =  INFINITY;

#ifdef S52_USE_WORLD
    geo->nextPoly = NULL;
#endif

    return geo;
}

// experimental
S57_geo   *S57_setGeoLine(_S57_geo *geo, guint xyznbr, geocoord *xyz)
{
    return_if_null(geo);

    geo->obj_t      = LINES_T;  // because some Edge objet default to _META_T when no geo yet
    geo->linexyznbr = xyznbr;
    geo->linexyz    = xyz;

    return geo;
}

S57_geo   *S57_setLINES(guint xyznbr, geocoord *xyz)
{
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    return_if_null(geo);

    geo->id         = _id++;
    geo->obj_t      = LINES_T;
    geo->linexyznbr = xyznbr;
    geo->linexyz    = xyz;

    geo->rect.x1    =  INFINITY;
    geo->rect.y1    =  INFINITY;
    geo->rect.x2    = -INFINITY;
    geo->rect.y2    = -INFINITY;

    geo->scamin     =  INFINITY;


#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

    return geo;
}

#if 0
S57_geo   *S57_setMLINE(guint nLineCount, guint *linexyznbr, geocoord **linexyz)
{
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->id         = _id++;
    geo->obj_t      = MLINE_T;
    geo->linenbr    = nLineCount;
    geo->linexyznbr = linexyznbr;
    geo->linexyz    = linexyz;

#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

    return geo;
}
#endif

S57_geo   *S57_setAREAS(guint ringnbr, guint *ringxyznbr, geocoord **ringxyz, S57_AW_t origAW)
{
    return_if_null(ringxyznbr);
    return_if_null(ringxyz);

    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->id         = _id++;
    geo->obj_t      = AREAS_T;
    geo->ringnbr    = ringnbr;
    geo->ringxyznbr = ringxyznbr;
    geo->ringxyz    = ringxyz;

    geo->rect.x1    =  INFINITY;
    geo->rect.y1    =  INFINITY;
    geo->rect.x2    = -INFINITY;
    geo->rect.y2    = -INFINITY;

    geo->scamin     =  INFINITY;

#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

#ifdef S52_USE_SUPP_LINE_OVERLAP
    geo->origAW     = origAW;
#else
    (void)origAW;
#endif

    return geo;
}

S57_geo   *S57_set_META()
{
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->id       = _id++;
    geo->obj_t    = _META_T;

    geo->rect.x1 =  INFINITY;
    geo->rect.y1 =  INFINITY;
    geo->rect.x2 = -INFINITY;
    geo->rect.y2 = -INFINITY;

    geo->scamin  =  INFINITY;

#ifdef S52_USE_WORLD
    geo->nextPoly = NULL;
#endif

    return geo;
}

int        S57_setName(_S57_geo *geo, const char *name)
// NOTE: this is a S57 object name .. UTF-16 or UTF-8
// use g_string to handle that
{
    return_if_null(geo);
    return_if_null(name);

    geo->name = g_string_new(name);

    return TRUE;
}

GCPTR      S57_getName(_S57_geo *geo)
{
    return_if_null(geo);
    return_if_null(geo->name);

    return geo->name->str;
}

guint      S57_getRingNbr(_S57_geo *geo)
{
    return_if_null(geo);

    // since this is used with S57_getGeoData
    // META object don't need to be projected for rendering
    switch (geo->obj_t) {
        case POINT_T:
        case LINES_T:
            return 1;
        case AREAS_T:
            return geo->ringnbr;
        default:
            return 0;
    }
}

int        S57_getGeoData(_S57_geo *geo, guint ringNo, guint *npt, double **ppt)
// helper providing uniform access to geo
// WARNING: npt is the allocated mem (capacity)
{
    return_if_null(geo);

    if  (AREAS_T==geo->obj_t && geo->ringnbr<ringNo) {
        PRINTF("WARNING: invalid ring number requested! \n");
        *npt = 0;
        g_assert(0);
        return FALSE;
    }

    switch (geo->obj_t) {
    	case _META_T: *npt = 0; break;        // meta geo stuff (ex: C_AGGR)

    	case POINT_T:
            if (NULL != geo->pointxyz) {
                *npt = 1;
                *ppt = geo->pointxyz;
            } else {
                *npt = 0;
            }
            break;

        case LINES_T:
            if (NULL != geo->linexyz) {
                *npt = geo->linexyznbr;
                *ppt = geo->linexyz;
            } else {
                *npt = 0;
            }
            break;

        case AREAS_T:
            if (NULL != geo->ringxyznbr) {
                *npt = geo->ringxyznbr[ringNo];
                *ppt = geo->ringxyz[ringNo];
            } else {
                *npt = 0;
            }

            // debug
            //if (geodata->ringnbr > 1) {
            //    PRINTF("DEBUG: AREA_T ringnbr:%i only exterior ring used\n", geodata->ringnbr);
            //}
            break;
        default:
            PRINTF("ERROR: object type invalid (%i)\n", geo->obj_t);
            g_assert(0);
            return FALSE;
    }

    if (*npt < geo->geoSize) {
        PRINTF("ERROR: geo lenght greater then npt - internal error\n");
        g_assert(0);
        return FALSE;
    }

    if (0==*npt) {
        PRINTF("DEBUG: no geo\n");
        return FALSE;
    }

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

S57_prim  *S57_initPrimGeo(_S57_geo *geo)
{
    return_if_null(geo);

    geo->prim = S57_initPrim(geo->prim);

    return geo->prim;
}

S57_geo   *S57_donePrimGeo(_S57_geo *geo)
{
    return_if_null(geo);

    if (NULL != geo->prim) {
        S57_donePrim(geo->prim);
        geo->prim = NULL;
    }

    return NULL;
}

int        S57_begPrim(_S57_prim *prim, int mode)
{
    _prim p;

    return_if_null(prim);

    p.mode  = mode;
    p.first = prim->vertex->len;

    g_array_append_val(prim->list, p);

    return TRUE;
}

int        S57_endPrim(_S57_prim *prim)
{
    return_if_null(prim);

    _prim *p = &g_array_index(prim->list, _prim, prim->list->len-1);
    if (NULL == p) {
        PRINTF("ERROR: no primitive at index: %i\n", prim->list->len-1);
        g_assert(0);
        return FALSE;
    }

    p->count = prim->vertex->len - p->first;

    // debug
    //if (p->count < 0) {
    //    g_assert(0);
    //}

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

S57_prim  *S57_getPrimGeo(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->prim;
}

guint      S57_getPrimData(_S57_prim *prim, guint *primNbr, vertex_t **vert, guint *vertNbr, guint *vboID)
{
    return_if_null(prim);
    //return_if_null(prim->list);

    *primNbr =            prim->list->len;
    *vert    = (vertex_t*)prim->vertex->data;
    *vertNbr =            prim->vertex->len;
    *vboID   =            prim->DList;

    return TRUE;
}

GArray    *S57_getPrimVertex(_S57_prim *prim)
{
    return_if_null(prim);

    return prim->vertex;
}

int        S57_setPrimDList (_S57_prim *prim, guint DList)
{
    return_if_null(prim);

    prim->DList = DList;

    return TRUE;
}

int        S57_getPrimIdx(_S57_prim *prim, unsigned int i, int *mode, int *first, int *count)
//int        S57_getPrimIdx(_S57_prim *prim, guint i, guint *mode, guint *first, guint *count)
{
    return_if_null(prim);

    if (i>=prim->list->len)
        return FALSE;

    _prim *p = &g_array_index(prim->list, _prim, i);
    if (NULL == p) {
        PRINTF("ERROR: no primitive at index: %i\n", i);
        g_assert(0);
        return FALSE;
    }

    *mode  = p->mode;
    *first = p->first;
    *count = p->count;

    return TRUE;
}

int        S57_setExt(_S57_geo *geo, double x1, double y1, double x2, double y2)
// assume: extent canonical
{
    return_if_null(geo);

    // debug
    //if (0 == g_strncasecmp(geo->name->str, "M_COVR", 6)) {
    //    PRINTF("DEBUG: %s: %f, %f  UR: %f, %f\n", geo->name->str, x1, y1, x2, y2);
    //}

    // canonize lng
    //x1 = (x1 < -180.0) ? 0.0 : (x1 > 180.0) ? 0.0 : x1;
    //x2 = (x2 < -180.0) ? 0.0 : (x2 > 180.0) ? 0.0 : x2;
    // canonize lat
    //y1 = (y1 < -90.0) ? 0.0 : (y1 > 90.0) ? 0.0 : y1;
    //y2 = (y2 < -90.0) ? 0.0 : (y2 > 90.0) ? 0.0 : y2;

    /*
    // check prime-meridian crossing
    if ((x1 < 0.0) && (0.0 < x2)) {
        PRINTF("DEBUG: prime-meridian crossing %s: LL: %f, %f  UR: %f, %f\n", geo->name->str, x1, y1, x2, y2);
        g_assert(0);
    }
    */

    if (isinf(x1) && isinf(x2)) {
        PRINTF("DEBUG: %s: LL: %f, %f  UR: %f, %f\n", geo->name->str, x1, y1, x2, y2);
        g_assert(0);
        return FALSE;
    }

    geo->rect.x1 = x1;
    geo->rect.y1 = y1;
    geo->rect.x2 = x2;
    geo->rect.y2 = y2;

    return TRUE;
}

int        S57_getExt(_S57_geo *geo, double *x1, double *y1, double *x2, double *y2)
// assume: extent canonical
{
    return_if_null(geo);

    *x1 = geo->rect.x1; // W
    *y1 = geo->rect.y1; // S
    *x2 = geo->rect.x2; // E
    *y2 = geo->rect.y2; // N

    return TRUE;
}

S57_Obj_t  S57_getObjtype(_S57_geo *geo)
//S52_Obj_t  S57_getObjtype(_S57_geo *geo)
{
    if (NULL == geo)
        return _META_T;

    return geo->obj_t;
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

int        S57_getNumAtt(S57_geo *geo)
{
    int cnt = 0;
    g_datalist_foreach(&geo->attribs, _countItems, &cnt);
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
int        S57_getAttributes(_S57_geo *geo, char **name, char **val)
{
  struct _qwerty tmp;

  tmp.currentIdx = 0;
  tmp.name       = name;
  tmp.value      = val;

  g_datalist_foreach(&geo->attribs, _getAttValues,  &tmp);
  //  strcpy(name[tmp.currentIdx], "x");
  //  strcpy(val[tmp.currentIdx], "y");
  return tmp.currentIdx;
}
#endif

GString   *S57_getAttVal(_S57_geo *geo, const char *att_name)
// return attribute string value or NULL if:
//      1- attribute name abscent
//      2- its a mandatory attribute but its value is not define (EMPTY_NUMBER_MARKER)
{
    return_if_null(geo);
    return_if_null(att_name);

    //GString *att = (GString*) g_datalist_get_data(&geo->attribs, att_name);
    //GString *att = (GString*) g_dataset_id_get_data(&geo->attribs, g_quark_try_string(att_name));
    GQuark   q   = g_quark_from_string(att_name);
    //GQuark   q   = g_quark_from_static_string(att_name);
    GString *att = (GString*) g_datalist_id_get_data(&geo->attribs, q);

    if (NULL!=att && (0==g_strcmp0(att->str, EMPTY_NUMBER_MARKER))) {
        //PRINTF("NOTE: mandatory attribute (%s) with ommited value\n", att_name);
        return NULL;
    }

    // display this NOTE once (because of to many warning)
    static int silent = FALSE;
    if (!silent && NULL!=att && 0==att->len) {
        PRINTF("NOTE: attribute (%s) has no value [obj:%s]\n", att_name, geo->name->str);
        PRINTF("      (this msg will not repeat)\n");
        silent = TRUE;
        return NULL;
    }

    return att;
}

static void   _string_free(gpointer data)
{
    g_string_free((GString*)data, TRUE);
}

GData     *S57_setAtt(_S57_geo *geo, const char *name, const char *val)
{
    return_if_null(geo);
    return_if_null(name);
    return_if_null(val);

    GQuark   qname = g_quark_from_string(name);
    GString *value = g_string_new(val);

    if (NULL == geo->attribs)
        g_datalist_init(&geo->attribs);

#ifdef S52_USE_SUPP_LINE_OVERLAP
    if ((0==g_strcmp0(S57_getName(geo), "Edge")) && (0==g_strcmp0(name, "RCID"))) {
         geo->rcidstr = value;
     }
#endif

    g_datalist_id_set_data_full(&geo->attribs, qname, value, _string_free);

    return geo->attribs;
}

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
    //return geo->scamin = (NULL==S57_getAttVal(geo, "SCAMIN")) ? UNKNOWN : S52_atof(valstr->str);
}

#ifdef S52_USE_C_AGGR_C_ASSO
int        S57_setRelationship(_S57_geo *geo, _S57_geo *geoRel)
{
    return_if_null(geo);
    return_if_null(geoRel);

    if (NULL == geo->relation) {
        geo->relation = geoRel;
    } else {
        // FIXME: ENC_ROOT/US3NY21M/US3NY21M.000 has multiple relation for the same object
        PRINTF("WARNING: 'geo->relation' allready in use ..\n");
        g_assert(0);
        return FALSE;
    }

    return TRUE;
}

S57_geo  * S57_getRelationship(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->relation;
}
#endif  // S52_USE_C_AGGR_C_ASSO

static void   _printAtt(GQuark key_id, gpointer data, gpointer user_data)
{
    // 'user_data' not used
    (void) user_data;

    const gchar *attName  = g_quark_to_string(key_id);

    // print only S57 attrib - assuming that OGR att are not 6 char in lenght!!
    if (6 == S52_strlen(attName)) {
        GString *attValue = (GString*) data;
        PRINTF("\t%s : %s\n", attName, attValue->str);
    }
}

gboolean   S57_setSupp(_S57_geo *geo, gboolean supp)
{
    return_if_null(geo);

    geo->supp = supp;

    return geo->supp;
}

gboolean   S57_getSupp(_S57_geo *geo)
// FIXME: bellow is an accesor - 'inline' it somehow
{
    return_if_null(geo);

    return geo->supp;
}

int        S57_dumpData(_S57_geo *geo, int dumpCoords)
// debug
// if dumpCoords is TRUE dump all coordinates else dump extent
{
    return_if_null(geo);


    PRINTF("S57ID : %i\n", geo->id);
    PRINTF("NAME  : %s\n", geo->name->str);

    g_datalist_foreach(&geo->attribs, _printAtt, NULL);

    {    // print coordinate
        guint     npt = 0;
        geocoord *ppt;

        S57_getGeoData(geo, 0, &npt, &ppt);

        switch (geo->obj_t) {
            case _META_T:  PRINTF("_META_T (%i)\n", npt); break;
            case POINT_T:  PRINTF("POINT_T (%i)\n", npt); break;
            case LINES_T:  PRINTF("LINES_T (%i)\n", npt); break;
            case AREAS_T:  PRINTF("AREAS_T (%i)\n", npt); break;
            default:
                PRINTF("WARNING: invalid object type; %i\n", geo->obj_t);
        }

        if (TRUE == dumpCoords) {
            guint     npt = 0;
            geocoord *ppt = NULL;

            if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt))
                return FALSE;

            for (guint i=0; i<npt; ++i) {
                PRINTF("\t\t(%f, %f, %f)\n", ppt[0], ppt[1], ppt[2]);
                ppt += 3;
            }
        } else {
            // dump extent
            PRINTF("EXTENT: %f, %f  --  %f, %f\n",
                   geo->rect.y1, geo->rect.x1, geo->rect.y2, geo->rect.x2);

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
    if (0 == g_strcmp0("NINFOM",    attName)) return;

    // save S57 attribute + system attribute (ex vessel name - AIS)
    if (0 != attList->len)
        g_string_append(attList, ",");

    g_string_append(attList, attName);
    g_string_append_c(attList, ':');
    g_string_append(attList, attValue->str);

    // FIXME: do not replace '\n' by ' ', for JSON
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

GCPTR      S57_getAtt(_S57_geo *geo)
{
    return_if_null(geo);


    PRINTF("S57ID : %i\n", geo->id);
    PRINTF("NAME  : %s\n", geo->name->str);

    g_string_set_size(_attList, 0);
    g_string_printf(_attList, "%i", geo->id);

    g_datalist_foreach(&geo->attribs, _getAtt, _attList);

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

    guint i = 0;
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

        for (guint i=0; i<geoA->linexyznbr; ++i) {
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
}

S57_geo   *S57_getGeoLink(_S57_geo *geo)
{
    // not needed
    //return_if_null(geo);

    return geo->link;
}

S57_geo   *S57_setGeoLink(_S57_geo *geo, _S57_geo *link)
{
    // not needed
    //return_if_null(geo);

    geo->link = link;

    return geo;
}
#endif

#ifdef S52_USE_WORLD
S57_geo   *S57_setNextPoly(_S57_geo *geo, _S57_geo *nextPoly)
{
    return_if_null(geo);

    if (NULL != geo->nextPoly)
        nextPoly->nextPoly = geo->nextPoly;

    geo->nextPoly = nextPoly;

    return geo;
}


S57_geo   *S57_getNextPoly(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->nextPoly;
}

S57_geo   *S57_delNextPoly(_S57_geo *geo)
// unlink Poly chain
{
    return_if_null(geo);

    while (NULL != geo) {
        S57_geo *nextPoly = geo->nextPoly;
        geo->nextPoly = NULL;
        geo = nextPoly;
    }

    return NULL;
}
#endif

guint      S57_getGeoID(S57_geo *geo)
{
    return_if_null(geo);

    return  geo->id;
}

int        S57_isPtInside(int npt, double *xyz, gboolean close, double x, double y)
// return TRUE if (x,y) inside area (close/open) xyz else FALSE
// FIXME: CW or CCW or work with either?
{
    return_if_null(xyz);

    int c = 0;
    pt3 *v = (pt3 *)xyz;

    if (0 == npt)
        return FALSE;

    if (TRUE == close) {
        for (int i=0; i<npt-1; ++i) {
            pt3 p1 = v[i];
            pt3 p2 = v[i+1];

            if ( ((p1.y>y) != (p2.y>y)) &&
                (x < (p2.x-p1.x) * (y-p1.y) / (p2.y-p1.y) + p1.x) )
                c = !c;
        }
    } else {
        for (int i=0, j=npt-1; i<npt; j=i++) {
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
    guint   nptA;
    double *pptA;
    guint   nptB;
    double *pptB;

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
        if (TRUE == S57_isPtInside(nptB, pptB, TRUE, pptA[0], pptA[1]))
            return TRUE;
    }

    return FALSE;
}

guint      S57_getGeoSize(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->geoSize;
}

guint      S57_setGeoSize(_S57_geo *geo, guint size)
{
    return_if_null(geo);

    if ((POINT_T==geo->obj_t) && (size > 1)) {
        PRINTF("ERROR: POINT_T size\n");
        g_assert(0);
        return FALSE;
    }
    if ((LINES_T==geo->obj_t) && (size > geo->linexyznbr)) {
        PRINTF("ERROR: LINES_T size\n");
        g_assert(0);
        return FALSE;
    }
    if ((AREAS_T==geo->obj_t) && (size > geo->ringxyznbr[0])) {
        PRINTF("ERROR: AREAS_T size\n");
        g_assert(0);
        return FALSE;
    }

    if (_META_T == geo->obj_t) {
        PRINTF("ERROR: object type invalid (%i)\n", geo->obj_t);
        g_assert(0);
        return FALSE;
    }

    return geo->geoSize = size;
}

int        S57_newCentroid(_S57_geo *geo)
// init or reset
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

    pt2 pt = {x, y};
    g_array_append_val(geo->centroid, pt);

    return TRUE;
}

int        S57_getNextCentroid(_S57_geo *geo, double *x, double *y)
{
    return_if_null(geo);
    return_if_null(geo->centroid);

    if (geo->centroidIdx < geo->centroid->len) {
        pt2 pt = g_array_index(geo->centroid, pt2, geo->centroidIdx);
        *x = pt.x;
        *y = pt.y;

        ++geo->centroidIdx;
        return TRUE;
    }


    return FALSE;
}

int        S57_hasCentroid(S57_geo *geo)
{
    return_if_null(geo);
    //return_if_null(geo->centroid);

    if (NULL == geo->centroid) {
        S57_newCentroid(geo);
    } else {
        // reset idx for call S57_getNextCentroid
        geo->centroidIdx = 0;
    }

    if (0 == geo->centroid->len)
        return FALSE;

    return TRUE;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
int        S57_markOverlapGeo(_S57_geo *geo, _S57_geo *geoEdge)
// mark coordinates in geo that match the chaine-node in geoEdge
{

    return_if_null(geo);
    return_if_null(geoEdge);

    guint   npt = 0;
    double *ppt;
    if (FALSE == S57_getGeoData(geo, 0, &npt, &ppt)) {
        PRINTF("WARNING: S57_getGeoData() failed\n");
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
    int   next = 0;
    guint i    = 0;
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
        if ( tmp < 0) {
            PRINTF("ERROR: tmp < 0\n");
            g_assert(0);
            return FALSE;
        }
    }

    if (1 == next) {
        if (nptEdge + i > npt) {
            PRINTF("ERROR: nptEdge + i > npt\n");
            g_assert(0);
            return FALSE;
        }
    }

    // optimisation not usefull in this case since it's a one time pass
    // FIXME: optimisation: push Z one to many edge
    // FIXME: optimisation: check if moving vertex to clip plane (Z_CLIP_PLANE)
    for (guint j=0; j<nptEdge; ++j) {
        ppt[i*3 + 2] = -10.0; // not Z_CLIP_PLANE
        i += next;
    }

    return TRUE;
}

GString   *S57_getRCIDstr(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->rcidstr;
}

S57_AW_t   S57_getOrigAW(S57_geo *geo)
// debug
{
    return_if_null(geo);

    return geo->origAW;
}
#endif  // S52_USE_SUPP_LINE_OVERLAP

int        S57_highlightON (_S57_geo *geo)
{
    return_if_null(geo);

    geo->highlight = TRUE;

    return TRUE;
}

int        S57_highlightOFF(_S57_geo *geo)
{
    return_if_null(geo);

    geo->highlight = FALSE;

    return TRUE;
}

gboolean   S57_isHighlighted(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->highlight;
}

int        S57_setHazard(S57_geo *geo, gboolean hazard)
{
    return_if_null(geo);

    geo->hazard = hazard;

    return TRUE;
}

int        S57_isHazard(S57_geo *geo)
{
    return_if_null(geo);

    return geo->hazard;
}


#if 0
int main(int argc, char** argv)
{

   return 1;
}
#endif
