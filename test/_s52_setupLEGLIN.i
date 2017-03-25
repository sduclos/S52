// _s52_setupLEGLIN.i: set route (leg line)
//
// SD 2016APR27

static S52ObjectHandle _leglin1 = FALSE;
static S52ObjectHandle _leglin2 = FALSE;
static S52ObjectHandle _leglin3 = FALSE;
//static S52ObjectHandle _leglin4 = FALSE;
//static S52ObjectHandle _leglin5 = FALSE;

static S52ObjectHandle _waypnt0 = FALSE;
static S52ObjectHandle _waypnt1 = FALSE;
static S52ObjectHandle _waypnt2 = FALSE;
static S52ObjectHandle _waypnt3 = FALSE;
static S52ObjectHandle _waypnt4 = FALSE;

static S52ObjectHandle _wholin  = FALSE;  // wheel-over-line

//static double _leglin4LL[2*2];

static int _s52_setupLEGLIN(double cLat, double cLon)
{

    typedef struct pt2 {
        double lat, lon;
    } pt2;

    // need to turn OFF guard zone because GL projection (used by pick)
    //not set yet (set via the first call to S52_draw())
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);

    /*
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, cLat - 0.01, cLon - 0.01, cLat - 0.010, cLon + 0.010);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, cLat - 0.01, cLon + 0.01, cLat + 0.010, cLon + 0.010);
    _leglin3  = S52_newLEGLIN(1, 12.0, 0.2, cLat + 0.01, cLon + 0.01, cLat - 0.010, cLon - 0.010);
    _leglin4  = S52_newLEGLIN(1, 12.0, 0.2, cLat - 0.01, cLon + 0.01, cLat - 0.015, cLon + 0.015);
    _leglin5  = S52_newLEGLIN(1, 12.0, 0.2, cLat - 0.01, cLon + 0.01, cLat - 0.005, cLon + 0.015);
    */

    /*
    //_leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpN.lat, wpN.lon, wpW.lat, wpW.lon);
    //_leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpW.lat, wpW.lon, wpS.lat, wpS.lon);
    //_leglin3  = S52_newLEGLIN(1, 12.0, 0.2, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    //_leglin4  = S52_newLEGLIN(1, 12.0, 0.2, wpE.lat, wpE.lon, wpN.lat, wpN.lon);
    */


    pt2 wpN = {cLat + 0.01, cLon       };
    pt2 wpE = {cLat       , cLon + 0.01};
    pt2 wpS = {cLat - 0.01, cLon       };
    pt2 wpW = {cLat       , cLon - 0.01};

    //-----------------------------
    /*
    // heading change from NW to NE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpS.lat, wpS.lon, wpW.lat, wpW.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpW.lat, wpW.lon, wpN.lat, wpN.lon);
    //*/

    /*
    // heading change from NE to NW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpE.lat, wpE.lon, wpN.lat, wpN.lon);
    //*/

    //-----------------------------
    /*
    // heading change from NW to SW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpE.lat, wpE.lon, wpN.lat, wpN.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpN.lat, wpN.lon, wpW.lat, wpW.lon);
    //*/

    /*
    // heading change from SW to NW
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpE.lat, wpE.lon, wpS.lat, wpS.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpW.lat, wpW.lon);
    //*/

    //-----------------------------
    /*
    // heading change from SE to NE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpW.lat, wpW.lon, wpS.lat, wpS.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpE.lat, wpE.lon);
    //*/

    /*
    // heading change from NE to SE
    _leglin1  = S52_newLEGLIN(1, 12.0, 0.1, wpW.lat, wpW.lon, wpN.lat, wpN.lon);
    _leglin2  = S52_newLEGLIN(1, 12.0, 0.3, wpN.lat, wpN.lon, wpE.lat, wpE.lon);
    //*/

    //-----------------------------
    //*
    // heading change from SW to SE
    _leglin1  = S52_newLEGLIN(1, 10.0, 0.1, wpN.lat, wpN.lon, wpW.lat, wpW.lon, FALSE);
    _leglin2  = S52_newLEGLIN(1, 11.0, 0.2, wpW.lat, wpW.lon, wpS.lat, wpS.lon, _leglin1);
    _leglin3  = S52_newLEGLIN(1, 12.0, 0.3, wpS.lat, wpS.lon, wpE.lat, wpE.lon, _leglin2);
    //*/

    if (0==_leglin1 || 0==_leglin2 || 0==_leglin3) {
        g_print("s52egl:_s52_setupLEGLIN(): S52_newLEGLIN() _leglinX failed\n");
        //g_assert(0);
        return FALSE;
    }



    {   // waypoint
        char attVal1[] = "";            // next waypoint on planned route
        char attVal2[] = "select:1";    // waypoint on planned route
        char attVal3[] = "select:2";    // waypoint on alternate planned route
        double xyz1[3] = {cLon - 0.01, cLat - 0.01, 0.0};
        double xyz2[3] = {cLon + 0.01, cLat - 0.01, 0.0};
        double xyz3[3] = {cLon + 0.01, cLat + 0.01, 0.0};
        double xyz4[3] = {wpW.lon,     wpW.lat,     0.0};

        _waypnt0 = S52_newMarObj("waypnt", S52_POINT, 1, xyz1,  attVal2);
        _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, xyz1,  attVal1);
        _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, xyz2,  attVal2);
        _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, xyz3,  attVal3);
        _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, xyz4,  attVal1);

        if (0==_waypnt0 || 0==_waypnt1 || 0==_waypnt2 || 0==_waypnt3 || 0==_waypnt4) {
            g_print("s52egl:_s52_setupLEGLIN(): S52_newMarObj() _waypntX failed\n");
            //g_assert(0);
            return FALSE;
        }

        // test
        S52_toggleDispMarObj(_waypnt0); // off
        S52_toggleDispMarObj(_waypnt0); // on
        S52_toggleDispMarObj(_waypnt2); // off

        // test - move to WP pos
        S52_pushPosition(_waypnt1,  cLat-0.01, cLon+0.01, 0.0);

        // test - over drawn a normal WP over an active WP (ugly)
        //_waypnt4 = S52_newMarObj("waypnt", S52_POINT_T, 1, xyz1,  attVal2);
    }

    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {cLon, cLat, 0.0,  cLon + 0.01, cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }

    return TRUE;
}

