
package com.grinninglizard.UFOAttack;

import java.io.File;                
import java.io.FileDescriptor;
import java.io.FilenameFilter;
import java.io.IOException;

import javax.microedition.khronos.egl.EGL;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGL11;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.opengles.GL10;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.AssetFileDescriptor;
import android.content.res.Resources;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
//import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;

public class UFOActivity extends Activity  {
	
	private AssetFileDescriptor afd = null;
	public AssetFileDescriptor getAFD() { return afd; }
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        this.setVolumeControlStream(AudioManager.STREAM_MUSIC); // switch the hardware volume control to control the sound level
        
        // On the main thread, before we fire off the render thread:
        setWritePaths();
        loadUFOAssets();
       
        mGLView = new DemoGLSurfaceView(this);
        setContentView(mGLView);
    }
    
     @Override
    protected void onPause() {
    	 Log.v( "UFOATTACK", "Activity onPause" );
        super.onPause();
        mGLView.onPause();
    }

    @Override
    protected void onResume() {
    	Log.v( "UFOATTACK", "Activity onResume" );
        super.onResume();
        mGLView.onResume();
    }
    
    protected void onDestroy() {
    	super.onDestroy();
    	mGLView.destroy();
    	//logger.destroy();
    }
    
    private void loadUFOAssets() 
    {
    	// Get the path to the application resource.
	    String apkFilePath = null;
	    PackageManager packMgmr = getPackageManager();
	    try
	    {
	    	ApplicationInfo ppInfo = packMgmr.getApplicationInfo( "com.grinninglizard.UFOAttack", 0 );
	        apkFilePath = ppInfo.sourceDir;
	    }
	    catch( NameNotFoundException e){}
	    
    	Resources r = getResources();
    	afd = r.openRawResourceFd( R.raw.uforesource );
   		long offset = afd.getStartOffset();
        long fileSize = afd.getLength();
        UFORenderer.nativeResource( apkFilePath, offset, fileSize );
        
        String state = Environment.getExternalStorageState();
        boolean mExternalStorageAvailable = false;
        
        if (Environment.MEDIA_MOUNTED.equals(state) || Environment.MEDIA_MOUNTED_READ_ONLY.equals(state)) {
            // We can only read the media
            mExternalStorageAvailable = true;
        }
        
        if ( mExternalStorageAvailable ) {
	        File downloads = Environment.getExternalStoragePublicDirectory( Environment.DIRECTORY_DOWNLOADS );
	        
	        FilenameFilter filter = new FilenameFilter() {
	            public boolean accept(File dir, String name) {
	                return name.endsWith(".xwdb");
	            }
	        };	        	        
	        
	        String[] children = downloads.list( filter );
	        if ( children != null && children.length > 0 ) {
	        	for( int i=0; i<children.length; ++i ) {
	        		String p = downloads.getAbsolutePath() + "/" + children[i];
	        		UFORenderer.nativeDatabase( p );
	        	}
	        }
        }
	}
    
    private void setWritePaths() 
    {
    	File file = this.getFilesDir();
    	if ( file != null ) {
    		// Disable crash logger for now. The core code bugs are coming in from
    		// the much more reliable Win32 code, and this hasn't found any
    		// Android specific issues.
    		//logger = new CrashLogger( this, file.getAbsolutePath() );		// send crash logs
    		UFORenderer.nativeSavePath( file.getAbsolutePath() );
    	}
    }
   
    private DemoGLSurfaceView mGLView;
    //private CrashLogger logger = null;

    static {
        System.loadLibrary("ufoattack");
    }
}


class ScaleListener extends ScaleGestureDetector.SimpleOnScaleGestureListener 
{
	ScaleListener( DemoGLSurfaceView view ) {
		super();
		mView = view;
	}
	
    @Override
    public boolean onScale(ScaleGestureDetector detector) {
        //Log.v( "UFOATTACK", "Scale factor=" + (1.0f-detector.getScaleFactor()) );

    	// The would be so useful methods:
    	// 	getCurrentSpanX, getCurrentSpanY 
    	// don't seem to actually exist. A search on code.google.com reveals no hits as well. *sigh*

    	//Log.v("UFOATTACK",   "scale factor=" + (1.0f-detector.getScaleFactor())
    	//		           + " span=" + detector.getCurrentSpanX() + "," + detector.getCurrentSpanY() );
    			           
        mView.zoom( 1.0f - detector.getScaleFactor() );
        return true;
    }
    private DemoGLSurfaceView mView;
}


class DemoGLSurfaceView extends GLSurfaceView { 	//implements MultiTouchObjectCanvas<PinchPlane>  { 

	public DemoGLSurfaceView(UFOActivity ufoActivity) {
        super(ufoActivity);
        mRenderer = new UFORenderer( ufoActivity.getAFD().getFileDescriptor() );
        setRenderer(mRenderer);
        
        requestFocusFromTouch();
        setFocusableInTouchMode( true );
        
        mScaleDetector = new ScaleGestureDetector(ufoActivity, new ScaleListener( this ));
    }
	
