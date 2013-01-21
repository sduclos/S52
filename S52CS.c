// S52CS.c : Conditional Symbologie procedure 3.2 (CS)
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



// NOTE: remarks commenting each CS are extracted from pslb03_2.pdf (sec. 12)

// FIXME: DEPCNT02: call DB for area DEPARE & DRGARE that intersect this line
// FIXME:_DEPVAL01: call DB for area DEPARE & UNSARE that intersect this area
// FIXME:_UDWHAZ03: call DB for area DRGARE & DEPARE that intersect this point/area

#include "S52CS.h"
#include "S52MP.h"      // S52_MP_get/set()
#include "S52utils.h"   // PRINTF(), S52_atof(), S52_atoi()

#include <math.h>       // floor()

#define version "3.2.0"

//#define UNKNOWN HUGE_VAL   // INFINITY/NAN
#define UNKNOWN  (1.0/0.0)   //HUGE_VAL   // INFINITY/NAN

#define COALNE   30   // Coastline
#define DEPARE   42   // Depth area
#define DEPCNT   43   // Depth contour
#define DRGARE   46   // Dredged area
#define UWTROC  153   // Underwater rock / awash rock
#define WRECKS  159   // Wreck

// NOTE: rigid_list is useless
typedef struct _localObj {
    GPtrArray *lights_list;  // list of: LIGHTS
    GPtrArray *topmar_list;  // list of: LITFLT, LITVES, BOY???; to find floating platform
    GPtrArray *depare_list;  // list of: DEPARE, DRGARE

    //GPtrArray *obstrn_list;  // list of: DEPARE, DRGARE, UNSARE
    GPtrArray *depval_list;  // list of: DEPARE, UNSARE
} _localObj;

// size of attributes value list buffer
#define LISTSIZE   16   // list size

// system wide for OWNSHP & VESSEL,
//static int _vecper = 12;  // Vector length time-period (min),
//static int _vecmrk =  2;  // Vector time-mark interval 0/1/2 (0 - for none, 1 - 1&6 min, 2 - 6 min)
//static int _vecstb =  2;  // Vector Stabilization      0/1/2 (0 - for none, 1 - ground, 2 - water)


static char *_strpbrk(const char *s, const char *list)
{
    //return strpbrk(s, list);

    const char *p;
    const char *r;

    if (NULL==s || NULL==list) return NULL;

    for (r=s; *r; r++)
        for (p = list; *p; p++)
            if (*r == *p)
                return (char *)r;

    return NULL;
}

const char *S52_CS_version()
{
    return version;
}

localObj *S52_CS_init()
{
    _localObj *local = g_new0(_localObj, 1);
    //_localObj *local = g_try_new0(_localObj, 1);
    if (NULL == local)
        g_assert(0);

    local->lights_list = g_ptr_array_new();
    local->topmar_list = g_ptr_array_new();
    //local->rigid_list = g_ptr_array_new();
    local->depare_list = g_ptr_array_new();

    //local->obstrn_list = g_ptr_array_new();
    local->depval_list = g_ptr_array_new();

    //  vecper: Vector length time-period,
    //  vecmrk: Vector time-mark interval,
    //  vecstb: Vector Stabilization

    return local;
}

localObj *S52_CS_done(_localObj *local)
{
    return_if_null(local);

    g_ptr_array_free(local->lights_list, TRUE);
    //g_ptr_array_unref(local->lights_list);

    g_ptr_array_free(local->topmar_list, TRUE);
    //g_ptr_array_unref(local->topmar_list);

    //g_ptr_array_free(local->rigid_list, TRUE);
    g_ptr_array_free(local->depare_list, TRUE);
    //g_ptr_array_unref(local->depare_list);

    //g_ptr_array_free(local->obstrn_list, TRUE);
    g_ptr_array_free(local->depval_list, TRUE);
    //g_ptr_array_unref(local->depval_list);

    g_free(local);

    return NULL;
}

int       S52_CS_add(_localObj *local, S57_geo *geo)
// return TRUE
{
    return_if_null(local);
    return_if_null(geo);

    const char *name = S57_getName(geo);

    ///////////////////////////////////////////////
    // for LIGHTS05
    //
    // set floating platform
    if ((0==g_strcmp0  (name, "LITFLT")) ||
        (0==g_strcmp0  (name, "LITVES")) ||
        (0==S52_strncmp(name, "BOY", 3)))
    {
        g_ptr_array_add(local->topmar_list, (gpointer) geo);
        return TRUE;
    }

    // set rigid platform --useless
    //if (0==g_strcmp0(name, "BCN",    3))
    //    g_ptr_array_add(local->rigid_list, (gpointer) geo);

    // set light object
    if (0==g_strcmp0(name, "LIGHTS")) {
        g_ptr_array_add(local->lights_list, (gpointer) geo);
        return TRUE;
    }


    ///////////////////////////////////////////////
    // for DEPCNT02 and
    // for _UDWHAZ03 (via OBSTRN04, WRECKS02)
    //
    if ((0==g_strcmp0(name, "DEPARE")) ||
        (0==g_strcmp0(name, "DRGARE"))
       )
    {
        // FIXME: could object be something else then AREAS_T!
        if (AREAS_T == S57_getObjtype(geo))
            g_ptr_array_add(local->depare_list, (gpointer) geo);
        //else
        //    PRINTF("NOTE: depare_list: %s not of type AREAS_T\n", name);

        //g_ptr_array_add(local->obstrn_list, (gpointer) geo);
        //return TRUE;
    }

    ///////////////////////////////////////////////
    // for _DEPVAL01 (via OBSTRN04, WRECKS02)
    if ((0==g_strcmp0(name, "DEPARE")) ||
        (0==g_strcmp0(name, "DRGARE")) ||    // not in S52!
        (0==g_strcmp0(name, "UNSARE"))       // this does nothing!
       )
    {
        // FIXME: could object be something else then AREAS_T!
        if (AREAS_T == S57_getObjtype(geo))
            g_ptr_array_add(local->depval_list, (gpointer) geo);
        //else
        //    PRINTF("NOTE: depval_list: %s not of type AREAS_T\n", name);
                   //return TRUE;
    }

    //return FALSE;
    return TRUE;
}

static int      _intersec(S57_geo *A, S57_geo *B)
// TRUE if A instersec B, else FALSE
{
    /*
    if (B.n < A.s) return FALSE;
    if (B.e < A.w) return FALSE;
    if (B.s > A.n) return FALSE;
    if (B.w > A.e) return FALSE;
    */
    double Ax1, Ay1, Ax2, Ay2;
    double Bx1, By1, Bx2, By2;

    S57_getExt(A, &Ax1, &Ay1, &Ax2, &Ay2);
    S57_getExt(B, &Bx1, &By1, &Bx2, &By2);

    if (By2 < Ay1) return FALSE;
    if (Bx2 < Ax1) return FALSE;
    if (By1 > Ay1) return FALSE;
    if (Bx1 > Ax2) return FALSE;

    return TRUE;
}

int       S52_CS_touch(localObj *local, S57_geo *geo)
// compute witch geo object of this cell "touch" this one (geo)
// return TRUE
{
    static int silent = FALSE;

    return_if_null(local);
    return_if_null(geo);

    char *name = S57_getName(geo);

    // lights reverse the link so this test is bad
    //if (NULL != S57_getTouch(geo)) {
    //    PRINTF("ERROR: object (%s) already 'touch' an object\n", name);
    //    g_assert(0);
    //}

    ////////////////////////////////////////////
    // floating object
    //
    if (0==g_strcmp0(name, "TOPMAR")) {
        GString *lnam = S57_getAttVal(geo, "LNAM");
        //unsigned int i = 0;
        for (guint i=0; i<local->topmar_list->len; ++i) {
            S57_geo *other = (S57_geo *) g_ptr_array_index(local->topmar_list, i);
            GString *olnam = S57_getAttVal(other, "LNAM");

            // skip if not at same position
            if (FALSE == _intersec(geo, other))
                continue;

            // skip if it's same S57 object
            //if (TRUE == g_string_equal(lnam, olnam))
            if (TRUE == S52_string_equal(lnam, olnam))
                continue;

            if (NULL == S57_getTouchTOPMAR(geo)) {
                S57_setTouchTOPMAR(geo, other);

                // bailout as soon as we got one
                break;
            }
        }

        // finish
        return TRUE;
    }

    ////////////////////////////////////////////
    // experimental:
    // check if this buoy has a lights
    //
    if (0==g_strcmp0(name, "BOYLAT")) {
        //unsigned int i = 0;
        for (guint i=0; i<local->lights_list->len; ++i) {
            S57_geo *light = (S57_geo *) g_ptr_array_index(local->lights_list, i);

            // skip if this light is not at buoy's position
            if (FALSE == _intersec(geo, light))
                continue;

            if (NULL == S57_getTouchLIGHTS(geo)) {
                // WARNING: reverse chaining --could this collide with other
                // lights sheme (next case --lights at the same position)
                // debug
                if (NULL != S57_getTouchLIGHTS(light)) {
                    if (FALSE == silent) {
                        PRINTF("FIXME: more than 1 light for the same bouy!!!\n");
                        PRINTF("       (this msg will not repeat)\n");
                        silent = TRUE;
                    }
                }

                S57_setTouchLIGHTS(light, geo);

                // bailout as soon as we got one
                break;
            } else {
                if (FALSE == silent) {
                    PRINTF("FIXME: more than 1 light for the same bouy!!!\n");
                    PRINTF("       (this msg will not repeat)\n");
                    silent = TRUE;
                }
            }
        }
        return TRUE;
    }


    ////////////////////////////////////////////
    // chaine light at same position
    //
    if (0==g_strcmp0(name, "LIGHTS")) {
        GString *lnam = S57_getAttVal(geo, "LNAM");
        //unsigned int i = 0;

        // debug - 1556
        //if (1555 == S57_getGeoID(geo)) {
        //    PRINTF("lights found\n");
        //    //g_assert(0);
        //}

        for (guint i=0; i<local->lights_list->len; ++i) {
            S57_geo *other = (S57_geo *) g_ptr_array_index(local->lights_list, i);
            GString *olnam = S57_getAttVal(other, "LNAM");

            // skip if not at same position
            if (FALSE == _intersec(geo, other))
                continue;

            // skip if it's same S57 object
            //if (TRUE == g_string_equal(lnam, olnam))
            if (TRUE == S52_string_equal(lnam, olnam))
                continue;

            // chaine lights
            //if (NULL == S57_getTouchLIGHTS(geo)) {
            if (NULL == S57_getTouchLIGHTS(other)) {
                // HACK: link in reverse - this depend on the ordrering
                // of object in the cell. All light at this position are
                // linked to the fist one .. this is dumb. But some light
                // have more than one text (ie CA579016.000 range at lower left)
                // FIX: use LNAMREF maybe
                //S57_setTouchLIGHTS(other, geo);

                // the hack above doesn't work for text
                S57_setTouchLIGHTS(geo, other);

                // bailout as soon as we got one
                break;
            }
        }

        // finish
        return  TRUE;
    }

    ///////////////////////////////////////////////
    // object: line DEPCNT and line DEPARE
    // link to the shallower object that intersec this object
    //
    if ((0==g_strcmp0(name, "DEPCNT")) ||
        (0==g_strcmp0(name, "DEPARE") &&
         LINES_T==S57_getObjtype(geo))
       )
    {
        GString  *lnam     = S57_getAttVal(geo, "LNAM");
        //double  drval    = -UNKNOWN;
        GString  *drvalstr = S57_getAttVal(geo, "DRVAL1");
        double    drval    = 0.0;
        //double    drvalmin = -INFINITY;
        double    drvalmin = -UNKNOWN;
        //unsigned int i     = 0;

        // debug
        //PRINTF("--------------------------\n");

        if (NULL == drvalstr) {
            drvalstr = S57_getAttVal(geo, "VALDCO");
            //PRINTF("VALDCO\n");
        }

        if (NULL != drvalstr) {
            drval = S52_atof(drvalstr->str);
            //PRINTF("DRVAL:%f\n", drval);
        } else {
            //PRINTF("DRVAL:NULL\n");
            return TRUE;
        }

        //drvalmin = drval;

        // debug
        //if (491 == S57_getGeoID(geo)) {
        //    PRINTF("491 found\n");
        //}
        //if (127 == S57_getGeoID(geo)) {
        //    PRINTF("127 found\n");
        //}

        for (guint i=0; i<local->depare_list->len; ++i) {
            S57_geo *other = (S57_geo *) g_ptr_array_index(local->depare_list, i);
            GString *olnam = S57_getAttVal(other, "LNAM");
            //char    *oname = S57_getName(other);

            // skip if it's same S57 object
            //if (TRUE == g_string_equal(lnam, olnam))
            if (TRUE == S52_string_equal(lnam, olnam))
                continue;

            /*
            {// skip UNSARE
                char *name = S57_getName(geo);
                if (0==g_strcmp0(name, "UNSARE", 6)) {
                    PRINTF("WARNING: skipping adjacent UNSARE\n");
                    g_assert(0);
                    continue;
                }
            }
            */

            // link to the area next to this one with a depth just above (shallower) this one,
            // FIXME: make list of objet that share Edge, now its only object list base
            // on extent overlap
            if (TRUE == _intersec(geo, other)) {
                // debug
                //if (551 == S57_getGeoID(other)) {
                //    PRINTF("551 found\n");
                //}

                //*
                GString *drval2str = S57_getAttVal(other, "DRVAL2");

                if (NULL != drval2str) {
                    double drval2 = S52_atof(drval2str->str);

                    // is this area just above (shallower) then this one
                    if (drval2 <= drval) {
                        if (drval2 > drvalmin) {
                            drvalmin = drval2;
                            S57_setTouchDEPARE(geo, other);
                        }
                    }
                    continue;
                    // debug
                    //PRINTF("drval/drvalmin/drval1: %3.1f %3.1f %3.1f \n", drval, drvalmin, drval1);
                }
                //*/

                //*
                GString *drval1str = S57_getAttVal(other, "DRVAL1");

                if (NULL != drval1str) {
                    double drval1 = S52_atof(drval1str->str);

                    // is this area just above (shallower) then this one
                    if (drval1 <= drval) {
                        if (drval1 > drvalmin) {
                            drvalmin = drval1;
                            S57_setTouchDEPARE(geo, other);
                        }
                    }
                    // debug
                    //PRINTF("drval/drvalmin/drval1: %3.1f %3.1f %3.1f \n", drval, drvalmin, drval1);
                }
                //*/

            }
        }

        // finish
        return TRUE;
    }

    // _UDWHAZ03
    // - object OBSTRN and UWTROC call _UDWHAZ03 (via OBSTRN04)
    // - object WRECKS call _UDWHAZ03 (via WRECKS02)
    // in turn, _UDWHAZ03 need to know what is the depth of
    // the shallower object 'beneath' it
    if ((0==g_strcmp0(name, "OBSTRN")) ||
        (0==g_strcmp0(name, "UWTROC")) ||
        (0==g_strcmp0(name, "WRECKS"))
       )
    {

        // FIXME: UWTROC (OSTRN04) hit this all the time
        // FIX: commended for now
        /*
        for (guint i=0; i<local->obstrn_list->len; ++i) {
            S57_geo *other = g_ptr_array_index(local->obstrn_list, i);

            if (TRUE == _intersec(geo, other)) {
                S57_setTouch(geo, other);
                return TRUE;
            }
        }
        */

        /*
        for (guint i=0; i<local->depare_list->len; ++i) {
            S57_geo *other = (S57_geo *) g_ptr_array_index(local->depare_list, i);

            // skip if not overlapping
            if (FALSE == _intersec(geo, other))
                continue;

            // link to depthest object
            // - use DRVAL1 for AREA DEPARE and AREA DRGARE
            // or
            // - use DRVAL2 for LINE DEPARE
            if (LINES_T == S57_getObjtype(other)) {
                GString *drval2str = S57_getAttVal(other, "DRVAL2");
                double   drval2    = (NULL == drval2str) ? UNKNOWN : S52_atof(drval2str->str);
                if (NULL!=drval2str && drval2>drval) {
                    drval = drval2;
                    S57_setTouch(geo, other);
                }
            } else { // AREA DEPARE and AREA DRGARE
                GString *drval1str = S57_getAttVal(other, "DRVAL1");
                double   drval1    = (NULL == drval1str) ? UNKNOWN : S52_atof(drval1str->str);
                if (NULL!=drval1str && drval1>drval) {
                    drval = drval1;
                    S57_setTouch(geo, other);
                }

            }
        }
        */

        //unsigned int i = 0;
        for (guint i=0; i<local->depare_list->len; ++i) {
            S57_geo *candidate = (S57_geo *) g_ptr_array_index(local->depare_list, i);

            // skip if not overlapping
            if (FALSE == _intersec(geo, candidate))
                continue;

            // BUG: S57_touch() work only for point in poly not point in line
            if (LINES_T == S57_getObjtype(candidate))
                continue;

            // find if geo 'touch' this DEPARE geo (other)
            if (TRUE == S57_touch(geo, candidate)) {
                S57_geo *crntmin = S57_getTouchDEPARE(geo);

                // case of more than one geo 'touch' this geo
                // link to the swallower
                if (NULL != crntmin) {
                    double drvalmin = INFINITY;
                    // BUG: S57_touch() work only for point in poly not point in line
                    //if (LINES_T == S57_getObjtype(candidate)) {
                        //GString *drval2str = S57_getAttVal(candidate, "DRVAL2");
                        //double   drval2    = (NULL == drval2str) ? UNKNOWN : S52_atof(drval2str->str);
                        //if (NULL!=drval2str && drval2<drvalmin) {
                        //    drvalmin = drval2;
                        //    S57_setTouch(geo, candidate);
                        //}
                    //} else { // AREA DEPARE and AREA DRGARE
                        GString *drval1str = S57_getAttVal(candidate, "DRVAL1");
                        double   drval1    = (NULL == drval1str) ? UNKNOWN : S52_atof(drval1str->str);
                        if (NULL!=drval1str && drval1<drvalmin) {
                            drvalmin = drval1;
                            S57_setTouchDEPARE(geo, candidate);
                        }
                    //}
                } else
                    S57_setTouchDEPARE(geo, candidate);
            }
        }

        // finish
        //return TRUE;
    }

    // _DEPVAL01 (called via OBSTRN04, WRECKS02)
    // set reference to oject of 'least_depht'
    if ((0==g_strcmp0(name, "OBSTRN")) ||
        (0==g_strcmp0(name, "WRECKS"))
       )
    {
        //unsigned int i = 0;

        // debug
        //if (582 == S57_getGeoID(geo))
        //    PRINTF("OBSTRN found\n");

        for (guint i=0; i<local->depval_list->len; ++i) {
            S57_geo *candidate = (S57_geo *) g_ptr_array_index(local->depval_list, i);

            // skip if extent not overlapping
            if (FALSE == _intersec(geo, candidate))
                continue;

            // NOTE: depval_list is a list of AREAS_T
            if (TRUE == S57_touch(geo, candidate)) {
                S57_geo *crntmin = S57_getTouchDEPVAL(geo);

                // case of more than one geo 'touch' this geo
                // link to the swallower
                if (NULL != crntmin) {
                    double   drvalmin  = INFINITY;
                    GString *drval1str = S57_getAttVal(candidate, "DRVAL1");
                    double   drval1    = (NULL == drval1str) ? UNKNOWN : S52_atof(drval1str->str);

                    if (NULL!=drval1str && drval1<drvalmin) {
                        drvalmin = drval1;
                        S57_setTouchDEPVAL(geo, candidate);
                    }
                } else
                    S57_setTouchDEPVAL(geo, candidate);
            }
        }

        // finish
        return TRUE;
    }

    // all other object
    //return FALSE;
    return TRUE;
}

