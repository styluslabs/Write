#include "pentoolbar.h"
#include "usvg/svgparser.h"  // for parseNumbersList
#include "ugui/textedit.h"
#include "touchwidgets.h"
#include "scribbleapp.h"


// or have spinbox buttons step by 1.25x or 0.8x of current value?
const Dim PenToolbar::PEN_WIDTHS[] = {0.1, 0.25, 0.5, 0.75, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.4, 2.8, 3.2, 3.6,
   4.0, 5.0, 6.0, 8.0, 10, 12, 16, 20, 25, 30, 35, 40, 48, 60, 72, 86, 100, 125, 150, 175, 200};

std::unique_ptr<SvgNode> PenToolbar::widthBtnNode;


//PaletteWidget::PaletteWidget(SvgNode* n, Widget* main, Widget* overflow, Widget* btn) : Widget(n),
//    mainGroup(main), overflowGroup(overflow), overflowBtn(btn) {}

// setupMenuItem requires a button
void PaletteWidget::addButton(Button* w)
{
  w->setMargins(0, 1);
  setupMenuItem(w);  // in case widget ends up on overflow menu; does no harm if not
  items.push_back(w);
  overflowBtn->setVisible(int(items.size()) > numMainGroup);

  int ii = items.size() - 1;
  (ii < numMainGroup ? mainGroup : overflowGroup)->addWidget(w);
  if(ii > numMainGroup && (ii - numMainGroup)%4 == 0)
    w->node->setAttribute("flex-break", "before");
}

// this will typically be used with AutoAdjContainer
void PaletteWidget::setNumVisible(int n)
{
  SvgGui* gui = window() ? window()->gui() : NULL;
  if(!gui || (numMainGroup >= int(items.size()) && n >= int(items.size()))) {
    numMainGroup = n;
    return;
  }

  numMainGroup = n;
  overflowBtn->setVisible(int(items.size()) > numMainGroup);
  for(Widget* w : items) {
    gui->onHideWidget(w);
    w->removeFromParent();
  }
  for(int ii = 0; ii < int(items.size()); ++ii) {
    items[ii]->node->removeAttr("flex-break");
    (ii < numMainGroup ? mainGroup : overflowGroup)->addWidget(items[ii]);
    if(ii > numMainGroup && (ii - numMainGroup)%4 == 0)
      items[ii]->node->setAttribute("flex-break", "before");
  }
}

void PaletteWidget::clear()
{
  SvgGui* gui = window() ? window()->gui() : NULL;
  if(gui) {
    gui->deleteContents(mainGroup);
    gui->deleteContents(overflowGroup);
    items.clear();
  }
}

PaletteWidget* createPaletteWidget()
{
  static const char* moreMenuSVG = R"#(
    <g id="menu" class="menu" display="none" position="absolute" box-anchor="fill" layout="box">
      <rect box-anchor="fill" width="20" height="20"/>
      <g class="child-container" box-anchor="fill"
          layout="flex" flex-direction="row" flex-wrap="wrap" justify-content="flex-start" margin="6 6">
      </g>
    </g>
  )#";

  static std::unique_ptr<SvgNode> moreMenuNode;
  if(!moreMenuNode)
    moreMenuNode.reset(loadSVGFragment(moreMenuSVG));

  PaletteWidget* container = new PaletteWidget(new SvgG());
  container->node->setAttribute("layout", "flex");
  container->node->setAttribute("flex-direction", "row");

  Widget* mainGroup = createRow();
  mainGroup->node->setAttribute("box-anchor", "");  // no stretch
  Button* overflowBtn = createToolbutton(SvgGui::useFile("icons/chevron_down.svg"), _("More"));
  Menu* overflowMenu = new Menu(moreMenuNode->clone(), Menu::VERT_LEFT);
  overflowBtn->setMenu(overflowMenu);
  overflowBtn->setVisible(false);  // hidden initially

  container->addWidget(mainGroup);
  container->addWidget(overflowBtn);

  // this hacky mess exposes some fundamental problems with our gui design
  container->mainGroup = mainGroup;
  container->overflowGroup = overflowMenu->selectFirst(".child-container");
  container->overflowBtn = overflowBtn;
  container->numMainGroup = 16;

  return container;
}

