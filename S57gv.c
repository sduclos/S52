// S57gv.c: interface to OpenEV (GV) S57 geo data
//
// Project:  OpENCview/OpenEV

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2016 Sylvain Duclos sduclos@users.sourceforge.net

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



// Note: use the actual data of GV (witch is a copy of OGR data)

#include "S57gv.h"
#include "S57data.h"         // S57_geo
#include "S52utils.h"        // PRINTF()
                             
#include "gvproperties.h"    // gv_properties_*()

#include "gvshapes.h"        // GvShapes, GvShape, gv_shape_get_extents()
#include "gvshapeslayer.h"   // GvShapesLayer,


// this is more 'computed and get' then a getter
#define gv_shape_compute_extents gv_shape_get_extents 


typedef struct GvShape _srcData;

static int    _loadAtt(S57_geo *geoData, GvShape *shape)
{
    GvProperties *props = gv_shape_get_properties(shape);
    int           nProp = gv_properties_count(props);

    for (int i=0; i<nProp; ++i) {
        const char *propName  = gv_properties_get_name_by_index (props, i);
        const char *propValue = gv_properties_get_value_by_index(props, i);

        if (NULL == propValue) {
            PRINTF("gv_properties_value = null \n");
            g_assert(0);
            return FALSE;
        }

        S57_setAtt(geoData, propName, propValue);
    }

    return TRUE;
}

int        S57_gvLoadCell (const char *filename,  S52_loadLayer_cb cb)
{
    GvData *raw_data = NULL;
    int     i        = 0;

    // call 'cb' for each layer
    while (NULL != (raw_data = gv_shapes_from_ogr(filename, i))) {
        cb(filename, raw_data);
        ++i;
    }

    return TRUE;
}

//int        S57_gvLoadLayer(const char *layername, GvShapesLayer *layer, _loadObj_cb cb)
int        S57_gvLoadLayer(const char *layername, void *layer, S52_loadObj_cb cb)
{
    GvShapesLayer *l = (GvShapesLayer*) layer;
    int nShapes = gv_shapes_num_shapes(l->data);

    // check that geometric data type are in sync with OpenGL
    g_assert(sizeof(geocoord) == sizeof(double));

    for (int i=0; i<nShapes; ++i) {
        GvShape *shape = gv_shapes_get_shape(GV_SHAPES_LAYER(layer)->data, i);

        cb(layername, shape);
    }

    return TRUE;
}

S57_geo   *S57_gvLoadObject(const char *objname, void *shape)
// get object geo data from openev GvShape
// NOTE: caller responsible to free mem for the moment
{
    S57_geo *geoData   = NULL;
    int      shapetype = 0;
    GvRect   rect;

    // return empty shape --for pick & Mariners' object
    if (NULL==shape) {
        geoData = S57_set_META();

        if (NULL != objname)
            S57_setName(geoData, objname);

        return geoData;
    }

    gv_shape_compute_extents((GvShape*)shape, &rect);

    // mariners' object
    shapetype = gv_shape_type((GvShape*)shape);

    // get geo data
    switch (shapetype) {

        case GVSHAPE_POINT: {
            GvPointShape *point  = (GvPointShape *) shape;
            geocoord *pt = &point->x;
            geoData = S57_setPOINT(pt);
            break;
        }

        case GVSHAPE_LINE: {
            GvLineShape  *line   = (GvLineShape *) shape;

            geoData = S57_setLINES(line->num_nodes, line->xyz_nodes);

            break;
        }

        case GVSHAPE_AREA: {
            // FIXME: check if winding is OK (at load time)
            GvAreaShape *area    = (GvAreaShape *) shape;

            geoData = S57_setAREAS((guint)area->num_rings, (guint*)area->num_ring_nodes, area->xyz_ring_nodes, S57_AW_NONE);

            break;
        }

        case GVSHAPE_COLLECTION: {
            // ogr SPLIT_MULTIPOINT prob !!
            //GvCollectionShape *collection  = (GvCollectionShape *) shape;
            //int nCollection = gv_shape_collection_get_count(shape);

            //PRINTF("nCollection = %i\n", nCollection);

            PRINTF("FIXME: wkbMultiLineString found ???\n");
            g_assert_not_reached(); // MultiLineString (need this for line removal)

            geoData = S57_set_META();

            break;
        }

        default: {
            // FIXME: find a decent default (see openev/gvshapes.h)!!!
            PRINTF("ERROR: invalid object type GVSHAPE_??? = %i\n", shapetype);
        }
    }

    S57_setName(geoData, objname);
    S57_setExt(geoData, rect.x, rect.y, rect.x+rect.width, rect.y+rect.height);

    _loadAtt(geoData, (GvShape*)shape);

    // debug
    //_dumpData(geoData);

    return geoData;
}
