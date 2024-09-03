#include <time.h>
#include "mainwindow.h"
#include "scribbleapp.h"
#include "scribblearea.h"
#include "scribblewidget.h"
#include "scribblesync.h"
#include "bookmarkview.h"
#include "clippingview.h"
#include "scribbledoc.h"
#include "pentoolbar.h"
#include "touchwidgets.h"

// SVG for main window, default clippings; preferences info
#include "res_ui.cpp"

// TODO: figure out why just capturing mw doesn't work
#define SLOT(x) [=](){ app->x; }

MainWindow* createMainWindow()
{
  return new MainWindow(createWindowNode(mainWindowSVG));
}

MainWindow::~MainWindow()
{
  for(Action* action : actionList) {
    if(action->buttons.empty() && action->menu)
      delete action->menu->node;
    delete action;
  }
}

//template<typename T>
Action* MainWindow::createAction(const char* name,
    const char* title, const char* iconfile, const char* shortcut, const std::function<void()>& callback)
{
  const SvgNode* icon = (iconfile && iconfile[0]) ? SvgGui::useFile(iconfile) : NULL;
  // this works (but crashes due to double free) - might need it if we want to use stroked icon style for iOS
  //if(icon && !icon->parent())
  //  node->document()->namedNode("main-defs")->asContainerNode()->addChild(const_cast<SvgNode*>(icon));
  std::string titlestr(title);
  titlestr.erase(remove(titlestr.begin(), titlestr.end(), '&'), titlestr.end());

  Action* action = new Action(name, _(titlestr.c_str()), icon);
  actionList.push_back(action);
//#if !PLATFORM_MOBILE
  if(shortcut && shortcut[0])
    shortcuts.emplace(shortcut, action);
  if(callback)
    action->onTriggered = callback;
  return action;
}

Menu* MainWindow::createMenu(const char* name, const char* title, Menu::Align align, bool showicons)
{
  Menu* menu = ::createMenu(align, showicons);
  //menu->setObjectName(name);
  //menu->setTitle(_(title));
  return menu;
}

void MainWindow::refreshScribbleWidget(ScribbleWidget* w, const UIState* uiState)
{
  //pageNumLabel->setVisible(Application::cfg->Bool("displayPageNum"));
  if(w->pageNumLabel) {
    std::string s = fstring("%d / %d", uiState->pageNum, uiState->totalPages);
    // if we come up with additional use cases, we can consider moving the check into, e.g., TextBox
    if(s != w->pageNumLabel->text())
      w->pageNumLabel->setText(s.c_str());
  }
  if(w->prevPage)
    w->prevPage->setEnabled(uiState->pageNum > 1);
  if(w->nextPage) {
    bool lastpage = uiState->pageNum == uiState->totalPages;
    w->nextPage->setIcon(lastpage ? appendPageIcon : nextPageIcon);
    w->nextPage->setEnabled(!lastpage || (uiState->currPageStrokes > 0 && !uiState->rxOnly));
  }

  //if(cfg->Bool("extraPageInfo")) {
  //  int idx = sprintf(pagetext, "%s%d / %s%d (", (uiState->pagemodified != 0) ? "*" : "", currPageNum+1,
  //      uiState->docmodified ? "*" : "", numPages());
  if(w->timeRangeLabel) {
    bool showtime = ScribbleApp::cfg->Int("displayTimeRange") > 0 && w->node->bounds().width() > 325;
    w->timeRangeLabel->setVisible(uiState->minTimestamp > 0 && uiState->maxTimestamp > 0 && showtime);
    if(w->timeRangeLabel->isVisible()) {
      // we tried a mode to show max time as "3 days ago", etc. but I didn't like it - removed June 2 2019
      char tminstr[64];
      char tmaxstr[64];
      time_t tmin = uiState->minTimestamp/1000;
      time_t tmax = uiState->maxTimestamp/1000;  // in seconds
      // if we display "N days ago" etc, we want maxTimestamp, but use minTimestamp for showing/hiding year
      Timestamp ago = (mSecSinceEpoch() - uiState->minTimestamp)/1000;
      const char* timefmt = ago < 60*60*24*364 ? "%d %b %H:%M" : "%d %b %Y %H:%M";
      strftime(tminstr, sizeof(tminstr), timefmt, localtime(&tmin));
      strftime(tmaxstr, sizeof(tmaxstr), timefmt, localtime(&tmax));
      // \u2013 is en dash; u8 specifier is required for MSVC
      std::string s = fstring(u8"%s \u2013 %s", tminstr, tmaxstr);
      if(ScribbleApp::cfg->Int("displayTimeRange") > 1) {
        s += fstring(" (%.0f x %.0f)", uiState->pageWidth, uiState->pageHeight);
        s += uiState->activeSel ? fstring(" (%d/", uiState->currSelStrokes).c_str() : " (";
        s += fstring("%d strokes)", uiState->currPageStrokes);
      }
      if(s != w->timeRangeLabel->text())
        w->timeRangeLabel->setText(s.c_str());
    }
  }
  if(w->zoomLabel) {
    int zoom = int(uiState->zoom*100 + 0.5);
    // let's try hiding "100%" on status bar; need to add press-drag handler to icon!
    w->zoomLabel->setVisible(zoom != 100);  //w->pageNumLabel || zoom != 100);
    std::string s = fstring("%d%%", zoom);
    if(s != w->zoomLabel->text())
      w->zoomLabel->setText(s.c_str());
  }
}

void MainWindow::refreshCommonUI(ScribbleDoc* doc, const UIState* uiState)
{
  if(winTitle[0] != '*' && doc->isModified())
    setTitle(("*" + winTitle).c_str());
  else if(winTitle[0] == '*' && !doc->isModified())
    setTitle(winTitle.substr(1).c_str());
  //actionReset_Zoom->setEnabled(uiState->zoom != 1);  // UI doesn't always refresh on zoom
  actionSave->setEnabled(doc->isModified() || PLATFORM_EMSCRIPTEN);  // don't know if user canceled download
  actionUndo->setEnabled(doc->canUndo());
  actionRedo->setEnabled(doc->canRedo());
  // we're not going to bother changing tooltip text anymore
  undoRedoBtn->setEnabled(doc->canUndo() || doc->canRedo());

  actionCut->setEnabled(uiState->activeSel || uiState->pageSel);
  actionCopy->setEnabled(uiState->activeSel || uiState->pageSel);
  actionDelete_Selection->setEnabled(uiState->activeSel || uiState->pageSel);
  actionDupSel->setEnabled(uiState->activeSel || uiState->pageSel);  // maybe only enable if 1 page selected?
  //actionSelect_Similar->setEnabled(uiState->activeSel);
  actionInvert_Selection->setEnabled(uiState->activeSel || uiState->pageSel);
  actionCreate_Link->setEnabled(uiState->activeSel);
  actionUngroup->setEnabled(uiState->selHasGroup);
  actionPaste->setEnabled(app->clipboard != NULL || !ScribbleApp::cfg->Bool("preloadClipboard"));

  actionPrevious_View->setEnabled(uiState->prevView);
  actionNext_View->setEnabled(uiState->nextView);
  action_Previous_Page->setEnabled(uiState->pageNum > 1);

  bool lastpage = uiState->pageNum == uiState->totalPages;
  action_Next_Page->setIcon(lastpage ? appendPageIcon : nextPageIcon);
  action_Next_Page->setEnabled(!lastpage || (uiState->currPageStrokes > 0 && !uiState->rxOnly));

  // disable add/insert/delete page actions for lecture mode whiteboards
  actionNew_Page_After->setEnabled(!uiState->rxOnly);
  actionNew_Page_Before->setEnabled(!uiState->rxOnly);
  actionSelect_Pages->setEnabled(!uiState->rxOnly);
  actionInsertDocument->setEnabled(!uiState->rxOnly);
  // change title button icon to a cloud when connected
  titleButton->setIcon(uiState->syncActive ? swbIcon : appIcon);

  // disable send now button if it will have no effect
  //if(actionSendNow)
  //  actionSendNow->setEnabled(uiState->syncActive && doc->scribbleSync->canSendHist());
  if(actionViewSync) {
    actionViewSync->setEnabled(uiState->syncActive);
    if(uiState->syncActive) {
      actionViewSync->setChecked(doc->scribbleSync->syncViewBox != ScribbleSync::SYNCVIEW_OFF);
      actionViewSyncMaster->setChecked(doc->scribbleSync->syncViewBox == ScribbleSync::SYNCVIEW_MASTER);
    }
  }
  // might just be temporary until I figure out a better interface
  if(uiState->syncActive != menuWhiteboardBtn->isEnabled()) {
    menuWhiteboardBtn->setEnabled(uiState->syncActive);
    menuWhiteboardBtn->setVisible(uiState->syncActive);
    actionSendImmed->setEnabled(!uiState->rxOnly);
    if(uiState->rxOnly) {
      actionViewSyncMaster->setEnabled(false);
      actionSendImmed->setChecked(false);
    }
  }

  PenPreview::bgColor = doc->getCurrPageColor();
  // setWindowModified does not exit early if no change
  /*if(isWindowModified() != uiState->docmodified)
    setWindowModified(uiState->docmodified);*/
}

