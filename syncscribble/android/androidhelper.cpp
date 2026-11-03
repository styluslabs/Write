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

// calls to Java (from app thread, which never returns to Java, so local refs must be freed explicitly)
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
    env->PushLocalFrame(16);
    // retrieve the Java instance of the activity
    activity = (jobject)SDL_AndroidGetActivity();
    if(!activity) return;
    // find the Java class of the activity (com.styluslabs.writeqt.MainActivity)
    clazz = env->GetObjectClass(activity);
    if(!clazz) return;
    // find the identifier of the method to call
    method_id = env->GetMethodID(clazz, name, sig);
  }

  ~AndroidMethod()
  {
    if(!env) return;
    if(env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->PopLocalFrame(NULL);  // frees activity, clazz, and any strings created for call
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

// fns called from Java (on UI thread, so app must only be accessed on app thread)
#define JNI_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_styluslabs_writeqt_MainActivity_##name

JNI_FN(jint, jniNotify)(JNIEnv* env, jclass, jint code)
{
  if(code == A_VOL_KEYS)
    return AndroidHelper::acceptVolKeys ? 1 : 0;

  if(code == A_REQ_PERM)
    ScribbleApp::storagePermission(true);
  else if(code == A_REQ_PERM_FAIL)
    ScribbleApp::storagePermission(false);
  return 0;
}

static void openDocumentOnAppThread(std::string filename)
{
  platformRunOnAppThread([filename](){
    if(AndroidHelper::mainWindowInst)
      AndroidHelper::mainWindowInst->openDocument(filename);
  });
}

JNI_FN(void, jniOpenFile)(JNIEnv* env, jclass, jstring jfilename)
{
  const char* filename = env->GetStringUTFChars(jfilename, 0);
  openDocumentOnAppThread(filename);
  env->ReleaseStringUTFChars(jfilename, filename);
}

JNI_FN(void, jniOpenFileDesc)(JNIEnv* env, jclass, jstring jfilename, jint jfd)
{
  char buff[256];
  int len = readlink(fstring("/proc/self/fd/%d", jfd).c_str(), buff, 256);
  if(len > 0 && len < 256) {
    buff[len] = '\0';
    openDocumentOnAppThread(buff);
  }
}

// Android seems to be giving us ABGR pixels?
JNI_FN(void, jniInsertImage)(JNIEnv* env, jclass, jobject jbitmap, jstring jmimetype, jboolean fromintent)
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
