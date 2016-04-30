// _s52_setupVRMEBL.i: setup VRM/EBL
//
// SD 2016APR27


// test - VRMEBL
// S52 object name:"ebline"
static S52ObjectHandle _vrmeblA = FALSE;

// test - cursor DISP 9 (instead of IHO PLib DISP 8)
// need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = FALSE;  // 2 - open cursor

// ebline draw flag (F4 toggle ON/OFF)
//static int _drawVRMEBL = TRUE;
static int _drawVRMEBL = FALSE;

//static int _originIsSet = FALSE;  //for VRMEBL


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

    if (FALSE == _drawVRMEBL) {
        // suppression ON
        S52_setS57ObjClassSupp("cursor", TRUE);
        S52_setS57ObjClassSupp("ebline", TRUE);
        S52_setS57ObjClassSupp("vrmark", TRUE);

        // or supp one obj
        //S52_toggleDispMarObj(_cursor2);
        //S52_toggleDispMarObj(_vrmeblA);
    }

    return TRUE;
}

#if 0
static int      _setVRMEBL()
{
    //int vrm             = TRUE;
    //int ebl             = FALSE;
    //int normalLineStyle = FALSE;
    //int setOrigin       = FALSE;

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

    return TRUE;
}

static int      _s52_setupVRMEBL(s52droid_state_t *state)
{
    //char *attVal   = NULL;      // ordinary cursor
    char  attVal[] = "cursty:2,_cursor_label:0.0N 0.0W";  // open cursor
    double xyz[3] = {state->cLon, state->cLat, 0.0};
    int S52_VRMEBL_vrm = TRUE;
    int S52_VRMEBL_ebl = TRUE;
    int S52_VRMEBL_sty = TRUE;  // normalLineStyle
    int S52_VRMEBL_ori = TRUE;  // (user) setOrigin

    _cursor2 = S52_newMarObj("cursor", S52_POINT, 1, xyz, attVal);
    //int ret = S52_setS57ObjClassSupp("cursor");
    //g_print("_s52_setupVRMEBL(): S52_setS57ObjClassSupp('cursor'); ret=%i\n", ret);
    //int ret = S52_setS57ObjClassSupp("cursor");
    //g_print("_s52_setupVRMEBL(): S52_setS57ObjClassSupp('cursor'); ret=%i\n", ret);


    _vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    // suppression ON
    S52_setS57ObjClassSupp("cursor", TRUE);
    S52_setS57ObjClassSupp("ebline", TRUE);
    S52_setS57ObjClassSupp("vrmark", TRUE);

    return TRUE;
}
#endif