AutoAdjContainer* createPenToolbarAutoAdj()
{
  PenToolbar* tb = new PenToolbar;

  AutoAdjContainer* adjtb = new AutoAdjContainer(new SvgG(), tb);
  adjtb->node->setAttribute("box-anchor", "hfill");
  adjtb->node->addClass("pen-toolbar-autoadj");

  adjtb->adjFn = [tb, adjtb](const Rect& src, const Rect& dest) {
    if(dest.width() <= src.width() + 0.5 && tb->stretch->node->bounds().width() >= 1)
      return;
    // reset
    if(!tb->closeBtn->isVisible()) {
      for(Widget* sep : tb->select(".toolbar-separator"))
        sep->setVisible(true);
      tb->closeBtn->setVisible(true);
      tb->colorPalette->setVisible(true);
      tb->widthPalette->setVisible(true);
    }
    tb->colorPalette->setNumVisible(16);
    tb->widthPalette->setNumVisible(16);
    adjtb->repeatLayout(dest);
    while(tb->stretch->node->bounds().width() < 1 && tb->colorPalette->numMainGroup > 0) {
      int n = tb->colorPalette->numMainGroup - 1;
      tb->colorPalette->setNumVisible(n);
      tb->widthPalette->setNumVisible(n);
      adjtb->repeatLayout(dest);
    }
    if(tb->stretch->node->bounds().width() < 1) {
      for(Widget* sep : tb->select(".toolbar-separator"))
        sep->setVisible(false);
      tb->closeBtn->setVisible(false);
      tb->colorPalette->setVisible(false);
      tb->widthPalette->setVisible(false);
      adjtb->repeatLayout(dest);
    }
  };

  // feels hacky; should PenToolbar inherit from auto adj container instead?  We need more examples!
  tb->closeBtn->onClicked = [adjtb](){ adjtb->setVisible(false); };

  return adjtb;
}

