// S57ogr.h: interface to load S57 from OGR
//
// Project:  OpENCview

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



#ifndef _S57OGR_H_
#define _S57OGR_H_

#include "S52.h"       // S52_loadObject_cb()
#include "S57data.h"   // S57_geo

typedef int   (*S52_loadLayer_cb)(const char *layername, void *layer, S52_loadObject_cb loadObject_cb);

int      S57_ogrLoadCell  (const char *filename,                  S52_loadLayer_cb  loadLayer_cb, S52_loadObject_cb loadObject_cb);
int      S57_ogrLoadLayer (const char *layername, void *ogrlayer, S52_loadObject_cb loadObject_cb);
S57_geo *S57_ogrLoadObject(const char *objname,   void *shape);

#endif // _S57OGR_H_