//int        S52_CS_setVectorParam(int vecper, int vecstb, int vecmrk)
//{
//    _vecper = vecper;
//    _vecmrk = vecstb;
//    _vecstb = vecmrk;
//
//    return TRUE;
//}

//int        S52_CS_getVectorPer()
//{
//    return _vecper;
//}

//static int      _overlap(S57_geo *geoA, S57_geo *geoB)
static int      _sectOverlap(S57_geo *geoA, S57_geo *geoB)
// TRUE if A overlap B and arc of A is bigger, else FALSE
{
    // check for extend arc radius
    GString *Asectr1str = S57_getAttVal(geoA, "SECTR1");
    GString *Asectr2str = S57_getAttVal(geoA, "SECTR2");
    GString *Bsectr1str = S57_getAttVal(geoB, "SECTR1");
    GString *Bsectr2str = S57_getAttVal(geoB, "SECTR2");

    // check if sector present
    if (NULL == Asectr1str ||
        NULL == Asectr2str ||
        NULL == Bsectr1str ||
        NULL == Bsectr2str)
        return FALSE;

    {
        double Asectr1 = S52_atof(Asectr1str->str);
        double Asectr2 = S52_atof(Asectr2str->str);
        double Bsectr1 = S52_atof(Bsectr1str->str);
        double Bsectr2 = S52_atof(Bsectr2str->str);

        if (Asectr1 > Asectr2) Asectr2 += 360;
        if (Bsectr1 > Bsectr2) Bsectr2 += 360;

        if ((Bsectr1>=Asectr1 && Bsectr1<=Asectr2) ||
            (Bsectr2>=Asectr1 && Bsectr2<=Asectr2)) {
            double Asweep = Asectr2-Asectr1;
            double Bsweep = Bsectr2-Bsectr1;
            // is sector larger
            //if (Asweep > Bsweep)
            if (Asweep >= Bsweep)
                return TRUE;
        }
    }

    return FALSE;
}

static int      _parseList(const char *str, char *buf)
// Put a string of comma delimited number in an array (buf).
// Return: the number of value in buf.
// Assume: - number < 256,
//         - list size less then LISTSIZE-1 .
// Note: buf is \0 terminated for _strpbrk().
// FIXME: use g_strsplit_set() instead!
{
    int i = 0;

    if (NULL != str && *str != '\0') {
        do {
            if ( i>= LISTSIZE-1) {
                PRINTF("OVERFLOW - value in list lost!!\n");
                break;
            }

            /*
            if (255 <  (unsigned char) atoi(str)) {
                PRINTF("value overflow (>255)\n");
                exit(0);
            }
            */

            buf[i++] = (unsigned char) S52_atoi(str);

            // skip digit
            while('0' <= *str && *str <= '9') str++;
            //while(isdigit(*str))
            //    ++str;   // next
            // glib-2
            //while( g_ascii_isdigit(c) );   // next

        } while(*str++ != '\0');      // skip ',' or exit
    }

    buf[i] = '\0';

    return i;
}

static const char *_selSYcol(char *buf)
{
    // FIXME: C1 3.1 use LIGHTS0x          and specs 3.2 use LIGHTS1x

    const char *sym = ";SY(LIGHTDEF";            //sym = ";SY(LITDEF11";

    // max 1 color
    if ('\0' == buf[1]) {
        if (_strpbrk(buf, "\003"))
            sym = ";SY(LIGHTS01";          //sym = ";SY(LIGHTS11";
        else if (_strpbrk(buf, "\004"))
            sym = ";SY(LIGHTS02";          //sym = ";SY(LIGHTS12";
        else if (_strpbrk(buf, "\001\006\013"))
            sym = ";SY(LIGHTS03";          //sym = ";SY(LIGHTS13";
    } else {
        // max 2 color
        if ('\0' == buf[2]) {
            if (_strpbrk(buf, "\001") && _strpbrk(buf, "\003"))
                sym = ";SY(LIGHTS01";          //sym = ";SY(LIGHTS11";
            else if (_strpbrk(buf, "\001") && _strpbrk(buf, "\004"))
                sym = ";SY(LIGHTS02";          //sym = ";SY(LIGHTS12";
        }
    }

    return sym;
}

static GString *CLRLIN01 (S57_geo *geo)
// Remarks: A clearing line shows a single arrow head at one of its ends. The direction
// of the clearing line must be calculated from its line object in order to rotate
// the arrow head symbol and place it at the correct end. This cannot be
// achieved with a complex linestyle since linestyle symbols cannot be sized
// to the length of the clearing line. Instead a linestyle with a repeating pattern
// of arrow symbols had to be used which does not comply with the required
// symbolization.
{
    GString *clrlin01  = g_string_new(";SY(CLRLIN01);LS(SOLD,1,NINFO)");
    GString *catclrstr = S57_getAttVal(geo, "catclr");


    if (NULL != catclrstr) {
        if ('1' == *catclrstr->str)
            // NMT
            g_string_append(clrlin01, ";TX('NMT',2,1,2,'15110',-1,-1,CHBLK,51)");
        else
            // NLT
            g_string_append(clrlin01, ";TX('NLT',2,1,2,'15110',-1,-1,CHBLK,51)");

    }
    //PRINTF("Mariner's object not drawn\n");

    return clrlin01;
}

static GString *DATCVR01 (S57_geo *geo)
// Remarks: This conditional symbology procedure describes procedures for:
// - symbolizing the limit of ENC coverage;
// - symbolizing navigational purpose boundaries ("scale boundarie"); and
// - indicating overscale display.
//
// Note that the mandatory meta object M_QUAL:CATQUA is symbolized by the look-up table.
//
// Because the methods adopted by an ECDIS to meet the IMO and IHO requirements
// listed on the next page will depend on the manufacturer's software, and cannot be
// described in terms of a flow chart in the same way as other conditional procedures,
// this procedure is in the form of written notes.
{
    // FIXME: 'geo' useless
    (void) geo;

    // NOTE: this CS apply to object M_COVR and M_CSCL

    // debug --return empty command for now
    //GString *datcvr01 = g_string_new(";OP(----)");
    GString *datcvr01 = NULL;

    ///////////////////////
    // 1- REQUIREMENT
    // (IMO/IHO specs. explenation)

    ///////////////////////
    // 2- ENC COVERAGE
    //
    // 2.1- Limit of ENC coverage
    // FIXME: union of all M_COVR:CATCVR=1
    datcvr01 = g_string_new(";OP(3OD11060);LC(HODATA01)");

    // 2.2- No data areas
    // FIXME: This can be done outside of CS (ie when clearing the screen)
    // FIXME: ";OP(0OD11050);AC(NODATA);AP(NODATA)"
    // FIXME: set geo to cover earth (!)

    //////////////////////
    // 3- SCALE BOUNDARIES
    //
    // 3.1- Chart scale boundaties
    // FIXME: use Data set identification field,
    // intended usage (navigational purpose) (DSID,INTU)
    //g_string_append(datcvr01, ";OP(3OS21030);LS(SOLD,1,CHGRD)");
    // -OR-
    //g_string_append(datcvr01, ";OP(3OS21030);LC(SCLBDYnn)");

    // 3.2- Graphical index of navigational purpose
    // FIXME: draw extent of available SENC in DB

    //////////////////////
    // 4- OVERSCALE
    //
    // FIXME: get metadata CSCL of DSPM field
    // FIXME: get object M_CSCL or CSCALE
    // in gdal is named:
    // DSID:DSPM_CSCL (gdal metadata)
    // M_CSCL:CSCALE
    //
    // 4.1- Overscale indication
    // FIXME: compute, scale = [denominator of the compilation scale] /
    //                         [denominator of the display scale]
    // FIXME: draw overscale indication (ie TX("X%3.1f",scale))
    //        color SCLBR, display base

    //
    // 4.2- Ovescale area at a chart scale boundary
    // FIXME: to  put on STANDARD DISPLAY but this object
    // is on DISPLAYBASE in section 2
    //g_string_append(datcvr01, ";OP(3OS21030);AP(OVERSC01)");

    //
    // 4.3- Larger scale data available
    // FIXME: display indication of better scale available (?)

    // FIXME
    static int silent = FALSE;
    if (FALSE == silent) {
        PRINTF("NOTE: DATCVR01/OVERSCALE not computed\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    return datcvr01;
}

static GString *DATCVR02 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(DATCVR02) switch to CS(DATCVR01)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //GString *datcvr02 = g_string_new(";OP(----)");
    GString *datcvr02 = DATCVR01(geo);

    return datcvr02;
}

static GString *_SEABED01(double drval1, double drval2);
static GString *_RESCSP01(S57_geo *geo);
static GString *DEPARE01 (S57_geo *geo)
// Remarks: An object of the class "depth area" is coloured and covered with fill patterns
// according to the mariners selections of shallow contour, safety contour and
// deep contour. This requires a decision making process provided by the sub-procedure
// "SEABED01" which is called by this symbology procedure.
// Objects of the class "dredged area" are handled by this routine as well to
// ensure a consistent symbolization of areas that represent the surface of the
// seabed.
{
    GString *depare01  = NULL;
    int      objl      = 0;
    GString *objlstr   = NULL;
    GString *drval1str = S57_getAttVal(geo, "DRVAL1");
    double   drval1    = UNKNOWN;
    GString *drval2str = S57_getAttVal(geo, "DRVAL2");
    double   drval2    = UNKNOWN;

    drval1 = (NULL == drval1str) ? -1.0        : S52_atof(drval1str->str);
    drval2 = (NULL == drval2str) ? drval1+0.01 : S52_atof(drval2str->str);

    if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
        double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
        drval1 += datum;
        drval2 += datum;
    }

    depare01 = _SEABED01(drval1, drval2);

    objlstr = S57_getAttVal(geo, "OBJL");
    objl    = (NULL == objlstr) ? 0 : S52_atoi(objlstr->str);

    // debug --this should not trigger an assert since
    // there is no object number zero
    if (0 == objl) {
        PRINTF("ERROR: OBJL == 0 (this is impossible!)\n");
        //g_assert(objl);
    }

    if (DRGARE == objl) {
        g_string_append(depare01, ";AP(DRGARE01)");
        g_string_append(depare01, ";LS(DASH,1,CHGRF)");

        if (NULL != S57_getAttVal(geo, "RESTRN")) {
            GString *rescsp01 = _RESCSP01(geo);
            if (NULL != rescsp01) {
                g_string_append(depare01, rescsp01->str);
                g_string_free(rescsp01, TRUE);
            }
        }

    }

    return depare01;
}

static GString *DEPARE02 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(DEPARE02) --> CS(DEPARE01)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //return g_string_new(";LC(QUESMRK1)");
    return DEPARE01(geo);
}

