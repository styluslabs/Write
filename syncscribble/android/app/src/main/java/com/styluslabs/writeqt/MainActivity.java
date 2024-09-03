package com.styluslabs.writeqt;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.WindowManager;
import android.view.KeyEvent;
import android.view.Surface;
import android.graphics.Point;
import android.content.pm.ResolveInfo;
import android.media.AudioManager;
import android.widget.Toast;
import android.util.Log;
import android.Manifest;
import android.content.pm.PackageManager;
import android.support.v4.content.FileProvider;
import android.content.ClipboardManager;
import android.content.ClipDescription;
import android.provider.Settings;

// for onTouch
import android.view.View;
import android.view.MotionEvent;
import android.view.InputDevice;

// Changes needed in SDLActivity: see SDLActivity.diff
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity implements View.OnTouchListener, View.OnHoverListener
{
  private static native void jniInsertImage(Bitmap bitmap, String mimetype, boolean fromintent);
  private static native void jniOpenFile(String filename);
  private static native void jniOpenFileDesc(String filename, int fd);
  private static native int  jniNotify(int code);
  private static native void jniTouchEvent(
      int devId, int ptrId, int action, int t, float x, float y, float p, float major, float minor);

  // multiplex simple actions into a single method (jniNotify) to make adding new ones easier
  private static final int A_CLICK_SOUND = 1000;
  private static final int A_KEEP_SCREEN_ON = 1001;
  private static final int A_REQ_PERM = 1002;
  private static final int A_REQ_PERM_FAIL = 1003;
  private static final int A_VOL_KEYS = 1004;
  private static final int A_GPU_BLACKLIST = 1005;
  private static final int A_CLIPBOARD_SERIAL = 1006;
  private static final int A_CHECK_PERM = 1007;

  private ClipboardManager mClipboardMgr = null;

  @Override
  protected void onCreate(Bundle savedstate)
  {
    super.onCreate(savedstate);
    getSDLSurfaceView().setOnTouchListener(this);
    getSDLSurfaceView().setOnHoverListener(this);
    //m_instance = this;
    //hasDirectStylus = getPackageManager().hasSystemFeature("com.nvidia.nvsi.feature.DirectStylus");
    // reference: http://developer.android.com/training/sharing/receive.html
    // onNewIntent(getIntent());
    //android.util.Log.d("onCreate", "Hello!");
  }

  @Override
  protected void onNewIntent(Intent intent)
  {
    //android.util.Log.d("onNewIntent", "Action: " + intent.getAction());
    String action = intent.getAction();
    if(Intent.ACTION_SEND.equals(action)) {
      if(intent.getType() != null && intent.getType().startsWith("image/")) {
        final Uri imageURI = (Uri)intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if(doInsertImage(new ImageInputStreamFactory() {
            public InputStream getStream() throws FileNotFoundException {
              return getContentResolver().openInputStream(imageURI);
            }
          }, true)) {
          // TODO: this should be done from Qt
          Toast.makeText(this, "Image copied to clipboard. Paste where desired.", Toast.LENGTH_SHORT).show();
        }
      }
    }
    else if(Intent.ACTION_VIEW.equals(action) || Intent.ACTION_EDIT.equals(action)) {
      if(("text/html".equals(intent.getType()) || "image/svg+xml".equals(intent.getType()))
          && intent.getData() != null) {
        if(intent.getData().toString().startsWith("content://")) {
          try {
            // openFileDescriptor only works for mode="r", but /proc/self/fd/<fd> gives us symlink to actual
            //  file which we can open for writing
            // an alternative would be to use android.system.Os.readlink here instead of in androidhelper.cpp
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(intent.getData(), "r");
            jniOpenFileDesc(intent.getData().getPath(), pfd.getFd());
            pfd.close();
          } catch(Exception e) {
            Log.v("onNewIntent", "Error opening document: " + intent.getData().toString(), e);
          }
        }
        else
          jniOpenFile(intent.getData().getPath());
      }
    }
    else
      super.onNewIntent(intent);
  }

//~   @Override
//~   protected void onResume()
//~   {
//~     super.onResume();
//~     if(hasDirectStylus) {
//~       Intent intent = new Intent("com.nvidia.intent.action.ENABLE_STYLUS");
//~       intent.putExtra("package", getPackageName());
//~       sendBroadcast(intent);
//~     }
//~   }

  @Override
  public boolean dispatchKeyEvent(KeyEvent event)
  {
    if (SDLActivity.mBrokenLibraries) { return false; }
    int k = event.getKeyCode();
    // Ignore certain special keys so they're handled by Android
    if (k == KeyEvent.KEYCODE_CAMERA || k == KeyEvent.KEYCODE_ZOOM_IN || k == KeyEvent.KEYCODE_ZOOM_OUT) {
      return false;
    }
    else if(k == KeyEvent.KEYCODE_VOLUME_DOWN || k == KeyEvent.KEYCODE_VOLUME_UP) {
      if(jniNotify(A_VOL_KEYS) == 0)
        return false;
    }
    return super.dispatchKeyEvent(event);
  }

  public void sendToBack()
  {
    //if(m_instance != null)
    //  m_instance.moveTaskToBack(true);
    moveTaskToBack(true);
  }

//~   public static int doAction(int actionid)
//~   {
//~     return m_instance != null ? m_instance.doActionInst(actionid) : -1;
//~   }

  private String packageId()
  {
    return getContext().getApplicationContext().getPackageName();  //BuildConfig.APPLICATION_ID
  }

  public int doAction(int actionid)
  {
    if(actionid == A_CLICK_SOUND) {
      AudioManager am = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
      am.playSoundEffect(AudioManager.FX_KEY_CLICK);
      // crashes JVM for unknown reasons
      //findViewById(android.R.id.content).playSoundEffect(android.view.SoundEffectConstants.CLICK);
    }
    else if(actionid == A_KEEP_SCREEN_ON) {
      getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    else if(actionid == A_REQ_PERM) {
      // request external storage permission
      if(android.os.Build.VERSION.SDK_INT >= 30) {
        Uri uri = Uri.parse("package:" + packageId());
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, uri);
        startActivity(intent);
        // must restart app for MANAGE_EXTERNAL_STORAGE permission to take effect
        System.exit(0);  //finishAffinity(); -- got messed up graphics in same cases
        //try { } catch (Exception ex) {}
      }
      else if(android.os.Build.VERSION.SDK_INT >= 23) {
        // ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.STORAGE)
        requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, A_REQ_PERM);
      }
    }
    else if(actionid == A_GPU_BLACKLIST) {
      String model = android.os.Build.MODEL;
      // blacklist Galaxy Tab A, Mediapad M5 lite
      return model.startsWith("SM-T5") || model.startsWith("SM-P5") || model.startsWith("BAH2") ? 1 : 0;
    }
    else if(actionid == A_CLIPBOARD_SERIAL) {
      if(mClipboardMgr == null)
        mClipboardMgr = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
      if(mClipboardMgr != null) {
        ClipDescription desc = mClipboardMgr.getPrimaryClipDescription();
        if(desc != null)
          return (int) (desc.getTimestamp() & Integer.MAX_VALUE);
      }
      return -1;
    }
    else if(actionid == A_CHECK_PERM) {
      int res = 0;  // 0x1 - WRITE_EXTERNAL_STORAGE, 0x2 - API level >= 30, 0x4 - MANAGE_EXTERNAL_STORAGE
      if(android.os.Build.VERSION.SDK_INT >= 30)
        res += Environment.isExternalStorageManager() ? 6 : 2;
      else if(android.os.Build.VERSION.SDK_INT >= 23)
        res += checkSelfPermission(
            Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED ? 1 : 0;
      return res;
    }

    return 0;
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String permissions[], int[] grantResults)
  {
    if(requestCode == A_REQ_PERM) {
      if(grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED)
        jniNotify(A_REQ_PERM);
      else
        jniNotify(A_REQ_PERM_FAIL);
    }
    else
      super.onRequestPermissionsResult(requestCode, permissions, grantResults);
  }

  // called at end of MainWindow constructor
  //  doesn't work to call onNewIntent from onCreate - maybe Qt isn't done initializing?
  public void processInitialIntent()
  {
    //if(m_instance != null)
    //  m_instance.onNewIntent(m_instance.getIntent());
    onNewIntent(getIntent());
  }

//~   public static void sendDocument(String filepath, String mimetype, String title)
//~   {
//~     if(m_instance != null)
//~       m_instance.sendDocumentInst(filepath, mimetype, title);
//~   }

  public void sendDocument(String filepath, String mimetype, String title)
  {
    final String finaltitle = title;
    String authority = packageId() + ".fileprovider";
    File file = new File(filepath);
    Uri contentUri = FileProvider.getUriForFile(getContext(), authority, file);
    final Intent intent = new Intent(android.content.Intent.ACTION_SEND);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    intent.putExtra(Intent.EXTRA_SUBJECT, title);
    intent.putExtra(Intent.EXTRA_STREAM, contentUri);
    intent.setType(mimetype);
    startActivity(Intent.createChooser(intent, finaltitle));
  }

  public void openUrl(String url)
  {
    Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
    startActivity(viewUrlIntent);
  }

  public Surface getSDLSurface()
  {
    return getSDLSurfaceView().getHolder().getSurface();
  }

  /*public static float getScreenDPI()
  {
    return m_instance != null ? m_instance.getScreenDPIInst() : 0.0f;
  }

  public float getScreenDPIInst()
  {
    DisplayMetrics metrics = new DisplayMetrics();
    getWindowManager().getDefaultDisplay().getMetrics(metrics);
    return metrics.xdpi;
  }

  public static int detectScreenSize()
  {
    return m_instance != null ? m_instance.detectScreenSizeInst() : 0;
  }

  public int detectScreenSizeInst()
  {
    Display display = getWindowManager().getDefaultDisplay();
    DisplayMetrics metrics = new DisplayMetrics();
    display.getMetrics(metrics);
    // depends on orientation
    int rawwidth = metrics.widthPixels;
    int rawheight = metrics.heightPixels;
    try {
      Point rawsize = new Point(rawwidth, rawheight);
      // documented method added at API level 17
      Method mGetRealSize = Display.class.getMethod("getRealSize");
      mGetRealSize.invoke(display);
      rawwidth = rawsize.x;
      rawheight = rawsize.y;
    }
    catch(Exception e1) {
      try {
        // undocumented methods removed at API level 17
        Method mGetRawH = Display.class.getMethod("getRawHeight");
        Method mGetRawW = Display.class.getMethod("getRawWidth");
        rawwidth = (Integer) mGetRawW.invoke(display);
        rawheight = (Integer) mGetRawH.invoke(display);
      }
      catch(Exception e2) {}
    }
    //int pagewidth = (int)(Math.min(rawwidth, rawheight)/metrics.density) - 24;
    //int pageheight = (int)(Math.max(rawwidth, rawheight)/metrics.density) - 24;
    // pack dimensions into an int
    return (rawwidth << 16) + rawheight;
  }

  public static int detectPenType(int pentype)
  {
    return m_instance != null ? m_instance.detectPenTypeInst(pentype) : 0;
  }

  // these must match values in scribbleinput.h
  private static final int THINKPAD_PEN = 1;
  private static final int ICS_PEN = 2;
  private static final int SAMSUNG_PEN = 3;
  private static final int TEGRA_PEN = 4;
  // The original value of DETECTED_PEN was 10; everytime we make changes that allow for more specific
  //  pen detection, we should increment the value of DETECTED_PEN, which will result in detectPenType()
  //  being rerun once
  private static final int REDETECT_PEN = 10;
  private static final int DETECTED_PEN = 11;

  // still need to handle detected pen if we get a QTabletEvent
  public int detectPenType(int mPenType)
  {
    // first, do generic pen detection
    if(getPackageManager().hasSystemFeature("android.hardware.touchscreen.pen")) {
      // Quill checks a list to determine if pressure sensitivity is available; any way to get this from OS?
      mPenType = ICS_PEN;
    }
    else if(mPenType >= REDETECT_PEN && mPenType < DETECTED_PEN)
      mPenType = DETECTED_PEN;

    // We could also iterate over InputDevice.getDeviceIds(), checking getDevice().getSources()
    // now try to detect specific devices
    String model = android.os.Build.MODEL;
    if(model.equalsIgnoreCase("ThinkPad Tablet")) {
      // Lenovo ThinkPad Tablet: pressure sensitive pen
      mPenType = THINKPAD_PEN;
    }
    else if(model.startsWith("GT-N") || model.startsWith("SM-P") || model.startsWith("SM-N")
        || (mPenType > 0 && (android.os.Build.BRAND.equalsIgnoreCase("Samsung")
        || android.os.Build.MANUFACTURER.equalsIgnoreCase("Samsung")))) {
      // international galaxy note is GT-N7000, note 2 is GT-N7100, Note 10.1 is GT-N8013
      // Note 10.1 2014 models are SM-P6xx
      // Note 3 phones are SM-N9xxx
      // US Note phones have a ton of different model numbers
      mPenType = SAMSUNG_PEN;
    }
    else if(model.startsWith("TegraNote")
        || getPackageManager().hasSystemFeature("com.nvidia.nvsi.feature.DirectStylus")
        || getPackageManager().hasSystemFeature("com.nvidia.nvsi.product.TegraNOTE7")) {
      // need to rescale pressure for TegraNote/DirectStylus
      mPenType = TEGRA_PEN;
    }
    //for(android.content.pm.FeatureInfo f : getPackageManager().getSystemAvailableFeatures())
    //if(f.name.contains("com.nvidia.nvsi.product.TegraNOTE"))
    //mPenType = TEGRA_PEN;

    // enable pan from edge by default if no pen
    //if(mPenType == 0 && !mPrefs.contains("panFromEdge"))
    //  prefEditor.putBoolean("panFromEdge", true);
    return mPenType;
  }

  public static boolean rawResourceToFile(String resname, String outfile)
  {
    return m_instance != null ? m_instance.rawResourceToFileInst(resname, outfile) : false;
  }*/

  // loading raw resources from APK
  public boolean assetToFile(String resname, String outfile)
  {
    try {
      File file = new File(outfile);
      // ensure that path exists
      if(!file.exists())
        file.getParentFile().mkdirs();
      FileOutputStream out = new FileOutputStream(file);
      // this returns InputStream object
      InputStream in = getAssets().open(resname);
      // copy byte by byte ... doesn't seem to be a more elegant soln!
      byte[] buf = new byte[65536];
      int len;
      while((len = in.read(buf)) > 0)
        out.write(buf, 0, len);
      in.close();
      out.close();
      return true;
    }
    catch(IOException e) {
      // oh well, no tips
      return false;
    }
  }

  // image insertion

  private static final int ID_SELECTIMG = 1020;
  private static final int ID_CAPTUREIMG = 1021;
  private static final int ID_GETIMG = 1022;

  // Thanks Java!
  private interface ImageInputStreamFactory {
    InputStream getStream() throws FileNotFoundException;
  }

  // ref: http://stackoverflow.com/questions/4455558/allow-user-to-select-camera-or-gallery-for-image
  public void combinedGetImage()
  {
    /* / Test system file picker
    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
    intent.addCategory(Intent.CATEGORY_OPENABLE);
    intent.setType("application/pdf");
    ///Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
    //intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, pickerInitialUri);
    startActivityForResult(intent, 3853);  //PICK_PDF_FILE);  PICK_PDF_FILE = 2;
    */
    final File cameraFile = new File(getExternalCacheDir(), "_camera.jpg");
    cameraFile.delete();  // remove existing
    //Uri outputFileUri = Uri.fromFile(cameraFile);  -- stopped working in Android 11
    String authority = packageId() + ".fileprovider";
    Uri outputFileUri = FileProvider.getUriForFile(getContext(), authority, cameraFile);
    // capture image (camera) intents - get all and add to list
    final java.util.List<Intent> cameraIntents = new java.util.ArrayList<Intent>();
    final Intent captureIntent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
    final java.util.List<ResolveInfo> listCam = getPackageManager().queryIntentActivities(captureIntent, 0);
    for(ResolveInfo res : listCam) {
      final Intent intent = new Intent(captureIntent);
      intent.setComponent(new android.content.ComponentName(res.activityInfo.packageName, res.activityInfo.name));
      intent.setPackage(res.activityInfo.packageName);
      intent.putExtra(MediaStore.EXTRA_OUTPUT, outputFileUri);
      //intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
      cameraIntents.add(intent);
    }

    // select image intents
    final Intent galleryIntent = new Intent();
    galleryIntent.setType("image/*");
    galleryIntent.setAction(Intent.ACTION_PICK);  // ACTION_GET_CONTENT shows generic file chooser

    // combined intent
    final Intent chooserIntent = Intent.createChooser(galleryIntent, "Select Image");
    // prepend the camera options
    chooserIntent.putExtra(Intent.EXTRA_INITIAL_INTENTS, cameraIntents.toArray(new android.os.Parcelable[]{}));
    startActivityForResult(chooserIntent, ID_GETIMG);
  }

  private boolean doInsertImage(ImageInputStreamFactory streamfactory, boolean fromintent)
  {
    BitmapFactory.Options opt = new BitmapFactory.Options();
    opt.inPreferredConfig = Bitmap.Config.ARGB_8888;
    opt.inSampleSize = 1;
    Bitmap img = null;
    do {
      try {
        img = BitmapFactory.decodeStream(streamfactory.getStream(), null, opt);
      }
      catch(OutOfMemoryError e) {}
      catch(Exception e) {
        Log.v("doInsertImage", "Exception decoding image: ", e);
        break;
      }
      opt.inSampleSize *= 2;
    } while(img == null && opt.inSampleSize <= 16);
    if(img != null && img.getWidth() > 0 && img.getHeight() > 0)
      jniInsertImage(img, opt.outMimeType, fromintent);
    else {
      Toast.makeText(this, "Error opening image", Toast.LENGTH_SHORT).show();
      return false;
    }
    return true;
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent intent)
  {
    if(requestCode == ID_GETIMG && resultCode == RESULT_OK) {
      // MediaStore.ACTION_IMAGE_CAPTURE.equals(intent.getAction() no longer seems to work
      final File cameraFile = new File(getExternalCacheDir(), "_camera.jpg");
      if(cameraFile.length() > 0) {
        doInsertImage(new ImageInputStreamFactory() {
          public InputStream getStream() throws FileNotFoundException {
            return new FileInputStream(cameraFile);
          }
        }, false);
      }
      else if(intent != null && intent.getData() != null) {
        final Uri streamuri = intent.getData();
        doInsertImage(new ImageInputStreamFactory() {
          public InputStream getStream() throws FileNotFoundException {
            return getContentResolver().openInputStream(streamuri);
          }
        }, false);
      }
    }
    else
      super.onActivityResult(requestCode, resultCode, intent);
  }

  private boolean penBtnPressed = false;

  //@ANDROID-14
  private void sendTouchEvent(MotionEvent event, int action, int i, long tt, float x, float y, float p)
  {
    int t = (int)(tt % Integer.MAX_VALUE);
    int tool = event.getToolType(i);
    if (p > 1.0f) { p = 1.0f; } // p = 1.0 is "normal" pressure, so >1 is possible
    switch (tool) {
    case MotionEvent.TOOL_TYPE_STYLUS:
    case MotionEvent.TOOL_TYPE_ERASER:
      jniTouchEvent(tool, penBtnPressed || event.getButtonState() != 0 ? 4 : 0, action, t, x, y, p, 0, 0);
      break;
    default:
      jniTouchEvent(tool, event.getPointerId(i), action, t, x, y, p, event.getTouchMajor(i), event.getTouchMinor(i));
    }
    if(action == MotionEvent.ACTION_UP)
      penBtnPressed = false;
  }

  // Touch events
  @Override
  public boolean onTouch(View v, MotionEvent event)
  {
    // Ref: http://developer.android.com/training/gestures/multi.html
    final int pointerCount = event.getPointerCount();
    int action = event.getActionMasked();
    int mouseButton;
    int i = -1, j = -1;

    // 12290 = Samsung DeX mode desktop mouse
    if ((event.getSource() == InputDevice.SOURCE_MOUSE || event.getSource() == 12290)) {
      try {
        mouseButton = (Integer) event.getClass().getMethod("getButtonState").invoke(event);
      } catch(Exception e) {
        mouseButton = 1;  // oh well.
      }
      // We need to check if we're in relative mouse mode and get the axis offset rather than the x/y values
      // if we are. We'll leverage our existing mouse motion listener
      //org.libsdl.app.SDLGenericMotionListener_API12 motionListener = SDLActivity.getMotionListener();
      //float x = motionListener.getEventX(event);
      //float y = motionListener.getEventY(event);
      SDLActivity.onNativeMouse(mouseButton, action, event.getX(0), event.getY(0), false);
    } else {
      switch(action) {
      case 213:  // Galaxy Note pen motion w/ button pressed (some devices)
        action = MotionEvent.ACTION_MOVE;
      case MotionEvent.ACTION_MOVE:
        for (j = 0; j < event.getHistorySize(); j++) {
          for (i = 0; i < pointerCount; i++) {
            sendTouchEvent(event, action, i, event.getHistoricalEventTime(j), event.getHistoricalX(i, j),
                event.getHistoricalY(i, j), event.getHistoricalPressure(i, j));
          }
        }
        for (i = 0; i < pointerCount; i++) {
          sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
        }
        break;
      case 211:  // Galaxy Note press w/ pen button pressed
      case 212:  // Galaxy Note release w/ pen button pressed
        action = (action == 211) ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP;
      case MotionEvent.ACTION_UP:
      case MotionEvent.ACTION_DOWN:
        // Primary pointer up/down, the index is always zero
        i = 0;
      case MotionEvent.ACTION_POINTER_UP:
      case MotionEvent.ACTION_POINTER_DOWN:
        // Non primary pointer up/down
        if (i == -1) {
          i = event.getActionIndex();
        }
        sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
        break;
      case MotionEvent.ACTION_CANCEL:
        for (i = 0; i < pointerCount; i++) {
          sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
        }
        break;
      default:
        break;
      }
    }
    return true;
  }

  // accept hover events w/ button pressed (needed for s-pen)
  // Note that MotionEvent.ACTION_HOVER_MOVE events are delivered to onGenericMotionEvent, not onTouchEvent
  @Override
  public boolean onHover(View v, MotionEvent event)
  {
    penBtnPressed = event.getButtonState() != 0;
    return true;
  }
}
