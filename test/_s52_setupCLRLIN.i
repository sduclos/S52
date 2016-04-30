// _s52_setupCLRLIN.i: clearning line
//
// SD 2016APR27


static S52ObjectHandle _clrlin = FALSE;


static int _s52_setupCLRLIN(double cLat, double cLon)
{
    _clrlin = S52_newCLRLIN(1, cLat - 0.02, cLon - 0.02, cLat - 0.02, cLon + 0.01);

    return TRUE;
}
