// _s52_setupOWNSHP.i: setup OWNSHIP
//
// SD 2016APR26
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


//#define OWNSHPLABEL "OWNSHP\n220 deg / 6.0 kt"
#define OWNSHPLABEL "OWNSHP"
static S52ObjectHandle _ownshp = FALSE;

static int _s52_setupOWNSHP(double cLat, double cLon)
{
    _ownshp = S52_newOWNSHP(OWNSHPLABEL);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 0.0, 30.0);
    _ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 15.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 100.0, 0.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 0.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 0.0, 100.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 1000.0, 50.0, 15.0, 15.0);

    S52_pushPosition(_ownshp, cLat + 0.01, cLon - 0.01, 300.0);

    S52_setVector(_ownshp, 0, 290.0, 16.0);  // ownship use S52_MAR_VECSTB

    // test - supp ON
    // all obj of a class
    //S52_setS57ObjClassSupp("ownshp", TRUE);
    // supp this obj
    //S52_toggleDispMarObj(_ownshp);

    return TRUE;
}