static GString *_SNDFRM02(S57_geo *geo, double depth_value);
static GString *DEPCNT02 (S57_geo *geo)
// Remarks: An object of the class "depth contour" or "line depth area" is highlighted and must
// be shown under all circumstances if it matches the safety contour depth value
// entered by the mariner (see IMO PS 3.6). But, while the mariner is free to enter any
// safety contour depth value that he thinks is suitable for the safety of his ship, the
// SENC only contains a limited choice of depth contours. This symbology procedure
// determines whether a contour matches the selected safety contour. If the selected
// safety contour does not exist in the data, the procedure will default to the next deeper
// contour. The contour selected is highlighted as the safety contour and put in
// DISPLAYBASE. The procedure also identifies any line segment of the spatial
// component of the object that has a "QUAPOS" value indicating unreliable
// positioning, and symbolizes it with a double dashed line.
//
// Note: Depth contours are not normally labeled. The ECDIS may provide labels, on demand
// only as with other text, or provide the depth value on cursor picking
{
    GString *depcnt02  = NULL;
    int      safe      = FALSE;     // initialy not a safety contour
    GString *objlstr   = NULL;
    int      objl      = 0;
    GString *quaposstr = NULL;
    int      quapos    = 0;
    //double   depth_value;          // for depth label (facultative in S-52)


    objlstr = S57_getAttVal(geo, "OBJL");
    objl    = (NULL == objlstr) ? 0 : S52_atoi(objlstr->str);

    // debug
    //if (483 == S57_getGeoID(geo)) {
    //    PRINTF("483 found\n");
    //}
    //if (491 == S57_getGeoID(geo)) {
    //    PRINTF("491 found\n");
    //}

    // first reset original scamin
    S57_resetScamin(geo);

    // debug --this should not trigger an assert since
    // there is no object number zero
    g_assert(objl);

    // DEPARE (line)
    if (DEPARE==objl && LINES_T==S57_getObjtype(geo)) {
        GString *drval1str = S57_getAttVal(geo, "DRVAL1");
        // NOTE: if drval1 not given then set it to 0.0 (ie. LOW WATER LINE as FAIL-SAFE)
        double   drval1    = (NULL == drval1str) ? 0.0    : S52_atof(drval1str->str);
        GString *drval2str = S57_getAttVal(geo, "DRVAL2");
        double   drval2    = (NULL == drval2str) ? drval1 : S52_atof(drval2str->str);

        // paranoia
        g_assert(drval1 <= drval2);

        // adjuste datum
        if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
            double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
            drval1 += datum;
            drval2 += datum;
        }

        // FIXME: in some case this skip line that is the only one available
        //if ((drval1<=S52_MP_get(S52_MAR_SAFETY_CONTOUR)) && (drval2>=S52_MP_get(S52_MAR_SAFETY_CONTOUR))) {

            if (drval1 <= S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                if (drval2 >= S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                    safe = TRUE;

                    //char *name = S57_getName(geo);
                    //PRINTF("### DEPARE: SET SAFETY CONTOUR --> touch %s:%f\n", name, drval1);
                }
            } else {
            //    else {
                    // collect area DEPARE & DRGARE that touch this line
                    //S57_geo *geoTmp    = S57_getTouchDEPARE(geo);
                    //GString *drval1str = S57_getAttVal(geoTmp, "DRVAL1");
                    //double   drval1    = (NULL == drval1str) ? 0.0 : S52_atof(drval1str->str);
                    S57_geo *geoTouch       = S57_getTouchDEPARE(geo);
                    GString *drval1touchstr = S57_getAttVal(geoTouch, "DRVAL1");
                    double   drval1touch    = (NULL == drval1touchstr) ? 0.0 : S52_atof(drval1touchstr->str);
                    //drval1str = S57_getAttVal(geoTmp, "DRVAL1");
                    //drval1    = (NULL == drval1str) ? 0.0 : S52_atof(drval1str->str);

                    // adjuste datum
                    if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                        double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
                        drval1 += datum;
                    }

                    // debug
                    //if (483 == S57_getGeoID(geo)) {
                    //    PRINTF("XXX 483 found\n");
                    //}
                    //if (491 == S57_getGeoID(geo)) {
                    //    PRINTF("XXX 491 found\n");
                    //}

                    if (NULL == drval1touchstr) {
                        safe = TRUE;
                        //PRINTF("=== DEPARE: SET SAFETY CONTOUR --> touch NULL: %f\n", drval1);
                    } else {
                        // debug
                        //GString *drval2touchstr = S57_getAttVal(geoTouch, "DRVAL2");
                        //double   drval2touch    = (NULL == drval2touchstr) ? drval1touch : S52_atof(drval2touchstr->str);

                        if (drval1touch < S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                        //if (drval2touch < S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                        //if (drval2 > S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                            safe = TRUE;

                            //char *name = S57_getName(geoTmp);
                            //PRINTF("--- DEPARE: SET SAFETY CONTOUR --> touch %s:%f\n", name, drval1);
                        }
                        /*
                        else {
                            if (drval2 > S52_MP_get(S52_MAR_SAFETY_CONTOUR) && drval1 < S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                                safe = TRUE;
                            }

                            //else {
                            //    if (drval2touch > S52_MP_get(S52_MAR_SAFETY_CONTOUR) && drval1touch < S52_MP_get(S52_MAR_SAFETY_CONTOUR))
                            //        safe = TRUE;
                            //}
                        }
                        */
                    }
                }
            //}
            //depth_value = drval1;
        //}
    } else {
        // continuation A (DEPCNT (line))
        GString *valdcostr = S57_getAttVal(geo, "VALDCO");
        double   valdco    = (NULL == valdcostr) ? 0.0 : S52_atof(valdcostr->str);

        if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
            double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
            valdco += datum;
        }

        //depth_value = valdco;

        // debug
        //if (127 == S57_getGeoID(geo)) {
        //    PRINTF("127 found\n");
        //}

        if (valdco == S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
            safe = TRUE;
            //PRINTF("*** DEPCNT: SET SAFETY CONTOUR VALCO: %f\n", valdco);
        } else {
            if (valdco > S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                // collect area DEPARE & DRGARE that touche this line
                S57_geo *geoTmp    = S57_getTouchDEPARE(geo);
                GString *drval1str = S57_getAttVal(geoTmp, "DRVAL1");
                double   drval1    = (NULL == drval1str) ? 0.0 : S52_atof(drval1str->str);

                // debug
                //S57_dumpData(geo, FALSE);
                //S57_dumpData(geoTmp, FALSE);
                //PRINTF("---------------------------------\n");

                // adjuste datum
                if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                    double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
                    drval1 += datum;
                }

                if (NULL == drval1str) {
                    safe = TRUE;

                    //char *name = S57_getName(geoTmp);
                    //PRINTF("XXX DEPCNT: SET SAFETY CONTOUR --> touch %s:%f\n", name, drval1);
                } else {
                    if (drval1 < S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                        safe = TRUE;
                        //PRINTF("### DEPCNT: SET SAFETY CONTOUR DRVAL1: %f\n", drval1);
                    }
                }
            }
        }
    }

    // Continuation B
    // ASSUME: OGR split lines to preserv different QUAPOS for a given line
    // FIXME: check that the assumtion above is valid!
    quaposstr = S57_getAttVal(geo, "QUAPOS");
    if (NULL != quaposstr) {
        quapos = S52_atoi(quaposstr->str);
        if ( 2 <= quapos && quapos < 10) {
            if (safe)
                depcnt02 = g_string_new(";LS(DASH,2,DEPSC)");
            else
                depcnt02 = g_string_new(";LS(DASH,1,DEPCN)");
        }
    } else {
        if (safe)
            depcnt02 = g_string_new(";LS(SOLD,2,DEPSC)");
        else
            depcnt02 = g_string_new(";LS(SOLD,1,DEPCN)");
    }

    if (safe) {
        //S57_setAtt(geo, "SCAMIN", "INFINITY");
        //S57_setScamin(geo, (1.0/0.0));
        S57_setScamin(geo, INFINITY);
        depcnt02 = g_string_prepend(depcnt02, ";OP(8OD13010)");
    } else
        depcnt02 = g_string_prepend(depcnt02, ";OP(---33020)");

    // depth label (facultative in S-52)
    //if (TRUE == S52_MP_get(S52_MAR_SHOW_TEXT)) {
    //    GString *sndfrm02 = _SNDFRM02(geo, depth_value);
    //    depcnt02 = g_string_append(depcnt02, sndfrm02->str);
    //    g_string_free(sndfrm02, TRUE);
    //}

    // debug
    //PRINTF("depth= %f\n", depth_value);

    return depcnt02;
}

static GString *DEPCNT03 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(DEPCNT03) --> CS(DEPCNT02)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //return g_string_new(";LC(QUESMRK1)");
    return DEPCNT02(geo);
}

static double   _DEPVAL01(S57_geo *geo, double least_depth)
// Remarks: S-57 Appendix B1 Annex A requires in Section 6 that areas of rocks be
// encoded as area obstruction, and that area OBSTRNs and area WRECKS
// be covered by either group 1 object DEPARE or group 1 object UNSARE.
// If the value of the attribute VALSOU for an area OBSTRN or WRECKS
// is missing, the DRVAL1 of an underlying DEPARE is the preferred default
// for establishing a depth vale. This procedure either finds the shallowest
// DRVAL1 of the one or more underlying DEPAREs, or returns an
// "unknown"" depth value to the main procedure for the next default
// procedure.

// NOTE: UNSARE test is useless since least_depth is already UNKNOWN
{
    // collect group 1 area DEPARE & DRGARE that touch this point/line/area
    S57_geo *geoTmp    = S57_getTouchDEPVAL(geo);
    GString *drval1str = S57_getAttVal(geoTmp, "DRVAL1");
    double   drval1    = (NULL == drval1str) ? UNKNOWN : S52_atof(drval1str->str);

    // NOTE: change procedure to use any incomming geometry
    // on area DEPARE & DRGARE (S52 say to use area UNSARE & DEPARE
    // but this sound awkward since UNSARE doesn't have any depth!
    // so it has to default to UNKNOWN implicitly!)
    // FIX: return default UNKNOWN, assume the worst
    // NOTE: maybe if an UNSARE is found then all other underlying
    // objects can be ignore
    least_depth = UNKNOWN;

    // debug
    //PRINTF("DEBUG: %s:%c\n", S57_getName(geo), S57_getObjtype(geo));
    //S57_dumpData(geo, FALSE);


    if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
        double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
        if (UNKNOWN != drval1)
            drval1 += datum;
    }

    if (NULL != drval1str) {
        if (UNKNOWN==least_depth || least_depth<drval1)
            least_depth = drval1;
    }

    return least_depth;
}

static GString *LEGLIN02 (S57_geo *geo)

// Remarks: The course of a leg is given by its start and end point. Therefore this
// conditional symbology procedure calculates the course and shows it
// alongside the leg. It also places the "distance to run" labels and cares for the
// different presentation of planned & alternate legs.
{
    GString *leglin02  = g_string_new("");
    GString *selectstr = S57_getAttVal(geo, "select");
    GString *plnspdstr = S57_getAttVal(geo, "plnspd");

    if ((NULL!=selectstr) && ('1'==*selectstr->str)) {
        g_string_append(leglin02, ";SY(PLNSPD03);LC(PLNRTE03)");
        // LUCM 42210 DISPLAYBASE
    } else {
        // alternate or undefined (check the later)
        g_string_append(leglin02, ";SY(PLNSPD04);LS(DOTT,2,APLRT)");
        // LUCM 52210 STANDARD
    }

    // NOTE: problem of determining witch TX() to draw

    // TX: course made good (mid point of leg)
    g_string_append(leglin02, ";TX('leglin',2,1,2,'15110',-1,-1,CHBLK,51)");

    // TX: planned speed (mid point of leg)
    if ((NULL!=plnspdstr) && (0.0<S52_atof(plnspdstr->str)))
        g_string_append(leglin02, ";TX('leglin',2,1,2,'15110',-1,-1,CHBLK,51)");

    // TX: distance tags
    if (0.0 < S52_MP_get(S52_MAR_DISTANCE_TAGS)) {
        g_string_append(leglin02, ";SY(PLNPOS02);TX('leglin',2,1,2,'15110',-1,-1,CHBLK,51)");
    }

    return leglin02;
}

static GString *LEGLIN03 (S57_geo *geo)

// Remarks: The course of a leg is given by its start and end point. Therefore this
// conditional symbology procedure calculates the course and shows it
// alongside the leg. It also places the "distance to run" labels and cares for the
// different presentation of planned & alternate legs.
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(LEGLIN03) switching to CS(LEGLIN02)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }


    // next WP
    //SY(WAYPNT11)



    //PRINTF("Mariner's object not drawn\n");

    GString *leglin03 = LEGLIN02(geo);

    return leglin03;
}

static GString *_LITDSN01(S57_geo *geo);
static GString *LIGHTS05 (S57_geo *geo)
// Remarks: A light is one of the most complex S-57 objects. Its presentation depends on
// whether it is a light on a floating or fixed platform, its range, it's colour and
// so on. This conditional symbology procedure derives the correct
// presentation from these parameters and also generates an area that shows the
// coverage of the light.
//
// Notes on light sectors:
// 1.) The radial leg-lines defining the light sectors are normally drawn to only 25mm
// from the light to avoid clutter (see Part C). However, the mariner should be able to
// select "full light-sector lines" and have the leg-lines extended to the nominal range
// of the light (VALMAR).
//
// 2.) Part C of this procedure symbolizes the sectors at the light itself. In addition,
// it should be possible, upon request, for the mariner to be capable of identifying
// the colour and sector limit lines of the sectors affecting the ship even if the light
// itself is off the display.
// [ed. last sentence in bold]

// NOTE: why is this relationship not already encoded in S57 (ei. C_AGGR or C_STAC) ?

{
    GString *lights05          = NULL;
    //GString *valnmrstr         = S57_getAttVal(geo, "VALNMR");
    //double   valnmr            = 0.0;
    GString *catlitstr         = S57_getAttVal(geo, "CATLIT");
    char     catlit[LISTSIZE]  = {'\0'};
    int      flare_at_45       = FALSE;
    int      extend_arc_radius = TRUE;
    GString *sectr1str         = NULL;
    GString *sectr2str         = NULL;
    double   sectr1            = 0.0;
    double   sectr2            = 0.0;
    GString *colourstr         = NULL;
    char     colist[LISTSIZE]  = {'\0'};   // colour list
    GString *orientstr         = NULL;
    double   sweep             = 0.0;

    // debug - 1556
    //if (1555 == S57_getGeoID(geo)) {
    //    PRINTF("lights found\n");
    //}

    lights05 = g_string_new("");

    // NOTE: valmnr is only use when rendering
    //valnmr = (NULL == valnmrstr) ? 9.0 : S52_atof(valnmrstr->str);

    if ( NULL != catlitstr) {
        _parseList(catlitstr->str, catlit);

        // FIXME: OR vs AND/OR
        if (_strpbrk(catlit, "\010\013")) {
            g_string_append(lights05, ";SY(LIGHTS82)");
            return lights05;
        }

        if (_strpbrk(catlit, "\011")) {
            g_string_append(lights05, ";SY(LIGHTS81)");
            return lights05;
        }

        // bail out if this light is an emergecy light
        if (_strpbrk(catlit, "\021")) {
            return lights05;
        }

        if (_strpbrk(catlit, "\001\020")) {
            orientstr = S57_getAttVal(geo, "ORIENT");
            if (NULL != orientstr) {
                // FIXME: create a geo object (!?) LINE of lenght VALNMR
                // using ORIENT (from seaward) & POINT_T position
                g_string_append(lights05, ";LS(DASH,1,CHBLK)");
            }
        }
    }

    // Continuation A
    colourstr = S57_getAttVal(geo, "COLOUR");
    if (NULL != colourstr)
        _parseList(colourstr->str, colist);
    else {
        colist[0] = '\014';  // maganta (12)
        colist[1] = '\000';
    }

    sectr1str = S57_getAttVal(geo, "SECTR1");
    sectr1    = (NULL == sectr1str) ? 0.0 : S52_atof(sectr1str->str);
    sectr2str = S57_getAttVal(geo, "SECTR2");
    sectr2    = (NULL == sectr2str) ? 0.0 : S52_atof(sectr2str->str);

    if (NULL==sectr1str || NULL==sectr2str) {
        // not a sector light
        const char *sym;

        //flare_at_45 = _setPtPos(geo, LIGHTLIST);
        //if (_setPtPos(geo, LIGHTLIST)) {
        if (NULL != S57_getTouchLIGHTS(geo)) {
            if (_strpbrk(colist, "\001\005\013"))
                flare_at_45 = TRUE;
        }

        sym = _selSYcol(colist);

        if (_strpbrk(catlit, "\001\020")) {
            if (NULL != orientstr){
                g_string_append(lights05, sym);
                g_string_sprintfa(lights05, ",%s)", orientstr->str);
                g_string_append(lights05, ";TE('%03.0lf deg','ORIENT',3,3,3,'15110',3,1,CHBLK,23)" );
            } else
                g_string_append(lights05, ";SY(QUESMRK1)");
        } else {
            g_string_append(lights05, sym);
            if (flare_at_45)
                g_string_append(lights05, ", 45)");
            else
                g_string_append(lights05, ",135)");
        }

        GString *litdsn01 = _LITDSN01(geo);
        if (NULL != litdsn01){
            g_string_append(lights05, ";TX('");
            g_string_append(lights05, litdsn01->str);
            g_string_free(litdsn01, TRUE);

            if (flare_at_45)
                g_string_append(lights05, "',3,3,3,'15110',2,-1,CHBLK,23)" );
            else
                g_string_append(lights05, "',3,2,3,'15110',2,0,CHBLK,23)" );
        }

        return lights05;
    }

    // Continuation B --sector light
    if (NULL == sectr1str) {
        sectr1 = 0.0;
        sectr2 = 0.0;
    } else
        sweep = (sectr1 > sectr2) ? sectr2-sectr1+360 : sectr2-sectr1;


    if (sweep<1.0 || sweep==360.0) {
        // handle all round light
        const char *sym = _selSYcol(colist);;

        g_string_append(lights05, sym);
        g_string_append(lights05, ",135)");

        GString *litdsn01 = _LITDSN01(geo);
        if (NULL != litdsn01) {
            g_string_append(lights05, ";TX('");
            g_string_append(lights05, litdsn01->str);
            g_string_append(lights05, "',3,2,3,'15110',2,0,CHBLK,23)" );
            g_string_free(litdsn01, TRUE);
        }

        return lights05;
    } else {
        // sector light: set sector legs
        // NOTE:
        // 'LEGLEN' = 'VALNMR' or 'LEGLEN' = 25mm
        // is done in _renderLS_LIGHTS05()
        g_string_append(lights05, ";LS(DASH,1,CHBLK)");
    }

    //extend_arc_radius = _setPtPos(geo, SECTRLIST);
    S57_geo *geoTmp = NULL;
    for (geoTmp=S57_getTouchLIGHTS(geo); geoTmp!=NULL; geoTmp=S57_getTouchLIGHTS(geoTmp)) {
        extend_arc_radius = _sectOverlap(geo, geoTmp);


        // passe value via attribs to _renderAC()
        if (extend_arc_radius) {
            GString   *extradstr = S57_getAttVal(geoTmp, "extend_arc_radius");

            if (NULL!=extradstr && 'Y'==*extradstr->str)
                S57_setAtt(geo, "extend_arc_radius", "N");
            else
                S57_setAtt(geo, "extend_arc_radius", "Y");

        } else
            S57_setAtt(geo, "extend_arc_radius", "N");

    }

    // setup sector
    {
        char litvis[LISTSIZE] = {'\0'};  // light visibility
        GString *litvisstr = S57_getAttVal(geo, "LITVIS");

        // get light vis.
        if (NULL != litvisstr) _parseList(litvisstr->str, litvis);

        // faint light
        // FIXME: spec say OR (ie 1 number) the code is AND/OR
        if (_strpbrk(litvis, "\003\007\010")) {
            // NOTE: LS(DASH,1,CHBLK)
            // pass flag to _renderAC()

            // FIXME: what is that !? 'faint_light' is not used anywhere
            // find specs for this
            //g_string_append(lights05, ";AC(CHBLK)");
            //S57_setAtt(geo, "faint_light", "Y");

            // sector leg --logic is _renderLS()
            g_string_append(lights05, ";LS(DASH,1,CHBLK)");

        } else {
            // set arc colour
            const char *sym = ";AC(CHMGD)";  // other

            // max 1 color
            if ('\0' == colist[1]) {
                if (_strpbrk(colist, "\003"))
                    sym = ";AC(LITRD)";
                else if (_strpbrk(colist, "\004"))
                    sym = ";AC(LITGN)";
                else if (_strpbrk(colist, "\001\006\013"))
                    sym = ";AC(LITYW)";
            } else {
                // max 2 color
                if ('\0' == colist[2]) {
                    if (_strpbrk(colist, "\001") && _strpbrk(colist, "\003"))
                        sym = ";AC(LITRD)";
                    else if (_strpbrk(colist, "\001") && _strpbrk(colist, "\004"))
                        sym = ";AC(LITGN)";
                }
            }

            g_string_append(lights05, sym);
        }
    }

    return lights05;
}

