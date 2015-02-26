// gvS57layer.h: S57 layer classe header for OpenEV
//
// Project:  OpENCview/OpenEV

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2015 Sylvain Duclos sduclos@users.sourceforge.net

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



#ifndef _GVS57LAYER_H_
#define _GVS57LAYER_H_

#include "gvshapeslayer.h"  // GvShapesLayerClass, GvShapesLayer, GvShapes
#include <gmodule.h>        // GModule

// FIXME: where are declaration for:
//   - GtkType
//   - GtkObject
//   - gchar

//#define GV_TYPE_S57_LAYER            (gv_S57_layer_get_type ())
//#define GV_S57_LAYER(obj)            (GTK_CHECK_CAST ((obj),         GV_TYPE_S57_LAYER, GvS57Layer))
//#define GV_S57_LAYER_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GV_TYPE_S57_LAYER, GvS57LayerClass))
//#define GV_IS_S57_LAYER(obj)         (GTK_CHECK_TYPE ((obj),         GV_TYPE_S57_LAYER))
//#define GV_IS_S57_LAYER_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GV_TYPE_S57_LAYER))

//typedef struct _GvS57Layer      GvS57Layer;
//typedef struct _GvS57LayerClass GvS57LayerClass;

//struct _GvS57Layer
//{
//    GvShapesLayer layer;
//};

//struct _GvS57LayerClass
//{
//    GvShapesLayerClass parent_class;
//};

//GtkType     gv_S57_layer_get_type(void);
GtkObject*  gv_S57_layer_new(GvShapes *shapes);
void        gv_S57_layer_init(GvShapesLayer *layer);

// used in plug-in mode --put here for 'completess'
// since it's part of the interface
void          _layer_init(GvShapesLayer *layer);
const gchar  *_ogr_driver_name();
const gchar *g_module_check_init(GModule *module);
void         g_module_unload();

#endif // __GVS57LAYER_H__
