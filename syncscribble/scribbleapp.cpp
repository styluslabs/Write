#include <fstream>
#include <time.h>

#include "scribbleapp.h"
#include "application.h"
#include "basics.h"
#include "mainwindow.h"
#include "scribbledoc.h"
#include "scribblewidget.h"
#include "scribblesync.h"
#include "bookmarkview.h"
#include "clippingview.h"
#include "scribbleinput.h"
#include "documentlist.h"
#include "rulingdialog.h"
#include "pentoolbar.h"
#include "linkdialog.h"
#include "configdialog.h"
#include "touchwidgets.h"
#include "usvg/pdfwriter.h"
#include "ulib/unet.h"
#include "usvg/svgparser.h"
#if PLATFORM_WIN
#include "windows/winhelper.h"
#include <shellapi.h>  // for ShellExecute for openUrl
#elif PLATFORM_OSX
#include "macos/macoshelper.h"
#elif PLATFORM_LINUX
#include "linux/linuxtablet.h"
#endif

ScribbleApp* ScribbleApp::app = NULL;
MainWindow* ScribbleApp::win = NULL;
ScribbleConfig* ScribbleApp::cfg = NULL;
Dialog* ScribbleApp::currDialog = NULL;
Uint32 ScribbleApp::scribbleSDLEvent = 0;

ScribbleApp::ScribbleApp(int argc, char* argv[])
{
  // print the current version to aid investigating errors reported by users
  PLATFORM_LOG("Write r" PPVALUE_TO_STRING(SCRIBBLE_REV_NUMBER) "\n");
  // seed RNG (only applies to this thread)
  srandpp(mSecSinceEpoch());
  srand(randpp());

  // load config
  const char* basepath = appDir.empty() ? "." : appDir.c_str();
#if PLATFORM_ANDROID
  const char* appstorage = SDL_AndroidGetExternalStoragePath();
  if(!appstorage)
    appstorage = "/sdcard/Android/data/com.styluslabs.writeqt/files";  // prevent crash
  docRoot = "/sdcard/styluslabs/write/";
  // can't seem to move files into app storage on Android 11, so trash folder needs to be outside it
  tempPath = "/sdcard/styluslabs/.temp/";
  // can still access folders w/o permission - alternative would be to check for or create a file (.nomedia?)
  if(!FSPath(docRoot).exists() || !hasAndroidPermission()) {
    docRoot = FSPath(appstorage, "/").c_str();
    tempPath = FSPath(appstorage, ".temp/").c_str();
  }
  // use old location if existing config file, but otherwise we want to use appstorage local even if we
  //  get sdcard permission
  cfgFile = "/sdcard/styluslabs/write.xml";  // also accept write.xml to make it easier to copy from desktop
  if(!FSPath(cfgFile).exists())
    cfgFile = "/sdcard/styluslabs/.write.xml";
  if(!FSPath(cfgFile).exists())
    cfgFile = FSPath(appstorage, ".write.xml").c_str();
  savedPath = FSPath(appstorage, ".saved/").c_str();
#elif PLATFORM_IOS
  const char* ioshome = getenv("HOME");
  docRoot = FSPath(ioshome, "Documents/").c_str();
  FSPath iosLibRoot = FSPath(ioshome, "Library/");
  // allow user to supply custom config via write.xml in Documents/
  FSPath cfgOverride(docRoot, "write.xml");
  cfgFile = cfgOverride.exists() ? cfgOverride.c_str() : iosLibRoot.child("write.xml").c_str();
  tempPath = iosLibRoot.child("Caches/").c_str();  // iOS does not backup Library/Caches
  savedPath = "Library/saved/";  // relative to HOME, since HOME can change!
#elif PLATFORM_WIN
  std::string env_userprofile = wstr_to_utf8(_wgetenv(utf8_to_wstr("userprofile").data()));
  std::string env_temp = wstr_to_utf8(_wgetenv(utf8_to_wstr("TEMP").data()));
  std::string env_appdata = wstr_to_utf8(_wgetenv(utf8_to_wstr("APPDATA").data()));
  FSPath appDataCfg(env_appdata.size() ? env_appdata : "", "Stylus Labs/write.xml");
  FSPath basePathCfg(basepath, "write.xml");
  // use base path config if it is writable, else use appdata config if it is writable, else use base path
  //  config if we can create it, i.e., if we can write to program folder
  // The reason for preferring config in program folder is so user can complete reset by removing folder
  bool useBase = basePathCfg.exists("r+") || !env_appdata.size() || (!appDataCfg.exists("r+") && basePathCfg.exists("w+"));
  cfgFile = useBase ? basePathCfg.c_str() : appDataCfg.c_str();
  tempPath = env_temp.size() ? FSPath(env_temp, ".styluslabs/").c_str() : "C:/Windows/Temp/.styluslabs/";
  // DocumentList is completely broken if docRoot doesn't exist
  FSPath docrootinfo((env_userprofile.size() ? env_userprofile : "C:/Users/Public"), "Documents/");
  if(!docrootinfo.exists()) docrootinfo = docrootinfo.parent();
  docRoot = docrootinfo.exists() ? docrootinfo.c_str() : "C:/";
#else  // Unix and Mac
  // would like to store write.xml in app bundle on Mac, but this seems to be discouraged if even possible
  const char* env_home = getenv("HOME");
  env_home = env_home && env_home[0] ? env_home : basepath;
  FSPath baseCfg(basepath, "write.xml");
  cfgFile = baseCfg.exists("r+") ? baseCfg.c_str() : FSPath(env_home, ".config/styluslabs/write.xml").c_str();
  docRoot = FSPath(env_home, "/").c_str();
  tempPath = "/tmp/.styluslabs/";
#endif

  // on desktop, use config file in application dir for debug builds
#if PLATFORM_DESKTOP
  if(SCRIBBLE_DEBUG)
    cfgFile = FSPath(basepath, "write.xml").c_str();
  savedPath = FSPath(cfgFile).parent().childPath("saved/");
#elif PLATFORM_ANDROID
  // loading /system/fonts/Roboto-Regular.ttf seems to not work on some devices (not seen personally)
  FSPath sansFont(savedPath, "Roboto-Regular.ttf");
  if(!sansFont.exists()) {
    createPath(savedPath.c_str());
    AndroidHelper::rawResourceToFile("Roboto-Regular.ttf", sansFont.c_str());
  }
  FSPath fallbackFont(savedPath, "DroidSansFallback.ttf");
  if(!fallbackFont.exists()) {
    createPath(savedPath.c_str());
    AndroidHelper::rawResourceToFile("DroidSansFallback.ttf", fallbackFont.c_str());
  }
#endif
  createPath(tempPath.c_str());

  cfg = new ScribbleConfig;
  if(!cfg->loadConfigFile(cfgFile.c_str())) {
#if PLATFORM_DESKTOP
    if(!createPath(FSPath(cfgFile).parent().c_str()))
      cfgFile = FSPath(basepath, "write.xml").c_str();
#endif
    runType = "first";
  }

  // allow config values to be passed on command line as --name=value, where value may be in quotes
  bool saveconfig = false;
  for(int ii = 1; ii < argc; ++ii) {
    StringRef arg(argv[ii]);
    if(arg.startsWith("--")) {
      arg += 2;
      auto pieces = splitStringRef(arg, '=');
      if(pieces.size() > 1) {
        if(pieces[1][0] == '"' || pieces[1][0] == '\'')
          pieces[1].chop(1) += 1;  // remove delimiting quotes
        // we don't want to save values passed on command line to permanent config file; temporary soln is
        //  to just disable config write; command-line config is just for testing, so this is fine
        disableConfigSave = true;
        // Maybe add a passThru flag to ScribbleConfig (to pass set() to parent)?
        cfg->setConfigValue(pieces[0].toString().c_str(), pieces[1].toString().c_str());
      }
      else {
        if(arg == "saveconfig")
          saveconfig = true;  //disableConfigSave = false;
        else if(arg == "reset") {
          delete cfg;
          cfg = new ScribbleConfig;
        }
        else if(arg == "exit")
          finish();  // exit immediately
        else if(arg == "out" && ii+1 < argc)
          outDoc = argv[++ii];
        else if(arg.endsWith("test"))
          runType = arg.toString();
        else if(ii+1 < argc && argv[ii+1][0] != '-') {  // support space instead of '=' between arg and value
          disableConfigSave = true;
          cfg->setConfigValue(arg.data(), argv[++ii]);
        }
      }
    }
    else if(!arg.startsWith("-"))  // macOS passes some weird -psn_0... arg on first run
      argDoc = argv[ii];
  }
  disableConfigSave = disableConfigSave && !saveconfig;
}

void ScribbleApp::init()
{
  scribbleSDLEvent = SDL_RegisterEvents(1);
  // Default pens
  auto dflttip = ScribblePen::TIP_FLAT | ScribblePen::WIDTH_PR;
  switch(std::max(0, 8 - int(cfg->pens.size()))) {
    case 8: cfg->pens.push_back(ScribblePen(Color::BLACK, 1.6, dflttip, 0.9, 2.0));
    case 7: cfg->pens.push_back(ScribblePen(Color::RED, 1.6, dflttip, 0.9, 2.0));
    case 6: cfg->pens.push_back(ScribblePen(Color::DARKGREEN, 1.6, dflttip, 0.9, 2.0));
    case 5: cfg->pens.push_back(ScribblePen(Color::BLUE, 1.6, dflttip, 0.9, 2.0));
    case 4: cfg->pens.push_back(
        ScribblePen(Color::BLACK, 3.0, dflttip | ScribblePen::WIDTH_DIR, 0.8, 2.0, 0, 45));
    case 3: cfg->pens.push_back(
        ScribblePen(Color::BLACK, 1.8, ScribblePen::TIP_FLAT | ScribblePen::WIDTH_SPEED, 0.7, 0, 0.5, 0));
    case 2: cfg->pens.push_back(ScribblePen(Color::BLACK, 4.0, ScribblePen::TIP_ROUND));
    case 1: cfg->pens.push_back(  //Color(255, 255, 0, 127)
        ScribblePen(Color(255, 127, 255, 127), 34, ScribblePen::TIP_CHISEL | ScribblePen::DRAW_UNDER));
  }

  // load mode logic
  scribbleMode = new ScribbleMode(cfg);
  scribbleMode->loadModes(cfg->String("toolModes"));
  scribbleMode->setMode(MODE_STROKE);

  // create android helper needed to emit signals for Android callbacks since those run on different thread
#if PLATFORM_ANDROID
  AndroidHelper::mainWindowInst = this;
  // request interception of volume keys
  //if(cfg->Int("volButtonMode") > 0) qputenv("QT_ANDROID_VOLUME_KEYS", "1");

  // No more device detection - too many devices, and we don't do it for any other platforms
  // detect pen if necessary -  see scribbleinput.h for explanation of REDETECT_PEN, DETECTED_PEN
  //int pentype = cfg->Int("penType");
  //if(pentype < 0 || (pentype >= ScribbleInput::REDETECT_PEN && pentype < ScribbleInput::DETECTED_PEN)) {
  //  pentype = AndroidHelper::detectPenType(pentype < 0 ? 0 : pentype);
  //  cfg->set("penType", pentype);
  //  // enable single touch draw (disabled by default) and pan from edge if no pen
  //  if(pentype <= 0) {
  //    cfg->set("singleTouchMode", 2);
  //  }
  //}
#endif
  // keep screen on
  cfg->Bool("keepScreenOn") ? SDL_DisableScreenSaver() : SDL_EnableScreenSaver();
  //AndroidHelper::doAction(A_KEEP_SCREEN_ON);

  // setup page sizes on first run
  if(cfg->Float("pageWidth") == 0) {
    int w, h;
    getScreenPageDims(&w, &h);
    cfg->set("pageWidth", Dim(w));
    cfg->set("pageHeight", Dim(h));
    cfg->set("marginLeft", Dim(MIN(100, w/7)));
  }

  // memory limit
  Document::memoryLimit = 1024*1024*size_t(cfg->Int("maxMemoryMB"));  // MB to bytes

  // file extension is used several times (no longer with the "." included!)
  fileExt = cfg->String("docFileExt");
  //nameFilter = "Write Document (*" + fileExt + ")";
  // user agent string for http requests
  httpUserAgent = std::string("Mozilla/5.0 (") + PLATFORM_NAME + ") Write r" + PPVALUE_TO_STRING(SCRIBBLE_REV_NUMBER);

  /// UI creation

  win = createMainWindow();  // probably should be done in ScribbleApp
  win->sdlWindow = sdlWindow;  // window created by Application in order to create an OpenGL context
  win->addHandler([this](SvgGui*, SDL_Event* event){ return sdlEventHandler(event); });

  ScribbleDoc* doc = new ScribbleDoc(this, cfg, scribbleMode);
  mActiveArea = new ScribbleArea();
  doc->addArea(mActiveArea);
  scribbleAreas.push_back(mActiveArea);
  scribbleDocs.push_back(doc);

  bookmarkArea = new BookmarkView(cfg, doc);  //bookmarkArea->setScribbleDoc(doc);

  clippingDoc = new ScribbleDoc(this, cfg, NULL);  // scribbleMode = NULL
  clippingArea = new ClippingView();
  clippingDoc->addArea(clippingArea);

  // create UI elements
  win->setupUI(this);
  // add window to SvgGui
  gui->showWindow(win, NULL, false);

  penToolbar = static_cast<PenToolbar*>(win->penToolbarAutoAdj->contents);
  penToolbar->onChanged = [this](int c){ penChanged(c); };
  overlayWidget = win->overlayWidget;

  loadConfig();
  // newDocument results in a call to refreshUI(), so all UI components have to be initialized!
  doc->newDocument();

  // set pen to first saved pen
  win->refreshPens(doc);
  setPen(*cfg->getPen(0));
  currPenIndex = 0;
  bookmarkColor = Color::fromArgb(cfg->Int("bookmarkColor"));

  // loading clippings doc is deferred until dock is shown
  clippingsPath = "";
  clippingDoc->newDocument();

  const char* recentDocsStr = cfg->String("recentDocs");
  auto recentDocsSplit = splitStringRef(StringRef(recentDocsStr), ":::", true);
  for(const StringRef& s : recentDocsSplit)
    recentDocs.emplace_back(s.toString());
  onLoadFile("");  // this will call populateRecentFiles();
  //scribbleDoc->activeArea->setFocus();

  // update check
#if ENABLE_UPDATE
  if(cfg->Bool("updateCheck")
      && cfg->Int("lastUpdateCheck") + cfg->Int("updateCheckInterval") < int(mSecSinceEpoch()/1000))
    updateCheck(false);
#endif

  // testing
#ifdef SCRIBBLE_TEST
  if(StringRef(runType).endsWith("test")) {
    SCRIBBLE_LOG(runTest(runType).c_str());
    finish();
    return;
  }
#endif

  // clear temp folder
  removeDir(tempPath.c_str(), false);

#if PLATFORM_ANDROID
  // this will cause permission prompt for fresh install
  if(!FSPath(cfgFile).exists())
    cfg->set("currFolder", "/sdcard/styluslabs/write/");
  // Android permissions disaster: upgrading from target API 29 w/ write permission to 30 will set permission
  //  to "media only", which allows some files created by us outside Android/data app folder to be writable,
  //  others read-only, others neither; can't create new files - so we need to prompt user to grant manage
  //  files permission or reset to Android/data app folder
  const char* currfolder = cfg->String("currFolder");
  if(!hasAndroidPermission()) {
    if(!StringRef(currfolder).contains("com.styluslabs.writeqt") && !requestAndroidPermission()) {
      cfg->set("currFolder", docRoot.c_str());
      cfg->set("reopenLastDoc", false);  // prevent opening of document from changing currFolder
      openOrCreateDoc();  // showing dialog prevents delayedShowDocList from working
      return;
    }
  }
  else if(StringRef(currfolder).startsWith("/sdcard/styluslabs/write/")) {
    //PLATFORM_LOG("Creating /sdcard/styluslabs/write/\n");
    createPath("/sdcard/styluslabs/write/");
  }
  // we are now ready to handle initial intent
  //  if we were sent a document to open, openDocument will close doc list that is shown by newDocument()
  AndroidHelper::processInitialIntent();
  if(activeDoc()->fileName()[0])
    return;
#elif PLATFORM_IOS
  const char* bkmk = cfg->Bool("reopenLastDoc") ? cfg->String("iosBookmark0") : NULL;
  if(runType == "first" && openHelp())
    bkmk = "!";  // this will cause main Write view to be shown after initializing doc browser
  initDocumentBrowser(bkmk);
  return;
#endif

  if(!argDoc.empty()) {
    if(StringRef(argDoc).startsWith("swb://")) {
      if(!openSharedDoc(argDoc)) {
        showNotify(fstring(_("\"%s\" could not be opened."), argDoc.c_str()), 2);
        PLATFORM_LOG("Error opening whiteboard link %s\n", argDoc.c_str());
      }
      return;
    }
    FSPath argInfo(canonicalPath(argDoc));
    if(argInfo.isDir()) {
      if(argInfo.exists())
        cfg->set("currFolder", argInfo.c_str());  // open doc list to passed folder
    }
    else {
      // don't use doOpenDocument() because it will display dialog on error
      // use argInfo instead of argDoc because relative path causes lots of problems
      Document::loadresult_t res = activeDoc()->openDocument(argInfo.c_str());
      if(res != Document::LOAD_FATAL && res != Document::LOAD_EMPTYDOC) {
        onLoadFile(activeDoc()->fileName());
        if(!outDoc.empty()) {
          FSPath outinfo(outDoc);
          if(outinfo.exists())
            PLATFORM_LOG("Error: output file %s already exists\n", outinfo.c_str());
          else if(outinfo.extension() == "pdf") {
            if(!writePDF(outinfo.c_str()))
              PLATFORM_LOG("Error writing %s\n", outinfo.c_str());
          }
          else {
            auto flags = outinfo.extension() == "html" ? Document::SAVE_MULTIFILE : 0;
            if(!activeDoc()->saveDocument(outinfo.c_str(), Document::SAVE_FORCE | flags))
              PLATFORM_LOG("Error saving %s\n", outinfo.c_str());
          }
        }
      }
      else {
        showNotify(fstring(_("\"%s\" could not be opened."), argDoc.c_str()), 2);
        PLATFORM_LOG("Error opening %s\n", argDoc.c_str());
      }
      return;
    }
  }
  if(cfg->Bool("reopenLastDoc") && recentDocs.size() > 0
      && FSPath(recentDocs[0]).exists() && doOpenDocument(recentDocs[0]))
    return;
  if(runType == "first" && openHelp())
    return;

  // delay display of doc list of desktop platforms so that it is shown on top of main window
  delayedShowDocList = true;
}

