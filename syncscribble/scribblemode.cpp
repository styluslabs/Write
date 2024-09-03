#include "scribblemode.h"
#include <sstream>


// if we get rid of ruled mode, how do we determine what to do for move sel?  use insert space setting?
// select move sel tool from pen menu?  select menu?

int ScribbleMode::getSpecificMode(int mode) const
{
  switch(mode) {
  // handle general modes
  case MODE_ERASE:
    return eraserMode;
  case MODE_SELECT:
    return selectMode;
  case MODE_INSSPACE:
    return insSpaceMode;
  case MODE_MOVESEL:
    return moveSelMode;
  default:
    return mode;
  }
}

// UI code is always in flux, so there is no point going out of the way to make it elegant or general
int ScribbleMode::getModeType(int mode)
{
  switch(mode) {
  case MODE_PAN:
    return MODE_PAN;
  case MODE_STROKE:
  case MODE_BOOKMARK:
    return MODE_STROKE;
  case MODE_ERASE:
  case MODE_ERASESTROKE:
  case MODE_ERASERULED:
  case MODE_ERASEFREE:
    return MODE_ERASE;
  case MODE_SELECT:
  case MODE_SELECTRECT:
  case MODE_SELECTRULED:
  case MODE_SELECTLASSO:
  case MODE_SELECTPATH:
    return MODE_SELECT;
  case MODE_MOVESELFREE:
  case MODE_MOVESELRULED:
    return MODE_MOVESEL;
  case MODE_INSSPACE:
  case MODE_INSSPACERULED:
  case MODE_INSSPACEVERT:
  case MODE_INSSPACEHORZ:
    return MODE_INSSPACE;
  case MODE_PAGESEL:
    return MODE_PAGESEL;
  }
  return 0;  // to suppress compiler warning
}

// alternative interface (will probably be removed)
void ScribbleMode::setRuled(bool ruled)
{
  eraserMode = ruled ? MODE_ERASERULED : MODE_ERASESTROKE;
  selectMode = ruled ? MODE_SELECTRULED : MODE_SELECTRECT;
  insSpaceMode = ruled ? MODE_INSSPACERULED : MODE_INSSPACEVERT;
  moveSelMode = ruled ? MODE_MOVESELRULED : MODE_MOVESELFREE;
}

std::string ScribbleMode::saveModes()
{
  std::ostringstream ss;
  ss << eraserMode << ' ' << selectMode << ' ' << insSpaceMode << ' ' << moveSelMode;
  return ss.str();
}

void ScribbleMode::loadModes(const char* modestr)
{
  std::stringstream ss(modestr ? modestr : "");
  int mode;
  // defaults
  eraserMode = MODE_ERASERULED;
  selectMode = MODE_SELECTRECT;  // MODE_SELECTRULED
  insSpaceMode = MODE_INSSPACERULED;
  moveSelMode = MODE_MOVESELFREE;  // tough call between ruled and free for initial
  // attempt to load from config string
  if(ss >> mode && getModeType(mode) == MODE_ERASE && mode != MODE_ERASE)
    eraserMode = mode;
  if(ss >> mode && getModeType(mode) == MODE_SELECT && mode != MODE_SELECT)
    selectMode = mode;
  if(ss >> mode && getModeType(mode) == MODE_INSSPACE && mode != MODE_INSSPACE)
    insSpaceMode = mode;
  if(ss >> mode && getModeType(mode) == MODE_MOVESEL && mode != MODE_MOVESEL)
    moveSelMode = mode;
  //moveSelMode = (insSpaceMode == MODE_INSSPACERULED) ? MODE_MOVESELRULED : MODE_MOVESELFREE;
}

void ScribbleMode::setMode(int mode, bool once)
{
  int newmode = mode;
  switch(mode) {
  // update saved tool modes
  case MODE_ERASESTROKE:
  case MODE_ERASERULED:
  case MODE_ERASEFREE:
    eraserMode = mode;
  case MODE_ERASE:
    newmode = MODE_ERASE;
    break;
  case MODE_SELECTRECT:
  case MODE_SELECTRULED:
  case MODE_SELECTLASSO:
  case MODE_SELECTPATH:
    selectMode = mode;
  case MODE_SELECT:
    newmode = MODE_SELECT;
    break;
  case MODE_MOVESELFREE:
  case MODE_MOVESELRULED:
    moveSelMode = mode;
  case MODE_MOVESEL:
    newmode = MODE_MOVESEL;
    break;
  case MODE_INSSPACERULED:
    //moveSelMode = MODE_MOVESELRULED;
    insSpaceMode = mode;
    newmode = MODE_INSSPACE;
    break;
  case MODE_INSSPACEVERT:
  case MODE_INSSPACEHORZ:
    //moveSelMode = MODE_MOVESELFREE;
    insSpaceMode = mode;
    newmode = MODE_INSSPACE;
    break;
  }

  // previously we had newmode == currMode, but I want to prevent changing mode of single-use tool from locking
  if(!once && (newmode == MODE_STROKE || newmode == MODE_PAGESEL || mode == currMode || !cfg->Bool("doubleTapSticky")))
    stickyMode = newmode;
  else if(currMode == MODE_PAGESEL)
    stickyMode = MODE_STROKE;

  currMode = newmode;
}

// Declaring this const to make it clear that it can be called multiple times for the same cursor down event
int ScribbleMode::getScribbleMode(int modifier) const
{
  int pressedmode = (modifier & MODEMOD_PRESSEDMASK) ? (modifier >> 24) : 0;
  if((modifier & MODEMOD_EDGEMASK) && cfg->Int("panFromEdge") > 0)
    return MODE_PAN;
  else if(currMode == MODE_PAGESEL)
    return MODE_PAGESEL;
  else if(modifier & MODEMOD_SCALESEL)
    return (modifier & MODEMOD_PENBTN) ? MODE_SCALESELW : MODE_SCALESEL;
  else if(modifier & MODEMOD_ROTATESEL)
    return (modifier & MODEMOD_PENBTN) ? MODE_ROTATESELW : MODE_ROTATESEL;
  else if(modifier & MODEMOD_CROPSEL)
    return MODE_CROPSEL;
  else if(modifier & MODEMOD_ERASE)
    return eraserMode;
  else if(modifier & MODEMOD_PENBTN)
    return getSpecificMode(cfg->Int("penButtonMode"));
  else if(pressedmode > MODE_NONE && pressedmode < MODE_LAST)
    return getSpecificMode(pressedmode);
  else if(modifier == MODEMOD_MOVESEL)
    return MODE_MOVESEL;  //moveSelMode;  ... this is now determined in ScribbleArea
  else
    return getSpecificMode(currMode);
}

int ScribbleMode::scribbleDone()
{
  currMode = stickyMode;
  return currMode;
}

bool ScribbleMode::isUndoable(int mode)
{
  return (mode != MODE_NONE && mode != MODE_PAN && mode != MODE_SELECTRECT && mode != MODE_SELECTRULED &&
      mode != MODE_SELECTLASSO && mode != MODE_SELECTPATH && mode != MODE_PAGESEL);
}

// returns true for a mode which can benefit from faster, lower quality drawing
bool ScribbleMode::isSlow(int mode)
{
  return (mode >= MODE_SELECT && mode <= MODE_INSSPACERULED);
}
