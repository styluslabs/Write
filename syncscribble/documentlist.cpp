#include <time.h>
#include "documentlist.h"

#include "scribbleapp.h"
#include "scribbledoc.h"
#include "touchwidgets.h"

static const char* docListWindowSVG = R"#(
<svg class="window" layout="box">
  <g id="main-layout" box-anchor="fill" layout="flex" flex-direction="column">
    <rect id="ios-statusbar-bg" class="toolbar" display="none" box-anchor="hfill" width="20" height="20"/>
  </g>
</svg>
)#";

DocumentList::DocumentList(const char* root, const char* temp) : Window(createWindowNode(docListWindowSVG))
{
  trashPath = temp;
  docFileExt = ScribbleApp::cfg->String("docFileExt");
  iconWidth = ScribbleApp::cfg->Int("thumbnailSize", 140) * ScribbleApp::getPreScale();

  createUI();
  setTitle(_("Documents"));

  docRoot = root;
#if PLATFORM_ANDROID
  // docRoot can only be /sdcard/styluslabs/write or /sdcard/Android/data/...
  docListSiloed = !ScribbleApp::hasAndroidPermission() || ScribbleApp::cfg->Bool("docListSiloed");  // !FSPath(docRoot).parent().parent().exists()
#endif
  // set initial dir - maybe we should do this in MainWindow?
  currDir = ScribbleApp::cfg->String("currFolder");
  if(!currDir.isAbsolute())
    currDir = canonicalPath(currDir);
  if(!currDir.isDir() || !currDir.exists())
    currDir = docRoot;
}

