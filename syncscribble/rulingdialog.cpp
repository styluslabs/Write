#include "rulingdialog.h"
#include "ugui/textedit.h"
#include "scribbleapp.h"
#include "mainwindow.h"
#include "touchwidgets.h"
#include "scribbledoc.h"
#include "page.h"


// move to touchwidgets.cpp if needed elsewhere
Widget* createTitledColumn(const char* title, Widget* control1, Widget* control2 = NULL)
{
  Widget* col = createColumn({}, "5 5", "space-between");
  SvgText* titlenode = createTextNode(title);
  titlenode->setAttribute("margin", "4 8 4 0");  // t r b l
  titlenode->setAttribute("box-anchor", "left");
  titlenode->addClass("column-text");
  col->containerNode()->addChild(titlenode);
  col->addWidget(control1);
  if(control2)
    col->addWidget(control2);
  return col;
}

// TODO: don't want to pass in MainWindow ... move getScreenPageDims() somewhere else?
RulingDialog::RulingDialog(ScribbleDoc* doc) : Dialog(createDialogNode()), scribbleDoc(doc)
{
  Page* currPage = doc->activeArea->getCurrPage();
  props = currPage->getProperties();

  Dim xruling = props.xRuling; //ruleLayer->getXRuling();
  Dim yruling = props.yRuling; //ruleLayer->getYRuling();
  Dim marginLeft = props.marginLeft;  //ruleLayer->getXMargin();
  Color ruleColor = props.ruleColor;  //ruleLayer->getColor();
  // no rule color indicates no rulings, so use a valid value to make things work if user enters custom ruling
  if(ruleColor == Color::INVALID_COLOR)
    ruleColor = Page::DEFAULT_RULE_COLOR;

  int screenw, screenh;
  ScribbleApp::getScreenPageDims(&screenw, &screenh);
  predefSizes[0][0] = props.width;  predefSizes[0][1] = props.height;
  predefSizes[1][0] = screenw;  predefSizes[1][1] = screenh;
  predefSizes[2][0] = screenh;  predefSizes[2][1] = screenw;
  // current ruling
  predefRulings[0][0] = xruling;  predefRulings[0][1] = yruling;
  predefRulings[0][2] = marginLeft;  predefRulings[0][3] = ruleColor.color;

  clipWarning = new Widget(createTextNode(_("This page size will clip content!")));
  clipWarning->setVisible(false);

  comboPaperSize = createComboBox(
      {_("Current"), _("Screen"), _("Screen (landscape)"), _("Letter"), _("Letter (landscape)"), _("A4"), _("A4 (landscape)")});
  comboPaperSize->onChanged = [this](const char* s){ setPaperType(comboPaperSize->index()); };
  spinWidth = createTextSpinBox(props.width, xruling > 0 ? xruling : 50, 0, 100000);  // step by xruling
  spinWidth->onValueChanged = [this](Dim w){ checkClipping(); };
  spinHeight = createTextSpinBox(props.height, yruling > 0 ? yruling : 50, 0, 100000);
  spinHeight->onValueChanged = [this](Dim h){ checkClipping(); };

  // custom ruling cannot be changed
  if(currPage->isCustomRuling)
    comboRuling = createComboBox({_("Custom")});
  else {
    comboRuling = createComboBox({_("Current"), _("Plain"), _("Wide ruled"), _("Medium ruled"),
        _("Narrow ruled"), _("Coarse grid"), _("Medium grid"), _("Fine grid")});
    comboRuling->onChanged = [this](const char* s){ setRuleType(comboRuling->index()); };
  }

  spinXRuling = createTextSpinBox(xruling, 10, 0, 200);
  spinYRuling = createTextSpinBox(yruling, 10, 0, 200);
  spinLeftMargin = createTextSpinBox(marginLeft, 10, 0, 100000);

  cbApplyToAll = createCheckBox();
  cbDocDefault = createCheckBox();
  cbGlobalDefault = createCheckBox();
  cbApplyToAll->onToggled = [this](bool){ checkClipping(); };

  pageColorPicker = createColorEditBox(false);
  ruleColorPicker = createColorEditBox();

  Widget* colorbtns = createRow({}, "0 0", "space-between");
  colorbtns->addWidget(createTitledColumn(_("Page Color"), pageColorPicker));
  colorbtns->addWidget(createTitledColumn(_("Rule Color"), ruleColorPicker));

  Widget* dialogBody = selectFirst(".body-container");
  Rect pbbox = ScribbleApp::win->winBounds();
  // ScrollWidget interferes a bit with combobox behavior, so don't use unless necessary
  if(pbbox.height() < 620 || (PLATFORM_MOBILE && pbbox.width() < 620)) {
    Widget* col = createColumn();
    col->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
    scrollWidget = new ScrollWidget(new SvgDocument(), col);
    scrollWidget->node->setAttribute("box-anchor", "fill");  // use vfill and mBounds.w = 0 for horz auto-size
    dialogBody->node->setAttribute("box-anchor", "fill");
    dialogBody->node->setAttribute("layout", "box");  // change from flex to box
    dialogBody->node->removeAttr("flex-direction");
    dialogBody->node->restyle();  // needed to pick up layout attribute changes
    dialogBody->addWidget(createFillRect());
    dialogBody->addWidget(scrollWidget);
    dialogBody = col;
    setWinBounds(Rect::centerwh(pbbox.center(), 300, std::min(pbbox.height() - 60, Dim(610))));
  }
  auto createIndentRow = [](const char* title, Widget* w)
      { Widget* row = createTitledRow(title, w); row->setMargins(5, 0, 5, 20); return row; };
  dialogBody->setMargins(0, 8);
  dialogBody->addWidget(createTitledRow(_("Page size"), comboPaperSize));
  dialogBody->addWidget(createIndentRow(_("Width"), spinWidth));
  dialogBody->addWidget(createIndentRow(_("Height"), spinHeight));
  dialogBody->addWidget(clipWarning);
  dialogBody->addWidget(createTitledRow(_("Ruling"), comboRuling));
  dialogBody->addWidget(createIndentRow(_("X Ruling"), spinXRuling));
  dialogBody->addWidget(createIndentRow(_("Y Ruling"), spinYRuling));
  dialogBody->addWidget(createIndentRow(_("Left Margin"), spinLeftMargin));
  dialogBody->addWidget(colorbtns);
  const char* strapply = doc->numSelPages > 0 ? "Apply to selected pages" : "Apply to all existing pages";
  dialogBody->addWidget(createTitledRow(_(strapply), cbApplyToAll));
  dialogBody->addWidget(createTitledRow(_("Document default"), cbDocDefault));
  dialogBody->addWidget(createTitledRow(_("Global default"), cbGlobalDefault));

  // color picker has to be added to document before setColor can be called
  pageColorPicker->setColor(props.color);
  ruleColorPicker->setColor(ruleColor);

  setTitle(_("Page Setup"));
  acceptBtn = addButton(_("OK"), [this](){ accept(); finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
}

void RulingDialog::checkClipping()
{
  props.width = spinWidth->value();
  props.height = spinHeight->value();
  bool clip = scribbleDoc->doesClipStrokes(&props, cbApplyToAll->isChecked());
  if(clipWarning->isVisible() == clip)
    return;
  if(!scrollWidget)
    setWinBounds(Rect::centerwh(winBounds().center(), winBounds().width(), 0));
  clipWarning->setVisible(clip);
}

void RulingDialog::accept()
{
  props.width = spinWidth->value();
  props.height = spinHeight->value();
  if(props.width != 0 && props.width < 20) props.width = 20;
  if(props.height != 0 && props.height < 20) props.height = 20;

  props.color = pageColorPicker->color();
  props.xRuling = spinXRuling->value();
  props.yRuling = spinYRuling->value();
  props.marginLeft = spinLeftMargin->value();
  props.ruleColor = ruleColorPicker->color();
  bool applyall = cbApplyToAll->isChecked();
  bool docdefault = cbDocDefault->isChecked();
  bool globaldefault = cbGlobalDefault->isChecked();
  //if(xruling != predefRulings[0][0] || yruling != predefRulings[0][1]
  //    || marginLeft != predefRulings[0][2] || ruleColor != predefRulings[0][3]) {
  //  props.ruleLayer = new RuleLayer(xruling, yruling, marginLeft, ruleColor);
  //}
  //else if(!applyall && !docdefault && !globaldefault)
  //  props.ruleLayer = NULL;
  scribbleDoc->setPageProperties(&props, applyall, docdefault, globaldefault);
}

// predefined values (0th entry of each combo box is title, hence the decrement of index)

// {width, height}
int RulingDialog::predefSizes[][2] = {
  {0, 0},  // populated with current values
  {0, 0},  // populated based on screen size
  {0, 0},  // populated based on screen size
  {int(8.5 * 150), int(11 * 150)},  // Letter
  {int(11 * 150), int(8.5 * 150)},  // Letter landscape
  {int(8.27 * 150), int(11.7 * 150)},  // A4
  {int(11.7 * 150), int(8.27 * 150)}  // A4 landscape
};

void RulingDialog::setPaperType(int index)
{
  spinWidth->setValue(predefSizes[index][0]);
  spinHeight->setValue(predefSizes[index][1]);
  checkClipping();
}

// {x ruling, y ruling, left margin, rule color}
unsigned int RulingDialog::predefRulings[][4] = {
  {0, 0, 0, 0},  // populated with current values
  {0, 0, 0, 0},  // plain
  {0, 45, 100, 0x9FFFFFFF & Color::BLUE},  // wide ruled
  {0, 40, 100, 0x9FFFFFFF & Color::BLUE},  // medium ruled
  {0, 35, 100, 0x9FFFFFFF & Color::BLUE},  // narrow ruled
  {35, 35, 35, 0x7FFFFFFF & Color::BLUE},  // coarse grid
  {30, 30, 30, 0x7FFFFFFF & Color::BLUE},  // medium grid
  {20, 20, 20, 0x7FFFFFFF & Color::BLUE}  // fine grid
};

void RulingDialog::setRuleType(int index)
{
  // maybe we should show input fields for x,y offset if clipboard options are selected?
  spinXRuling->setValue(predefRulings[index][0]);
  spinYRuling->setValue(predefRulings[index][1]);
  spinLeftMargin->setValue(predefRulings[index][2]);
  if(predefRulings[index][3] != 0)
    ruleColorPicker->setColor(predefRulings[index][3]);
}