ScribbleApp::~ScribbleApp()
{
  gui->closeWindow(win);
  // TODO: should use unique_ptr for these ... but then we need a bunch of get()s
  delete documentList;
  delete bookmarkArea;
  delete clippingDoc;
  delete clippingArea;
  for(ScribbleDoc* doc : scribbleDocs)
    delete doc;
  for(ScribbleArea* area : scribbleAreas)
    delete area;
  delete scribbleMode;
  // destroy other objects before writing out config, in case destructors set config values
  if(!disableConfigSave)
    cfg->saveConfigFile(cfgFile.c_str(), true);
  delete cfg;  cfg = NULL;
  delete win;  win = NULL;
#if PLATFORM_DESKTOP
  removeDir(tempPath.c_str(), true);
#endif
}

// for now, first button will be default (triggered by Enter key) and last button cancel (triggered by Esc)
// - if we need more flexibility, could use prefix chars, e.g. buttons = "Save|*Save All|~Cancel"
std::string ScribbleApp::messageBox(MessageType type,
    std::string title, std::string message, std::vector<std::string> buttons)
{
  // copied from usvg/test/mainwindow.cpp ... can we deduplicate?
  Dialog* dialog = createDialog(title.c_str());
  Widget* dialogBody = dialog->selectFirst(".body-container");

  //auto buttons = splitStr<std::vector>(buttons.c_str(), '|');
  for(size_t ii = 0; ii < buttons.size(); ++ii) {
    Button* btn = dialog->addButton(buttons[ii].c_str(), [dialog, ii](){ dialog->finish(int(ii)); });
    if(ii == 0)
      dialog->acceptBtn = btn;
    if(ii + 1 == buttons.size())
      dialog->cancelBtn = btn;
  }

  dialogBody->setMargins(10);
  SvgText* msgNode = createTextNode(message.c_str());
  dialogBody->addWidget(new Widget(msgNode));
  // wrap message text as needed
  std::string bmsg = SvgPainter::breakText(msgNode, 0.8*win->winBounds().width());
  if(bmsg != message) {
    msgNode->clearText();
    msgNode->addText(bmsg.c_str());
  }

  // currDialog used by ScribbleSync to clear blocking dialog on reconnect
  Dialog* prevDialog = currDialog;
  currDialog = dialog;
  int res = execDialog(dialog);
  currDialog = prevDialog;
  delete dialog;
  return (res >= 0 && res < (int)buttons.size()) ? buttons[res] : "";  //button_text.back();
}

/// config ///

void ScribbleApp::loadConfig()
{
  SvgWriter::DEFAULT_SAVE_IMAGE_SCALED = cfg->Bool("savePicScaled") ? std::max(Dim(1), gui->paintScale) : 0;
  Element::ERASE_IMAGES = cfg->Bool("eraseOnImage");
  // this should really be per-document, but stick here for now while we consider auto-detecting value
  Page::BLANK_Y_RULING = cfg->Float("blankYRuling");
#if PLATFORM_ANDROID
  AndroidHelper::acceptVolKeys = cfg->Int("volButtonMode") != 0;
#endif
  // do this here avoids need for restart to change theme
  if(cfg->Int("uiTheme") == 2) {
    win->node->addClass("light");
    gui->setWindowXmlClass("light");  // for new windows and dialogs
  }
  else {
    win->node->removeClass("light");
    gui->setWindowXmlClass("");
  }

  // If we want to retain discard changes, we should make copy of doc before autosave
  // reasonable not to disable this when losing focus (on desktop), unless we save immediately
  int autoSaveInterval = cfg->Int("autoSaveInterval");
  if(autoSaveInterval > 0) {
    autoSaveInterval = std::max(autoSaveInterval, 30)*1000;  // sec to ms
    autoSaveTimer = gui->setTimer(autoSaveInterval, win, autoSaveTimer, [this, autoSaveInterval]() {
      for(ScribbleDoc* d : scribbleDocs) {
        if(d->isModified() && d->fileName()[0]) {
          // delay autosave until pen is lifted
          if(activeDoc()->getActiveMode() == MODE_NONE)
            d->saveDocument();
          else
            d->autoSaveReq = true;
        }
      }
      return autoSaveInterval;
    });
  }
}

void ScribbleApp::saveConfig()
{
  // save window layout
  //cfg->set("mainWindowGeometry", saveGeometry().toBase64().constData());
  //cfg->set("mainWindowState", saveState().toBase64().constData());
  cfg->set("bookmarkColor", bookmarkColor.argb());
  cfg->set("recentDocs", joinStr(recentDocs, ":::").c_str());
  // save mode of tools
  cfg->set("toolModes", scribbleMode->saveModes().c_str());
  if(penToolbar)
    penToolbar->saveConfig(cfg);
  // moved here from ~ScribbleApp(), which usually is never called on mobile
  int nstrokes = 0;
  for(ScribbleArea* area : scribbleAreas) {
    nstrokes += area->strokeCounter;
    area->strokeCounter = 0;
  }
  cfg->set("strokeCounter", cfg->Int("strokeCounter") + nstrokes);

  // save clipping file if modified or significant position change (or any pos change? - check yOffset?)
  if(!clippingsPath.empty()
       && (clippingDoc->isModified() || clippingDoc->cfg->Int("pageNum") != clippingArea->getPos().pagenum))
    clippingDoc->saveDocument(clippingsPath.c_str());
}

void ScribbleApp::storagePermission(bool granted)
{
  SvgGui::pushUserEvent(scribbleSDLEvent, STORAGE_PERMISSION, (void*)granted);
}

/// Split screen / multi-doc handling ///

ScribbleDoc* ScribbleApp::activeDoc() const { return activeArea()->scribbleDoc; }

void ScribbleApp::setActiveArea(ScribbleArea* area)
{
  if(mActiveArea == area)
    return;

  ScribbleDoc* olddoc = activeDoc();
  olddoc->doCancelAction();
  mActiveArea->widget->focusIndicator->node->addClass("inactive");
  area->widget->focusIndicator->node->removeClass("inactive");
  if(area->scribbleDoc != mActiveArea->scribbleDoc)
    setWinTitle(area->scribbleDoc->fileName());  // refreshUI will take care of adding '*' if modified
  area->scribbleDoc->activeArea = area;
  mActiveArea = area;
  if(activeDoc() != olddoc)
    repaintBookmarks(true);
  gui->setFocused(area->widget);  // this will find the focusable ancestor
  refreshUI(activeDoc(), UIState::SetDoc);
}

void ScribbleApp::openSplit()
{
  activeDoc()->addArea(scribbleAreas[1]);
  setActiveArea(scribbleAreas[1]);
  // openDocument() calls maybeSave(), which is not necessary when opening split
  openOrCreateDoc(true);
}

// hide scribbleAreas[1]
bool ScribbleApp::closeSplit()
{
  if(scribbleAreas[0]->scribbleDoc != scribbleAreas[1]->scribbleDoc) {
    setActiveArea(scribbleAreas[1]);
    if(!maybeSave())  // should we always prompt ... maybe better to stick w/ usual behavior
      return false;
    activeDoc()->newDocument();
  }
  setActiveArea(scribbleAreas[0]);
  scribbleAreas[1]->scribbleDoc->removeArea(scribbleAreas[1]);
  for(ScribbleArea* area : scribbleAreas)
    area->widget->fileNameLabel->setVisible(false);
  // make sure the current doc is first in recent files list (so it is reopened on restart)
  onLoadFile(activeDoc()->fileName());
  return true;
}

/// Event handling ///

// NOTE: this is called from sdlEventHandler on Android since it is not safe to save document on UI thread
//  w/o sync w/ SDL_main thread, which could be modifying document
// return 0 to prevent event from being added to queue for normal processing, 1 otherwise
int ScribbleApp::sdlEventFilter(SDL_Event* event)
{
  switch (event->type) {
  case SDL_APP_TERMINATING:
    // note that we expect SDL_APP_WILLENTERBACKGROUND will always be sent before SDL_APP_TERMINATING
    return PLATFORM_ANDROID ? 1 : 0;  // handle on our thread on Android (call finish())
  case SDL_APP_LOWMEMORY:
  case SDL_APP_WILLENTERBACKGROUND:
#if PLATFORM_ANDROID
    SvgGui::pushUserEvent(scribbleSDLEvent, APP_SUSPEND);
    saveSem.wait();
#else
    appSuspending();
#endif
    return 0;
  case SDL_APP_DIDENTERBACKGROUND:
    Application::isSuspended = true;
    return 0;
  case SDL_APP_WILLENTERFOREGROUND:  // restore state
    return 0;
  case SDL_APP_DIDENTERFOREGROUND:  // restart loops
    Application::isSuspended = false;
    return 1;  // make sure event loop wakes up
  default:
    return 1;  // No special processing, add it to the event queue
  }
}

void ScribbleApp::maybeQuit()
{
  if(win->modalOrSelf() == documentList)
    documentList->finish(DocumentList::REJECTED);
  else if(win->modalOrSelf() != win)
    return;  // send ESC key to cancel dialog?  flash dialog for attention?
  if(maybeSave()) {
    // save other doc if two doc split
    if(scribbleDocs.size() > 1) {
      ScribbleArea* otherArea = (activeArea() == scribbleAreas[0]) ? scribbleAreas[1] : scribbleAreas[0];
      if(otherArea->scribbleDoc && otherArea->scribbleDoc != activeDoc()) {
        setActiveArea(otherArea);
        if(!maybeSave())
          return;   // don't quit
      }
    }
    saveConfig();
    finish();
  }
}

static int systemClipboardSerial()
{
#if PLATFORM_IOS
  return iosClipboardChangeCount();
#elif PLATFORM_WIN
  return GetClipboardSequenceNumber();
#elif PLATFORM_OSX
  return macosClipboardChangeCount();
#elif PLATFORM_ANDROID
  return AndroidHelper::doAction(A_CLIPBOARD_SERIAL);
#else  // Linux
  return -1;
#endif
}

