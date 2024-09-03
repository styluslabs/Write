#include "linkdialog.h"
#include "ugui/textedit.h"
#include "mainwindow.h"
#include "scribblewidget.h"
#include "scribbledoc.h"
#include "scribbleapp.h"


LinkDialog::LinkDialog(ScribbleDoc* sd) : Dialog(createDialogNode()), scribbleDoc(sd)
{
  ScribbleArea* scribbleArea = scribbleDoc->activeArea;
  // need to get: color, width, type (link/bookmark), target or id
  // TODO: getPen should return color = NONE and width < 0 if multiple values
  //ScribblePen pen = scribbleArea->getPen();
  StrokeProperties props(Color::INVALID_COLOR, -1);
  Selection* sel = scribbleArea->currSelection;
  if(sel)
    props = sel->getStrokeProperties();
  // NULL if not a hyperref
  const char* href = scribbleArea->getHyperRef();
  // NULL if not a bookmark
  //const char* idstr = scribbleArea->getIdStr();

  Rect pbbox = ScribbleApp::win->winBounds();
  setWinBounds(Rect::centerwh(pbbox.center(), std::min(pbbox.width(), Dim(500)), pbbox.height() - 60));

  // linkColor might make sense as per-doc option, but there's no clear way in UI to set global vs. per-doc
  Color linkcolor = Color::fromArgb(ScribbleApp::cfg->Int("linkColor"));
  penColorPicker = createColorEditBox();
  //widthComboBox = new PenWidthBox;
  //widthComboBox->setValue(MAX(Dim(0), props.width));

  hrefEdit = createTextEdit();
  hrefEdit->node->setAttribute("box-anchor", "hfill");  // fill available width ...
  hrefEdit->setMargins(0, 0, 0, 16);  // ... save for a bit of a gap
  if(href && href[0])
    hrefEdit->setText(href);

  // ScribbleWidget does not own it's ScribbleView (since it is normally owned by ScribbleDoc)
  bookmarkSelect.reset(new BookmarkSelect(this, scribbleDoc->cfg, scribbleDoc));

  Widget* dialogBody = selectFirst(".body-container");
  dialogBody->setMargins(0, 8);
  dialogBody->addWidget(createTitledRow(_("Color"), penColorPicker));
  dialogBody->addWidget(createTitledRow(_("Enter URL:"), hrefEdit));
  dialogBody->addWidget(createTitledRow(_("or choose bookmark:"), createStretch()));
  //dialogBody->addWidget(new Widget(createTextNode("or choose bookmark:")));

  Widget* bkmkAreaContainer = new Widget(new SvgG());
  //bkmkAreaContainer->node->addAttribute("box-anchor", "fill");
  bkmkAreaContainer->node->setAttribute("layout", "box");
  bkmkAreaContainer->node->setAttribute("box-anchor", "fill");
  dialogBody->addWidget(bkmkAreaContainer);
  // set min size
  //bkmkAreaContainer->addWidget(new Widget(loadSVGFragment(R"(<rect fill="none" width="480" height="640"/>)")));
  ScribbleWidget* bookmarkWidget = ScribbleWidget::create(bkmkAreaContainer, bookmarkSelect.get());
  //bookmarkWidget->node->addAttribute("box-anchor", "fill");
  bkmkAreaContainer->setMargins(8, 8);

  // color picker must be added to document before setting color
  penColorPicker->setColor(linkcolor.isValid() ? linkcolor : props.color);

  setTitle(_("Create Link"));
  acceptBtn = addButton(_("OK"), [this](){ accept(); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
  // I don't think we want this, because on-screen keyboard could block part of dialog
  //focusedWidget = hrefEdit;
}

void LinkDialog::bookmarkSelected(Element* b)
{
  std::string id(b ? b->nodeId() : "");
  if(!id.empty())
    hrefEdit->setText(("#" + id).c_str());
  else
    hrefEdit->setText("");  //"<bookmark>"
}

void LinkDialog::accept()
{
  ScribbleArea* scribbleArea = scribbleDoc->activeArea;
  Color c = penColorPicker->color();
  //float w = widthComboBox->value();
  StrokeProperties props(c, -1);  // w <= 0 ? -1 : w);
  ScribbleApp::cfg->set("linkColor", c.argb());

  if(!hrefEdit->text().empty())
    scribbleArea->setSelProperties(&props, hrefEdit->text().c_str());
  else
    scribbleArea->setSelProperties(&props, NULL, bookmarkSelect->bookmarkHit);

  finish(ACCEPTED);
}

// BookmarkSelect

BookmarkSelect::BookmarkSelect(LinkDialog* parent, ScribbleConfig* _cfg, ScribbleDoc* target)
    : BookmarkView(_cfg, target)
{
  //setScribbleDoc(target);
  linkDialog = parent;
  repaintBookmarks();  //target->document->ensurePagesLoaded();
}

BookmarkSelect::~BookmarkSelect()
{
  unHighlightHit();
}

void BookmarkSelect::doDblClickAction(Point pos)
{
  doClickAction(pos);
  if(bookmarkHit)
    linkDialog->accept();
}

bool BookmarkSelect::doClickAction(Point pos)
{
  pos = screenToDim(pos);
  unHighlightHit();  // clears bookmarkHit
  // returns document position given bookmark list y pos
  Element* b = findBookmark(scribbleDoc->document, pos.y);
  if(b) {
    bookmarkHit = b;
    highlightHit(pos);
    linkDialog->bookmarkSelected(bookmarkHit);
    return false;  // reject single click to allow for double click
  }
  linkDialog->bookmarkSelected(NULL);
  return true;
}
