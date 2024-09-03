#ifndef SCRIBBLEMODE_H
#define SCRIBBLEMODE_H

#include "scribbleconfig.h"

// don't change numeric values (saved to config file to remember tool modes) - only add at end!
enum { MODE_NONE = 10, MODE_PAN, MODE_STROKE, MODE_ERASE, MODE_ERASESTROKE, MODE_ERASERULED, MODE_ERASEFREE,
    MODE_SELECT, MODE_SELECTRECT, MODE_SELECTRULED, MODE_SELECTLASSO, MODE_MOVESEL, MODE_MOVESELFREE,
    MODE_MOVESELRULED, MODE_INSSPACE, MODE_INSSPACEVERT, MODE_INSSPACEHORZ, MODE_INSSPACERULED,
    MODE_BOOKMARK, MODE_SCALESEL, MODE_SCALESELW, MODE_ROTATESEL, MODE_ROTATESELW, MODE_TOOLMENU,
    MODE_SELECTPATH, MODE_CROPSEL, MODE_PAGESEL, MODE_LAST};

constexpr int MODEMOD_NONE = 0;
constexpr int MODEMOD_ERASE = 0x01;
constexpr int MODEMOD_PENBTN = 0x02;
constexpr int MODEMOD_MOVESEL = 0x04;
constexpr int MODEMOD_SCALESEL = 0x08;
constexpr int MODEMOD_ROTATESEL = 0x10;
constexpr int MODEMOD_CROPSEL = 0x20;
// flags indicating cursor entered from edge of screen
constexpr int MODEMOD_EDGETOP = 0x100;
constexpr int MODEMOD_EDGEBOTTOM = 0x200;
constexpr int MODEMOD_EDGELEFT = 0x400;
constexpr int MODEMOD_EDGERIGHT = 0x800;
constexpr int MODEMOD_EDGEMASK = 0xF00;
// only set for release events
constexpr int MODEMOD_CLICK = 0x10000;
constexpr int MODEMOD_FASTCLICK = 0x20000;
constexpr int MODEMOD_DBLCLICK = 0x40000;
constexpr int MODEMOD_MAYBECLICK = 0x80000;
// for now, we're just going to use upper byte of modemod to hold id of pressed button for simul. pen + touch
constexpr int MODEMOD_PRESSEDMASK = 0xFF000000;

// actions
enum {ID_UNDO = 100, ID_REDO, ID_SELALL, ID_SELSIMILAR, ID_INVSEL, ID_DELSEL, ID_COPYSEL, ID_CUTSEL,
    ID_PASTE, ID_CANCEL, ID_EXPANDDOWN, ID_EXPANDRIGHT, ID_ZOOMIN, ID_ZOOMOUT, ID_RESETZOOM, ID_PREVPAGE,
    ID_NEXTPAGE, ID_PAGEAFTER, ID_PAGEBEFORE, ID_DELPAGE, ID_NEWDOC, ID_ZOOMALL, ID_ZOOMWIDTH, ID_DUPSEL,
    ID_LINKBOOKMARK, ID_PREVVIEW, ID_NEXTVIEW, ID_SELRECENT, ID_DESELRECENT, ID_SAVESEL, ID_NEXTPAGENEW,
    ID_PREVSCREEN, ID_NEXTSCREEN, ID_STARTOFDOC, ID_ENDOFDOC, ID_SCROLLUP, ID_SCROLLDOWN, ID_UNGROUP};

constexpr int ID_EMULATEPENBTN = 3000;
// clipping command flag
constexpr int CLIPPING_CMD = 0x40000000;

class ScribbleMode {
private:
  int currMode;  // specific mode
  int stickyMode;  // for return after single use
  // these are used to select the specific tool if a general tool selection is made
  ScribbleConfig* cfg;

  int getSpecificMode(int mode) const;

public:
  int eraserMode;
  int selectMode;
  int insSpaceMode;
  int moveSelMode;

  ScribbleMode(ScribbleConfig* _cfg) : currMode(MODE_NONE), stickyMode(MODE_NONE), cfg(_cfg) {}

  // used to show current mode in UI ... might want to return general mode, but return specific mode for
  //  now to maintain previous behavior
  int getMode() const { return getSpecificMode(currMode); }
  int getNextMode() const { return getSpecificMode(stickyMode); }
  // this should be called when user selects a tool
  void setMode(int mode, bool once=false);
  // legacy support
  void setRuled(bool ruled);
  // interface to ScribbleArea
  int getScribbleMode(int modifier = MODEMOD_NONE) const;  // called by ScribbleArea::onPressEvent
  int scribbleDone();  // called on cursor up or cancel

  std::string saveModes();
  void loadModes(const char* modestr);

  static int getModeType(int mode);
  static bool isUndoable(int mode);
  static bool isSlow(int mode);
};

#endif // SCRIBBLEMODE_H