void DocumentList::createUI()
{
  // context menu
  contextMenu = createMenu(Menu::FLOATING, false);
  contextMenu->addItem(_("Cut"), [this](){ cutItem(); });
  contextMenuCopy = contextMenu->addItem(_("Copy"), [this](){ copyItem(); });
  contextMenuOpenCopy = contextMenu->addItem(_("Open Copy"), [this](){ openCopyItem(); });
  contextMenu->addItem(_("Delete"), [this](){ deleteItem(); });
  contextMenu->addItem(_("Rename"), [this](){ renameItem(); });

  // toolbar
  breadCrumbs.push_back(createToolbutton(SvgGui::useFile("icons/ic_folder.svg"), ""));
  breadCrumbs.push_back(createToolbutton(SvgGui::useFile("icons/ic_folder.svg"), ""));
  breadCrumbs[0]->setShowTitle(true);
  breadCrumbs[1]->setShowTitle(true);

  Menu* convertCtxMenu = createMenu(Menu::FLOATING, false);
  convertCtxMenu->addItem(_("Convert Documents"), [this](){ convertDocuments(currDir); });
  SvgGui::setupRightClick(breadCrumbs[0], [convertCtxMenu](SvgGui* gui, Widget* w, Point p){
    gui->showContextMenu(convertCtxMenu, p, w);
  });

  newDocBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_add_doc.svg"), _("New Document"), true);
  newDocBtn->onClicked = [this](){ newDoc(); };
  newFolderBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_add_folder.svg"), _("New Folder"));
  newFolderBtn->onClicked = [this](){ newFolder(); };
  helpBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_help.svg"), _("Help"));
  helpBtn->onClicked = [this](){ finish(OPEN_HELP); };

  saveHereBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_save.svg"), _("Save Here"), true);
  saveHereBtn->onClicked = [this](){ newDoc(); };
  cancelChoose = createToolbutton(SvgGui::useFile("icons/ic_menu_cancel.svg"), _("Cancel"));
  cancelChoose->onClicked = [this](){ finish(REJECTED); };
  whiteboardBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_people.svg"), _("Open Whiteboard"));
  whiteboardBtn->onClicked = [this](){ selectedFile = currDir.c_str(); finish(OPEN_WHITEBOARD); };

  Widget* stretch = createStretch();

  Toolbar* mainToolbar = createToolbar();
#if PLATFORM_WIN || PLATFORM_ANDROID
  Menu* driveMenu = createMenu(Menu::VERT_RIGHT, false);  // no icons
  drivesBtn = createToolbutton(SvgGui::useFile("icons/ic_drive.svg"), _("Drives"));
#if PLATFORM_WIN
  drivesBtn->setShowTitle(true);
  auto driveNames = lsDrives();
  for(std::string& drive : driveNames) {
    std::string letter = drive.substr(0, 2);
    std::string title = drive.size() > 2 ? drive.substr(3) + " (" + letter + ")" : letter;
    Button* item = createMenuItem(title.c_str());
    item->onClicked = [this, letter]() { setCurrDir((letter + "/").c_str()); };
    driveMenu->addItem(item);
  }
#elif PLATFORM_ANDROID
  drivesBtn->setShowTitle(false);
  sharedDirBtn = createMenuItem("styluslabs/write");
  sharedDirBtn->onClicked = [this]() {
    if(!ScribbleApp::hasAndroidPermission()) {
      ScribbleApp::cfg->set("currFolder", "/sdcard/styluslabs/write/");  // so we open after restarting
      if(!ScribbleApp::requestAndroidPermission())
        ScribbleApp::cfg->set("currFolder", currDir.c_str());  // user canceled!
    }
    else if(createPath("/sdcard/styluslabs/write/"))
      setCurrDir("/sdcard/styluslabs/write/");
  };
  driveMenu->addItem(sharedDirBtn);
  privateDirBtn = createMenuItem("Android/data");
  privateDirBtn->onClicked = [this]() {
    const char* appstorage = SDL_AndroidGetExternalStoragePath();
    setCurrDir(appstorage ? appstorage : "/sdcard/Android/data/com.styluslabs.writeqt/files");
  };
  driveMenu->addItem(privateDirBtn);
#endif
  drivesBtn->setMenu(driveMenu);
  drivesBtn->setVisible(false);
  mainToolbar->addWidget(drivesBtn);
#endif
  mainToolbar->addWidget(breadCrumbs[1]);
  mainToolbar->addWidget(breadCrumbs[0]);
  mainToolbar->addWidget(stretch);
  mainToolbar->addWidget(saveHereBtn);
  mainToolbar->addWidget(newDocBtn);
  mainToolbar->addWidget(newFolderBtn);
  if(IS_DEBUG || !PLATFORM_IOS || ScribbleApp::cfg->String("syncServer", "")[0])
    mainToolbar->addWidget(whiteboardBtn);
  mainToolbar->addWidget(helpBtn);
  mainToolbar->addWidget(cancelChoose);

  AutoAdjContainer* adjtb = new AutoAdjContainer(new SvgG(), mainToolbar);
  adjtb->node->setAttribute("box-anchor", "hfill");

  adjtb->adjFn = [this, stretch, adjtb](const Rect& src, const Rect& dest) {
    if(dest.width() <= src.width() + 0.5 && stretch->node->bounds().width() >= 1)
      return;
    // reset
    newDocBtn->setShowTitle(true);
    for(auto& bc : breadCrumbs)
      bc->setShowTitle(true);
    adjtb->repeatLayout(dest);
    int bcidx = breadCrumbs.size();
    while(stretch->node->bounds().width() < 1) {
      if(newDocBtn->selectFirst(".title")->isVisible())
        newDocBtn->setShowTitle(false);
      else if(--bcidx >= 0)
        breadCrumbs[bcidx]->setShowTitle(false);
      else
        break;
      adjtb->repeatLayout(dest);
    }
  };

  // touchbar which only appears when doc is on clipboard
  pasteBar = createToolbar();
  pasteBar->setVisible(false);
  pasteBar->node->addClass("graybar");

  pasteButton = createToolbutton(SvgGui::useFile("icons/ic_menu_paste.svg"), "", true);
  pasteButton->onClicked = [this](){ pasteItem(); };
  pasteButton->setEnabled(false);

  Button* cancelPaste = createToolbutton(SvgGui::useFile("icons/ic_menu_cancel.svg"), "");
  cancelPaste->onClicked = [this](){ clearClipboard(); };

  pasteBar->addWidget(pasteButton);
  pasteBar->addWidget(createStretch());
  pasteBar->addWidget(cancelPaste);

  // undo delete touchbar
  undoBar = createToolbar();
  undoBar->setVisible(false);
  undoBar->node->addClass("graybar");

  undoButton = createToolbutton(SvgGui::useFile("icons/ic_menu_undo.svg"), "", true);
  undoButton->onClicked = [this](){ undoDelete(); };

  Button* cancelUndo = createToolbutton(SvgGui::useFile("icons/ic_menu_cancel.svg"), "");
  cancelUndo->onClicked = [this](){ hideUndo(); };

  undoBar->addWidget(undoButton);
  undoBar->addWidget(createStretch());
  undoBar->addWidget(cancelUndo);

  // bar for error messages
  msgBar = createToolbar();
  msgBar->setVisible(false);
  msgBar->node->addClass("graybar");
  msgBar->addWidget(new Widget(createTextNode("")));

  // mini-toolbar for changing icon size
  Toolbar* zoomBar = createVertToolbar();
  zoomBar->node->addClass("statusbar");
  zoomBar->node->setAttribute("box-anchor", "bottom right");
  zoomBar->setMargins(0, 6, 6, 0);
  zoomBar->node->setTransform(Transform2D().scale(0.5));

  Button* cbSortName = createCheckBoxMenuItem(_("Name"), "#radiobutton");
  Button* cbSortDate = createCheckBoxMenuItem(_("Last Modified"), "#radiobutton");
  auto sortUpdate = [=](){
    int sortby = ScribbleApp::cfg->Int("docListSort");
    cbSortName->setChecked(sortby == 0);
    cbSortDate->setChecked(sortby == 1);
  };
  sortUpdate();  // set initial button state
  cbSortName->onClicked = [=](){ ScribbleApp::cfg->set("docListSort", 0); sortUpdate(); refresh(); };
  cbSortDate->onClicked = [=](){ ScribbleApp::cfg->set("docListSort", 1); sortUpdate(); refresh(); };
  Menu* sortMenu = createMenu(Menu::HORZ_LEFT);
  sortMenu->addItem(cbSortName);
  sortMenu->addItem(cbSortDate);
  Button* sortBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_overflow.svg"), _("Sort"));
  sortBtn->setMenu(sortMenu);
  sortMenu->node->setTransform(Transform2D().scale(2.0));
  sortMenu->node->setAttr<float>("font-size", 14);

  zoomIn = createToolbutton(SvgGui::useFile("icons/ic_menu_plus.svg"), "");
  zoomOut = createToolbutton(SvgGui::useFile("icons/ic_menu_minus.svg"), "");
  zoomIn->onClicked = [this](){ zoomListView(1); };
  zoomOut->onClicked = [this](){ zoomListView(-1); };
  zoomIn->setEnabled(iconWidth < 400);
  zoomOut->setEnabled(iconWidth > 60);
  zoomBar->addWidget(sortBtn);
  zoomBar->addSeparator();
  zoomBar->addWidget(zoomIn);
  zoomBar->addWidget(zoomOut);

  listView = new Widget(new SvgG());
  // Needs to be shorter; what about SvgNodeLayout::setLayout(int lay_contain, int lay_behave)?
  listView->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
  listView->node->setAttribute("layout", "flex");
  listView->node->setAttribute("flex-direction", "row");
  listView->node->setAttribute("flex-wrap", "wrap");
  listView->node->setAttribute("justify-content", "flex-start");

  scrollWidget = new ScrollWidget(new SvgDocument(), listView);
  scrollWidget->node->setAttribute("box-anchor", "fill");

  Widget* scrollContainer = new Widget(new SvgG());
  scrollContainer->node->addClass("list");  // solely for background rect color
  scrollContainer->node->setAttribute("box-anchor", "fill");
  scrollContainer->node->setAttribute("layout", "box");
  scrollContainer->addWidget(createFillRect());
  scrollContainer->addWidget(scrollWidget);

  Widget* mainLayout = selectFirst("#main-layout");
  mainLayout->addWidget(adjtb);  //mainToolbar);
  mainLayout->addWidget(createHRule());
  mainLayout->addWidget(pasteBar);
  mainLayout->addWidget(undoBar);
  mainLayout->addWidget(msgBar);
  mainLayout->addWidget(scrollContainer);

  addWidget(zoomBar);
  // add context menus
  addWidget(contextMenu);
  addWidget(convertCtxMenu);

  // setup modal behavior
  addHandler([this](SvgGui*, SDL_Event* event){
    if(event->type == SvgGui::OUTSIDE_MODAL)
      return true;  // swallow all events outside modal
    else if(event->type == SvgGui::SCREEN_RESIZED) {
      Rect newsize = static_cast<Rect*>(event->user.data1)->toSize();
      setWinBounds(PLATFORM_MOBILE ? newsize : newsize.pad(-40));
      return true;
    }
    else if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_ESCAPE)
        finish(REJECTED);
      else if((event->key.keysym.sym == SDLK_MINUS || event->key.keysym.sym == SDLK_EQUALS)
          && (event->key.keysym.mod & KMOD_CTRL)) {
        zoomListView(event->key.keysym.sym == SDLK_MINUS ? -1 : 1);
      }
#if PLATFORM_ANDROID
      else if(event->key.keysym.sym == SDLK_AC_BACK)
        AndroidHelper::moveTaskToBack();
#endif
      else
        return false;
      return true;
    }
    return false;
  });

  // prepare list item prototype
  static const char* listItemProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5"></g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="mtime-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
          <text class="fsize-text weak" box-anchor="left bottom" margin="0 160" font-size="12"></text>
        </g>
      </g>
    </g>
  )";
  listItemProto.reset(loadSVGFragment(listItemProtoSVG));

  static const char* gridItemProtoSVG = R"(
    <g class="listitem" margin="20 20" layout="box">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="column">
        <g class="image-container"></g>
        <text class="title-text" margin="10 0"></text>
      </g>
    </g>
  )";
  gridItemProto.reset(loadSVGFragment(gridItemProtoSVG));

  // prepare folder and generic file icons
  fileUseNode.reset(new SvgUse(Rect::wh(100, 100), "", SvgGui::useFile("icons/ic_file.svg")));
  folderUseNode.reset(new SvgUse(Rect::wh(100, 100), "", SvgGui::useFile("icons/ic_folder.svg")));
}

