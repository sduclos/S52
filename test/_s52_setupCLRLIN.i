// _s52_setupCLRLIN.i: clearning line
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


static S52ObjectHandle _clrlin = FALSE;


static int _s52_setupCLRLIN(double cLat, double cLon)
{
    _clrlin = S52_newCLRLIN(1, cLat - 0.02, cLon - 0.02, cLat - 0.02, cLon + 0.01);

    return TRUE;
}
