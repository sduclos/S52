// _s52_setupPASTRK.i: setup past tarck
//
// SD 2016APR27


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

