// S57data.c: S57 geo data
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


#include "S57data.h"    // S57_geo
//#include "S52utils.h"   // PRINTF()

#include <math.h>       // INFINITY, nearbyint()

#ifdef S52_USE_PROJ
static projPJ      _pjsrc   = NULL;   // projection source
static projPJ      _pjdst   = NULL;   // projection destination
static CCHAR      *_pjstr   = NULL;
static int         _doInit  = TRUE;   // will set new src projection
static const char *_argssrc = "+proj=latlong +ellps=WGS84 +datum=WGS84";
//static const char *_argsdst = "+proj=merc +ellps=WGS84 +datum=WGS84 +unit=m +no_defs";
// Note: ../../../FWTools/FWTools-2.0.6/bin/gdalwarp
//       -t_srs "+proj=merc +ellps=WGS84 +datum=WGS84 +unit=m +no_defs"
//        46307260_LOD2.tif 46307260_LOD2.merc.tif
// FIXME: test POLAR ENC omerc:
//  "+proj=omerc +lat_0=4 +lonc=115 +alpha=53.31582047222222 +k=0.99984 +x_0=590476.8727431979 +y_0=442857.6545573985
//   +ellps=evrstSS +towgs84=-533.4,669.2,-52.5,0,0,4.28,9.4 +to_meter=0.3047994715386762 +no_defs ";
#endif

// MAXINT-6 is how OGR tag an UNKNOWN value
// see gdal/ogr/ogrsf_frmts/s57/s57.h:126
// it is then turn into a string in gv_properties
//#define EMPTY_NUMBER_MARKER "2147483641"  /* MAXINT-6 */

// object's internal ID
static unsigned int _S57ID = 1;  // start at 1, the number of object loaded

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
#define S57_ATT_NM_LN    6   // S57 Class Attribute Name lenght
#define S57_GEO_NM_LN   13   // GDAL/OGR primitive max name length: "ConnectedNode"
typedef struct _S57_geo {
    guint        S57ID;          // record ID / S52ObjectHandle use as index in S52_obj GPtrArray
                                 // Note: must be the first member for S57_getS57ID(geo)

    //guint        s52objID;     // optimisation: numeric value of OBCL string

    char         name[S57_GEO_NM_LN+1]; //  6 - object name    + '\0'
                                        //  8 - WOLDNM         + '\0'
                                        // 13 - ConnectedNode  + '\0'

    S57_Obj_t    objType;       // PL & S57 - P/L/A

    ObjExt_t     ext;         // geographic coordinate
    //ObjExt_t     extGEO;         // geographic coordinate
    //ObjExt_t     extPRJ;         // projected coordinate

    // length of geo data (POINT, LINE, AREA) currently in buffer (NOT capacity)
    guint        geoSize;        // max is 1 point / linexyznbr / ringxyznbr[0]

    // hold coordinate before and after projection
    // FIXME: why alloc xyz*1, easy to handle like the reste, but fragment mem !?!
    geocoord    *pointxyz;    // point (alloc)

    guint        linexyznbr;  // line number of point XYZ (alloc/capacity)
    geocoord    *linexyz;     // line coordinate

    // area
    guint        ringnbr;     // number of ring
    guint       *ringxyznbr;  // number coords per ring (alloc/capacity)
    geocoord   **ringxyz;     // coords of rings        (alloc)

    // hold tessalated geographic and projected coordinate of area
    // in a format suitable for OpenGL
    S57_prim    *prim;

    // scamin overide attribs val
    double       scamin;      // 0.0 - S57_RESET_SCAMIN val resetted by CS DEPCNT02, _UDWHAZ03 (via OBSTRN04, WRECKS02)

    // FIXME: SCAMAX
    // FIXME:
    // double drval1 init UNKNOWN
    // double drval2 init unknown
    // ...

//12377 valName:DRVAL1
//15262 valName:SCAMIN
//17010 valName:CATLMK
//43629 valName:DRVAL2
//2350410 valName:LNAM

    //gooblean     isUTF8;  // text in attribs
    GData       *attribs;

#ifdef S52_USE_C_AGGR_C_ASSO
    // point to the S57 relationship object C_AGGR / C_ASSO this S57_geo belong
    // FIXME: handle multiple relation for the same object (ex US3NY21M/US3NY21M.000, CA379035.000)
    S57_geo     *relation;
#endif

    // for CS - object "touched" by this object - ref to object local to this object
    // FIXME: union assume use is exclusif - check this!
    //union {
    struct {
        S57_geo *TOPMAR; // break out objet class "touched"
        S57_geo *LIGHTS; // break out objet class "touched"
        S57_geo *DEPCNT; // break out objet class "touched"
        S57_geo *UDWHAZ; // break out objet class "touched"
        S57_geo *DEPVAL; // break out objet class "touched"
    } touch;

#ifdef S52_USE_SUPP_LINE_OVERLAP
    // only for object "Edge"
    gchar       *name_rcidstr;  // optimisation: point to Att NAME_RCID str value

    S57_geo     *geoOwner;      // s57 obj that use this edge

    //S57_AW_t     origAW;        // debug - original Area Winding, CW: area < 0,  CCW: area > 0
#endif

    // centroid - save current centroids of this object
    // optimisation mostly for layer 9 AREA (FIXME: exemple of centroid on layer 9 ?!)
    guint        centroidIdx;
    GArray      *centroid;

#ifdef S52_USE_WORLD
    S57_geo     *nextPoly;
#endif

    gboolean     highlight;  // highlight this geo object (cursor pick / hazard - experimental)

    //gboolean     hazard;     // TRUE if a Safety Contour / hazard - use by leglin and GUARDZONE

    // optimisation: set LOD
    //S57_setLOD(obj, *c->dsid_intustr->str);
    //char       LOD;           // optimisation: chart purpose: cell->dsid_intustr->str

} _S57_geo;

static GString *_attList = NULL;

// tolerance used by: _inLine(), S57_isPtInSet(), posibly S57_cmpGeoExt()
//#define S57_GEO_TOLERANCE 0.0001     // *60*60 = .36'
//#define S57_GEO_TOLERANCE 0.00001    // *60*60 = .036'   ; * 1852 =
//#define S57_GEO_TOLERANCE 0.000001   // *60*60 = .0036'  ; * 1852 = 6.667 meter       _simplifyGEO(): CA27904A.000 (Gulf): CTNARE:4814 poly reduction: 80% 12683 (15064	->	2381)
#define S57_GEO_TOLERANCE 0.0000001  // *60*60 = .00036' ; * 1852 = 0.6667 meter      _simplifyGEO(): CA27904A.000 (Gulf): CTNARE:4814 poly reduction: 40%  6309 (15064	->	8755)
//#define S57_GEO_TOLERANCE 0.00000001   // *60*60 = .000036'; * 1852 = 0.06667 meter     _simplifyGEO(): CA27904A.000 (Gulf): CTNARE:4814 poly reduction:  8%  1302 (15064	->	13762)
#define S57_GEO_TOL_LINES 0.000001

// when check for Z0=Z1=Z2
//S57data.c:544 in _simplifyGEO(): CTNARE:4814 poly reduction: 0 (no reduction)
// Z0=Z2
//S57data.c:538 in _simplifyGEO(): DEBUG: CTNARE:4814 poly reduction: 12683 	(15064	->	2381)
// no Z check
//S57data.c:542 in _simplifyGEO(): DEBUG: CTNARE:4814 poly reduction: 12683 	(15064	->	2381)

