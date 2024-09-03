#include "androidhelper.h"
#include <jni.h>
#include <unistd.h>
#include "ugui/svggui.h"
#include "android/bitmap.h"
#include "android/native_window_jni.h"
#include "scribbleapp.h"

ScribbleApp* AndroidHelper::mainWindowInst = NULL;
bool AndroidHelper::acceptVolKeys = false;
static const char className[] = "com/styluslabs/writeqt/MainActivity";

// Note: see https://stackoverflow.com/questions/27424633/java-callback-from-c-with-android-and-sdl
//  if you run into any problems

// calls to Java
class AndroidMethod
{
public:
  JNIEnv* env = NULL;
  jobject activity = NULL;
  jclass clazz = NULL;
  jmethodID method_id = NULL;

  AndroidMethod(const char* name, const char* sig)
  {
    // retrieve the JNI environment
    env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if(!env) return;
    // retrieve the Java instance of the SDLActivity
    activity = (jobject)SDL_AndroidGetActivity();
    if(!activity) return;
    // find the Java class of the activity. It should be SDLActivity or a subclass of it.
    clazz = env->GetObjectClass(activity);
    if(!clazz) return;
    // find the identifier of the method to call
    method_id = env->GetMethodID(clazz, name, sig);
  }

  ~AndroidMethod()
  {
    // clean up the local references.
    if(activity)
      env->DeleteLocalRef(activity);
    if(clazz)
      env->DeleteLocalRef(clazz);
  }
};


void AndroidHelper::moveTaskToBack()
{
  AndroidMethod fn("sendToBack", "()V");
  if(!fn.method_id) return;
  fn.env->CallVoidMethod(fn.activity, fn.method_id);
}

int AndroidHelper::doAction(int actionid)
{
  AndroidMethod fn("doAction", "(I)I");
  if(!fn.method_id) return -1;
  return fn.env->CallIntMethod(fn.activity, fn.method_id, actionid);
}

void AndroidHelper::processInitialIntent()
{
  AndroidMethod fn("processInitialIntent", "()V");
  if(!fn.method_id) return;
  fn.env->CallVoidMethod(fn.activity, fn.method_id);
}

void AndroidHelper::blitSurface(void* pixels, int width, int height, int x, int y, int w, int h)
{
  AndroidMethod fn("getSDLSurface", "()Landroid/view/Surface;");
  if(!fn.method_id) return;
  jobject jsurface = fn.env->CallObjectMethod(fn.activity, fn.method_id);
  ANativeWindow* awin = ANativeWindow_fromSurface(fn.env, jsurface);
  ANativeWindow_Buffer info;
  //if(ANativeWindow_getWidth(awin) != width || ANativeWindow_getHeight(awin) != height)
  ARect adirty = {x, y, x + w, y + h};
  if(ANativeWindow_lock(awin, &info, &adirty) >= 0) {
    if(info.width != width || info.height != height)
      PLATFORM_LOG("ANativeWindow size mismatch!\n");
    else if(info.format == WINDOW_FORMAT_RGB_565)
      PLATFORM_LOG("ANativeWindow is RGB565!\n");
    else {
      // we're assuming 32 bit pixels
      uint32_t* dst = (uint32_t*)info.bits + adirty.top*info.stride + adirty.left;
      uint32_t* src = (uint32_t*)pixels + adirty.top*width + adirty.left;
      size_t dw = adirty.right - adirty.left;
      if(dw == info.stride)  // && adirty.left == 0
        memcpy(dst, src, 4*dw*(adirty.bottom - adirty.top));  // full width case
      else {
        for(int row = adirty.top; row < adirty.bottom; ++row) {
          memcpy(dst, src, 4*dw);
          dst += info.stride;
          src += width;
        }
      }
    }
    ANativeWindow_unlockAndPost(awin);
  }
  else
    PLATFORM_LOG("ANativeWindow_lock failed!\n");
  ANativeWindow_release(awin);
  fn.env->DeleteLocalRef(jsurface);
}

/*
int AndroidHelper::detectPenType(int pentype)
{
  AndroidMethod fn("detectPenType", "(I)I");
  return fn.env->CallIntMethod(fn.activity, fn.method_id, pentype);
}

float android_getScreenDPI()
{
  return QAndroidJniObject::callStaticMethod<jfloat>(className, "getScreenDPI", "()F");
}

int android_detectScreenSize()
{
  return QAndroidJniObject::callStaticMethod<jint>(className, "detectScreenSize", "()I");
}*/

void AndroidHelper::openUrl(const char* url)
{
  AndroidMethod fn("openUrl", "(Ljava/lang/String;)V");
  if(!fn.method_id) return;
  jstring jurl = fn.env->NewStringUTF(url);
  fn.env->CallVoidMethod(fn.activity, fn.method_id, jurl);
}