void MainWindow::refreshPens(ScribbleDoc* doc)
{
  penDoc = doc && !doc->cfg->pens.empty() ? doc : NULL;
  for(size_t ii = 0; ii < penPreviews.size(); ii++) {
    Widget* previewbtn = penPreviews[ii];
    PenPreview* penpreview = static_cast<PenPreview*>(previewbtn->selectFirst(".pen-preview"));
    Widget* tooltip = previewbtn->selectFirst(".tooltip");
    const ScribblePen* pen = doc ? doc->cfg->getPen(ii) : ScribbleApp::cfg->getPen(ii);
    if(!pen || !previewbtn || !tooltip || !penpreview) continue;  // should never happen
    penpreview->setPen(*pen);

    std::string s;
    s += "Color: " + colorToHex(pen->color);
    s += fstring(" Width: %.3g ", pen->width);
    s += pen->hasFlag(ScribblePen::TIP_FLAT) ? _("Flat") : pen->hasFlag(ScribblePen::TIP_ROUND) ?
        _("Round") : pen->hasFlag(ScribblePen::TIP_CHISEL) ? _("Chisel") : "";
    std::string s2 = pen->hasFlag(ScribblePen::DRAW_UNDER) ? " Under," : "";
    s2 += pen->hasFlag(ScribblePen::SNAP_TO_GRID) ? " Snap," : "";
    s2 += pen->hasFlag(ScribblePen::LINE_DRAWING) ? " Lines," : "";
    s2 += pen->hasFlag(ScribblePen::EPHEMERAL) ? " Ephemeral," : "";
    if(!s2.empty())
      s += "\nSpecial:" + s2.substr(0, s2.size()-1);
    if(pen->hasVarWidth()) {
      s += fstring("\nVary Width %.3g", pen->wRatio);
      s += pen->hasFlag(ScribblePen::WIDTH_PR) ? fstring(" Pressure %.2f", 1/pen->prParam) : "";
      s += pen->hasFlag(ScribblePen::WIDTH_SPEED) ? fstring(" Speed %.2f", pen->spdMax) : "";
      s += pen->hasFlag(ScribblePen::WIDTH_DIR) ? fstring(u8" Direction %.3g\u00B0", pen->dirAngle) : "";
    }
    if(pen->dash > 0 || pen->gap > 0)
      s += fstring("\nDash: %.3g %.3g", pen->dash, pen->gap);

    tooltip->setText(s.c_str());
  }
}

// ideally, we'd avoid a full UI update on stroke finished, but lots of things can change, so let's just make
//  sure that there is no unnecessary layout or rendering
void MainWindow::refreshUI(ScribbleDoc* doc, int reason)
{
  if(doc == app->clippingDoc) {
    actionClippingsUndo->setEnabled(doc->canUndo());
    return;
  }

  UIState areaState;
  for(ScribbleArea* area : app->scribbleAreas) {
    ScribbleDoc* adoc = area->scribbleDoc;
    if(adoc) {
      area->updateUIState(&areaState);
      areaState.syncActive = adoc->scribbleSync && adoc->scribbleSync->isSyncActive();
      areaState.rxOnly = adoc->scribbleSync && !adoc->scribbleSync->enableTX;
      refreshScribbleWidget(area->widget, &areaState);
      if(area == app->activeArea())
        refreshCommonUI(adoc, &areaState);
    }
  }
  areaState.zoom = app->bookmarkArea->getZoom();
  refreshScribbleWidget(app->bookmarkArea->widget, &areaState);
  updateMode();

  // if a pen was released, may need to save pen to recent pens list
  if(reason & (1 << UIState::PenRelease)) {
    int savepen = ScribbleApp::cfg->Int("savePenMode");
    if(savepen > 0) {
      ScribbleApp::cfg->savePen(app->currPen);
      if(savepen == 2)  // 2 = save to both doc and global
        doc->cfg->savePen(app->currPen);
      refreshPens(doc);
    }
  }
  // refresh pens if necessary
  if((reason & (1 << UIState::SetDoc)) && penDoc != (!doc->cfg->pens.empty() ? doc : NULL))
    refreshPens(doc);
}

Action* MainWindow::modeToAction(int mode)
{
  switch(mode) {
    case MODE_PAN:  return actionPan;
    case MODE_STROKE:  return actionDraw;
    case MODE_BOOKMARK:  return actionAdd_Bookmark;
    case MODE_ERASE:  return actionErase;
    case MODE_ERASESTROKE:  return actionStroke_Eraser;
    case MODE_ERASERULED:  return actionRuled_Eraser;
    case MODE_ERASEFREE:  return actionFree_Eraser;
    case MODE_SELECT:  return actionSelect;
    case MODE_SELECTRECT:  return actionRect_Select;
    case MODE_SELECTRULED:  return actionRuled_Select;
    case MODE_SELECTLASSO:  return actionLasso_Select;
    case MODE_SELECTPATH:  return actionPath_Select;
    case MODE_INSSPACE:  return actionInsert_Space;
    case MODE_INSSPACERULED:  return actionRuled_Insert_Space;
    case MODE_INSSPACEVERT:  return actionInsert_Space_Vert;
    case MODE_PAGESEL:  return actionSelect_Pages;
    default: return NULL;
  }
}

/// actions

void MainWindow::updateMode()
{
  int mode = app->scribbleMode->getMode();
  int nextmode = app->scribbleMode->getNextMode();
  //if(Application::gui->hoveredWidget == app->scribbleDoc->activeArea->widget)
  //  app->scribbleDoc->setCursorfromMode(mode);
  Action* subaction = modeToAction(mode);
  Action* action = modeToAction(ScribbleMode::getModeType(mode));
  if(!action || !subaction)
    return;
  if(action != checkedMode) {
    if(checkedMode)
      checkedMode->setChecked(false);
    action->setChecked(true);
    checkedMode = action;
  }
  mode != nextmode ? checkedMode->addClass("once") : checkedMode->removeClass("once");
  if(actionTools_Menu->visible()) {
    actionTools_Menu->setChecked(mode != MODE_STROKE && mode != MODE_BOOKMARK);
    if(actionTools_Menu->isChecked()) {
      toolsMenuMode = ScribbleMode::getModeType(mode);  // required for double tap to lock
      mode != nextmode ? actionTools_Menu->addClass("once") : actionTools_Menu->removeClass("once");
    }
  }

  if(mode == MODE_STROKE) {
    if(action->icon() != drawIcon)
      action->setIcon(drawIcon);
  }
  if(subaction != action) {
    if(subaction != checkedSubMode) {
      if(checkedSubMode)
        checkedSubMode->setChecked(false);
      if(subaction != actionCustom_Pen) // && subaction != actionAdd_Bookmark)
        subaction->setChecked(true);
      checkedSubMode = subaction;
    }
    if(action->icon() != subaction->icon())
      action->setIcon(subaction->icon());
    if(actionTools_Menu->visible()) {
      if(actionTools_Menu->icon() != subaction->icon())
        actionTools_Menu->setIcon(subaction->icon());
    }
  }
  else {
    if(checkedSubMode)
      checkedSubMode->setChecked(false);
    checkedSubMode = NULL;
  }
  // update pen toolbar
  app->updatePenToolbar();  //penToolbar->setPen(app->getPen());
}

void MainWindow::toggleBookmarks()
{
  bool show = !actionShow_Bookmarks->checked();
  actionShow_Bookmarks->setChecked(show);
  if(show) {
    // we must do this before showing panel, since it could make parent bounds too big!
    Dim parentw = std::min(clippingsPanel->parent()->node->bounds().width(), winBounds().width());
    if(bookmarkPanel->node->bounds().width() > 0.75*parentw)
      bookmarkSplitter->setSplitSize(0.75*parentw);
    app->repaintBookmarks();
  }
  bookmarkPanel->setVisible(show);
  bookmarkSplitter->setVisible(show);
}

void MainWindow::toggleClippings()
{
  bool show = !actionShow_Clippings->isChecked();
  if(show && !app->loadClippingsDoc())
    return;
  if(show) {
    Dim parentw = std::min(clippingsPanel->parent()->node->bounds().width(), winBounds().width());
    if(clippingsPanel->node->bounds().width() > 0.75 * parentw)
      clippingsSplitter->setSplitSize(0.75*parentw);
  }
  actionShow_Clippings->setChecked(show);
  clippingsPanel->setVisible(show);
  clippingsSplitter->setVisible(show);
}

void MainWindow::toggleFullscreen()
{
  bool fs = !(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN);  // toggle
  if(fs)
    SDL_MaximizeWindow(sdlWindow);  // otherwise switches to some partial screen video mode!
  SDL_SetWindowFullscreen(sdlWindow, fs ? SDL_WINDOW_FULLSCREEN : 0);
  actionFullscreen->setChecked(fs);
#if PLATFORM_IOS
  selectFirst("#ios-statusbar-bg")->setVisible(!fs);
#endif
}

void MainWindow::toggleInvertColors()
{
  bool invert = !actionInvertColors->checked();
  actionInvertColors->setChecked(invert);
  ScribbleApp::cfg->set("invertColors", invert);
  redraw();
}

void MainWindow::togglePenToolbar()
{
  bool show = !penToolbarAutoAdj->isVisible();
  penToolbarAutoAdj->setVisible(show);
  if(show)
    app->updatePenToolbar();
  ScribbleApp::cfg->set("showPenToolbar", show);
}

void MainWindow::toggleDisableTouch()
{
  bool disable = !actionDisable_Touch->checked();
  actionDisable_Touch->setChecked(disable);
  ScribbleInput::disableTouch = disable;
}

void MainWindow::toggleSyncView()
{
  actionViewSync->setChecked(!actionViewSync->isChecked());
  app->syncView();
}

void MainWindow::toggleSyncViewMaster()
{
  actionViewSyncMaster->setChecked(!actionViewSyncMaster->isChecked());
  // if view sync is disabled and user becomes master, automatically enable
  if(actionViewSyncMaster->isChecked())
    actionViewSync->setChecked(true);
  app->syncView();
}

