#pragma once

#include "ugui/widgets.h"
#include "page.h"

class ScribbleDoc;
class ColorEditBox;

class RulingDialog : public Dialog
{
public:
  RulingDialog(ScribbleDoc* doc);
  void accept();

  static int predefSizes[][2];
  static unsigned int predefRulings[][4];

private:
  void setPaperType(int index);
  void setRuleType(int index);
  void checkClipping();

  ScribbleDoc* scribbleDoc;

  SpinBox* spinWidth;
  SpinBox* spinHeight;
  SpinBox* spinXRuling;
  SpinBox* spinYRuling;
  SpinBox* spinLeftMargin;
  ColorEditBox* pageColorPicker;
  ColorEditBox* ruleColorPicker;
  CheckBox* cbApplyToAll;
  CheckBox* cbDocDefault;
  CheckBox* cbGlobalDefault;
  ComboBox* comboRuling;
  ComboBox* comboPaperSize;
  Widget* clipWarning;
  ScrollWidget* scrollWidget = NULL;

  PageProperties props;
};