static GString *_LITDSN01(S57_geo *geo)
// Remarks: In S-57 the light characteristics are held as a series of attributes values. The
// mariner may wish to see a light description text string displayed on the
// screen similar to the string commonly found on a paper chart. This
// conditional procedure, reads the attribute values from the above list of
// attributes and composes a light description string which can be displayed.
// This procedure is provided as a C function which has as input, the above
// listed attribute values and as output, the light description.
{
    GString *litdsn01         = g_string_new("");
    GString *gstr             = NULL;  // tmp
    GString *catlitstr        = S57_getAttVal(geo, "CATLIT");
    char     catlit[LISTSIZE] = {'\0'};
    GString *litchrstr        = S57_getAttVal(geo, "LITCHR");
    char     litchr[LISTSIZE] = {'\0'};
    GString *colourstr        = S57_getAttVal(geo, "COLOUR");
    char     colour[LISTSIZE] = {'\0'};
    GString *statusstr        = S57_getAttVal(geo, "STATUS");
    char     status[LISTSIZE] = {'\0'};

    // FIXME: need grammar to create light's text

    // CATLIT, LITCHR, COLOUR, HEIGHT, LITCHR, SIGGRP, SIGPER, STATUS, VALNMR

    // debug
    if (3154 == S57_getGeoID(geo)) {
        PRINTF("lights found         XXXXXXXXXXXXXXXXXXXXXXX\n");
    }


    // CATLIT
    if (NULL != catlitstr) {
        const char *tmp     = NULL;
        int         i       = 0;
        int         ncatlit = 0;

        ncatlit = _parseList(catlitstr->str, catlit);

        //if (1 < _parseList(catlitstr->str, catlit))
        //    PRINTF("WARNING: more then one 'category of light' (CATLIT), other not displayed (%s)\n", catlitstr->str);

        while (i < ncatlit) {
            switch (catlit[i]) {
                // CATLIT attribute has no value!
                case 0: break;

                //1: directional function    IP 30.1-3;  475.7;
                case 1: tmp = "Dir "; break;

                //2: rear/upper light
                //3: front/lower light
            	case 3:
                //4: leading light           IP 20.1-3;  475.6;
                case 4: break;

                //5: aero light              IP 60;      476.1;
                case 5: tmp = "Aero "; break;

                //6: air obstruction light   IP 61;      476.2;
                case 6: tmp = "Aero "; break;                    // CHS chart1.pdf (INT)
                //7: fog detector light      IP 62;      477;
                //8: flood light             IP 63;      478.2;
                //9: strip light             IP 64;      478.5;
                //10: subsidiary light        IP 42;      471.8;
                //11: spotlight
                //12: front
            	case 12: break;
                //13: rear
            	case 13: break;
                //14: lower
                //15: upper
                //16: moire' effect         IP 31;      475.8;

                //17: emergency (bailout because this text overight the good one)
                case 17:
                    g_string_free(litdsn01, TRUE);
                    litdsn01 = NULL;
                    return NULL;
                   //break;
                //18: bearing light                       478.1;
                //19: horizontally disposed
                //20: vertically disposed

                default:
                    // FIXME: what is a good default
                    // or should it be left empty!
                    tmp = "FIXME:CATLIT ";
                    //PRINTF("WARNING: no abreviation for CATLIT (%i)\n", catlit[0]);
            }
            ++i;
        }
        if (NULL != tmp)
            g_string_append(litdsn01, tmp);
    }


    // LITCHR
    if (NULL != litchrstr) {
        const char *tmp = NULL;

        if (1 < _parseList(litchrstr->str, litchr)) {
            PRINTF("ERROR: more then one 'light characteristic' (LITCHR), other not displayed\n");
            g_assert(0);
        }

        switch (litchr[0]) {
            //1: fixed                             IP 10.1;
            case 1: tmp = "F"; break;
            //2: flashing                          IP 10.4;
            case 2: tmp = "Fl"; break;
            //3: long-flashing                     IP 10.5;
            case 3: tmp = "LFl"; break;
            //4: quick-flashing                    IP 10.6;
            case 4: tmp = "Q";   break;
            //5: very quick-flashing               IP 10.7;
            case 5: tmp = "VQ"; break;
            //6: ultra quick-flashing              IP 10.8;
            case 6: tmp = "UQ"; break;
            //7: isophased                         IP 10.3;
            case 7: tmp = "Iso"; break;
            //8: occulting                         IP 10.2;
            case 8: tmp = "Oc"; break;
            //9: interrupted quick-flashing        IP 10.6;
            case 9: tmp = "IQ"; break;
            //10: interrupted very quick-flashing   IP 10.7;
            case 10: tmp = "IVQ"; break;
            //11: interrupted ultra quick-flashing  IP 10.8;
            case 11: tmp = "IUQ"; break;
            //12: morse                             IP 10.9;
            case 12: tmp = "Mo"; break;
            //13: fixed/flash                       IP 10.10;
            case 13: tmp = "FFl"; break;
            //14: flash/long-flash
            case 14: tmp = "Fl+LFl"; break;
            // FIXME: not mention of 'alternating' occulting/flash in S57 attributes
            // but S52 say 'alternating occulting/flash' (p. 188)
            //15: occulting/flash
            case 15: tmp = "AlOc Fl"; break;
            //16: fixed/long-flash
            case 16: tmp = "FLFl"; break;
            //17: occulting alternating
            case 17: tmp = "AlOc"; break;
            //18: long-flash alternating
            case 18: tmp = "AlLFl"; break;
            //19: flash alternating
            case 19: tmp = "AlFl"; break;
            //20: group alternating
            case 20: tmp = "Al"; break;

            //21: 2 fixed (vertical)
            //22: 2 fixed (horizontal)
            //23: 3 fixed (vertical)
            //24: 3 fixed (horizontal)

            //25: quick-flash plus long-flash
            case 25: tmp = "Q+LFl"; break;
            //26: very quick-flash plus long-flash
            case 26: tmp = "VQ+LFl"; break;
            //27: ultra quick-flash plus long-flash
            case 27: tmp = "UQ+LFl"; break;
            //28: alternating
            case 28: tmp = "Al"; break;
            //29: fixed and alternating flashing
            case 29: tmp = "AlF Fl"; break;

            default:
                // FIXME: what is a good default
                // or should it be left empty!
                tmp = "FIXME:LITCHR ";
                //PRINTF("WARNING: no abreviation for LITCHR (%i)\n", litchr[0]);
        }
        g_string_append(litdsn01, tmp);
    }

    // SIGGRP, (c)(c) ..., SIGnal light GRouPing
    gstr = S57_getAttVal(geo, "SIGGRP");
    if (NULL != gstr) {
        //PRINTF("WARNING: SIGGRP not translated into text (%s)\n", gstr->str);
        g_string_append(litdsn01, gstr->str);
        //g_string_append(litdsn01, " ");
    }

    // COLOUR,
    if (NULL != colourstr) {
        const char *tmp = NULL;

        //if (1 < _parseList(colourstr->str, colour))
        //    PRINTF("WARNING: more then one 'colour' (COLOUR), other not displayed\n");
        int ncolor = _parseList(colourstr->str, colour);
        int i = 0;

        for (i=0; i<ncolor; ++i) {
            switch (colour[0]) {
                //1: white   IP 11.1;    450.2-3;
                case 1: tmp = "W"; break;

                //2: black

                //3: red     IP 11.2;   450.2-3;
                case 3: tmp = "R"; break;
                //4: green   IP 11.3;   450.2-3;
                case 4: tmp = "G"; break;
                //5: blue    IP 11.4;   450.2-3;
                case 5: tmp = "Bu"; break;        // CHS chart1.pdf (INT)
                //6: yellow  IP 11.6;   450.2-3;
                case 6: tmp = "Y"; break;

                //7: grey
                //8: brown

                //9: amber   IP 11.8;   450.2-3;
                case 9:  tmp = "Am"; break;       // CHS chart1.pdf (INT)
                //10: violet  IP 11.5;   450.2-3;
                case 10: tmp = "Vi"; break;       // CHS chart1.pdf (INT)
                //11: orange  IP 11.7;   450.2-3;
                case 11: tmp = "Or"; break;       // CHS chart1.pdf (INT)
                //12: magenta
                //13: pink

                default:
                    // FIXME: what is a good default
                    // or should it be left empty!
                    tmp = "FIXME:COLOUR ";
                    //PRINTF("WARNING: no abreviation for COLOUR (%i)\n", colour[0]);
            }
            g_string_append(litdsn01, tmp);
        }
        g_string_append(litdsn01, " ");
    }

    // SIGPER, xx.xx, SIGnal light PERiod
    gstr = S57_getAttVal(geo, "SIGPER");
    if (NULL != gstr) {
        //PRINTF("WARNING: SIGPER not translated into text (%s)\n", gstr->str);
        g_string_append(litdsn01, gstr->str);
        g_string_append(litdsn01, "s ");
    }

    // HEIGHT, xxx.x
    gstr = S57_getAttVal(geo, "HEIGHT");
    if (NULL != gstr) {
        if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
            double datum  = S52_MP_get(S52_MAR_DATUM_OFFSET);
            double height = S52_atof(gstr->str);
            height -= datum;
            char str[8] = {0};
            g_snprintf(str, 8, "%.1fm ", height);
            g_string_append(litdsn01, str);
        } else {
            g_string_append(litdsn01, gstr->str);
            g_string_append(litdsn01, "m ");
        }
    }

    // VALNMR, xx.x
    gstr = S57_getAttVal(geo, "VALNMR");
    if (NULL != gstr) {
        // BUG: GDAL rounding problem (some value is 14.99 instead of 15)
        if (gstr->len > 3) {
            char str[5];
            // reformat in full
            g_snprintf(str, 3, "%3.1f", S52_atof(gstr->str));
            g_string_append(litdsn01, str);
        } else {
            //PRINTF("VALNMR:%s\n", gstr->str);
            g_string_append(litdsn01, gstr->str);
        }

        // FIXME: conflict in specs, nominal range in nautical miles in S57
        // S52 imply that it can be express in meter
        g_string_append(litdsn01, "M");
    }

    // STATUS,
    if (NULL != statusstr) {
        const char *tmp = NULL;

        if (1 < _parseList(statusstr->str, status))
            PRINTF("ERROR: more then one 'status' (STATUS), other not displayed\n");

        switch (status[0]) {
            //1: permanent

            //2: occasional             IP 50;  473.2;
            case 2: tmp = "occas"; break;

            //3: recommended            IN 10;  431.1;
            //4: not in use             IL 14, 44;  444.7;
            //5: periodic/intermittent  IC 21; IQ 71;   353.3; 460.5;
            //6: reserved               IN 12.9;

            //7: temporary              IP 54;
            case 7: tmp = "temp"; break;
            //8: private                IQ 70;
            case 8: tmp = "priv"; break;

            //9: mandatory
            //10: destroyed/ruined

            //11: extinguished
            case 11: tmp = "exting"; break;

            //12: illuminated
            //13: historic
            //14: public
            //15: synchronized
            //16: watched
            //17: un-watched
            //18: existence doubtful
            default:
                // FIXME: what is a good default
                // or should it be left empty!
                tmp = "FIXME:STATUS ";
                //PRINTF("WARNING: no abreviation for STATUS (%i)\n", status[0]);
        }
        g_string_append(litdsn01, tmp);
    }

    return litdsn01;
}

static GString *_UDWHAZ03(S57_geo *geo, double depth_value);
static GString *_QUAPNT01(S57_geo *geo);

static GString *OBSTRN04 (S57_geo *geo)
// Remarks: Obstructions or isolated underwater dangers of depths less than the safety
// contour which lie within the safe waters defined by the safety contour are
// to be presented by a specific isolated danger symbol and put in IMO
// category DISPLAYBASE (see (3), App.2, 1.3). This task is performed
// by the sub-procedure "UDWHAZ03" which is called by this symbology
// procedure. Objects of the class "under water rock" are handled by this
// routine as well to ensure a consistent symbolization of isolated dangers on
// the seabed.
//
// NOTE: updated to Cs1_md.pdf (ie was OBSTRN03)