// creation of second ScribbleArea is deferred until split is first opened
void MainWindow::toggleSplitView(int newstate)
{
  newstate = newstate == SPLIT_TOGGLE ? -splitState : newstate;
  static const char* icons[] = {":/icons/ic_menu_split_tb.svg", ":/icons/ic_menu_split_bt.svg",
      ":/icons/ic_menu_split_lr.svg", ":/icons/ic_menu_split_rl.svg"};
  actionSplitView->setIcon(SvgGui::useFile(icons[std::abs(newstate) - SPLIT_H12]));
  if(newstate < 0 && (splitState < 0 || !app->closeSplit()))
    return;  // saving of modified doc in split was canceled

  if(app->scribbleAreas.size() < 2) {
    // create second ScribbleArea
    ScribbleArea* area = new ScribbleArea();
    app->scribbleAreas.push_back(area);
    ScribbleWidget* areaWidget = createScribbleAreaWidget(scribbleContainer2, area);
    areaWidget->focusIndicator = focusIndicator2;
  }

  if(splitState < 0)
    app->openSplit();

  Widget* layoutContainer = selectFirst("#scribble-split-layout");
  Rect layoutrect = layoutContainer->node->bounds();

  bool split = newstate > 0;
  bool horz = newstate == SPLIT_H12 || newstate == SPLIT_H21;
  if(split != (splitState > 0)) {
    actionSplitView->setChecked(split);
    scribbleFocusContainer2->setVisible(split);
    scribbleSplitter->setVisible(split);
  }
  if(split) {
    static const char* flexdirs[] = {"column", "column-reverse", "row", "row-reverse"};
    static Splitter::SizerPosition sizerpos[] = {Splitter::BOTTOM, Splitter::TOP, Splitter::RIGHT, Splitter::LEFT};
    layoutContainer->node->setAttribute("flex-direction", flexdirs[newstate - SPLIT_H12]);
    scribbleFocusContainer2->node->setAttribute("box-anchor", horz ? "hfill" : "vfill");
    scribbleSplitter->setDirection(sizerpos[newstate - SPLIT_H12]);  //horz ? Splitter::BOTTOM : Splitter::RIGHT);
    scribbleSplitter->setSplitSize(horz ? layoutrect.height()/2 : layoutrect.width()/2);
  }
  splitState = newstate;
  ScribbleApp::cfg->set("splitLayout", newstate);
}

void MainWindow::orientationChanged()
{
#if PLATFORM_IOS
  // the crux here is iPhone notch - we need a big offset in portrait but none in landscape
  // note that iPhone point sizes are roughly the same as our UI units, so no conversion needed
  Widget* statusBarBG = selectFirst("#ios-statusbar-bg");
  float top, bottom;
  // iosSafeAreaInsets will return 1 for iPhone, 0 for iPad (for which we assume our default insets)
  if(iosSafeAreaInsets(&top, &bottom)) {
    // non-notch iPhone status bar doesn't seem to be included in safe area inset; unfortunately, SDL
    //  only hides status bar if fullscreen is set, whereas default iOS behavior is to hide in phone landscape
    static_cast<SvgRect*>(statusBarBG->node)->setRect(Rect::wh(20, std::max(20.0f, top)));
    // this will only matter for initial call (before ScribbleAreas are created)
    scribbleAreaStatusInset.y = std::min(std::max(bottom, 6.0f), 18.0f);
  }
  statusBarBG->setVisible(!(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN));
#endif
}

/// Setup

// now only used for filename label
static TextBox* createTextLabel(const char* box_anchor, Dim top, Dim right, Dim bottom, Dim left)
{
  // color should be set with CSS
  static const char* textLabelSVG = R"(
    <g class="textlabel" layout="box">
      <rect box-anchor="hfill" fill="black" fill-opacity="0.65" width="20" height="24"/>
      <text font-size="14" margin="4 8"></text>
    </g>
  )";
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(textLabelSVG));

  TextBox* textLabel = new TextBox(proto->clone());
  textLabel->node->setAttribute("box-anchor", box_anchor);
  textLabel->setMargins(top, right, bottom, left);
  return textLabel;
}

// add labels to container for ScribbleArea
ScribbleWidget* MainWindow::createScribbleAreaWidget(Widget* container, ScribbleArea* area)
{
  ScribbleWidget* areaWidget = ScribbleWidget::create(container, area);
  Widget* contents = container->selectFirst(".scribble-content");
  areaWidget->node->addClass("scribbleArea");
  areaWidget->fileNameLabel = createTextLabel("top left", 6, 0, 0, 6);
  areaWidget->pageNumLabel = new TextBox(createTextNode(""));
  areaWidget->timeRangeLabel = new TextBox(createTextNode(""));
  areaWidget->zoomLabel = new TextBox(createTextNode(""));
  areaWidget->pageNumLabel->setMargins(0, 4);
  areaWidget->timeRangeLabel->setMargins(0, 4);
  areaWidget->zoomLabel->setMargins(0, 4);

  Button* zoomBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_zoom.svg"));
  zoomBtn->onClicked = SLOT(doCommand(ID_RESETZOOM));
  // press and drag on zoom label to zoom ... not sure if I'll keep this
  float initX = 0;
  Dim initZoom = 0;
  //Widget* zoomLabel = areaWidget->zoomLabel;  -- press-drag was previous on label instead of button
  zoomBtn->addHandler([initX, initZoom, area, zoomBtn](SvgGui* gui, SDL_Event* event) mutable {
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      initX = event->tfinger.x;   //gui->currInputPoint.x;
      initZoom = area->getZoom();
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == zoomBtn)
      area->zoomCenter(initZoom*pow(1.25, 0.05f*(event->tfinger.x - initX)), false);  //event->motion.x
    else if(event->type == SDL_FINGERUP || (event->type == SvgGui::OUTSIDE_PRESSED)) {
      area->zoomCenter(area->getZoom(), true);
    }
    return false;  // continue to button handler
  });
  setupTooltip(zoomBtn, _("Zoom"), Tooltips::LEFT | Tooltips::BOTTOM | Tooltips::ABOVE);

  areaWidget->prevPage = createToolbutton(SvgGui::useFile(":/icons/ic_menu_prev.svg"), _("Previous Page"));
  areaWidget->prevPage->onClicked = SLOT(doCommand(ID_PREVPAGE));
  areaWidget->nextPage = createToolbutton(SvgGui::useFile(":/icons/ic_menu_next.svg"), _("Next Page"));
  areaWidget->nextPage->onClicked = SLOT(doCommand(ID_NEXTPAGENEW));
  setupTooltip(areaWidget->prevPage, _("Previous Page"), Tooltips::LEFT | Tooltips::BOTTOM | Tooltips::ABOVE);
  setupTooltip(areaWidget->nextPage, _("Next Page"), Tooltips::LEFT | Tooltips::BOTTOM | Tooltips::ABOVE);

  Button* timeRangeBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_clock.svg"));
  timeRangeBtn->onClicked = [this](){
    ScribbleApp::cfg->set("displayTimeRange",  // should do right click/long press, but I'm lazy
        (SDL_GetModState() & KMOD_SHIFT) ? 2 : !ScribbleApp::cfg->Int("displayTimeRange"));
    refreshUI(app->activeDoc(), UIState::Command);
  };
  setupTooltip(timeRangeBtn, _("Time Range"), Tooltips::LEFT | Tooltips::BOTTOM | Tooltips::ABOVE);

  // toolbar background opacity is set via CSS to 0.75 - w/ 0.5, disabled icons don't show up w/ white page
  Toolbar* statusbar = createToolbar();
  statusbar->node->addClass("statusbar");
  statusbar->node->setAttribute("box-anchor", "bottom left");
  statusbar->setMargins(0, 0, scribbleAreaStatusInset.y, scribbleAreaStatusInset.x);

  // hack to make toolbar smaller - alternative would be to set toolbar dimensions in toolbar widget SVG
  //  instead of toolbutton SVG, then provide a separate mini-toolbar widget (or size arg for createToolbar())
  statusbar->node->setTransform(Transform2D().scale(0.5));
  //statusbar->node->setAttr<float>("font-size", 18);  -- set by CSS

  statusbar->addWidget(areaWidget->prevPage);
  statusbar->addWidget(areaWidget->pageNumLabel);
  statusbar->addWidget(areaWidget->nextPage);
  statusbar->addSeparator();
  statusbar->addWidget(zoomBtn);
  statusbar->addWidget(areaWidget->zoomLabel);
  statusbar->addSeparator();
  statusbar->addWidget(timeRangeBtn);
  statusbar->addWidget(areaWidget->timeRangeLabel);
  contents->addWidget(statusbar);

  contents->addWidget(areaWidget->fileNameLabel);
  areaWidget->fileNameLabel->setVisible(false);  // only shown for multi-doc split

  // we want container, not ScribbleWidget to be focusable so that pressing scroll handle switches focus too
  contents->isFocusable = true;
  contents->addHandler([this, area](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::FOCUS_GAINED)
      app->setActiveArea(area);
    // For pinch zoom, is CTRL press+release sent for every step?  Investigate before enabling this
    // Also, for this to work, have area take focus when ctrl + scroll zoom begins
    //if(event->key.keysym.sym == SDLK_RCTRL || event->key.keysym.sym == SDLK_LCTRL)
    //  area->zoomCenter(area->getZoom(), true);
    return false;  // this is kind of a hack, so pretend we don't exist
  });
  return areaWidget;
}

// see Qt version of this method for theme colors
void MainWindow::setupTheme()
{
  appIcon = SvgGui::useFile(":/icons/write_icon_flat.svg");
  orientationChanged();
}

static Tooltips tooltipsInst;

