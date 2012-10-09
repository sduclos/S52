# run_sl4a_server.sh: start sl4a server

/system/bin/am start                                                                  \
        -a   com.googlecode.android_scripting.action.LAUNCH_SERVER                    \
        -n   com.googlecode.android_scripting/.activity.ScriptingLayerServiceLauncher \
        --ei com.googlecode.android_scripting.extra.USE_SERVICE_PORT 45001            \
        --activity-previous-is-top                        
