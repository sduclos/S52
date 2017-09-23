// s52ais.h: interface to GPSD client
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2017 Sylvain Duclos sduclos@users.sourceforge.net

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


#ifndef _S52AIS_H_
#define _S52AIS_H_

#ifdef __cplusplus
extern "C" {
#endif

int s52ais_initAIS(void);
int s52ais_updtAISLabel(int keepTarget);
int s52ais_doneAIS(void);

#ifdef __cplusplus
}
#endif

#endif // _S52AIS_H_
