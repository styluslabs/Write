package com.styluslabs.writeqt;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.view.KeyEvent;
import android.content.pm.ResolveInfo;
import android.media.AudioManager;
import android.widget.Toast;
import android.util.Log;
import android.Manifest;
import android.content.pm.PackageManager;
import androidx.core.content.FileProvider;
import android.content.ClipboardManager;
import android.content.ClipDescription;
import android.provider.Settings;

// common Android platform layer (ugui/platforms/android/)
public class MainActivity extends com.styluslabs.ugui.MainActivity
{
  static {
    // must load C++ runtime explicitly on API < 23
    try { System.loadLibrary("c++_shared"); } catch(UnsatisfiedLinkError e) {}
    System.loadLibrary("main");
  }

  // see androidhelper.cpp
  private static native void jniInsertImage(Bitmap bitmap, String mimetype, boolean fromintent);
  private static native void jniOpenFile(String filename);
  private static native void jniOpenFileDesc(String filename, int fd);
  private static native int  jniNotify(int code);

  // multiplex simple actions into a single method (jniNotify) to make adding new ones easier
  // these must match the values in androidhelper.h
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
  protected void onNewIntent(Intent intent)
  {
    String action = intent.getAction();
    if(Intent.ACTION_SEND.equals(action)) {
      if(intent.getType() != null && intent.getType().startsWith("image/")) {
        final Uri imageURI = (Uri)intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if(doInsertImage(new ImageInputStreamFactory() {
            public InputStream getStream() throws FileNotFoundException {
              return getContentResolver().openInputStream(imageURI);
            }
          }, true)) {
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

  @Override
  protected boolean wantKey(int keyCode)
  {
    if(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
      return jniNotify(A_VOL_KEYS) != 0;
    return super.wantKey(keyCode);
  }

  private String packageId()
  {
    return getApplicationContext().getPackageName();  //BuildConfig.APPLICATION_ID
  }

  // called from app thread
  public int doAction(int actionid)
  {
    if(actionid == A_CLICK_SOUND) {
      AudioManager am = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
      am.playSoundEffect(AudioManager.FX_KEY_CLICK);
    }
    else if(actionid == A_KEEP_SCREEN_ON) {
      keepScreenOn(true);
    }
    else if(actionid == A_REQ_PERM) {
      runOnUiThread(new Runnable() { @Override public void run() { requestStoragePermission(); } });
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
        if(desc != null && android.os.Build.VERSION.SDK_INT >= 26)
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

  private void requestStoragePermission()
  {
    if(android.os.Build.VERSION.SDK_INT >= 30) {
      Uri uri = Uri.parse("package:" + packageId());
      Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, uri);
      startActivity(intent);
      // must restart app for MANAGE_EXTERNAL_STORAGE permission to take effect
      System.exit(0);  //finishAffinity(); -- got messed up graphics in same cases
    }
    else if(android.os.Build.VERSION.SDK_INT >= 23) {
      requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, A_REQ_PERM);
    }
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

  // called from app thread at end of ScribbleApp::init()
  public void processInitialIntent()
  {
    runOnUiThread(new Runnable() { @Override public void run() { onNewIntent(getIntent()); } });
  }

  public void sendDocument(String filepath, String mimetype, String title)
  {
    final String finaltitle = title;
    String authority = packageId() + ".fileprovider";
    File file = new File(filepath);
    Uri contentUri = FileProvider.getUriForFile(this, authority, file);
    final Intent intent = new Intent(android.content.Intent.ACTION_SEND);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    intent.putExtra(Intent.EXTRA_SUBJECT, title);
    intent.putExtra(Intent.EXTRA_STREAM, contentUri);
    intent.setType(mimetype);
    startActivity(Intent.createChooser(intent, finaltitle));
  }

  // loading raw resources from APK
  public boolean assetToFile(String resname, String outfile)
  {
    try {
      File file = new File(outfile);
      // ensure that path exists
      if(!file.exists())
        file.getParentFile().mkdirs();
      FileOutputStream out = new FileOutputStream(file);
      InputStream in = getAssets().open(resname);
      byte[] buf = new byte[65536];
      int len;
      while((len = in.read(buf)) > 0)
        out.write(buf, 0, len);
      in.close();
      out.close();
      return true;
    }
    catch(IOException e) {
      return false;
    }
  }

  // image insertion

  private static final int ID_GETIMG = 1022;

  private interface ImageInputStreamFactory {
    InputStream getStream() throws FileNotFoundException;
  }

  // ref: http://stackoverflow.com/questions/4455558/allow-user-to-select-camera-or-gallery-for-image
  public void combinedGetImage()
  {
    final File cameraFile = new File(getExternalCacheDir(), "_camera.jpg");
    cameraFile.delete();  // remove existing
    String authority = packageId() + ".fileprovider";
    Uri outputFileUri = FileProvider.getUriForFile(this, authority, cameraFile);
    // capture image (camera) intents - get all and add to list
    final java.util.List<Intent> cameraIntents = new java.util.ArrayList<Intent>();
    final Intent captureIntent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
    final java.util.List<ResolveInfo> listCam = getPackageManager().queryIntentActivities(captureIntent, 0);
    for(ResolveInfo res : listCam) {
      final Intent intent = new Intent(captureIntent);
      intent.setComponent(new android.content.ComponentName(res.activityInfo.packageName, res.activityInfo.name));
      intent.setPackage(res.activityInfo.packageName);
      intent.putExtra(MediaStore.EXTRA_OUTPUT, outputFileUri);
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
}