static int    _initPROJ()
// Note: corrected for PROJ 4.6.0 ("datum=WGS84")
{
    if (FALSE == _doInit)
        return FALSE;

#ifdef S52_USE_PROJ
    const char *pj_ver = pj_get_release();
    if (NULL != pj_ver)
        PRINTF("PROJ4 VERSION: %s\n", pj_ver);

    // setup source projection
    if (!(_pjsrc = pj_init_plus(_argssrc))){
        PRINTF("ERROR: init src PROJ4\n");
        S57_donePROJ();
        g_assert(0);
        return FALSE;
    }
#endif

    if (NULL == _attList)
        _attList = g_string_new("");

    // FIXME: will need resetting for different projection
    _doInit = FALSE;

    return TRUE;
}

int        S57_donePROJ(void)
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
    _attList = NULL;

    g_free((gpointer)_pjstr);
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

    // FIXME: POLAR ENC
    // const char *templ = "+proj=omerc +lat_0=4 +lonc=115 +alpha=53.31582047222222 +k=0.99984
    //                      +x_0=590476.8727431979 +y_0=442857.6545573985
    //                      +ellps=evrstSS +towgs84=-533.4,669.2,-52.5,0,0,4.28,9.4
    //                      +to_meter=0.3047994715386762 +no_defs ";
    //
    //FAIL: ENC skewed: const char *templ = "+proj=omerc +lat_0=%.6f +lonc=%.6f +x_0=0 +y_0=0 +alpha=45 +gamma=0 +k_0=1 +ellps=WGS84 +no_defs";

    const char *templ = "+proj=merc +lat_ts=%.6f +lon_0=%.6f +ellps=WGS84 +datum=WGS84 +unit=m +no_defs";
    // FIXME: utm tilt ENC .. why?
    //const char *templ = "+proj=utm +lat_ts=%.6f +lon_0=%.6f +ellps=WGS84 +datum=WGS84 +unit=m +no_defs";

    if (NULL != _pjstr) {
        PRINTF("WARNING: Merc projection str allready set\n");
        return FALSE;
    }

    _pjstr = g_strdup_printf(templ, lat, lon);
    PRINTF("DEBUG: lat:%f, lon:%f [%s]\n", lat, lon, _pjstr);

#ifdef S52_USE_PROJ
    if (NULL != _pjdst)
        pj_free(_pjdst);

    _pjdst = pj_init_plus(_pjstr);
    if (FALSE == _pjdst) {
        PRINTF("ERROR: init pjdst PROJ4 (lat:%f) [%s]\n", lat, pj_strerrno(pj_errno));
        g_assert(0);
        return FALSE;
    }
#endif

    return TRUE;
}

CCHAR     *S57_getPrjStr(void)
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