void DocumentList::zoomListView(int step)
{
  iconWidth += step < 0 ? -20 : 20;  //*= step < 0 ? 0.8 : 1.25;
  iconWidth = std::max(60, std::min(iconWidth, 400));
  zoomIn->setEnabled(iconWidth < 400);
  zoomOut->setEnabled(iconWidth > 60);
  ScribbleApp::cfg->set("thumbnailSize", iconWidth);
  refresh();
};

void DocumentList::setup(Window* parent, Mode_t mode, const char* exts, bool cancelable)
{
  fileExts = (exts && exts[0]) ? exts : (docFileExt + " svgz svg html htm").c_str();
  currMode = mode;
  bool chooseOnly = mode == CHOOSE_DOC || mode == CHOOSE_IMAGE;

  // show/hide buttons depending on mode
  newDocBtn->setVisible(mode == OPEN_DOC);
  newFolderBtn->setVisible(!chooseOnly);
  helpBtn->setVisible(mode == OPEN_DOC);
  if(whiteboardBtn)
    whiteboardBtn->setVisible(mode == OPEN_DOC);
  saveHereBtn->setVisible(mode == SAVE_DOC || mode == SAVE_PDF);
  cancelChoose->setVisible(cancelable);
  focusedWidget = scrollWidget;  // so Page Up/Down, etc. work

  // doc list no longer used on iOS - comment this out since it is no longer up-to-date
//#ifdef PLATFORM_IOS
//  bool fs = parent->sdlWindow ? (SDL_GetWindowFlags(parent->sdlWindow) & SDL_WINDOW_FULLSCREEN) : false;
//  selectFirst("#ios-statusbar-bg")->setVisible(!fs);
//#endif

  setWinBounds(PLATFORM_MOBILE ? parent->winBounds() : parent->winBounds().pad(-40));
  // we want _NET_WM_WINDOW_TYPE_DIALOG but this isn't possible with SDL
  //svgGui->showWindow(this, parent, true, SDL_WINDOW_RESIZABLE|SDL_WINDOW_UTILITY);

  refresh();
}

void DocumentList::refresh()
{
  setCurrDir(currDir.c_str());
}

void DocumentList::finish(Result_t res)
{
  hideUndo();
  clearClipboard();
  gui()->closeWindow(this);
  result = res;
  if(onFinished)
    onFinished(res);
}