void MainWindow::setupUI(ScribbleApp* a)
{
  app = a;
  // too lazy to do translations for tooltips (also, probably should use keys instead of English)
  if(!app->hasI18n)
    Tooltips::inst = &tooltipsInst;

  setupActions();

  // this is set so focus is not returned to pen toolbar edit boxes if lost
  isFocusable = true;

  // set initial icons for tool buttons
  modeToAction(ScribbleMode::getModeType(app->scribbleMode->eraserMode))->setIcon(
        modeToAction(app->scribbleMode->eraserMode)->icon());
  modeToAction(ScribbleMode::getModeType(app->scribbleMode->selectMode))->setIcon(
        modeToAction(app->scribbleMode->selectMode)->icon());
  modeToAction(ScribbleMode::getModeType(app->scribbleMode->insSpaceMode))->setIcon(
        modeToAction(app->scribbleMode->insSpaceMode)->icon());

  // create and populate toolbars
  createToolBars();

  // if split is prevented from reaching min size set for spliiter, behavior will be incorrect on next drag
  bookmarkSplitter = new Splitter(containerNode()->selectFirst("#bookmark-splitter"),
      containerNode()->selectFirst("#bookmark-split-sizer"), Splitter::LEFT, 120);
  bookmarkPanel = selectFirst("#bookmark-panel");
  //bookmarkPanel->selectFirst(".panel-title")->setText(_("Bookmarks"));

  ComboBox* combobkmk = createComboBox({_("Bookmarks"), _("Margin Content")});
  combobkmk->isFocusable = false;
  combobkmk->setIndex(ScribbleApp::cfg->Int("bookmarkMode") == BookmarkView::MARGIN_CONTENT ? 1 : 0);
  combobkmk->onChanged = [this, combobkmk](const char* s){
    int mode = combobkmk->index() == 1 ? BookmarkView::MARGIN_CONTENT : BookmarkView::BOOKMARKS;
    ScribbleApp::cfg->set("bookmarkMode", mode);
    app->repaintBookmarks();
  };
  // don't put combo box on toolbar to allow narrower bookmark panel width (with toolbar covering combo box)
  selectFirst("#bookmarks-combo-container")->addWidget(combobkmk);

  Toolbar* bookmarkstb = createToolbar();
  bookmarkstb->addAction(actionAdd_Bookmark);
  bookmarkstb->addAction(actionBookmarksPin);
  bookmarkstb->addAction(actionBookmarksClose);
  selectFirst("#bookmarks-toolbar-container")->addWidget(bookmarkstb);

  Widget* scribbleContainer = selectFirst("#scribble-container");
  ScribbleWidget* areaWidget = createScribbleAreaWidget(scribbleContainer, app->activeArea());
  areaWidget->focusIndicator = selectFirst("#scribble-focus");

  // container and splitter for split - initially hidden
  scribbleContainer2 = selectFirst("#scribble-container-2");
  scribbleFocusContainer2 = selectFirst("#scribble-focus-container-2");
  // create splitter widget now so that layout doesn't create default Widget - behavior which should be fixed
  scribbleSplitter = new Splitter(containerNode()->selectFirst("#scribble-splitter"),
      containerNode()->selectFirst("#scribble-split-sizer"), Splitter::BOTTOM, 120);
  focusIndicator2 = selectFirst("#scribble-focus-2");

  ScribbleWidget* bkmkWidget = ScribbleWidget::create(selectFirst("#bookmark-container"), app->bookmarkArea);
  bkmkWidget->zoomLabel = createTextLabel("bottom left", 0, 0, 6, 6);
  selectFirst("#bookmark-container")->addWidget(bkmkWidget->zoomLabel);

  clippingsSplitter = new Splitter(containerNode()->selectFirst("#clippings-splitter"),
      containerNode()->selectFirst("#clippings-split-sizer"), Splitter::LEFT, 120);
  clippingsPanel = selectFirst("#clippings-panel");
  clippingsPanel->selectFirst(".panel-title")->setText(_("Clippings"));

  Widget* clippingsContainer = selectFirst("#clippings-container");
  ScribbleWidget* clippingWidget = ScribbleWidget::create(clippingsContainer, app->clippingArea);
  clippingWidget->node->addClass("clippingView");  // needed for ClippingView drops
  //clippingsContainer->addWidget(app->clippingArea->pageMenu);
  Widget* clipdel = new Widget(loadSVGFragment(clippingsDelSVG));
  clipdel->node->setAttribute("box-anchor", "bottom left");
  clipdel->setMargins(0, 0, 6, 6);
  clipdel->setLayoutIsolate(true);  // only visible when dragging clipping
  clipdel->setVisible(false);
  clippingsContainer->selectFirst(".scribble-content")->addWidget(clipdel);
  app->clippingArea->delTarget = clipdel;

  Toolbar* clippingstb = createToolbar();
  clippingstb->addAction(actionClippingsUndo);
  clippingstb->addAction(actionClippingsPin);
  clippingstb->addAction(actionClippingsClose);
  //clippingsTbContainer = selectFirst("#clippings-toolbar-container");
  selectFirst("#clippings-toolbar-container")->addWidget(clippingstb);

  // overlay for rendering on top of everything else
  Widget* subWinLayout = selectFirst(".sub-window-layout");
  overlayWidget = new OverlayWidget(subWinLayout);
  overlayWidget->node->addClass("overlayWidget");
  selectFirst("#main-container")->addWidget(overlayWidget);

  // popup selection toolbar
  Menubar* selToolbar = createMenubar();  // Menubar used instead of Toolbar so that popup closes after use
  selToolbar->addAction(actionCut); // TouchBar::HideText is default
  selToolbar->addAction(actionCopy);
  selToolbar->addAction(actionDupSel);
  selToolbar->addAction(actionDelete_Selection);
  selToolbar->addAction(actionCreate_Link);
  //selPopup = ::createMenu(Menu::VERT_RIGHT, false); -- requires event->user.data2 = 0 hack for OUTSIDE_MODAL
  selPopup = new AbsPosWidget(loadSVGFragment(
       "<g class='menu' position='absolute' box-anchor='fill' layout='box'></g>"));
  selPopup->addWidget(selToolbar);
  selPopup->setVisible(false);

  selPopup->addHandler([](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::OUTSIDE_PRESSED) {
      gui->closeMenus();
      return true;
    }
    if(event->type == SvgGui::OUTSIDE_MODAL) {
      gui->closeMenus();
      return false;
    }
    // don't let repeat key events close popup (happens with Ctrl key held down for sel mode on Windows)
    if(event->type == SDL_KEYDOWN && !event->key.repeat) {
      gui->closeMenus();
      if(event->key.keysym.sym == SDLK_ESCAPE)  // only swallow Esc key
        return true;
    }
    return false;
  });

  selectFirst("#main-container")->addWidget(selPopup);
  // set up one-time help popups
  setupHelpTips();
}

Action* MainWindow::findAction(const char* name)
{
  if(name && name[0]) {
    for(size_t ii = 0; ii < actionList.size(); ii++) {
      if(actionList.at(ii)->name == name)
        return actionList.at(ii);
    }
  }
  return NULL;
}

void MainWindow::createToolBars()
{
  // previously, we supported multiple toolbars, but for now, we only use the first
  auto tbscfg = splitStr<std::vector>(ScribbleApp::cfg->String("toolBars2"), ';', true);
  auto tbcfg = splitStr<std::vector>(tbscfg[0].c_str(), ',', true);

  Toolbar* tb = vertToolbar ? createVertToolbar() : createToolbar();
  Widget* stretch = NULL, *eraseBtn = NULL, *selectBtn = NULL, *insSpaceBtn = NULL, *toolsBtn = NULL;
  Menubar* toolsToolbar = NULL;
  auto addTBWidget = [this](Widget* w, int priority) {
    w->node->setAttr<int>("ui-priority", priority);
    tbWidgets.push_back(w);
  };
  for(size_t jj = 0; jj < tbcfg.size(); ++jj) {
    if(tbcfg[jj] == "separator") {
      addTBWidget(tb->addSeparator(), -100);
    }
    else if(tbcfg[jj] == "stretch") {
      stretch = createStretch();
      tb->addWidget(stretch);  // not included in tbWidgets
    }
    else if(tbcfg[jj] == "docTitle") {
      tb->addWidget(titleButton);
      addTBWidget(titleButton, 2);
    }
    else if(tbcfg[jj] == "undoRedoBtn") {
      tb->addWidget(undoRedoBtn);
      addTBWidget(undoRedoBtn, 2);
    }
    else if(tbcfg[jj] == "seltools") {
      if(!ScribbleApp::cfg->Bool("popupToolbar")) {
        addTBWidget(tb->addAction(actionCut), actionCut->priority);
        addTBWidget(tb->addAction(actionCopy), actionCopy->priority);
      }
      addTBWidget(tb->addAction(actionPaste), actionPaste->priority);
    }
    else if(tbcfg[jj] == "tools") {
#ifdef SCRIBBLE_IAP
      if(!iosIsPaid()) {
        const char* iapButtonSVG = R"(<g class="toolbutton" layout="box">
          <rect class="background" box-anchor="hfill" width="36" height="42"/>
          <text class="title" style="fill: #1F9FFF" margin="8 8">XXX</text>
        </g>)";

        iapButton = new Button(loadSVGFragment(iapButtonSVG));
        iapButton->setText(_("Upgrade Write"));  // for i18n
        iapButton->onClicked = [](){ iosRequestIAP(); };  //app->openURL("https://apps.apple.com/us/app/stylus-labs-write/id1498369428"); };
        tb->addWidget(iapButton);
        addTBWidget(iapButton, Action::NormalPriority - 10);
        addTBWidget(tb->addSeparator(), -100);
      }
#endif
      // if touch input disabled, show pan tool (add directly main toolbar for now)
      if(ScribbleApp::cfg->Int("singleTouchMode") == 0 && ScribbleApp::cfg->Int("multiTouchMode") == 0)
        addTBWidget(tb->addAction(actionPan), actionPan->priority);
      // draw, erase, select, insert space as a menu bar
      toolsToolbar = vertToolbar ? createVertMenubar() : createMenubar();
      toolsToolbar->autoClose = true;  //ScribbleApp::cfg->Bool("pressOpenMenus");
      toolsToolbar->addAction(actionDraw);
      eraseBtn = toolsToolbar->addAction(actionErase);
      selectBtn = toolsToolbar->addAction(actionSelect);
      insSpaceBtn = toolsToolbar->addAction(actionInsert_Space);
      toolsToolbar->node->setAttribute("box-anchor", "");  // no stretching for this subtoolbar!
      toolsBtn = toolsToolbar->addAction(actionTools_Menu);  // for size adjustment
      toolsBtn->setVisible(false);
      tb->addWidget(toolsToolbar);
      addTBWidget(toolsToolbar, 1);
    }
    else {
      Action* action = findAction(tbcfg[jj].c_str());
      if(action)
        addTBWidget(tb->addAction(action), action->priority);
    }
  }

  // container to hide/show toolbar items depending on width, using adjFn
  AutoAdjContainer* adjtb = new AutoAdjContainer(new SvgG(), tb);
  adjtb->node->setAttribute("box-anchor", vertToolbar ? "vfill" : "hfill");
  adjtb->node->addClass("main-toolbar-autoadj");

  // although hiding of certain buttons basically breaks application (e.g. doc title, undo), we won't worry
  //  about putting on overflow menu as user may be using split screen and can adjust split width to recover
  // The motivation behind auto-adjust is mostly to automatically accommodate various screen sizes, not to
  //  support user resizing window
  // Tools menu logic is pretty ugly, but can't think of a better way to get the desired behavior
  std::stable_sort(tbWidgets.begin(), tbWidgets.end(), [](Widget* a, Widget* b){
    return a->node->getIntAttr("ui-priority", 0) < b->node->getIntAttr("ui-priority", 0);
  });

  size_t nextAdjIdx = 0;
  adjtb->adjFn = [=](const Rect& src, const Rect& dest) mutable {
    if(dest.width() <= src.width() + 0.5 && stretch->node->bounds().width() >= 1)
      return;
    // reset
    nextAdjIdx = 0;
    titleButton->setShowTitle(!vertToolbar);
    titleButton->setText(titleStr.c_str());
    for(Widget* w : tbWidgets)
      w->setVisible(true);
    if(toolsToolbar) {
      toolsBtn->setVisible(false);
      eraseBtn->setVisible(true);
      selectBtn->setVisible(true);
      insSpaceBtn->setVisible(true);
    }
    adjtb->repeatLayout(dest);
    while(stretch->node->bounds().width() < 1) {
      if(titleButton->selectFirst(".title")->isVisible()) {
        titleButton->setShowTitle(false);
        adjtb->repeatLayout(dest);
        Dim w = stretch->node->bounds().width();
        if(w > 1) {
          SvgText* textnode = static_cast<SvgText*>(titleButton->containerNode()->selectFirst("text"));
          if(w > 12 && textnode) {
            titleButton->setShowTitle(true);
            SvgPainter::elideText(textnode, w - 4);
            adjtb->repeatLayout(dest);
          }
          return;
        }
      }
      else if(nextAdjIdx < tbWidgets.size()) {
        if(tbWidgets[nextAdjIdx] == toolsToolbar) {
          toolsBtn->setVisible(true);
          eraseBtn->setVisible(false);
          selectBtn->setVisible(false);
          insSpaceBtn->setVisible(false);
        }
        else
          tbWidgets[nextAdjIdx]->setVisible(false);
        ++nextAdjIdx;
      }
      else
        return;  // nothing else we can do
      adjtb->repeatLayout(dest);
    }
  };

  selectFirst("#main-toolbar-container")->addWidget(adjtb);

  penToolbarAutoAdj = createPenToolbarAutoAdj();
  penToolbarAutoAdj->setVisible(ScribbleApp::cfg->Bool("showPenToolbar"));
  selectFirst("#pen-toolbar-container")->addWidget(penToolbarAutoAdj);
}

