#include <stdio.h>
#include <stdlib.h>
#include "scribbleconfig.h"
#include "document.h"


ScribbleConfig::ScribbleConfig() : upconfig(NULL)
{
  init();
}

void ScribbleConfig::init()
{
  // set defaults
  //cfg["limitToolToSel"] = 1;
  // erase strokes covered by negative ruled insert space
  cfg["insSpaceErase"] = 1;
  // touch vs. cover for ruled eraser
  cfg["greedyRuledErase"] = 1;
  // apply erasers to images?
  cfg["eraseOnImage"] = 1;
  // 1: return to previous mode on cursor up (except from draw mode) 0: do not return to previous mode
  cfg["doubleTapSticky"] = 1;
  // show floating toolbar when selection is made
  cfg["popupToolbar"] = 1;
  // automatically enlarge page if strokes added near or beyond edge?
  cfg["growDown"] = 1;
  cfg["growRight"] = 0;
  cfg["growWithPen"] = 1;
  // reflow w/ insert space tool ... always does live reflow now
  cfg["reflow"] = 1;
  // if selection is cleared on cursor down, don't do anything until cursor up
  cfg["clearSelOnly"] = 1;
  // group strokes (by setting com.y of all to same value) based on time, proximity
  cfg["groupStrokes"] = 1;
  // ruled mode setting needs to be retained!
  cfg["ruledMode"] = 1;
  // pen width adjusted when zoomed so that it appears the same width to the user
  //cfg["scalePenWithZoom"] = 0;
  // ruled selector column detect mode: 0 = none, 1 = normal, 2 = aggressive (full page)
  cfg["columnDetectMode"] = 1;
  // save pictures as smaller size if scaled down
  cfg["savePicScaled"] = 1;
  // snap bookmarks near left margin
  cfg["snapBookmarks"] = 1;
  // apply pen color and width to selection when pen changed (previous behavior, got complaints when removed)
  cfg["applyPenToSel"] = 0;

  // input modes: 0 = ignore, 1 = pan, 2 = draw or zoom (multitouch)
  cfg["mouseMode"] = 2;
  cfg["singleTouchMode"] = 2;  // touch will be disabled if pen input is detected
  cfg["multiTouchMode"] = 2;
  // detected pen type (0 = no pen. -1 = detect on startup)
  cfg["penType"] = -1;
  // alternative to singleTouchMode,multiTouchMode - only used for android at the moment
  //cfg["touchInputMode"] = 0;
  // Pen button setup: see codes in scribblemode.h (17 = MODE_SELECT)
  cfg["penButtonMode"] = 17;
  // smoothing: tau = 0.5*inputSmoothing/mZoom for single pole IIR (simple exponential filter)
  // (was (n + 1)/2, where n is window size of Savitzky-Golay filter used for stroke smoothing); 0 disables
  // 0.5/mZoom is sufficient for iPad, 1.0/mZoom for Galaxy Note 10
  cfg["inputSmoothing"] = PLATFORM_IOS ? 1 : (PLATFORM_ANDROID ? 2 : (PLATFORM_EMSCRIPTEN ? 6 : 0));
  // RDP line simplification threshold in 0.05 pixel steps
  cfg["inputSimplify"] = 0;
  // volume button setup (Android only?) - see volUp/DownActions in mainwindow.cpp
  cfg["volButtonMode"] = 0;  //PLATFORM_MOBILE ? 3 : 0;  // 3 = Next Page/Prev Page
  // hidden option to disable back button to address accidental palm presses
  cfg["backKeyEnabled"] = 1;
  cfg["keepScreenOn"] = 0;

  // 1 = use Wintab instead of WM_POINTER if available; can we be sure WM_POINTER works for all USB tablets?
  cfg["useWintab"] = 1;

  // 1 = swipe in from edge of screen to pan - not very useful on windows since win 8 uses swipes
  // 2 = pen down off page pans (undocumented)
  cfg["panFromEdge"] = PLATFORM_MOBILE;
  // position of pen press event is bad on some platforms (e.g. my X1 Yoga)
  cfg["dropFirstPenPoint"] = 0;

  // autoscroll (scroll if pen nears edge of screen when making or moving selection)
  cfg["autoScrollSelect"] = 1;
  // continuous zoom, or only discrete steps?
  cfg["continuousZoom"] = 0;
  // number of undo/redo steps per rotation around undo/redo circle
  cfg["undoCircleSteps"] = 36;
  // timer period in milliseconds
  cfg["timerPeriod"] = 33;

  // view mode: 0 = single page, 1 = continuous vertical scrolling, 2 = cont. horz.
  cfg["viewMode"] = 1;
  // 1 = center pages narrower than max width (or shorter than max height for horz scroll); 0 = left/top align
  cfg["centerPages"] = 1;
  // multiple document views
  cfg["splitLayout"] = -1;
  // hide BookmarkView, ClippingView after use?
  cfg["autoHideBookmarks"] = PLATFORM_MOBILE;
  cfg["autoHideClippings"] = 0;
  // bookmark view mode: any margin content considered bookmark if 1
  cfg["bookmarkMode"] = 0;
  // after delay, completely hide scroller (1) or just dim (0)?
  cfg["autoHideScroller"] = 0;
  // how to save pens: 0 = manual, 1 = recent (global), 2 = recent (per document)
  cfg["savePenMode"] = 0;
  // show pen toolbar (several users have requested this be persisted)
  cfg["showPenToolbar"] = 0;
  // 0 = hide cursor in draw mode, 1 = system cursors, 2 = custom drawn cursor in draw and erase modes
  cfg["drawCursor"] = PLATFORM_MOBILE ? 2 : 1;

  // compression level
  cfg["compressLevel"] = 2;
  // save documents as a single file instead of one SVG file per page ... deprecated, not exposed in UI
  cfg["singleFile"] = 1;
  // save unmodified files (to update thumbnail and viewing position)?
  cfg["saveUnmodified"] = 0;
  // prompt before save
  cfg["savePrompt"] = 0;
  // autosave interval in seconds; set <= 0 to disable
  cfg["autoSaveInterval"] = 0; //120;
  // Use custom document list dialog to open and create documents
  cfg["useDocList"] = 1;
  // doc list icon (thumbnail) size
  cfg["thumbnailSize"] = 140;
  // doc list sort order; 0 = name; 1 = last modified time
  cfg["docListSort"] = 0;
  // disallow browsing out of base directory
  cfg["docListSiloed"] = PLATFORM_IOS;

  // save thumbnail to HTML file - currently only disabled when running tests
  cfg["saveThumbnail"] = 1;
  // always use first page for thumbnail (so it serves as a title page)
  cfg["thumbFirstPage"] = 0;
  // include rule lines when sending page
  cfg["sendRuleLines"] = 0;
  // only load page when we first need to draw it (or draw bookmarks)
  cfg["delayLoad"] = 1;
  // for Qt, reopen last open doc
  cfg["reopenLastDoc"] = 0;
  // warn if file appears to be modified outside of Write
  cfg["warnExtModified"] = 1;
  // 1 = read system clipboard when focused, 0 = don't read until paste attempted
  cfg["preloadClipboard"] = PLATFORM_DESKTOP;
  // display time range - want to disable by default for touch UI
  cfg["displayTimeRange"] = 0;

  // UI theme: 1 = dark, 2 = light
  cfg["uiTheme"] = 1;
  // screen DPI override - 0 uses autodetected value
  cfg["screenDPI"] = 0;
  // use sRGB color space properly if set - i.e. perform blending in linear space and convert back to sRGB
  cfg["sRGB"] = 1;
  // nanovg-2 flags for debugging - 1<<4 == 16 == NVG_NO_FB_FETCH; 1<<5 == 32 == NVG_NO_IMG_ATOMIC
  cfg["nvgGlFlags"] = 0;
  cfg["glRender"] = 1;  // we'll try GL on first run

  // advanced vs. basic prefs in Preferences dialog
  cfg["showAdvPrefs"] = 0;
  // prompt to convert old documents
  cfg["askConvertDocs"] = 1;

  // color inversion
  cfg["invertColors"] = 0;
  cfg["colorXorMask"] = 0x00FFFFFF;

  // page color
  cfg["pageColor"] = Color(Color::WHITE).argb();
  cfg["ruleColor"] = Color(0, 0, 0xFF, 0x9F).argb();
  // bookmark color and link color (default is blue)
  cfg["bookmarkColor"] = Color(Color::BLUE).argb();
  cfg["linkColor"] = Color(Color::BLUE).argb();

  // update check
  cfg["updateCheck"] = PLATFORM_MOBILE ? 0 : 1;
  cfg["lastUpdateCheck"] = 0;
  cfg["updateCheckInterval"] = 60*60*24*7;  // 1 week in seconds
  cfg["strokeCounter"] = 0;
  // review prompt
  cfg["lastReviewPrompt"] = 0;
  // show translations keys in place of strings in UI
  cfg["showTranslationKeys"] = 0;
  // sync
  cfg["syncCompress"] = 3;  // compression level for SWB data
  cfg["syncViewPageOffset"] = 0;
  cfg["syncMsgLevel"] = -100;  // only show messages w/ level >= this value
  cfg["perfTrace"] = 0;  // print performance traces?
  cfg["maxMemoryMB"] = 1024;  // start unloading pages when memory usage hits 1GB

  // floats
  // page defaults - initial values are determined from screen size on first run
  cfgF["pageWidth"] = 0.0f;
  cfgF["pageHeight"] = 0.0f;
  // ruling parameters ... back from being deprecated in favor of custom rulings
  cfgF["xRuling"] = 0.0f;
  cfgF["yRuling"] = 40.0f;
  cfgF["marginLeft"] = 100.0f;

  // ruling to be used by ruled selector on blank pages
  cfgF["blankYRuling"] = 60;
  // for continuous view
  cfgF["pageSpacing"] = 20;
  // allow slight overzoom for visual indication of zoom limits
  cfgF["MIN_ZOOM"] = 0.09f;
  cfgF["MAX_ZOOM"] = 11.0f;
  // a Wacom tablet with touch will likely be smaller than screen so, when scaled to screen coords, touch
  //  points will be relatively far apart and we need to give user the option to increase touchMinZoomPtrDist
  cfgF["touchMinZoomPtrDist"] = 150.0f;
  cfgF["touchMinPtrDist"] = 40.0f;
  // scale factor to account for screen dpi - now only used internally
  cfgF["preScale"] = 1.0f;
  // size of border around content, as fraction of window size
  cfgF["BORDER_SIZE"] = 0.2f;
  // reduced horz border (set to 0 to just use BORDER_SIZE)
  cfgF["horzBorder"] = 10;
  // autoscroll when selection dragged near edge of screen
  cfgF["autoScrollSpeed"] = 0.4f;
  // mouse wheel scroll speed
  cfgF["wheelScrollSpeed"] = 0.2f;
  // Ctrl+mouse wheel zoom speed
  cfgF["wheelZoomSpeed"] = 1.0f;
  // minimum word break size, as fraction of Y ruling
  cfgF["minWordSep"] = 0.3f;  // = 12/40
  // border size for pan from edge
  cfgF["panBorder"] = 10;
  // palm rejection threshold (platform reported major axis of touch in Dim units); 140 works well for recent iPad
  cfgF["palmThreshold"] = PLATFORM_IOS ? 140.0f : 0.0f;  // not tested on Android

  // strings
  // previously called "fileExt"; strings are always written to config file, so we can't change default
  cfgS["docFileExt"] = "svgz";
  //cfgS["mainWindowGeometry"] = "";
  cfgS["windowState"] = "";
  cfgS["recentDocs"] = "";
  cfgS["currFolder"] = "";
  cfgS["toolModes"] = "";
  cfgS["clippingDoc"] = "";
  // for pen toobar
  cfgS["savedColors"] = "black,red,green,blue";
  cfgS["savedWidths"] = "1.4, 2.4, 4.0, 8.0";
#if PLATFORM_IOS
  cfgS["iosBookmark0"] = "";  // security-scoped bookmark for most recent doc (base64 encoded)
#endif

  // default toolbar config
  cfgS["toolBars"] = "";  // preserve users' old config for now
#if PLATFORM_ANDROID || PLATFORM_IOS  // we want save button for emscripten (which is PLATFORM_MOBILE)
  // this is really the small screen setup - iPad, e.g., should be same as desktop except save btn
  //cfgS["toolBars"] = "docTitle,stretch,tools,undoRedoBtn,"
  //    "actionSelection_Menu,actionShow_Bookmarks,actionShow_Clippings,actionSplitView,actionOverflow_Menu";
  cfgS["toolBars2"] = "docTitle,stretch,tools,separator,undoRedoBtn,separator,seltools,"
    "separator,actionShow_Bookmarks,actionShow_Clippings,actionSplitView,separator,actionOverflow_Menu";
#else
  // appropriate for Surface Pro portrait display
  cfgS["toolBars2"] = "docTitle,stretch,actionSave,separator,tools,separator,undoRedoBtn,separator,seltools,"
    "separator,actionShow_Bookmarks,actionShow_Clippings,actionSplitView,separator,actionOverflow_Menu";
#endif
  // strftime format string for doc title
  cfgS["newDocTitleFmt"] = "%b %e %Hh%M";
  // fallback fonts (user specified for now; maybe we try to figure out from locale in the future)
  cfgS["userFonts"] = "";
  // translations
  cfgS["translations"] = "";
  cfgS["revTranslations"] = "";
  cfgS["locale"] = "";
  cfgS["guiSVG"] = "";
  cfgS["guiCSS"] = "";

  // document specific config
  cfg["docFormatVersion"] = Document::docFormatVersion;
  cfg["pageNum"] = 0;
  cfgF["xOffset"] = -10.0f;
  cfgF["yOffset"] = -10.0f;
  cfgS["docTitle"] = "";
  cfgS["docTags"] = "";
  //cfgS["backupFilename"] = "";

  // sync
  cfgS["syncServer"] = IS_DEBUG ? "localhost" : ""; // www.styluslabs.com
  cfgS["syncUser"] = "";
  cfgS["syncPass"] = "";
}