PenToolbar::PenToolbar() : Toolbar(widgetNode("#toolbar")), pen(Color::BLACK, 0)
{
  // For width, I don't think we should change fill color to match currently selected color - would be
  //  distracting and we'd have to change background to match page color
  static const char* widthBtnSVG = R"#(
    <g class="previewbtn" transform="translate(17,17)">
      <rect fill="white" stroke="currentColor" stroke-width="2" x="-17" y="-17" width="34" height="34"/>
      <line class="width-line" x1="-4" y1="0" x2="4" y2="0" stroke="black" stroke-width="1" stroke-linecap="round"/>
    </g>
  )#";

  if(!widthBtnNode)
    widthBtnNode.reset(loadSVGFragment(widthBtnSVG));

  node->addClass("graybar");

  const char* colorStr = ScribbleApp::cfg->String("savedColors", "black,red,green,blue");
  auto colorStrSplit = splitStringRef(colorStr, ',', true);
  for(auto& s : colorStrSplit) {
    Color color = parseColor(s);
    if(color.isValid())
      savedColors.push_back(color);
  }

  // I think we may want to increase size of circles or use a short stroke instead of circle
  preScale = ScribbleApp::getPreScale();
  StringRef widthStr(ScribbleApp::cfg->String("savedWidths", "1.6,2.4,3.0,4.0"));
  parseNumbersList(widthStr, savedWidths);

  colorPalette = createPaletteWidget();
  widthPalette = createPaletteWidget();

  // context menus for saved colors, widths
  // ... instead of disabling delete when only one item, maybe we should add context menu for preview button?
  colorCtxMenu = createMenu(Menu::FLOATING, false);
  colorCtxMenu->addItem(_("Insert Current"), [this](){
    savedColors.erase(std::remove(savedColors.begin(), savedColors.end(), pen.color), savedColors.end());
    int pos = std::min(contextMenuIdx + 1, int(savedColors.size()));
    savedColors.insert(savedColors.begin() + pos, pen.color);
    rebuildGrids();
  });
  colorMenuDelete = colorCtxMenu->addItem(_("Delete"), [this](){
    savedColors.erase(savedColors.begin() + contextMenuIdx);
    rebuildGrids();
  });

  widthCtxMenu = createMenu(Menu::FLOATING, false);
  widthCtxMenu->addItem(_("Insert Current"), [this](){
    savedWidths.erase(std::remove(savedWidths.begin(), savedWidths.end(), pen.width), savedWidths.end());
    int pos = std::min(contextMenuIdx + 1, int(savedWidths.size()));
    savedWidths.insert(savedWidths.begin() + pos, pen.width);
    rebuildGrids();
  });
  widthMenuDelete = widthCtxMenu->addItem(_("Delete"), [this](){
    savedWidths.erase(savedWidths.begin() + contextMenuIdx);
    rebuildGrids();
  });

  colorPicker = createColorEditBox();
  colorPicker->onColorChanged = [this](Color){ updateColor(); };
  // clamp length? ... user may want to paste in longer string and then cut down
  //colorPicker->hexEdit->maxLength = strlen("lightgoldenrodyellow") + 1;

  widthPreview = new Button(widthBtnNode->clone());
  // add menu indicator
  widthPreview->containerNode()->addChild(loadSVGFragment(R"#(<use fill="black"
      x="4" y="6" width="12" height="12" xlink:href=":/icons/chevron_down.svg" />)#"));
  spinWidth = createTextSpinBox(1.25, 0.01, 0, 200, "%.3g", 120);  // 3 significant digits
  spinWidth->onValueChanged = [this](Dim w){ updateWidth(); };
  // override spinbox onStep to step though PEN_WIDTHS
  spinWidth->onStep = [this](int nsteps){
    Dim w = 0, oldw = spinWidth->value();
    if(nsteps > 0) {
      const Dim* next = std::upper_bound(std::begin(PEN_WIDTHS), std::end(PEN_WIDTHS), oldw);
      w = next != std::end(PEN_WIDTHS) ? *next : PEN_WIDTHS[NELEM(PEN_WIDTHS)-1];
    }
    else {
      const Dim* next = std::lower_bound(std::begin(PEN_WIDTHS), std::end(PEN_WIDTHS), oldw);
      w = next != std::begin(PEN_WIDTHS) ? *(next-1) : PEN_WIDTHS[0];
    }
    // spinbox can't be focused when menu is open, so only open if stepping w/ buttons, not when using keyboard!
    if((oldw <= 4) != (w <= 4) && mode == PEN_MODE) {
      window()->gui()->showMenu(widthPreview->mMenu);
      //window()->gui()->setPressed(widthPreview->mMenu);
      if(w > 4 && comboPenTip->index() == 0) {  // Flat?
        comboPenTip->setIndex(1);  // Round
        cbRatio->setChecked(false);
        //comboWidthFn->setIndex(0);  // None
        updatePen();
      }
      else if(w <= 4 && comboPenTip->index() == 1) {  // Round?
        comboPenTip->setIndex(0);  // Flat
        //if(!cbRatio->isChecked())  //comboWidthFn->index() == 0)
          cbRatio->setChecked(true);  //comboWidthFn->setIndex(1);  // Pressure
        updatePen();
      }
    }
    return w;
  };

  // pen tip, width options
  Widget* widthPicker = new Widget(new SvgG());
  widthPicker->node->setAttribute("layout", "flex");
  widthPicker->node->setAttribute("flex-direction", "column");
  widthPicker->node->setAttribute("margin", "0 6");

  Menu* widthPickerMenu = createMenu(Menu::VERT_LEFT);
  widthPickerMenu->addWidget(widthPicker);
  widthPickerMenu->isPressedGroupContainer = false;  // otherwise combo box menus don't close on 2nd click
  widthPreview->setMenu(widthPickerMenu);
  // hack to prevent widthPickerMenu from becoming pressedWidget; can be made slightly less hacky when
  //  menu opening is moved out of default button event handler and into onPressed() (w/ class="menuopen")
  widthPreview->onPressed = [this](){ window()->gui()->pressedWidget = NULL; };

  penPreview = new PenPreview();
  penPreview->mBounds = Rect::wh(250, 60);
  penPreview->setMargins(5, 0);
  // add a message below if Round selected: "Not recommended for width < 4" (width > 6 for flat)?
  comboPenTip = createComboBox({_("Flat"), _("Round"), _("Chisel")});
  setMinWidth(comboPenTip, 120);

  spinRatio = createTextSpinBox(80, 5, 0, 100, "%.3g%%", 120);
  spinPrPrm = createTextSpinBox(0.5, 0.05, -10, 10, "%.2f", 120);
  spinMaxSp = createTextSpinBox(0.5, 0.05, -5, 5, "%.2f", 120);  // Dim/ms
  spinAngle = createTextSpinBox(0, 15, 0, 360, u8"%.3g\u00B0", 120);  // deg
  spinDash = createTextSpinBox(0, 1, 0, 200, "%.3g", 120);  // Dim
  spinGap = createTextSpinBox(0, 1, 0, 200, "%.3g", 120);  // Dim

  cbRatio = createCheckBox(_("Vary Width"));
  cbPrPrm = createCheckBox(_("Pressure"));
  cbMaxSp = createCheckBox(_("Speed"));
  cbAngle = createCheckBox(_("Direction"));

  rowRatio = createTitledRow(NULL, cbRatio, spinRatio);
  rowPrPrm = createTitledRow(NULL, cbPrPrm, spinPrPrm);
  rowMaxSp = createTitledRow(NULL, cbMaxSp, spinMaxSp);
  rowAngle = createTitledRow(NULL, cbAngle, spinAngle);
  rowDash = createTitledRow(_("Dash"), spinDash);
  rowGap = createTitledRow(_("Gap"), spinGap);

  widthPicker->addWidget(penPreview);
  widthPicker->addWidget(createTitledRow(_("Pen Tip"), comboPenTip));
  widthPicker->addWidget(rowRatio);
  widthPicker->addWidget(rowPrPrm);
  widthPicker->addWidget(rowMaxSp);
  widthPicker->addWidget(rowAngle);
  widthPicker->addWidget(rowDash);
  widthPicker->addWidget(rowGap);

  comboPenTip->onChanged = [this](const char*){ updatePen(); };
  spinRatio->onValueChanged = [this](Dim){ updatePen(); };
  spinPrPrm->onValueChanged = [this](Dim){ updatePen(); };
  spinMaxSp->onValueChanged = [this](Dim){ updatePen(); };
  spinAngle->onValueChanged = [this](Dim){ updatePen(); };
  spinDash->onValueChanged = [this](Dim){ updatePen(); };
  spinGap->onValueChanged = [this](Dim){ updatePen(); };

  cbRatio->onToggled = [this](bool){ updatePen(); };
  cbPrPrm->onToggled = [this](bool){ updatePen(); };
  cbMaxSp->onToggled = [this](bool){ updatePen(); };
  cbAngle->onToggled = [this](bool){ updatePen(); };

  // filter to help reduce creation of unnecessary undo items
  auto focusFilt = [this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::FOCUS_GAINED)
      changesSinceFocused = 0;
    else if(event->type == SvgGui::FOCUS_LOST)
      changesSinceFocused = -1;
    return false;  // continue
  };
  colorPicker->addHandler(focusFilt);
  spinWidth->addHandler(focusFilt);

  Menu* overflowMenu = createMenu(Menu::VERT_LEFT);
  cbHighlight = createCheckBoxMenuItem(_("Highlight"));
  cbHighlight->onClicked = [this](){ cbHighlight->setChecked(!cbHighlight->checked()); updatePen(); };
  overflowMenu->addItem(cbHighlight);
  cbSnaptoGrid = createCheckBoxMenuItem(_("Snap to Grid"));
  cbSnaptoGrid->onClicked = [this](){ cbSnaptoGrid->setChecked(!cbSnaptoGrid->checked()); updatePen(); };
  overflowMenu->addItem(cbSnaptoGrid);
  cbLineDrawing = createCheckBoxMenuItem(_("Draw Lines"));
  cbLineDrawing->onClicked = [this](){ cbLineDrawing->setChecked(!cbLineDrawing->checked()); updatePen(); };
  overflowMenu->addItem(cbLineDrawing);
  cbEphemeral = createCheckBoxMenuItem(_("Ephemeral"));
  cbEphemeral->onClicked = [this](){ cbEphemeral->setChecked(!cbEphemeral->checked()); updatePen(); };
  overflowMenu->addItem(cbEphemeral);
  Menu* menuSavePen = createMenu(Menu::HORZ_LEFT, false);
  for(int ii = 0; ii < std::max(8, int(ScribbleApp::cfg->pens.size())); ++ii)
    menuSavePen->addItem(fstring("%d", ii+1).c_str(), [this, ii](){ if(onChanged) onChanged(SAVE_PEN | ii); });
  comboSavePen = overflowMenu->addSubmenu(_("Save Pen"), menuSavePen);

  overflowBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_overflow.svg"), _("More Options"));
  overflowBtn->setMenu(overflowMenu);

  Menu* selOverflowMenu = createMenu(Menu::VERT_LEFT);
  Button* useAsPenItem = createMenuItem(_("Use as Pen"));
  useAsPenItem->onClicked = [this]() {
    pen.color = colorPicker->color();
    pen.width = spinWidth->value();
    if(pen.color.alpha() > 0 && pen.width > 0 && onChanged)
      onChanged(PEN_CHANGED);
  };
  selOverflowMenu->addItem(useAsPenItem);
  selOverflowBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_overflow.svg"), _("Selection Options"));
  selOverflowBtn->setMenu(selOverflowMenu);
  selOverflowBtn->setVisible(false);

  closeBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_cancel.svg"), "");
  //closeBtn->onClicked = [this](){ setVisible(false); };  -- must be set by auto adj container

  static const char* spacerSVG = R"(<rect fill="none" width="12" height="20"/>)";
  widthGroup = createRow();
  widthGroup->node->setAttribute("box-anchor", "");  // no stretching for this subtoolbar!
  widthGroup->addWidget(spinWidth);
  widthGroup->addWidget(widthPreview);
  widthGroup->addWidget(new Widget(loadSVGFragment(spacerSVG)));
  widthGroup->addWidget(widthPalette);

  colorGroup = createRow();
  colorGroup->node->setAttribute("box-anchor", "");  // no stretching for this subtoolbar!
  colorGroup->addWidget(colorPicker);
  colorGroup->addWidget(new Widget(loadSVGFragment(spacerSVG)));
  colorGroup->addWidget(colorPalette);

  stretch = createStretch();

  addWidget(stretch);
  addWidget(widthGroup);
  addSeparator();
  addWidget(colorGroup);
  addSeparator();
  addWidget(overflowBtn);
  addWidget(selOverflowBtn);
  addSeparator();
  addWidget(closeBtn);
  addWidget(colorCtxMenu);
  addWidget(widthCtxMenu);

  addHandler([this](SvgGui* gui, SDL_Event* event) {
    // Return focus to ScribbleArea if Enter or Esc pressed (these keys are propagated above TextEdit)
    if(event->type == SDL_KEYDOWN) {
      SDL_Keycode key = event->key.keysym.sym;
      //Uint16 mods = event->key.keysym.mod;
      if(key == SDLK_ESCAPE || key == SDLK_RETURN) {
        if(onChanged)
          onChanged(YIELD_FOCUS);
        return true;
      }
    }
    return false;
  });

  // populate color and width grids (rows)
  rebuildGrids();
}