{
    GString *obstrn04str = g_string_new("");
    GString *sndfrm02str = NULL;
    GString *udwhaz03str = NULL;
    GString *valsoustr   = S57_getAttVal(geo, "VALSOU");
    double   valsou      = UNKNOWN;
    double   depth_value = UNKNOWN;
    double   least_depth = UNKNOWN;

    // debug CA49995B.000:305859
    //GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //if (0==strcmp("965651", FIDNstr->str)) {
    //    PRINTF("%s\n",FIDNstr->str);
    //}

    if (NULL != valsoustr) {
        valsou      = S52_atof(valsoustr->str);
        depth_value = valsou;
        sndfrm02str = _SNDFRM02(geo, depth_value);
    } else {
        if (AREAS_T == S57_getObjtype(geo))
            least_depth = _DEPVAL01(geo, least_depth);

        if (UNKNOWN != least_depth) {
            GString *catobsstr = S57_getAttVal(geo, "CATOBS");
            GString *watlevstr = S57_getAttVal(geo, "WATLEV");

            if (NULL != catobsstr && '6' == *catobsstr->str)
                depth_value = 0.01;
            else {
                if (NULL == watlevstr) // default
                    depth_value = -15.0;
                else {
                    switch (*watlevstr->str){
                        case '5': depth_value =   0.0 ; break;
                        case '3': depth_value =   0.01; break;
                        case '4':
                        case '1':
                        case '2':
                        default : depth_value = -15.0 ; break;
                    }
                }
            }
        } else
            depth_value = least_depth;
    }

    udwhaz03str = _UDWHAZ03(geo, depth_value);

    if (POINT_T == S57_getObjtype(geo)) {
        // Continuation A
        int      sounding    = FALSE;
        GString *quapnt01str = _QUAPNT01(geo);

        if (NULL != udwhaz03str){
            g_string_append(obstrn04str, udwhaz03str->str);
            if (NULL != quapnt01str)
                g_string_append(obstrn04str, quapnt01str->str);

            if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
            if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);
            if (NULL != quapnt01str) g_string_free(quapnt01str, TRUE);

            return obstrn04str;
        }

        if (UNKNOWN != valsou) {
            if (valsou <= 20.0) {
                GString *objlstr   = S57_getAttVal(geo, "OBJL");
                int      objl      = (NULL == objlstr)? 0 : S52_atoi(objlstr->str);
                GString *watlevstr = S57_getAttVal(geo, "WATLEV");

                // debug --this should not trigger an assert since
                // there is no object number zero
                g_assert(objl);

                if (UWTROC == objl) {
                    if (NULL == watlevstr) {  // default
                        g_string_append(obstrn04str, ";SY(DANGER01)");
                        sounding = TRUE;
                    } else {
                        switch (*watlevstr->str){
                            case '3': g_string_append(obstrn04str, ";SY(DANGER01)"); sounding = TRUE ; break;
                            case '4':
                            case '5': g_string_append(obstrn04str, ";SY(UWTROC04)"); sounding = FALSE; break;
                            default : g_string_append(obstrn04str, ";SY(DANGER01)"); sounding = TRUE ; break;
                        }
                    }
                } else { // OBSTRN
                    if (NULL == watlevstr) { // default
                        g_string_append(obstrn04str, ";SY(DANGER01)");
                        sounding = TRUE;
                    } else {
                        switch (*watlevstr->str) {
                            case '1':
                            case '2': g_string_append(obstrn04str, ";SY(OBSTRN11)"); sounding = FALSE; break;
                            case '3': g_string_append(obstrn04str, ";SY(DANGER01)"); sounding = TRUE;  break;
                            case '4':
                            case '5': g_string_append(obstrn04str, ";SY(DANGER03)"); sounding = TRUE; break;
                            default : g_string_append(obstrn04str, ";SY(DANGER01)"); sounding = TRUE; break;
                        }
                    }
                }
            } else {  // valsou > 20.0
                g_string_append(obstrn04str, ";SY(DANGER02)");
                sounding = FALSE;
            }

        } else {  // NO valsou
                GString *objlstr   = S57_getAttVal(geo, "OBJL");
                int     objl       = (NULL == objlstr)? 0 : S52_atoi(objlstr->str);
                GString *watlevstr = S57_getAttVal(geo, "WATLEV");

                // debug --this should not trigger an assert since
                // there is no object number zero
                g_assert(objl);

                if (UWTROC == objl) {
                    if (NULL == watlevstr)  // default
                       g_string_append(obstrn04str, ";SY(UWTROC04)");
                    else {
                        if ('3' == *watlevstr->str)
                            g_string_append(obstrn04str, ";SY(UWTROC03)");
                        else
                            g_string_append(obstrn04str, ";SY(UWTROC04)");
                    }

                } else { // OBSTRN
                    if ( NULL == watlevstr) // default
                        g_string_append(obstrn04str, ";SY(OBSTRN01)");
                    else {
                        switch (*watlevstr->str) {
                            case '1':
                            case '2': g_string_append(obstrn04str, ";SY(OBSTRN11)"); break;
                            case '3': g_string_append(obstrn04str, ";SY(OBSTRN01)"); break;
                            case '4':
                            case '5':
                            default : g_string_append(obstrn04str, ";SY(OBSTRN01)"); break;
                        }
                    }
                }

        }

        if (TRUE==sounding && NULL!=sndfrm02str)
            g_string_append(obstrn04str, sndfrm02str->str);

        if (NULL != quapnt01str)
            g_string_append(obstrn04str, quapnt01str->str);

        if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
        if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);
        if (NULL != quapnt01str) g_string_free(quapnt01str, TRUE);

        return obstrn04str;

    } else {
        if (LINES_T == S57_getObjtype(geo)) {
            // Continuation B
            GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
            int      quapos    = 0;

            if (NULL != quaposstr) {
                quapos = S52_atoi(quaposstr->str);
                if ( 2 <= quapos && quapos < 10){
                    if (NULL != udwhaz03str)
                        g_string_append(obstrn04str, ";LC(LOWACC41)");
                    else
                        g_string_append(obstrn04str, ";LC(LOWACC31)");
                }
            }

            if (NULL != udwhaz03str)
                g_string_append(obstrn04str, ";LS(DOTT,2,CHBLK)");

            if (UNKNOWN != valsou) {
                if (valsou <= 20.0)
                    g_string_append(obstrn04str, ";LS(DOTT,2,CHBLK)");
                else
                    g_string_append(obstrn04str, ";LS(DASH,2,CHBLK)");
            } else
                g_string_append(obstrn04str, ";LS(DOTT,2,CHBLK)");


            if (NULL != udwhaz03str)
                g_string_append(obstrn04str, udwhaz03str->str);
            else {
                if (UNKNOWN != valsou)
                    if (valsou<=20.0 && NULL!=sndfrm02str)
                        g_string_append(obstrn04str, sndfrm02str->str);
            }

            if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
            if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);

            return obstrn04str;

        } else {
            // Continuation C (AREAS_T)
            GString *quapnt01str = _QUAPNT01(geo);
            if (NULL != udwhaz03str) {
                g_string_append(obstrn04str, ";AC(DEPVS);AP(FOULAR01)");
                g_string_append(obstrn04str, ";LS(DOTT,2,CHBLK)");
                g_string_append(obstrn04str, udwhaz03str->str);
                if (NULL != quapnt01str)
                    g_string_append(obstrn04str, quapnt01str->str);

                if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
                if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);
                if (NULL != quapnt01str) g_string_free(quapnt01str, TRUE);

                return obstrn04str;
            }

            if (UNKNOWN != valsou) {
                // S52 BUG (see CA49995B.000:305859) we get here because
                // there is no color beside NODATA (ie there is a hole in group 1 area!)
                // and this mean there is still not AC() command at this point.
                // FIX 1: add danger.
                // g_string_append(obstrn04str, ";AC(DNGHL)");
                // FIX 2: do nothing and CA49995B.000:305859 will have NODATA
                // wich is normal for a test data set !


                if (valsou <= 20.0)
                    g_string_append(obstrn04str, ";LS(DOTT,2,CHBLK)");
                else
                    g_string_append(obstrn04str, ";LS(DASH,2,CHBLK)");

                if (NULL != sndfrm02str)
                    g_string_append(obstrn04str, sndfrm02str->str);

            } else {
                GString *watlevstr = S57_getAttVal(geo, "WATLEV");

                if (NULL == watlevstr)   // default
                    g_string_append(obstrn04str, ";AC(DEPVS);LS(DOTT,2,CHBLK)");
                else {
                    if ('3' == *watlevstr->str) {
                        GString *catobsstr = S57_getAttVal(geo, "CATOBS");
                        if (NULL != catobsstr && '6' == *catobsstr->str)
                            g_string_append(obstrn04str, ";AC(DEPVS);AP(FOULAR01);LS(DOTT,2,CHBLK)");
                    } else {
                        switch (*watlevstr->str) {
                            case '1':
                            case '2': g_string_append(obstrn04str, ";AC(CHBRN);LS(SOLD,2,CSTLN)"); break;
                            case '4': g_string_append(obstrn04str, ";AC(DEPIT);LS(DASH,2,CSTLN)"); break;
                            case '5':
                            case '3':
                            default : g_string_append(obstrn04str, ";AC(DEPVS);LS(DOTT,2,CHBLK)");  break;
                        }
                    }
                }
            }


            if (NULL != quapnt01str)
                g_string_append(obstrn04str, quapnt01str->str);

            if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
            if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);
            if (NULL != quapnt01str) g_string_free(quapnt01str, TRUE);

            return obstrn04str;
        }
    }

    // FIXME: check if one exit point could do!!!
    return NULL;
}

static GString *OBSTRN05 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(OBSTRN05) --> CS(OBSTRN04)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //return g_string_new(";OP(----)");
    return OBSTRN04(geo);
}

static GString *OBSTRN06 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(OBSTRN06) --> CS(OBSTRN04)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //return g_string_new(";OP(----)");
    return OBSTRN04(geo);
}

static GString *OWNSHP02 (S57_geo *geo)
// Remarks:
// 1. CONNING POSITION
//    1.1 When own-ship is drawn to scale, the conning position must be correctly located in
//        relation to the ship's outline. The conning position then serves as the pivot point for
//        the own-ship symbol, to be located by the ECDIS at the correct latitude, longitude
//        for the conning point, as computed from the positioning system, correcting for
//        antenna offset.
//    1.2 In this procedure it is assumed that the heading line, beam bearing line and course
//        and speed vector originate at the conning point. If another point of origin is used,
//        for example to account for the varying position of the ship’s turning centre, this must
//        be made clear to the mariner.
//
// 2. DISPLAY OPTIONS
//    2.1 Only the ship symbol is mandatory for an ECDIS. The mariner should be prompted
//        to select from the following additional optional features:
//    - display own-ship as:
//        1. symbol, or
//        2. scaled outline
//    - select time period determining vector length for own-ship and other vessel course and speed
//      vectors, (all vectors must be for the same time period),
//    - display own-ship vector,
//    - select ground or water stabilization for all vectors, and select whether to display the type of
//      stabilization, (by arrowhead),
//    - select one-minute or six-minute vector time marks,
//    - select whether to show a heading line, to the edge of the display window,
//    - select whether to show a beam bearing line, and if so what length (default: 10mm total
//      length)

// NOTE: attribure used:
//  shpbrd: Ship's breadth (beam),
//  shplen: Ship's length over all,
//  headng: Heading,
//  cogcrs: Course over ground,
//  sogspd: Speed over ground,
//  ctwcrs: Course through water,
//  stwspd: Speed through water,