// html file, "text/html", "Share Document"
// png file, "image/png", "Share Page"
// Also PDF!
void AndroidHelper::sendFile(const char* filename, const char* mimetype, const char* title)
{
  //SCRIBBLE_LOG("sendFile(%s, %s, %s)", filename, mimetype, title);
  AndroidMethod fn("sendDocument", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
  if(!fn.method_id) return;
  jstring jfilename = fn.env->NewStringUTF(filename);
  jstring jmimetype = fn.env->NewStringUTF(mimetype);
  jstring jtitle = fn.env->NewStringUTF(title);
  fn.env->CallVoidMethod(fn.activity, fn.method_id, jfilename, jmimetype, jtitle);
}

bool AndroidHelper::rawResourceToFile(const char* resname, const char* outfile)
{
  AndroidMethod fn("assetToFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
  if(!fn.method_id) return false;
  jstring jresname = fn.env->NewStringUTF(resname);
  jstring joutfile = fn.env->NewStringUTF(outfile);
  return fn.env->CallBooleanMethod(fn.activity, fn.method_id, jresname, joutfile);
}

void AndroidHelper::getImage()
{
  AndroidMethod fn("combinedGetImage", "()V");
  if(!fn.method_id) return;
  fn.env->CallVoidMethod(fn.activity, fn.method_id);
}

// fns called from Java
static jint jniNotify(JNIEnv* env, jclass, jint code)
{
  if(code == A_VOL_KEYS)
    return AndroidHelper::acceptVolKeys ? 1 : 0;

  if(code == A_REQ_PERM)
    ScribbleApp::storagePermission(true);
  else if(code == A_REQ_PERM_FAIL)
    ScribbleApp::storagePermission(false);
  return 0;
}

static void jniOpenFile(JNIEnv* env, jclass, jstring jfilename)
{
  const char* filename = env->GetStringUTFChars(jfilename, 0);
  if(AndroidHelper::mainWindowInst)
    AndroidHelper::mainWindowInst->openDocument(filename);
  env->ReleaseStringUTFChars(jfilename, filename);
}

static void jniOpenFileDesc(JNIEnv* env, jclass, jstring jfilename, jint jfd)
{
  char buff[256];
  int len = readlink(fstring("/proc/self/fd/%d", jfd).c_str(), buff, 256);
  if(len > 0 && len < 256) {
    buff[len] = '\0';
    //PLATFORM_LOG("readlink returned: %s\n", buff);
    //const char* filename = env->GetStringUTFChars(jfilename, 0);
    if(AndroidHelper::mainWindowInst)
      AndroidHelper::mainWindowInst->openDocument(buff);  //filename);
    //env->ReleaseStringUTFChars(jfilename, filename);
  }
}

// Android seems to be giving us ABGR pixels?
static void jniInsertImage(JNIEnv* env, jclass, jobject jbitmap, jstring jmimetype, jboolean fromintent)
{
  const char* mimetype = env->GetStringUTFChars(jmimetype, 0);
  AndroidBitmapInfo info;
  AndroidBitmap_getInfo(env, jbitmap, &info);
  unsigned char* pixels;
  if(AndroidBitmap_lockPixels(env, jbitmap, (void**)&pixels) >= 0) {
    // image is placed on clipboard and user is shown message
    auto imgfmt = (mimetype && strcasestr(mimetype, "jpeg")) ? Image::JPEG : Image::PNG;
    ScribbleApp::insertImageSync(Image::fromPixels(info.width, info.height, pixels, imgfmt), fromintent);
    AndroidBitmap_unlockPixels(env, jbitmap);
  }
  env->ReleaseStringUTFChars(jmimetype, mimetype);
}

static void jniTouchEvent(JNIEnv* env, jclass,
    jint toolType, jint ptrId, jint action, jint t, jfloat x, jfloat y, jfloat p, jfloat major, jfloat minor)
{
  // ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3,
  //  ACTION_OUTSIDE = 4, ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6
  static const int actionToSDL[]
      = {SDL_FINGERDOWN, SDL_FINGERUP, SDL_FINGERMOTION, SVGGUI_FINGERCANCEL, 0, SDL_FINGERDOWN, SDL_FINGERUP};
  // unknown, finger, stylus, mouse, eraser
  static const int toolToSDL[] = {0, 1, PenPointerPen, 3, PenPointerEraser};

  SDL_Event event = {0};
  event.type = action >= 0 && action < 7 ? actionToSDL[action] : SDL_FINGERMOTION;
  event.tfinger.timestamp = t;
  event.tfinger.touchId = toolType >= 0 && toolType < 5 ? toolToSDL[toolType] : 0;
  event.tfinger.fingerId = ptrId;  //eventtype == SDL_FINGERMOTION ? buttons : button;
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.dx = major;
  event.tfinger.dy = minor;
  event.tfinger.pressure = p;
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
}


static JNINativeMethod jniMethods[] = {
  {"jniNotify", "(I)I", (void*)jniNotify},
  {"jniOpenFile", "(Ljava/lang/String;)V", (void*)jniOpenFile},
  {"jniOpenFileDesc", "(Ljava/lang/String;I)V", (void*)jniOpenFileDesc},
  {"jniInsertImage", "(Landroid/graphics/Bitmap;Ljava/lang/String;Z)V", (void*)jniInsertImage},
  {"jniTouchEvent", "(IIIIFFFFF)V", (void*)jniTouchEvent}
};

jint JNICALL JNI_OnLoad(JavaVM *vm, void*)
{
  JNIEnv* env;
  if(vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4) != JNI_OK)
    return JNI_FALSE;

  jclass clazz = env->FindClass(className);
  if(env->RegisterNatives(clazz, jniMethods, sizeof(jniMethods) / sizeof(jniMethods[0])) < 0)
    return JNI_FALSE;

  return JNI_VERSION_1_4;
}
