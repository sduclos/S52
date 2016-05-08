// _s52_setupPRDARE.i: test centroid (PRDARE: wind farm)
//
// SD 2016APR27


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