// FIXME: get conning position (where/how to get those values?)
{
    GString *ownshp02  = g_string_new("");
    GString *headngstr = S57_getAttVal(geo, "headng");
    GString *vlabelstr = S57_getAttVal(geo, "_vessel_label");

    // text label (experimental)
    if (NULL != vlabelstr) {
        //g_string_append(ownshp02, ";TX(_vessel_label,3,3,3,'15110',1,1,SHIPS,23)" );
        g_string_append(ownshp02, ";TX(_vessel_label,3,3,3,'15110',1,1,SHIPS,75)" );
    }

    // heading need specific MP (ON/OFF)
    // NOTE: first instruction before OUTLINE flag in GL
    if (NULL != headngstr)
        // draw to the edge of window
        g_string_append(ownshp02, ";LS(SOLD,1,SHIPS)");

    if (TRUE == S52_MP_get(S52_MAR_SHIPS_OUTLINE))
        // draw OWNSHP05 if length > 10 mm, else OWNSHP01 (circle)
        g_string_append(ownshp02, ";SY(OWNSHP05)");

    g_string_append(ownshp02, ";SY(OWNSHP01)");


    // course / speed vector on ground / water
    //if ((NULL!=vecperstr) && ('-'!=vecperstr->str[1])) {
    //if (NULL != vecperstr)  {
    if (0.0 != S52_MP_get(S52_MAR_VECPER))  {
        // draw line according to ships course (cogcrs or ctwcrs) and speed (sogspd or stwspd)
        g_string_append(ownshp02, ";LS(SOLD,2,SHIPS)");

        // vector stabilisation (symb place at the end of vector)
        //if (NULL != vecstbstr) {
        if (0.0 != S52_MP_get(S52_MAR_VECSTB)) {

            // none
            //if ('0' == *vecstbstr->str) { ; }

            // ground
            //if ('1' == *vecstbstr->str)
            if (1.0 == S52_MP_get(S52_MAR_VECSTB))
                g_string_append(ownshp02, ";SY(VECGND01)");

            // water
            //if ('2' == *vecstbstr->str)
            if (2.0 == S52_MP_get(S52_MAR_VECSTB))
                g_string_append(ownshp02, ";SY(VECWTR01)");

            /*
            if (1.0 == S52_MP_get(S52_MAR_VECSTB))
                // ground
                g_string_append(ownshp02, ";SY(VECGND01)");
                // water
                g_string_append(ownshp02, ";SY(VECWTR01)");
            */
        }

        // time mark (on vector)
        //if (NULL != vecmrkstr) {
        if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {
            // none
            //if ('0' == *vecmrkstr->str) { ; }

            // 6 min. and 1 min. symb.
            //if ('1' == *vecmrkstr->str)
            if (1.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(ownshp02, ";SY(OSPSIX02);SY(OSPONE02)");

            // 6 min. symb
            //if ('2' == *vecmrkstr->str)
            if (2.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(ownshp02, ";SY(OSPSIX02)");

            /*
            if (1.0 == S52_MP_get(S52_MAR_VECMRK))
                // 6 min. and 1 min. symb.
                g_string_append(ownshp02, ";SY(OSPSIX02);SY(OSPONE02)");
            else
                // 6 min. symb
                g_string_append(ownshp02, ";SY(OSPSIX02)");
            */
        }

    }

    // beam bearing
    if (0.0 != S52_MP_get(S52_MAR_BEAM_BRG_NM)) {
        g_string_append(ownshp02, ";LS(SOLD,1,SHIPS)");
    }

    return ownshp02;
}

static GString *PASTRK01 (S57_geo *geo)
// Remarks: This conditional symbology procedure was designed to allow the mariner
// to select time labels at the pasttrack (see (3) 10.5.11.1). The procedure also
// cares for the presentation of primary and secondary pasttrack.
//
// The manufacturer should define his own data class (spatial primitive) in xyt
// (position and time) in order to represent Pastrk.
{
    //PRINTF("Mariner's object not drawn\n");

    GString *pastrk01  = NULL;
    GString *catpststr = S57_getAttVal(geo, "catpst");

    if (NULL != catpststr) {
        // FIXME: view group: 1 - standard (52430) , 2 - other (52460)
        // NOTE: text grouping 51
        if ('1' == *catpststr->str)
            pastrk01  = g_string_new(";LS(SOLD,2,PSTRK);SY(PASTRK01);TX('pastrk',2,1,2,'15110',-1,-1,CHBLK,51)");

        if ('2' == *catpststr->str)
            pastrk01  = g_string_new(";LS(SOLD,1,SYTRK);SY(PASTRK02);TX('pastrk',2,1,2,'15110',-1,-1,CHBLK,51)");

    }

    return pastrk01;
}

static GString *_QUALIN01(S57_geo *geo);
static GString *QUAPOS01 (S57_geo *geo)
// Remarks: The attribute QUAPOS, which identifies low positional accuracy, is attached
// to the spatial object, not the feature object.
//
// This procedure passes the object to procedure QUALIN01 or QUAPNT01,
// which traces back to the spatial object, retrieves any QUAPOS attributes,
// and returns the appropriate symbolization to QUAPOS01.
{
    GString *quapos01 = NULL;

    if (LINES_T == S57_getObjtype(geo))
        quapos01 = _QUALIN01(geo);
    else
        quapos01 = _QUAPNT01(geo);

    return quapos01;
}

static GString *_QUALIN01(S57_geo *geo)
// Remarks: The attribute QUAPOS, which identifies low positional accuracy, is attached
// only to the spatial component(s) of an object.
//
// A line object may be composed of more than one spatial object.
//
// This procedure looks at each of the spatial
// objects, and symbolizes the line according to the positional accuracy.
{
    GString *qualino1  = NULL;
    GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
    int      quapos    = 0;
    const char *line   = NULL;

    if (NULL != quaposstr) {
        quapos = S52_atoi(quaposstr->str);
        if ( 2 <= quapos && quapos < 10)
            line = ";LC(LOWACC21)";
    } else {
        GString *objlstr = S57_getAttVal(geo, "OBJL");
        int      objl    = (NULL == objlstr)? 0 : S52_atoi(objlstr->str);

        // debug --this should not trigger an assert since
        // there is no object number zero
        g_assert(objl);

        if (COALNE == objl) {
            GString *conradstr = S57_getAttVal(geo, "CONRAD");

            if (NULL != conradstr) {
                if ('1' == *conradstr->str)
                    line = ";LS(SOLD,3,CHMGF);LS(SOLD,1,CSTLN)";
                else
                    line = ";LS(SOLD,1,CSTLN)";
            } else
                line = ";LS(SOLD,1,CSTLN)";

        } else  //LNDARE
            line = ";LS(SOLD,1,CSTLN)";
    }

    if (NULL != line)
        qualino1 = g_string_new(line);

    return qualino1;
}

static GString *_QUAPNT01(S57_geo *geo)
// Remarks: The attribute QUAPOS, which identifies low positional accuracy, is attached
// only to the spatial component(s) of an object.
//
// This procedure retrieves any QUAPOS attributes, and returns the
// appropriate symbols to the calling procedure.
{
    GString *quapnt01  = NULL;
    int      accurate  = TRUE;
    GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
    int      quapos    = (NULL == quaposstr)? 0 : S52_atoi(quaposstr->str);

    if (NULL != quaposstr) {
        if ( 2 <= quapos && quapos < 10)
            accurate = FALSE;
    }

    if (accurate)
        quapnt01 = g_string_new(";SY(LOWACC01)");

    return quapnt01;
}

static GString *SLCONS03 (S57_geo *geo)
// Remarks: Shoreline construction objects which have a QUAPOS attribute on their
// spatial component indicating that their position is unreliable are symbolized
// by a special linestyle in the place of the varied linestyles normally used.
// Otherwise this procedure applies the normal symbolization.
{
    GString *slcons03  = NULL;
    GString *valstr    = NULL;
    const char *cmdw   = NULL;   // command word
    GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
    int      quapos    = (NULL == quaposstr)? 0 : S52_atoi(quaposstr->str);

    if (POINT_T == S57_getObjtype(geo)) {
        if (NULL != quaposstr) {
            if (2 <= quapos && quapos < 10)
                cmdw =";SY(LOWACC01)";
        }
    } else {
        // LINE_T and AREA_T are the same
        if (NULL != quaposstr) {
            if (2 <= quapos && quapos < 10)
                cmdw =";LC(LOWACC01)";
        } else {
            valstr = S57_getAttVal(geo, "CONDTN");

            if (NULL != valstr && ( '1' == *valstr->str || '2' == *valstr->str))
                    cmdw = ";LS(DASH,1,CSTLN)";
            else {
                int val = 0;
                valstr  = S57_getAttVal(geo, "CATSLC");
                val     = (NULL == valstr)? 0 : S52_atoi(valstr->str);

                if (NULL != valstr && ( 6  == val || 15 == val || 16 == val ))
                        cmdw = ";LS(SOLD,4,CSTLN)";
                else {
                    valstr = S57_getAttVal(geo, "WATLEV");

                    if (NULL != valstr && '2' == *valstr->str)
                            cmdw = ";LS(SOLD,2,CSTLN)";
                    else
                        if (NULL != valstr && ('3' == *valstr->str || '4' == *valstr->str))
                            cmdw = ";LS(DASH,2,CSTLN)";
                        else
                            cmdw = ";LS(SOLD,2,CSTLN)";  // default

                }
            }
        }
    }

    // WARNING: not explicitly specified in S-52 !!
    // FIXME: this is to put AC(DEPIT) --intertidal area

    /* */
    if (AREAS_T == S57_getObjtype(geo)) {
        GString    *seabed01  = NULL;
        GString    *drval1str = S57_getAttVal(geo, "DRVAL1");
        double      drval1    = (NULL == drval1str)? -UNKNOWN : S52_atof(drval1str->str);
        GString    *drval2str = S57_getAttVal(geo, "DRVAL2");
        double      drval2    = (NULL == drval2str)? -UNKNOWN : S52_atof(drval2str->str);
        // NOTE: change sign of infinity (minus) to get out of bound in seabed01

        if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
            double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
            if ( -UNKNOWN != drval1)
                drval1 += datum;
            if ( -UNKNOWN != drval2)
                drval2 += datum;
        }

        // debug
        //PRINTF("***********drval1=%f drval2=%f \n", drval1, drval2);

        seabed01 = _SEABED01(drval1, drval2);
        if (NULL != seabed01) {
            slcons03 = g_string_new(seabed01->str);
            g_string_free(seabed01, TRUE);
        }

    }
    /* */


    if (NULL != cmdw) {
        if (NULL == slcons03)
            slcons03 = g_string_new(cmdw);
        else
            g_string_append(slcons03, cmdw);
    }

    return slcons03;
}

static GString *RESARE02 (S57_geo *geo)
// Remarks: A list-type attribute is used because an area of the object class RESARE may
// have more than one category (CATREA). For example an inshore traffic
// zone might also have fishing and anchoring prohibition and a prohibited
// area might also be a bird sanctuary or a mine field.
//
// This conditional procedure is set up to ensure that the categories of most
// importance to safe navigation are prominently symbolized, and to pass on
// all given information with minimum clutter. Only the most significant
// restriction is symbolized, and an indication of further limitations is given by
// a subscript "!" or "I". Further details are given under conditional
// symbology procedure RESTRN01
//
// Other object classes affected by attribute RESTRN are handled by
// conditional symbology procedure RESTRN01.
{
    GString *resare02         = g_string_new("");
    GString *restrnstr        = S57_getAttVal(geo, "RESTRN");
    char     restrn[LISTSIZE] = {'\0'};
    GString *catreastr        = S57_getAttVal(geo, "CATREA");
    char     catrea[LISTSIZE] = {'\0'};
    const char *symb          = NULL;
    const char *line          = NULL;
    const char *prio          = NULL;

    if ( NULL != restrnstr) {
        _parseList(restrnstr->str, restrn);

        if (NULL != catreastr) _parseList(catreastr->str, catrea);

        if (_strpbrk(restrn, "\007\010\016")) {
            // Continuation A
            if (_strpbrk(restrn, "\001\002\003\004\005\006"))
                symb = ";SY(ENTRES61)";
            else {
                if (NULL != catreastr && _strpbrk(catrea, "\001\010\011\014\016\023\025\031"))
                        symb = ";SY(ENTRES61)";
                else {
                    if (_strpbrk(restrn, "\011\012\013\014\015"))
                        symb = ";SY(ENTRES71)";
                    else {
                        if (NULL != catreastr && _strpbrk(catrea, "\004\005\006\007\012\022\024\026\027\030"))
                            symb = ";SY(ENTRES71)";
                        else
                            symb = ";SY(ENTRES51)";
                    }
                }
            }

            if (TRUE == S52_MP_get(S52_MAR_SYMBOLIZED_BND))
                line = ";LC(CTYARE51)";
            else
                line = ";LS(DASH,2,CHMGD)";

            prio = ";OP(6---)";  // display prio set to 6


        } else {
            if (_strpbrk(restrn, "\001\002")) {
                // Continuation B
                if (_strpbrk(restrn, "\003\004\005\006"))
                    symb = ";SY(ACHRES61)";
                else {
                    if (NULL != catreastr && _strpbrk(catrea, "\001\010\011\014\016\023\025\031"))
                            symb = ";SY(ACHRES61)";
                    else {
                        if (_strpbrk(restrn, "\011\012\013\014\015"))
                            symb = ";SY(ACHRES71)";
                        else {
                            if (NULL != catreastr && _strpbrk(catrea, "\004\005\006\007\012\022\024\026\027\030"))
                                symb = ";SY(ACHRES71)";
                            else
                                symb = ";SY(ACHRES51)";
                        }
                    }
                }

                if (TRUE == S52_MP_get(S52_MAR_SYMBOLIZED_BND))
                    line = ";LC(ACHRES51)";
                else
                    line = ";LS(DASH,2,CHMGD)";

                prio = ";OP(6---)";  // display prio set to 6

            } else {
                if (_strpbrk(restrn, "\003\004\005\006")) {
                    // Continuation C
                    if (NULL != catreastr && _strpbrk(catrea, "\001\010\011\014\016\023\025\031"))
                            symb = ";SY(FSHRES51)";
                    else {
                        if (_strpbrk(restrn, "\011\012\013\014\015"))
                            symb = ";SY(FSHRES71)";
                        else{
                            if (NULL != catreastr && _strpbrk(catrea, "\004\005\006\007\012\022\024\026\027\030"))
                                symb = ";SY(FSHRES71)";
                            else
                                symb = ";SY(FSHRES51)";
                        }
                    }

                    if (TRUE == S52_MP_get(S52_MAR_SYMBOLIZED_BND))
                        line = ";LC(FSHRES51)";
                    else
                        line = ";LS(DASH,2,CHMGD)";

                    prio = ";OP(6---)";  // display prio set to 6

                } else {
                    if (_strpbrk(restrn, "\011\012\013\014\015"))
                        symb = ";SY(INFARE51)";
                    else
                        symb = ";SY(RSRDEF51)";

                    if (TRUE == S52_MP_get(S52_MAR_SYMBOLIZED_BND))
                        line = ";LC(CTYARE51)";
                    else
                        line = ";LS(DASH,2,CHMGD)";

                }
            }
        }

    } else {
        // Continuation D
        if (NULL != catreastr) {
            if (_strpbrk(catrea, "\001\010\011\014\016\023\025\031")) {
                if (_strpbrk(catrea, "\004\005\006\007\012\022\024\026\027\030"))
                    symb = ";SY(CTYARE71)";
                else
                    symb = ";SY(CTYARE51)";
            } else {
                if (_strpbrk(catrea, "\004\005\006\007\012\022\024\026\027\030"))
                    symb = ";SY(INFARE71)";
                else
                    symb = ";SY(RSRDEF51)";
            }
        } else
            symb = ";SY(RSRDEF51)";

        if (TRUE == S52_MP_get(S52_MAR_SYMBOLIZED_BND))
            line = ";LC(CTYARE51)";
        else
            line = ";LS(DASH,2,CHMGD)";
    }

    // create command word
    if (NULL != prio)
        g_string_append(resare02, prio);
    g_string_append(resare02, line);
    g_string_append(resare02, symb);

    return resare02;
}

static GString *RESTRN01 (S57_geo *geo)
// Remarks: Objects subject to RESTRN01 are actually symbolised in sub-process
// RESCSP01, since the latter can also be accessed from other conditional
// symbology procedures. RESTRN01 merely acts as a "signpost" for
// RESCSP01.
//
// Object class RESARE is symbolised for the effect of attribute RESTRN in a separate
// conditional symbology procedure called RESARE02.
//
// Since many of the areas concerned cover shipping channels, the number of symbols used
// is minimised to reduce clutter. To do this, values of RESTRN are ranked for significance
// as follows:
// "Traffic Restriction" values of RESTRN:
// (1) RESTRN 7,8: entry prohibited or restricted
//     RESTRN 14: IMO designated "area to be avoided" part of a TSS
// (2) RESTRN 1,2: anchoring prohibited or restricted
// (3) RESTRN 3,4,5,6: fishing or trawling prohibited or restricted
// (4) "Other Restriction" values of RESTRN are:
//     RESTRN 9, 10: dredging prohibited or restricted,
//     RESTRN 11,12: diving prohibited or restricted,
//     RESTRN 13   : no wake area.
{
    GString *restrn01str = S57_getAttVal(geo, "RESTRN");
    GString *restrn01    = NULL;

    if (NULL != restrn01str)
        restrn01 = _RESCSP01(geo);
    else
        restrn01 = g_string_new(";OP(----)");  // return NOOP to silence error msg

    return restrn01;
}

static GString *_RESCSP01(S57_geo *geo)
// Remarks: See procedure RESTRN01
{
    GString *rescsp01         = NULL;
    GString *restrnstr        = S57_getAttVal(geo, "RESTRN");
    char     restrn[LISTSIZE] = {'\0'};   // restriction list
    const char *symb          = NULL;

    if ( NULL != restrnstr) {
        _parseList(restrnstr->str, restrn);

        if (_strpbrk(restrn, "\007\010\016")) {
            // continuation A
            if (_strpbrk(restrn, "\001\002\003\004\005\006"))
                symb = ";SY(ENTRES61)";
            else {
                if (_strpbrk(restrn, "\011\012\013\014\015"))
                    symb = ";SY(ENTRES71)";
                else
                    symb = ";SY(ENTRES51)";

            }
        } else {
            if (_strpbrk(restrn, "\001\002")) {
                // continuation B
                if (_strpbrk(restrn, "\003\004\005\006"))
                    symb = ";SY(ACHRES61)";
                else {
                    if (_strpbrk(restrn, "\011\012\013\014\015"))
                        symb = ";SY(ACHRES71)";
                    else
                        symb = ";SY(ACHRES51)";
                }


            } else {
                if (_strpbrk(restrn, "\003\004\005\006")) {
                    // continuation C
                    if (_strpbrk(restrn, "\011\012\013\014\015"))
                        symb = ";SY(FSHRES71)";
                    else
                        symb = ";SY(FSHRES51)";


                } else {
                    if (_strpbrk(restrn, "\011\012\013\014\015"))
                        symb = ";SY(INFARE51)";
                    else
                        symb = ";SY(RSRDEF51)";

                }
            }
        }

        rescsp01 = g_string_new(symb);
    }

    return rescsp01;
}

static GString *_SEABED01(double drval1, double drval2)
// Remarks: An area object that is part of the seabed is coloured as necessary according
// to the mariners selection of two shades, (shallow contour, safety contour,
// deep contour), or four shades (safety contour only). This requires a decision
// making process provided by this conditional symbology procedure. Note
// that this procedure is called as a sub-procedure by other conditional
// symbology procedures.
//
// Note: The requirement to show four depth shades is not mandatory. Also,
// the requirement to show the shallow pattern is not mandatory. However,
// both these features are strongly recommended.

// return: is never NULL

{
    GString *seabed01 = NULL;
    gboolean shallow  = TRUE;
    const char *arecol   = ";AC(DEPIT)";

    if (drval1 >= 0.0 && drval2 > 0.0)
        arecol  = ";AC(DEPVS)";

    if (TRUE == S52_MP_get(S52_MAR_TWO_SHADES)){
        if (drval1 >= S52_MP_get(S52_MAR_SAFETY_CONTOUR)  &&
            drval2 >  S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
            arecol  = ";AC(DEPDW)";
            shallow = FALSE;
        }
    } else {
        if (drval1 >= S52_MP_get(S52_MAR_SHALLOW_CONTOUR) &&
            drval2 >  S52_MP_get(S52_MAR_SHALLOW_CONTOUR))
            arecol  = ";AC(DEPMS)";

            if (drval1 >= S52_MP_get(S52_MAR_SAFETY_CONTOUR)  &&
                drval2 >  S52_MP_get(S52_MAR_SAFETY_CONTOUR)) {
                arecol  = ";AC(DEPMD)";
                shallow = FALSE;
            }

            if (drval1 >= S52_MP_get(S52_MAR_DEEP_CONTOUR)  &&
                drval2 >  S52_MP_get(S52_MAR_DEEP_CONTOUR)) {
                arecol  = ";AC(DEPDW)";
                shallow = FALSE;
            }

    }

    seabed01 = g_string_new(arecol);

    if (TRUE==S52_MP_get(S52_MAR_SHALLOW_PATTERN) && TRUE==shallow)
        g_string_append(seabed01, ";AP(DIAMOND1)");

    return seabed01;
}

static GString *SOUNDG02 (S57_geo *geo)
// Remarks: In S-57 soundings are elements of sounding arrays rather than individual
// objects. Thus this conditional symbology procedure examines each
// sounding of a sounding array one by one. To symbolize the depth values it
// calls the procedure SNDFRM02 which in turn translates the depth values
// into a set of symbols to be shown at the soundings position.
{
    guint   npt = 0;
    double *ppt = NULL;

    if (POINT_T != S57_getObjtype(geo)) {
        PRINTF("invalid object type (not POINT_T)\n");
        g_assert(0);

        return NULL;
    }

    if (FALSE==S57_getGeoData(geo, 0, &npt, &ppt)) {
        PRINTF("invalid object type (not POINT_T)\n");
        g_assert(0);
        return NULL;
    }

    if (npt > 1) {
        PRINTF("ERROR: GDAL config error, SOUNDING array instead or point\n");
        g_assert(0);
    }

    return _SNDFRM02(geo, ppt[2]);
}

static GString *_SNDFRM02(S57_geo *geo, double depth_value)
// Remarks: Soundings differ from plain text because they have to be readable under all
// circumstances and their digits are placed according to special rules. This
// conditional symbology procedure accesses a set of carefully designed
// sounding symbols provided by the symbol library and composes them to
// sounding labels. It symbolizes swept depth and it also symbolizes for low
// reliability as indicated by attributes QUASOU and QUAPOS.
{
    GString *sndfrm02         = g_string_new("");
    const char *symbol_prefix = NULL;
    GString *tecsoustr        = S57_getAttVal(geo, "TECSOU");
    char     tecsou[LISTSIZE] = {'\0'};
    GString *quasoustr        = S57_getAttVal(geo, "QUASOU");
    char     quasou[LISTSIZE] = {'\0'};
    GString *statusstr        = S57_getAttVal(geo, "STATUS");
    char     status[LISTSIZE] = {'\0'};
    double   leading_digit    = 0.0;

    // debug
    //if (7.5 == depth_value) {
    //    PRINTF("7.5 found ID:%i\n", S57_getGeoID(geo));
    //    g_string_sprintfa(sndfrm02, ";SY(BRIDGE01)");
    //}


    // FIXME: test to fix the rounding error (!?)
    depth_value  += (depth_value > 0.0)? 0.01: -0.01;
    leading_digit = (int) depth_value;
    //leading_digit = floor(depth_value);

    if (depth_value <= S52_MP_get(S52_MAR_SAFETY_DEPTH))
        symbol_prefix = "SOUNDS";
    else
        symbol_prefix = "SOUNDG";

    if (NULL != tecsoustr) {
        _parseList(tecsoustr->str, tecsou);
        if (_strpbrk(tecsou, "\006"))
            g_string_sprintfa(sndfrm02, ";SY(%sB1)", symbol_prefix);
    }

    if (NULL != quasoustr) _parseList(quasoustr->str, quasou);
    if (NULL != statusstr) _parseList(statusstr->str, status);

    if (_strpbrk(quasou, "\003\004\005\010\011") || _strpbrk(status, "\022"))
            g_string_sprintfa(sndfrm02, ";SY(%sC2)", symbol_prefix);
    else {
        GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
        int      quapos    = (NULL == quaposstr)? 0 : S52_atoi(quaposstr->str);

        if (NULL != quaposstr) {
            if (2 <= quapos && quapos < 10)
                g_string_sprintfa(sndfrm02, ";SY(%sC2)", symbol_prefix);
        }
    }

    // Continuation A
    if (depth_value < 10.0) {
        // can be above water (negative)
        int fraction = (int)ABS((depth_value - leading_digit)*10);

        g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)ABS(leading_digit));
        g_string_sprintfa(sndfrm02, ";SY(%s5%1i)", symbol_prefix, fraction);

        // above sea level (negative)
        if (depth_value < 0.0)
            g_string_sprintfa(sndfrm02, ";SY(%sA1)", symbol_prefix);

        return sndfrm02;
    }

    if (depth_value < 31.0) {
        double fraction = depth_value - leading_digit;

        if (fraction != 0.0) {
            fraction = fraction * 10;
            // FIXME: use modulus '%' instead of '/'  --check this at 100m too
            if (leading_digit >= 10.0) {
                g_string_sprintfa(sndfrm02, ";SY(%s2%1i)", symbol_prefix, (int)leading_digit/10);
                // remove tenth
                leading_digit -= floor(leading_digit/10.0) * 10.0;
            }

            g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)leading_digit);
            g_string_sprintfa(sndfrm02, ";SY(%s5%1i)", symbol_prefix, (int)fraction);

            return sndfrm02;
        }
    }

    // Continuation B
    depth_value = leading_digit;    // truncate to integer
    if (depth_value < 100.0) {
        double first_digit = floor(leading_digit / 10.0);
        double secnd_digit = floor(leading_digit - (first_digit * 10.0));

        g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)first_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s0%1i)", symbol_prefix, (int)secnd_digit);

        return sndfrm02;
    }

    if (depth_value < 1000.0) {
        double first_digit = floor((leading_digit) / 100.0);
        double secnd_digit = floor((leading_digit - (first_digit * 100.0)) / 10.0);
        double third_digit = floor( leading_digit - (first_digit * 100.0) - (secnd_digit * 10.0));

        g_string_sprintfa(sndfrm02, ";SY(%s2%1i)", symbol_prefix, (int)first_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)secnd_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s0%1i)", symbol_prefix, (int)third_digit);

        return sndfrm02;
    }

    if (depth_value < 10000.0) {
        double first_digit = floor((leading_digit) / 1000.0);
        double secnd_digit = floor((leading_digit - (first_digit * 1000.0)) / 100.0);
        double third_digit = floor((leading_digit - (first_digit * 1000.0) - (secnd_digit * 100.0)) / 10.0);
        double last_digit  = floor( leading_digit - (first_digit * 1000.0) - (secnd_digit * 100.0) - (third_digit * 10.0));

        g_string_sprintfa(sndfrm02, ";SY(%s2%1i)", symbol_prefix, (int)first_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)secnd_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s0%1i)", symbol_prefix, (int)third_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s4%1i)", symbol_prefix, (int)last_digit);

        return sndfrm02;
    }

    // Continuation C
    {
        double first_digit  = floor((leading_digit) / 10000.0);
        double secnd_digit  = floor((leading_digit - (first_digit * 10000.0)) / 1000.0);
        double third_digit  = floor((leading_digit - (first_digit * 10000.0) - (secnd_digit * 1000.0)) / 100.0);
        double fourth_digit = floor((leading_digit - (first_digit * 10000.0) - (secnd_digit * 1000.0) - (third_digit * 100.0)) / 10.0);
        double last_digit   = floor( leading_digit - (first_digit * 10000.0) - (secnd_digit * 1000.0) - (third_digit * 100.0) - (fourth_digit * 10.0));

        g_string_sprintfa(sndfrm02, ";SY(%s3%1i)", symbol_prefix, (int)first_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s2%1i)", symbol_prefix, (int)secnd_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s1%1i)", symbol_prefix, (int)third_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s0%1i)", symbol_prefix, (int)fourth_digit);
        g_string_sprintfa(sndfrm02, ";SY(%s4%1i)", symbol_prefix, (int)last_digit);

        return sndfrm02;
    }

    return sndfrm02;
}