void PenToolbar::rebuildGrids()
{
  colorPalette->clear();
  widthPalette->clear();

  for(size_t ii = 0; ii < savedColors.size(); ++ii) {
    Color color = savedColors[ii];
    Button* btn = createColorBtn();  //new Button(colorBtnNode->clone());
    btn->selectFirst(".btn-color")->node->setAttr<color_t>("fill", color.color);
    // setting on <g class=toolbutton gets overridden by toolbutton CSS fill - do we need class=toolbutton?
    btn->onClicked = [this, color](){ colorPicker->setColor(color); updateColor(); };

    SvgGui::setupRightClick(btn, [this, ii](SvgGui* gui, Widget* w, Point p){
      contextMenuIdx = ii;
      gui->showContextMenu(colorCtxMenu, p, w);
    });
    setupTooltip(btn, altTooltip(colorToHex(color).c_str(), _("Edit")));
    colorPalette->addButton(btn);
  }
  colorMenuDelete->setEnabled(savedColors.size() > 1);

  for(size_t ii = 0; ii < savedWidths.size(); ++ii) {
    Dim width = savedWidths[ii];
    Button* btn = new Button(widthBtnNode->clone());
    Dim sc = std::min(penWidthPreviewMax, width); // * preScale/ScribbleApp::gui->globalScale);
    //btn->containerNode()->selectFirst(".width-circle")->setTransform(Transform2D::scaling(sc));
    btn->containerNode()->selectFirst(".width-line")->setAttr("stroke-width", sc);
    btn->onClicked = [this, width](){ spinWidth->setValue(width); updateWidth(); };

    SvgGui::setupRightClick(btn, [this, ii](SvgGui* gui, Widget* w, Point p){
      contextMenuIdx = ii;
      gui->showContextMenu(widthCtxMenu, p, w);
    });
    setupTooltip(btn, altTooltip(fstring(_("Pen width: %.1f"), width).c_str(), _("Edit")));
    widthPalette->addButton(btn);
  }
  widthMenuDelete->setEnabled(savedWidths.size() > 1);
}