// main event dispatcher
bool ScribbleApp::sdlEventHandler(SDL_Event* event)
{
  switch(event->type) {
  case SDL_QUIT:
    maybeQuit();
    return true;
  case SDL_WINDOWEVENT:
    // only window event sent at start on Android is ENTER
    if(event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED
        || (PLATFORM_ANDROID && event->window.event == SDL_WINDOWEVENT_ENTER)) {
      if(delayedShowDocList) {
        delayedShowDocList = false;
        // allow initial doc list to be canceled on desktop
        if(!SCRIBBLE_DEBUG) openOrCreateDoc();
      }
      isWindowActive = true;
      // requesting app store review
#if PLATFORM_IOS
      if(cfg->Int("strokeCounter") > 4000 && cfg->Int("lastReviewPrompt") + 180*24*60*60 < int(mSecSinceEpoch()/1000)) {
        cfg->set("lastReviewPrompt", int(mSecSinceEpoch()/1000));
        cfg->set("strokeCounter", 0);
        iosRequestReview();  // "https://apps.apple.com/app/id1498369428?action=write-review"
      }
#elif PLATFORM_ANDROID
      if(cfg->Int("strokeCounter") > 10000 && cfg->Int("lastReviewPrompt") == 0) {
        cfg->set("lastReviewPrompt", int(mSecSinceEpoch()/1000));
        auto choice = messageBox(Question, _("Leave a review?"),
            _("You can support the development of Write by leaving a review."), {_("OK"), _("Cancel")});
        if(choice == _("OK"))
          openURL("http://play.google.com/store/apps/details?id=com.styluslabs.writeqt");
      }
#endif
      if((PLATFORM_LINUX && clipboardExternal) || (cfg->Bool("preloadClipboard") && clipboardSerial != systemClipboardSerial()))
        loadClipboard();
      // external modification check timer
#if !PLATFORM_IOS
      gui->setTimer(5000, win);
      checkExtModified();
#endif
    }
    else if(event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
      isWindowActive = false;
      storeClipboard();
#if !PLATFORM_IOS
      gui->removeTimer(win);  // external modification check timer
#endif
    }
    else if(event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
#if PLATFORM_OSX
      // we may want this for other desktop platforms too, but need to test first
      Dim dpi = cfg->Int("screenDPI");
      setupUIScale(dpi >= 10 && dpi <= 1200 ? dpi : 0);
#elif PLATFORM_MOBILE
      win->orientationChanged();
#endif
    }
    return false;  // propagate event for GUI
  case SDL_KEYDOWN:
    return keyPressEvent(event);
  case SDL_KEYUP:
    ScribbleInput::pressedKey = 0;
    return false;
  case SDL_DROPFILE:
    dropEvent(event);
    return true;
  case SDL_CLIPBOARDUPDATE:
    // SDL seems to only send this event for external changes
    clipboardExternal = true;
    // because loading clipboard is async on X11, we can't wait until paste command as on other platforms
    // when switching between two instances of Write, we get the WindowActivate event before the clipboard
    //  change event (since the other instance only updates the clipboard when it gets WindowDeactivate)
    if(isWindowActive && (PLATFORM_LINUX || (cfg->Bool("preloadClipboard") && clipboardSerial != systemClipboardSerial())))
      loadClipboard();
    return true;
#if PLATFORM_ANDROID
  case SDL_APP_TERMINATING:
    // SDL expects main() to return and will call it again, so force exit even if dialog open
    finish();
    return true;
#endif
  default:
    if(event->type == ScribbleSync::sdlEventType) {
      ScribbleDoc* ssdoc = static_cast<ScribbleDoc*>(event->user.data1);
      ScribbleSync* ssync = static_cast<ScribbleSync*>(event->user.data2);
      // we do this instead checking our scribbleDocs list to support ScribbleTest sync test
      if(ssdoc->scribbleSync == ssync)  // make sure ScribbleSync* is still valid
        ssync->sdlEvent(event);
      return true;
    }
    else if(event->type == SvgGui::TIMER) {
      checkExtModified();
      return true;
    }
    else if(event->type == SvgGui::FOCUS_GAINED) {
      // isFocusable is just set to steal focus from pen toolbar text boxes - we don't actually want focus
      win->focusedWidget = NULL;
    }
    else if(event->type == scribbleSDLEvent) {
      if(event->user.code == INSERT_IMAGE) {
        std::unique_ptr<Image> image(static_cast<Image*>(event->user.data1));
        bool fromintent = event->user.data2;
        insertImage(std::move(*image), fromintent);
      }
      else if(event->user.code == DISMISS_DIALOG) {
        if(currDialog)
          currDialog->finish((intptr_t)event->user.data1);
        currDialog = NULL;
      }
      else if(event->user.code == SIMULATE_PEN_BTN) {
        // we want barrel tap to support other alternative behavior besides changing mode (e.g. sel scale internal)
        ScribbleInput::simulatePenBtn = !ScribbleInput::simulatePenBtn;
        // this is done to provide visual feedback; if pen button behavior changes for other modes in the future,
        //  we'll need to revisit this
        if(ScribbleInput::simulatePenBtn)
          scribbleMode->setMode(cfg->Int("penButtonMode"), true);  // once = true
        else
          scribbleMode->scribbleDone();  // revert to previous mode
        win->updateMode();
      }
      else if(event->user.code == IAP_COMPLETE) {
        delete ScribbleArea::watermark;
        ScribbleArea::watermark = NULL;
        if(win->iapButton)
          win->iapButton->setVisible(false);
        win->redraw();
      }
#if ENABLE_UPDATE
      else if(event->user.code == UPDATE_CHECK)
        updateInfoReceived((char*)event->user.data1);
#endif
#if PLATFORM_ANDROID
      else if(event->user.code == APP_SUSPEND) {
        appSuspending();
        saveSem.post();  // signal completion to Android thread
      }
      else if(event->user.code == STORAGE_PERMISSION) {
        if(event->user.data1) {
          const char* p = "/sdcard/styluslabs/write/";
          if(createPath(p)) {
            docRoot = p;
            if(cfg->loadConfigFile("/sdcard/styluslabs/.write.xml"))
              cfgFile = "/sdcard/styluslabs/.write.xml";
            tempPath = "/sdcard/styluslabs/.temp/";
            createPath(tempPath.c_str());
          }
        }
        if(!openHelp())
          openOrCreateDoc();
      }
#endif
      return true;
    }
    return false;
  }
}

const int ScribbleApp::volUpActions[] = {0, ID_REDO, ID_ZOOMIN, ID_PREVPAGE, ID_NEXTPAGE, ID_EMULATEPENBTN};
const int ScribbleApp::volDnActions[] = {0, ID_UNDO, ID_ZOOMOUT, ID_NEXTPAGE, ID_PREVPAGE, ID_EMULATEPENBTN};

bool ScribbleApp::keyPressEvent(SDL_Event* event)
{
  SDL_Keycode key = event->key.keysym.sym;
  ScribbleInput::pressedKey = 0;  // will only be set to new key if not handled
  switch(key) {
  case SDLK_ESCAPE:
    // cancel action now handled in ScribbleWidget
    // quick exit for debugging version
    if(SCRIBBLE_DEBUG)
      finish();
    break;
  case SDLK_SEMICOLON:
    penSelected(currPenIndex > 0 ? currPenIndex-1 : cfg->pens.size() - 1);
    break;
  case SDLK_QUOTE:
    penSelected(currPenIndex+1 < int(cfg->pens.size()) ? currPenIndex+1 : 0);
    break;
#if PLATFORM_ANDROID
  case SDLK_AC_BACK:
    // hide bookmarks if autohide enabled; otherwise, display document list
    // use Android 4.4 immersive mode instead of disabling back button
    if(cfg->Bool("backKeyEnabled")) {
      // back key press on android which is not accepted generates a close event; this seems to
      //  work automatically for dialogs, but wasn't for main window
      if(backToDocList)
        openDocument();
      else {
        AndroidHelper::moveTaskToBack();
        // if backToDocList is false and we do moveTaskToBack(), focus will return to the launching activity
        //  (e.g. file manager app); but Write may be subsequently launched from recent tasks list or
        //  launcher, in which case we want back key to show doc list
        // Note, openDocument(std::string) may be called from doc list dialog's event loop, so we have to avoid
        //  running this line in that case!
        backToDocList = true;
      }
    }
    break;
#endif
  case SDLK_VOLUMEUP:
  case SDLK_VOLUMEDOWN:
  {
    // note that overriding volume button behavior doesn't work (and isn't allowed) on iOS
    int idx = cfg->Int("volButtonMode");
    if(volUpActions[idx] == ID_EMULATEPENBTN)
      ScribbleInput::pressedKey = key;
    else if(idx > 0)
      activeDoc()->doCommand(key == SDLK_VOLUMEUP ? volUpActions[idx] : volDnActions[idx]);
    break;
  }
  default:
    // Action shortcuts
    if(!win->shortcuts.empty()) {
      // we could get a slight optimization by returning if pressed key is a modifier, but not worth the trouble
      std::string keystr;
      Uint16 mods = event->key.keysym.mod;
#if PLATFORM_OSX
      if(mods & KMOD_GUI) keystr.append("Ctrl+");  // Mac OS Cmd key
#else
      if(mods & KMOD_CTRL) keystr.append("Ctrl+");
#endif
      if(mods & KMOD_ALT)
        keystr.append("Alt+");
      if(mods & KMOD_SHIFT)
        keystr.append("Shift+");
      keystr.append(SDL_GetKeyName(event->key.keysym.sym));
      auto it = win->shortcuts.find(keystr);
      if(it != win->shortcuts.end()) {
        if(it->second->enabled())
          it->second->onTriggered(); //gui, event);
        return true;
      }
    }
    // currently, saved pens are custom menu items w/ no action, so shortcuts won't work - fix this!
    if(key >= SDLK_1 && key <= SDLK_8) {
      if(key - SDLK_1 < int(cfg->pens.size()))
        penSelected(key - SDLK_1);
      break;
    }
    ScribbleInput::pressedKey = key;
    return false;  // key not handled
  }
  return true;  // key handled
}

#if PLATFORM_IOS
// these are called from ioshelper.m
void pencilBarrelTap(void)
{
  // I don't think this is called from a separate thread, but we need an event so that processEvents loop runs
  SDL_Event event = {0};
  event.type = ScribbleApp::scribbleSDLEvent;
  event.user.code = ScribbleApp::SIMULATE_PEN_BTN;
  event.user.timestamp = SDL_GetTicks();
  SDL_PushEvent(&event);
  PLATFORM_WakeEventLoop();  // might not be needed
}

void* loadDocumentContents(void* data, size_t len, size_t capacity, const char* url, void* uidoc)
{
  return new UIDocStream(data, len, capacity, url, uidoc); //, len + (4 << 20));  // reserve extra 4 MB
}

const char* getCfgString(const char* name, const char* dflt) { return ScribbleApp::cfg->String(name, dflt); }
//const char* getCfgInt(const char* name, int dflt) { return ScribbleApp::cfg->Int(name, dflt); }

void iapCompleted(void)
{
  // I don't think this is called from a separate thread, but we need an event so that processEvents loop runs
  SDL_Event event = {0};
  event.type = ScribbleApp::scribbleSDLEvent;
  event.user.code = ScribbleApp::IAP_COMPLETE;
  event.user.timestamp = SDL_GetTicks();
  SDL_PushEvent(&event);
  PLATFORM_WakeEventLoop();  // might not be needed
}
#elif PLATFORM_ANDROID
bool ScribbleApp::hasAndroidPermission()
{
  // 0x1 - WRITE_EXTERNAL_STORAGE, 0x2 - API level >= 30, 0x4 - MANAGE_EXTERNAL_STORAGE
  int permis = AndroidHelper::doAction(A_CHECK_PERM);
  return permis == 0x1 || (permis & 0x4);
  //return FSPath("/sdcard/styluslabs/write/.nomedia").exists();
}

bool ScribbleApp::requestAndroidPermission()
{
  if(hasAndroidPermission())
    return false;  // already has permission
  auto choice = messageBox(Question, _("Storage Access"),
      _("To open documents in shared folders, enable storage access for Write."), {_("OK"), _("Cancel")});
  if(choice != _("OK"))
    return false;
  AndroidHelper::doAction(A_REQ_PERM);
  ScribbleApp::app->appSuspending();
  finish();
  return true;
}
#endif

Clipboard* ScribbleApp::importExternalDoc(SvgDocument* doc)
{
  // discard if no paintable content; this won't catch empty doc if viewbox is set!
  if(!doc->bounds().isValid()) {
    delete doc;
    return NULL;
  }
  // clipboard effectively unwraps top level document - avoid this if doing so will alter doc
  if(doc->stylesheet()) {
    // remove CSS selector and convert CSS style to inline style
    doc->setStylesheet(NULL);
    doc->cssToInlineStyle();
    // remove CSS fragment nodes ... we should support select("style") by checking SvgXmlFragment name
    for(SvgNode* node : doc->select("unknown")) {
      if(strcmp(static_cast<SvgXmlFragment*>(node)->fragment->name(), "style") == 0) {
        node->parent()->asContainerNode()->removeChild(node);
        delete node;
      }
    }
  }
  Clipboard* clip = NULL;
  if(!doc->viewBox().isValid() && doc->getTransform().isTranslate())
    clip = new Clipboard(doc);  // this effectively removes the top level <svg>
  else {
    clip = new Clipboard;
    clip->content->addChild(doc);
  }
  // make sure width and height are set for <svg>s with viewBox so they doesn't use page width and height,
  //  which can change
  // perhaps we should do this in ScribbleArea::doPasteAt instead, but I'd rather not have a bunch of code
  //  handling edge cases for external SVG spread about
  for(SvgNode* node : clip->content->children()) {
    if(node->type() == SvgNode::DOC) {
      SvgDocument* subdoc = static_cast<SvgDocument*>(node);
      if(subdoc->viewBox().isValid()) {
        if(subdoc->width().isPercent())
          subdoc->setWidth(std::min(activeArea()->getViewWidth()/3.0, activeArea()->getCurrPage()->width()/3));
        if(subdoc->height().isPercent())
          subdoc->setHeight(std::min(activeArea()->getViewHeight()/3.0, activeArea()->getCurrPage()->height()/3));
      }
    }
  }
  return clip;
}