bool ScribbleConfig::loadConfigString(const char* cfgstr)
{
  pugi::xml_document doc;
  if(doc.load(cfgstr))
    return loadConfig(doc.child("map"));
  else
    return false;
}

bool ScribbleConfig::loadConfigFile(const char* cfgfile)
{
  pugi::xml_document doc;
  if(doc.load_file(PLATFORM_STR(cfgfile)))
    return loadConfig(doc.child("map"));
  else
    return false;
}

bool ScribbleConfig::loadConfig(const pugi::xml_node& cfgroot)
{
  for(pugi::xml_node pref = cfgroot.first_child(); pref; pref = pref.next_sibling()) {
    if(strcmp(pref.name(), "pen") == 0) {
      pens.push_back(ScribblePen(pref.attribute("color").as_uint(), pref.attribute("width").as_float(),
          pref.attribute("flags").as_uint(), pref.attribute("wRatio").as_float(1.0),
          pref.attribute("pressure").as_float(), pref.attribute("speed").as_float(),
          pref.attribute("direction").as_float(), pref.attribute("dash").as_float(),
          pref.attribute("gap").as_float()));
      if(std::isnan(pref.attribute("wRatio").as_float(NAN))) {  // legacy pen
        auto& pen = pens.back();
        if(pen.prParam > 0)
          pen.setFlag(ScribblePen::WIDTH_PR, 1);
        if(pen.hasFlag(ScribblePen::DRAW_UNDER))
          pen.setFlag(ScribblePen::TIP_CHISEL, 1);
        else if(pen.width > 4)
          pen.setFlag(ScribblePen::TIP_ROUND, 1);
        else
          pen.setFlag(ScribblePen::TIP_FLAT, 1);
      }
    }
    else if(strcmp(pref.name(), "ruling") == 0) {} // old ruling ... ignore
    else {
      const char* name = pref.attribute("name").value();
      const char* val = pref.attribute("value").value();
      // allow value to be given as node contents for legacy support
      if(strlen(val) == 0)
        val = pref.child_value();
      setConfigValue(name, val);
    }
  }
  //migrateConfig();
  return true;
}