/*
static int      _s52_setupLEGLIN_alarm()
// will trigger alarm / indication
{
    if (FALSE != _leglin4) {
        _leglin4 = S52_delMarObj(_leglin4);
        if (FALSE != _leglin4) {
            LOGI("s52egl:_s52_setupLEGLIN(): delMarObj _leglin4 failed\n");
            g_assert(0);
        }
        // clear alarm
        S52_setMarinerParam(S52_MAR_GUARDZONE_ALARM, 0.0);
    }

    // test vertical route on DEPCNT
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat - 0.02, state->cLon + 0.00, state->cLat + 0.02, state->cLon + 0.00, NULL);

    // oblique / upbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.01, state->cLon + 0.00, state->cLat + 0.05, state->cLon + 0.02, NULL);

    // oblique - no alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon + 0.00, state->cLat + 0.05, state->cLon + 0.02, NULL);

    // oblique \  upbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat - 0.01, state->cLon + 0.00, state->cLat + 0.02, state->cLon - 0.02, NULL);

    // oblique / downbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon + 0.02, state->cLat - 0.05, state->cLon - 0.02, NULL);

    // oblique \  downbound - alarm
    //_leglin4 = S52_newLEGLIN(1, 0.0, 0.0, state->cLat + 0.02, state->cLon - 0.02, state->cLat - 0.02, state->cLon + 0.02, NULL);


    // test LEGLIN setup via cursor
    _leglin4 = S52_newLEGLIN(1, 0.0, 0.0, _leglin4LL[1], _leglin4LL[0], _leglin4LL[3], _leglin4LL[2], FALSE);
    //if (FALSE == _leglin4) {
    //    LOGI("s52egl:_s52_setupLEGLIN(): failed\n");
        if (1.0 == S52_getMarinerParam(S52_MAR_GUARDZONE_ALARM))
            LOGI("s52egl:_s52_setupLEGLIN(): ALARM ON\n");
        if (2.0 == S52_getMarinerParam(S52_MAR_GUARDZONE_ALARM))
            LOGI("s52egl:_s52_setupLEGLIN(): INDICATION ON\n");
    //}

    return TRUE;
}
*/

static int      _s52_setupIceRte(void)
{
/*

http://www.marinfo.gc.ca/fr/Glaces/index.asp

SRCN04 CWIS 122100
Bulletin des glaces pour le fleuve et le golfe Saint-Laurent de Les Escoumins aux détroits de
Cabot et de Belle-Isle émis à 2100TUC dimanche 12 février 2012 par le Centre des glaces de
Québec de la Garde côtière canadienne.

Route recommandée no 01
De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4847N 06830W puis
point de changement CHARLIE: 4900N 06800W puis
point de changement DELTA:   4930N 06630W puis
point de changement ECHO:    4930N 06425W puis
point de changement FOXTROT: 4745N 06000W puis
route normale de navigation.

Route recommandée no 05
Émise à 1431UTC le 17 FEVRIER 2012
par le Centre des Glaces de Québec de la Garde côtière canadienne.

De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4930N 06630W puis
point de changement CHARLIE: 4945N 06450W puis
point de changement DELTA:   4730N 06000W puis
route normale de navigation.
*/
    typedef struct WPxyz_t {
        double x,y,z;
    } WPxyz_t;

    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-68.5,     48.78333, 0.0},  // WP2 - BRAVO
        {-68.0,     49.00,    0.0},  // WP3 - CHARLIE
        {-66.5,     49.5,     0.0}   // WP4 - DELTA

        //{-66.5,     49.0,     0.0}   // WP4 - test horizontal dot
        //{-66.0,     49.5,     0.0}   // WP4 - test vertical dot
    };

    char attVal1[] = "select:2,OBJNAM:ALPHA";    // waypoint on alternate planned route
    char attVal2[] = "select:2,OBJNAM:BRAVO";    // waypoint on alternate planned route
    char attVal3[] = "select:2,OBJNAM:CHARLIE";  // waypoint on alternate planned route
    char attVal4[] = "select:2,OBJNAM:DELTA";    // waypoint on alternate planned route

    _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[0], attVal1);
    _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[1], attVal2);
    _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[2], attVal3);
    _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[3], attVal4);

    // need to turn OFF guard zone because GL projection not set yet (set via the first call to S52_draw())
    S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, 0.0);  // trun off
#define ALT_RTE 2
    // select: alternate (2) legline for Ice Route 2012-02-12T21:00:00Z
    _leglin1 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[0].y, WPxyz[0].x, WPxyz[1].y, WPxyz[1].x, FALSE);
    _leglin2 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[1].y, WPxyz[1].x, WPxyz[2].y, WPxyz[2].x, _leglin1);
    _leglin3 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[2].y, WPxyz[2].x, WPxyz[3].y, WPxyz[3].x, _leglin2);

    //S52_setMarinerParam(S52_MAR_GUARDZONE_BEAM, gz);  // trun on

    /*
    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_view.cLon, _view.cLat, 0.0,  _view.cLon + 0.01, _view.cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }
    */

    return TRUE;
}