void ScribbleApp::dropEvent(SDL_Event* event)
{
#if PLATFORM_IOS
  UIDocStream* strm = (UIDocStream*)event->user.data1;
  long mode = (long)event->user.data2;
  if(mode == iosOpenDocMode || mode == iosChooseDocMode) {
    if(!doOpenDocument(strm))
      openOrCreateDoc(false);  // reopen doc browser on failure
    else if(activeArea() == scribbleAreas[0]) {  // only save bookmark for primary doc
      char* bkmk = iosGetSecuredBookmark(strm->uiDocument);
      cfg->set("iosBookmark0", bkmk ? bkmk : "");
      free(bkmk);
    }
  }
  else if(mode == iosUpdateDocMode) {
    // figure out which ScribbleDoc owns the doc
    for(ScribbleDoc* doc : scribbleDocs) {
      UIDocStream* docstrm = (UIDocStream*)doc->document->blockStream.get();
      if(docstrm && docstrm->uiDocument == strm->uiDocument) {
        if(doc->isModified()) {
          // restore old buffer on WriteDocument, since we may need it for ensurePagesLoaded for saveAs
          iosSaveDocument(strm->uiDocument, docstrm->data(), 0);  // len = 0 disables saving
          // free new buffer immediately
          strm->uiDocument = NULL;
          delete strm;
          iosSaveAs(iosConflictSaveMode);  //execDocumentList(DocumentList::SAVE_DOC);
        }
        else {
          docstrm->uiDocument = NULL;
          if(doc->openDocument(strm) == Document::LOAD_OK)
            messageBox(Info, _("Document reloaded"),
                fstring(_("%s has been reloaded due to modification outside of Write."),
                docShortName(strm->name()).c_str()));
        }
        break;
      }
    }
  }
  else if(mode == iosInsertDocMode)  // insert document
    activeDoc()->insertDocument(strm);
  else if(mode == iosSaveAsMode || mode == iosConflictSaveMode) {
    auto flags = FSPath(strm->name()).extension() == "html" ? Document::SAVE_MULTIFILE : 0;
    if(activeDoc()->saveDocument(strm, Document::SAVE_FORCE | flags))
      onLoadFile(strm->name());
    else
      messageBox(Error, _("Save As"), _("Error saving document. Please try a different folder."));
  }
#else
  FSPath fileinfo(event->drop.file);
  SDL_free(event->drop.file);
  std::string basename = fileinfo.baseName();
  std::string ext = fileinfo.extension();
  if(!fileinfo.exists())
    return;
  if(documentList && documentList->isVisible()) {}  // always try to open as doc if doc list visible
  else if(ext == "svgz" || (ext == "svg" && !StringRef(basename).chop(3).endsWith("_page"))) {
    // we load as Document to check if this is a Write document, then reload as Write doc or external SVG
    //  depending on result - not ideal, but this isn't expected to be a common use case
    FileStream* strm = new FileStream(fileinfo.c_str(), "rb");
    Document newdoc;  // newdoc will take ownership of strm
    // ensurePagesLoaded() will return true as long as content has valid bounds
    if(newdoc.load(strm, true) != Document::LOAD_OK && newdoc.numPages() < 2) {
      // Page may modify SVG (e.g. removing viewBox), so just reload
      SvgDocument* svgdoc = SvgParser().parseFile(fileinfo.c_str());
      Clipboard* clip = svgdoc ? importExternalDoc(svgdoc) : NULL;
      if(clip) {
        // can we get drop event on Android?  if so, how to handle this?
        int x, y, x0, y0;
        SDL_GetGlobalMouseState(&x, &y);
        SDL_GetWindowPosition(win->sdlWindow, &x0, &y0);
        // offset.isNan() selects PasteCenter instead of PasteOrigin
        clip->content->addClass("external");
        activeArea()->clipboardDropped(clip, Point(x - x0, y - y0)*gui->inputScale, Point(NaN, NaN));
        delete clip;
        return;
      }
    }
  }
  else if(ext == "bmp" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "png") {
    // TODO: we should figure out what area image was dropped on and insert there!
    insertImage(fileinfo.c_str());
    return;
  }
  // default: try to load as document
  if(maybeSave())
    openDocument(fileinfo.c_str());
#endif
}

void ScribbleApp::appSuspending()
{
  if(!activeDoc())  // app may not be initialized yet
    return;
  // save if modified or multiple file doc (to save view position)
  for(ScribbleDoc* doc : scribbleDocs) {
    if(doc->nViews > 0 && (doc->isModified() || doc->cfg->Bool("saveUnmodified") || doc->document->isEmptyFile())) {
      // no chance to prompt user, so don't use doSave()
      if(!doc->fileName()[0]) {
        if(doc->isModified()) { // don't bother unless doc is actually modified
          doc->saveDocument(createRecoveryName(FSPath(cfg->String("currFolder"), "Untitled").c_str()).c_str());
          onLoadFile(doc->fileName());  // add to recent doc list before saving config
        }
      }
      else
        doc->saveDocument();  //checkExtModified(doc) || doc->saveDocument() -- can't prompt anyway
    }
  }
  saveConfig();
  if(!disableConfigSave)
    cfg->saveConfigFile(cfgFile.c_str(), true);
}

/// clipboard ///
// move this stuff (and overlay stuff?) to a ClipboardManager class?

// sync with system clipboard - remember that on Linux, clipboard contents are lost when owning app closes
void ScribbleApp::loadClipboard()
{
#if !PLATFORM_LINUX
  // on X11 (not sure about Wayland), we only get SDL_CLIPBOARDUPDATE when we lose ownership of clipboard,
  //  so we must try to load every time we gain focus unless we own it - so we can't reset clipboardExternal
  clipboardExternal = false;
#endif
  clipboardSerial = systemClipboardSerial();

#if PLATFORM_WIN
  Image clipImg = getClipboardImage();
  if(!clipImg.isNull()) {
    setClipboardToImage(std::move(clipImg));
    return;
  }
#elif PLATFORM_IOS
  // getClipboardImage calls imagePicked() and returns 1 if image available on clipboard
  if(iosGetClipboardImage())
    return;
#endif

#if PLATFORM_LINUX
  // SDL_GetClipboardText on Linux doesn't support INCR, so fails w/ large amounts of text
  requestClipboard(win->sdlWindow);  // async, so will set clipboard when contents are ready
#else
  const char* clipText = SDL_GetClipboardText();
  if(clipText && clipText[0])
    loadClipboardText(clipText);
  SDL_free((void*)clipText);
#endif
}

void ScribbleApp::loadClipboardText(const char* clipText, size_t len)
{
  SvgDocument* svgDoc = NULL;
  StringRef clipRef = StringRef(clipText, len ? len : strlen(clipText)).trimL();
  // don't try to load clipboard as SVG unless first char is "<"
  if(clipRef.startsWith("<")) {
    // don't require ' ' after "<svg" since it could be any whitespace
    svgDoc = clipRef.startsWith("<svg") || clipRef.startsWith("<?xml") || clipRef.startsWith("<!DOCTYPE") ?
        SvgParser().parseString(clipRef.data(), clipRef.size()) :
        SvgParser().parseFragment(clipRef.data(), clipRef.size());
    if(!svgDoc) {
      int offset = clipRef.find("<svg");
      if(offset >= 0)
        svgDoc = SvgParser().parseString(clipRef.data() + offset, clipRef.size() - offset);
    }
  }
  Clipboard* clip = svgDoc ? importExternalDoc(svgDoc) : NULL;
  if(clip) {
    clipboard.reset(clip);
    clipboard->content->addClass("external");
    clipboardPage = NULL;
    clipboardFlags = 0;
    refreshUI(activeDoc(), UIState::ClipboardChange);  // enabled state of paste may have changed
  }
}

// called when main window loses focus
bool ScribbleApp::storeClipboard()
{
  if(!clipboard || !newClipboard)
    return false;
  std::ostringstream ss;
  clipboard->saveSVG(ss);
  SDL_SetClipboardText(ss.str().c_str());
  newClipboard = false;
  clipboardExternal = false;
  clipboardSerial = systemClipboardSerial();
  return true;
}

void ScribbleApp::setClipboardToImage(Image img, bool lossy)
{
  Dim imgw = img.getWidth()*ScribbleView::unitsPerPx;
  Dim imgh = img.getHeight()*ScribbleView::unitsPerPx;
  Dim s = std::min(Dim(1),
      std::min(activeArea()->getCurrPage()->width()/2/imgw, activeArea()->getCurrPage()->height()/2/imgh));
  Rect bbox = Rect::ltwh(100*ScribbleView::unitsPerPx, 100.5*ScribbleView::unitsPerPx, imgw*s, imgh*s);
  img.encoding = lossy ? Image::JPEG : Image::PNG;
  Element* pic = new Element(new SvgImage(std::move(img), bbox));
  clipboard.reset(new Clipboard);
  clipboard->addStroke(pic);
  //clipboard->content->addClass("external");  -- not needed since we already have scaled image
  clipboardPage = NULL;
  clipboardFlags = 0;
  refreshUI(activeDoc(), UIState::ClipboardChange);
}

void ScribbleApp::setClipboard(Clipboard* cb, const Page* srcpage, int flags)
{
  clipboard.reset(cb);
  clipboardPage = srcpage;
  clipboardFlags = flags;
  newClipboard = true;
  clipboardExternal = false;
  clipboardSerial = systemClipboardSerial();
}

// this is needed to get reasonable behavior w/ iOS split-screen since clipboard notifications don't work
void ScribbleApp::pasteClipboard()
{
  if(clipboardSerial != systemClipboardSerial())  // false on Linux since clipboardSerial is always -1
    loadClipboard();
  doCommand(ID_PASTE);
}

/// openURL ///

bool ScribbleApp::openURL(const char* url)
{
#if PLATFORM_WIN
  HINSTANCE result = ShellExecute(0, 0, PLATFORM_STR(url), 0, 0, SW_SHOWNORMAL);
  // ShellExecute returns a value greater than 32 if successful
  return (int)result > 32;
#elif PLATFORM_ANDROID
  AndroidHelper::openUrl(url);
  return true;
#elif PLATFORM_IOS
  if(!strchr(url, ':'))
    iosOpenUrl((std::string("http://") + url).c_str());
  else
    iosOpenUrl(url);
  return true;
#elif PLATFORM_OSX
  return strchr(url, ':') ? macosOpenUrl(url) : macosOpenUrl((std::string("http://") + url).c_str());
#else  // Linux
  system(fstring("xdg-open '%s' || x-www-browser '%s' &", url, url).c_str());
  return true;
#endif
}

/// pen ///

// type of `changed` should be PenToolbar::ChangeFlag but I'm lazy
void ScribbleApp::penChanged(int changed)
{
  const ScribblePen& pen = penToolbar->pen;
  if(changed & PenToolbar::SAVE_PEN) {
    cfg->savePen(pen, changed & ~PenToolbar::SAVE_PEN);
    win->refreshPens(activeDoc());
  }
  else if(changed == PenToolbar::YIELD_FOCUS)
    gui->setFocused(activeArea()->widget);
  else if(penToolbar->mode == PenToolbar::PEN_MODE || changed == PenToolbar::PEN_CHANGED)
    setPen(pen);
  else if(penToolbar->mode == PenToolbar::BOOKMARK_MODE)
    bookmarkColor = pen.color;
  else if(penToolbar->mode == PenToolbar::SELECTION_MODE) {
    size_t hpos = activeDoc()->history->histPos();
    // must always create undo item if syncing
    bool undoable = !(changed & PenToolbar::UNDO_PREV) || historyPos == hpos || activeDoc()->scribbleSync;
    if(undoable)
      historyPos = hpos;

    activeArea()->setStrokeProperties(StrokeProperties(
        changed & PenToolbar::COLOR_CHANGED ? pen.color : Color::INVALID_COLOR,
        changed & PenToolbar::WIDTH_CHANGED ? pen.width : -1), undoable);
  }
}

void ScribbleApp::setPen(const ScribblePen& pen)
{
  currPen = pen;
  // to handle transparent pens (highlighters), just mix colors instead of having two separate fills
  Color c = Color::mix(pen.color, activeDoc()->getCurrPageColor());
  const_cast<SvgNode*>(win->drawIcon)->setAttr<color_t>("fill", c.color);
  // must manually dirty buttons using icon since icon is not part of GUI doc (it's <use>d)
  for(Button* btn : win->actionDraw->buttons)
    btn->node->setDirty(SvgNode::PIXELS_DIRTY);
}

void ScribbleApp::updatePenToolbar()
{
  if(activeArea()->hasSelection())
    penToolbar->setPen(activeArea()->getPenForSelection(), PenToolbar::SELECTION_MODE);
  else if(scribbleMode->getMode() == MODE_BOOKMARK)
    penToolbar->setPen(ScribblePen(bookmarkColor, -1), PenToolbar::BOOKMARK_MODE);
  else
    penToolbar->setPen(currPen, PenToolbar::PEN_MODE);
}

// load saved pen
void ScribbleApp::penSelected(int penindex)
{
  ScribblePen* pen = activeDoc()->cfg->getPen(penindex);
  if(pen) {
    currPenIndex = penindex;
    setPen(*pen);
    setMode(MODE_STROKE);
    if(cfg->Bool("applyPenToSel"))
      activeArea()->setStrokeProperties(StrokeProperties(pen->color, pen->width));  // no-op if no sel
  }
}

/// painting and commands ///

Dim ScribbleApp::getPreScale()
{
  // TODO: remove use of this fn
  return Dim(1.0);  //cfg->Float("preScale");
}

void ScribbleApp::getScreenPageDims(int* w, int* h)
{
//#ifdef Q_OS_ANDROID
//  int androiddims = android_detectScreenSize();
//  int screenw = androiddims >> 16;
//  int screenh = androiddims & 0x0000FFFF;
//#else
  SDL_Rect r;
#if PLATFORM_IOS
  SDL_GL_GetDrawableSize(sdlWindow, &r.w, &r.h);
#else
  SDL_GetDisplayBounds(std::max(0, SDL_GetWindowDisplayIndex(sdlWindow)), &r);  // or SDL_GetDisplayUsableBounds
#endif
  // 0,0 page size causes problems (e.g. errors loading doc)
  r.w = std::max(200, r.w);
  r.h = std::max(200, r.h);
  *w = 2*int((std::min(r.w, r.h)*ScribbleView::unitsPerPx - 24)/2);
  *h = 2*int((std::max(r.w, r.h)*ScribbleView::unitsPerPx - 24)/2);
}

void ScribbleApp::refreshUI(ScribbleDoc* doc, int reason)
{
  win->refreshUI(doc, reason);
}

// newdoc means change of Document or of ScribbleDoc - in either case we want to reset scroll position
void ScribbleApp::repaintBookmarks(bool newdoc)
{
  if(newdoc)
    bookmarkArea->setScribbleDoc(activeDoc());  // use this to reset scroll position
  if(win->actionShow_Bookmarks->checked())
    bookmarkArea->repaintBookmarks();  // force refresh
}

