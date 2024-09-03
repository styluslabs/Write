#pragma once

#include "pugixml.hpp"
#include "ulib/painter.h"
#include "ugui/widgets.h"
#include "basics.h"
#include "application.h"
#include "scribblepen.h"
#if PLATFORM_ANDROID
#include "android/androidhelper.h"
#elif PLATFORM_IOS
#include "ios/ioshelper.h"

// move this somewhere else
struct UIDocStream : public MemStream
{
  std::string url;
  void* uiDocument;

  UIDocStream(void* src, size_t len, size_t reserve, const char* _url, void* uidoc)  //, size_t reserve = 0)
    : url(_url), uiDocument(uidoc) { buffer = (char*)src; buffsize = len; capacity = reserve; }  //MemStream(src, len, reserve)
  // UIDocument will free buffer
  ~UIDocStream() override { if(uiDocument) { iosCloseDocument(uiDocument); buffer = NULL; } }

  bool flush() override { iosSaveDocument(uiDocument, data(), size()); return true; }
  const char* name() const override { return url.c_str(); }
  int type() const override { return MEMSTREAM | UIDOCSTREAM; }
};
#endif

// enable update on Android now that we can't update on Google Play
#define ENABLE_UPDATE !PLATFORM_IOS

class MainWindow;
class ScribbleConfig;
class ScribbleDoc;
class ScribbleMode;
class ScribbleArea;
class BookmarkView;
class ClippingView;
class OverlayWidget;
class DocumentList;
class PenToolbar;
class Selection;
class Clipboard;
class Page;
struct Url;

class ScribbleApp : public Application
{
public:
  void dropEvent(SDL_Event* event);
  bool keyPressEvent(SDL_Event* event);
  int sdlEventFilter(SDL_Event* event);
  bool sdlEventHandler(SDL_Event* event);

  void newDocument();
  bool openDocument();
  bool saveDocument();
  bool doSave(ScribbleDoc* doc);
  bool doSaveAs();
  bool openHelp();
  void sendPageImage();
  void sendPDF();
  void sendDocument();
  void about();
  void revert();
  void clipboardChange();
  void pasteClipboard();
  void openPreferences();
  void resetDocPrefs();
  bool loadClippingsDoc();
  void setMode(int mode);
  void showPageSetup();
  void openRecentFile(const std::string& filename);
  void createLink();
  void exportPDF();
  void insertImage();
  void insertImage(const std::string& filename);
  void insertImage(Image image, bool fromintent = false);
  void showNotify(const std::string& msg, int level = 1);
  void dismissNotify();
  void appSuspending();
  void hideBookmarks();
  void hideClippings();
  void insertDocument();
  void penSelected(int penindex);
  void closeDocs(const FSPath& path);
  void maybeQuit();
  // android callbacks
  bool openDocument(std::string filename);
  static void storagePermission(bool granted);
  static void insertImageSync(Image image, bool fromintent = false);
  // syncing
  bool openSharedDoc(std::string sharename);
  bool openSharedDoc();
  void shareDocument();
  void syncSendImmed();
  void syncView();
  void showSyncInfo();
  //bool syncSignIn(const QUrl& baseurl);
  //void syncMessage(std::string msg, int level);
  // update check
  void updateCheck(bool userreq = true);
#ifdef SCRIBBLE_TEST
  std::string runTest(std::string runtype);
  void runTestUI(std::string runtype);
#endif

  ScribbleApp(int argc, char* argv[]);
  void init();
  ~ScribbleApp();

  // public so that RulingDialog can access
  ScribbleMode* scribbleMode = NULL;
  //ScribbleDoc* scribbleDoc = NULL;
  ScribbleArea* mActiveArea = NULL;
  ScribbleDoc* clippingDoc = NULL;
  ClippingView* clippingArea = NULL;
  BookmarkView* bookmarkArea = NULL;
  OverlayWidget* overlayWidget = NULL;
  std::vector<ScribbleDoc*> scribbleDocs;
  std::vector<ScribbleArea*> scribbleAreas;

  std::unique_ptr<Clipboard> clipboard;
  bool newClipboard = false;
  bool clipboardExternal = true;
  const Page* clipboardPage = NULL;
  int clipboardFlags = 0;

  PenToolbar* penToolbar;
  ScribblePen currPen = {Color::BLACK, 1};
  Color bookmarkColor = Color::BLUE;