void DocumentList::setCurrDir(const char* path)
{
  FSPath pathinfo(path);
  if(!pathinfo.exists())
    return;
  //if(docListSiloed && !StringRef(pathinfo.c_str()).startsWith(docRoot.c_str())) { pathinfo = docRoot; }

  bool hasLegacyDocs = false;
  bool writable = true; //pathinfo.isWritable();
  newDocBtn->setEnabled(writable);
  newFolderBtn->setEnabled(writable);
  // can't paste into read-only folder, obviously
  pasteButton->setEnabled(writable);

  // TODO: reuse existing nodes instead of always deleting and recreating
  if(gui())
    gui()->deleteContents(listView, ".listitem");

  bool uselist = iconWidth < 80;
  listView->node->setAttribute("flex-direction", uselist ? "column" : "row");
  listView->node->setAttribute("flex-wrap", uselist ? "nowrap" : "wrap");

  Rect iconSize = uselist ? Rect::wh(30, 50) : Rect::wh(iconWidth, (5*iconWidth)/3);
  fileUseNode->setViewport(iconSize);
  folderUseNode->setViewport(iconSize);

  auto itemRightClick = [this](SvgGui* gui, Widget* widget, Point p){
    //listView->clearSelection();
    contextMenuItem = widget->userData<FSPath>();
    contextMenuCopy->setVisible(!contextMenuItem.isDir());  // or setEnabled() to disable instead of hide?
    contextMenuOpenCopy->setVisible(!contextMenuItem.isDir());
    //widget->node->addClass("pressed");  -- how to clear pressed when context menu closed?
    gui->showContextMenu(contextMenu, p);
  };

  auto isWriteDoc = [this](const FSPath& fileinfo){
    if(fileinfo.name().substr(0,1) == ".")  // don't crash if name is empty
      return false;
    // don't show old multi-file doc page files ending with "_pageXXX.svg"
    if(fileinfo.extension() == "svg" && StringRef(fileinfo.baseName()).chop(3).endsWith("_page"))
      return false;
    return fileinfo.isDir() || containsWord(fileExts.c_str(), fileinfo.extension().c_str());
  };

  // update contents
  enum sortBy_t {SORT_NAME, SORT_MTIME};
  sortBy_t sortBy = ScribbleApp::cfg->Int("docListSort") == 1 ? SORT_MTIME : SORT_NAME;
  std::vector<std::string> allfiles = lsDirectory(pathinfo);
  // extract folders and files we support
  std::vector<std::string> files;
  std::vector<Timestamp> mtimes;
  std::vector<long> fsizes;
  for(const std::string& file : allfiles) {
    if(file.empty() || file.front() == '.')
      continue;
    FSPath fileinfo = pathinfo.child(file);
    if(!isWriteDoc(fileinfo))
      continue;
    files.emplace_back(file);
    hasLegacyDocs = hasLegacyDocs || (fileinfo.extension() == "html"
        && fileinfo.baseName().size() == 13 && fileinfo.baseName()[0] == '1');
    if(sortBy == SORT_MTIME || uselist)
      mtimes.push_back(fileinfo.isDir() ? 0 : getFileMTime(fileinfo));
    if(uselist) {
      if(fileinfo.isDir()) {
        std::vector<std::string> contents = lsDirectory(fileinfo);
        fsizes.push_back(std::count_if(contents.begin(), contents.end(), isWriteDoc));
      }
      else
        fsizes.push_back(getFileSize(fileinfo));
    }
  }

  // sort indices instead of names themselves (i.e., argsort)
  std::vector<size_t> indices(files.size());
  for(size_t ii = 0; ii < files.size(); ++ii) { indices[ii] = ii; }  // or use std::iota
  std::sort(indices.begin(), indices.end(), [&files, &mtimes, sortBy](size_t a, size_t b) {
    if(files[a].back() == '/' && files[b].back() != '/')
      return true;
    if(files[a].back() != '/' && files[b].back() == '/')
      return false;
    if(sortBy == SORT_MTIME && mtimes[a] > 0 && mtimes[b] > 0)
      return mtimes[a] > mtimes[b];  // we want most recently modified (larger mtime) first
    return toLower(files[a]) < toLower(files[b]);  // always case-insensitive
  });

  for(size_t ii : indices) {
    const std::string& filename = files[ii];
    FSPath fileinfo = pathinfo.child(filename);
    Button* item = new Button(uselist ? listItemProto->clone() : gridItemProto->clone());
    item->setUserData<FSPath>(fileinfo);
    item->onClicked = [this, fileinfo](){
      //const FSPath& fileinfo = widget->userData<FSPath>();
      hideUndo();
      if(fileinfo.isDir())
        setCurrDir(fileinfo.c_str());
      else {
        selectedFile = fileinfo.c_str();
        finish(EXISTING_DOC);
      }
    };
    item->setEnabled((currMode != SAVE_DOC && currMode != SAVE_PDF) || fileinfo.isDir());  // prevent file selection in
    // tentative approach to handling right click is to wrap sdlEvent with second event handler for
    //  right click/long press; we expect right click handlers to be much less numerous than left click handlers
    if(currMode != CHOOSE_DOC && currMode != CHOOSE_IMAGE)
      SvgGui::setupRightClick(item, itemRightClick);

    SvgContainerNode* container = item->selectFirst(".image-container")->containerNode();
    // TODO: provide SvgImage::setImage() and implement move assignment operator for Image
    if(fileinfo.isDir())
      container->addChild(folderUseNode->clone());
    else if(!containsWord("svg svgz html htm", fileinfo.extension().c_str()))  //fileinfo.extension() != docFileExt)
      container->addChild(fileUseNode->clone());
    else {
      Image thumbnail = ScribbleDoc::extractThumbnail(fileinfo.c_str());
      if(!thumbnail.isNull())
        container->addChild(new SvgImage(std::move(thumbnail), iconSize));
      else
        container->addChild(fileUseNode->clone());
    }

    listView->addWidget(item); //structureNode()->addChild(item->node);

    // ellipsize name as needed - have to do this after adding widget to list so style is correct
    std::string s = fileinfo.extension() == docFileExt ? fileinfo.baseName() : fileinfo.fileName();
    SvgText* textnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    textnode->addText(s.c_str());
    if(!uselist)
      SvgPainter::elideText(textnode, iconWidth);

    if(uselist) {
      SvgText* mtimenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".mtime-text"));
      if(fileinfo.isDir())
        mtimenode->addText(fsizes[ii] == 1 ? _("1 item") : fstring(_("%d items"), fsizes[ii]).c_str());
      else {
        // last modified time
        char timestr[64];
        time_t mtime = mtimes[ii];
        //Timestamp ago = mSecSinceEpoch()/1000 - mtimes[ii];
        const char* timefmt = "%d %b %Y %H:%M";  //ago < 60*60*24*364 ? "%d %b %H:%M" : "%d %b %Y %H:%M";
        strftime(timestr, sizeof(timestr), timefmt, localtime(&mtime));
        mtimenode->addText(timestr);
        SvgText* fsizenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".fsize-text"));
        // file size
        double fsize = fsizes[ii];
        if(fsize >= 999500)
          fsizenode->addText(fstring("%.3g MB", fsize/1E6).c_str());
        else if(fsize >= 999)
          fsizenode->addText(fstring("%.3g KB", fsize/1E3).c_str());
        else
          fsizenode->addText(fstring("%.0f B", fsize).c_str());
      }
    }

  }

  // reset scroll if different folder, otherwise, just ensure position is still valid
  if(currDir == pathinfo)
    scrollWidget->scroll(Point(0, 0));
  else
    scrollWidget->scrollTo(Point(0, 0));
  currDir = pathinfo;
  // previously, we updated currFolder when opening doc, but that meant drag and drop changed startup folder
  ScribbleApp::cfg->set("currFolder", currDir.c_str());

  // update breadcrumbs
  bool ok = true;
  for(Button* breadCrumb : breadCrumbs) {
    breadCrumb->setVisible(ok);
    if(ok) {
      std::string title = pathinfo.isRoot() ? "/" : pathinfo.fileName().c_str();
      breadCrumb->setText(title.size() > 20 ? title.substr(0, 17).append("...").c_str() : title.c_str());
      breadCrumb->setShowTitle(true);  // force layout auto adjust
      breadCrumb->onClicked = [this, pathinfo]() { setCurrDir(pathinfo.c_str()); };
      pathinfo = pathinfo.parent();
      ok = pathinfo.exists();
      if(docListSiloed && !StringRef(pathinfo.c_str()).startsWith(docRoot.c_str()))
        ok = false;
    }
  }