bool ScribbleConfig::setConfigValue(const char* name, const char* val)
{
  // we must not use the passed name pointer to create a new config value, since the string contents could be
  //  changed - this was happening with strings passed from JNI
  const char* key = NULL;
  if((key = isInt(name))) {
    // if atoi can't parse string, it returns 0, so "false" gets handled correctly
    if(val[0] == 't' || val[0] == 'T')
      cfg[key] = 1;
    else
      cfg[key] = atoi(val);
  }
  else if((key = isFloat(name)))
    cfgF[key] = atof(val);
  else if((key = isString(name)))
    cfgS[key] = val;
  else
    return false;
  return true;
}

// serialization format was initially based on android shared_prefs xml file
void ScribbleConfig::saveConfig(pugi::xml_node cfgroot, bool skipdefaults)
{
  pugi::xml_node node;
  ScribbleConfig* df = skipdefaults ? new ScribbleConfig() : NULL;
  for(cfgIterator ii = cfg.begin(); ii != cfg.end(); ii++) {
    if(!df || df->Int(ii->first) != ii->second) {
      node = cfgroot.append_child("int");
      node.append_attribute("name").set_value(ii->first);
      node.append_attribute("value").set_value(ii->second);
    }
  }
  for(cfgFIterator ii = cfgF.begin(); ii != cfgF.end(); ii++) {
    if(!df || df->Float(ii->first) != ii->second) {
      node = cfgroot.append_child("float");
      node.append_attribute("name").set_value(ii->first);
      node.append_attribute("value").set_value(ii->second);
    }
  }
  for(cfgSIterator ii = cfgS.begin(); ii != cfgS.end(); ii++) {
    // we always write string unless both value and default are empty string
    if(!df || df->String(ii->first, "")[0] || ii->second.c_str()[0]) {
      node = cfgroot.append_child("string");
      node.append_attribute("name").set_value(ii->first);
      node.append_attribute("value").set_value(ii->second.c_str());
    }
  }
  // pens
  for(penIterator ii = pens.begin(); ii != pens.end(); ii++) {
    node = cfgroot.append_child("pen");
    node.append_attribute("color").set_value(ii->color.color);
    node.append_attribute("width").set_value(float(ii->width));  // float reduces number of excess digits
    node.append_attribute("wRatio").set_value(float(ii->wRatio));
    node.append_attribute("pressure").set_value(float(ii->prParam));
    node.append_attribute("speed").set_value(float(ii->spdMax));
    node.append_attribute("direction").set_value(float(ii->dirAngle));
    node.append_attribute("dash").set_value(float(ii->dash));
    node.append_attribute("gap").set_value(float(ii->gap));
    node.append_attribute("flags").set_value(ii->flags);
  }
  if(df)
    delete df;
}