void MainWindow::setupActions()
{
  ScribbleConfig* cfg = app->cfg;
  setupTheme();

  // simple command actions
  // an alternative for connecting multiple signals to one slot is the QSignalMapper approach
  action_Previous_Page = createAction("action_Previous_Page",
      "&Previous Page", ":/icons/ic_menu_prev.svg", "Left", SLOT(doCommand(ID_PREVPAGE)));
  action_Next_Page = createAction("action_Next_Page",
      "&Next Page", ":/icons/ic_menu_next.svg", "Ctrl+Right", SLOT(doCommand(ID_NEXTPAGENEW)));

  // keyboard only actions
  createAction("action_Page_Down", "Next Page", "", "Right", SLOT(doCommand(ID_NEXTPAGE)));
  createAction("action_Prev_Screen", "Previous Screen", "", "PageUp", SLOT(doCommand(ID_PREVSCREEN)));
  createAction("action_Next_Screen", "Next Screen", "", "PageDown", SLOT(doCommand(ID_NEXTSCREEN)));
  createAction("action_Scroll_Up", "Scroll Up", "", "Up", SLOT(doCommand(ID_SCROLLUP)));
  createAction("action_Scroll_Down", "Scroll Down", "", "Down", SLOT(doCommand(ID_SCROLLDOWN)));
  // should we just use Home/End?
  createAction("action_JumpBeginning", "Beginning of Document", "", "Ctrl+Home", SLOT(doCommand(ID_STARTOFDOC)));
  createAction("action_JumpEnd", "End of Document", "", "Ctrl+End", SLOT(doCommand(ID_ENDOFDOC)));

  actionZoom_In = createAction("actionZoom_In", "Zoom &In", "", "Ctrl+=", SLOT(doCommand(ID_ZOOMIN)));
  actionZoom_Out = createAction("actionZoom_Out", "Zoom &Out", "", "Ctrl+-", SLOT(doCommand(ID_ZOOMOUT)));
  actionReset_Zoom = createAction("actionReset_Zoom", "&Reset Zoom", "", "Ctrl+0", SLOT(doCommand(ID_RESETZOOM)));
  //actionZoom_All = createAction("actionZoom_All", "Zoom All", "", "", SLOT(doCommand(ID_ZOOMALL)));
  //actionZoom_Width = createAction("actionZoom_Width", "Zoom Width", "", "", SLOT(doCommand(ID_ZOOMWIDTH)));

  actionPrevious_View = createAction("actionPrevious_View",
      "&Previous View", ":/icons/ic_menu_back.svg", "Backspace", SLOT(doCommand(ID_PREVVIEW)));
  actionNext_View = createAction("actionNext_View",
      "&Next View", ":/icons/ic_menu_forward.svg", "Shift+Backspace", SLOT(doCommand(ID_NEXTVIEW)));

  // note that any checkable menu icon should have an icon to suppress Qt's (non-dpi-indep.) checkmark
  actionFullscreen = createAction("actionFullscreen",
      "&Fullscreen", ":/icons/ic_menu_fullscreen.svg", "F11", [this](){ toggleFullscreen(); });
  actionFullscreen->setCheckable(true);

  // setup split view menu
  actionSplitView = createAction("actionSplitView",
      "&Split View", ":/icons/ic_menu_split_tb.svg", "Ctrl+T", [this](){ toggleSplitView(SPLIT_TOGGLE); });
  actionSplitView->setCheckable(true);
  actionSplitView->setPriority(Action::NormalPriority - 2);  // below bookmarks and clippings
  splitState = -std::min(std::max(1, std::abs(ScribbleApp::cfg->Int("splitLayout"))), int(SPLIT_V21));
  toggleSplitView(splitState);  // set icon for actionSplitView

  Action* actionSplitH12 = createAction("actionSplitH12", "Split T/B", ":/icons/ic_menu_split_tb.svg", "",
      [this](){ toggleSplitView(SPLIT_H12); });
  Action* actionSplitH21 = createAction("actionSplitH21", "Split B/T", ":/icons/ic_menu_split_bt.svg", "",
      [this](){ toggleSplitView(SPLIT_H21); });
  Action* actionSplitV12 = createAction("actionSplitV12", "Split L/R", ":/icons/ic_menu_split_lr.svg", "",
      [this](){ toggleSplitView(SPLIT_V12); });
  Action* actionSplitV21 = createAction("actionSplitV21", "Split R/L", ":/icons/ic_menu_split_rl.svg", "",
      [this](){ toggleSplitView(SPLIT_V21); });
  actionSplitView->addMenuAction(actionSplitH12);
  actionSplitView->addMenuAction(actionSplitH21);
  actionSplitView->addMenuAction(actionSplitV12);
  actionSplitView->addMenuAction(actionSplitV21);

  actionInvertColors = createAction("actionInvertColors",
      "Invert Colors", "", "", [this](){ toggleInvertColors(); });
  actionInvertColors->setCheckable(true);
  actionInvertColors->setChecked(ScribbleApp::cfg->Bool("invertColors"));

  actionNew_Page_Before = createAction("actionNew_Page_Before",
      "New Page &Before", "", "Ctrl+Shift+Left", SLOT(doCommand(ID_PAGEBEFORE)));
  actionNew_Page_After = createAction("actionNew_Page_After",
      "New Page &After", "", "Ctrl+Shift+Right", SLOT(doCommand(ID_PAGEAFTER)));
  actionExpand_Down = createAction("actionExpand_Down",
      "Expand Down", ":/icons/ic_menu_expanddown.svg", "", SLOT(doCommand(ID_EXPANDDOWN)));
  actionExpand_Right = createAction("actionExpand_Right",
      "Expand Right", ":/icons/ic_menu_expandright.svg", "", SLOT(doCommand(ID_EXPANDRIGHT)));
  actionSelect_Pages = createAction("actionSelect_Pages", "Select Pages", ":/icons/ic_menu_pagesel.svg", "Ctrl+Shift+P",
      SLOT(setMode(app->scribbleMode->getMode() == MODE_PAGESEL ? MODE_STROKE : MODE_PAGESEL);));

  actionUndo = createAction("actionUndo", "&Undo", ":/icons/ic_menu_undo.svg", "Ctrl+Z", SLOT(doCommand(ID_UNDO)));
  actionRedo = createAction("actionRedo", "&Redo", ":/icons/ic_menu_redo.svg", "Ctrl+Y", SLOT(doCommand(ID_REDO)));

  actionCut = createAction("actionCut", "Cu&t", ":/icons/ic_menu_cut.svg", "Ctrl+X", SLOT(doCommand(ID_CUTSEL)));
  actionCopy = createAction("actionCopy", "&Copy", ":/icons/ic_menu_copy.svg", "Ctrl+C", SLOT(doCommand(ID_COPYSEL)));
  actionPaste = createAction("actionPaste", "&Paste", ":/icons/ic_menu_paste.svg", "Ctrl+V", SLOT(pasteClipboard()));  //doCommand(ID_PASTE)));
  actionDupSel = createAction("actionDupSel", "Duplicate", ":/icons/ic_menu_duplicate.svg", "Ctrl+D", SLOT(doCommand(ID_DUPSEL)));

  actionSelect_All = createAction("actionSelect_All", "Select &All", "", "Ctrl+A", SLOT(doCommand(ID_SELALL)));
  actionSelect_Similar = createAction("actionSelect_Similar", "Select Similar", "", "", SLOT(doCommand(ID_SELSIMILAR)));
  actionInvert_Selection = createAction("actionInvert_Selection", "Invert", "", "", SLOT(doCommand(ID_INVSEL)));
  actionDelete_Selection = createAction("actionDelete_Selection",
       "Delete", ":/icons/ic_menu_discard.svg", "Delete", SLOT(doCommand(ID_DELSEL)));
  actionCreate_Link = createAction("actionCreate_Link", "Create Link...", ":/icons/ic_menu_link.svg", "Ctrl+L", SLOT(createLink()));
  // icon: something like https://www.brandeps.com/icon/U/Ungroup-01 but with a solid instead of dashed line
  actionUngroup = createAction("actionUngroup", "Ungroup", "", "", SLOT(doCommand(ID_UNGROUP)));

  // more
  actionNew_Document = createAction("actionNew_Document",
      "&New", ":/icons/ic_menu_document.svg", "Ctrl+N", SLOT(newDocument()));
  action_Open = createAction("action_Open", "&Open...", ":/icons/ic_menu_folder.svg", "Ctrl+O", SLOT(openDocument()));
  actionSave = createAction("actionSave", "&Save", ":/icons/ic_menu_save.svg", "Ctrl+S", SLOT(saveDocument()));
  actionSave->setPriority(Action::NormalPriority - 2);
  actionSave_As = createAction("actionSave_As", "Save &As...", "", "Ctrl+Shift+S", SLOT(doSaveAs()));
  //actionSave_Single_File = createAction("actionSave_Single_File", "Save As Single File...", "", "", SLOT(saveSingleFile()));
  actionRevert = createAction("actionRevert", "Discard Changes...", ":/icons/ic_menu_no_save.svg", "Ctrl+W", SLOT(revert()));

  // changed from "Show Bookmarks" to "Bookmarks" because it was the longest menu text - if it overflows to
  //  menu (text isn't shown otherwise), that means we're on a narrow screen and need to conserve horz. space!
  actionShow_Bookmarks = createAction("actionShow_Bookmarks",
      "Bookmarks", ":/icons/ic_menu_bookmark.svg", "Ctrl+B", [this](){ toggleBookmarks(); });
  actionShow_Bookmarks->setCheckable(true);
  // demote slightly so next page/prev page have higher priority
  actionShow_Bookmarks->setPriority(Action::NormalPriority - 1);
  actionPage_Setup = createAction("actionPage_Setup",
      "Page Setup...", ":/icons/ic_menu_document.svg", "", SLOT(showPageSetup()));
  actionInsert_Image = createAction("actionInsert_Image",
      "Insert Image...", ":/icons/ic_menu_add_pic.svg", "", SLOT(insertImage()));
  // insert pages from another document
  actionInsertDocument = createAction("actionInsertDocument", "Insert Document...", "", "", SLOT(insertDocument()));
  actionShow_Clippings = createAction("actionShow_Clippings",
      "Clippings", ":/icons/ic_menu_drawer.svg", "Ctrl+H", [this](){ toggleClippings(); });
  actionShow_Clippings->setCheckable(true);
  actionShow_Clippings->setPriority(Action::NormalPriority - 1);

  // undo button for clippings
  actionClippingsUndo = createAction("actionClippingsUndo",
      "Undo", ":/icons/ic_menu_undo.svg", "", SLOT(doCommand(CLIPPING_CMD | ID_UNDO)));
  actionClippingsClose = createAction("actionClippingsClose",
      "Close", ":/icons/ic_menu_cancel.svg", "", [this](){ toggleClippings(); });
  auto tgClipPin = [this](){
    bool ah = !app->cfg->Bool("autoHideClippings");
    app->cfg->set("autoHideClippings", ah);
    actionClippingsPin->setChecked(!ah);
  };
  actionClippingsPin = createAction("actionClippingsPin", "Keep Open", ":/icons/ic_menu_pin.svg", "", tgClipPin);
  actionClippingsPin->setChecked(!cfg->Bool("autoHideClippings"));

  actionBookmarksClose = createAction("actionBookmarksClose",
      "Close", ":/icons/ic_menu_cancel.svg", "", [this](){ toggleBookmarks(); });
  auto tgBkmkPin = [this](){
    bool ah = !app->cfg->Bool("autoHideBookmarks");
    app->cfg->set("autoHideBookmarks", ah);
    actionBookmarksPin->setChecked(!ah);
  };
  actionBookmarksPin = createAction("actionBookmarksPin", "Keep Open", ":/icons/ic_menu_pin.svg", "", tgBkmkPin);
  actionBookmarksPin->setChecked(!cfg->Bool("autoHideBookmarks"));

  actionExport_PDF = createAction("actionExport_PDF", "Export PDF...", "", "", SLOT(exportPDF()));
  // don't use direct connection for prefs because it might destroy the toolbars (which might contain menu
  //  from which it was launched)
  actionPreferences = createAction("actionPreferences",
      "Preferences...", ":/icons/ic_menu_settings.svg", "Ctrl+P", SLOT(openPreferences()));  //ic_menu_preferences

  //actionViewHelp = createAction("actionViewHelp", "Open Help Document", "", "F1", SLOT(openHelpDoc()));
  actionUpdateCheck = createAction("actionUpdateCheck", "Check for Update", "", "", SLOT(updateCheck()));
  action_About = createAction("action_About", "&About", "", "", SLOT(about()));
  actionExit = createAction("actionExit", "E&xit", "", "Ctrl+Q", SLOT(maybeQuit()));

  // disable touch - not available by default but can be added to toolbar
  actionDisable_Touch = createAction("actionDisable_Touch", "&Disable Touch",
      ":/icons/ic_menu_pan.svg", "", [this](){ toggleDisableTouch(); });
  actionDisable_Touch->setCheckable(true);

  // sync/SWB
  actionShare_Document = createAction("actionShare_Document",
      "Create Whiteboard...", ":/icons/ic_menu_add_people.svg", "", SLOT(shareDocument()));
  actionOpen_Shared_Doc = createAction("actionOpen_Shared_Doc",
      "Open Whiteboard...", ":/icons/ic_menu_people.svg", "", SLOT(openSharedDoc()));
  actionSendImmed = createAction("actionSendImmed", "Sync Edits", "", "", SLOT(syncSendImmed()));  // ":/icons/ic_menu_send_now.svg"
  actionSendImmed->setCheckable(true);
  actionSendImmed->setChecked(true);

  // shared whiteboard view sync actions
  //menuViewSync = createMenu("menuViewSync", "View Sync");
  actionViewSync = createAction("actionViewSync", "Sync View", "", "", [this](){ toggleSyncView(); });
  actionViewSync->setCheckable(true);
  //actionViewSync->setMenu(menuViewSync);
  actionViewSyncMaster = createAction("actionViewSyncMaster",
      "View Master", "", "", [this](){ toggleSyncViewMaster(); });
  actionViewSyncMaster->setCheckable(true);
  //menuViewSync->addAction(actionViewSyncMaster);
  actionSyncInfo = createAction("actionSyncInfo", "Whiteboard Info...", "", "", SLOT(showSyncInfo()));

  // email/share document
  actionSend_Page = createAction("actionSend_Page", "Send Page Image", "", "", SLOT(sendPageImage()));
  actionSend_HTML = createAction("actionSend_HTML", "Send Document", "", "", SLOT(sendDocument()));
  actionSend_PDF = createAction("actionSend_PDF", "Send PDF", "", "", SLOT(sendPDF()));

  // tool menus and actions
  auto tbMenuAlign = vertToolbar ? Menu::HORZ : Menu::VERT_RIGHT;  // open to right even if more space to left
  menuDraw = createMenu("menuDraw", "Draw", tbMenuAlign);
  menuErase = createMenu("menuErase", "Erase", tbMenuAlign);
  menuSelect = createMenu("menuSelect", "Select", tbMenuAlign);
  menuInsert_Space = createMenu("menuInsert_Space", "Insert Space", tbMenuAlign);

  actionPan = createAction("actionPan", "&Pan", ":/icons/ic_menu_pan.svg", "", SLOT(setMode(MODE_PAN)));
  actionPan->setCheckable(true);

  actionDraw = createAction("actionDraw", "Draw", ":/icons/ic_menu_draw.svg", "`", SLOT(setMode(MODE_STROKE)));
  actionDraw->setCheckable(true);
  actionDraw->setMenu(menuDraw);
  // draw menu items; 1-8 select saved pens; 9 for pen toolbar, 0 for bookmark
  actionCustom_Pen = createAction("actionCustom_Pen",
      "Pen Setup...", ":/icons/ic_menu_set_pen.svg", "9", [this](){ togglePenToolbar(); });
  actionCustom_Pen->tooltip = _("Customize and save pens");
  actionAdd_Bookmark = createAction("actionAdd_Bookmark",
      "Add Bookmark", ":/icons/ic_menu_add_bookmark.svg", "0", SLOT(setMode(MODE_BOOKMARK)));
  actionAdd_Bookmark->setCheckable(true);
  actionAdd_Bookmark->tooltip = _("Drop beside text to show in bookmark pane");

  actionErase = createAction("actionErase", "Erase", "", "", SLOT(setMode(MODE_ERASE)));
  actionErase->setCheckable(true);
  actionErase->setMenu(menuErase);
  // erase menu items
  actionStroke_Eraser = createAction("actionStroke_Eraser",
      "Stroke Eraser", ":/icons/ic_menu_erase.svg", "", SLOT(setMode(MODE_ERASESTROKE)));
  actionStroke_Eraser->setCheckable(true);
  actionStroke_Eraser->tooltip = _("Erase whole strokes");
  actionRuled_Eraser = createAction("actionRuled_Eraser",
      "Ruled Eraser", ":/icons/ic_menu_erase_ruled.svg", "", SLOT(setMode(MODE_ERASERULED)));
  actionRuled_Eraser->setCheckable(true);
  actionRuled_Eraser->tooltip = _("Erase handwritten text\nUse in margin to erase whole lines");
  actionFree_Eraser = createAction("actionFree_Eraser",
      "Free Eraser", ":/icons/ic_menu_erase_free.svg", "", SLOT(setMode(MODE_ERASEFREE)));
  actionFree_Eraser->setCheckable(true);
  actionFree_Eraser->tooltip = _("Erase parts of strokes");

  actionSelect = createAction("actionSelect", "Select", "", "", SLOT(setMode(MODE_SELECT)));
  actionSelect->setCheckable(true);
  actionSelect->setMenu(menuSelect);
  // select menu items
  actionRect_Select = createAction("actionRect_Select",
      "Rect Select", ":/icons/ic_menu_select.svg", "", SLOT(setMode(MODE_SELECTRECT)));
  actionRect_Select->setCheckable(true);
  actionRect_Select->tooltip = _("Select within rectangle");
  actionRuled_Select = createAction("actionRuled_Select",
      "Ruled Select", ":/icons/ic_menu_select_ruled.svg", "", SLOT(setMode(MODE_SELECTRULED)));
  actionRuled_Select->setCheckable(true);
  actionRuled_Select->tooltip = _("Select handwritten text\nUse in margin to select whole lines");
  actionLasso_Select = createAction("actionLasso_Select",
      "Lasso Select", ":/icons/ic_menu_select_lasso.svg", "", SLOT(setMode(MODE_SELECTLASSO)));
  actionLasso_Select->setCheckable(true);
  actionLasso_Select->tooltip = _("Select within path");
  actionPath_Select = createAction("actionPath_Select",
      "Path Select", ":/icons/ic_menu_select_path.svg", "", SLOT(setMode(MODE_SELECTPATH)));
  actionPath_Select->setCheckable(true);
  actionPath_Select->tooltip = _("Select along path");

  actionInsert_Space = createAction("actionInsert_Space", "Insert Space", "", "", SLOT(setMode(MODE_INSSPACE)));
  actionInsert_Space->setCheckable(true);
  actionInsert_Space->setMenu(menuInsert_Space);
  // insert space menu items
  actionInsert_Space_Vert = createAction("actionInsert_Space_Vert",
      "Insert Space", ":/icons/ic_menu_insert_space.svg", "", SLOT(setMode(MODE_INSSPACEVERT)));
  actionInsert_Space_Vert->setCheckable(true);
  actionInsert_Space_Vert->tooltip = _("Insert vertical space");
  actionRuled_Insert_Space = createAction("actionRuled_Insert_Space",
      "Ruled Insert Space", ":/icons/ic_menu_insert_space_ruled.svg", "", SLOT(setMode(MODE_INSSPACERULED)));
  actionRuled_Insert_Space->setCheckable(true);
  actionRuled_Insert_Space->tooltip = _("Insert whole lines\nReflow handwritten text");

  // populate tool menus
  // add custom pens to draw menu
  for(size_t ii = 0; ii < cfg->pens.size(); ii++) {
    PenPreview* penpreview = new PenPreview;  //((int)ii);
    Button* previewbtn = createMenuItem(penpreview);
    penpreview->node->setAttribute("box-anchor", "fill");
    penpreview->setMargins(1, 5);
    previewbtn->onClicked = SLOT(penSelected((int)ii));
    penpreview->node->addClass("pen-preview");
    penPreviews.push_back(previewbtn);
    setupTooltip(previewbtn, "");
    menuDraw->addItem(previewbtn);
  }
  menuDraw->addAction(actionCustom_Pen);
  menuDraw->addAction(actionAdd_Bookmark);
  menuErase->addAction(actionStroke_Eraser);
  menuErase->addAction(actionRuled_Eraser);
  menuErase->addAction(actionFree_Eraser);
  menuSelect->addAction(actionRect_Select);
  menuSelect->addAction(actionRuled_Select);
  menuSelect->addAction(actionLasso_Select);
  menuSelect->addAction(actionPath_Select);
  menuInsert_Space->addAction(actionInsert_Space_Vert);
  menuInsert_Space->addAction(actionRuled_Insert_Space);
  // tool menus have priority over other toolbar items (except overflow menu); pan should hide before tools
  actionPan->setPriority(Action::NormalPriority + 1);
  actionDraw->setPriority(Action::NormalPriority + 2);
  actionErase->setPriority(Action::NormalPriority + 2);
  actionSelect->setPriority(Action::NormalPriority + 2);
  actionInsert_Space->setPriority(Action::NormalPriority + 2);
  // paste is more important than copy, cut, etc. assuming popup sel menu is enabled
  actionPaste->setPriority(Action::NormalPriority + 1);

  // combined tools menu (option for small screen sizes)
  Menu* toolsmenu = createMenu("toolsMenu", "Tools", tbMenuAlign);
  toolsmenu->addAction(actionStroke_Eraser);
  toolsmenu->addAction(actionRuled_Eraser);
  toolsmenu->addAction(actionFree_Eraser);
  toolsmenu->addSeparator();
  toolsmenu->addAction(actionRect_Select);
  toolsmenu->addAction(actionRuled_Select);
  toolsmenu->addAction(actionLasso_Select);
  toolsmenu->addAction(actionPath_Select);
  toolsmenu->addSeparator();
  toolsmenu->addAction(actionInsert_Space_Vert);
  toolsmenu->addAction(actionRuled_Insert_Space);
  // action for menu
  actionTools_Menu = createAction("actionTools_Menu", "Tools Menu", "", "", [this](){app->setMode(toolsMenuMode);});
  // need an initial icon for tools menu to be shown by toolbar editor
  actionTools_Menu->setIcon(actionStroke_Eraser->icon());
  actionTools_Menu->setMenu(toolsmenu);
  toolsMenuMode = MODE_ERASESTROKE;
  // actions menu needed even with touch UI for popup tool menu
  //menu_Actions = createMenu("menu_Actions", "&Tools");
  //menu_Actions->addAction(actionPan);  actionDraw, actionErase, actionSelect, actionInsert_Space

  // storage for other tool icons (switched in/out depending on mode)
  drawIcon = SvgGui::useFile(":/icons/ic_menu_draw.svg");
  nextPageIcon = SvgGui::useFile(":/icons/ic_menu_next.svg");
  appendPageIcon = SvgGui::useFile(":/icons/ic_menu_append_page.svg");
  editBoxIcon = SvgGui::useFile(":/icons/ic_menu_editbox.svg");
  swbIcon = SvgGui::useFile(":/icons/ic_menu_cloud.svg");

  // toolbar menus
  actionOverflow_Menu = createAction("actionOverflow_Menu", "Menu", ":/icons/ic_menu_overflow.svg", "", NULL);
  actionSelection_Menu = createAction("actionSelection_Menu", "Selection Menu", ":/icons/ic_menu_paste.svg", "", NULL);

  Menu* docmenu = createMenu("documentMenu", "Document", Menu::HORZ);
  // don't rely on these actions being on toolbar when not using doc list!
  if(!cfg->Bool("useDocList")) {
    docmenu->addAction(actionNew_Document);
    docmenu->addAction(action_Open);
    docmenu->addAction(actionSave);
  }
  if(IS_DEBUG || !PLATFORM_IOS || cfg->String("syncServer", "")[0]) {
    docmenu->addAction(actionShare_Document);
#if PLATFORM_IOS
    docmenu->addAction(actionOpen_Shared_Doc);
#endif
  }
  //if(!cfg->Bool("useDocList")) docmenu->addAction(actionOpen_Shared_Doc);
  docmenu->addAction(actionRevert);
  docmenu->addAction(actionSave_As);
  docmenu->addAction(actionInsertDocument);
#if PLATFORM_MOBILE
  docmenu->addAction(actionSend_Page);
  docmenu->addAction(actionSend_HTML);
  docmenu->addAction(actionSend_PDF);
#else
  docmenu->addAction(actionExport_PDF);
#endif

  Menu* pagemenu = createMenu("pageMenu", "Page", Menu::HORZ);
  pagemenu->addAction(actionNew_Page_Before);
  pagemenu->addAction(actionNew_Page_After);
  // would be nice if we could avoid having menu items for these expand down/right
  pagemenu->addAction(actionExpand_Down);
  pagemenu->addAction(actionExpand_Right);
  pagemenu->addAction(actionSelect_Pages);

  Menu* viewmenu = createMenu("viewMenu", "View", Menu::HORZ);
  viewmenu->addAction(actionPrevious_View);
  viewmenu->addAction(actionNext_View);
  // Doesn't much sense to have these buried in submenu now that we have zoom button on statusbar
  //viewmenu->addAction(actionZoom_In);
  //viewmenu->addAction(actionZoom_Out);
  //viewmenu->addAction(actionReset_Zoom);
  Button* splitviewbtn = viewmenu->addAction(actionSplitView);
  splitviewbtn->mMenu->setAlign(Menu::HORZ_LEFT);
  viewmenu->addAction(actionShow_Bookmarks);  // in case hidden from toolbar
  // not sure this is the best place...
  viewmenu->addAction(actionShow_Clippings);
  // Qt uses immersive mode (sticky) for fullscreen on Android 4.4+; hides status bar on earlier versions
  viewmenu->addAction(actionFullscreen);
  viewmenu->addAction(actionInvertColors);

  Action* selactions[] = {actionCut, actionCopy, actionPaste, actionDupSel, actionDelete_Selection,
      actionUngroup, actionInvert_Selection, actionSelect_All, actionCreate_Link};  //actionSelect_Similar
  Menu* selectionmenu = createMenu("selMenu", "Selection", Menu::HORZ);
  for(Action* a : selactions)
    selectionmenu->addAction(a);
  // trying to share the same Menu between overflow menu and Selection Menu action causes problems
  Menu* selmenu2 = createMenu("selMenu2", "Selection", tbMenuAlign);
  for(Action* a : selactions)
    selmenu2->addAction(a);
  actionSelection_Menu->setMenu(selmenu2);
  // selection menu is one step above title button
  //actionSelection_Menu->setPriority(Action::NormalPriority - 2);

  // menu for shared whiteboard actions we want to always to be available - this is just a temp solution
  menuWhiteboard = createMenu("swbMenu", "Whiteboard", Menu::HORZ);
  menuWhiteboard->addAction(actionSyncInfo);
  menuWhiteboard->addAction(actionViewSync);
  menuWhiteboard->addAction(actionViewSyncMaster);
  menuWhiteboard->addAction(actionSendImmed);

  // Note: Nexus 4 fits 10 menu items on screen (portrait)
  overflowMenu = createMenu("overflowMenu", "", vertToolbar ? Menu::HORZ : Menu::VERT);  //Menu::VERT_LEFT);
  menuWhiteboardBtn = overflowMenu->addSubmenu(_("Whiteboard"), menuWhiteboard);
  overflowMenu->addSubmenu(_("Document"), docmenu);
  overflowMenu->addSubmenu(_("Page"), pagemenu);
  overflowMenu->addSubmenu(_("View"), viewmenu);
  overflowMenu->addSubmenu(_("Selection"), selectionmenu);
  overflowMenu->addAction(actionPage_Setup);
  overflowMenu->addAction(actionInsert_Image);  // move to Document menu?
  overflowMenu->addAction(actionPreferences);

#ifdef SCRIBBLE_TEST
  Action* actionRunTests = createAction("actionRunTests", "Run Tests", "", "", SLOT(runTestUI("test")));
  Action* actionSyncTests = createAction("actionSyncTests", "Sync Tests", "", "", SLOT(runTestUI("synctest")));
  Action* actionPerfTests = createAction("actionPerfTests", "Performance Test", "", "", SLOT(runTestUI("perftest")));
  Action* actionInputTests = createAction("actionInputTests", "Input Test", "", "", SLOT(runTestUI("inputtest")));
  Menu* testmenu = createMenu("testMenu", "Testing", Menu::HORZ);
  testmenu->addAction(actionRunTests);
  testmenu->addAction(actionSyncTests);
  testmenu->addAction(actionPerfTests);
  testmenu->addAction(actionInputTests);
  overflowMenu->addSubmenu("Testing", testmenu);
#endif

  actionOverflow_Menu->setMenu(overflowMenu);
  // never hide overflow menu
  actionOverflow_Menu->setPriority(100); //Action::HighPriority);

  // hide whiteboard menu initially
  menuWhiteboardBtn->setEnabled(false);
  menuWhiteboardBtn->setVisible(false);

  // undoRedoBtn and docTitle ... how to set priority for these?
  // - maybe create a subclass of Action that is tied to a single button?
  titleButton = createToolbutton(appIcon, "Write", true);
  titleButton->onClicked = SLOT(openDocument());
  menuRecent_Files = createMenu("menuRecent", "Recent Documents", tbMenuAlign, false);  //Menu::VERT_RIGHT
  // iOS doc browser has recents already (and we'd need to save secured bookmarks to open recents ourselves)
#if !PLATFORM_IOS
  titleButton->addWidget(menuRecent_Files);
  titleButton->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(isLongPressOrRightClick(event)) {
      gui->showMenu(menuRecent_Files);
      gui->setPressed(menuRecent_Files);
      titleButton->node->setXmlClass(
          addWord(removeWord(titleButton->node->xmlClass(), "hovered"), "pressed").c_str());
      return true;
    }
    return false;
  });
  setupTooltip(titleButton, altTooltip(_("Documents"), _("Recent Documents")));