#if PLATFORM_WIN
  drivesBtn->setVisible(currDir.fileName().back() == ':');
#elif PLATFORM_ANDROID
  drivesBtn->setVisible(!breadCrumbs.back()->isVisible());
  if(drivesBtn->isVisible())
    privateDirBtn->setChecked(StringRef(currDir.c_str()).contains("Android/data/com.styluslabs.writeqt"));
    //sharedDirBtn->setChecked(!privateDirBtn->isChecked());
#endif

  if(hasLegacyDocs && ScribbleApp::cfg->Bool("askConvertDocs")) {
    auto promptres = ScribbleApp::messageBox(ScribbleApp::Question, _("Convert Documents"),
        _("Documents from your previous version of Write need to be converted to restore titles and folders."),
        {_("Convert"), _("Cancel")});
    if(promptres != _("Convert") || !convertDocuments(currDir))
      ScribbleApp::messageBox(ScribbleApp::Info, _("Convert Documents"),
          _("To convert later, long press on the folder title in the toolbar."));
    ScribbleApp::cfg->set("askConvertDocs", false);
  }
}

void DocumentList::renameItem()
{
  FSPath oldinfo(contextMenuItem);
  const char* dialogtitle = oldinfo.isDir() ? _("Rename folder") : _("Rename document");
  NewDocDialog dialog(dialogtitle, oldinfo, false);  //editname.c_str(), false);
  int res = Application::execDialog(&dialog);
  std::string newname = dialog.getName();  //nameEdit->text();
  if(res != Dialog::ACCEPTED || newname.empty())
    return;
  ScribbleApp::app->closeDocs(oldinfo);
  bool ok = true;
  FSPath newinfo = oldinfo.parent().child(newname);
  if(!oldinfo.isDir() && oldinfo.extension() != newinfo.extension())
    newinfo = FSPath(newinfo.path + "." + oldinfo.extension());
  if(!newinfo.exists() && oldinfo.exists()) {
    if(isMultiFileDoc(oldinfo))
      ok = copyDocument(oldinfo, newinfo, true);
    else
      ok = moveFile(oldinfo, newinfo);
  }
  if(!ok) {
    msgBar->setText(fstring(_("Error renaming \"%s\""), oldinfo.fileName().c_str()).c_str());
    msgBar->setVisible();
  }
  refresh();
}

void DocumentList::cutItem()
{
  clipboard = contextMenuItem;
  hideUndo();  // make sure undo bar isn't visible
  pasteBar->setVisible(true);
  pasteButton->setText(fstring(_("Move \"%s\" here"), clipboard.baseName().c_str()).c_str());
  cutClipboard = true;
}

void DocumentList::copyItem()
{
  if(contextMenuItem.isDir())  // should never happen since context menu copy is hidden for folders
    return;
  cutItem();
  pasteButton->setText(fstring(_("Copy \"%s\" here"), clipboard.baseName().c_str()).c_str());
  cutClipboard = false;
}

