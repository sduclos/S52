// S57data.h: interface to S57 geo data
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2012  Sylvain Duclos sduclos@users.sourceforgue.net

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



#ifndef _S57DATA_H_
#define _S57DATA_H_

#include "S52utils.h"

#include <glib.h>       // guint, GArray, GData, GString

// internal geo enum used to link S52 to S57 geo
// S57 object type have a PLib enum: P,L,A
typedef enum S52_Obj_t {
//typedef enum S57_Obj_t {
    _META_T  =  0 ,       // meta geo stuff (ex: C_AGGR)
    POINT_T  = 'P',       // 80 (point)
    LINES_T  = 'L',       // 76 (line)
    AREAS_T  = 'A',       // 65 (area)
    N_OBJ_T  =  4         // number of object type
} S52_Obj_t;
//} S57_Obj_t;

typedef double geocoord;
typedef struct _S57_geo  S57_geo;
typedef struct _S57_prim S57_prim;

extern int       S57_doneData(S57_geo *geoData, gpointer user_data);

extern S57_geo  *S57_setPOINT(geocoord *xyz);
extern S57_geo  *S57_setLINES(guint xyznbr, geocoord *xyz);
//extern S57_geo  *S57_setMLINE(guint linenbr, guint *linexyznbr, geocoord **linexyz);
extern S57_geo  *S57_setAREAS(guint ringnbr, guint *ringxyznbr, geocoord **ringxyz);
extern S57_geo  *S57_set_META();

// used for PASTRK
extern S57_geo  *S57_setGeoLine(S57_geo *geoData, guint xyznbr, geocoord *xyz);


//extern S57_geo  *S57_getGeoNext(S57_geo *geoData);
//extern S57_geo  *S57_setGeoNext(S57_geo *geoData, S57_geo *next);
extern S57_geo  *S57_getGeoLink(S57_geo *geoData);
extern S57_geo  *S57_setGeoLink(S57_geo *geoData, S57_geo *link);

#ifdef S52_USE_WORLD
extern S57_geo  *S57_setNextPoly(S57_geo *geoData, S57_geo *nextPoly);
extern S57_geo  *S57_getNextPoly(S57_geo *geoData);
extern S57_geo  *S57_delNextPoly(S57_geo *geoData);
#endif

extern int       S57_setName  (S57_geo *geoData, const char *name);
extern char     *S57_getName  (S57_geo *geoData);

// debug:
//extern int       S57_setOGRGeo(S57_geo *geoData, void *hGeom);
//extern void     *S57_getOGRGeo(S57_geo *geoData);

// get the number of rings
extern guint     S57_getRingNbr(S57_geo *geoData);
// get data
extern int       S57_getGeoData(S57_geo *geoData, guint ringNo, guint *npt, double **ppt);

// handling of S52/S57 object rendering primitive
extern S57_prim *S57_initPrim     (S57_prim *prim);
extern S57_prim *S57_donePrim     (S57_prim *prim);
extern S57_prim *S57_initPrimGeo  (S57_geo  *geoData);
extern S57_geo  *S57_donePrimGeo  (S57_geo  *geoData);
extern int       S57_begPrim      (S57_prim *prim, int mode);
//extern int       S57_begPrim      (S57_prim *prim, int mode, int cIdx);

// GLES2 need float vertex
#ifdef S52_USE_GLES2
typedef float  vertex_t;
#else
typedef double vertex_t;
#endif
extern int       S57_addPrimVertex(S57_prim *prim, vertex_t *ptr);

extern int       S57_endPrim      (S57_prim *prim);
extern S57_prim *S57_getPrimGeo   (S57_geo  *geoData);
//extern guint     S57_getPrimData  (S57_prim *prim, int *primNbr, double **vertex);
//extern guint     S57_getPrimData  (S57_prim *prim, int *primNbr, double **vert, int *vertNbr);
//extern guint     S57_getPrimData  (S57_prim *prim, int *primNbr, vertex_t **vert, int *vertNbr);
extern guint     S57_getPrimData  (S57_prim *prim, guint *primNbr, vertex_t **vert, guint *vertNbr, guint *vboID);
extern GArray   *S57_getPrimVertex(S57_prim *prim);
extern int       S57_getPrimIdx   (S57_prim *prim, unsigned int i, int *mode, int *first, int *count);
//extern int       S57_getPrimIdx   (S57_prim *prim, unsigned int i, int *mode, int *first, int *count, int *cIdx);
extern S57_prim *S57_setPrimSize  (S57_prim *prim, int sz);
extern int       S57_setPrimDList (S57_prim *prim, guint DList);