#else
  setupTooltip(titleButton, "Open Document");
#endif

  undoRedoBtn = createToolbutton(actionUndo->icon(), _("Undo/Redo"));
  ButtonDragDial* undoDial = new ButtonDragDial(undoRedoBtn);
  // undo dial is fixed at 5x size of button (height), but a more general way to center would be nice
  undoDial->node->setAttribute(vertToolbar ? "top" : "left", "-200%");
  undoDial->node->setAttribute(vertToolbar ? "left" : "top", "130%");
  undoDial->onStep = [this](int delta){
    while(delta > 0 && app->activeDoc()->canRedo()) {
      app->doCommand(ID_REDO);
      delta--;
    }
    while(delta < 0 && app->activeDoc()->canUndo()) {
      app->doCommand(ID_UNDO);
      delta++;
    }
    return delta;
  };
  undoDial->onAltStep = [this](int delta){
    if(delta == 0)
      app->activeArea()->recentStrokeSelDone();
    while(delta > 0 && app->activeArea()->recentStrokeDeselect())
      delta--;
    while(delta < 0 && app->activeArea()->recentStrokeSelect())
      delta++;
    return delta;
  };
  setupTooltip(undoRedoBtn, altTooltip(_("Undo/Redo"), _("Select Recent")));
}

// one-time help popups ... disabled for now awaiting further consideration
#ifdef ONE_TIME_TIPS
#include <unordered_set>