// create a copy of item, prompt to rename, then open (to take using templates easier)
void DocumentList::openCopyItem()
{
  // we want copy to default to default file type, not type of template, so we have to open, then save document
  hideUndo();
  FSPath srcinfo(contextMenuItem);
  FSPath newinfo(contextMenuItem);
  for(int ii = 2; newinfo.exists(); ii++)
    newinfo = newinfo.parent().child(fstring("%s (%d).%s", newinfo.baseName().c_str(), ii, docFileExt.c_str()));
  NewDocDialog dialog(_("New Document"), newinfo, false);
  int res = Application::execDialog(&dialog);
  std::string newname = dialog.getName();  //nameEdit->text();
  if(res != Dialog::ACCEPTED || newname.empty())
    return;
  newinfo = currDir.child(newname);
  if(!containsWord(fileExts.c_str(), newinfo.extension().c_str()))
    newinfo = currDir.child(newname + "." + docFileExt);
  if(!FSPath(newinfo.c_str()).exists("wb")) {
    msgBar->setText(_("Unable to create document. Try a different folder."));
    msgBar->setVisible();
    return;
  }
  selectedFile = newinfo.c_str();
  selectedSrcFile = contextMenuItem.c_str();
  selectedRuling = 0;
  finish(OPEN_COPY);
}

void DocumentList::clearClipboard()
{
  clipboard.clear();
  pasteBar->setVisible(false);
}

// paste button will be enabled/disabled based only on writibility of current folder; paste bar (and thus
//  paste button) will only be visible if clipboard is non-empty
void DocumentList::pasteItem()
{
  if(clipboard.isEmpty())
    return;
  FSPath srcinfo(clipboard);
  clipboard.clear();
  //pasteButton->setEnabled(false);
  pasteBar->setVisible(false);
  if(!srcinfo.exists())
    return;

  FSPath destinfo = currDir.child(srcinfo.name());
  // cut and paste into same dir is a nop
  if(cutClipboard && currDir == srcinfo.dir())
    return;
  if(cutClipboard)
    ScribbleApp::app->closeDocs(srcinfo);
  bool ok = true;
  // regardless of whether we are pasting into same directory or not, we need to avoid name conflict!
  std::string suffix = srcinfo.isDir() ? "/" : ("." + srcinfo.extension());
  for(int ii = 2; destinfo.exists(); ii++)
    destinfo = currDir.child(fstring("%s (%d)%s", srcinfo.baseName().c_str(), ii, suffix.c_str()));
  if(isMultiFileDoc(srcinfo))
    copyDocument(srcinfo, destinfo, cutClipboard);
  else if(cutClipboard)
    ok = moveFile(srcinfo, destinfo);
  else
    copyFile(srcinfo, destinfo);
  if(!ok) {
    msgBar->setText(fstring(_("Error moving \"%s\""), srcinfo.fileName().c_str()).c_str());
    msgBar->setVisible();
  }
  refresh();
}

// for multi-file documents - note that we cannot just rename each file because of refs to _pageXXX.svg files
//  in the html file; this discards thumbnail - ok since svgz is the default and preferred doc format
bool DocumentList::copyDocument(const FSPath& src, const FSPath& dest, bool move)
{
  Document tempdoc;
  if(tempdoc.load(new FileStream(src.c_str()), false) != Document::LOAD_OK)
    return false;
  if(!tempdoc.save(new FileStream(dest.c_str(), "wb"), NULL, Document::SAVE_FORCE | Document::SAVE_MULTIFILE))
    return false;
  return move ? ScribbleDoc::deleteDocument(src.c_str()) : true;
}

bool DocumentList::isMultiFileDoc(const FSPath& fileinfo) const
{
  return !fileinfo.isDir() && fileinfo.extension() != "svg" && fileinfo.extension() != "svgz"
      && FSPath(fileinfo.basePath() + "_page001.svg").exists();
}

// How deletion works:
// - to delete, we move item to $TEMP/write-trash/, after emptying any previous contents
// - to undelete, we move everything in $TEMP/write-trash/ back to original folder

void DocumentList::undoDelete()
{
  // move everything in trash folder to undoDeleteDir
  auto contents = lsDirectory(trashPath);
  for(const std::string& file : contents)
    moveFile(trashPath.child(file), undoDeleteDir.child(file));
  undoDeleteDir.clear();
  hideUndo();
  refresh();
}

void DocumentList::hideUndo()
{
  undoBar->setVisible(false);
  msgBar->setVisible(false);
}

void DocumentList::deleteItem()
{
  // ensure trash folder exists and remove any previous contents (i.e. last item deleted)
  createPath(trashPath.c_str());
  removeDir(trashPath.c_str(), false);

  bool ok = true;
  FSPath& srcinfo = contextMenuItem;
  ScribbleApp::app->closeDocs(srcinfo);
  undoDeleteDir = srcinfo.parent();
  if(isMultiFileDoc(srcinfo)) {
    // to make it possible to delete damaged documents, don't try to load the document
    for(int pagenum = 1;; ++pagenum) {
      FSPath svginfo(srcinfo.basePath() + fstring("_page%03d.svg", pagenum));
      if(!svginfo.exists())
        break;
      ok = moveFile(svginfo, trashPath.child(svginfo.fileName())) && ok;
    }
  }
  // move the html file (or folder if folder)
  ok = moveFile(srcinfo, trashPath.child(srcinfo.name())) && ok;
  if(!ok) {
    undoDelete();
    PLATFORM_LOG("Delete file: rename(%s, %s) failed!", srcinfo.c_str(), trashPath.child(srcinfo.name()).c_str());
    msgBar->setText(fstring(_("Error deleting \"%s\""), srcinfo.fileName().c_str()).c_str());
    msgBar->setVisible();
  }
  else {
    undoButton->setText(fstring(_("Undo delete \"%s\""), srcinfo.baseName().c_str()).c_str());
    undoBar->setVisible(true);
  }
  refresh();
}

void DocumentList::openWhiteboard()
{
  selectedFile = "";  //fileModel->rootPath();
  finish(OPEN_WHITEBOARD);
}

