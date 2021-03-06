
package com.yarlungsoft.ams;

import java.io.IOException;
import java.net.ota.OTADownload;

import com.yarlungsoft.util.Log;

/**
 * The first class loaded in VM by main.c to perform a MIDP command.
 * <p>
 * This class performs 6 basic commands:
 * <ul>
 * <li>Application installation
 * <li>Application removal
 * <li>Application listing
 * <li>Application execution
 * <li>Graphical Application Management
 * <li>Execute a single Applet from the classpath
 * </ul>
 * For the case of executing an installed applet, the wrapper c code in main.c takes care of adding
 * the appropriate .jar file to the class path.
 * <p>
 * One extra duty for this class is to reset the VM in a callback.
 */
public class Main {
	
	public static final int NATIVE_NEXT_SCDH_STATE_NULL = 0x00;
	public static final int NATIVE_NEXT_SCDH_STATE_TCK  = 0x01;

    private static final String AMS_NAME = "AppletsManager";
    private static final String TAG = "Main";
    private static CommandProcessor sCmdState;

    public static void main(String[] args) {
        try {
            for (int i = 0; i < args.length; i++) {
                Log.amsLog(TAG, "args[" + i + "] = " + args[i]);
            }

            sCmdState = new CommandProcessor();
            sCmdState.setCurCmd(CommandProcessor.CMD_EXIT);
            sCmdState.parseCommandArgs(args);

            switch (sCmdState.getCurCmd()) {
            case CommandProcessor.CMD_RUN:
                launchApp(sCmdState.getMainClsName());
                break;

            case CommandProcessor.CMD_LIST:
            case CommandProcessor.CMD_INSTALL:
                launchAms();
                break;

            case CommandProcessor.CMD_OTA:
                OTADownload ota = new OTADownload(sCmdState.getInstallURL());
                ota.OTAStart();
                break;

            case CommandProcessor.CMD_TCK:
                TCKRunner tckrunner = new TCKRunner(sCmdState.getInstallURL());
                tckrunner.start();
                Log.amsLog(TAG, "launch tck runner!");
                break;

            default:
            case CommandProcessor.CMD_EXIT:
                break;
            }
        } catch (Exception e) {

        }
        Log.amsLog(TAG, "dvm main over!");
    }

    protected static CommandProcessor getCmdState() {
        return sCmdState;
    }

    private static void launchAms() {
        try {
            AppletContent ac = new AppletContent(AMS_NAME, "com.yarlungsoft.ams.AppletsManager");
            Scheduler.schedule(ac);
            Log.amsLog(TAG, ">>>>>>>>>>>>>>launchAMS end");
        } catch (Throwable t) {
            Log.amsLog(TAG, "launch AMS failure!");
        }
    }

    public static void launchApp(String mClsName) {
        AppletContent ac = null;
        /* how to get App Name in java side? */
        ac = new AppletContent("test", mClsName);
        try {
            Scheduler.schedule(ac);
        } catch (Throwable t) {
            Log.amsLog(TAG, "launch APP failure!");
        }
    }
    
    public static void launchApp(String mClsName, String mClsPath) {
        AppletContent ac = null;
        /* how to get App Name in java side? */
        loadClassByPath(mClsPath);
        ac = new AppletContent("test", mClsName);
        try {
            Scheduler.schedule(ac);
        } catch (Throwable t) {
            Log.amsLog(TAG, "launch APP failure!");
        }
    }
    
    public static void setNextSchedulerState(int state){
    	Log.amsLog(TAG, "set next state: " + state + "\n");
    	setNextSchedulerState0(state);
    }
    
    private static void loadClassByPath(String path){
    	Log.amsLog(TAG, "load APP path: " + path + "\n");
    	loadClassByPath0(path);    	
    }
    
    private static native void loadClassByPath0(String path);
    private static native void setNextSchedulerState0(int state);
}
