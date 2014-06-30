// S57ogr.c: interface to OGR S57 object data
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


#include "S57ogr.h"     // S57_geo
#include "S52utils.h"   // PRINTF()
#include "ogr_api.h"    // OGR*

#include <glib.h>       // GPtrArray


// WARNING: must be in sync with S52.c:WORLD_SHP
#define WORLD_BASENM   "--0WORLD"

static int        _setExtent(S57_geo *geoData, OGRGeometryH geometry)
{
    return_if_null(geoData);
    return_if_null(geometry);
    //if (NULL == geometry) || NULL==geoData)
    //    return FALSE;

    OGREnvelope envelope;

    OGR_G_GetEnvelope(geometry, &envelope);

    S57_setExt(geoData, envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY);
                  
    return TRUE;
}


static int        _getGeoPtCount(OGRGeometryH hGeom, int iGeo, OGRGeometryH *hGeomRef )
{
    int vert_count = 0;

    *hGeomRef  = OGR_G_GetGeometryRef(hGeom, iGeo);
    if (NULL != *hGeomRef)
        vert_count = OGR_G_GetPointCount(*hGeomRef);
    else
    {
        /* FIXME: something is wrong in OGR if we get here
         * ie the geometry handle doesn't refer to a geometry!
         */
        PRINTF("WARNING: got null geometry\n" );
        g_assert(0);
    }

    return vert_count;
}

static int        _setAtt(S57_geo *geoData, OGRFeatureH hFeature)
{
    int field_count = OGR_F_GetFieldCount(hFeature);
    for (int field_index=0; field_index<field_count; ++field_index) {
        if (OGR_F_IsFieldSet(hFeature, field_index)) {
            const char *propName  = OGR_Fld_GetNameRef(OGR_F_GetFieldDefnRef(hFeature,field_index));
            const char *propValue = OGR_F_GetFieldAsString(hFeature, field_index);

            S57_setAtt(geoData, propName, propValue);

            if (0 == g_strcmp0(S57_getName(geoData), "M_NPUB")) {
                PRINTF("DEBUG: M_NPUB-%i: %s --> %s\n", field_index, propName, propValue);
            }
            if (0 == g_strcmp0(S57_getName(geoData), "C_AGGR")) {
                PRINTF("DEBUG: C_AGGR-%i: %s --> %s\n", field_index, propName, propValue);
            }
            if (0 == g_strcmp0(S57_getName(geoData), "C_ASSO")) {
                PRINTF("DEBUG: C_ASSO-%i: %s --> %s\n", field_index, propName, propValue);
            }
        }
    }

    // optimisation: direct link to GString save the search in attList
    GString  *scamin = S57_getAttVal(geoData, "SCAMIN");
    if ((NULL!=scamin) && (NULL!=scamin->str)){
        S57_setScamin(geoData, S52_atof(scamin->str));
    }

    return TRUE;
}

DLL int   STD  S52_loadLayer(const char *layername, void *layer, S52_loadObject_cb loadObject_cb);
static int        _ogrLoadCell(const char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb)
{
    OGRDataSourceH hDS         = NULL;;
    OGRSFDriverH   hDriver     = NULL;

    PRINTF("DEBUG: starting to load cell (%s)\n", filename);

    hDS = OGROpen(filename, FALSE, &hDriver);

    if (NULL == hDS) {
        PRINTF("WARNING: file loading failed (%s)\n", filename);
        return FALSE;
    }

    if (NULL == loadLayer_cb) {
        PRINTF("ERROR: should be using default S52_loadLayer() callback\n");
        g_assert(0);
    }

    if (NULL == loadObject_cb) {
        PRINTF("ERROR: should be using default S52_loadObject_cb() callback\n");
        g_assert(0);
    }

    //_loadAux(hDS);
    int nLayer = OGR_DS_GetLayerCount(hDS);
    for (int iLayer=0; iLayer<nLayer; ++iLayer) {
        OGRLayerH       ogrlayer  = OGR_DS_GetLayer(hDS, iLayer);
        OGRFeatureDefnH defn      = OGR_L_GetLayerDefn(ogrlayer);
        const char     *layername = OGR_FD_GetName(defn);

#ifdef _MINGW
        // on Windows 32 the callback is broken
        S52_loadLayer(layername, ogrlayer, NULL);
#else
        //loadLayer_cb(layername, ogrlayer, NULL);
        loadLayer_cb(layername, ogrlayer, loadObject_cb);
#endif

    }

    //OGR_DS_Destroy(hDS);
    OGRReleaseDataSource(hDS);

    return TRUE;
}