// create a blank .html file and prompt to rename
void DocumentList::newDoc()
{
  hideUndo();

  time_t rawtime;
  struct tm* timeinfo;
  char buffer[80];
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(buffer, 80, ScribbleApp::cfg->String("newDocTitleFmt", "%b %e %Hh%M"), timeinfo);

  std::string ext = currMode == SAVE_PDF ? "pdf" : docFileExt;
  FSPath docinfo = currDir.child(&buffer[0] + ("." + ext));
  // find available doc name
  for(int ii = 2; docinfo.exists(); ii++)
    docinfo = currDir.child(fstring(_("New Document %d.%s"), ii, ext.c_str()));

  //refresh();  renameItem(true);
  const char* title = _("New Document");
  if(currMode == SAVE_DOC)
    title = _("Save Document");
  else if(currMode == SAVE_PDF)
    title = _("Save PDF");
  NewDocDialog dialog(title, docinfo, currMode == OPEN_DOC);
  int res = Application::execDialog(&dialog);
  std::string newname = dialog.getName();  //nameEdit->text();
  if(res != Dialog::ACCEPTED || newname.empty())
    return;

  // create file
  docinfo = currDir.child(newname);
  if(!containsWord(fileExts.c_str(), docinfo.extension().c_str()))
    docinfo = currDir.child(newname + "." + ext);
  // we assume NewDocDialog ensures that file does not already exist (so we don't overwrite it)
  FILE* f = fopen(docinfo.c_str(), "wb");
  if(!f) {
    msgBar->setText(_("Unable to create document. Try a different folder."));
    msgBar->setVisible();
    return;
  }
  fclose(f);
  selectedFile = docinfo.c_str();
  selectedRuling = dialog.comboRuling ? dialog.comboRuling->index() : 0;
  finish(NEW_DOC);
}

void DocumentList::newFolder()
{
  hideUndo();
  // find available name
  FSPath folderinfo = currDir.child(_("New Folder/"));
  for(int ii = 2; folderinfo.exists(); ii++)
    folderinfo = currDir.child(fstring(_("New Folder %d/"), ii));

  NewDocDialog dialog(_("Create folder"), folderinfo, false);
  int res = Application::execDialog(&dialog);
  std::string newname = dialog.getName();  //nameEdit->text();
  if(res != Dialog::ACCEPTED || newname.empty())
    return;
  folderinfo = currDir.child(newname);
  if(!createDir(folderinfo.c_str())) {
    msgBar->setText(_("Unable to create new folder. Try a different folder."));
    msgBar->setVisible();
    return;
  }
  refresh();
}