void ScribbleApp::doCommand(int cmd)
{
  if(cmd & CLIPPING_CMD)
    clippingDoc->doCommand(cmd & ~CLIPPING_CMD);
  else
    activeDoc()->doCommand(cmd);
}

void ScribbleApp::setMode(int mode)
{
  if((scribbleMode->getMode() == MODE_PAGESEL) != (mode == MODE_PAGESEL)) {
    for(ScribbleDoc* doc : scribbleDocs) {
      doc->clearSelection();
      doc->uiChanged(UIState::SelChange);
      doc->repaintAll();
      doc->doRefresh();
    }
  }
  scribbleMode->setMode(mode);
  win->updateMode();
}

void ScribbleApp::hideBookmarks()
{
  if(win->bookmarkPanel->isVisible())
    win->toggleBookmarks();
}

void ScribbleApp::hideClippings()
{
  if(win->clippingsPanel->isVisible())
    win->toggleClippings();
}

void ScribbleApp::showSelToolbar(Point pos)
{
  // sel toolbar only opened on pen up, so don't make pressed (because outside_pressed event will invoke 2nd
  //  call to ScribbleArea::doReleaseEvent)
  gui->showContextMenu(win->selPopup, pos, NULL, false);   //if(!pos.isNaN())
}

bool ScribbleApp::oneTimeTip(const char* id, Point pos, const char* message)
{
  return win->oneTimeTip(id, pos, message);
}

/// clippings ///

extern const char* clippingDocHTML;

// I think it is reasonable not to compress clippings doc since each page is likely to be small (but probably
//  don't want to save as single file since user could add a large clipping)
bool ScribbleApp::loadClippingsDoc()
{
  if(!clippingsPath.empty())
    return true;  // already loaded
  // load clippings doc
  clippingsPath = cfg->String("clippingDoc", "");
  if(clippingsPath.empty()) {
    clippingsPath = savedPath + "clippings.html";
    cfg->set("clippingDoc", clippingsPath.c_str());
  }
#if PLATFORM_IOS
  // clippings path is relative to HOME on iOS (since HOME can change!)
  clippingsPath = FSPath(getenv("HOME"), clippingsPath).c_str();
#endif
  Document::loadresult_t res = clippingDoc->openDocument(clippingsPath.c_str());
  if(res != Document::LOAD_OK) {
    // if file already exists, don't replace it (so user can try to recover)
    if(FSPath(clippingsPath).exists()) {
      messageBox(Warning, _("Damaged document"),
          fstring(_("Error opening clipping document - please examine %s"), clippingsPath.c_str()));
      clippingsPath = "";
      return false;
    }
    else {
      createPath(FSPath(clippingsPath).parent().c_str());
      // MemStream will be freed when replaced by FileStream on save
      clippingDoc->openDocument(new MemStream(clippingDocHTML, strlen(clippingDocHTML)));
      clippingDoc->saveDocument(clippingsPath.c_str(), Document::SAVE_FORCE | Document::SAVE_MULTIFILE);
    }
  }
  return true;
}

/// file handling ///

// Document handling in UI:
// - save automatically on exit (configurable); prompt if document doesn't have a filename
// - save as to save under a different name
// - "discard changes" to exit without saving

void ScribbleApp::insertDocument()
{
  std::string filename = execDocumentList(DocumentList::CHOOSE_DOC);
  if(!filename.empty() && activeDoc()->insertDocument(new FileStream(filename.c_str(), "rb")) != Document::LOAD_OK)
    messageBox(Warning, _("Error inserting document"), fstring(_("An error occured opening %s"), docDisplayName(filename).c_str()));
}

std::string ScribbleApp::execDocumentList(int mode, const char* exts, bool cancelable)
{
#if PLATFORM_IOS
  // close current document (since it could be touched by doc browser) if doc browser can't be cancelled
  if(mode == DocumentList::OPEN_DOC) {
    // use doc chooser for split ... would be nice to use doc browser to allow creating new doc, but
    //  confusing UX and we'd need to add a cancel button to browser
    if(!cancelable) {
      if(activeDoc()->nViews < 2)
        activeDoc()->newDocument();
      showDocumentBrowser();
    }
    else
      iosPickDocument(iosChooseDocMode);
  }
  else if(mode == DocumentList::CHOOSE_DOC)
    iosPickDocument(iosInsertDocMode);
  else if(mode == DocumentList::SAVE_DOC)
    iosSaveAs(iosSaveAsMode);
  return "";
#else
  if(!documentList)
    documentList = new DocumentList(docRoot.c_str(), tempPath.c_str());
  documentList->setup(win, DocumentList::Mode_t(mode), exts, cancelable);
  execWindow(documentList);
  return documentList->result > 0 ? documentList->selectedFile : "";
#endif
}

bool ScribbleApp::openOrCreateDoc(bool cancelable)
{
  backToDocList = true;
  // on Android, reopenLastDoc is not a user pref, but determined by whether user was viewing doc vs. doc list
  if(!cancelable && PLATFORM_MOBILE)
    cfg->set("reopenLastDoc", false);
  // let's always allow doc list to be closed on desktop
  std::string filename = execDocumentList(DocumentList::OPEN_DOC, NULL, cancelable || !PLATFORM_MOBILE);
  int res = documentList ? documentList->result : DocumentList::REJECTED;
  if(res == DocumentList::OPEN_HELP) {
    if(!openHelp()) {
      openURL("http://www.styluslabs.com/write/Help.html");
      return openOrCreateDoc(cancelable);  // try again
    }
  }
  else if(filename.empty()) {  // document list canceled
#if PLATFORM_ANDROID
    cfg->set("reopenLastDoc", true);  // reopenLastDoc is cleared by openDocument() on Android
#endif
    checkExtModified();  // handle case of current doc being deleted or renamed via document list
  }
  else if(res == DocumentList::OPEN_WHITEBOARD) {
    //cfg->set("currFolder", filename.c_str());
    if(!openSharedDoc())
      return openOrCreateDoc(cancelable);  // try again
  }
  else if(res == DocumentList::EXISTING_DOC) {
    //if(scribbleDoc->fileName != documentList->selectedFile || getFileMTime(currFileName.c_str()) != currFileLastMod)
    return doOpenDocument(filename) || openOrCreateDoc(cancelable);
  }
  else if(res == DocumentList::OPEN_COPY) {
    if(!doOpenDocument(documentList->selectedSrcFile))
      return openOrCreateDoc(cancelable);  //false;
    if(!activeDoc()->saveDocument(filename.c_str(), Document::SAVE_FORCE)) {
      // this should never happen since doc list creates the file first
      doNewDocument();
      messageBox(Error, _("Save As"), _("Error saving document. Please try a different folder."));
      return false;
    }
    onLoadFile(filename);  // filename not the same as when opened
  }
  else if(res == DocumentList::NEW_DOC) {
    if(!doOpenDocument(filename))
      return openOrCreateDoc(cancelable);  //false;
    int ruleidx = documentList->selectedRuling;
    if(ruleidx > 0 && ruleidx < 8) {
      PageProperties props(0, 0, RulingDialog::predefRulings[ruleidx][0],
          RulingDialog::predefRulings[ruleidx][1], RulingDialog::predefRulings[ruleidx][2],
          Color::fromRgb(cfg->Int("pageColor")), RulingDialog::predefRulings[ruleidx][3]);
      activeDoc()->setPageProperties(&props, true, true, false, false);
    }
    // force multi-file for .html extension, since new file will default to single file because _page001.svg
    //  doesn't exist; user can use .htm for single file doc
    if(FSPath(filename).extension() == "html")
      activeDoc()->saveDocument((IOStream*)NULL, Document::SAVE_MULTIFILE);
  }
  return true;
}

bool ScribbleApp::openHelp()
{
  if(!maybeSave())
    return false;
  const char* helpdoc = "Intro.svg";
  FSPath helpTemp = FSPath(tempPath).child(helpdoc);
  if(helpTemp.exists())
    removeFile(helpTemp.c_str());
#if PLATFORM_ANDROID
  bool ok = AndroidHelper::rawResourceToFile("Intro.svg", helpTemp.c_str());
#else
  FSPath helpSrc(appDir, helpdoc);
  bool ok = helpSrc.exists() && copyFile(helpSrc, helpTemp);
#endif
  if(ok && activeDoc()->openDocument(helpTemp.c_str()) != Document::LOAD_FATAL) {
    onLoadFile(activeDoc()->fileName(), false);  // add to recents = false
    // this is a hack to prevent crash casting to UIDocStream for thumbnail on iOS
    activeDoc()->cfg->set("saveThumbnail", false);
    return true;
  }
  return false;
}

std::string ScribbleApp::docDisplayName(std::string filename, int maxwidth)
{
#if PLATFORM_IOS
  return docShortName(filename, maxwidth);
#else
  if(filename.empty())
    return _("Untitled");  //New Document" -- this makes title button look like a new doc button
#if PLATFORM_ANDROID
  StringRef fref(filename);
  if(fref.startsWith(docRoot.c_str()))
    fref += docRoot.size();
  if(fref.endsWith(fileExt.c_str()))
    fref.chop(fileExt.size() + 1);
  filename = fref.toString();
#else
  const char* homestr = getenv("HOME");
  if(homestr && StringRef(filename).startsWith(homestr))
    filename.replace(0, strlen(homestr), "~");
#endif
  // elide from left if necessary
  /*if(maxwidth > 0) {
    Dim textwidth = Painter::textBounds(0, 0, filename.c_str());
    if(textwidth > maxwidth) {
      int tokeep = (filename.size()*maxwidth)/textwidth - 3.5;
      filename.replace(0, filename.size() - tokeep, "...");
    }
  }*/
  return filename;
#endif
}

std::string ScribbleApp::docShortName(const std::string& filename, int maxwidth)
{
  return filename.empty() ? _("Untitled") : FSPath(filename).baseName();
}

void ScribbleApp::setWinTitle(const std::string& filename)
{
  const char* winTitle =
      SCRIBBLE_DEBUG ? (" - Write (r" PPVALUE_TO_STRING(SCRIBBLE_REV_NUMBER) ")") : " - Stylus Labs Write";
  win->setTitle((docDisplayName(filename) + winTitle).c_str());
  win->titleStr = docShortName(filename);  // storage for full title string, since button text may be elided
  win->titleButton->setText(win->titleStr.c_str());
  win->titleButton->setShowTitle(true);  // text may have been hidden by layout
}

// to be called after document successfully loaded
void ScribbleApp::onLoadFile(const std::string& filename, bool addrecent)
{
  dismissNotify();  // close any old notifications
  setWinTitle(filename);
  activeArea()->widget->fileNameLabel->setText(docShortName(filename).c_str());

  if(!filename.empty() && addrecent) {
    FSPath fileinfo(filename);
    // update recent docs; no automatic way to find or remove string w/o case sensitivity
    for(size_t ii = 0; ii < recentDocs.size(); ++ii) {
      if(strcasecmp(filename.c_str(), recentDocs[ii].c_str()) == 0) {
        recentDocs.erase(recentDocs.begin() + ii);
        break;
      }
    }
    // unbelieveably didn't have any limit on growth of recentDocs previously
    while(recentDocs.size() > 10)
      recentDocs.pop_back();
    // populate list before adding current file so it isn't shown on list while open
    populateRecentFiles();
#if PLATFORM_IOS
    // since we don't save security scoped bookmarks (yet?), don't add external files to recents
    if(!StringRef(filename).startsWith(docRoot.c_str()))
      return;
#endif
    recentDocs.insert(recentDocs.begin(), filename);
    //cfg->set("currFolder", fileinfo.parentPath().c_str());  -- now handled by DocumentList
  }
  else
    populateRecentFiles();
}

void ScribbleApp::newDocument()
{
  if(maybeSave()) {
    // DocumentList creates a new blank HTML file, which we then open
    if(!openOrCreateDoc())
      doNewDocument();
  }
}

void ScribbleApp::doNewDocument()
{
  activeDoc()->newDocument();
  onLoadFile("");
}

bool ScribbleApp::openDocument(std::string filename)
{
  if(documentList && documentList->isVisible())
    documentList->finish(DocumentList::REJECTED);
  backToDocList = false;
  // don't save clean doc - on Android, I think doc will always be clean when we're called
  return maybeSave() && doOpenDocument(filename);  //!activeDoc()->isModified() ||
}

bool ScribbleApp::openDocument()
{
  // on Android, we want doc list to be cancelable, but we still want to turn off reopenLastDoc
#if PLATFORM_ANDROID
  cfg->set("reopenLastDoc", false);
#endif
  return maybeSave() && openOrCreateDoc(!PLATFORM_IOS);  // cancelable except on iOS
}

bool ScribbleApp::doOpenDocument(std::string filename)
{
  // if SVG file, try to find the parent HTML file
  if(FSPath(filename).extension() == "svg") {
    // if user is trying to open a _pageXXX.svg file (not just any SVG file), prompt to open whole document
    // we do not check for existance of of HTML file, since Document::load will try to open all SVG files if
    //  the HTML file is missing
    size_t chopat = filename.rfind("_page");
    if(chopat != std::string::npos) {
      std::string htmlfile = filename.substr(0, chopat) + ".html";  // + fileExt;
      auto choice = messageBox(Question, _("Write"),
          _("You are attempting to open a single page SVG file. Would you like to try opening the entire document instead?"),
          {_("Yes"), _("No")});
      if(choice == _("Yes"))
        filename = htmlfile;
    }
  }
  FileStream* strm = new FileStream(filename.c_str(), "rb+");
  if(strm->is_open())
    return doOpenDocument(strm);
  // try opening as read-only
  strm->open("rb");
  if(!doOpenDocument(strm))  // still need to show error even if we can't open stream at all
    return false;
  // successfully opened read-only
  strm->filename.clear();  // prevent attempts to save to read-only location
#if PLATFORM_ANDROID
  requestAndroidPermission();
#endif
  messageBox(Warning, _("Read-only file"),
      _("The document cannot be saved to its current location, please choose a new location."));
  return doSaveAs();
}