int            S57_ogrLoadCell(const char *filename, S52_loadLayer_cb loadLayer_cb, S52_loadObject_cb loadObject_cb)
{
    // check that geometric data type are in sync with OpenGL
    if (sizeof(geocoord) != sizeof(double)) {
        PRINTF("ERROR: sizeof(geocoord) != sizeof(double)\n");
        g_assert(0);
    }

    OGRRegisterAll();

    _ogrLoadCell(filename, loadLayer_cb, loadObject_cb);

    return TRUE;
}

int            S57_ogrLoadLayer(const char *layername, void *ogrlayer, S52_loadObject_cb loadObject_cb)
{
    if (NULL==layername || NULL==ogrlayer) {
        PRINTF("ERROR: layername || ogrlayer || S52_loadLayer_cb is NULL\n");
        g_assert(0);
    }

    if (NULL == loadObject_cb) {
        static int  silent  = FALSE;
        if (FALSE == silent) {
            PRINTF("NOTE: using default S52_loadObject() callback\n");
            PRINTF("       (this msg will not repeat)\n");
            silent = TRUE;
        }
        loadObject_cb = S52_loadObject;
    }

    OGRFeatureH feature = NULL;
    while ( NULL != (feature = OGR_L_GetNextFeature((OGRLayerH)ogrlayer))) {
        // debug
        //PRINTF("layer:feature %X:%X\n",  ogrlayer, feature);

#ifdef _MINGW
        // on Windows 32 the callback is broken
        S52_loadObject(layername, feature);
#else
        loadObject_cb(layername, (void*)feature);
#endif

        OGR_F_Destroy(feature);
    }

    return TRUE;
}