  std::string tempPath;
  std::string savedPath;
  //std::string backupPath;
  std::string docRoot;
  std::string clippingsPath;
  std::string runType;
  std::string argDoc;
  std::string outDoc;
  bool isWindowActive = true;
  bool hasI18n = false;
  static Uint32 scribbleSDLEvent;
  enum scribbleSDLEventCode {INSERT_IMAGE=1, UPDATE_CHECK,
      STORAGE_PERMISSION, DISMISS_DIALOG, SIMULATE_PEN_BTN, IAP_COMPLETE, APP_SUSPEND};

  ScribbleArea* activeArea() const { return mActiveArea; }
  ScribbleDoc* activeDoc() const;
  void setActiveArea(ScribbleArea* area);
  void openSplit();
  bool closeSplit();

  void repaintBookmarks(bool newdoc = false);
  void refreshUI(ScribbleDoc* doc, int reason);
  bool oneTimeTip(const char* id, Point pos = {}, const char* message = NULL);
  void showSelToolbar(Point pos);
  void doCommand(int cmd);
  void setClipboard(Clipboard* cb, const Page* srcpage, int flags);
  void loadClipboardText(const char* clipText, size_t len = 0);
  void loadClipboard();
  bool storeClipboard();
  void setClipboardToImage(Image img, bool lossy = false);
  void penChanged(int changed);
  void updatePenToolbar();
  void setPen(const ScribblePen& pen);
  const ScribblePen* getPen() const { return &currPen; }
  void loadConfig();

  static ScribbleApp* app;
  static MainWindow* win;
  static ScribbleConfig* cfg;
  static Dialog* currDialog;

  enum MessageType {Info, Question, Warning, Error};
  static std::string messageBox(
      MessageType type, std::string title, std::string message, std::vector<std::string> buttons = {"OK"});
  static Dim getPreScale();
  static void getScreenPageDims(int* w, int* h);
  static bool openURL(const char* url);
#if PLATFORM_ANDROID
  static bool hasAndroidPermission();
  static bool requestAndroidPermission();
#endif

private:
  friend class ScribbleTest;

  DocumentList* documentList = NULL;
  std::string cfgFile;
  std::string fileExt;
  std::string httpUserAgent;
  std::string currFolder;
  std::vector<std::string> recentDocs;
  std::vector< std::unique_ptr<Action> > recentFileActions;
  //std::string backupFilename;
  int clipboardSerial = 0;  // iOS and Windows
  int currPenIndex;
  bool backToDocList = true;
  bool delayedShowDocList = false;
  bool disableConfigSave = false;
  size_t historyPos = 0;  // used in penChanged

  bool maybeSave(bool prompt = false);
  bool checkExtModified();
  bool checkExtModified(ScribbleDoc* doc);
  void doNewDocument();
  bool doOpenDocument(std::string filename);
  bool doOpenDocument(IOStream* filestrm);
  void populateRecentFiles();
  void writePDF(std::ostream& strm);
  bool writePDF(const std::string& filename);
  void saveConfig();
  void setWinTitle(const std::string& filename);
  void onLoadFile(const std::string& filename, bool addrecent = true);
  void sendFile(const std::string& body, const std::string& attachfile);
  bool openOrCreateDoc(bool cancelable = false);
  std::string execDocumentList(int mode, const char* exts = NULL, bool cancelable = true) ;
  std::string createRecoveryName(std::string filename, const char* toappend = " - recovered");
  std::string docDisplayName(std::string filename, int maxwidth=0);
  std::string docShortName(const std::string& filename, int maxwidth=0);
  Clipboard* importExternalDoc(SvgDocument* doc);
  bool openExternalDoc();
#if PLATFORM_ANDROID
  Semaphore saveSem;
#endif
  // update
#if ENABLE_UPDATE
  //std::thread* updateThread;
  bool userUpdateReq = false;
  int updateSocket = -1;
  void updateNotify(const char* msg, int level);
  void updateInfoReceived(char* updateData);
  static int updateThreadFn(void* _self);
#endif
  // sync
  bool syncSignIn(const Url& baseurl);
  std::string syncAPICall(const Url& baseurl, const std::string& route);
  bool doSharedDoc(std::string host, std::string reply, bool master);

  Toolbar* notifyBar = NULL;
  TextBox* notifyText = NULL;
  Timer* notifyTimer = NULL;
  Timer* autoSaveTimer = NULL;
  std::string syncSession;  // session cookie

  static const int volUpActions[];
  static const int volDnActions[];
};