bool ScribbleConfig::saveConfigFile(const char* cfgfile, bool skipdefaults)
{
  pugi::xml_document doc;
  saveConfig(doc.append_child("map"), skipdefaults);
  return doc.save_file(PLATFORM_STR(cfgfile), "  ");
}

// consider moving pen stuff into child class ... maybe BasicConfig -> ScribbleConfig
ScribblePen* ScribbleConfig::getPen(int num)
{
  if((unsigned int)num < pens.size()) {
    penIterator it = pens.begin();
    std::advance(it, num);
    return &(*it);
  }
  else if(upconfig)
    return upconfig->getPen(num);
  else
    return NULL;
}

void ScribbleConfig::savePen(const ScribblePen& pen, int slot)
{
  if(slot < 0) {
    if(pens.front() == pen) return;
    // remove any copies of pen from list
    pens.remove(pen);
    pens.push_front(pen);
    if(pens.size() > 8)  // remove at most 1 pen (in case user edited config file to give more pens)
      pens.pop_back();
  }
  else if(slot < int(pens.size())) {
    penIterator it = pens.begin();
    std::advance(it, slot);
    *it = pen;
  }
  else
    pens.push_back(pen);
}

// right now, these are only used when loading a config string, but I can imagine other uses
const char* ScribbleConfig::isInt(const char* s) const
{
  cfgIterator it = cfg.find(s);
  if(it != cfg.end())
    return it->first;
  else if(upconfig)
    return upconfig->isInt(s);
  else
    return NULL;
}