void PenToolbar::saveConfig(ScribbleConfig* cfg) const
{
  std::vector<std::string> colorStrs;
  for(Color c : savedColors)
    colorStrs.emplace_back(fstring("#%02X%02X%02X%02X", c.alpha(), c.red(), c.green(), c.blue()));
  cfg->set("savedColors", joinStr(colorStrs, ",").c_str());

  std::vector<std::string> widthStrs;
  for(Dim w : savedWidths)
    widthStrs.push_back(fstring("%.3f", w));
  cfg->set("savedWidths", joinStr(widthStrs, ",").c_str());
}

void PenToolbar::setPen(const ScribblePen& newpen, Mode m)
{
  mode = m;
  widthGroup->setEnabled(mode != BOOKMARK_MODE);
  overflowBtn->setEnabled(mode == PEN_MODE);
  overflowBtn->setVisible(mode != SELECTION_MODE);
  selOverflowBtn->setVisible(mode == SELECTION_MODE);
  widthPreview->setEnabled(mode != SELECTION_MODE);
  comboSavePen->setVisible(ScribbleApp::cfg->Int("savePenMode") == 0);

  if(newpen == pen)
    return;
  pen = newpen;
  colorPicker->setColor(pen.color);
  spinWidth->setValue(pen.width);
  Dim sc = std::min(penWidthPreviewMax, pen.width); // * preScale/ScribbleApp::gui->globalScale);
  //widthPreview->containerNode()->selectFirst(".width-circle")->setTransform(Transform2D().scale(sc));
  widthPreview->containerNode()->selectFirst(".width-line")->setAttr("stroke-width", sc);
  //comboPressure->setText(fstring(_("Pressure: %g"), pen.pressureparam > 0 ? 1/pen.pressureparam : 0).c_str());
  cbHighlight->setChecked(pen.hasFlag(ScribblePen::DRAW_UNDER));
  cbSnaptoGrid->setChecked(pen.hasFlag(ScribblePen::SNAP_TO_GRID));
  cbLineDrawing->setChecked(pen.hasFlag(ScribblePen::LINE_DRAWING));
  cbEphemeral->setChecked(pen.hasFlag(ScribblePen::EPHEMERAL));

  // pen tip options
  penPreview->setPen(pen);
  comboPenTip->setIndex(pen.hasFlag(ScribblePen::TIP_FLAT) ? 0 :
      (pen.hasFlag(ScribblePen::TIP_CHISEL) ? 2 : 1));  // round or stroke both show as "Round"
  if(pen.hasFlag(ScribblePen::WIDTH_PR))
    spinPrPrm->setValue(pen.prParam != 0 ? 1.0/pen.prParam : 0.0);
  if(pen.hasFlag(ScribblePen::WIDTH_SPEED))
    spinMaxSp->setValue(pen.spdMax);
  if(pen.hasFlag(ScribblePen::WIDTH_DIR))
    spinAngle->setValue(pen.dirAngle);
  if(!pen.hasVarWidth() && !pen.hasFlag(ScribblePen::TIP_CHISEL)) {
    spinDash->setValue(pen.dash);
    spinGap->setValue(pen.gap);
  }
  cbRatio->setChecked(pen.hasVarWidth());
  if(pen.hasVarWidth()) {
    spinRatio->setValue(pen.wRatio*100);
    cbPrPrm->setChecked(pen.hasFlag(ScribblePen::WIDTH_PR));
    cbMaxSp->setChecked(pen.hasFlag(ScribblePen::WIDTH_SPEED));
    cbAngle->setChecked(pen.hasFlag(ScribblePen::WIDTH_DIR));
  }

  updateWidthPicker(pen.hasVarWidth(), pen.hasFlag(ScribblePen::TIP_CHISEL));
}