	// @Override
	public void onPause() {
		super.onPause();
		queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_PAUSE, 0, 0, 0, 0 ) );
	}
	
	// @Override
	public void onResume() {
		super.onResume();
		queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_RESUME, 0, 0, 0, 0 ) );
	}
	
	public void destroy() {
		queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_DESTROY, 0, 0, 0, 0 ) );
	}
	
	public void zoom( float delta ) {
		// Once zooming kicks in, have to stop moving. Bug in battlescene 
		// can't handle zoom & pan at the same time. The drag is computed from
		// a relative starting coordinate that conflicts with the zoom height
		// getting changed.
		// It would be nice to fix this restriction.
		queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_ZOOM, GAME_ZOOM_PINCH, 0, 0, delta ) );
	}

	private static final int GAME_TAP_DOWN		= 0;
	private static final int GAME_TAP_MOVE		= 1;
	private static final int GAME_TAP_UP		= 2;
	private static final int GAME_TAP_CANCEL	= 3;
	
	private static final int GAME_ZOOM_DISTANCE = 0;
	private static final int GAME_ZOOM_PINCH 	= 1;
	
	
    private UFORenderer mRenderer;
    private ScaleGestureDetector mScaleDetector; 
	private boolean needToSendUpOrCancel = false;
	
	@Override
	public boolean onTrackballEvent(MotionEvent event) {
		
		// NEXUS-ONE does fine with 1.0, Droid Incredible likes the 0.5f
		// Very few trackballs left these days.
		float ATTENUATION = 0.5f;
		 
		if ( event.getY() != 0 )
			queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_ZOOM, GAME_ZOOM_DISTANCE, 0, 0, event.getY()*ATTENUATION ) );
		if ( event.getX() != 0 )
			queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_ROTATE, 0, 0, 0, event.getX()*90.0f*ATTENUATION ) );
		return true;
	}
	
    @Override
    public boolean onTouchEvent(MotionEvent event) {

    	// Feed info to our helper class:
        int action = event.getAction();
        int type = -1;
        int x = (int)event.getX();
        int y = (int)(getHeight() - event.getY());
        
    	if ( event.getPointerCount() > 1 && needToSendUpOrCancel ) {
   			queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_TAP, GAME_TAP_CANCEL, x, y, 0 ) );
   			needToSendUpOrCancel = false;
    	}
 
        mScaleDetector.onTouchEvent(event);

        if ( event.getPointerCount() == 1 ) {
	        switch ( action ) {
		    	case MotionEvent.ACTION_DOWN:
		        	type = GAME_TAP_DOWN;
		        	needToSendUpOrCancel = true;
		        	break;
		        	
		        case MotionEvent.ACTION_UP:
		        	if ( needToSendUpOrCancel ) {
		        		type = GAME_TAP_UP;
		        		needToSendUpOrCancel = false;
		        	}
		        	break;
		        	        	
		        case MotionEvent.ACTION_CANCEL:
		        	if ( needToSendUpOrCancel ) {
		        		type = GAME_TAP_CANCEL;
		        		needToSendUpOrCancel = false;
		        	}
		        	break;
		        	
		        case MotionEvent.ACTION_MOVE:
		        	if ( needToSendUpOrCancel ) {
		        		type = GAME_TAP_MOVE;
		        	}
		        	break;
	        }
	        if ( type >= 0 ) {
	        	queueEvent( new RendererEvent( mRenderer, RendererEvent.TYPE_TAP, type, x, y, 0 ) );
	        	return true;
	        }
        }
       	return super.onTouchEvent(event);
    }
}


/**
 * The renderer runs on a different thread than the UI. (<vent>Seems needlessly complex - why isn't the driver
 * on a separate thread and the renderer on the main thread?</vent>). Can't call it directly. No evidence it
 * is thread safe, and the underlying c++ code is NOT thread safe. Any communication to the renderer needs
 * to be through the RenderEvent class. 
 *
 */
final class RendererEvent implements Runnable
{
	public static final int TYPE_PAUSE		= 1;
	public static final int TYPE_RESUME 	= 2;
	public static final int TYPE_DESTROY	= 3;
	public static final int TYPE_TAP 		= 100;
	public static final int TYPE_HOTKEY 	= 101;
	public static final int TYPE_ZOOM		= 200;
	public static final int TYPE_ROTATE		= 201;
	
	private UFORenderer renderer;
	private int type;
	private int action;
	private int x;
	private int y;
	private float val;
	
	RendererEvent( UFORenderer renderer, int type, int action, int x, int y, float v ) 
	{
		this.renderer = renderer;
		this.type = type;
		this.action = action;
		this.x = x;
		this.y = y;
		this.val = v;
	}
	