// get/set extend
extern int       S57_setExt   (S57_geo *geoData, double  x1, double  y1, double  x2, double  y2);
extern int       S57_getExt   (S57_geo *geoData, double *x1, double *y1, double *x2, double *y2);
//extern int       S57_getExtPRJ(S57_geo *geoData, double *x1, double *y1, double *x2, double *y2);

// get geo type (P,L,A) of this object
// Note: return the same thing as a call to S52_PL_getFTYP()
extern S52_Obj_t S57_getObjtype(S57_geo *geoData);
//extern S57_Obj_t S57_getObjtype(S57_geo *geoData);
// return TRUE if same point
//extern int       S57_samePtPos(S57_geo *geoA, S57_geo *geoB);
// return S57 attribute value of the attribute name
extern GString  *S57_getAttVal(S57_geo *geoData, const char *name);
// set attribute name and value
extern GData    *S57_setAtt(S57_geo *geoData, const char *name, const char *val);
// get str of the form ",KEY1:VAL1,KEY2:VAL2, ..." of S57 attribute only (not OGR)
extern const char *S57_getAtt(S57_geo *geoData);

//extern int       S57_setTouch(S57_geo *geo, S57_geo *touch);
//extern S57_geo  *S57_getTouch(S57_geo *geo);
extern int       S57_setTouchTOPMAR(S57_geo *geo, S57_geo *touch);
extern S57_geo  *S57_getTouchTOPMAR(S57_geo *geo);
extern int       S57_setTouchLIGHTS(S57_geo *geo, S57_geo *touch);
extern S57_geo  *S57_getTouchLIGHTS(S57_geo *geo);
extern int       S57_setTouchDEPARE(S57_geo *geo, S57_geo *touch);
extern S57_geo  *S57_getTouchDEPARE(S57_geo *geo);
extern int       S57_setTouchDEPVAL(S57_geo *geo, S57_geo *touch);
extern S57_geo  *S57_getTouchDEPVAL(S57_geo *geo);

extern double    S57_setScamin(S57_geo *geo, double scamin);
extern double    S57_getScamin(S57_geo *geo);
extern double    S57_resetScamin(S57_geo *geo);


// suppression
extern gboolean  S57_setSup(S57_geo *geoData, gboolean sup);
extern gboolean  S57_getSup(S57_geo *geoData);

// count the number of 'real (6length)' attributes
//extern int       S57_getNumAtt(S57_geo *geoData);
// return the 'real' attributes of the geodata. name and val must be preallocated, and be sufficient large. (use S57_getNumAtt for counting)
//extern int       S57_getAttributes(S57_geo *geoData, char **name, char **val);

// debug
extern int       S57_dumpData(S57_geo *geoData, int dumpCoords);
extern unsigned int S57_getGeoID(S57_geo *geoData);

#ifdef S52_USE_PROJ
#include <proj_api.h>   // projXY, projUV, projPJ
extern int       S57_donePROJ();
extern int       S57_setMercPrj(double lat);
extern projXY    S57_prj2geo(projUV uv);
extern int       S57_geo2prj3dv(guint npt, double *data);
extern int       S57_geo2prj(S57_geo *geo);
#endif

//extern int       S57_isPtInside(int npt, pt3 *v, double x, double y, int close)
extern int       S57_isPtInside(int npt, double *xyz, double x, double y, int close);
extern int       S57_touch(S57_geo *geoA, S57_geo *geoB);

//extern guint     S57_getCrntIdx(S57_geo *geo);
//extern guint     S57_setCrntIdx(S57_geo *geo, guint index);
extern guint     S57_getGeoSize(S57_geo *geo);
extern guint     S57_setGeoSize(S57_geo *geo, guint size);

extern int       S57_newCentroid(S57_geo *geo);
extern int       S57_addCentroid(S57_geo *geo, double  x, double  y);
extern int       S57_getNextCentroid(S57_geo *geo, double *x, double *y);

#ifdef S52_USE_SUPP_LINE_OVERLAP
extern int       S57_markOverlapGeo(S57_geo *geo, S57_geo *geoEdge);
extern GString  *S57_getRCIDstr(S57_geo *geo);
// experimental (fail)
//extern int       S57_sameChainNode(S57_geo *geoA, S57_geo *geoB);
#endif

//// returns the window boundary with the current projection. After  the geo2prj and initproj have been public, this function may be moved to application layer.
//extern void S57_getGeoWindowBoundary(double lat, double lng, double scale, int width, int height, double *latMin, double *latMax, double *lngMin, double *lngMax);
//#endif

#endif