const char* ScribbleConfig::isFloat(const char* s) const
{
  cfgFIterator it = cfgF.find(s);
  if(it != cfgF.end())
    return it->first;
  else if(upconfig)
    return upconfig->isFloat(s);
  else
    return NULL;
}

const char* ScribbleConfig::isString(const char* s) const
{
  cfgSIterator it = cfgS.find(s);
  if(it != cfgS.end())
    return it->first;
  else if(upconfig)
    return upconfig->isString(s);
  else
    return NULL;
}

int ScribbleConfig::Int(const char* s, int defaultval) const
{
  cfgIterator it = cfg.find(s);
  if(it != cfg.end())
    return it->second;
  else if(upconfig)
    return upconfig->Int(s, defaultval);
  else {
#if IS_DEBUG
    SCRIBBLE_LOG("Config value %s (Int/Bool) not present!\n", s);
#endif
    return defaultval;
  }
}

bool ScribbleConfig::Bool(const char* s, bool defaultval) const
{
  return Int(s, defaultval ? 1 : 0) != 0;
}

float ScribbleConfig::Float(const char* s, float defaultval) const
{
  cfgFIterator it = cfgF.find(s);
  if(it != cfgF.end())
    return it->second;
  else if(upconfig)
    return upconfig->Float(s, defaultval);
  else {
#if IS_DEBUG
    SCRIBBLE_LOG("Config value %s (Float) not present!\n", s);
#endif
    return defaultval;
  }
}

const char* ScribbleConfig::String(const char* s, const char* defaultval) const
{
  cfgSIterator it = cfgS.find(s);
  if(it != cfgS.end())
    return it->second.c_str();
  else if(upconfig)
    return upconfig->String(s, defaultval);
  else {
#if IS_DEBUG
    SCRIBBLE_LOG("Config value %s (String) not present!\n", s);
#endif
    return defaultval;
  }
}

int ScribbleConfig::removeInt(const char* s) { return cfg.erase(s); }
int ScribbleConfig::removeFloat(const char* s) { return cfgF.erase(s); }
int ScribbleConfig::removeString(const char* s) { return cfgS.erase(s); }

void ScribbleConfig::set(const char* s, bool x)
{
  cfg[s] = x ? 1 : 0;
}

void ScribbleConfig::set(const char* s, int x)
{
  cfg[s] = x;
}

void ScribbleConfig::set(const char* s, float x)
{
  cfgF[s] = x;
}

void ScribbleConfig::set(const char* s, double x)
{
  cfgF[s] = (float)x;
}

void ScribbleConfig::set(const char* s, const char* x)
{
  cfgS[s] = x;
}
