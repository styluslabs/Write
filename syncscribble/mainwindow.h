#pragma once

#include "ugui/widgets.h"

class ScribbleApp;
struct UIState;

class ScribbleWidget;
class ScribbleView;
class ScribbleArea;
class ScribbleDoc;
class OverlayWidget;
class AutoAdjContainer;

class MainWindow : public Window
{
public:
  MainWindow(SvgDocument* n) : Window(n) {}
  ~MainWindow();
  void setupUI(ScribbleApp* a);
  void setupActions();
  void createToolBars();
  Action* modeToAction(int mode);
  void updateMode();
  void refreshScribbleWidget(ScribbleWidget* w, const UIState* uiState);
  void refreshCommonUI(ScribbleDoc* doc, const UIState* uiState);
  void refreshUI(ScribbleDoc* doc, int reason);
  void orientationChanged();
  void refreshPens(ScribbleDoc* doc);
  bool oneTimeTip(const char* id, Point pos = {}, const char* message = NULL);

  void togglePenToolbar();
  void toggleFullscreen();
  void toggleClippings();
  void toggleBookmarks();
  void toggleDisableTouch();
  void toggleSyncView();
  void toggleSyncViewMaster();
  void toggleSplitView(int newstate);
  void toggleInvertColors();

  Action* findAction(const char* name);

  //static ScribbleWidget* createScribbleWidget(Widget* container, ScribbleView* area);
  ScribbleWidget* createScribbleAreaWidget(Widget* container, ScribbleArea* area);

  std::vector<Action*> actionList;
  std::unordered_map<std::string, Action*> shortcuts;

  Action* checkedMode = NULL;
  Action* checkedSubMode = NULL;

  Action* action_Previous_Page;
  Action* action_Next_Page;
  Action* actionZoom_In;
  Action* actionZoom_Out;
  Action* actionPan;
  Action* actionNew_Page_Before;
  Action* actionNew_Page_After;
  Action* action_About;
  Action* action_Open;
  Action* actionSave_As;
  Action* actionExit;
  Action* actionSelect_All;
  Action* actionSelect_Similar;
  Action* actionInvert_Selection;
  Action* actionDelete_Selection;
  Action* actionCopy;
  Action* actionPaste;
  Action* actionDupSel;
  Action* actionInsertDocument;
  Action* actionSave;
  Action* actionPage_Setup;
  Action* actionUndo;
  Action* actionRedo;
  Action* actionExpand_Down;
  Action* actionExpand_Right;
  Action* actionCut;
  Action* actionRevert;
  Action* actionReset_Zoom;
  Action* actionNew_Document;
  Action* actionShow_Bookmarks;
  Action* actionSend_Page;
  Action* actionStroke_Eraser;
  Action* actionRuled_Eraser;
  Action* actionRect_Select;
  Action* actionRuled_Select;
  Action* actionInsert_Space_Vert;
  Action* actionRuled_Insert_Space;
  Action* actionCustom_Pen;
  Action* actionAdd_Bookmark;
  Action* actionDraw;
  Action* actionErase;
  Action* actionSelect;
  Action* actionInsert_Space;
  Action* actionLasso_Select;
  Action* actionPath_Select;
  Action* actionExport_PDF;
  Action* actionPreferences;
  Action* actionCreate_Link;
  Action* actionUngroup;
  Action* actionUpdateCheck;
  Action* actionFree_Eraser;
  Action* actionPrevious_View;
  Action* actionNext_View;
  Action* actionSend_HTML;
  Action* actionSend_PDF;
  Action* actionInsert_Image;
  Action* actionShare_Document;
  Action* actionOpen_Shared_Doc;
  Action* actionSendImmed;
  Action* actionOverflow_Menu;
  Action* actionSelection_Menu;
  Action* actionTools_Menu;
  Action* actionFullscreen;
  Action* actionSplitView;
  Action* actionSelect_Pages;
  Action* actionViewSync;
  Action* actionViewSyncMaster;
  Action* actionShow_Clippings;
  Action* actionClippingsUndo;
  Action* actionClippingsClose;
  Action* actionClippingsPin;
  Action* actionBookmarksClose;
  Action* actionBookmarksPin;
  Action* actionDisable_Touch;
  Action* actionSyncInfo;
  Action* actionInvertColors;

  Menu* menuRecent_Files;
  Menu* menuErase;
  Menu* menuSelect;
  Menu* menuInsert_Space;
  Menu* menuDraw;
  Menu* overflowMenu;
  Menu* menuWhiteboard;
  Button* menuWhiteboardBtn;
  Button* undoRedoBtn;
  Button* titleButton;
  Button* iapButton = NULL;  // iOS IAP
  Widget* toolBarStretch;
  Widget* selPopup;
  AutoAdjContainer* penToolbarAutoAdj;
  std::vector<Widget*> tbWidgets;
  std::string titleStr;
  std::vector<Widget*> penPreviews;
  ScribbleDoc* penDoc = NULL;

  const SvgNode* appIcon;
  const SvgNode* drawIcon;
  const SvgNode* nextPageIcon;
  const SvgNode* appendPageIcon;
  const SvgNode* editBoxIcon;
  const SvgNode* swbIcon;

  Splitter* bookmarkSplitter;
  Widget* bookmarkPanel;
  Splitter* clippingsSplitter;
  Widget* clippingsPanel;
  OverlayWidget* overlayWidget;

  Widget* scribbleContainer2 = NULL;
  Widget* scribbleFocusContainer2 = NULL;
  Splitter* scribbleSplitter = NULL;
  Widget* focusIndicator2 = NULL;
  //enum {SplitNone, SplitHorz, SplitVert, SplitNumStates};
  // splitState = +/- SPLIT_XXX : > 0 if open, < 0 if not; toggle just flips sign
  enum SplitState { SPLIT_TOGGLE=0, SPLIT_H12, SPLIT_H21, SPLIT_V12, SPLIT_V21 };
  int splitState = -SPLIT_H12;
  Point scribbleAreaStatusInset = {6, 6};
  bool vertToolbar = false;  // probably will have to become toolbarPos = top/left/right/bottom
  int toolsMenuMode = 0;

private:
  ScribbleApp* app;

  void setupTheme();
  void setupHelpTips();
  Action* createAction(const char* name,
      const char* title, const char* iconfile, const char* shortcut, const std::function<void()>& callback = NULL);
  Menu* createMenu(const char* name, const char* title, Menu::Align = Menu::VERT_RIGHT, bool showicons = true); //Widget* parent = 0);
};

MainWindow* createMainWindow();
