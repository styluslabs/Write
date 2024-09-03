#ifndef DOCUMENTLIST_H
#define DOCUMENTLIST_H

#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "basics.h"

class ScribbleApp;

class NewDocDialog : public Dialog
{
public:
  NewDocDialog(const char* title, const FSPath& fsinfo, bool newdoc = false);

  std::string getName() const { return StringRef(nameEdit->text()).trimmed().toString(); }
  TextEdit* nameEdit = NULL;
  ComboBox* comboRuling = NULL;
  Toolbar* msgBar = NULL;
};

class DocumentList : public Window
{
public:
  DocumentList(const char* root, const char* temp);
  //~DocumentList() { MainWindow::removeDir(trashPath, true); }

  std::string selectedFile;  // filename out
  std::string selectedSrcFile;  // source file when opening copy of file
  enum Result_t {REJECTED = 0, NEW_DOC, EXISTING_DOC, OPEN_COPY, OPEN_WHITEBOARD, OPEN_HELP} result;  // out
  int selectedRuling;

  enum Mode_t {OPEN_DOC, CHOOSE_DOC, CHOOSE_IMAGE, SAVE_DOC, SAVE_PDF};
  bool isMultiFileDoc(const FSPath& fileinfo) const;
  void setup(Window* parent, Mode_t mode = OPEN_DOC, const char* exts = NULL, bool cancelable = true);
  void finish(Result_t result);

  static bool copyDocument(const FSPath& src, const FSPath& dest, bool move);

  std::function<void(int)> onFinished;

protected:
  void cutItem();
  void copyItem();
  void openCopyItem();
  void renameItem();
  void deleteItem();
  void pasteItem();
  void newDoc();
  void newFolder();
  void openWhiteboard();
  void clearClipboard();
  void undoDelete();
  void hideUndo();
  void refresh();

private:
  //SvgGui* svgGui;
  std::string docFileExt;
  std::string fileExts;
  Button* newFolderBtn;
  Button* newDocBtn;
  Button* pasteButton;
  Button* undoButton;
  Button* helpBtn;
  Button* whiteboardBtn;
  Button* contextMenuCopy;
  Button* contextMenuOpenCopy;
  Button* saveHereBtn;
  Button* cancelChoose;
  Button* zoomIn;
  Button* zoomOut;
  Toolbar* pasteBar;
  Toolbar* undoBar;
  Toolbar* msgBar;
  Widget* listView;
  Menu* contextMenu;
  ScrollWidget* scrollWidget;
  //Widget* mainWindow;
  std::unique_ptr<SvgNode> listItemProto;
  std::unique_ptr<SvgNode> gridItemProto;
  std::vector<Button*> breadCrumbs;
  Button* drivesBtn;
#if PLATFORM_ANDROID
  Button* privateDirBtn;
  Button* sharedDirBtn;
#endif

  std::unique_ptr<SvgUse> fileUseNode;
  std::unique_ptr<SvgUse> folderUseNode;
  int iconWidth;

  FSPath docRoot;
  bool docListSiloed = false;
  FSPath undoDeleteDir;
  FSPath trashPath;

  FSPath currDir;
  FSPath contextMenuItem;
  FSPath clipboard;
  bool cutClipboard;
  Mode_t currMode = OPEN_DOC; //chooseOnly;

  void setCurrDir(const char* path);
  void createUI();
  bool convertDocuments(FSPath src);
  void zoomListView(int step);
};

#endif // DOCUMENTLIST_H
