// _s52_setupmarfea.i: setup Mariners' Feature (marfea)
//
// SD 2016APR27


//static S52ObjectHandle _marfea_area = FALSE;
//static S52ObjectHandle _marfea_line = FALSE;
//static S52ObjectHandle _marfea_point = FALSE;

static S52ObjectHandle _positn_point = FALSE;

static int      _s52_setupmarfea(double cLat, double cLon)
// exemple to display directly something define in the PLib
{
    // quiet gcc
    (void)cLat;
    (void)cLon;

    /*
    // CCW doesn't center text
    double xyz[5*3] = {
        cLon + 0.00, cLat + 0.000, 0.0,
        cLon + 0.00, cLat + 0.005, 0.0,
        cLon - 0.01, cLat + 0.005, 0.0,
        cLon - 0.01, cLat + 0.000, 0.0,
        cLon + 0.00, cLat + 0.000, 0.0,
    };
    */

    /*
    // AREA (CW: to center the text)
    double xyzArea[5*3]  = {
        cLon + 0.00, cLat + 0.000, 0.0,
        cLon - 0.01, cLat + 0.000, 0.0,
        cLon - 0.01, cLat + 0.005, 0.0,
        cLon + 0.00, cLat + 0.005, 0.0,
        cLon + 0.00, cLat + 0.000, 0.0,
    };
    */

    // LINE
    //double xyzLine[2*3]  = {
    //    cLon + 0.00, cLat + 0.000, 0.0,
    //    cLon + 0.02, cLat - 0.005, 0.0
    //};

    // debug
    //double xyzLine[2*3]  = {
    //    -72.3166666, 46.416666, 0.0,
    //    -72.4,       46.4,      0.0
    //};

    // POINT
    //double xyzPoint[1*3] = {
    //    cLon - 0.02, cLat - 0.005, 0.0
    //};
    // add the text
    //char attVal[] = "OBJNAM:6.5_marfea";

    //_marfea_area  = S52_newMarObj("marfea", S52_AREAS, 5, xyzArea,  attVal);
    //_marfea_area  = S52_newMarObj("marfea", S52_AREAS, 5, NULL,     attVal);
    //_marfea_line  = S52_newMarObj("marfea", S52_LINES, 2, xyzLine,  attVal);
    //_marfea_point = S52_newMarObj("marfea", S52_POINT, 1, xyzPoint, attVal);

    //S52_pushPosition(_marfea_area, cLat + 0.000, cLon + 0.00, 0.0);
    //S52_pushPosition(_marfea_area, cLat + 0.000, cLon - 0.01, 0.0);
    //S52_pushPosition(_marfea_area, cLat + 0.005, cLon - 0.01, 0.0);
    //S52_pushPosition(_marfea_area, cLat + 0.005, cLon + 0.00, 0.0);
    //S52_pushPosition(_marfea_area, cLat + 0.000, cLon + 0.00, 0.0);

    // Petit-Cap GPS pos
    double xyzPoint[1*3] = {-64.440065, 49.027642, 0.0};
    char attVal[] = "loctim:Petit-Cap";
    _positn_point = S52_newMarObj("positn", S52_POINT, 1, xyzPoint, attVal);

    return TRUE;
}