static GString *TOPMAR01 (S57_geo *geo)
// Remarks: Topmark objects are to be symbolized through consideration of their
// platforms e.g. a buoy. Therefore this conditional symbology procedure
// searches for platforms by looking for other objects that are located at the
// same position.. Based on the finding whether the platform is rigid or
// floating, the respective upright or sloping symbol is selected and presented
// at the objects location. Buoy symbols and topmark symbols have been
// carefully designed to fit to each other when combined at the same position.
// The result is a composed symbol that looks like the traditional symbols the
// mariner is used to.
{
    // NOTE: This CS fall on layer 0 (NODATA) for LUPT TOPMAR SIMPLIFIED POINT and no INST
    // (hence can't land here - nothing rendered.)
    // Only LUPT TOPMAR PAPER_CHART use this CS

    GString *topshpstr = S57_getAttVal(geo, "TOPSHP");
    GString *topmar    = NULL;
    const char *sy     = NULL;

    if (NULL == topshpstr)
        sy = ";SY(QUESMRK1)";
    else {
        int floating    = FALSE; // not a floating platform
        int topshp      = (NULL==topshpstr) ? 0 : S52_atoi(topshpstr->str);

        S57_geo *other  = S57_getTouchTOPMAR(geo);

        // S-52 BUG: topmar01 differ from floating object to
        // rigid object. Since it default to rigid object, there
        // is no need to test for that!

        //if (TRUE == _atPtPos(geo, FLOATLIST))
        if (NULL != other)
            floating = TRUE;
        //else
            // FIXME: this test is wierd since it doesn't affect 'floating'
            //if (TRUE == _atPtPos(geo, RIGIDLIST))
            //    floating = FALSE;


        if (floating) {
            // floating platform
            switch (topshp) {
                case 1 : sy = ";SY(TOPMAR02)"; break;
                case 2 : sy = ";SY(TOPMAR04)"; break;
                case 3 : sy = ";SY(TOPMAR10)"; break;
                case 4 : sy = ";SY(TOPMAR12)"; break;

                case 5 : sy = ";SY(TOPMAR13)"; break;
                case 6 : sy = ";SY(TOPMAR14)"; break;
                case 7 : sy = ";SY(TOPMAR65)"; break;
                case 8 : sy = ";SY(TOPMAR17)"; break;

                case 9 : sy = ";SY(TOPMAR16)"; break;
                case 10: sy = ";SY(TOPMAR08)"; break;
                case 11: sy = ";SY(TOPMAR07)"; break;
                case 12: sy = ";SY(TOPMAR14)"; break;

                case 13: sy = ";SY(TOPMAR05)"; break;
                case 14: sy = ";SY(TOPMAR06)"; break;
                case 17: sy = ";SY(TMARDEF2)"; break;
                case 18: sy = ";SY(TOPMAR10)"; break;

                case 19: sy = ";SY(TOPMAR13)"; break;
                case 20: sy = ";SY(TOPMAR14)"; break;
                case 21: sy = ";SY(TOPMAR13)"; break;
                case 22: sy = ";SY(TOPMAR14)"; break;

                case 23: sy = ";SY(TOPMAR14)"; break;
                case 24: sy = ";SY(TOPMAR02)"; break;
                case 25: sy = ";SY(TOPMAR04)"; break;
                case 26: sy = ";SY(TOPMAR10)"; break;

                case 27: sy = ";SY(TOPMAR17)"; break;
                case 28: sy = ";SY(TOPMAR18)"; break;
                case 29: sy = ";SY(TOPMAR02)"; break;
                case 30: sy = ";SY(TOPMAR17)"; break;

                case 31: sy = ";SY(TOPMAR14)"; break;
                case 32: sy = ";SY(TOPMAR10)"; break;
                case 33: sy = ";SY(TMARDEF2)"; break;
                default: sy = ";SY(TMARDEF2)"; break;
            }
        } else {
            // not a floating platform
            switch (topshp) {
                case 1 : sy = ";SY(TOPMAR22)"; break;
                case 2 : sy = ";SY(TOPMAR24)"; break;
                case 3 : sy = ";SY(TOPMAR30)"; break;
                case 4 : sy = ";SY(TOPMAR32)"; break;

                case 5 : sy = ";SY(TOPMAR33)"; break;
                case 6 : sy = ";SY(TOPMAR34)"; break;
                case 7 : sy = ";SY(TOPMAR85)"; break;
                case 8 : sy = ";SY(TOPMAR86)"; break;

                case 9 : sy = ";SY(TOPMAR36)"; break;
                case 10: sy = ";SY(TOPMAR28)"; break;
                case 11: sy = ";SY(TOPMAR27)"; break;
                case 12: sy = ";SY(TOPMAR14)"; break;

                case 13: sy = ";SY(TOPMAR25)"; break;
                case 14: sy = ";SY(TOPMAR26)"; break;
                case 15: sy = ";SY(TOPMAR88)"; break;
                case 16: sy = ";SY(TOPMAR87)"; break;

                case 17: sy = ";SY(TMARDEF1)"; break;
                case 18: sy = ";SY(TOPMAR30)"; break;
                case 19: sy = ";SY(TOPMAR33)"; break;
                case 20: sy = ";SY(TOPMAR34)"; break;

                case 21: sy = ";SY(TOPMAR33)"; break;
                case 22: sy = ";SY(TOPMAR34)"; break;
                case 23: sy = ";SY(TOPMAR34)"; break;
                case 24: sy = ";SY(TOPMAR22)"; break;

                case 25: sy = ";SY(TOPMAR24)"; break;
                case 26: sy = ";SY(TOPMAR30)"; break;
                case 27: sy = ";SY(TOPMAR86)"; break;
                case 28: sy = ";SY(TOPMAR89)"; break;

                case 29: sy = ";SY(TOPMAR22)"; break;
                case 30: sy = ";SY(TOPMAR86)"; break;
                case 31: sy = ";SY(TOPMAR14)"; break;
                case 32: sy = ";SY(TOPMAR30)"; break;
                case 33: sy = ";SY(TMARDEF1)"; break;
                default: sy = ";SY(TMARDEF1)"; break;
            }
        }

    }

    topmar = g_string_new(sy);

    return topmar;
}

static GString *_UDWHAZ03(S57_geo *geo, double depth_value)
// Remarks: Obstructions or isolated underwater dangers of depths less than the safety
// contour which lie within the safe waters defined by the safety contour are
// to be presented by a specific isolated danger symbol as hazardous objects
// and put in IMO category DISPLAYBASE (see (3), App.2, 1.3). This task
// is performed by this conditional symbology procedure.
{
    GString *udwhaz03str    = NULL;
    int      danger         = FALSE;
    double   safety_contour = S52_MP_get(S52_MAR_SAFETY_CONTOUR);

    // first reset original scamin
    S57_resetScamin(geo);

    if (UNKNOWN!=depth_value && depth_value<=safety_contour) {
        // that intersect this point/line/area for OBSTRN04
        // that intersect this point/area      for WRECKS02
        //S57_geo *geoTmp = geo;
        S57_geo *geoTmp = S57_getTouchDEPARE(geo);

        //  collect area/line DEPARE & area DRGARE that touche this point/line/area
        //S57_ogrTouche(geoTmp, N_OBJ_T);
        //while (NULL != (geoTmp = S57_nextObj(geoTmp))) {

            if (LINES_T == S57_getObjtype(geoTmp)) {
                GString *drval2str = S57_getAttVal(geoTmp, "DRVAL2");
                //double   drval2    = (NULL == drval2str) ? 0.0 : S52_atof(drval2str->str);
                double   drval2    = (NULL == drval2str) ? UNKNOWN : S52_atof(drval2str->str);

                if (NULL == drval2str)
                    return NULL;

                if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                    double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
                    drval2 += datum;
                }

                if (drval2 > safety_contour) {
                    danger = TRUE;
                    //break;
                }

            } else {
                // area DEPARE or DRGARE
                GString *drval1str = S57_getAttVal(geoTmp, "DRVAL1");
                //double   drval1    = (NULL == drval1str) ? 0.0 : S52_atof(drval1str->str);
                double   drval1    = (NULL == drval1str) ? UNKNOWN : S52_atof(drval1str->str);

                if (NULL == drval1str)
                    return NULL;

                if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                    double datum = S52_MP_get(S52_MAR_DATUM_OFFSET);
                    drval1 += datum;
                }

                if (drval1 >= safety_contour) {
                    danger = TRUE;
                    //break;
                }
            }

        //}
        //S57_unlinkObj(geo);

        //danger = TRUE;   // true
        if (TRUE == danger) {
            GString *watlevstr = S57_getAttVal(geo, "WATLEV");
            if (NULL != watlevstr && ('1' == *watlevstr->str || '2' == *watlevstr->str))
                udwhaz03str = g_string_new(";OP(--D14050");
            else {
                udwhaz03str = g_string_new(";OP(8OD14010);SY(ISODGR01)");
                S57_setAtt(geo, "SCAMIN", "INFINITY");
                // can't set Att to no value
                //S57_setAtt(geo, "SCAMIN", NULL);
            }
        }
    }

    return udwhaz03str;
}