bool ScribbleApp::doOpenDocument(IOStream* filestrm)
{
  std::string filename = filestrm->name();
  if(activeDoc()->nViews > 1) {
    if(filename == activeDoc()->fileName())
      return true;  // same doc selected for split ... do nothing
    // single doc split -> separate doc split
    if(scribbleDocs.size() < 2) {
      ScribbleDoc* doc = new ScribbleDoc(this, cfg, scribbleMode);
      doc->newDocument();
      scribbleDocs.push_back(doc);
    }
    ScribbleDoc* otherDoc = (activeDoc() == scribbleDocs[0]) ? scribbleDocs[1] : scribbleDocs[0];
    activeDoc()->removeArea(activeArea());
    otherDoc->addArea(activeArea()); // this makes otherDoc the active doc
    repaintBookmarks(true);
    for(ScribbleArea* area : scribbleAreas) {
      if(area != activeArea())
        area->widget->fileNameLabel->setText(docShortName(area->scribbleDoc->fileName()).c_str());
      area->widget->fileNameLabel->setVisible(true);
    }
  }
  else if(scribbleDocs.size() > 1) {
    ScribbleDoc* otherDoc = (activeDoc() == scribbleDocs[0]) ? scribbleDocs[1] : scribbleDocs[0];
    if(filename == otherDoc->fileName()) {
      // separate doc split -> single doc split
      activeDoc()->newDocument();  // close the doc being hidden!
      activeDoc()->removeArea(activeArea());
      otherDoc->addArea(activeArea()); // this makes otherDoc the active doc
      repaintBookmarks(true);
      for(ScribbleArea* area : scribbleAreas)
        area->widget->fileNameLabel->setVisible(false);
      onLoadFile(activeDoc()->fileName());
      delete filestrm;
      return true;  // do not reload doc in this case
    }
  }

  // try to open document - note that even if filestrm is not open, this could work (e.g. for missing .html)
  Document::loadresult_t res = activeDoc()->openDocument(filestrm);
  if(res == Document::LOAD_OK || res == Document::LOAD_EMPTYDOC) {
    onLoadFile(activeDoc()->fileName());
#if PLATFORM_MOBILE
    cfg->set("reopenLastDoc", true);
#endif
    return true;
  }
  else if(res != Document::LOAD_FATAL) {
    activeDoc()->checkAndClearErrors(true);  // clear errors so document can be saved
    onLoadFile(activeDoc()->fileName());
    if(res == Document::LOAD_NEWERVERSION) {
      messageBox(Warning, _("Newer document"), _("This document was created with a more recent"
          " version of Write. Saving with this version may result in data loss."));
    }
    else if(res == Document::LOAD_NONWRITE) {
      return openExternalDoc();
    }
    else {
      // we used to automatically save a copy, but on iOS, we cannot create new document outside app
      //  folder w/o user interaction; prompting makes things clearer for user anyway
      auto choice = messageBox(Warning, _("Damaged document"), _("Errors occurred while opening the document.  "
          "To avoid possible data loss, a copy should be saved."), {_("Save a copy"), _("Open anyway")});
      if(choice == _("Save a copy"))
        doSaveAs();
    }
    return true;
  }
#if PLATFORM_ANDROID
  if(requestAndroidPermission())
    return false;
#endif
  messageBox(Warning, _("Error opening document"), fstring(_("\"%s\" could not be opened."), filename.c_str()));
  return false;  //openOrCreateDoc() && !PLATFORM_IOS;
}

// handle opening external SVG (vs. importExternalDoc, which handles loading external doc to Clipboard)
bool ScribbleApp::openExternalDoc()
{
  auto choice = messageBox(Warning, _("Foreign document"), _("This file does not appear to be a Write document."
      "  Saving with Write could result in data loss - you will be prompted to save a copy."),
      {_("Use as background"), _("Ungroup all"), _("No change")});
  if(choice == _("Ungroup all")) {
    for(Page* page : activeDoc()->document->pages) {
      for(int ii = 0; ii < 32; ++ii) {  // counter is just to protect against infinite loop
        Selection sel(page);
        sel.selectAll();
        if(!sel.containsGroup())
          break;
        sel.ungroup();
      }
    }
  }
  else if(choice == _("Use as background")) {
    for(Page* page : activeDoc()->document->pages)
      page->contentToRuling();
  }
  doSaveAs();
  return true;
}

bool ScribbleApp::maybeSave(bool prompt)
{
  ScribbleDoc* doc = activeDoc();
  if(!doc->fileName()[0]) {
    if(!doc->isModified())
      return true;
    auto ret = messageBox(Question, _("Unsaved document"),
        _("The document has never been saved.\nWhat would you like to do?"),
        {_("Save"), _("Discard"), _("Cancel")});
    return (ret == _("Save")) ? doSaveAs() : (ret == _("Discard"));
  }
  if(prompt || doc->cfg->Bool("savePrompt")) {
    if(!doc->isModified())
      return true;
    auto ret = messageBox(Warning, _("Modified document"),
        fstring(_("Save changes to \"%s\"?"), docDisplayName(doc->fileName()).c_str()),
        {_("Save"), _("Discard"), _("Cancel")});
    if(ret != _("Save"))
      return (ret == _("Discard"));
  }
  // isEmptyFile(): we always save if doc loaded from empty file so that it has a thumbnail
  //  perhaps we should save immediately after loading empty file instead
  if(doc->isModified() || doc->cfg->Bool("saveUnmodified") || doc->document->isEmptyFile())
    return saveDocument();
  // no need to save ... in the future, we might save doc state to config here
  return true;
}

// close docs affected by document list file operation; one remaining bug is with split view filename label
void ScribbleApp::closeDocs(const FSPath& path)
{
  for(ScribbleDoc* doc : scribbleDocs) {
    if(StringRef(doc->fileName()).startsWith(path.c_str()) && !doc->isModified()) {
      doc->newDocument();
      if(activeDoc() == doc)
        onLoadFile("");
    }
  }
}

// returns true to indicate scribbleDoc is synced with disk file (so no save/loaded needed)
// An alternative would be QFileSystemWatcher, but then we'd have to deal with it being triggered by our own
//  writes to file
bool ScribbleApp::checkExtModified()
{
  for(ScribbleDoc* doc : scribbleDocs) {
    if(doc->activeArea)
      checkExtModified(doc);
  }
  return false;
}

bool ScribbleApp::checkExtModified(ScribbleDoc* doc)
{
#if PLATFORM_IOS || PLATFORM_EMSCRIPTEN
  return false;
#else
  std::string filename = doc->fileName();
  if(filename.empty() || doc->fileLastMod == 0 || !cfg->Bool("warnExtModified"))
    return false;
  const Timestamp lastmod = getFileMTime(filename.c_str());
  if(lastmod == doc->fileLastMod)
    return false;
  doc->fileLastMod = 0;  // prevent additional alerts until document is saved or reloaded

  setActiveArea(doc->activeArea);
  // just reload if our version isn't modified
  if(!doc->isModified()) {
    // assume external deletion was intentional
    if(!FSPath(filename).exists()) {
      doNewDocument();
      return true;
    }
    if(doc->openDocument(filename.c_str()) == Document::LOAD_OK) {
      // don't bother displaying message if doc list is on top
      if(documentList == NULL || !documentList->isVisible()) {
        messageBox(Info, _("Document reloaded"),
            fstring(_("%s has been reloaded due to modification outside of Write."), docShortName(filename).c_str()));
      }
      return true;
    }
  }
#ifdef QT_CORE_LIB
  // if we can't display a message box, just save to new file
  if(QGuiApplication::applicationState() == Qt::ApplicationSuspended) {
    doSaveAs();
    QMessageBox::information(this, "Stylus Labs Write",
        "Document was saved to new file because original was modified outside Write.");
    return true;
  }
#endif
  // the bad case: conflict between local and disk versions
  auto choice = messageBox(Warning, _("Save conflict"),
      fstring(_("%s has been modified outside Write.\nWhat would you like to do?"), docDisplayName(filename).c_str()),
      {_("Keep Both"), _("Discard Other"), _("Discard Current")});
  if(choice == _("Discard Other") && doc->saveDocument(filename.c_str())) {}
  else if(choice == _("Discard Current") && doc->openDocument(filename.c_str()) == Document::LOAD_OK) {}
  else
    doSaveAs();
  // this will cause doSave() to return immediately
  return true;
#endif
}

// this handles "Save" button
bool ScribbleApp::saveDocument()
{
  return doSave(activeDoc()) || doSaveAs();
}

bool ScribbleApp::doSave(ScribbleDoc* doc)
{
  if(!doc->fileName()[0])
    return false;
  if(checkExtModified(doc))
    return true;
  if(doc->saveDocument())
    return true;
  messageBox(Warning, _("Save error"), _("An error occurred saving the document.  Please try saving"
      " to a different location.  Contact support@styluslabs.com if this error persists."));
  return false;
}

#if PLATFORM_EMSCRIPTEN
#include "emscripten.h"

EM_JS(void, jsSaveFile, (const char* filename, void* data, int len),
{
  const view = new Uint8Array(Module.HEAPU8.buffer, data, len);
  const blob = new Blob([view], { type: 'octet/stream' });
  const url = window.URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.style = 'display:none';
  document.body.appendChild(a);
  a.href = url;
  a.download = UTF8ToString(filename);
  a.click();
  //setTimeout(function() { ... }, 0)
  window.URL.revokeObjectURL(url);
  document.body.removeChild(a);
});

bool ScribbleApp::doSaveAs()
{
  // Document will take ownership of memstream ... consider a flag to prevent this
  MemStream* strm = new MemStream;
  activeDoc()->checkAndClearErrors(true);  // clear errors so document can be saved
  activeDoc()->saveDocument(strm, Document::SAVE_FORCE);
  jsSaveFile("Untitled.svgz", strm->data(), strm->size());
  return true;
}
#else
bool ScribbleApp::doSaveAs()
{
  activeDoc()->checkAndClearErrors(true);  // clear errors so document can be saved
  // standard UX is to always create document file before editing, but on desktop document list can be
  //  cancelled to allow editing new unnamed file; for now, we use document list to get name to save new file;
  //  previously native OS file save dialog was used to get name
  std::string filename = execDocumentList(DocumentList::SAVE_DOC, "pdf svgz svg html htm");
  if(filename.empty())
    return false;
  if(FSPath(filename).extension() == "pdf") {
    writePDF(filename);
    return false;  // exporting PDF doesn't count as saving!
  }
  auto flags = FSPath(filename).extension() == "html" ? Document::SAVE_MULTIFILE : 0;
  if(!activeDoc()->saveDocument(filename.c_str(), Document::SAVE_FORCE | flags)) {
    messageBox(Error, _("Save As"), _("Error saving document. Please try a different folder."));
    return false;
  }
  onLoadFile(filename);
  //syncRequired = true;
  return true;
}
#endif

// for now, this is actually "Discard Changes"
void ScribbleApp::revert()
{
  // don't prompt unless document is modified; just use standard save prompt message
  if(activeDoc()->isModified() && !maybeSave(true))
    return;
  // always create a new document so we don't ask user again if they cancel doc list
  doNewDocument();
  // show doc list if enabled
  openOrCreateDoc();
}

// toappend defaults to " - recovered"
std::string ScribbleApp::createRecoveryName(std::string filename, const char* toappend)
{
  if(FSPath(filename).extension() == fileExt)
    filename.erase(filename.size() - fileExt.size() - 1);
  filename.append(toappend);
  // find available file name
  std::string recovfile = filename + "." + fileExt;
  for(int suffix = 1; FSPath(recovfile).exists(); suffix++)
    recovfile = fstring("%s (%d).%s", filename.c_str(), suffix, fileExt.c_str());
  return recovfile;
}

/// recent files ///

// what to do if recentDocs is empty?
void ScribbleApp::populateRecentFiles()
{
  // recent files menu is optional
  if(!win->menuRecent_Files)
    return;

  SDL_Rect screenrect;
  SDL_GetDisplayBounds(std::max(0, SDL_GetWindowDisplayIndex(sdlWindow)), &screenrect);  // or SDL_GetDisplayUsableBounds
  int maxwidth = 0.75*MIN(screenrect.w, screenrect.h);
  for(size_t ii = 0; ii < recentDocs.size(); ++ii) {
    if(recentFileActions.size() <= ii) {
      // should we use MainWindow::createAction instead?
      recentFileActions.emplace_back(new Action(fstring("recentFile%d", ii).c_str(), ""));
      win->menuRecent_Files->addAction(recentFileActions[ii].get());
    }
    std::string filename = recentDocs[ii];
    recentFileActions[ii]->setVisible(true);
    std::string shortname = docDisplayName(recentDocs[ii], maxwidth);
    recentFileActions[ii]->setTitle(fstring("%d %s", ii+1, shortname.c_str()).c_str());
    recentFileActions[ii]->onTriggered = [this, filename](){ openRecentFile(filename); };
    //recentFileActions[ii]->setUserData(recentDocs[ii]);
  }
  for(size_t ii = recentDocs.size(); ii < recentFileActions.size(); ++ii)
    recentFileActions[ii]->setVisible(false);
}

void ScribbleApp::openRecentFile(const std::string& filename)
{
  // maybeSave updates recent files, so we must save filename before we call it!
  if(maybeSave()) {
    // don't show error message if file has been deleted
    if(!FSPath(filename).exists() || !doOpenDocument(filename)) {
      // if opening doc fails, remove from recent doc list
      recentDocs.erase(std::remove(recentDocs.begin(), recentDocs.end(), filename), recentDocs.end());
      populateRecentFiles();
    }
  }
}

/// images ///

void ScribbleApp::insertImage()
{
#if PLATFORM_ANDROID
  AndroidHelper::getImage();  // will call insertImage(QImage) when user selects image
#elif PLATFORM_IOS
  showImagePicker();
#else
  // TODO: also show "Choose Image" in place of new doc/new folder/help buttons
  std::string filename = execDocumentList(DocumentList::CHOOSE_IMAGE, "jpg jpeg png");
  if(documentList->result > 0 && !filename.empty())
    insertImage(filename);
#endif
}

void ScribbleApp::insertImage(const std::string& filename)
{
  std::vector<unsigned char> buff;
  if(readFile(&buff, filename.c_str())) {
    Image img = Image::decodeBuffer(&buff[0], buff.size());
    if(!img.isNull()) {
      activeArea()->insertImage(std::move(img));
      return;
    }
  }
  messageBox(Warning, _("Error opening image"), fstring(_("Error reading \"%s\""), filename.c_str()));
}

void ScribbleApp::insertImage(Image image, bool fromintent)
{
  if(!fromintent && (documentList == NULL || !documentList->isVisible()))
    activeArea()->insertImage(std::move(image));
  else
    setClipboardToImage(std::move(image));
}

// callback for image insertion on Android ... since this may be called from a different thread, we push event
//  to ensure only main thread touches document (or clipboard)
void ScribbleApp::insertImageSync(Image image, bool fromintent)
{
  SvgGui::pushUserEvent(scribbleSDLEvent, INSERT_IMAGE, new Image(std::move(image)), fromintent ? (void*)0x1 : NULL);
  //PLATFORM_WakeEventLoop();
}