	public void run() 
	{
		switch( type ) {
			case TYPE_PAUSE:
				renderer.pause();
				break;
			case TYPE_RESUME:
				renderer.resume();
				break;
			case TYPE_DESTROY:
				renderer.destroy();
				break;
			case TYPE_TAP:
				renderer.tap( action, (float)x, (float)y );
				//Log.v("UFOATTACK", "Tap action=" + action + " x=" + (int)x + " y=" + (int)y );
				break;
			case TYPE_HOTKEY:
				renderer.hotKey( action );
				break;
			case TYPE_ZOOM:
				renderer.zoom( action, val );
			case TYPE_ROTATE:
				renderer.rotate( val );
			default:
				break;
		}
	}
}


/* The renderer is on a separate thread. (oops.) Really
 * does need to be private, and communication is over
 * messages from queueEvent().
 */
class UFORenderer implements GLSurfaceView.Renderer {
	
	private FileDescriptor fd = null;
	private MediaPlayer[] player = new MediaPlayer[2];
	
	public UFORenderer( FileDescriptor fd ) {
		super();
		this.fd = fd;
		for( int i=0; i<player.length; ++i ) {
			player[i] = new MediaPlayer();
		}
	}
	
	public boolean HasContext() {
        EGL egl = EGLContext.getEGL();
        if ( egl instanceof EGL10 ) {
        	EGL10 egl10 = (EGL10)egl;
        	EGLContext context = egl10.eglGetCurrentContext();  
            return context != EGL10.EGL_NO_CONTEXT && egl10.eglGetError() != EGL11.EGL_CONTEXT_LOST;
        }
        return false;
	}

    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
    	Log.v("UFOJAVA", "onSurfaceCreated");
    	if ( HasContext() )
    		nativeInit();
    }

    public void onSurfaceChanged(GL10 gl, int w, int h) {
    	Log.v("UFOJAVA", "onSurfaceChanged");
    	if ( HasContext() )
    		nativeResize(w, h);
    }
    
    private long startTime = 0;
    private static long FRAME_TIME = 33;	// 30 fps

    public void onDrawFrame(GL10 gl) 
    {
    	// Implements a frame cap at ~30 fps. Since this isn't done
    	// by the OS (that API seems to be missing) it isn't super
    	// stable.
    	long endTime = System.currentTimeMillis();
    	long dt = endTime - startTime;
    	if ( dt < FRAME_TIME ) {
    		try {
				Thread.sleep( FRAME_TIME-dt );
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
    	}
    	startTime = System.currentTimeMillis();
    	
    	//Log.v("UFOJAVA", "onDrawFrame");
    	if ( HasContext() )
    		nativeRender();
    	
    	while ( true ) {
	    	long pop = nativeSoundPop();
	    	if ( pop == 0 )
	    		break;
    		
	    	int offset = (int)(pop & 0xffffffff);
	    	int size = (int)((pop>>32) & 0xffffffff);
    		// Play sound!!
	    	//Log.v("UFOJAVA", "play sound offest=" + offset + " size=" + size );

	    	boolean done = false;
	    	for( int i=0; i<player.length && !done; ++i ) {
	    		MediaPlayer mediaPlayer = player[i];
	    		
	    		if ( !mediaPlayer.isPlaying() ) {
			    	try {
						mediaPlayer.reset();
			    		mediaPlayer.setDataSource( fd, offset, size );
						mediaPlayer.prepare();
			    		mediaPlayer.start();
			    		done = true;
					} catch (IllegalArgumentException e) {
						Log.v("UFOATTACK", "audio playback failed - IllegalArgumentException" );
					} catch (IllegalStateException e) {
						Log.v("UFOATTACK", "audio playback failed - IllegalStateException" );
					} catch (IOException e) {
						Log.v("UFOATTACK", "audio playback failed - IOException" );
					}
	    		}
	    	}
    	}
    }
    
    public void pause() {
    	Log.v("UFOJAVA", "pause=1");
    	nativePause( 1 );
    }
    
    public void resume() {
    	Log.v("UFOJAVA", "pause=0");
    	nativePause( 0 );
    }
    
    public void destroy() {
    	Log.v("UFOJAVA", "destroy");
    	nativeDone();
    }
    
    public void tap( int action, float x, float y ) {
    	nativeTap( action, x, y );
    }
    
    public void hotKey( int key ) {
    	//Log.v( "UFOJAVA", "sending key=" + key );
    	nativeHotKey( key );
    }
    
    public void zoom( int style, float delta ) {
    	//Log.v("UFOATTACK", "zoom=" + delta );
    	nativeZoom( style, delta );
    }
    
    public void rotate( float delta ) {
    	nativeRotate( delta );
    }

    public static native void nativeResource( String path, long offset, long len );
    public static native void nativeSavePath( String path );
    public static native void nativeDatabase( String path );
    
    private static native void nativeResize(int w, int h);
    private static native void nativeRender();

    private static native void nativeInit();
    private static native void nativePause( int pause );
    private static native void nativeDone();
    
    private static native void nativeTap( int action, float x, float y );
    private static native void nativeHotKey( int key );
    
    private static native void nativeZoom( int style, float delta );
    private static native void nativeRotate( float delta );
    
    private static native long nativeSoundPop();		// pops the sound, returns the offset(low) & size(high)
}
