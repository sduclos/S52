// S57gv.h: interface to get S57 data from openev (GV)
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



#ifndef _S57GV_H_
#define _S57GV_H_

#include "S57data.h"    // S57_geo
#include "S52.h"        // S52_loadLayer_cb, S52_loadObj_cb

//#include "gvshapeslayer.h"  // GvShapesLayerClass, GvShapesLayer, GvShapes


extern int      S57_gvLoadCell  (const char *filename,  S52_loadLayer_cb cb);
//extern int      S57_gvLoadLayer (const char *layername, GvShapesLayer *gvlayer, _loadObj_cb cb);
extern int      S57_gvLoadLayer (const char *layername, void *layer, S52_loadObj_cb cb);
extern S57_geo *S57_gvLoadObject(const char *objname,   void *shape);

#endif
