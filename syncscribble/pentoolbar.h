#pragma once

#include "ugui/widgets.h"
#include "basics.h"
#include "scribbleconfig.h"


class ScribbleApp;
class ColorEditBox;
class PenPreview;

// idea here is to abstract a container for a group of widgets that handles overflow, currently w/ an overflow
//  menu, but scrolling could be another option
class PaletteWidget : public Widget
{
public:
  //PaletteWidget(SvgNode* n, Widget* main, Widget* overflow, Widget* btn);
  PaletteWidget(SvgNode* n) : Widget(n) {}
  // we could add insert and delete methods in the future if needed
  void addButton(Button* w);
  void setNumVisible(int n);
  void clear();

  std::vector<Button*> items;
  Widget* mainGroup = NULL;
  Widget* overflowGroup = NULL;
  Widget* overflowBtn = NULL;
  int numMainGroup;
};

class PenToolbar : public Toolbar
{
public:
  PenToolbar();

  ScribblePen pen;
  enum Mode { PEN_MODE, BOOKMARK_MODE, SELECTION_MODE } mode = PEN_MODE;

  void saveConfig(ScribbleConfig* cfg) const;
  void updateColor();
  void updateWidth();
  void updatePen();
  void setPen(const ScribblePen& newpen, Mode m);
  //void dragWidth(int delta);

  enum ChangedFlag { COLOR_CHANGED=1, WIDTH_CHANGED=2, PEN_CHANGED=4, YIELD_FOCUS=8,
      UNDO_PREV=0x10000, SAVE_PEN=0x20000 };
  std::function<void(int)> onChanged;

  // access needded for auto adjust
  PaletteWidget* colorPalette;
  PaletteWidget* widthPalette;
  Widget* stretch;
  Button* closeBtn;

  const Dim penWidthPreviewMax = 22;  // was 30 for circle instead of line; static constexpr only works for int

private:
  void rebuildGrids();
  void updateWidthPicker(bool varw, bool chisel);

  Button* cbHighlight;
  Button* cbSnaptoGrid;
  Button* cbLineDrawing;
  Button* cbEphemeral;
  //Button* comboPressure;
  Button* comboSavePen;
  ColorEditBox* colorPicker;
  SpinBox* spinWidth;
  Button* widthPreview;
  Widget* colorGroup;
  Widget* widthGroup;

  // advanced pen options
  PenPreview* penPreview;
  ComboBox* comboPenTip;
  SpinBox* spinRatio;
  SpinBox* spinPrPrm;
  SpinBox* spinMaxSp;
  SpinBox* spinAngle;
  SpinBox* spinDash;
  SpinBox* spinGap;
  CheckBox* cbRatio;
  CheckBox* cbPrPrm;
  CheckBox* cbMaxSp;
  CheckBox* cbAngle;
  Widget* rowRatio;
  Widget* rowPrPrm;
  Widget* rowMaxSp;
  Widget* rowAngle;
  Widget* rowDash;
  Widget* rowGap;

  Button* overflowBtn;
  Button* selOverflowBtn;
  Menu* colorCtxMenu;
  Menu* widthCtxMenu;
  Button* colorMenuDelete;
  Button* widthMenuDelete;
  Dim preScale;

  int changesSinceFocused = -1;
  int contextMenuIdx;
  std::vector<Color> savedColors;
  std::vector<Dim> savedWidths;
  static std::unique_ptr<SvgNode> widthBtnNode;
  static const Dim PEN_WIDTHS[];
};

class AutoAdjContainer;
AutoAdjContainer* createPenToolbarAutoAdj();