//void PenToolbar::dragWidth(int delta)
//{
//  // 1 pixel = 1% change, but done such that only absolute change matters (i.e. 1,1 gives same result as 2)
//  pen.width = MIN(Dim(200), Dim(pen.width*pow(1.01, delta)));
//  // this will result in call to updatePen()
//  spinWidth->setValue(pen.width);
//}

void PenToolbar::updateColor()
{
  pen.color = colorPicker->color();
  penPreview->setPen(pen);
  if(onChanged)
    onChanged(COLOR_CHANGED | (changesSinceFocused > 0 ? UNDO_PREV : 0));
  if(changesSinceFocused >= 0) ++changesSinceFocused;
}

void PenToolbar::updateWidth()
{
  pen.width = spinWidth->value();
  penPreview->setPen(pen);
  Dim sc = std::min(penWidthPreviewMax, pen.width); // * preScale/ScribbleApp::gui->globalScale);
  //widthPreview->containerNode()->selectFirst(".width-circle")->setTransform(Transform2D().scale(sc));
  widthPreview->containerNode()->selectFirst(".width-line")->setAttr("stroke-width", sc);

  if(onChanged)
    onChanged(WIDTH_CHANGED | (changesSinceFocused > 0 ? UNDO_PREV : 0));
  if(changesSinceFocused >= 0) ++changesSinceFocused;
}