static S57_geo   *_ogrLoadObject(const char *objname, void *feature, OGRGeometryH hGeomNext)
{
    S57_geo           *geoData = NULL;
    OGRGeometryH       hGeom   = NULL;
    OGRwkbGeometryType eType   = wkbNone;

    if (NULL != feature)
        hGeom = OGR_F_GetGeometryRef((OGRFeatureH)feature);
    else
        hGeom = hGeomNext;

    if (NULL != hGeom)
        eType = OGR_G_GetGeometryType(hGeom);
    else
        eType = wkbNone; // DSIS

    switch (eType) {
        // POINT
        case wkbPoint25D:
        case wkbPoint: {
            geocoord *pointxyz = g_new(geocoord, 3);

            pointxyz[0] = OGR_G_GetX(hGeom, 0);
            pointxyz[1] = OGR_G_GetY(hGeom, 0);
            pointxyz[2] = OGR_G_GetZ(hGeom, 0);

            geoData = S57_setPOINT(pointxyz);
            _setExtent(geoData, hGeom);

            break;
        }

        // LINE
        case wkbLineString25D:
        case wkbLineString: {
            int count = OGR_G_GetPointCount(hGeom);

            // NOTE: when S52_USE_SUPP_LINE_OVERLAP then Edge might have 0 node
            /* so this code fail
            if (count < 2) {
                PRINTF("WARNING: a line with less than 2 points!?\n");
                g_assert(0);
                return NULL;
            }
            */

            geocoord *linexyz = NULL;
            if (0 != count)
                linexyz = g_new(geocoord, 3*count);

            for (int node=0; node<count; ++node) {
                linexyz[node*3+0] = OGR_G_GetX(hGeom, node);
                linexyz[node*3+1] = OGR_G_GetY(hGeom, node);
                linexyz[node*3+2] = OGR_G_GetZ(hGeom, node);
            }

            geoData = S57_setLINES(count, linexyz);

            _setExtent(geoData, hGeom);

            break;
        }

        // AREA
        case wkbPolygon25D:
        case wkbPolygon: {
            // Note: S57 area have CW outer ring and CCW inner ring
            guint        nRingCount = OGR_G_GetGeometryCount(hGeom);
            guint       *ringxyznbr;
            geocoord   **ringxyz;
            double       area = 0;

            ringxyznbr = g_new(guint,      nRingCount);
            ringxyz    = g_new(geocoord *, nRingCount);

            // NOTE: to check winding on an open area
            //for (i = n-1, j = 0; j < n; i = j, j++) {
            //     ai = x[i] * y[j] - x[j] * y[i];
            //}

            for (guint iRing=0; iRing<nRingCount; ++iRing) {
                OGRGeometryH hRing;
                guint vert_count = _getGeoPtCount(hGeom, iRing, &hRing);

                ringxyznbr[iRing] = vert_count;

                // skip this ring if no vertex
                if (0 == vert_count) {
                    // FIXME: what should be done here,
                    // FIX: S52 - discard this ring or the object or the layer or the whole chart (update)
                    // FIX: GDAL/OGR - is it a bug in the reader or in the chart it self (S57)
                    // FIX: or this is an empty Geo
                    PRINTF("WARNING: wkbPolygon, empty ring  (%s)\n", objname);
                    g_assert(0);
                    continue;
                }

                ringxyz[iRing]    = g_new(geocoord, vert_count*3*sizeof(geocoord));

                // check if last vertex is NOT the first vertex (ie ring not close)
                if ((OGR_G_GetX(hRing, 0) != OGR_G_GetX(hRing, vert_count-1)) ||
                    (OGR_G_GetY(hRing, 0) != OGR_G_GetY(hRing, vert_count-1)) ) {

                    PRINTF("ERROR: S-57 ring (AREA) not closed (%s)\n", objname);

                    g_assert(0);

                    // Note: to compute area of an open poly
                    //double area = 0;
                    //for (guint i=vert_count-1, j=0; j<vert_count; i=j, ++j) {
                    //    double x1 = OGR_G_GetX(hRing, i);
                    //    double y1 = OGR_G_GetY(hRing, i);
                    //    double x2 = OGR_G_GetX(hRing, j);
                    //    double y2 = OGR_G_GetY(hRing, j);
                    //    area += (x1*y2) - (x2*y1);
                    //}
                }

                for (guint i=0; (i+1)<vert_count; i++) {
                    double x1 = OGR_G_GetX(hRing, i  );
                    double y1 = OGR_G_GetY(hRing, i  );
                    double x2 = OGR_G_GetX(hRing, i+1);
                    double y2 = OGR_G_GetY(hRing, i+1);
                    area += (x1*y2) - (x2*y1);
                }

                // CW if area is < 0, else CCW
                PRINTF("AREA(ring=%i/%i): %s (%s)\n", iRing, nRingCount, (area <= 0.0) ? "CW" : "CCW", objname);

                // CCW winding
                if (area > 0.0) {
                    // if first ring reverse winding to CW
                    if (0==iRing) {
                        PRINTF("DEBUG: reversing S-57 outer ring to CW (%s)\n", objname);
                        //g_assert(0);
                        for (guint node=0; node<vert_count; ++node) {
                            ringxyz[iRing][node*3+0] = OGR_G_GetX(hRing, vert_count - node-1);
                            ringxyz[iRing][node*3+1] = OGR_G_GetY(hRing, vert_count - node-1);
                            ringxyz[iRing][node*3+2] = OGR_G_GetZ(hRing, vert_count - node-1);
                        }
                    } else {
                        for (guint node=0; node<vert_count; ++node) {
                            ringxyz[iRing][node*3+0] = OGR_G_GetX(hRing, node);
                            ringxyz[iRing][node*3+1] = OGR_G_GetY(hRing, node);
                            ringxyz[iRing][node*3+2] = OGR_G_GetZ(hRing, node);
                        }
                    }


                } else {  // CW winding
                    if (0==iRing) {
                        for (guint node=0; node<vert_count; ++node) {
                            ringxyz[iRing][node*3+0] = OGR_G_GetX(hRing, node);
                            ringxyz[iRing][node*3+1] = OGR_G_GetY(hRing, node);
                            ringxyz[iRing][node*3+2] = OGR_G_GetZ(hRing, node);
                        }
                    } else {
                        // if NOT first ring reverse winding (CCW)
                        PRINTF("DEBUG: reversing S-57 inner ring to CCW (%s)\n", objname);
                        //g_assert(0);

                        for (guint node=0; node<vert_count; ++node) {
                            ringxyz[iRing][node*3+0] = OGR_G_GetX(hRing, vert_count - node-1);
                            ringxyz[iRing][node*3+1] = OGR_G_GetY(hRing, vert_count - node-1);
                            ringxyz[iRing][node*3+2] = OGR_G_GetZ(hRing, vert_count - node-1);
                        }
                    }

                }
            }     // for loop

            geoData = S57_setAREAS(nRingCount, ringxyznbr, ringxyz, (area <= 0.0) ? S57_AW_CW : S57_AW_CCW);
            _setExtent(geoData, hGeom);

            if (0==strcmp(WORLD_BASENM, objname)) {
                // Note: loading shapefile as a 'marfea' use a transparent fill so NODATA
                // is still visible (seem better than 'mnufea' wich has no colour fill)
                S57_setName(geoData, "marfea");

                // pslb3_2.pdf (p. II-22): Mariners' Object Class: Manufacturers' feature
                //    Note that manufacturers' areas, whether non-chart or chart areas, should not use area colour fill.
                //S57_setName(geoData, "mnufea");
            }

            break;
        }

#ifdef S52_USE_WORLD
        // shapefile area
        case wkbMultiPolygon: {
            guint nPolyCount = OGR_G_GetGeometryCount(hGeom);
            for (guint iPoly=0; iPoly<nPolyCount; ++iPoly) {
                OGRGeometryH hGeomNext = OGR_G_GetGeometryRef(hGeom, iPoly);
                // recursion
                S57_geo *geo = _ogrLoadObject(objname, NULL, hGeomNext);
                if (NULL == geoData)
                    geoData = geo;
                else
                    S57_setNextPoly(geoData, geo);

            }
            break;
        }
#endif

        case wkbGeometryCollection:
        case wkbMultiLineString: {
            PRINTF("WARNING: wkbGeometryCollection & wkbMultiLineString not handled \n");
            g_assert(0);  // land here if GDAL/OGR was patched multi-line
            break;
        }

        case wkbNone:
            // DSID layer get here
            geoData = S57_set_META();
            break; // META_T

        case wkbMultiPoint:
            // ogr SPLIT_MULTIPOINT prob !!
            PRINTF("DEBUG: Multi-Pass!!!\n");
            //PRINTF("ERROR: set env var OGR_S57_OPTIONS to SPLIT_MULTIPOINT:ON\n");
            //PRINTF("FIXME: or wkbMultiLineString found!\n");
            //g_assert_not_reached(); // MultiLineString (need this for line removal)

            //geoData = S57_set_META();

            //GvCollectionShape *collection  = (GvCollectionShape *) shape;
            //int nCollection = gv_shape_collection_get_count(shape);
            break;

        default:
            // FIXME: find a decent default (see openev/gvshapes.h)!!!
            PRINTF("WKB type not handled  = %i %0x\n", eType, eType);
            g_assert(0); 

    }

    // debug
    //PRINTF("name: %s\n", objname);

    return geoData;
}

S57_geo       *S57_ogrLoadObject(const char *objname, void *feature)
{
    return_if_null(objname);
    return_if_null(feature);

    // debug
    //PRINTF("DEBUG: start loading object (%s:%X)\n", objname, feature);

    S57_geo *geoData = _ogrLoadObject(objname, feature, NULL);
    if (NULL == geoData)
        return NULL;

    if (0 != strcmp(WORLD_BASENM, objname)) {
        S57_setName(geoData, objname);
    }
    _setAtt(geoData, feature);


    // debug
    //if (207 == S57_getGeoID(geoData)) {
    //    S57_dumpData(geoData, FALSE);
    //}

    //PRINTF("DEBUG: finish loading object (%s)\n", objname);

    return geoData;
}



#if 0
int main(int argc, char** argv)
{

   return 1;
}
#endif
