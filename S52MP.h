// S52MP.h: Mariner Parameter
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



#ifndef _S52MP_H_
#define _S52MP_H_

#include "S52.h"   // S52MarinerParameter

extern double S52_MP_get(S52MarinerParameter param);
extern int    S52_MP_set(S52MarinerParameter param, double val);

extern int    S52_MP_setTextDisp(unsigned int prioIdx, unsigned int count, unsigned int state);
extern int    S52_MP_getTextDisp(unsigned int prioIdx);


#endif //_S52MP_H_
