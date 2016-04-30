// _s52_setupOWNSHP.i: setup OWNSHIP
//
// SD 2016APR26


#ifdef USE_FAKE_AIS

#define OWNSHPLABEL "OWNSHP\n220 deg / 6.0 kt"
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

    S52_setVector(_ownshp, 0, 290.0, 6.0);  // ownship use S52_MAR_VECSTB

    // test - supp ON
    // all obj of a class
    //S52_setS57ObjClassSupp("ownshp", TRUE);
    // supp this obj
    //S52_toggleDispMarObj(_ownshp);

    return TRUE;
}
#endif  // USE_FAKE_AIS