#if PLATFORM_IOS
// this is called from ioshelper.m
void imagePicked(const void* data, int len, int fromclip)
{
  if(fromclip)  // call directly so clipboard update check on paste works
    ScribbleApp::app->insertImage(Image::decodeBuffer((const unsigned char*)data, len), fromclip);
  else  // insertImageSync to ensure UI updated even though we should be on same thread
    ScribbleApp::insertImageSync(Image::decodeBuffer((const unsigned char*)data, len), fromclip);
}
#endif

// used for linux clipboard
extern "C" { void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image); }
void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image)
{
  if(is_image)
    ScribbleApp::app->insertImage(Image::decodeBuffer(buff, len), true);  // fromintent = true to set clipboard
  else
    ScribbleApp::app->loadClipboardText((const char*)buff, len);
}

/// PDF export ///

void ScribbleApp::writePDF(std::ostream& strm)
{
  Document* doc = activeDoc()->document;
  PdfWriter pdf(doc->numPages());
  pdf.anyHref = true;  // hack to work around our hack for links
  //pdf.compressionLevel = 0;  // for debugging
  Dim ptsPerDim = 72.0/150;
  pdf.resolveLink = [doc](const char* href, int* pgnum){ return doc->findNamedNode(href, pgnum); };
  for(Page* page : doc->pages) {
    page->ensureLoaded();
    pdf.newPage(page->width(), page->height(), ptsPerDim);
    pdf.drawNode(page->svgDoc.get());
  }
  pdf.write(strm);
}

bool ScribbleApp::writePDF(const std::string& filename)
{
  std::ofstream f(PLATFORM_STR(filename.c_str()), std::ios::out | std::ios::binary);
  if(!f)
    return false;
  writePDF(f);
  f.close();
  return true;
}

#if PLATFORM_EMSCRIPTEN
void ScribbleApp::exportPDF()
{
  std::stringstream strm;
  activeDoc()->checkAndClearErrors(true);  // clear errors so document can be saved
  writePDF(strm);
  std::string str = strm.str();
  jsSaveFile("Untitled.pdf", str.data(), str.size());
}
#else
void ScribbleApp::exportPDF()
{
  std::string filename = execDocumentList(DocumentList::SAVE_PDF, "pdf");
  if(filename.empty())
    return;
  if(!writePDF(filename))
    messageBox(Error, _("Export PDF"), _("Error saving document. Please try a different folder."));
}
#endif

/// dialogs ///

void ScribbleApp::createLink()
{
  //LinkDialog dialog(activeDoc());
  //execDialog(&dialog);  // blocking
  asyncDialog(new LinkDialog(activeDoc()));
}

void ScribbleApp::showPageSetup()
{
  //RulingDialog rulingDialog(activeDoc());
  //execDialog(&rulingDialog);  // blocking
  asyncDialog(new RulingDialog(activeDoc()));
}

void ScribbleApp::openPreferences()
{
  if(disableConfigSave)
    messageBox(Warning, _("Write"), _("Preferences will not be saved because some were set from command line."));
  //showSelToolbar(Point(NaN, NaN)); -- no longer possible to open prefs w/o sel toolbar being closed
  //ConfigDialog dialog(cfg);
  //int res = execDialog(&dialog);  // blocking
  asyncDialog(new ConfigDialog(cfg), [this](int res) {
    if(res == Dialog::ACCEPTED) {
      // dialog accepted; note we still save config file if res == 0 (rejected)
      loadConfig();
      for(ScribbleDoc* doc : scribbleDocs)
        doc->loadConfig(true);
      bookmarkArea->loadConfig(cfg);
      clippingDoc->loadConfig(true);
      // destroy doc list so that it will be recreated with new settings
      delete documentList;
      documentList = NULL;
    }
    else if(res == -1) {
      return;
    }
    // write config file immediately to better support multiple instances
    // would be nice if could also reread before opening dialog!
    if(!disableConfigSave && !cfg->saveConfigFile(cfgFile.c_str(), true)) {
      messageBox(Warning, _("Error"), fstring(_("Error saving preferences to \"%s\""), cfgFile.c_str()));
    }
  });
}

void ScribbleApp::resetDocPrefs()
{
  activeDoc()->resetDocPrefs();
}

// version: XXYYZZ where public version number is XX.YY.ZZ; no good reason for this to be in makefile
#define SCRIBBLE_VERSION_SERIAL 30000

void ScribbleApp::about()
{
  static const int ver = SCRIBBLE_VERSION_SERIAL;
  static const int maint = ver % 100;
  static const int minor = (ver/100) % 100;
  static const int major = (ver/10000) % 100;
  messageBox(Info, _("About Write"),
      //fstring("Write v%d.%d.%d\nBuild ID: ", major, minor, maint) + PPVALUE_TO_STRING(SCRIBBLE_REV_NUMBER)
      fstring("Write %d\nBuild ID: ", major) + PPVALUE_TO_STRING(SCRIBBLE_REV_NUMBER)
      + "; " + __DATE__ + (IS_DEBUG ? " DEBUG" : "") +
      "\nCreated by: Stylus Labs\nhttp://www.styluslabs.com"
      "\nsupport@styluslabs.com\n\nWrite is a word processor for handwriting."
#if PLATFORM_IOS
      "\nPrivacy: Write does not collect any personal data."
#else
      "\nAvailable for iOS, Android, Windows, Mac, and Linux."
#endif
  );
}

/// send/share ///

void ScribbleApp::sendFile(const std::string& body, const std::string& attachfile)
{
#if PLATFORM_ANDROID
  FSPath fileinfo(attachfile);
  std::string ext = fileinfo.extension();
  std::string mimetype;
  if(ext == "html" || ext == "htm")
    mimetype = "text/html";
  else if(ext == "svg" || ext == "svgz")
    mimetype = "image/svg+xml";
  else if(ext == "png")
    mimetype = "image/png";
  else if(ext == "pdf")
    mimetype = "application/pdf";
  AndroidHelper::sendFile(attachfile.c_str(), mimetype.c_str(), FSPath(activeDoc()->fileName()).baseName().c_str());
#elif PLATFORM_IOS
  iosSendFile(attachfile.c_str());
#elif IS_DEBUG
  PLATFORM_LOG("Sharing %s\n", attachfile.c_str());
#endif
}

void ScribbleApp::sendPageImage()
{
  Page* page = activeArea()->getCurrPage();
  Rect dirty = page->rect();
  Dim scale = gui->paintScale;
  Image img(page->width()*scale, page->height()*scale, Image::PNG);
  Painter imgpaint(Painter::PAINT_SW | Painter::SRGB_AWARE, &img);
  imgpaint.beginFrame();
  imgpaint.scale(scale);
  imgpaint.setsRGBAdjAlpha(true);
  page->draw(&imgpaint, dirty, cfg->Bool("sendRuleLines"));
  imgpaint.endFrame();

  std::string basename = activeDoc()->fileName()[0] ?
      FSPath(activeDoc()->fileName()).baseName() : std::string("untitled");
  std::string pngfile = FSPath(tempPath).childPath(basename + ".png");
  std::ofstream pngstrm(PLATFORM_STR(pngfile.c_str()), std::ios::binary);
  auto pngenc = img.encodePNG();
  pngstrm.write((char*)pngenc.data(), pngenc.size());
  pngstrm.close();
  sendFile(_("See attached image"), pngfile);
}

void ScribbleApp::sendPDF()
{
  std::string basename = activeDoc()->fileName()[0] ?
      FSPath(activeDoc()->fileName()).baseName() : std::string("untitled");
  std::string pdffile = FSPath(tempPath).childPath(basename + ".pdf");
  if(writePDF(pdffile))
    sendFile(_("See attached PDF"), pdffile);
}

void ScribbleApp::sendDocument()
{
  if(maybeSave())
    sendFile(_("See attached file"), activeDoc()->fileName());
}

/// Update check

void ScribbleApp::updateCheck(bool userreq)
{
#if ENABLE_UPDATE
  if(updateSocket != -1)  // this may happen in syncClearUndone()
    return;  //SDL_WaitThread(updateThread, NULL);
  userUpdateReq = userreq;
  std::thread updateThread(updateThreadFn, (void*)this);  //updateThread = SDL_CreateThread(updateThreadFn, "Write_updateThread", (void*)this);
  updateThread.detach();  //SDL_DetachThread(updateThread);
#endif
}

#if ENABLE_UPDATE

int ScribbleApp::updateThreadFn(void* _self)
{
  ScribbleApp* self = static_cast<ScribbleApp*>(_self);
  constexpr int RECV_BUFF_LEN = 1<<20;
  char* recvBuff = new char[RECV_BUFF_LEN];
  char* recvPtr = recvBuff;
  self->updateSocket = unet_socket(UNET_TCP, UNET_CONNECT, UNET_NOBLOCK, "www.styluslabs.com", "80");
  if(self->updateSocket != -1) {
    if(unet_select(-1, self->updateSocket, 4) == UNET_RDY_WR) {
      std::string req = fstring("GET /write/versions.xml?force=%d&sc=%d HTTP/1.1\r\nHost: www.styluslabs.com\r\n"
          "Connection: close\r\nUser-Agent: %s\r\n\r\n",
          self->userUpdateReq ? 1 : 0, cfg->Int("strokeCounter"), self->httpUserAgent.c_str());
      unet_send(self->updateSocket, req.data(), req.size());
      while(unet_select(self->updateSocket, -1, 4) == UNET_RDY_RD) {
        int n = unet_recv(self->updateSocket, recvPtr, RECV_BUFF_LEN - (recvPtr - recvBuff) - 1);
        if(n <= 0)
          break;
        recvPtr += n;
      }
    }
    unet_close(self->updateSocket);
    self->updateSocket = -1;
  }

  *recvPtr = '\0';
  SvgGui::pushUserEvent(scribbleSDLEvent, UPDATE_CHECK, (void*)recvBuff);
  return 0;
}

void ScribbleApp::updateNotify(const char* msg, int level)
{
  if(userUpdateReq)
    messageBox(level == 0 ? Info : Warning, _("Update Check"), msg);
  //userUpdateReq = false;
#ifdef SCRIBBLE_TEST
  SCRIBBLE_LOG("Update Check: %s\n", msg);
#endif
}

void ScribbleApp::updateInfoReceived(char* updateData)
{
  pugi::xml_document updateInfo;
  pugi::xml_node node;
  StringRef updateStr(updateData);
  int contentpos = updateStr.find("\r\n\r\n");
  if(!updateData[0] || contentpos < 0)
    updateNotify(_("Error connecting to update server."), 1);
  else if(!updateInfo.load(updateData + contentpos + 4)
      || !(node = updateInfo.child("rss").child("channel").child("item")))
    updateNotify(_("Error parsing update information from server."), 1);
  else {
    cfg->set("strokeCounter", 0);
    cfg->set("lastUpdateCheck", int(mSecSinceEpoch()/1000));
    // see if a new version is available
    if(node.child("sl:version").text().as_int(0) <= SCRIBBLE_VERSION_SERIAL)
      updateNotify(_("You have the latest version of Write."), 0);
    else {
      // no automatic update for now - just prompt to go to download page
      auto choice = messageBox(Question, _("Write Update"),
          _("A new version of Write is available.  Would you like to open the download page?"), {_("Yes"), _("No")});
      if(choice == _("Yes"))
        openURL("http://www.styluslabs.com/download/");
    }
  }
  delete[] updateData;

#ifdef Q_OS_WIN
    QNetworkReply* dlreply;
    // download the update ... I guess for now we'll just write out to file when everything has been received
    // if we are too old, must download full install
    pugi::xml_node enclosure = node.find_child_by_attribute("enclosure", "class", "win_update");
    if(enclosure.attribute("sl:minversion").as_int(0) > SCRIBBLE_VERSION_SERIAL)
      dlreply = qNetMgr->get(QNetworkRequest(QUrl(
          node.find_child_by_attribute("enclosure", "class", "win_full").attribute("url").as_string())));
    else
      dlreply = qNetMgr->get(QNetworkRequest(QUrl(enclosure.attribute("url").as_string())));
    connect(dlreply, SIGNAL(finished()), this, SLOT(updateDownloaded()));
#endif
}
#endif  // ENABLE_UPDATE

/// Shared whiteboard
#include "syncdialog.h"

#define MD5_IMPLEMENTATION
#include "ulib/md5.h"

struct Url {
  std::string user;
  std::string pass;
  std::string host;
  std::string path;
  std::string query;
};

// SWB url string: [user[:pass]@][server[:port]/]whiteboard_id[?query params]
// we allow SWB login in HTTP credential format, but we don't actually send as HTTP credentials (insecure)
static Url parseWhiteboard(std::string sharename)
{
  Url url;
  if(sharename.compare(0, 6, "swb://") == 0)
    sharename = sharename.substr(6);
  size_t atsym = sharename.find('@');
  if(atsym != std::string::npos) {
    size_t colon = sharename.find(':');
    if(colon != std::string::npos && colon < atsym) {
      url.pass = sharename.substr(colon + 1, atsym - colon);
      url.user = sharename.substr(0, colon);
    }
    else
      url.user = sharename.substr(0, atsym);
    sharename.substr(atsym+1).swap(sharename);
  }
  size_t slash = sharename.find('/');
  if(slash != std::string::npos) {
    url.host = sharename.substr(0, slash);
    sharename.substr(slash+1).swap(sharename);
  }
  else
    url.host = ScribbleApp::cfg->String("syncServer");
  if(url.host.empty())
    url.host = "www.styluslabs.com";
  size_t qmark = sharename.find('?');
  if(qmark != std::string::npos) {
    url.query = sharename.substr(qmark+1);
    url.path = sharename.substr(0, qmark);
  }
  else
    url.path = sharename;
  return url;
}

void ScribbleApp::showSyncInfo()
{
  SyncInfoDialog dialog(activeDoc()->scribbleSync);
  execDialog(&dialog);
}

