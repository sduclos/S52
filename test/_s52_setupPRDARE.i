// _s52_setupPRDARE.i: test centroid (PRDARE: wind farm)
//
// SD 2016APR27
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


static S52ObjectHandle _prdare  = FALSE;

static int      _s52_setupPRDARE(double cLat, double cLon)
{
    //*
    // AREA (CW)
    double xyzArea[6*3]  = {
        cLon + 0.000, cLat + 0.000, 0.0,  // SE
        cLon - 0.005, cLat + 0.004, 0.0,  // center
        cLon - 0.010, cLat + 0.000, 0.0,  // SW
        cLon - 0.010, cLat + 0.005, 0.0,  // NW
        cLon + 0.000, cLat + 0.005, 0.0,  // NE
        cLon + 0.000, cLat + 0.000, 0.0,  // SE
    };
    //*/

    /*
    // AREA (CCW)
    double xyzArea[6*3]  = {
        cLon + 0.000, cLat + 0.000, 0.0,
        cLon + 0.000, cLat + 0.005, 0.0,
        cLon - 0.010, cLat + 0.005, 0.0,
        cLon - 0.010, cLat + 0.000, 0.0,
        cLon - 0.005, cLat + 0.004, 0.0,
        cLon + 0.000, cLat + 0.000, 0.0,
    };
    */

    // PRDARE/WNDFRM51/CATPRA9
    char attVal[] = "CATPRA:9";
    _prdare = S52_newMarObj("PRDARE", S52_AREAS, 6, xyzArea,  attVal);

    // debug: test layer ordering when mixing cell and mariners object
    //LUPT   29LU00672NILmnufeaL00005OLINES
    //char attVal[] = "CATPRA:9";
    //_prdare = S52_newMarObj("mnufea", S52_LINES, 6, xyzArea,  NULL);

    return TRUE;
}