// uses fsinfo.baseName() as initial text
NewDocDialog::NewDocDialog(const char* title, const FSPath& fsinfo, bool newdoc) : Dialog(createDialogNode())
{
  nameEdit = createTextEdit();
  nameEdit->setText(fsinfo.baseName().c_str());
  setMinWidth(nameEdit, 320);
  nameEdit->onChanged = [this, fsinfo](const char* s){
    bool exists = s[0] && !StringRef(s).trimmed().isEmpty() && (fsinfo.parent().child(s).exists()
        || (!fsinfo.isDir() && fsinfo.parent().child(s + ("." + fsinfo.extension())).exists()));
    // on Windows, colon in filename creates alternative data stream, so everything works but file can't be
    //  reopened by Write!
    bool invalid = strpbrk(s, PLATFORM_WIN ? "\\/:*?\"<>|" : "/") != NULL;
    // force resize to fit message bar - I'd rather not shrink when hiding, but couldn't get it
    //  box-anchor="top" on nameEdit to work ... any way, provides a test of changing window bounds!
    if(msgBar->isVisible() != (exists || invalid))
      setWinBounds(Rect::centerwh(winBounds().center(), winBounds().width(), 0));
    if(invalid)
      msgBar->setText("Invalid characters in name");
    else if(exists)
      msgBar->setText(fsinfo.isDir() ? _("Folder already exists") : _("Document already exists"));
    msgBar->setVisible(exists || invalid);
    acceptBtn->setEnabled(!invalid && !exists && s[0]);
  };

  // we could consider adding a checkbox to allow replacement of existing file
  msgBar = createToolbar();
  msgBar->setVisible(false);
  msgBar->node->addClass("warning");
  //const char* msg = fsinfo.isDir() ? _("Folder already exists") : _("Document already exists");
  msgBar->addWidget(new Widget(createTextNode("")));

  Widget* dialogBody = selectFirst(".body-container");
  dialogBody->addWidget(nameEdit);
  dialogBody->addWidget(msgBar);
  dialogBody->setMargins(8, 8, 0, 8);
  focusedWidget = nameEdit;
  nameEdit->selectAll();

  if(newdoc) {
    comboRuling = createComboBox({_("Default"), _("Plain"), _("Wide ruled"), _("Medium ruled"),
        _("Narrow ruled"), _("Coarse grid"), _("Medium grid"), _("Fine grid")});
    dialogBody->addWidget(createTitledRow(_("Paper type"), comboRuling));
  }

  setTitle(title);
  acceptBtn = addButton(_("OK"), [this](){ finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
}

//#include <thread>
// This absolute disaster is due to the fact that we can't save a document on a thread, because we need bounds
//  for links (if nothing else), and we can't calculate bounds on a thread, because we need nanovg context for
//  bounds calculation, primarily for text bounds calculation

// main purpose is importing documents from original Android version of Write
bool DocumentList::convertDocuments(FSPath src)
{
  int nFiles = 0, nErrors = 0, nDup = 0, nConvert = 0;
  FSPath dest = src.parent().child(src.baseName() + "_converted/");

  auto confirm = ScribbleApp::messageBox(ScribbleApp::Info, _("Convert Documents"), fstring(
      _("Write documents in this folder will be converted to %s format (the default set in Preferences) in folder \"%s\""),
      docFileExt.c_str(), dest.baseName().c_str()), {_("Convert"), _("Cancel")});
  if(confirm != _("Convert"))
    return false;

  std::unique_ptr<Dialog> dialog(createDialog(_("Converting Documents: XXX")));
  Widget* dialogBody = dialog->selectFirst(".body-container");
  dialogBody->setMargins(10);
  Widget* msgText = new TextBox(createTextNode(_("Converting...")));
  dialogBody->addWidget(msgText);
  dialog->addButton(_("Cancel"), [&](){ dialog->finish(Dialog::CANCELLED); });
  ScribbleApp::gui->showModal(dialog.get(), this);
  // make sure processEvents() doesn't stall
  ScribbleApp::gui->setTimer(100, dialog.get(), NULL, []() { return 100; });

  ScribbleDoc* scribbleDoc = ScribbleApp::app->scribbleDocs[0];
  bool renameDups = !dest.exists();
  createPath(dest.path);
  std::vector<std::string> allfiles = lsDirectory(src);
  for(const std::string& file : allfiles) {
    ++nFiles;
    if(file.empty() || file.front() == '.')
      continue;
    FSPath oldinfo = src.child(file);
    if(oldinfo.isDir() || !(containsWord(fileExts.c_str(), oldinfo.extension().c_str())))
      continue;
    if(oldinfo.extension() == "svg" && StringRef(oldinfo.baseName()).chop(3).endsWith("_page"))
      continue;

    auto openres = scribbleDoc->openDocument(oldinfo.c_str(), true);

    //FileStream* instrm = new FileStream(oldinfo.c_str(), "rb");
    //std::unique_ptr<Document> newdoc(new Document());
    //auto openres = newdoc->load(instrm, true);
    if(openres == Document::LOAD_FATAL) {
      ScribbleApp::messageBox(ScribbleApp::Error, _("Error"), fstring(_("Error loading file %s"), oldinfo.c_str()));
      continue;
    }
    bool ok = (openres == Document::LOAD_OK || openres == Document::LOAD_EMPTYDOC);
    //std::unique_ptr<ScribbleConfig> doccfg(new ScribbleConfig());
    //doccfg->loadConfig(newdoc->getConfigNode());

    ScribbleConfig* doccfg = scribbleDoc->cfg;
    const char* oldtitle = doccfg->String("docTitle", "");
    std::string title = (oldtitle[0] ? toValidFilename(oldtitle): oldinfo.baseName().c_str()) + ("." + docFileExt);
    const char* tags = doccfg->String("docTags", "");
    FSPath newinfo = tags[0] ? dest.child(toValidFilename(tags)).child(title) : dest.child(title);
    if(!renameDups && newinfo.exists()) {
      ++nDup;
      continue;
    }
    // renamed, baseRenamed
    for(int ii = 2; newinfo.exists(); ii++)
      newinfo = newinfo.parent().child(fstring("%s (%d).%s", newinfo.baseName().c_str(), ii, newinfo.extension().c_str()));
    if(tags[0])
      createPath(newinfo.parent().path);


    // rendering document on main thread in ScribbleArea while saving seems to cause problems
    /*std::thread saveThread([&](){
      // thumbnail requires ScribbleArea to draw and can only be drawn main thread (owner of GL context)
      Image thumbnail = ScribbleDoc::extractThumbnail(oldinfo.c_str());
      auto buff = Image::toBase64(thumbnail.encode());
      FileStream* strm = new FileStream(newinfo.c_str(), "wb+");
      Document::saveflags_t flags = Document::SAVE_FORCE;
      flags |= doccfg->Int("compressLevel", 2) << 24;  // ignored for uncompressed formats
      if(!strm->is_open() || !newdoc->save(strm, (char*)buff.data(), flags)) {
        ok = false;
        delete strm;
      }

      SDL_Event event = {0};
      event.type = ScribbleApp::app->scribbleSDLEvent;
      event.user.code = ScribbleApp::DISMISS_DIALOG;
      event.user.data1 = (void*)-1;
      SDL_PushEvent(&event);
      PLATFORM_WakeEventLoop();
    });*/

    std::string fulltitle = fstring("%s%s%s", tags, tags[0] ? "/" : "", newinfo.baseName().c_str());
    //std::string msgres = ScribbleApp::messageBox(ScribbleApp::Info,
    //    fstring("Converting Documents: %d%%", 100*nFiles/allfiles.size()),
    //    fstring("Converting %s", fulltitle.c_str()), "Cancel");

    // show dialog
    dialog->setTitle(fstring(_("Converting Documents: %d%%"), 100*nFiles/allfiles.size()).c_str());
    msgText->setText(fstring(_("Converting %s"), fulltitle.c_str()).c_str());
    dialog->setWinBounds(Rect::centerwh(dialog->winBounds().center(), 0, 0));
    Application::processEvents();
    Application::layoutAndDraw();
    // save doc
    ok = scribbleDoc->saveDocument(newinfo.c_str(), Document::SAVE_FORCE) && ok;
    // check for dialog close
    Application::processEvents();
    Application::layoutAndDraw();
    if(!dialog->isVisible())
      return false;

    //saveThread.join();
    //if(!msgres.empty())
    //  return false;
    if(!ok) {
      ++nErrors;
      ScribbleApp::messageBox(ScribbleApp::Error, _("Error"), fstring(
          _("Errors occurred converting \"%s\" from file \"%s\" - please examine document after conversion finishes."),
          fulltitle.c_str(), oldinfo.fileName().c_str()));
    }
    ++nConvert;
  }
  dialog->finish(0);
  setCurrDir(dest.c_str());  // show user the output folder
  ScribbleApp::messageBox(ScribbleApp::Info, _("Conversion Results"),
      fstring(_("%d documents were converted with %d error(s) and %d duplicates skipped."
      " Please examine all converted documents before removing this folder."), nConvert, nErrors, nDup));
  return true;
}
