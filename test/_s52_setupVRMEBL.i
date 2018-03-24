// _s52_setupVRMEBL.i: setup VRM/EBL
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


// test - VRMEBL

//static int _originIsSet = FALSE;  //for VRMEBL

// S52 object name:"ebline"
static S52ObjectHandle _vrmeblA = FALSE;

// test - cursor DISP 9 (instead of IHO PLib DISP 8), need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = FALSE;  // 2 - open cursor

// ebline draw flag (F4 toggle ON/OFF)
//static int _drawVRMEBL = TRUE;
static int _drawVRMEBL = FALSE;


static int _s52_setupVRMEBL(double cLat, double cLon)
{
    //char *attVal   = NULL;      // ordinary cursor
    //char  attVal[] = "cursty:2,_cursor_label:0.0N 0.0W";  // open cursor
    char   attVal[] = "cursty:2";  // open cursor
    double xyz[3]   = {cLon, cLat, 0.0};
    _cursor2 = S52_newMarObj("cursor", S52_POINT, 1, xyz, attVal);

    int S52_VRMEBL_vrm = TRUE;
    int S52_VRMEBL_ebl = TRUE;
    int S52_VRMEBL_sty = TRUE;  // normalLineStyle
    int S52_VRMEBL_ori = TRUE;  // (user) setOrigin

    // normal VRM/EBL + setOrigin
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    // normal EBL + setOrigin
    _vrmeblA = S52_newVRMEBL(!S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    // normal VRM, freely moveable
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    // normal VRM + setOrigin
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    // --- old test --------------------------------------------
    // normal VRM/EBL
    //_vrmeblA = S52_newVRMEBL(TRUE, TRUE, TRUE, FALSE);
    // normal VRM (vrmark)
    //--> _vrmeblA = S52_newVRMEBL(vrm, ebl, normalLineStyle, setOrigin);
    // normal EBL
    //_vrmeblA = S52_newVRMEBL(FALSE, TRUE, TRUE, FALSE);
    // alterned VRM/EBL line style
    //_vrmebl = S52_newVRMEBL(TRUE, TRUE, FALSE);
    // alternate VRM/EBL, freely moveable
    //_vrmeblB = S52_newVRMEBL(TRUE, TRUE, FALSE, TRUE);
    // alternate VRM, freely moveable
    //_vrmeblB = S52_newVRMEBL(TRUE, FALSE, FALSE, TRUE);

    if (FALSE == _drawVRMEBL) {
        // suppression ON all obj of a class
        S52_setS57ObjClassSupp("cursor", TRUE);
        S52_setS57ObjClassSupp("ebline", TRUE);
        S52_setS57ObjClassSupp("vrmark", TRUE);

        // or supp one obj
        //S52_toggleDispMarObj(_cursor2);
        //S52_toggleDispMarObj(_vrmeblA);
    }

    return TRUE;
}