static const char* tipPopupSVG = R"#(
  <g class="tooltip tip-dialog" position="absolute" display="none" layout="box">
    <rect box-anchor="fill" stroke-width="0.5" stroke="#000" width="36" height="36"/>
    <g class="dialog-layout" box-anchor="fill" layout="flex" flex-direction="column">
      <g class="body-container" box-anchor="fill" layout="flex" flex-direction="column">
      <text margin="5"></text>
      </g>
      <g class="button-container dialog-buttons" margin="5 4" box-anchor="hfill" layout="flex" flex-direction="row" justify-content="flex-end">

        <g class="btn-close-tip toolbutton" box-anchor="vfill" layout="box" margin="0 5">
          <rect box-anchor="hfill" stroke-width="0.8" stroke="#000" width="36" height="36"/>
          <text class="title" margin="4 4"></text>
        </g>

        <g class="btn-no-tips toolbutton" box-anchor="vfill" layout="box" margin="0 5">
          <rect box-anchor="hfill" stroke-width="0.8" stroke="#000" width="36" height="36"/>
          <text class="title" margin="4 4"></text>
        </g>

      </g>
    </g>
  </svg>
)#";

bool MainWindow::oneTimeTip(const char* id, Point pos, const char* message)
{
  static auto flags1 = splitStr<std::unordered_set>(ScribbleApp::cfg->String("flags1", ""), ',', true);
  static AbsPosWidget* dialog = NULL;

  // load flags1 if not loaded yet
  if(flags1.count(id) || flags1.count("notips"))  //!cfg->Bool("showHelpTips"))
    return false;
  if(!message)
    return true;
  // now actually show the dialog
  if(!dialog) {
    dialog = new AbsPosWidget(loadSVGFragment(tipPopupSVG));
    // currently not possible to select by id in a fragment (i.e., no document)
    Button* closebtn = new Button(dialog->containerNode()->selectFirst(".btn-close-tip"));
    closebtn->setTitle(_("CLOSE "));
    closebtn->onClicked = [](){ dialog->setVisible(false); };
    Button* notipsbtn = new Button(dialog->containerNode()->selectFirst(".btn-no-tips"));
    notipsbtn->setTitle(_("NO MORE TIPS"));
    notipsbtn->onClicked = [](){ dialog->setVisible(false); flags1.insert("notips"); };
    selectFirst("#main-container")->addWidget(dialog);
  }
  dialog->selectFirst(".body-container")->setText(message);
  Rect parentBounds = dialog->node->parent()->bounds();
  dialog->node->setAttribute("left", fstring("%g", pos.x - parentBounds.left).c_str());
  dialog->node->setAttribute("top", fstring("%g", pos.y - parentBounds.top).c_str());
  dialog->setVisible(true);
  flags1.insert(id);  // mark tip as shown
  return true;
}

void MainWindow::setupHelpTips()
{
  if(oneTimeTip("clippings")) {
    clippingsPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
      if(event->type == SvgGui::VISIBLE) {
        oneTimeTip("clippings", Point(40, 90),
            _("Drag and drop clippings to use, reorder, and remove.\nDrag and drop selection here to save clipping."));
      }
      return false;
    });
  }
  if(oneTimeTip("autoclose")) {
    auto acactions = {actionDraw, actionErase, actionSelect, actionInsert_Space};
    for(auto action : acactions) {
      Widget* btn = action->buttons[0];
      if(btn) {
        btn->addHandler([=](SvgGui* gui, SDL_Event* event) {
          if(event->type == SDL_FINGERUP) {
            Rect b = btn->node->bounds();
            oneTimeTip("autoclose", Point(b.left, b.bottom + 10),
                _("Press button and drag down menu to change tool mode.\nTap button twice to lock tool."));
          }
          return false;
        });
      }
    }
  }
}
#else
bool MainWindow::oneTimeTip(const char* id, Point pos, const char* message) { return false; }
void MainWindow::setupHelpTips() {}
#endif