int        S57_geo2prj3dv(guint npt, pt3 *data)
// convert a vector of lon/lat/z (pt3) to XY(z) 'in-place'
{
#ifdef S52_USE_GV
    return TRUE;
#endif

    return_if_null(data);

    //pt3 *pt = (pt3*)data;
    pt3 *pt = data;

    if (TRUE == _doInit) {
        _initPROJ();
    }

    if (NULL == _pjdst) {
        PRINTF("WARNING: nothing to project to .. load a chart first!\n");
        return FALSE;
    }

#ifdef S52_USE_PROJ
    // deg to rad --latlon
    for (guint i=0; i<npt; ++i, ++pt) {
        pt->x *= DEG_TO_RAD;
        pt->y *= DEG_TO_RAD;
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

    /*
    // FIXME: test heuristic to reduce the number of point (for LOD):
    // try to (and check) reduce the number of points by flushing decimal
    // then libtess should remove coincident points.
    //
    // Other trick, try to reduce more by rounding using cell scale
    // pt->x = nearbyint(pt->x / (? * 10)) / (? * 10);
    //
    // test - 1km
    pt = (pt3*)data;
    for (guint i=0; i<npt; ++i, ++pt) {
        pt->x = nearbyint(pt->x / 1000.0) * 1000.0;
        pt->y = nearbyint(pt->y / 1000.0) * 1000.0;
    }
    //*/
#endif

    return TRUE;
}

//#if 0
// FIXME: this break line/poly match
static int    _inLine(pt3 A, pt3 B, pt3 C)
// TRUE if BC is inline with AC or visa-versa
// FIXME: adjust S57_GEO_TOL_LINES to nav purpose (INTU) or LOD (_SCAMIN)
{
    // test.1: A( 0, 0) B(2,2) C( 4, 4)
    // test.2: A(-2,-2) B(0,0) C(+2,+2)
    //A.x=+2;A.y=+2;B.x=0  ;B.y=0  ;C.x=-2;C.y=-2;
    //A.x=-2;A.y=-2;B.x=0  ;B.y=0  ;C.x=+2;C.y=+2;
    //A.x=-2;A.y=-2;B.x=-11;B.y=-11;C.x=+2;C.y=+2;

    // from: https://stackoverflow.com/questions/17692922/check-is-a-point-x-y-is-between-two-points-drawn-on-a-straight-line/17693189
    // if AC is vertical
    //if (A.x == C.x) return B.x == C.x;
    if (ABS(A.x-C.x) < S57_GEO_TOL_LINES) return ABS(B.x-C.x) < S57_GEO_TOL_LINES;

    // if AC is horizontal
    //if (A.y == C.y) return B.y == C.y;
    if (ABS(A.y-C.y) < S57_GEO_TOL_LINES) return ABS(B.y-C.y) < S57_GEO_TOL_LINES;

    // match the gradients (BUG: maybe after edit of '/' to '*')
    //return (A.x - C.x)*(A.y - C.y) == (C.x - B.x)*(C.y - B.y);
    //ex: -4*-4 == 2*2 = 16 == 4 !?!

    // slope: (y2-y1)/(x2-x1)
    // so slope AC == slope BC
    //return (A.y-C.y)/(A.x-C.x) == (B.y-C.y)/(B.x-C.x);  // div 0, need test above
    //return (A.y-C.y)*(B.x-C.x) == (B.y-C.y)*(A.x-C.x);  // inf
    return ABS(ABS((A.y-C.y)*(B.x-C.x)) - ABS((B.y-C.y)*(A.x-C.x))) < S57_GEO_TOL_LINES;

    // determinant = (ax-cx)(by-cy) - (bx-cx)(ay-cy)
    //return (A.x - C.x)*(B.y - C.y) == (B.x - C.x)*(A.y - C.y);
    // >0 above, <0 bellow, =0 on line
    // ex: (0-4)(2-4) - (2-4)(0-4) = 8-8 = 0
}

static guint  _delInLineSeg(guint npt, double *ppt)
// remove point ON the line segment
{
    //* FIXME: optimisation: use pt3 newArr[npt];
    // and only one memmove(ppt, newArr, sizeof(pt3) * j);  (see fail test bellow)
    //pt3 newArr[npt]; guint ii = 0, k = 0;

    pt3  *p = (pt3*)ppt;
    guint j = npt;
    for (guint i=0; i<(npt-2); ++i) {

        //if (p[0].z == p[1].z == p[2].z) {
        if (p[0].z == p[2].z) {
            // remove p[1]
            if (TRUE == _inLine(p[0], p[1], p[2])) {
                // A--B--C,       3-0-2=1
                // 0--A--B--C,    4-1-2=1
                // 0--A--B--C--0, 5-1-2=2
                memmove(&p[1], &p[2], sizeof(pt3) * (npt - i - 2));
                --j;
            } else {
                ++p;
            }
        } else {
            ++p;
        }
    }

#ifdef S52_DEBUG
    /* debug: check for duplicate vertex
    p = (pt3*)ppt;
    guint nDup = 0;
    for (guint i=1; i<j; ++i) {
        if ((p[i-1].x == p[i].x) && (p[i-1].y == p[i].y)) {
            // FIXME: check (p[i-1].z == p[i].z)
            ++nDup;
        }
    }
    if (0 != nDup) {
        PRINTF("DEBUG: dup %i\n", nDup);
        //g_assert(0);
    }
    //*/
#endif  // S52_DEBUG

    return j;

    /*
    if (TRUE == _inLine(p[0], p[k+1], p[k+2])) {
        --j;
        ++k;
    } else {
        newArr[ii++] = *p++;
        p += k;
    }
    //memmove(ppt, newArr, sizeof(pt3) * j);
    */

    /* FIXME: this fail
    pt3  newArr[npt];
    guint i = 0, j = 0, k = 0;
    while (i<(npt-2)) {
        if (TRUE == _inLine(p[0], p[k+1], p[k+2])) {
            ++k;
        } else {
            newArr[j++] = p[0];
            if (0 < k) {
                //newArr[j++] = p[k];
                //newArr[j++] = p[k+1];  // p[k+2]
                newArr[j++] = p[k-1];  // p[old_k]
                k = 0;
            }
        }
        ++p;
        ++i;
    }
    */
}

static int    _simplifyGEO(_S57_geo *geo)
{
    // LINE
    if (S57_LINES_T == geo->objType) {

        // need at least 3 pt
        if (2 < geo->linexyznbr) {
            guint npt = _delInLineSeg(geo->linexyznbr, geo->linexyz);
            if (npt != geo->linexyznbr) {
                //PRINTF("DEBUG: line reduction: %i \t(%i\t->\t%i)\n", geo->linexyznbr - npt, geo->linexyznbr, npt);
                geo->linexyznbr = npt;
            }
        }

        // degenerated after simplification!
        if (2 > geo->linexyznbr) {
            // FIXME: delete geo!
            PRINTF("WARNING: degenerated line: %i\n", geo->linexyznbr);
            g_assert(0);
        }
    }

    // AREA
    if (S57_AREAS_T == geo->objType) {
        for (guint i=0; i<geo->ringnbr; ++i) {
            if (3 < geo->ringxyznbr[i]) {
                guint npt = _delInLineSeg(geo->ringxyznbr[i], geo->ringxyz[i]);
                if (npt != geo->ringxyznbr[i]) {
                    PRINTF("DEBUG: %s:%i poly reduction: %i \t(%i\t->\t%i)\n", geo->name, geo->S57ID, geo->ringxyznbr[i] - npt, geo->ringxyznbr[i], npt);
                    geo->ringxyznbr[i] = npt;
                }
            }

            // degenerated after simplification!
            if (3 > geo->ringxyznbr[i]) {
                // FIXME: delete ring!
                PRINTF("WARNING: ring with less than 3 vertex (%i)\n", geo->ringxyznbr[i]);
                g_assert(0);
            }
        }
    }

    // debug -  CA27904A.000 (Gulf) CTNARE:4814 (caution area)
    //if (4814 == geo->S57ID)
    //    geo->highlight = TRUE;

    return TRUE;
}
//#endif  // 0

int        S57_geo2prj(_S57_geo *geo)
{
    // useless - rbin
    //return_if_null(geo);

    // FIXME: this break line/poly match
    // FIX: call on area object with a sy() (centroid in area) and no AC() or AP()
    if ('A' == geo->objType) {
        if ((0 == g_strcmp0(geo->name, "ISTZNE")) ||
            (0 == g_strcmp0(geo->name, "TSSLPT")) ||
            (0 == g_strcmp0(geo->name, "CTNARE")))   // LC()
        {
            _simplifyGEO(geo);
        }
    }

    if (TRUE == _doInit)
        _initPROJ();

#ifdef S52_USE_PROJ
    guint nr = S57_getRingNbr(geo);
    for (guint i=0; i<nr; ++i) {
        guint   npt;
        double *ppt;
        if (TRUE == S57_getGeoData(geo, i, &npt, &ppt)) {
            if (FALSE == S57_geo2prj3dv(npt, (pt3*)ppt))
                return FALSE;
        }
    }
#endif  // S52_USE_PROJ

    return TRUE;
}

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

    if (NULL != geo->attribs)
        g_datalist_clear(&geo->attribs);

    if (NULL != geo->centroid)
        g_array_free(geo->centroid, TRUE);

    g_free(geo);

    return TRUE;
}

