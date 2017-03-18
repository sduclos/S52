// _s52_setupVESSEL.i: setup VESSEL - no real AIS, fake target
//
// SD 2016APR26


#define VESSEL_TURN_UNDEFINED 129
#define VESSEL_LABEL "~~MV Non Such~~ "           // last char will be trimmed

static S52ObjectHandle _vessel_ais        = FALSE;

// test synthetic after glow on VESSEL
#ifdef S52_USE_AFGLOW
#define MAX_AFGLOW_PT (12 * 20)   // 12 min @ 1 vessel pos per 5 sec
//#define MAX_AFGLOW_PT 10        // debug
static S52ObjectHandle _vessel_ais_afglow = FALSE;
#endif  // S52_USE_AFGLOW

static int _s52_setupVESSEL(double cLat, double cLon)
{
    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _view.cLat + 0.01, _view.cLon - 0.02, 0.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 0.0);
    //S52_pushPosition(_vessel_ais, state->cLat - 0.04, state->cLon + 0.04, 0.0);
    S52_pushPosition(_vessel_ais, cLat - 0.01, cLon + 0.01, 45.0);
    S52_setVector(_vessel_ais, 1, 060.0, 16.0);   // ground

    // (re) set label
    S52_setVESSELlabel(_vessel_ais, VESSEL_LABEL);

    {
        //int vesselSelect = 0;  // OFF
        int vesselSelect = 1;  // ON
        int vestat       = 1; // AIS active
        //int vestat       = 2; // AIS sleeping
        //int vestat       = 3;  // AIS red, close quarters (compile with S52_USE_SYM_VESSEL_DNGHL)
        int vesselTurn   = VESSEL_TURN_UNDEFINED;
        S52_setVESSELstate(_vessel_ais, vesselSelect, vestat, vesselTurn);
    }

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_pushPosition(_vessel_ais, _view.cLat - 0.02, _view.cLon + 0.02, 0.0);

    // VTS (this will not draw anything!)
    //_vessel_vts = S52_newVESSEL(3, dummy);

#ifdef S52_USE_AFGLOW
    if (FALSE == _vessel_ais_afglow) {
        // afterglow
        _vessel_ais_afglow = S52_newMarObj("afgves", S52_LINES, MAX_AFGLOW_PT, NULL, NULL);
    }
#endif

    return TRUE;
}

static int _s52_updFakeAIS(double cLat, double cLon)
// fake one AIS
{
    if (FALSE != _vessel_ais) {
        gchar         str[80];
        GTimeVal      now;
        static double hdg = 0.0;

        hdg = (hdg >= 359.0) ? 0.0 : hdg+1;  // fake rotating hdg

        g_get_current_time(&now);
        g_sprintf(str, "%s %lis", VESSELLABEL, now.tv_sec);
        S52_setVESSELlabel(_vessel_ais, str);
        S52_pushPosition(_vessel_ais, cLat - 0.01, cLon + 0.01, hdg);
        S52_setVector(_vessel_ais, 1, hdg, 16.0);   // ground

#ifdef S52_USE_AFGLOW
        if (FALSE != _vessel_ais_afglow) {
            // stay at the same place but fill internal S52 buffer - in the search for possible leak
            S52_pushPosition(_vessel_ais_afglow, cLat, cLon, 0.0);
        }
#endif
    }

    return TRUE;
}
