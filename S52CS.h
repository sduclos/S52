// S52CS.h: interface to Conditional Symbology (CS) instruction
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



#ifndef _S52CS_H_
#define _S52CS_H_


#include "S57data.h"       // S57_geo

#include <glib.h>          // GString

// Cond. Symb. callback
// WARNING: caller of the callback has to free the returned GString
typedef GString *(*S52_CS_cb)(S57_geo *geo);

// Conditional Symbologie
typedef struct S52_CS_condSymb {
    const char *name;         // [9] '\0' terminated !?
    S52_CS_cb   CScb;         // end with NULL
} S52_CS_condSymb;

// Note: 'extern' keyword - publish CS jump table
extern S52_CS_condSymb S52_CS_condTable[];


const char *S52_CS_version(void);

typedef struct _localObj localObj;
localObj   *S52_CS_init (void);
localObj   *S52_CS_done (localObj *local);
int         S52_CS_add  (localObj *local, S57_geo *geo);
int         S52_CS_touch(localObj *local, S57_geo *geo);

#endif //_S52CS_H_
