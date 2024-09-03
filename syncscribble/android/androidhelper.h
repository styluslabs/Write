#ifndef ANDROIDHELPER_H
#define ANDROIDHELPER_H

class ScribbleApp;

class AndroidHelper
{
public:
  static int doAction(int actionid);
  static void moveTaskToBack();
  static void processInitialIntent();
  static int detectPenType(int pentype);
  static void openUrl(const char* url);
  static void sendFile(const char* filename, const char* mimetype, const char* title);
  static void getImage();
  static bool rawResourceToFile(const char* resname, const char* outfile);
  static void blitSurface(void* pixels, int width, int height, int x, int y, int w, int h);

  static ScribbleApp* mainWindowInst;
  static bool acceptVolKeys;
};

// these action codes must match the values in MainActivity.java
static const int A_CLICK_SOUND = 1000;
static const int A_KEEP_SCREEN_ON = 1001;
static const int A_REQ_PERM = 1002;
static const int A_REQ_PERM_FAIL = 1003;
static const int A_VOL_KEYS = 1004;
static const int A_GPU_BLACKLIST = 1005;
static const int A_CLIPBOARD_SERIAL = 1006;
static const int A_CHECK_PERM = 1007;
#endif