void PenToolbar::updatePen()
{
  pen.setFlag(ScribblePen::DRAW_UNDER, cbHighlight->isChecked());
  pen.setFlag(ScribblePen::SNAP_TO_GRID, cbSnaptoGrid->isChecked());
  pen.setFlag(ScribblePen::LINE_DRAWING, cbLineDrawing->isChecked());
  pen.setFlag(ScribblePen::EPHEMERAL, cbEphemeral->isChecked());

  int tip = comboPenTip->index();
  pen.clearAndSet(ScribblePen::TIP_MASK,
      tip == 0 ? ScribblePen::TIP_FLAT :
      tip == 1 ? ScribblePen::TIP_ROUND : ScribblePen::TIP_CHISEL);

  bool varw = cbRatio->isChecked();
  bool chisel = tip == 2;
  pen.setFlag(ScribblePen::WIDTH_PR, varw && cbPrPrm->isChecked());
  pen.setFlag(ScribblePen::WIDTH_SPEED, varw && cbMaxSp->isChecked());
  pen.setFlag(ScribblePen::WIDTH_DIR, varw && cbAngle->isChecked());

  pen.wRatio = varw ? std::max(0.0, std::min(spinRatio->value()/100.0, 1.0)) : 0;
  pen.prParam = pen.hasFlag(ScribblePen::WIDTH_PR) && spinPrPrm->value() != 0 ? 1.0/spinPrPrm->value() : 0.0;
  pen.spdMax = pen.hasFlag(ScribblePen::WIDTH_SPEED) ? spinMaxSp->value() : 0;
  pen.dirAngle = pen.hasFlag(ScribblePen::WIDTH_DIR) ? spinAngle->value() : 0;
  pen.dash = !varw && !chisel ? spinDash->value() : 0;
  pen.gap = !varw && !chisel ? spinGap->value() : 0;

  updateWidthPicker(varw, chisel);

  penPreview->setPen(pen);
  if(onChanged)
    onChanged(PEN_CHANGED);
}

void PenToolbar::updateWidthPicker(bool varw, bool chisel)
{
  spinRatio->setEnabled(varw);
  spinPrPrm->setEnabled(cbPrPrm->isChecked());
  spinMaxSp->setEnabled(cbMaxSp->isChecked());
  spinAngle->setEnabled(cbAngle->isChecked());

  rowPrPrm->setVisible(varw);
  rowMaxSp->setVisible(varw);
  rowAngle->setVisible(varw);
  rowDash->setVisible(!varw && !chisel);
  rowGap->setVisible(!varw && !chisel);
}