static GString *VESSEL01 (S57_geo *geo)
// Remarks: The mariner should be prompted to select from the following options:
// - ARPA target or AIS report (overall decision or vessel by vessel) (vesrce)
// - *time-period determining vector-length for all vectors (vecper)
// - whether to show a vector (overall or vessel by vessel) (vestat)
// - *whether to symbolize vector stabilization (vecstb)
// - *whether to show one-minute or six-minute vector time marks (vecmrk)
// - whether to show heading line on AIS vessel reports (headng)
// * Note that the same vector parameters should be used for own-ship and all vessel
// vectors.
{
    GString *vessel01  = g_string_new("");
    GString *vesrcestr = S57_getAttVal(geo, "vesrce");
    //GString *vecperstr = S57_getAttVal(geo, "vecper");
    //GString *vecmrkstr = S57_getAttVal(geo, "vecmrk");
    //GString *vecstbstr = S57_getAttVal(geo, "vecstb");
    GString *vlabelstr = S57_getAttVal(geo, "_vessel_label");

    // text label (experimental)
    if (NULL != vlabelstr) {
        //g_string_append(vessel01, ";TE('%s','_vessel_label',3,3,3,'15110',3,1,ARPAT,23)" );
        //g_string_append(vessel01, ";TX(_vessel_label,3,3,3,'15110',3,1,ARPAT,23)" );
        //g_string_append(vessel01, ";TX(_vessel_label,3,3,3,'15110',1,1,ARPAT,23)" );
        g_string_append(vessel01, ";TX(_vessel_label,3,3,3,'15110',1,1,ARPAT,76)" );
    }

#ifdef S52_USE_SYM_AISSEL01
    // experimental: put seclected symbol on taget
    g_string_append(vessel01, ";SY(AISSEL01)");
#endif

    // add the symbols ground and water arrow right now
    // and draw only the proper one when the user enter the 'vecstb'
    g_string_append(vessel01, ";SY(VECGND21);SY(VECWTR21)");

    // experimental: AIS draw ship's silhouettte (OWNSHP05) if length > 10 mm
    if (TRUE == S52_MP_get(S52_MAR_SHIPS_OUTLINE) && (NULL!=vesrcestr && '2'==*vesrcestr->str))
        g_string_append(vessel01, ";SY(OWNSHP05)");

    // ARPA
    if (NULL!=vesrcestr && '1'==*vesrcestr->str) {
        g_string_append(vessel01, ";SY(ARPATG01)");

        // add time mark (on ARPA vector)
        //if (NULL!=vecmrkstr && '0'!=*vecmrkstr->str) {
        //if (NULL != vecmrkstr) {
        if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {

            // none - do nothing
            //if ('0' == *vecmrkstr->str) { ; }

            // 6 min. and 1 min. symb.
            //if ('1' == *vecmrkstr->str)
            if (1.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(vessel01, ";SY(ARPSIX01);SY(ARPONE01)");

            // 6 min. symb
            //if ('2' == *vecmrkstr->str)
            if (2.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(vessel01, ";SY(ARPSIX01)");
        }

        // add time mark (on vector) ARPA
        //if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {
        //    if (1.0 == S52_MP_get(S52_MAR_VECMRK))
        //        // 6 min. and 1 min. symb.
        //        g_string_append(vessel01, ";SY(ARPSIX01);SY(ARPONE01)");
        //    else
        //        // 6 min. symb
        //        g_string_append(vessel01, ";SY(ARPSIX01)");
        //}
    }

    // AIS
    if (NULL!=vesrcestr && '2'==*vesrcestr->str) {
        //GString *vestatstr = S57_getAttVal(geo, "vestat");
        //GString *headngstr = S57_getAttVal(geo, "headng");

        // 1. Option to show vessel symbol only:
        // no heading
        //if (NULL==headngstr) {
            g_string_append(vessel01, ";SY(AISDEF01)");
        //    return vessel01;
        //}

        // sleeping
        //if (NULL!=vestatstr && '2'==*vestatstr->str) {
            g_string_append(vessel01, ";SY(AISSLP01)");
            //return vessel01;
        //}

        // active
        //if (NULL!=vestatstr && '1'==*vestatstr->str) {
            g_string_append(vessel01, ";SY(AISVES01)");
        //}

        // AIS only: add heading line (50 mm)
        //if (1.0 == S52_MP_get(S52_MAR_HEADNG_LINE)) {
        if (TRUE == S52_MP_get(S52_MAR_HEADNG_LINE)) {
            g_string_append(vessel01, ";LS(SOLD,1,ARPAT)");
        }

        // add time mark (on AIS vector)
        //if (NULL!=vecmrkstr && '0'!=*vecmrkstr->str) {
        //if (NULL != vecmrkstr) {
        if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {

            // none - do nothing
            //if ('0' == *vecmrkstr->str) { ; }

            // 6 min. and 1 min. symb
            //if ('1' == *vecmrkstr->str)
            if (1.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(vessel01, ";SY(AISSIX01);SY(AISONE01)");

            // 6 min. symb
            //if ('2' == *vecmrkstr->str)
            if (2.0 == S52_MP_get(S52_MAR_VECMRK))
                g_string_append(vessel01, ";SY(AISSIX01)");



        }


        // add time mark (on AIS vector)
        //if (0.0 != S52_MP_get(S52_MAR_VECMRK)) {
        //    //if ('1' == *vecmrkstr->str)
        //    if (1.0 == S52_MP_get(S52_MAR_VECMRK))
        //        // 6 min. and 1 min. symb.
        //        g_string_append(vessel01, ";SY(AISSIX01);SY(AISONE01)");
        //    else
        //        // 6 min. symb
        //        g_string_append(vessel01, ";SY(AISSIX01)");
        //}
    }

    // ARPA & AIS - course and speed vector
    if (0.0 != S52_MP_get(S52_MAR_VECPER)) {
    //if (NULL!=vecperstr && '0'!=*vecperstr->str) {
        //GString *vecstbstr = S57_getAttVal(geo, "vecstb");

        // BUG: AIS: leave a gap (size AISSIX01) at every minute mark
        g_string_append(vessel01, ";LS(SOLD,2,ARPAT)");


        // add vector stabilisation symbol
        if (0.0 != S52_MP_get(S52_MAR_VECSTB)) {
            // ground
            if (1.0 == S52_MP_get(S52_MAR_VECSTB))
                g_string_append(vessel01, ";SY(VECGND21)");

            // water
            if (2.0 == S52_MP_get(S52_MAR_VECSTB))
                g_string_append(vessel01, ";SY(VECWTR21)");
        }

        /*
        // add vector stabilisation symbol
        // ground
        if (NULL!=vecstbstr && '1'==*vecstbstr->str) {
            g_string_append(vessel01, ";SY(VECGND21)");
        }

        // water
        if (NULL!=vecstbstr && '2'==*vecstbstr->str) {
            g_string_append(vessel01, ";SY(VECWTR21)");
        }
        */
    }

    // VTS
    if (NULL!=vesrcestr && '3'==*vesrcestr->str) {
        // FIXME: S52 say to use 'vesrce' but this attrib can have the
        // value 3, witch has no LUP or CS !
        PRINTF("WARNING: no specfic rendering rule for VTS report (vesrce=3)\n");
        g_assert(0);
        return vessel01;
    }

    return vessel01;
}

static GString *VRMEBL01 (S57_geo *geo)
// Remarks: This conditional symbology procedure symbolizes the three cases of range
// circle, bearing line and range/bearing line. VRM's and EBL's can be ship-centred
// or freely movable, and two line-styles are available
{
    GString *vrmebl01str = g_string_new("");

    // freely movable origine symb (a dot)
    GString *ownshpcenteredstr = S57_getAttVal(geo, "_setOrigin");
    if (NULL!=ownshpcenteredstr && ('Y'==*ownshpcenteredstr->str || 'I'==*ownshpcenteredstr->str))
        g_string_append(vrmebl01str, ";SY(EBLVRM11)");

    // line style
    GString *normallinestylestr = S57_getAttVal(geo, "_normallinestyle");
    if (NULL!=normallinestylestr && 'Y'==*normallinestylestr->str)
        g_string_append(vrmebl01str, ";LC(ERBLNA01)");
    else
        g_string_append(vrmebl01str, ";LC(ERBLNB01)");

    // symb range marker
    GString *symbrngmrkstr = S57_getAttVal(geo, "_symbrngmrk");
    if (NULL!=symbrngmrkstr && 'Y'==*symbrngmrkstr->str)
        g_string_append(vrmebl01str, ";SY(ERBLTIK1)");
    else
        g_string_append(vrmebl01str, ";AC(CURSR)");

    // EXPERIMENTAL: add text, bearing & range
    g_string_append(vrmebl01str, ";TX(_vrmebl_label,3,3,3,'15110',1,1,CURSR,77)");

    return vrmebl01str;
}

static GString *WRECKS02 (S57_geo *geo)
// Remarks: Wrecks of depths less than the safety contour which lie within the safe waters
// defined by the safety contour are to be presented by a specific isolated
// danger symbol and put in IMO category DISPLAYBASE (see (3), App.2,
// 1.3). This task is performed by the sub-procedure "UDWHAZ03" which is
// called by this symbology procedure.
{
    GString *wrecks02str = NULL;
    GString *sndfrm02str = NULL;
    GString *udwhaz03str = NULL;
    GString *quapnt01str = NULL;
    double   least_depth = UNKNOWN;
    double   depth_value = UNKNOWN;
    GString *valsoustr   = S57_getAttVal(geo, "VALSOU");
    double   valsou      = UNKNOWN;

    // debug
    //GString *FIDNstr = S57_getAttVal(geo, "FIDN");
    //if (0==strcmp("2135161787", FIDNstr->str)) {
    //    PRINTF("%s\n",FIDNstr->str);
    //}

    if (NULL != valsoustr) {
        valsou      = S52_atof(valsoustr->str);
        depth_value = valsou;
        sndfrm02str = _SNDFRM02(geo, depth_value);
    } else {
        if (AREAS_T == S57_getObjtype(geo))
            least_depth = _DEPVAL01(geo, least_depth);

        if (least_depth == UNKNOWN) {
            // WARNING: ambiguity removed in WRECKS03 (see update in C&S_MD2.PDF)
            GString *watlevstr = S57_getAttVal(geo, "WATLEV");
            GString *catwrkstr = S57_getAttVal(geo, "CATWRK");

            // S52 BUG: negative depth
            // FIX: change to positive
            if (NULL == watlevstr) // default (missing)
                depth_value = 15.0;
            else {
                // incidentaly EMPTY_NUMBER_MARKER str start with a '2' and
                // have the same value as the case '2'
                //if (0==strcmp(watlevstr->str, EMPTY_NUMBER_MARKER)) {
                //    PRINTF("FIXME: WATLEV att with no value\n");
                //    depth_value = 15.0;
                //}

                switch (*watlevstr->str) { // ambiguous
                    case '1':
                    //case '2': depth_value = -15.0 ; break;
                    case '2': depth_value =  15.0 ; break;
                    case '3': depth_value =   0.01; break;
                    //case '4': depth_value = -15.0 ; break;
                    case '4': depth_value =  15.0 ; break;
                    case '5': depth_value =   0.0 ; break;
                    //case '6': depth_value = -15.0 ; break;
                    case '6': depth_value =  15.0 ; break;
                    //default :{
                    //     if (NULL != catwrkstr) {
                    //        switch (*catwrkstr->str) {
                    //            case '1': depth_value =  20.0; break;
                    //            case '2': depth_value =   0.0; break;
                    //            case '4':
                    //            case '5': depth_value = -15.0; break;
                    //        }
                    //    }
                    //}
                }

                if (NULL != catwrkstr) {
                    switch (*catwrkstr->str) {
                        case '1': depth_value =  20.0; break;
                        case '2': depth_value =   0.0; break;
                        case '4':
                        //case '5': depth_value = -15.0; break;
                        case '5': depth_value =  15.0; break;
                    }
                }
            }
        } else
            depth_value = least_depth;
    }

    udwhaz03str = _UDWHAZ03(geo, depth_value);
    quapnt01str = _QUAPNT01(geo);

    if (POINT_T == S57_getObjtype(geo)) {
        if (NULL != udwhaz03str) {
            wrecks02str = g_string_new(udwhaz03str->str);

            if (NULL != quapnt01str)
                g_string_append(wrecks02str, quapnt01str->str);

        } else {
            // Continuation A (POINT_T)
            if (UNKNOWN != valsou) {

                if (valsou <= 20.0) {
                    wrecks02str = g_string_new(";SY(DANGER01)");
                    if (NULL != sndfrm02str)
                        g_string_append(wrecks02str, sndfrm02str->str);

                } else
                    wrecks02str = g_string_new(";SY(DANGER02)");

                if (NULL != udwhaz03str)
                    g_string_append(wrecks02str, udwhaz03str->str);
                if (NULL != quapnt01str)
                    g_string_append(wrecks02str, quapnt01str->str);

            } else {
                const char *sym    = NULL;
                GString *catwrkstr = S57_getAttVal(geo, "CATWRK");
                GString *watlevstr = S57_getAttVal(geo, "WATLEV");

                if (NULL!=catwrkstr && NULL!=watlevstr) {
                    if ('1'==*catwrkstr->str && '3'==*watlevstr->str)
                        sym =";SY(WRECKS04)";
                    else
                        if ('2'==*catwrkstr->str && '3'==*watlevstr->str)
                            sym = ";SY(WRECKS05)";
                } else {
                    if (NULL!=catwrkstr && ('4' == *catwrkstr->str || '5' == *catwrkstr->str)) {
                        sym = ";SY(WRECKS01)";
                    } else {
                        if (NULL != watlevstr) {
                            if ('1' == *watlevstr->str ||
                                '2' == *watlevstr->str ||
                                '5' == *watlevstr->str ||
                                '4' == *watlevstr->str )
                                sym = ";SY(WRECKS01)";
                        } else
                            sym = ";SY(WRECKS05)"; // default

                    }
                }

                wrecks02str = g_string_new(sym);
                if (NULL != quapnt01str)
                    g_string_append(wrecks02str, quapnt01str->str);

            }

        }


    } else {
        // Continuation B (AREAS_T)
        GString *quaposstr = S57_getAttVal(geo, "QUAPOS");
        int      quapos    = (NULL == quaposstr)? 0 : S52_atoi(quaposstr->str);
        const char *line   = NULL;

        if (2 <= quapos && quapos < 10)
            line = ";LC(LOWACC41)";
        else {
            if ( NULL != udwhaz03str)
                line = ";LS(DOTT,2,CHBLK)";
            else {
                 if (UNKNOWN != valsou){
                     if (valsou <= 20)
                         line = ";LS(DOTT,2,CHBLK)";
                     else
                         line = ";LS(DASH,2,CHBLK)";
                 } else {
                     GString  *watlevstr = S57_getAttVal(geo, "WATLEV");

                     if (NULL == watlevstr)
                         line = ";LS(DOTT,2,CSTLN)";
                     else {
                         switch (*watlevstr->str){
                             case '1':
                             case '2': line = ";LS(SOLD,2,CSTLN)"; break;
                             case '4': line = ";LS(DASH,2,CSTLN)"; break;
                             case '3':
                             case '5':

                             default : line = ";LS(DOTT,2,CSTLN)"; break;
                         }
                     }

                 }
            }
        }
        wrecks02str = g_string_new(line);

        if (UNKNOWN != valsou) {
            if (valsou <= 20) {
                if (NULL != udwhaz03str)
                    g_string_append(wrecks02str, udwhaz03str->str);

                if (NULL != quapnt01str)
                    g_string_append(wrecks02str, quapnt01str->str);

                if (NULL != sndfrm02str)
                    g_string_append(wrecks02str, sndfrm02str->str);

            } else {
                // NOTE: ??? same as above ???
                if (NULL != udwhaz03str)
                    g_string_append(wrecks02str, udwhaz03str->str);

                if (NULL != quapnt01str)
                    g_string_append(wrecks02str, quapnt01str->str);
            }
        } else {
            const char *ac     = NULL;
            GString *watlevstr = S57_getAttVal(geo, "WATLEV");

            if (NULL == watlevstr)
                ac = ";AC(DEPVS)";
            else {
                switch (*watlevstr->str) {
                    case '1':
                    case '2': ac = ";AC(CHBRN)"; break;
                    case '4': ac = ";AC(DEPIT)"; break;
                    case '5':
                    case '3':
                    default : ac = ";AC(DEPVS)"; break;
                }
            }
            g_string_append(wrecks02str, ac);

            if (NULL != udwhaz03str)
                g_string_append(wrecks02str, udwhaz03str->str);

            if (NULL != quapnt01str)
                g_string_append(wrecks02str, quapnt01str->str);
        }
    }

    if (NULL != sndfrm02str) g_string_free(sndfrm02str, TRUE);
    if (NULL != udwhaz03str) g_string_free(udwhaz03str, TRUE);
    if (NULL != quapnt01str) g_string_free(quapnt01str, TRUE);

    return wrecks02str;
}

static GString *WRECKS03 (S57_geo *geo)
{
    static int silent = FALSE;

    if (FALSE == silent) {
        PRINTF("FIXME: CS(WRECKS03) --> CS(WRECKS02)\n");
        PRINTF("       (this msg will not repeat)\n");
        silent = TRUE;
    }

    //return g_string_new(";OP(----)");
    return WRECKS02(geo);
}

static GString *QUESMRK1 (S57_geo *geo)
// this is a catch all, the LUP link to unknown CS
{
    GString   *err = NULL;
    //S57_Obj_t  ot  = S57_getObjtype(geo);
    S52_Obj_t  ot  = S57_getObjtype(geo);

    switch (ot) {
        case POINT_T: err = g_string_new(";SY(QUESMRK1)"); break;
        case LINES_T: err = g_string_new(";LC(QUESMRK1)"); break;
        case AREAS_T: err = g_string_new(";AP(QUESMRK1)"); break;
        default:
            PRINTF("WARNING: unknown S57 object type for CS(QUESMRK1)\n");
    }

    return err;
}

/*

Mariner Parameter           used in CS (via CS)

S52_MAR_DEEP_CONTOUR        _SEABED01(via DEPARE01);
S52_MAR_SAFETY_CONTOUR      DEPCNT02; _SEABED01(via DEPARE01); _UDWHAZ03(via OBSTRN04, WRECKS02);
S52_MAR_SAFETY_DEPTH        _SNDFRM02(via OBSTRN04, WRECKS02);
S52_MAR_SHALLOW_CONTOUR     _SEABED01(via DEPARE01);
S52_MAR_SHALLOW_PATTERN     _SEABED01(via DEPARE01);
S52_MAR_SHOW_TEXT           DEPCNT02; LIGHTS05;
S52_MAR_SYMBOLIZED_BND      RESARE02;
S52_MAR_TWO_SHADES          _SEABED01(via DEPARE01);


//CS          called by S57 objects
DEPARE01  <-  DEPARE DRGARE
DEPCNT02  <-  DEPARE DEPCNT
LIGHTS05  <-  LIGHTS
OBSTRN04  <-  OBSTRN UWTROC
RESARE02  <-  RESARE
WRECKS02  <-  WRECKS


//-----------------------------------
S57_getTouch() is called by:

DEPCNT02
_DEPVAL01 <-  OBSTRN04, WRECKS02
LIGHTS05
TOPMAR01
_UDWHAZ03 <-  OBSTRN04, WRECKS02

*/


//--------------------------------
//
// JUMP TABLE SECTION
//
//--------------------------------


S52_CS_condSymb S52_CS_condTable[] = {
   // name      call            Sub-Procedure
   {"CLRLIN01", CLRLIN01},   //
   {"DATCVR01", DATCVR01},   //
   {"DATCVR02", DATCVR02},   // ????
   {"DEPARE01", DEPARE01},   // _RESCSP01, _SEABED01
   {"DEPARE02", DEPARE02},   // ????
   {"DEPCNT02", DEPCNT02},   //
   {"DEPCNT03", DEPCNT03},   // ????
   {"LEGLIN02", LEGLIN02},   //
   {"LEGLIN03", LEGLIN03},   // ????
   {"LIGHTS05", LIGHTS05},   // _LITDSN01
   {"OBSTRN04", OBSTRN04},   // _DEPVAL01, _QUAPNT01, _SNDFRM02, _UDWHAZ03
   {"OBSTRN05", OBSTRN05},   // ????
   {"OBSTRN06", OBSTRN06},   // ????
   {"OWNSHP02", OWNSHP02},   //
   {"PASTRK01", PASTRK01},   //
   {"QUAPOS01", QUAPOS01},   // _QUALIN01, _QUAPNT01
   {"RESARE02", RESARE02},   //
   {"RESTRN01", RESTRN01},   // _RESCSP01
   {"SLCONS03", SLCONS03},   //
   {"SOUNDG02", SOUNDG02},   // _SNDFRM02
   {"TOPMAR01", TOPMAR01},   //
   {"VESSEL01", VESSEL01},   //
   {"VRMEBL01", VRMEBL01},   //
   {"WRECKS02", WRECKS02},   // _DEPVAL01, _QUAPNT01, _SNDFRM02, _UDWHAZ03
   {"WRECKS03", WRECKS03},   // ????

   {"QUESMRK1", QUESMRK1},
   {"########", NULL}
};

