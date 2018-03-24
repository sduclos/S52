// _s52_setupPASTRK.i: setup past tarck
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


static S52ObjectHandle _pastrk = FALSE;

static int _s52_setupPASTRK(double cLat, double cLon)
{
    _pastrk = S52_newPASTRK(1, 10);

    S52_pushPosition(_pastrk, cLat + 0.01, cLon - 0.01, 1.0);
    S52_pushPosition(_pastrk, cLat + 0.01, cLon + 0.01, 2.0);
    S52_pushPosition(_pastrk, cLat + 0.02, cLon + 0.02, 3.0);

    //S52_addPosition(_pastrk, cLat + 0.01, cLon - 0.01, 1.0);
    //S52_addPosition(_pastrk, cLat + 0.01, cLon + 0.01, 2.0);
    //S52_addPosition(_pastrk, cLat + 0.02, cLon + 0.02, 3.0);

    // SW - NE
    //S52_addPosition(_pastrk, cLat - 0.01, cLon - 0.01, 1.0);
    //S52_addPosition(_pastrk, cLat + 0.01, cLon + 0.01, 1.0);
    // vertical
    //S52_addPosition(_pastrk, cLat - 0.01, cLon + 0.01, 1.0);
    // horizontal
    //S52_addPosition(_pastrk, cLat - 0.01, cLon - 0.01, 1.0);

    return TRUE;
}