// check menu item + get sharing URL?
void ScribbleApp::shareDocument()
{
  if(activeDoc()->scribbleSync) {
    auto choice = messageBox(Question, _("Disconnect Whiteboard"),
        _("Disconnect from current whiteboard?"), {_("OK"), _("Cancel")});
    if(choice != _("OK"))
      return;
    delete activeDoc()->scribbleSync;
    activeDoc()->scribbleSync = NULL;
  }
  if(!maybeSave())
    return;
  std::string titlein = FSPath(activeDoc()->fileName()).baseName();
  // show link to styluslabs.com/share if no server or user saved
  bool showLink = !cfg->String("syncServer")[0] && !cfg->String("syncUser", "")[0];
  SyncCreateDialog dialog(toLower(randomStr(6)).c_str(), titlein.c_str(), showLink);
  for(;;) {
    if(execDialog(&dialog) != Dialog::ACCEPTED)
      return;
    Url url = parseWhiteboard(trimStr(dialog.urlEdit->text()));
    if(url.path != urlEncode(url.path.c_str())) {
      dialog.setMessage(_("ID cannot contain special characters"));
      continue;
    }
    std::string title = trimStr(dialog.titleEdit->text());
    std::string query = "&title=" + urlEncode(title.c_str());
    if(dialog.lectureMode->isChecked())
      query += "&rxonly=1";
    if(!url.query.empty())
      query.append("&").append(url.query);
    if(!syncSignIn(url))
      return;
    if(doSharedDoc(url.host, syncAPICall(url, "/v1/createswb?name=" + url.path + query), true))
      return;
    dialog.setMessage(_("Error creating whiteboard"));  // try again
  }
}

bool ScribbleApp::openSharedDoc()
{
  if(!maybeSave())
    return false;
  bool showLink = !cfg->String("syncServer")[0] && !cfg->String("syncUser", "")[0];
  SyncOpenDialog dialog(showLink);
  for(;;) {
    if(execDialog(&dialog) != Dialog::ACCEPTED)
      return false;
    Url url = parseWhiteboard(dialog.urlEdit->text());
    if(!syncSignIn(url))
      return false;
    if(doSharedDoc(url.host, syncAPICall(url, "/v1/openswb?name=" + url.path), false))
      return true;
    dialog.setMessage(_("Error connecting to whiteboard"));  // try again
  }
}

bool ScribbleApp::openSharedDoc(std::string sharename)
{
  Url url = parseWhiteboard(sharename);
  return syncSignIn(url) && doSharedDoc(url.host, syncAPICall(url, "/v1/openswb?name=" + url.path), false);
}

bool ScribbleApp::doSharedDoc(std::string host, std::string reply, bool master)
{
  size_t contentpos = reply.find("\r\n\r\n");
  if(!StringRef(reply).startsWith("HTTP/1.1 200") || contentpos == std::string::npos)
    return false;
  contentpos += 4;
  pugi::xml_document doc;
  doc.load_buffer(reply.data() + contentpos, reply.size() - contentpos);
  pugi::xml_node swb = doc.child("swb");
  const char* name = swb.attribute("name").as_string(NULL);
  const char* token = swb.attribute("token").as_string(NULL);
  if(!name || !token)
    return false;
  win->actionSendImmed->setChecked(true);  // reflect state of ScribbleSync
  showNotify(_("Connecting..."), 0);
  activeDoc()->openSharedDoc(host.c_str(), swb, master);
  if(!master) {
    std::string title = swb.attribute("title").as_string();
    if(title.empty())
      title = name;
#if PLATFORM_IOS
    onLoadFile("");
    doSaveAs();
#else
    FSPath fileinfo = FSPath(cfg->String("currFolder", ".")).child(title + "." + fileExt);
#if PLATFORM_MOBILE
    if(fileinfo.exists())
      fileinfo = FSPath(createRecoveryName(fileinfo.c_str(), ""));
#endif
    if(fileinfo.exists() || !activeDoc()->saveDocument(fileinfo.c_str(), Document::SAVE_FORCE))
      doSaveAs();
    else
      onLoadFile(fileinfo.c_str());
#endif
  }
  return true;
}

bool ScribbleApp::syncSignIn(const Url& baseurl)
{
  char md5Temp[2*MD5_DIGEST_SIZE+1];
  bool saveuser = baseurl.user.empty() && baseurl.pass.empty() && baseurl.host == cfg->String("syncServer");
  std::string user = saveuser ? std::string(cfg->String("syncUser", "")) : baseurl.user;
  std::string pw = saveuser ? cfg->String("syncPass", "") : "";
  if(!baseurl.pass.empty())
    pw = MD5hex(("styluslabs" + baseurl.pass).c_str(), 0, md5Temp);
  std::string msg;
  syncSession.clear();
  for(;;) {
    bool savepw = false;
    if(user.empty() || pw.empty()) {  // we previously allowed empty username, so check here
      // "See styluslabs.com/faq"; //"<a href='http://www.styluslabs.com/account/'>Create or edit account</a>";
      std::string accountlink = _("Login to ") + baseurl.host;
      SyncLoginDialog dialog(user.c_str(), saveuser, msg.empty() ? accountlink.c_str() : msg.c_str());
      if(execDialog(&dialog) != Dialog::ACCEPTED)
        return false;
      // remove leading and trailing space, which can easily be added accidentally or automatically on Android
      user = trimStr(dialog.userEdit->text());
      if(saveuser)
        cfg->set("syncUser", user.c_str());
      std::string saltedpw = "styluslabs" + trimStr(dialog.passEdit->text());
      pw = MD5hex(saltedpw.c_str(), 0, md5Temp);
      savepw = saveuser && dialog.savePassword->isChecked();
    }

    // we'll let QNetworkAccessManager handle the session cookie returned by /auth
    std::string ts = fstring("%lld", mSecSinceEpoch());
    std::string sig = MD5hex((pw + ts).c_str(), 0, md5Temp);
    std::string replystr = syncAPICall(baseurl, "/v1/auth?user=" + user + "&timestamp=" + ts + "&signature=" + sig);
    StringRef reply(replystr);
    if(reply.startsWith("HTTP/1.1 200")) {
      if(savepw && saveuser)
        cfg->set("syncPass", pw.data());
      const char* setCookieStr = "Set-Cookie: session=";
      int c0 = reply.find(setCookieStr);
      if(c0 > 0) {
        c0 += strlen(setCookieStr);
        int c1 = reply.findFirstOf("\r;", c0);
        syncSession = reply.substr(c0, c1 - c0).toString();
      }
      return true;
    }
    if(reply.isEmpty())
      msg = _("Could not connect to ") + baseurl.host;
    else
      msg = StringRef(reply).startsWith("HTTP/1.1 401") ?
          std::string(_("Incorrect username or password.")) : (_("HTTP Error: ") + replystr.substr(9, 3));
    // try again
    if(saveuser)
      cfg->set("syncPass", "");
    pw.clear();
  }
}

// We want to redisplay login, create SWB, open SWB dialogs on failure because mistyped password or doc name
//  is expected to be fairly common, we have to show some kind of error info anyway and for the user to
//  manually reopen dialogs they have to renavigate to submenu of overflow menu (so 3 clicks)
// - but how to do it?
// - recursively call with retry = true? ... but we don't want to repeat save or sign in!
// - use a loop inside the fn? ... do we recreate the dialog inside the loop; if not, how do we add the
//  the error message when retrying?; if so, I suppose we need bool retry; variable

/*QNetworkReply* MainWindow::asyncAPICall(const QUrl& baseurl, const std::string& route, const QObject* obj, const char* method)
{
  if(!syncAPIMgr)
    syncAPIMgr = new QNetworkAccessManager(this);

  QUrl requrl(std::string("http://") + cfg->String("syncServer") + route);
  requrl.setPort(baseurl.port(7000));
  if(!baseurl.host().isEmpty())
    requrl.setHost(baseurl.host());
  QNetworkRequest request;
  request.setUrl(requrl);
  request.setRawHeader("User-Agent", httpUserAgent);
  QNetworkReply* reply = syncAPIMgr->get(request);
  if(obj)
    QObject::connect(reply, SIGNAL(finished()), obj, method);
  return reply;
}*/

// For sign-in and create/open SWB, I think blocking API call is actually preferrable (and easier)
std::string ScribbleApp::syncAPICall(const Url& baseurl, const std::string& route)
{
  // TODO: show "Please Wait..." message box
  constexpr int RECV_BUFF_LEN = 4096;
  char recvBuff[RECV_BUFF_LEN];
  std::string reply;
  int sock = unet_socket(UNET_TCP, UNET_CONNECT, UNET_NOBLOCK, baseurl.host.c_str(), "7000");
  if(sock != -1) {
    if(unet_select(-1, sock, 4) > 0) {
      std::string req = fstring("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser Agent: %s\r\n",
          route.c_str(), baseurl.host.c_str(), httpUserAgent.c_str());
      if(!syncSession.empty())
        req.append("Cookie: session=").append(syncSession).append("\r\n");
      req.append("\r\n");
      unet_send(sock, req.data(), req.size());
      while(unet_select(sock, -1, 4) > 0) {
        int n = unet_recv(sock, recvBuff, RECV_BUFF_LEN);
        if(n <= 0)
          break;
        reply.append(recvBuff, n);
      }
    }
    unet_shutdown(sock, UNET_SHUT_RDWR);
    unet_close(sock);
  }
  return reply;
}

// toggle sending edits
void ScribbleApp::syncSendImmed()
{
  win->actionSendImmed->setChecked(!win->actionSendImmed->isChecked());
  activeDoc()->scribbleSync->syncImmed = win->actionSendImmed->isChecked();
  if(activeDoc()->scribbleSync->syncImmed)
    activeDoc()->scribbleSync->sendHist(true);
}

// view sync button for SWB
void ScribbleApp::syncView()
{
  if(!activeDoc()->scribbleSync)
    return;
  activeDoc()->scribbleSync->syncViewBox = win->actionViewSync->isChecked() ?
      (win->actionViewSyncMaster->isChecked() ? ScribbleSync::SYNCVIEW_MASTER : ScribbleSync::SYNCVIEW_SLAVE)
      : ScribbleSync::SYNCVIEW_OFF;
}

// level: 0 <= info, 1 = warn, 2 = error; level < 0 concats to previous info message if still visible
void ScribbleApp::showNotify(const std::string& msg, int level)
{
  static const char* bg[] = {"#aaa", "#aa0", "#a00"};

  if(level < cfg->Int("syncMsgLevel")) return;
  if(!notifyBar) {
    notifyBar = createToolbar();
    notifyBar->node->removeClass("toolbar");  // otherwise CSS fill overrides ours
    notifyText = new TextBox(createTextNode(""));
    notifyText->setMargins(0, 0, 0, 16);
    Button* closeNotify = createToolbutton(SvgGui::useFile("icons/ic_menu_cancel.svg"), "");
    closeNotify->onClicked = [this](){ dismissNotify(); };

    notifyBar->addWidget(notifyText);
    notifyBar->addWidget(createStretch());
    notifyBar->addWidget(closeNotify);
    win->selectFirst("#notify-toolbar-container")->addWidget(notifyBar);
  }
  // pass level < 0 to concat to last info message; don't let message grow to more then 1/4 of window height
  if(notifyBar->isVisible() && level < 0 && notifyBar->node->bounds().height() < win->winBounds().height()/4)
    notifyText->setText((notifyText->text() + "\n" + msg).c_str());
  else
    notifyText->setText(msg.c_str());

  notifyBar->node->setAttribute("fill", bg[std::max(0, std::min(level, 2))]);
  notifyBar->setVisible(true);
  // we might want to see notifications longer when testing
#ifndef SCRIBBLE_TEST
  notifyTimer = gui->setTimer(5000, win, notifyTimer, [this]() {
    dismissNotify();
    return 0;
  });
#endif
}

void ScribbleApp::dismissNotify()
{
  if(notifyBar)
    notifyBar->setVisible(false);
}

// update check

#ifdef Q_OS_WIN
void MainWindow::updateDownloaded()
{
  QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
  if(reply->error() == QNetworkReply::NoError) {
    // assume this is a download request
    // is there any reason to let filename be variable?
    std::string updaterpath = QApplication::applicationDirPath() + "/write_update.exe";
    QFile file(updaterpath);
    if(!file.open(QIODevice::WriteOnly)) {
      updateNotify("Error saving update installer to " + updaterpath
            + ". You may need to run Write as an administrator to perform update.");
      goto cleanup;
    }
    file.write(reply->readAll());
    file.close();
    // don't bug user until next week if they cancel the update
    cfg->set("lastUpdateCheck", int(mSecSinceEpoch()/1000));
    // ask user if they want to install update
    pugi::xml_node node = updateInfo.child("rss").child("channel").child("item");
    QMessageBox msgbox(QMessageBox::Question, tr("Update Available"),
        tr("A new version of Write is available, would you like to update now?\n\n")
        + node.child_value("description"));
    QPushButton* updatebtn = msgbox.addButton(tr("Update Now"), QMessageBox::AcceptRole);
    msgbox.addButton(tr("Skip Update"), QMessageBox::RejectRole);
    msgbox.setDefaultButton(updatebtn);
    msgbox.exec();
    // quit() doesn't appear to generate call to closeEvent
    if(msgbox.clickedButton() != updatebtn || !maybeSave()) {
      QMessageBox::information(this, "Update Skipped", "To update later, select \"Check for Update\" from the "
          "Help menu.  Automatic update check can be disabled in Preferences.");
      goto cleanup;
    }
    // launch the updater and exit
    QProcess::startDetached(updaterpath, std::stringList());
    QApplication::quit();
  }
  else
    updateNotify("Error downloading update from " + reply->url().path());
cleanup:
  reply->deleteLater();
}
#endif

#ifdef SCRIBBLE_TEST
// an alternative to this is TESTSOURCES= in Makefile.common
#include "scribbletest/scribbletest.cpp"

void ScribbleApp::runTestUI(std::string runtype)
{
  messageBox(Info, "Test Results - " + runtype, runTest(runtype));
}

std::string ScribbleApp::runTest(std::string runtype)
{
  if(runtype == "test") {
    ScribbleTest test(SCRIBBLE_TEST_PATH);
    test.runAll();
    return test.resultStr;
  }
  else if(runtype == "synctest") {
    ScribbleTest test(SCRIBBLE_TEST_PATH);
    // set server for synctest
    test.scribbleConfig->set("syncServer", cfg->String("syncServer"));
    test.runAll(true);
    return test.resultStr;
  }
  else if(runtype == "perftest") {
    ScribbleTest test(activeDoc(), bookmarkArea, scribbleMode);
    test.performanceTest();
    return test.resultStr;
  }
  else if(runtype == "inputtest") {
    // see 83a76eea88eb for TouchInputFilter::notifyTouchEvent test
    ScribbleTest test(activeDoc(), bookmarkArea, scribbleMode);
    test.inputTest();
    return test.resultStr;
  }
  return runtype + " is not a valid test mode.";
}

#endif