S57_geo   *S57_setPOINT(geocoord *xyz)
{
    return_if_null(xyz);

    // FIXME: use g_slice()
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->S57ID    = _S57ID++;
    geo->objType  = S57_POINT_T;
    geo->pointxyz = xyz;

    geo->ext.W    =  INFINITY;
    geo->ext.S    =  INFINITY;
    geo->ext.E    = -INFINITY;
    geo->ext.N    = -INFINITY;

    geo->scamin   =  S57_RESET_SCAMIN;

#ifdef S52_USE_WORLD
    geo->nextPoly = NULL;
#endif

    return geo;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
// experimental
S57_geo   *S57_setGeoLine(_S57_geo *geo, guint xyznbr, geocoord *xyz)
{
    return_if_null(geo);

    geo->objType    = S57_LINES_T;  // because some Edge objet default to _META_T when no geo yet
    geo->linexyznbr = xyznbr;
    geo->linexyz    = xyz;

    return geo;
}
#endif  // S52_USE_SUPP_LINE_OVERLAP

S57_geo   *S57_setLINES(guint xyznbr, geocoord *xyz)
{
    // Edge might have 0 node
    //return_if_null(xyz);

    // FIXME: use g_slice()
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->S57ID      = _S57ID++;
    geo->objType    = S57_LINES_T;
    geo->linexyznbr = xyznbr;
    geo->linexyz    = xyz;

    geo->ext.W      =  INFINITY;
    geo->ext.S      =  INFINITY;
    geo->ext.E      = -INFINITY;
    geo->ext.N      = -INFINITY;

    geo->scamin     =  S57_RESET_SCAMIN;


#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

    return geo;
}

#if 0
//*
S57_geo   *S57_setMLINE(guint nLineCount, guint *linexyznbr, geocoord **linexyz)
{
    // FIXME: use g_slice()
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->ID         = _ID++;
    geo->objType    = MLINE_T;
    geo->linenbr    = nLineCount;
    geo->linexyznbr = linexyznbr;
    geo->linexyz    = linexyz;

#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

    return geo;
}
//*/
#endif  // 0

//S57_geo   *S57_setAREAS(guint ringnbr, guint *ringxyznbr, geocoord **ringxyz, S57_AW_t origAW)
S57_geo   *S57_setAREAS(guint ringnbr, guint *ringxyznbr, geocoord **ringxyz)
{
    return_if_null(ringxyznbr);
    return_if_null(ringxyz);

    // FIXME: use g_slice()
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->S57ID      = _S57ID++;
    geo->objType    = S57_AREAS_T;
    geo->ringnbr    = ringnbr;
    geo->ringxyznbr = ringxyznbr;
    geo->ringxyz    = ringxyz;

    geo->ext.W      =  INFINITY;
    geo->ext.S      =  INFINITY;
    geo->ext.E      = -INFINITY;
    geo->ext.N      = -INFINITY;

    geo->scamin     =  S57_RESET_SCAMIN;

#ifdef S52_USE_WORLD
    geo->nextPoly   = NULL;
#endif

//#ifdef S52_USE_SUPP_LINE_OVERLAP
//    geo->origAW     = origAW;
//#else
//    (void)origAW;
//#endif

    return geo;
}

S57_geo   *S57_set_META(void)
{
    // FIXME: use g_slice()
    _S57_geo *geo = g_new0(_S57_geo, 1);
    //_S57_geo *geo = g_try_new0(_S57_geo, 1);
    if (NULL == geo)
        g_assert(0);

    geo->S57ID  = _S57ID++;
    geo->objType= S57__META_T;

    geo->ext.W  =  INFINITY;
    geo->ext.S  =  INFINITY;
    geo->ext.E  = -INFINITY;
    geo->ext.N  = -INFINITY;

    geo->scamin =  S57_RESET_SCAMIN;

#ifdef S52_USE_WORLD
    geo->nextPoly = NULL;
#endif

    return geo;
}

int        S57_setName(_S57_geo *geo, const char *name)
// FIXME: name come from GDAL/OGR s57objectclasses.csv
{
    return_if_null(geo);
    return_if_null(name);

    //*
    if (S57_GEO_NM_LN < strlen(name)) {
        PRINTF("DEBUG: S57_geo name overflow max S57_GEO_NM_LN : %s\n", name);
        g_assert(0);
    }
    //*/

    int len = strlen(name);
    len = (S57_GEO_NM_LN < len) ? S57_GEO_NM_LN : len;
    memcpy(geo->name, name, len);
    geo->name[len] = '\0';

    return TRUE;
}

CCHAR     *S57_getName(_S57_geo *geo)
{
    return_if_null(geo);
    return_if_null(geo->name);

    return geo->name;
}

guint      S57_getRingNbr(_S57_geo *geo)
{
    return_if_null(geo);

    // since this is used with S57_getGeoData
    // META object don't need to be projected for rendering (pick do)
    switch (geo->objType) {
        case S57_POINT_T:
        case S57_LINES_T:
            return 1;
        case S57_AREAS_T:
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

    if  (S57_AREAS_T==geo->objType && ringNo>=geo->ringnbr) {
        PRINTF("WARNING: invalid ring no requested! \n");
        *npt = 0;
        *ppt = NULL;
        g_assert(0);
        return FALSE;
    }

    switch (geo->objType) {

        case S57_POINT_T:
            if (NULL != geo->pointxyz) {
                *npt = 1;
                *ppt = geo->pointxyz;
            } else {
                *npt = 0;
                *ppt = NULL;
            }
            break;

        case S57_LINES_T:
            if (NULL != geo->linexyz) {
                *npt = geo->linexyznbr;
                *ppt = geo->linexyz;
            } else {
                *npt = 0;
                *ppt = NULL;
            }
            break;

        case S57_AREAS_T:
            if (NULL != geo->ringxyznbr) {
                *npt = geo->ringxyznbr[ringNo];
                *ppt = geo->ringxyz[ringNo];
            } else {
                *npt = 0;
                *ppt = NULL;
            }

            // debug
            //if (geodata->ringnbr > 1) {
            //    PRINTF("DEBUG: AREA_T ringnbr:%i only exterior ring used\n", geodata->ringnbr);
            //}
            break;

        case S57__META_T:
            *npt = 0;
            *ppt = NULL;
            break;        // meta geo stuff (ex: C_AGGR)

        default:
            PRINTF("ERROR: object type invalid (%i)\n", geo->objType);
            g_assert(0);
            return FALSE;
    }

    // alloc'ed mem for xyz vs xyz size
    if ((0==ringNo) && (*npt<geo->geoSize)) {
        PRINTF("ERROR: geo lenght greater then npt - internal error\n");
        g_assert(0);
        return FALSE;
    }

    if (0 == *npt) {
        //PRINTF("DEBUG: npt == 0\n");
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

    // debug
    return_if_null(prim->vertex);

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

int        S57_setGeoExt(_S57_geo *geo, double W, double S, double E, double N)
// assume: extent canonical - W, S, E, N
{
    return_if_null(geo);

    // debug
    //if (0 == g_strncasecmp(geo->name->str, "M_COVR", 6)) {
    //    PRINTF("DEBUG: %s: %f, %f  UR: %f, %f\n", geo->name->str, W, S, E, N);
    //}

    // canonize lng
    //W = (W < -180.0) ? 0.0 : (W > 180.0) ? 0.0 : W;
    //E = (E < -180.0) ? 0.0 : (E > 180.0) ? 0.0 : E;
    // canonize lat
    //S = (S < -90.0) ? 0.0 : (S > 90.0) ? 0.0 : S;
    //N = (N < -90.0) ? 0.0 : (N > 90.0) ? 0.0 : N;

    /*
    // check prime-meridian crossing
    if ((W < 0.0) && (0.0 < E)) {
        PRINTF("DEBUG: prime-meridian crossing %s: LL: %f, %f  UR: %f, %f\n", geo->name->str, W, S, E, N);
        g_assert(0);
    }
    */

    /* newVRMEBL pass here now, useless anyway
    if (isinf(W) && isinf(E)) {
        //PRINTF("DEBUG: %s: LL: %f, %f  UR: %f, %f\n", geo->name->str, W, S, E, N);
        PRINTF("DEBUG: %s: LL: %f, %f  UR: %f, %f\n", geo->name, W, S, E, N);
        g_assert(0);
        return FALSE;
    }
    */

    geo->ext.W = W;
    geo->ext.S = S;
    geo->ext.E = E;
    geo->ext.N = N;

    return TRUE;
}

ObjExt_t   S57_getGeoExt(_S57_geo *geo)
{
    // no extent: "$CSYMB", afgves, vessel, ..
    if (0 != isinf(geo->ext.W)) {  // inf
        geo->ext.W = -INFINITY;  // W
        geo->ext.S = -INFINITY;  // S
        geo->ext.E =  INFINITY;  // E
        geo->ext.N =  INFINITY;  // N
    }

    return geo->ext;
}

gboolean   S57_cmpGeoExt(_S57_geo *geoA, _S57_geo *geoB)
// TRUE if intersect else FALSE

// Note: object in a cell are in same hemesphire - no need to check anti-meridian
/* S-57 4.0.0 ann. B1
2.1.8.2     180° Meridian of Longitude
Clause  2.2  of  S-57  Appendix  B.1  –  ENC  Product  Specification,  describes  the  construct,  including
geographic extent, to be used for ENC cells.  This clause does not address ENC cells that cross the
180º  Meridian  of  Longitude. There  is  currently  no  production  software  or  ECDIS  system  that  can
handle ENC cells that cross the 180º  Meridian, therefore to avoid ECDIS load and display issues ENC
cells must not span the 180º Meridian of Longitude.
*/
{
    return_if_null(geoA);
    return_if_null(geoB);

    // debug
    // FIXME: extent of parallele vert/horiz line will not intersect, diag parallele will
    // FIX: augment ext to dominant lat/lon
    if (S57_LINES_T==geoA->objType && S57_LINES_T==geoB->objType) {
        PRINTF("FIXME: if vert/horiz, parallele lines will not ext. intersect\n");
        //g_assert(0);
    }

    ObjExt_t A = geoA->ext;
    ObjExt_t B = geoB->ext;

    /* FIXME: do we need to check if point at same location are within tolerance
     FIX: not required - produce the same result as cmpExt()
    if (S57_POINT_T==geoA->objType && S57_POINT_T==geoB->objType) {

        gboolean same = TRUE;

        if ((ABS(A.S-B.S) < S57_GEO_TOLERANCE) && (ABS(A.W-B.W) < S57_GEO_TOLERANCE)) {
            //return TRUE;
            same = TRUE;
        } else {
            //return FALSE;
            same = FALSE;
        }

        // debug - check that cmpExt() produce the same result
        {
            gboolean sameAlt = TRUE;
            if (B.N < A.S)
                sameAlt = FALSE;
            else
                if (B.E < A.W)
                    sameAlt = FALSE;
                else
                    if (B.S > A.N)
                        sameAlt = FALSE;
                    else
                        if (B.W > A.E)
                            sameAlt = FALSE;

            // debug - OK on GB4X0000.000, CA279037.000
            PRINTF("DEBUG: cmp if S57_POINT_T A == S57_POINT_T B XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
            g_assert(same == sameAlt);

            return same;
        }
    }
    //*/

    //return !S57_cmpExt(A, B);
    return S57_cmpExt(A, B);
 }

gboolean   S57_cmpExt(ObjExt_t A, ObjExt_t B)
// TRUE if extent of A intersect exent of B else FALSE
// assume: canonic
{
    if (B.N < A.S) return FALSE;
    if (B.E < A.W) return FALSE;
    if (B.S > A.N) return FALSE;
    if (B.W > A.E) return FALSE;

    return TRUE;

    /* TRUE if B is outside A (and witch side of A) else FALSE
    // box A side: 0 - touch, 1 - S, 2 - W, 3 - N, 4 - E

    // FIXME: CW or CCW

    // CW
    if (B.N < A.S) return 1;
    if (B.E < A.W) return 2;
    if (B.S > A.N) return 3;
    if (B.W > A.E) return 4;

    // CCW
    if (B.N < A.S) return 1;
    if (B.W > A.E) return 4;
    if (B.S > A.N) return 3;
    if (B.E < A.W) return 2;

    return FALSE;
    */
}

S57_Obj_t  S57_getObjtype(_S57_geo *geo)
{
    if (NULL == geo)
        return S57__META_T;

    return geo->objType;
}

#if 0
//* return the number of attributes.
static void   _countItems(GQuark key_id, gpointer data, gpointer user_data)
{
    const gchar *attName  = g_quark_to_string(key_id);
    if (6 == strlen(attName)){
        int *cnt = (int*)user_data;
        *cnt = *cnt + 1;
    }
}

int        S57_getNumAtt(_S57_geo *geo)
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

    if (S57_ATT_NM_LN == strlen(attName)){
        strcpy(attData->value[attData->currentIdx], attValue->str);
        strcpy(attData->name [attData->currentIdx], attName );
        PRINTF("NOTE: inserting %s %s %d", attName, attValue->str, attData->currentIdx);
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

void       S57_getGeoWindowBoundary(double lat, double lng, double scale, int width, int height, double *latMin, double *latMax, double *lngMin, double *lngMax)
{

  _initPROJ();

  {
    projUV pc1, pc2;   // pixel center

    pc1.v = lat;
    pc1.u = lng;

    pc1 = S57_geo2prj(pc1); // mercator center in meters

    // lower right
    pc2.u = (width/2.  + 0.5)*scale + pc1.u;
    pc2.v = (height/2. + 0.5)*scale + pc1.v;
    pc2 = S57_prj2geo(pc2);
    *lngMax = pc2.u;
    *latMax = pc2.v;
    // upper left
    pc2.u = -((width/2. )*scale + 0.5) + pc1.u;
    pc2.v = -((height/2.)*scale + 0.5) + pc1.v;
    pc2 = S57_prj2geo(pc2);
    *lngMin = pc2.u;
    *latMin = pc2.v;
  }

  PRINTF("lat/lng: %lf/%lf scale: %lf, w/h: %d/%d lat: %lf/%lf lng: %lf/%lf\n", lat, lng, scale, width, height, *latMin, *latMax, *lngMin, *lngMax);

  //S57_donePROJ();

}
//*/
#endif  // 0


static guint   _qMax = 0;
//static GArray *_qList    = NULL;
static guint _qcnt[500] = {0};
GString   *S57_getAttVal(_S57_geo *geo, const char *attName)
// return attribute string value or NULL if:
//      1 - attribute name abscent
//      2 - its a mandatory attribute but its value is not define (EMPTY_NUMBER_MARKER)
{
    return_if_null(geo);
    return_if_null(attName);

    // FIXME: check and time for direct value in geo (heading / course)
/*
110 1 valName:DSID_INTU
112 1 valName:DSID_EDTN
113 1 valName:DSID_UPDN
114 1 valName:DSID_UADT
115 1 valName:DSID_ISDT
134 1 valName:DSPM_HDAT
135 1 valName:DSPM_VDAT
136 1 valName:DSPM_SDAT
137 1 valName:DSPM_CSCL
138 1 valName:DSPM_DUNI
139 1 valName:DSPM_HUNI
145 15262 valName:SCAMIN
146 1 valName:DSID_SDAT
147 1 valName:DSID_VDAT
148 2350410 valName:LNAM
150 7538 valName:RCID
154 2754 valName:NAME_RCID_0
160 2754 valName:NAME_RCID_1
167 4975 valName:OBJL
171 4106 valName:NAME_RCNM
172 4106 valName:NAME_RCID
176 522 valName:CATBRG
177 26 valName:VERCLR
180 7376 valName:LNAM_REFS
182 153 valName:BOYSHP
183 236 valName:COLOUR
185 61 valName:OBJNAM
186 63 valName:CATSPM
187 3 valName:CATCBL
188 273 valName:CONRAD
189 9 valName:VERCSA
190 6 valName:INFORM
192 2628 valName:CATCOA
193 12377 valName:DRVAL1
194 43629 valName:DRVAL2
195 3204 valName:VALDCO
197 17010 valName:CATLMK
198 1800 valName:CONVIS
199 1905 valName:FUNCTN
200 118 valName:HEIGHT
201 118 valName:LITCHR
202 118 valName:SIGGRP
203 118 valName:SIGPER
204 118 valName:VALNMR
205 295 valName:SECTR1
206 295 valName:SECTR2
207 236 valName:CATLIT
208 348 valName:CATOBS
209 384 valName:VALSOU
210 3981 valName:WATLEV
212 304 valName:ORIENT
213 216 valName:TRAFIC
216 6 valName:CATSLC
217 7 valName:CONDTN
218 3 valName:BURDEP
219 20 valName:CATWRK
220 1 valName:POSACC
221 1 valName:CSCALE
222 2 valName:CATCOV
223 12 valName:MARSYS
224 37 valName:CATZOC
226 1236 valName:$SCODE
*/

    GQuark   q      = g_quark_from_string(attName);
    GString *attVal = (GString*) g_datalist_id_get_data(&geo->attribs, q);

    { // stat
        if (500 < q)
            g_assert(0);
        _qcnt[q]++;

        // CATLMK = 197
        //if (197 == q) {
        //    PRINTF("DEBUG: XXX attribute (%s) quark:%i\n", att_name, q);
        //    //g_assert(0);
        //}
    }

    // FIXME: optimisation: check for EMPTY_NUMBER_MARKER in S57_setAtt() but then valName will
    // be removed from attribs and dumpData() will fail to show a mandatory att with no value!!
    if (NULL!=attVal && (0==g_strcmp0(attVal->str, EMPTY_NUMBER_MARKER))) {
        // clutter
        //PRINTF("DEBUG: mandatory attribute (%s) with ommited value\n", att_name);

        //#include <signal.h>
        //raise(SIGINT);

        return NULL;
    }

    // display this NOTE once (because of too many warning)
    static int silent = FALSE;
    if (!silent && NULL!=attVal && 0==attVal->len) {
        //PRINTF("NOTE: attribute (%s) has no value [obj:%s]\n", att_name, geo->name->str);
        PRINTF("NOTE: attribute (%s) has no value [obj:%s]\n", attName, geo->name);
        PRINTF("NOTE: (this msg will not repeat)\n");
        silent = TRUE;
        return NULL;
    }

    return attVal;
}

GString   *S57_getAttValALL(_S57_geo *geo, const char *attName)
// return attribute string value or NULL if attribute value abscent
{
    return_if_null(geo);
    return_if_null(attName);


    GQuark   q      = g_quark_from_string(attName);
    GString *attVal = (GString*) g_datalist_id_get_data(&geo->attribs, q);

    return attVal;
}

static void   _string_free(gpointer data)
{
    g_string_free((GString*)data, TRUE);
}

GData     *S57_setAtt(_S57_geo *geo, const char *name, const char *val)
// FIXME: returning GData is useless
{
    return_if_null(geo);
    return_if_null(name);
    return_if_null(val);

    GQuark   qname = g_quark_from_string(name);
    GString *value = g_string_new(val);

    // FIXME: _qMin
    // stat
    if (_qMax < qname) {
        _qMax = qname;
        //PRINTF("DEBUG: maxQuark:%i att_name: %s\n", maxQuark, name);
    }

    if (NULL == geo->attribs)
        g_datalist_init(&geo->attribs);

#ifdef S52_USE_SUPP_LINE_OVERLAP
    if ((0==g_strcmp0(geo->name, "Edge")) && (0==g_strcmp0(name, "RCID"))) {
        geo->name_rcidstr = value->str;

        // FIXME: check for substring ",...)" if found at the end
        // this mean that TEMP_BUFFER_SIZE in OGR is not large anought.
     }
#endif

    // Note: if value NULL then _string_free must be NULL and qname is remove from attribs
    g_datalist_id_set_data_full(&geo->attribs, qname, value, _string_free);

    return geo->attribs;
}

int        S57_setTouchTOPMAR(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    /* debug
    if ((0==g_strcmp0(touch->name, "LITFLT")) ||
        (0==g_strcmp0(touch->name, "LITVES")) ||
        (0==strncmp  (touch->name, "BOY", 3)))
    {
        if (NULL != geo->touch.TOPMAR) {
            PRINTF("DEBUG: touch.TOMAR allready in use by %s\n", geo->touch.TOPMAR->name);
        }
    } else {
        PRINTF("DEBUG: not TOMAR: %s\n", touch->name);
        g_assert(0);
    }
    //*/

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

    // WARNING: reverse chaining

    /* debug
    if (0 == g_strcmp0(geo->name, "LIGHTS")) {
        if (NULL != touch->touch.LIGHTS) {
            PRINTF("DEBUG: touch.LIGHTS allready in use by %s\n", touch->touch.LIGHTS->name);
        }
    } else {
        PRINTF("DEBUG: not LIGHTS: %s\n", geo->name);
        g_assert(0);
    }
    //*/

    geo->touch.LIGHTS = touch;

    return TRUE;
}

S57_geo   *S57_getTouchLIGHTS(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.LIGHTS;
}

int        S57_setTouchDEPCNT(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    /* debug
    if ((0==g_strcmp0(geo->name, "DEPCNT")) ||
        (0==g_strcmp0(geo->name, "DEPARE"))
       )
    {
        if (NULL != geo->touch.DEPCNT) {
            PRINTF("DEBUG: touch.DEPARE allready in use by %s\n", geo->touch.DEPCNT->name);
            //if (0 != g_strcmp0(geo->name, geo->touch.DEPCNT->name)) {
            //    PRINTF("DEBUG: %s:%i touch.DEPCNT allready  in use by %s:%i will be replace by %s:%i\n",
            //           geo->name, geo->S57ID, geo->touch.DEPCNT->name, geo->touch.DEPCNT->S57ID, touch->name, touch->S57ID);
            //}
        }
    } else {
        PRINTF("DEBUG: geo not for DEPCNT: %s\n", geo->name);
        g_assert(0);
    }
    //*/

    geo->touch.DEPCNT = touch;

    return TRUE;
}

S57_geo   *S57_getTouchDEPCNT(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.DEPCNT;
}

int        S57_setTouchUDWHAZ(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    /* debug
    if ((0==g_strcmp0(geo->name, "OBSTRN")) ||
        (0==g_strcmp0(geo->name, "UWTROC")) ||
        (0==g_strcmp0(geo->name, "WRECKS"))
       )
    {
        if (NULL != geo->touch.DEPARE) {
            PRINTF("DEBUG: touch.DEPARE allready in use by %s\n", geo->touch.DEPARE->name);
            //if (0 != g_strcmp0(geo->name, geo->touch.DEPARE->name)) {
            //    PRINTF("DEBUG: %s:%i touch.DEPARE allready  in use by %s:%i will be replace by %s:%i\n",
            //           geo->name, geo->S57ID, geo->touch.DEPARE->name, geo->touch.DEPARE->S57ID, touch->name, touch->S57ID);
            //}
        }
    } else {
        PRINTF("DEBUG: geo not for DEPARE: %s\n", geo->name);
        g_assert(0);
    }
    //*/

    geo->touch.UDWHAZ = touch;

    return TRUE;
}

S57_geo   *S57_getTouchUDWHAZ(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->touch.UDWHAZ;
}

int        S57_setTouchDEPVAL(_S57_geo *geo, S57_geo *touch)
{
    return_if_null(geo);

    /* debug
    if ((0==g_strcmp0(geo->name, "OBSTRN")) ||
        (0==g_strcmp0(geo->name, "UWTROC")) ||
        (0==g_strcmp0(geo->name, "WRECKS"))
       )
    {
        if (NULL != geo->touch.DEPVAL) {
            PRINTF("DEBUG: %s:%i touch.DEPVAL allready in use by %s:%i\n",
                   geo->name, geo->S57ID, geo->touch.DEPVAL->name, geo->touch.DEPVAL->S57ID);
        }
    } else {
        PRINTF("DEBUG: geo not for DEPVAL: %s\n", geo->name);
        g_assert(0);
    }
    //*/

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
    // test useless since the only caller allready did that
    //return_if_null(geo);

    // FIXME: should write SCAMIN value to attribs?

    geo->scamin = scamin;

    return geo->scamin;
}

double     S57_getScamin(_S57_geo *geo)
{
    // test useless since the only caller allready did that
    //return_if_null(geo);

    if (S57_RESET_SCAMIN == geo->scamin) {
        GString *valstr = S57_getAttVal(geo, "SCAMIN");
        geo->scamin = (NULL == valstr) ? INFINITY : S52_atof(valstr->str);
    }

    // debug - parano, attribs scamin can't be 0
    g_assert(0.0 != geo->scamin);

    return geo->scamin;
}

#ifdef S52_USE_C_AGGR_C_ASSO
int        S57_setRelationship(_S57_geo *geo, _S57_geo *geoRel)
// this geo has in a C_AGGR or C_ASSO (geoRel) relationship
{
    return_if_null(geo);
    return_if_null(geoRel);

    if (NULL == geo->relation) {
        geo->relation = geoRel;
    } else {
        // FIXME: US3NY21M/US3NY21M.000 has multiple relation for the same object
        // also CA379035.000, CA479020.000
        PRINTF("DEBUG: 'geo->relation' allready in use ..\n");
        //g_assert(0);

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

static void   _printAttVal(GQuark key_id, gpointer data, gpointer user_data)
{
    // 'user_data' not used
    (void) user_data;

    const gchar *attName  = g_quark_to_string(key_id);

    // print only S57 attrib - assuming that OGR att are not 6 char in lenght!!
    //if (S57_ATT_NM_LN == strlen(attName)) {
        GString *attValue = (GString*) data;
        PRINTF("%s: %s\n", attName, attValue->str);
    //}
}

int        S57_dumpData(_S57_geo *geo, int dumpCoords)
// debug - if dumpCoords is TRUE dump all coordinates
{
    return_if_null(geo);

    // dump stat mode
    if (NULL == geo) {
        PRINTF("STAT START:\n");
        // FIXME: _qMin
        for (guint i=0; i<_qMax; ++i) {
            // FIXME: show att with 0 call
            if (0 != _qcnt[i])
                PRINTF("STAT: i:%i nCall:%i valName:%s\n", i, _qcnt[i], g_quark_to_string(i));
        }

        return TRUE;
    }

    // normal mode
    PRINTF("----------------\n");
    PRINTF("NAME  : %s\n", geo->name);
    PRINTF("S57ID : %i\n", geo->S57ID);

    switch (geo->objType) {
        case S57__META_T:  PRINTF("oType : S57__META_T\n"); break;
        case S57_POINT_T:  PRINTF("oType : S57_POINT_T\n"); break;
        case S57_LINES_T:  PRINTF("oType : S57_LINES_T\n"); break;
        case S57_AREAS_T:  PRINTF("oType : S57_AREAS_T\n"); break;
        default:
            PRINTF("WARNING: invalid object type; %i\n", geo->objType);
    }

    // dump Att/Val
    g_datalist_foreach(&geo->attribs, _printAttVal, NULL);

    // dump extent
    PRINTF("EXT   : %f, %f  --  %f, %f\n", geo->ext.S, geo->ext.W, geo->ext.N, geo->ext.E);

    if (TRUE == dumpCoords) {
        guint     npt = 0;
        geocoord *ppt = NULL;
        S57_getGeoData(geo, 0, &npt, &ppt);
        PRINTF("COORDS: %i\n", npt);
        for (guint i=0; i<npt; ++i) {
            PRINTF("\t\t(%f, %f, %f)\n", ppt[0], ppt[1], ppt[2]);
            ppt += 3;
        }
        //PRINTF("\n");
    }

    return TRUE;
}

#ifdef S52_DEBUG
guint      S57_getS57ID(_S57_geo *geo)
// get the first field of S57_geo
{
    return_if_null(geo);

//    return  geo->S57ID;
    return (*(guint *)geo);
}
#endif  // S52_DEBUG

static void   _getAtt(GQuark key_id, gpointer data, gpointer user_data)
{

    const gchar *attName  = g_quark_to_string(key_id);
    GString     *attValue = (GString*) data;
    GString     *attList  = (GString*) user_data;

    /*
    // filter out OGR internal S57 info
    if (0 == g_strcmp0("MASK",      attName)) return;
    if (0 == g_strcmp0("USAG",      attName)) return;
    if (0 == g_strcmp0("ORNT",      attName)) return;
    if (0 == g_strcmp0("NAME_RCNM", attName)) return;
    if (0 == g_strcmp0("NAME_RCID", attName)) return;
    if (0 == g_strcmp0("NINFOM",    attName)) return;
    */

    // save S57 attribute + system attribute (ex vessel name - AIS)
    if (0 != attList->len)
        g_string_append(attList, ",");

    g_string_append(attList, attName);
    g_string_append_c(attList, ':');
    g_string_append(attList, attValue->str);

    //PRINTF("\t%s : %s\n", attName, (char*)attValue->str);

    return;
}

CCHAR     *S57_getAtt(_S57_geo *geo)
{
    return_if_null(geo);

    //PRINTF("S57ID : %i\n", geo->S57ID);
    //PRINTF("NAME  : %s\n", geo->name);

    g_string_set_size(_attList, 0);
    g_string_printf(_attList, "%s:%i", geo->name, geo->S57ID);

    g_datalist_foreach(&geo->attribs, _getAtt, _attList);

    return _attList->str;
}

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

gboolean   S57_isPtInArea(_S57_geo *geo, double x, double y)
// return TRUE if (x,y) inside geo area (close/open) else FALSE
{
    return_if_null(geo);

    gboolean inArea = FALSE;

    guint nr = S57_getRingNbr(geo);
    for (guint i=0; i<nr; ++i) {
        guint   npt;
        double *ppt;
        if (TRUE == S57_getGeoData(geo, i, &npt, &ppt)) {
            if (TRUE == S57_isPtInRing(npt, (pt3*)ppt, TRUE, x, y)) {
                // exterior ring
                if (0 == i) {
                    inArea = TRUE;
                } else {
                    // interior ring
                    // FIXME: assume S57 rings not concentric - check this
                    inArea = FALSE;
                }
            }
        }
    }

    return inArea;
}

gboolean   S57_isPtInRing(guint npt, pt3 *ppt, gboolean close, double x, double y)
// return TRUE if (x,y) inside ring (close/open) else FALSE
// Note: CW or CCW, work with either
{
    return_if_null(ppt);

    if (npt < 3) {
        PRINTF("FIXME: logic bug - area with less than 3 pt\n");
        g_assert(0);
        return FALSE;
    }

    gboolean c = 0;
    pt3     *v = ppt;

    if (TRUE == close) {
        for (guint i=0; i<npt-1; ++i) {
            pt3 p1 = v[i];
            pt3 p2 = v[i+1];

            if ( ((p1.y>y) != (p2.y>y)) && (x < (p2.x-p1.x) * (y-p1.y) / (p2.y-p1.y) + p1.x) )
                c = !c;
        }
    } else {
        for (guint i=0, j=npt-1; i<npt; j=i++) {
            pt3 p1 = v[i];
            pt3 p2 = v[j];

            if ( ((p1.y>y) != (p2.y>y)) && (x < (p2.x-p1.x) * (y-p1.y) / (p2.y-p1.y) + p1.x) )
                c = !c;
        }
    }

    // debug
    //PRINTF("npt: %i inside: %s\n", npt, (c==1)?"TRUE":"FALSE");

    return c;
}

gboolean   S57_isPtInSet(_S57_geo *geo, double x, double y)
// TRUE if XY is the same as one in geo
{
    //return_if_null(geo);

    guint nr = S57_getRingNbr(geo);
    for (guint i=0; i<nr; ++i) {
        guint   npt;
        double *ppt;

        if (TRUE == S57_getGeoData(geo, i, &npt, &ppt)) {
            for (guint j=0; j<npt; ++j, ppt+=3) {
                if (ABS(ppt[0]-x)<S57_GEO_TOLERANCE && ABS(ppt[1]-y)<S57_GEO_TOLERANCE)
                    return TRUE;
            }
        }
    }

    return FALSE;
}

gboolean   S57_isPtOnLine(_S57_geo *geoLine, double x, double y)
// TRUE if XY is on the line else FALSE
{
    return_if_null(geoLine);

    // debug / paranoid
    if (S57_LINES_T != S57_getObjtype(geoLine)) {
        PRINTF("FIXME: geoLine not a line .. logic bug\n");
        g_assert(0);
        return FALSE;
    }

    guint   npt;
    double *ppt;

    if (FALSE == S57_getGeoData(geoLine, 0, &npt, &ppt))
        return FALSE;

    for (guint i=0; i<npt-1; ++i, ppt+=3) {
        // check if outside segment extent
        // X
        if (ppt[0] < ppt[3]) {
            if (x<ppt[0] || ppt[3]<x)
                continue;
        } else {
            if (x<ppt[3] || ppt[0]<x)
                continue;
        }
        // Y
        if (ppt[1] < ppt[4]) {
            if (y<ppt[1] || ppt[4]<y)
                continue;
        } else {
            if (y<ppt[4] || ppt[1]<y)
                continue;
        }

        // compare slope A-B, B-C
        // FIXME: find that this is not overkill
        //pt3 B = {x, y, 0.0};
        //if (TRUE == _inLine(*(pt3*)ppt/* A */, B, *(pt3*)(ppt+3)/* C */))
        //    return TRUE;
    }

    return FALSE;
}

#if 0
gboolean   S57_touchArea(_S57_geo *geoArea, _S57_geo *geo)
// TRUE if A touch B else FALSE
// A:P/L/A, B:A
{
    guint   nptA;
    double *pptA;
    guint   nptB;
    double *pptB;

    return_if_null(geoArea);
    return_if_null(geo);

    if (FALSE == S57_getGeoData(geoArea, 0, &nptA, &pptA))
        return FALSE;

    if (FALSE == S57_getGeoData(geo, 0, &nptB, &pptB))
        return FALSE;

    if (S57_AREAS_T != S57_getObjtype(geoArea)) {
        PRINTF("FIXME: geoB is a S57_POINT_T or S57_LINES_T .. this algo break on that type (%c)\n", S57_getObjtype(geoArea));
        g_assert(0);
        return FALSE;
    }

    for (guint i=0; i<nptB; ++i, pptB+=3) {
        // FIXME: optimisation check if ptA inside B extent
        if (TRUE == S57_isPtInRing(nptA, (pt3*)pptA, TRUE, pptB[0], pptB[1]))
            return TRUE;
    }

    return FALSE;
}
#endif  // 0

guint      S57_getGeoSize(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->geoSize;
}

guint      S57_setGeoSize(_S57_geo *geo, guint size)
{
    return_if_null(geo);

    if ((S57_POINT_T==geo->objType) && (size > 1)) {
        PRINTF("ERROR: POINT_T size\n");
        g_assert(0);
        return FALSE;
    }
    if ((S57_LINES_T==geo->objType) && (size > geo->linexyznbr)) {
        PRINTF("ERROR: LINES_T size\n");
        g_assert(0);
        return FALSE;
    }
    if ((S57_AREAS_T==geo->objType) && (size > geo->ringxyznbr[0])) {
        PRINTF("ERROR: AREAS_T size\n");
        g_assert(0);
        return FALSE;
    }

    if (S57__META_T == geo->objType) {
        PRINTF("ERROR: object type invalid (%i)\n", geo->objType);
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

int        S57_getNextCent(_S57_geo *geo, double *x, double *y)
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

int        S57_hasCentroid(_S57_geo *geo)
{
    return_if_null(geo);

    if (NULL == geo->centroid) {
        S57_newCentroid(geo);
    } else {
        // reset idx for call S57_getNextCent()
        geo->centroidIdx = 0;
    }

    if (0 == geo->centroid->len)
        return FALSE;

    return TRUE;
}

#ifdef S52_USE_SUPP_LINE_OVERLAP
S57_geo   *S57_getEdgeOwner(_S57_geo *geoEdge)
{
    // not needed
    //return_if_null(geo);

    return geoEdge->geoOwner;
}

S57_geo   *S57_setEdgeOwner(_S57_geo *geoEdge, _S57_geo *geoOwner)
{
    // not needed
    //return_if_null(geo);

    geoEdge->geoOwner = geoOwner;

    return geoEdge;
}

int        S57_markOverlapGeo(_S57_geo *geo, _S57_geo *geoEdge)
// experimental: mark coordinates in geo that match the chaine-node in geoEdge
{

    return_if_null(geo);
    return_if_null(geoEdge);

    // M_COVR is used for system generated DATCOVR
    if (0 == g_strcmp0("M_COVR", geo->name)) {
        //PRINTF("DEBUG: found M_COVR, nptEdge: %i - skipped\n", nptEdge);
        return TRUE;
    }

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
    //if (0 == g_strcmp0("HRBARE", geo->name)) {
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
                if (0 == i) {
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

    // LS() use znear zfar Z_CLIP_PLANE (S57_OVERLAP_GEO_Z - 1) to clip overlap
    // LC() check for the value -S57_OVERLAP_GEO_Z
    for (guint j=0; j<nptEdge; ++j) {
        ppt[i*3 + 2] = -S57_OVERLAP_GEO_Z;
        i += next;
    }

    return TRUE;
}

gchar     *S57_getRCIDstr(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->name_rcidstr;
}

//S57_AW_t   S57_getOrigAW(_S57_geo *geo)
//// debug
//{
//    return_if_null(geo);
//
//    return geo->origAW;
//}
#endif  // S52_USE_SUPP_LINE_OVERLAP

int        S57_setHighlight(S57_geo *geo, gboolean highlight)
{
    return_if_null(geo);

    geo->highlight = highlight;

    return TRUE;
}

gboolean   S57_getHighlight(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->highlight;
}

#if 0
int        S57_setHazard(_S57_geo *geo, gboolean hazard)
{
    return_if_null(geo);

    geo->hazard = hazard;

    return TRUE;
}

int        S57_isHazard(_S57_geo *geo)
{
    return_if_null(geo);

    return geo->hazard;
}
#endif  // 0

#if 0
int        S57_setLOD(_S52_obj *obj, char LOD)
{
    return_if_null(obj);

    obj->LOD = LOD;

    return TRUE;
}

char       S57_getLOD(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->LOD;
}
#endif  // 0


#if 0
int main(int argc, char** argv)
{

   return 1;
}
#endif  // 0
