#include "selection.h"
#include "document.h"
#include "basics.h"

// Selection:
// Basic procedure:
// - search target Page for Elements not yet selected and within new selection region
// - search selection list for Elements outside new selection region
// In order to draw selected strokes with the proper z-order, they are drawn normally, but Element:applyStyle
//  checks to see if the strokes are selected and alters style if so.

// Erasing: - stoke and ruled erase are implemented as selections from which strokes cannot be removed
// Stroke eraser: we use Selection::selectPath which for now uses only the end points of the path
//  (so we should be breaking up long lines), which, in turn, calls Stroke::isNearPoint, which also only
//  considers the stroke's points, so we need to avoid long lines here too; this will also make implementing
//  free erase easier (we just delete the nearby segments instead of the whole stroke.
// Ruled eraser - currently, this works exactly like Ruled selection, but we may want an option to not jump
//  lines
// Free eraser: implementation is in ScribbleArea mostly
// backtracking on erasure: only makes sense for ruled erase (basically just deselect = true), and not worth
//  the extra complication (for the user) - one can just do ruled select + cut or delete

// Dealing with different types of selections:  For now, all selections are converted to rect selections once
//  they are made (i.e. on release of selection tool).  The different drawing of selected strokes still makes
//  it clear which strokes within the rectangle are selected.  Reasons:
// - every other drawing program does it
// - makes available the resize and rotate functionality of rect selections
// This also solves several problems with manipulating other selection types:
// - moving ruled selections by any offset in x, and by y offset not a multiple of y ruling
// - what background/type to use for pasted selection, inverted selection

// Selector approach - a bit ugly, but ...
// - allows us to change selection background/type without copying stroke list
// - allows us to separate quite different code found in different selectors
// Alternatives:
// - rectSelector = new RectSelector(); currSelection->setSelector(rectSelector); ... pretty minor change
// - currSelection->select(new RuledSelector(...)); ... but this doesn't allow for history!

Selection::Selection(Page* source, StrokeDrawType drawtype) : m_drawType(drawtype)
{
  page = source;
  sourceNode = static_cast<Element*>(page->contentNode->ext());
}

/*Selection::Selection(const Selection* sel, Page* page) : Selection(page)
{
  for(Element* s : sel->strokes) {
    strokes.push_back(s->cloneNode());
    strokes.back()->node->setParent(NULL);
  }
}*/

Selection::~Selection()
{
  if(selector)
    delete selector;
  clear();
}

void Selection::clear()
{
  // unselect all strokes
  invalidateBBox();
  if(selMode != SELMODE_PASSIVE) {
    for(Element* node : strokes) {
      node->setSelected(NULL);
      node->resetTransform();
    }
  }
  strokes.clear();
}

void Selection::deleteStrokes()
{
  for(Element* s : strokes) {
    s->setSelected(NULL);
    page->removeStroke(s);
  }
  // delete our list of now invalid strokes
  strokes.clear();
  invalidateBBox();
  maxTimestamp = 0;
}

void Selection::addStroke(Element* s, Element* next)
{
  s->setSelected(this);
  if(next)
    strokes.insert(std::find(strokes.begin(), strokes.end(), next), s);
  else
    strokes.push_back(s);
  if(bbox.isValid())
    bbox.rectUnion(s->bbox());
  maxTimestamp = 0;
}

bool Selection::removeStroke(Element* s)
{
  s->setSelected(NULL);
  auto it = std::find(strokes.begin(), strokes.end(), s);
  if(it == strokes.end())
    return false;
  strokes.erase(it);
  invalidateBBox();
  maxTimestamp = 0;
  return true;
}

void Selection::selectAll()
{
  invalidateBBox();
  maxTimestamp = 0;
  for(Element* s : sourceNode->children()) {
    s->setSelected(this);
    strokes.push_back(s);
  }
}

void Selection::invertSelection()
{
  strokes.clear();
  invalidateBBox();
  maxTimestamp = 0;
  // invert selection flag and add newly selected strokes to list
  for(Element* s : sourceNode->children()) {
    if(!s->isSelected(this)) {
      s->setSelected(this);
      strokes.push_back(s);
    }
    else
      s->setSelected(NULL);
  }
}

// ideally, would like to avoid handling each stroke property explicitly, but don't see any
//  easy way to do this
// If current strokes have multiple values for a property, we ignore that property
// - e.g. to select all red strokes, we select one, then do select similar, notice that some are not
//  selected (due to difference in other properties), select one of those and run select similar again
//  ... interesting, but need a way for user to specify which properties to ignore manually
void Selection::selectbyProps(Element* elproto, bool useColor, bool useWidth)
{
  SCRIBBLE_LOG("selectByProps disabled!!!");
  /*if(!elproto) {
    if(strokes.empty())
      return;
    // TODO: iterate over all strokes and clear color, width flags if any differ from proto
    elproto = strokes.front();
  }
  if(!elproto->isA(Element::STROKE))
    return;
  Stroke* proto = (Stroke*)elproto;

  Rect dirty;
  invalidateBBox();
  // not really possible to deselect, I guess
  for(Element* s : sourceNode->renderers()) {
    if(s->strokeFlags == proto->strokeFlags && !s->isSelected(this)) {
      if((!useColor || s->color == proto->color) && (!useWidth || s->width == proto->width)) {
        s->setSelected(this);
        dirty.rectUnion(s->bbox);
        strokes.push_back(s);
      }
    }
  }
  growDirtyRect(dirty);*/
}

void Selection::recalcTimeRange()
{
  if(maxTimestamp != 0)
    return;
  minTimestamp = MAX_TIMESTAMP;
  maxTimestamp = 0;
  for(Element* s : strokes) {
    minTimestamp = std::min(minTimestamp, s->timestamp());
    maxTimestamp = std::max(maxTimestamp, s->timestamp());
  }
}

// Note that dirtyRect can be larger than select region - consider ruled selection; also fat strokes!
void Selection::doSelect()
{
  if(selMode == SELMODE_NONE)
    return;
  // remove deselected strokes from list and clear selected flag
  Rect dirty;
  invalidateBBox();
  maxTimestamp = 0;
  if(selMode != SELMODE_UNION) {
    for(auto ii = strokes.begin(); ii != strokes.end(); ) {
      if(!selector->selectHit(*ii)) {
        (*ii)->setSelected(NULL);
        dirty.rectUnion((*ii)->bbox());
        ii = strokes.erase(ii);  // returns iterator pointing to next element
      }
      else
        ++ii;
    }
  }
  // set selected flag for selected strokes and add to list
  for(Element* node : sourceNode->children()) {
    // fetch bbox and compare ourselves or Stroke::isContained(...)
    // note that we don't steal elements from another selection (selection() must be NULL)
    if((!node->selection() || selMode == SELMODE_PASSIVE) && selector->selectHit(node)) {
      if(selMode != SELMODE_PASSIVE)
        node->setSelected(this);
      dirty.rectUnion(node->bbox());
      strokes.push_back(node);
    }
  }
}

Rect Selection::getBBox()
{
  if(!bbox.isValid()) {
    bbox = Rect();
    for(Element* s : strokes)
      bbox.rectUnion(s->bbox());
  }
  return bbox;
}

void Selection::translate(Dim dx, Dim dy)
{
  applyTransform(ScribbleTransform().translate(dx, dy));
}

void Selection::setOffset(Dim x, Dim y)
{
  translate(x - transform.xoffset(), y - transform.yoffset());
}

void Selection::rotate(Dim radians, Point origin)
{
  applyTransform(ScribbleTransform().rotate(radians, origin));
}

void Selection::scale(Dim sx, Dim sy, Point origin, bool scalewidths)
{
  ScribbleTransform tf(Transform2D(sx, 0, 0, sy, (1 - sx)*origin.x, (1 - sy)*origin.y),
      scalewidths ? 1 : 1/sx, scalewidths ? 1 : 1/sy);
      //scalewidths ? sx : SGN(sx), scalewidths ? sy : SGN(sy));
  applyTransform(tf);
}

void Selection::resetTransform()
{
  if(selector)
    selector->transform(transform.inverse());
  transform.reset();
  for(Element* s : strokes)
    s->resetTransform();
  invalidateBBox();
  // we used to just do this, but it doesn't handle reseting reflowed text
  //applyTransform(transform.inverse());
}

void Selection::applyTransform(const ScribbleTransform& tf)
{
  if(tf.isIdentity())
    return;
  transform = tf * transform;
  for(Element* stroke : strokes)
    stroke->applyTransform(tf);
  invalidateBBox();
  if(selector)
    selector->transform(tf);
}

Rect Selection::getBGBBox()
{
  return selector ? selector->getBGBBox() : Rect();
}

void Selection::setZoom(Dim zoom)
{
  if(selector)
    selector->setZoom(zoom);
}

// NOTE: caller is expected to call shrink() after this to update selection background!
void Selection::commitTransform()
{
  UndoHistory* hist = page->document->history;
  bool istf = !transform.isIdentity() && !transform.isTranslate();
  for(Element* node : strokes) {
    // complex transform (istf) percludes possibility of per-stroke offsets
    if(hist->undoable()) {
      if(istf)
        hist->addItem(new StrokeTransformItem(node, page, transform));
      else if(node->pendingTransform().xoffset() != 0 || node->pendingTransform().yoffset() != 0)
        hist->addItem(new StrokeTranslateItem(node, page, node->pendingTransform().xoffset(), node->pendingTransform().yoffset()));
    }
    node->commitTransform();
  }
  transform.reset();
}

// transform strokes without dirtying page or writing undo items
void Selection::stealthTransform(const ScribbleTransform& tf)
{
  //transform = tf * transform; -- don't think we want this
  for(Element* s : strokes) {
    s->applyTransform(tf);
    s->commitTransform();
  }
  invalidateBBox();
}

void Selection::shrink()
{
  if(selector)
    selector->shrink();
}

bool Selection::xchgBGDirty(bool b)
{
  return selector ? std::exchange(selector->bgDirty, b) : false;
}

void Selection::setDrawType(StrokeDrawType newtype)
{
  if(m_drawType != newtype) {
    for(Element* s : strokes) {
      if(newtype == STROKEDRAW_NONE || m_drawType == STROKEDRAW_NONE)
        s->node->setDisplayMode(newtype == STROKEDRAW_NONE ? SvgNode::AbsoluteMode : SvgNode::BlockMode);
      s->node->setDirty(SvgNode::PIXELS_DIRTY);
    }
    m_drawType = newtype;
  }
}

// clipboard must preserve z-order (doesn't matter for other Selection operations, which is why we don't
//  keep our stroke list sorted)
void Selection::toSorted(Clipboard* dest) const
{
  for(Element* s : sourceNode->children()) {
    if(s->isSelected(this)) {
      if(s->node->type() == SvgNode::USE) {
        // make sure <use> target is available in clipboard
        const char* href = static_cast<SvgUse*>(s->node)->href();
        const SvgNode* target = static_cast<SvgUse*>(s->node)->target();
        if(target && !dest->content->namedNode(href)) {
          // wrap in <defs> to prevent separate drawing of target, transform being added to <symbol>, etc.
          SvgDefs* wrap = new SvgDefs();
          wrap->addChild(target->clone());
          dest->addStroke(new Element(wrap));
        }
      }
      dest->addStroke(s->cloneNode());
    }
  }
}

StrokeProperties Selection::getStrokeProperties() const
{
  return StrokeProperties::get(strokes.begin(), strokes.end());
}

void Selection::setStrokeProperties(const StrokeProperties& props)
{
  // make a copy of strokes rather than trying to modify while iterating; could consider instead a
  //  Selection::replaceStroke() to replace a stroke with a clone
  std::vector<Element*> strokescopy(strokes.begin(), strokes.end());
  for(Element* s : strokescopy) {
    // if node doesn't have fill attribute, easier to replace it than handle it as a special case
    bool nofill = props.color.alpha() > 0 && !s->node->getAttr("fill");
    // since setProperties descends recursively into groups (container or <text> w/ <tspan>s) we need to
    //  replace group so we can undo properly
    if(s->containerNode() || s->node->type() == SvgNode::TEXT || nofill) {
      Element* t = s->cloneNode();
      page->addStroke(t, s);
      addStroke(t);
      removeStroke(s);
      page->removeStroke(s);
      t->setProperties(props);
    }
    else {
      StrokeChangedItem* undoitem = new StrokeChangedItem(s, page);
      if(s->setProperties(props))
        page->document->history->addItem(undoitem);
      else
        delete undoitem;
    }
  }
}

bool Selection::containsGroup()
{
  for(Element* s : strokes) {
    if(s->containerNode() && (s->node->type() == SvgNode::G || s->node->type() == SvgNode::DOC))
      return true;
  }
  return false;
}

void Selection::ungroup()
{
  // easiest way to avoid complication of modifying strokes list while iterating is to make a copy
  std::vector<Element*> strokescopy(strokes.begin(), strokes.end());
  for(Element* s : strokescopy) {
    // don't break up <defs> or <pattern>!
    if(s->containerNode() && (s->node->type() == SvgNode::G || s->node->type() == SvgNode::DOC)) {
      Transform2D tf = s->node->totalTransform();
      for(SvgNode* n : s->containerNode()->children()) {
        SvgNode* m = n->clone();
        // multistroke children will have Elements, but nodes in other (i.e. opaque) groups won't
        Element* t = m->hasExt() ? static_cast<Element*>(m->ext()) : new Element(m);
        page->addStroke(t, s);
        if(!tf.isIdentity()) {  //s->node->hasTransform()) {
          t->applyTransform(tf);  //s->node->getTransform());
          t->commitTransform();
        }
        // transfer attributes not overridden - but only standard attributes
        for(const SvgAttr& attr : s->node->attrs) {
          if(!t->node->getAttr(attr.name()) && attr.stdAttr() != SvgAttr::UNKNOWN)
            t->node->setAttr(attr);
        }
        // should we also copy class?
        addStroke(t);
      }
      removeStroke(s);
      page->removeStroke(s);  // delete original
    }
  }
}

// for overlay
void Selection::draw(Painter* painter, StrokeDrawType drawtype)  //const Rect& dirty)
{
  auto oldDrawType = m_drawType;
  m_drawType = drawtype != STROKEDRAW_NONE ? drawtype : m_drawType;
  SvgPainter svgp(painter);
  for(Element* s : strokes)
    svgp.drawNode(s->node);  //, dirty);
  m_drawType = oldDrawType;
}

// stuff for reflow

// this is a bit of a hack ... we return the ruleline of the first stroke after sorting
int Selection::sortRuled()
{
  if(strokes.empty()) return -1;
  strokes.sort(page->cmpRuled());
  return page->getLine(strokes.front());
}

// strokes should be sorted if dx != 0
void Selection::insertSpace(Dim dx, int dline)
{
  if(strokes.size() > 0 && dx != 0) {
    auto ii = strokes.begin();
    int currline = page->getLine(*ii);
    ScribbleTransform tf(Transform2D::translating(dx, 0));
    for(; ii != strokes.end() && page->getLine(*ii) == currline; ++ii)
      (*ii)->applyTransform(tf);  //translateStroke(*ii, dx, 0);
    invalidateBBox();
  }
  translate(0, dline * page->yruling(true));
}

// To enable realtime reflow w/o unnecessary drawing and bounds update, we use Element.scratch to store offset
//  for in-progress reflow, then compare to previous offset (from pendingTransform), updating iff different
static Rect workingBBox(Element* s)
{
  const ScribbleTransform& oldtf = s->pendingTransform();
  return s->bbox().translate(s->scratch.x - oldtf.xoffset(), s->scratch.y - oldtf.yoffset());
}

static int workingLine(Element* s, Page* page)
{
  const ScribbleTransform& oldtf = s->pendingTransform();
  return page->getLine(s->com().y + s->scratch.y - oldtf.yoffset());
}

// Although reflow might not work well for long paragraphs on unruled pages, don't really see any harm
//  enabling it since alternative is a bunch of strokes past edge of page; if it doesn't work, don't use it!
// TODO: consider rounding dx, nextdx to integers!
void Selection::reflowStrokes(Dim dx, int dline, Dim minWordSep)
{
  const Dim yruling = page->yruling(true);
  if(yruling == 0 || strokes.empty() || workingLine(strokes.front(), page) + dline < 0)
    return;
  if(dx != 0) {
    int currline = workingLine(strokes.front(), page);
    for(auto ii = strokes.begin(); ii != strokes.end() && workingLine(*ii, page) == currline; ++ii)
      (*ii)->scratch.x = dx;
  }
  if(dline != 0) {
    Dim dy = dline * yruling;
    for(Element* s : strokes)
      s->scratch.y = dy;
  }

  const Dim minWordGap = minWordSep * yruling;
  auto curr = strokes.begin();
  auto wordbreak = strokes.begin();
  // TODO: should we init currRight to left instead of 0?
  dx = 0;
  Dim nextdx = 0, currRight = 0;
  // rule line of first stroke
  int currline = workingLine(*curr, page);
  Dim left = page->props.marginLeft;
  Dim right = page->width() - 0.5*minWordGap;
  while(1) {
    if(currline+1 < int(((RuledSelector*)selector)->lstops.size())) {
      left = MAX(page->props.marginLeft, static_cast<RuledSelector*>(selector)->lstops[currline+1]);
      right = static_cast<RuledSelector*>(selector)->rstops[currline] - 0.5*minWordGap;
    }
    // Step 1: find last word break before overflow
    while(1) {
      currRight = MAX(currRight, workingBBox(*curr).right);
      if(currRight >= right)
        break;
      curr++;
      // no strokes beyond right on this line means we are all done reflowing
      if(curr == strokes.end() || workingLine(*curr, page) != currline)
        goto alldone;
      if(workingBBox(*curr).left - currRight >= minWordGap)
        wordbreak = curr;
    }
    // no word break found - abort; probably should notify user
    if(wordbreak == strokes.end())
      goto alldone;
    // Step 1.5: find first stroke of next line so we can start strokes moved down at same position
    //  if the next line is empty, we'll start strokes moved down at (left + minWordGap)
    dx = left + minWordGap;
    while(++curr != strokes.end()) {
      if(workingLine(*curr, page) == currline)
        continue;
      if(workingLine(*curr, page) == currline + 1)
        dx = workingBBox(*curr).left;
      break;
    }
    // Step 2: move all strokes incl and beyond wordbreak down to next line
    curr = wordbreak;
    dx -= workingBBox(*wordbreak).left;
    nextdx = 0;
    bool insblankline = false;
    while(curr != strokes.end() && workingLine(*curr, page) == currline) {
      (*curr)->scratch += Point(dx, yruling);  //translateStroke(*curr, dx, yruling);
      // note we want post-translation bbox here
      nextdx = MAX(nextdx, workingBBox(*curr).right);
      curr++;
      insblankline = true;  // 1 or more strokes moved down
    }
    // Step 3: shift all strokes on next line right to accommodate strokes moved down
    currline++;
    while(curr != strokes.end() && workingLine(*curr, page) == currline) {
      // we use insblankline here to test for first pass through loop.  We insert a 1.25*minWordGap space
      //  between strokes moved down (which end at nextdx) and strokes already on the line. Recall that
      //  strokes are sorted by bbox.left
      if(insblankline)
        nextdx += (1.25*minWordGap) - workingBBox(*curr).left;
      (*curr)->scratch.x += nextdx;  //translateStroke(*curr, nextdx, 0);
      curr++;
      insblankline = false;  // line is not blank
    }
    // if strokes were moved down to a blank line, insert a new blank line below
    if(insblankline) {
      for(auto rest = curr; rest != strokes.end(); rest++)
        (*rest)->scratch.y += yruling;  //translateStroke(*rest, 0, yruling);
    }
    // start next iteration from first stroke of next line because we need to find
    //  next word break; result is that all strokes moved down will be processed twice
    curr = wordbreak;
    currRight = MIN_DIM;
    wordbreak = strokes.end();
  }
alldone:
  for(Element* s : strokes) {
    Point olddr = Point(s->pendingTransform().xoffset(), s->pendingTransform().yoffset());
    if(!approxEq(s->scratch, olddr, 0.01))  // numerical errors seem to accumulate for x
      s->applyTransform(ScribbleTransform().translate(s->scratch - olddr));
    s->scratch = Point(0, 0);
  }
  invalidateBBox();
}

// Clipboard

void Clipboard::paste(Selection* sel, bool move)
{
  content->replaceIds(sel->page->svgDoc.get());  // replace any ids that already exist in target document
  for(SvgNode* n : content->children()) {
    SvgNode* m = move ? n : n->clone();
    Element* t = m->hasExt() ? static_cast<Element*>(m->ext()) : new Element(m);
    sel->page->addStroke(t);
    sel->addStroke(t);
  }
  if(move) {
    content->children().clear();
    content.reset(new SvgDocument);
  }
}

void Clipboard::saveSVG(std::ostream& file)
{
  XmlStreamWriter xmlwriter;
  SvgWriter writer(xmlwriter);
  writer.serialize(content.get());
  xmlwriter.save(file);
}

// Selector
// note that failure to define all virtual methods may result in
//  "undefined reference to vtable" errors attributed to the constructor and destructor

Selector::Selector(Selection* _selection) : selection(_selection)
{
  selection->selector = this;
  // use blue selection for light backgrounds, yellow for dark
  bgStroke = selection->page->props.color.luma() > 127 ? Color::BLUE : Color::YELLOW;
  bgFill = Color(bgStroke).setAlphaF(0.20f);
}

Selector::~Selector()
{
  if(selection)
    selection->selector = NULL;
}

// PathSelector class

// no reason not to have reasonable behavior for all structure nodes
static bool isNearPoint(Point p, Dim radius, SvgNode* node)
{
  if(!node->bounds().pad(radius).contains(p))
    return false;
  if(node->asContainerNode()) {
    for(SvgNode* child : node->asContainerNode()->children())
      if(isNearPoint(p, radius, child))
        return true;
    return false;
  }
  if(node->type() == SvgNode::IMAGE) {  //&& !Element::ERASE_IMAGES)
    Rect bbox = node->bounds();
    return std::abs(p.x - bbox.left) < radius || std::abs(p.x - bbox.right) < radius
        || std::abs(p.y - bbox.top) < radius || std::abs(p.y - bbox.bottom) < radius;
  }
  if(node->type() != SvgNode::PATH)
    return true;  // bbox hit for non-path node

  const Path2D& path = *static_cast<SvgPath*>(node)->path();
  const Transform2D tf = node->totalTransform();
  Dim dist = tf.isIdentity() ? path.distToPoint(p) : Path2D(path).transform(tf).distToPoint(p);
  return dist < radius;
}

bool PathSelector::selectHit(Element* s)
{
  return isNearPoint(selPos, selRadius, s->node);
}

void PathSelector::selectPath(Point p, Dim radius)
{
  selRadius = radius;
  path.addPoint(p);
  // subdivide as necessary to get sufficient point density
  if(selPos.isNaN())
    selPos = p;
  Point dr = p - selPos;
  Dim d = dr.dist();
  int nsteps = int(d/(radius/2)) + 1;
  Point step = dr/nsteps;
  for(int ii = 0; ii < nsteps; ++ii) {
    selPos += step;
    selection->doSelect();
  }
  bgDirty = true;
}

Rect PathSelector::getBGBBox()
{
  return path.empty() ? Rect() : selection->transform.mult(path.controlPointRect().pad(selRadius));
}

// RectSelection class
Dim RectSelector::HANDLE_SIZE = 4;
Dim RectSelector::HANDLE_PAD = 4;  // can be changed from ScribbleArea::loadConfig()

bool RectSelector::selectHit(Element* s)
{
  switch(rectSelMode) {
  case RECTSEL_BBOX:
    return selRect.contains(s->bbox());
  default:
    return false;
  }
}

Rect RectSelector::getBGBBox()
{
  if(!selRect.isValid())
    return selRect;

  // assume scale is never combined with rotation
  if(selection->transform.isRotating()) {
    Rect r = selRect;
    r.pad(HANDLE_SIZE/mZoom);
    r.top -= 7*HANDLE_SIZE/mZoom;
    Point center = selection->transform.mult(r.center());
    Dim maxwidth = sqrt(r.width()*r.width() + r.height()*r.height());
    return Rect::centerwh(center, maxwidth, maxwidth);
  }
  // padding accounts for the grab handles
  Rect r = Rect(selRect).pad(HANDLE_SIZE/mZoom);  //selection->transform.mult(selRect).pad(HANDLE_SIZE/mZoom);
  if(drawHandles)
    r.top -= 7*HANDLE_SIZE/mZoom;  // rotation handle
  return r;
}

// In the future, we can optimize selection by only looking for strokes in previously unselected region
// RectSelector does not use bgDirty - instead, user needs to check if BG bbox has changed
void RectSelector::selectRect(Dim x0, Dim y0, Dim x1, Dim y1)
{
  // ensure correct corners for selection rectangle (left, top, right, bottom)
  selRect = Rect::ltrb(MIN(x0, x1), MIN(y0, y1), MAX(x0, x1), MAX(y0, y1));
  selection->doSelect();
}

void RectSelector::shrink()
{
  selRect = selection->getBBox();
  // The absence of this check was causing a strange bug that only manifested on Android (could not reproduce
  //  on PC, even with Dim defined as a float).  Perhaps some subtle floating point thing, since an invalid
  //  rect uses +/-FLT_MAX (maybe we should change this)
  if(selRect.isValid()) {
    // min selection size is a multiple of resize handle size (to ensure there is a place to drag)
    Dim padx = (4*HANDLE_PAD/mZoom - selRect.width())/2;
    Dim pady = (4*HANDLE_PAD/mZoom - selRect.height())/2;
    selRect.pad(std::max(Dim(1.0), padx + 1), std::max(Dim(1.0), pady + 1));
  }
  enableCrop = selection->count() == 1 && selection->strokes.front()->node->type() == SvgNode::IMAGE;
}

// check for hit on rect selection handles used for resizing
// We return the scaling origin point, which is the corner opposite the handle grabbed!
Point RectSelector::scaleHandleHit(Point pos, bool touch)
{
  if(!drawHandles)
    return Point(NaN, NaN);
  Dim h = touch ? 2*HANDLE_SIZE : HANDLE_SIZE;
  Dim a = (h+3)/mZoom; // +3 is to make it easier to grab handle
  // use selection bbox, not BG bbox for scaling origin!
  Rect bbox = selRect;  //selection->getBBox();
  Rect handlerect = Rect::ltrb(-a, -a, a, a);
  if(Rect(handlerect).translate(selRect.left, selRect.top).contains(pos))
    return Point(bbox.right, bbox.bottom);
  if(Rect(handlerect).translate(selRect.right, selRect.top).contains(pos))
    return Point(bbox.left, bbox.bottom);
  if(Rect(handlerect).translate(selRect.left, selRect.bottom).contains(pos))
    return Point(bbox.right, bbox.top);
  if(Rect(handlerect).translate(selRect.right, selRect.bottom).contains(pos))
    return Point(bbox.left, bbox.top);
  return Point(NaN, NaN);
}

Point RectSelector::rotHandleHit(Point pos, bool touch)
{
  if(!drawHandles)
    return Point(NaN, NaN);
  Dim h = touch ? 2*HANDLE_SIZE : HANDLE_SIZE;
  Dim a = (h+3)/mZoom;
  Rect handlerect = Rect::ltrb(-a, -a, a, a);
  if(handlerect.translate(selRect.center().x, selRect.top - 6*HANDLE_SIZE/mZoom).contains(pos))
    return selRect.center();
  return Point(NaN, NaN);
}

Point RectSelector::cropHandleHit(Point pos, bool touch)
{
  if(!drawHandles || !enableCrop)
    return Point(NaN, NaN);
  Dim h = touch ? 2*HANDLE_SIZE : HANDLE_SIZE;
  Dim a = (h+3)/mZoom; // +3 is to make it easier to grab handle
  Rect handlerect = Rect::ltrb(-a, -a, a, a);
  Rect bbox = selection->getBBox();  //.toSize();
  if(Rect(handlerect).translate(selRect.left, selRect.center().y).contains(pos))
    return Point(bbox.right, NaN);  //Point(-1, 0);
  if(Rect(handlerect).translate(selRect.right, selRect.center().y).contains(pos))
    return Point(bbox.left, NaN);  //Point(1, 0);
  if(Rect(handlerect).translate(selRect.center().x, selRect.top).contains(pos))
    return Point(NaN, bbox.bottom);  //Point(0, -1);
  if(Rect(handlerect).translate(selRect.center().x, selRect.bottom).contains(pos))
    return Point(NaN, bbox.top);  //Point(0, 1);
  return Point(NaN, NaN);
}

void RectSelector::setZoom(Dim zoom)
{
  if(mZoom != zoom) {
    mZoom = zoom;
    shrink();
  }
}

// We tried using corner of contents bbox instead of sel BG bbox as scaling origin, and shrink()ing BG during
//  scaling to handle content growing larger than bbox (when not scaling stroke width), but this then causes
//  sel BG to move significantly when scaling stroke width, and cursor to become separated from scale handle.
// Given the complexity with stroke width, I think the best approach is to make sel BG to behave nicely when
//  scaling.
void RectSelector::transform(const Transform2D& tf)
{
  if(!tf.isRotating())
    selRect = tf.mapRect(selRect);  //shrink();
}

// RuledSelection class

bool RuledRange::isSingleLine() const
{
  return ymin + yruling >= ymax;
}

bool RuledRange::isValid() const
{
  return !isSingleLine() || (ymin < ymax && x0 <= x1);
}

RuledRange& RuledRange::translate(Dim dx, Dim dy)
{
  x0 += dx;
  x1 += dx;
  ymin += dy;
  ymax += dy;
  lstops.clear();
  rstops.clear();
  return *this;
}

Dim RuledRange::lstop(int idx) const
{
  if(idx >= 0 && idx < int(lstops.size()))
    return lstops[idx];
  if(idx == 0)
    return x0;
  return (idx >= 0 && idx < nlines()) ? MIN_DIM : MIN_DIM;  //MAX_X_DIM
}

Dim RuledRange::rstop(int idx) const
{
  if(idx >= 0 && idx < int(rstops.size()))
    return rstops[idx];
  if(idx == nlines() - 1)
    return x1;
  return (idx >= 0 && idx < nlines()) ? MAX_DIM : MAX_DIM;  //MIN_X_DIM
}

Dim RuledSelector::MIN_DIV_HEIGHT = 2;

// If findStops() did not run (e.g., due to cursor down in left margin) left = MIN_X_DIM and strokes in the
//  left margin will be selected and manipulated.  If findStops() did run, strokes in left margin will not be
//  selected - I think this is the desired behavior

static bool containedRuled(const RuledRange& range, Element* s)
{
  if(s->isMultiStroke()) {
    for(Element* ss : s->children())
      if(!containedRuled(range, ss))
        return false;
    return true;
  }
  Rect bbox = s->bbox();
  if(!s->isPathElement()) {
    if(bbox.top < range.ymin || bbox.bottom >= range.ymax)
      return false;

    int lastline = floor((bbox.bottom - range.ymin)/range.yruling);
    int ii = floor((bbox.top - range.ymin)/range.yruling);
    for(; ii <= lastline; ++ii) {
      if(bbox.left < range.lstop(ii) || bbox.right > range.rstop(ii))
        return false;
    }
    return true;
  }

  if(bbox.height() < 1.75*range.yruling) {
    int dl = floor((s->com().y - range.ymin)/range.yruling);
    return dl >= 0 && dl < range.nlines() && bbox.left >= range.lstop(dl) && bbox.right <= range.rstop(dl);
  }
  if(bbox.top < range.ymin - 0.25*range.yruling || bbox.bottom > range.ymax + 0.25*range.yruling)
    return false;

  const Path2D& path = *static_cast<SvgPath*>(s->node)->path();
  PathPointIter pts(path, s->node->totalTransform(), range.yruling/8);
  while(pts.hasNext()) {
    Point p = pts.next();
    if(p.y < range.ymin || p.y >= range.ymax) {
      // require that stroke extend at least 25% of the way through line to be considered on it
      if(p.y < range.ymin - 0.25*range.yruling || p.y > range.ymax + 0.25*range.yruling)
        return false;
    }
    else {
      int dl = floor((p.y - range.ymin)/range.yruling);
      if(p.x < range.lstop(dl) || p.x > range.rstop(dl))
        return false;
    }
  }
  return true;
}

static bool overlapRuled(const RuledRange& range, Element* s)
{
  if(s->isMultiStroke()) {
    for(Element* ss : s->children())
      if(overlapRuled(range, ss))
        return true;
    return false;
  }
  Rect bbox = s->bbox();
  if(!s->isPathElement()) {
    if(bbox.bottom < range.ymin || bbox.top >= range.ymax)
      return false;
    int l0 = int(floor((bbox.top - range.ymin)/range.yruling));
    int l1 = int(floor((bbox.bottom - range.ymin)/range.yruling));
    if(s->node->type() == SvgNode::IMAGE)
      return (l0 >= 0 && l0 < range.nlines() && bbox.left < range.rstop(l0) && bbox.right > range.lstop(l0))
          || (l1 >= 0 && l1 < range.nlines() && bbox.left < range.rstop(l1) && bbox.right > range.lstop(l1));
    // this would apply to text and opaque <g>s
    int lastline = std::min(l1, range.nlines() - 1);
    for(int ii = std::max(0, l0); ii <= lastline; ++ii) {
      if(bbox.left < range.rstop(ii) && bbox.right > range.lstop(ii))
        return true;
    }
    return false;
  }

  if(bbox.height() < 1.75*range.yruling) {
    int dl = floor((s->com().y - range.ymin)/range.yruling);
    return dl >= 0 && dl < range.nlines() && bbox.left < range.rstop(dl) && bbox.right > range.lstop(dl);
  }
  if(bbox.bottom < range.ymin + 0.25*range.yruling || bbox.top > range.ymax - 0.25*range.yruling)
    return false;

  const Path2D& path = *static_cast<SvgPath*>(s->node)->path();
  PathPointIter pts(path, s->node->totalTransform(), range.yruling/8);
  while(pts.hasNext()) {
    Point p = pts.next();
    if(p.y >= range.ymin && p.y < range.ymax) {
      int dl = floor((p.y - range.ymin)/range.yruling);
      if(p.x < range.rstop(dl) && p.x > range.lstop(dl))
        return true;
    }
  }
  return false;
}

bool RuledSelector::selectHit(Element* s)
{
  switch(selMode) {
  case SEL_CONTAINED:
    return containedRuled(selRange, s);
  case SEL_OVERLAP:
    return overlapRuled(selRange, s);
  }
  return false;
}

Rect RuledSelector::getBGBBox()
{
  if(selRange.isSingleLine())
    return Rect::ltrb(selRange.x0, selRange.ymin, selRange.x1, selRange.ymax);
  else
    return Rect::ltrb(0, selRange.ymin, selection->page->width(), selRange.ymax);
}

void RuledSelector::selectRuled(Dim x0, int line0, Dim x1, int line1)
{
  // always use x0,line0 as origin for findStops, even if it come after x1,line1
  if(line0 != line1)
    findStops(x0, line0);
  // see if we need to swap selection limits
  if(line0 > line1 || (line0 == line1 && x0 > x1)) {
    selectRuled(x1, line1, x0, line0);
    return;
  }

  Page* p = selection->page;
  selRange = RuledRange(x0, p->getYforLine(line0), x1, p->getYforLine(line1 + 1), p->props.xRuling, p->yruling(true));
  // if stops are present, add to range
  if(line0 >= 0 && line1 >= 0 && line0 < int(lstops.size())) {
    if(line1 >= int(rstops.size())) {
      line1 = rstops.size() - 1;
      x1 = rstops[line1];
    }
    selRange.lstops.push_back(MAX(lstops[line0], x0));
    for(int line = line0; line < line1; ) {
      selRange.rstops.push_back(rstops[line]);
      selRange.lstops.push_back(lstops[++line]);
    }
    selRange.rstops.push_back(MIN(rstops[line1], x1));
  }

  selection->doSelect();
  bgDirty = true;
}

void RuledSelector::selectRuled(const RuledRange& range)
{
  selRange = range;
  selection->doSelect();
  bgDirty = true;
}

// select all strokes to the right of x on rule line linenum, down to the end of the page
void RuledSelector::selectRuledAfter(Dim x, int linenum)
{
  selectRuled(x, linenum, MAX_DIM, MAX_LINE_NUM - 1);
}

// find column bounds for specified point
// must cover entire page because selection could be moved up (i.e. negative insert space)
// only run once, when ruled selection is created (should be fine as long as ruled selection gets converted
//  to rect selection on release)
void RuledSelector::findStops(Dim x, int linenum)
{
  Page* page = selection->page;
  // preserve old behavior (applying to entire line) when cursor down in left margin and don't run twice
  // also, disable stops for eraser (SELMODE_UNION)
  if(colMode == COL_NONE || selection->selMode == Selection::SELMODE_UNION
      || x < page->props.marginLeft || !lstops.empty())
    return;

  Rect margins = Rect::ltrb(0, 0, page->width(), page->height());
  // = page->ruleLayer->getMargins(x, page->getYforLine(linenum), Rect(0, 0, page->width, page->height));
  const Dim yruling = page->yruling(true);
  const int nlines = page->height()/yruling;
  if(nlines < 1) return;
  lstops.resize(nlines, margins.left);
  rstops.resize(nlines, margins.right);
  // only want to scan stroke list once
  for(Element* s : selection->sourceNode->children()) {
    if(s->bbox().height() > MIN_DIV_HEIGHT*yruling) {
      // conservative mode requires that stroke passes through linenum
      if((colMode == COL_NORMAL)
          && (page->getLine(s->bbox().top) > linenum || page->getLine(s->bbox().bottom) < linenum))
        continue;  // go to next stroke
      // require stroke extend at least 1/4 of the way into line to form a barrier
      int line0 = MAX(0, page->getLine(s->bbox().top + 0.25*yruling));
      int line1 = MIN((int)lstops.size()-1, page->getLine(s->bbox().bottom - 0.25*yruling));
      for(int ll = line0; ll <= line1; ll++) {
        if(!s->isPathElement()) {
          if(s->bbox().left > x)
            rstops[ll] = MIN(s->bbox().left, rstops[ll]);
          else if(s->bbox().right < x)
            lstops[ll] = MAX(s->bbox().right, lstops[ll]);
          else
            break;  // go to next stroke
        }
        // before processing each point of stroke, check bbox to see if it can move bounds
        else if(s->bbox().left < rstops[ll] || s->bbox().right > lstops[ll]) {
          const Path2D& path = *static_cast<SvgPath*>(s->node)->path();
          PathPointIter pts(path, s->node->totalTransform(), yruling/8);
          while(pts.hasNext()) {
            Point p = pts.next();
            int pline = page->getLine(p.y);
            if(pline >= line0 && pline <= line1) {
              if(p.x > x)
                rstops[pline] = MIN(p.x, rstops[pline]);
              else if(p.x < x)
                lstops[pline] = MAX(p.x, lstops[pline]);
            }
          }
          break;  // stroke has been completely processed - go to next stroke
        }
      }
    }
  }
  // post processing - clear all stops below a line w/o stops
  // TODO: we may want to set stops so nothing below end of column is selected!
  bool clearstops = false;
  for(unsigned int ll = linenum; ll < lstops.size(); ll++) {
    if(clearstops) {
      lstops[ll] = margins.left;
      rstops[ll] = margins.right;
    }
    else if(lstops[ll] == margins.left && rstops[ll] == margins.right)
      clearstops = true;
  }
}

void RuledSelector::shrink()
{
  // not used since selections converted to rect sel, but if we did impl ...
  // ... sort stroke list, use first and last strokes to determine selection range
}

// Lasso selection

LassoSelector::LassoSelector(Selection* sel, Dim simplify) : Selector(sel), simplifyThresh(simplify)
{
  lasso.setFillRule(Path2D::EvenOddFill);  // just because this was the previous behavior
}

void LassoSelector::addPoint(Dim x, Dim y)
{
  // we store lasso as a closed path - last element is same as first
  if(lasso.size() > 0)
    lasso.points.pop_back();
  // calculate normals of the triangle defining the changed region of the selection
  if(lasso.size() > 1 && selection->selMode != Selection::SELMODE_NONE) {
    Dim sa, sb;
    Point a = lasso.points.front();
    Point b = lasso.points.back();
    norm1 = Point(a.y - y, x - a.x).normalize();
    norm2 = Point(b.y - y, x - b.x).normalize();
    norm3 = Point(a.y - b.y, b.x - a.x).normalize();

    // compute range of triangle along it's normals; sc := sanity check (ha ha)
    sa = norm1.x*x + norm1.y*y;  sb = norm1.x*b.x + norm1.y*b.y;  //sc = norm1.x*a.x + norm1.y*a.y;
    s1min = MIN(sa, sb);  s1max = MAX(sa, sb);
    sa = norm2.x*x + norm2.y*y;  sb = norm2.x*a.x + norm2.y*a.y;  //sc = norm2.x*b.x + norm2.y*b.y;
    s2min = MIN(sa, sb);  s2max = MAX(sa, sb);
    sa = norm3.x*a.x + norm3.y*a.y;  sb = norm3.x*x + norm3.y*y;  //sc = norm3.x*b.x + norm3.y*b.y;
    s3min = MIN(sa, sb);  s3max = MAX(sa, sb);

    // compute range of triangle along axes (which are bbox normals)
    txmin = MIN(x, MIN(a.x, b.x));
    txmax = MAX(x, MAX(a.x, b.x));
    tymin = MIN(y, MIN(a.y, b.y));
    tymax = MAX(y, MAX(a.y, b.y));

    checkCollision = !norm1.isZero() && !norm2.isZero() && !norm3.isZero();

    lasso.addPoint(x, y);
    // idea here is to process path in chunks (since selection is live, we have to simplify live to get any
    //  benefit) - we keep collecting points until at least one point (since last simplification) exceeds
    //  threshold, then simplify the path between 2nd to last point of prev simplication (not last point!)
    //  and current end point
    std::vector<Point> simp = simplifyRDP(lasso.points, nextSimpStart, lasso.size() - 1, simplifyThresh);
    if(simp.size() > 2) {
      lasso.points.erase(std::copy(simp.begin(), simp.end(), lasso.points.begin() + nextSimpStart), lasso.points.end());
      nextSimpStart = lasso.size() - 2;
    }
  }
  else
    lasso.addPoint(x, y);

  lasso.closeSubpath();
  lassoBBox.rectUnion(Point(x, y));
  selection->doSelect();
  bgDirty = true;
  checkCollision = false;
}

// Strokes contained entirely within selection region are "hits."  Two approaches:
// 1. even-odd rule: count the number of times a line segment from each point to infinity crosses the
//  the selection boundary.  If the number of crossings is even, the point and thus the stroke is rejected
// Counting crossings is accomplished by iterating over all line segments of the lasso
// 2. non-zero rule: determine the winding number of the lasso around each point.  This is always a multiple
//  of 2*pi for a closed path.  The point is enclosed iff the winding != 0.  We only have to keep track of
//  quadrants, not the exact angle:
// 2|3
// -+->x
// 1|0
//  v y
// We use the even-odd rule for lasso selection, mostly for legacy reasons (not to say non-zero rule is better)

// Rejected strokes will usually get rejected quickly, but normally we'd have to iterate over every point
//  of a stroke to accept it.  We've implemented the following optimization: in addPoint, we determine the
//  triangle representing the changed region of the lasso.  If a stroke's bbox does not intersect this
//  triangle, the stroke's selection state is unchanged.  Since both the triangle and the bbox are convex, we
//  check for itersection by projecting both shapes onto directions normal to an edge.  These are just the
//  x and y axes for the bbox.  The triangle normals can be in any direction, which is the main source of
//  complication here.  If the projections along any of the directions are disjoint, there is no intersection
// An alternate optimization is to check if lasso passes through stroke bbox; if not, we only have to test
//  one point of stroke.  We tried doing this by checking for lasso points inside bbox - this is incorrect,
//  especially with the long line segment closing the lasso.
// We we're also unnecessarily checking to see if each point is inside lasso bbox - this is always true if
//  the stroke bbox is inside lasso bbox

bool LassoSelector::projectRect(const Point& p, const Rect& r, Dim smin, Dim smax)
{
  Dim pr0 = p.x*r.left + p.y*r.top;
  Dim pr1 = p.x*r.left + p.y*r.bottom;
  Dim pr2 = p.x*r.right + p.y*r.top;
  Dim pr3 = p.x*r.right + p.y*r.bottom;
  Dim pmin = MIN(MIN(pr0, pr1), MIN(pr2, pr3));
  Dim pmax = MAX(MAX(pr0, pr1), MAX(pr2, pr3));
  return pmin < smax && pmax > smin;  // overlap?
}

bool LassoSelector::rectCollide(const Rect& r)
{
  // project triangle onto rect normals (i.e., the x and y axes)
  if(txmin > r.right || txmax < r.left || tymin > r.bottom || tymax < r.top)
    return false;  // no collision
  // project rectangle onto triangle normals
  return projectRect(norm1, r, s1min, s1max)
      && projectRect(norm2, r, s2min, s2max) && projectRect(norm3, r, s3min, s3max);
}

// for now, we will just just PainterPath::isEnclosedBy which only considers path points, and so may
//  incorrectly include some paths w/ parts of segments outside a concave lasso - if this turns out to be
//  a real issue, the easiest solution is probably to just use PathPointIter for subject path (easy since
//  subject path points are iterated over in outer loop)
static bool isEnclosedBy(const Path2D& lasso, SvgNode* node)
{
  if(node->asContainerNode()) {
    for(SvgNode* child : node->asContainerNode()->children())
      if(!isEnclosedBy(lasso, child))
        return false;
    return true;
  }
  if(node->type() == SvgNode::PATH) {
    const Path2D* path = static_cast<SvgPath*>(node)->path();
    Transform2D tf = node->totalTransform();
    return tf.isIdentity() ? path->isEnclosedBy(lasso) : Path2D(*path).transform(tf).isEnclosedBy(lasso);
  }
  else
    return Path2D().addRect(node->bounds()).isEnclosedBy(lasso);
}

// even-odd rule approach - we use a horizontal line from the stroke point to x = +infinity
bool LassoSelector::selectHit(Element* s)
{
  // reject if lasso bbox does not contain stroke bbox
  if(!lassoBBox.contains(s->bbox()) || lasso.size() < 1)
    return false;
  if(checkCollision && !rectCollide(s->bbox())) {
    //SCRIBBLE_LOG("Quick accept %f %f %f %f", txmin, tymin, txmax, tymax);
    return s->isSelected(selection);  // no change in selection state
  }
  return isEnclosedBy(lasso, s->node);
}

Rect LassoSelector::getBGBBox()
{
  return lassoBBox.isValid() ? selection->transform.mult(lassoBBox) : lassoBBox;
}

// Draw selection background - for now a semi-transparent region on top of strokes;
//  selected strokes are drawn in order by Layer object (not Selection as before)

void Selection::drawBG(Painter* painter)
{
  if(selector) //&& drawType() == STROKEDRAW_SEL)
    selector->drawBG(painter);
}

void PathSelector::drawBG(Painter* painter)
{
  if(path.points.size() < 1)
    return;
  painter->save();
  painter->translate(selection->transform.xoffset(), selection->transform.yoffset());
  painter->setFillBrush(Color::NONE);
  painter->setStroke(bgFill, selRadius*2, Painter::RoundCap, Painter::RoundJoin);
  painter->drawPath(path);
  painter->restore();
}

void LassoSelector::drawBG(Painter* painter)
{
  if(lasso.points.size() < 1)
    return;
  painter->save();
  painter->translate(selection->transform.xoffset(), selection->transform.yoffset());
  painter->setFillBrush(bgFill);
  // dashed border looks better, but is much, much slower, on both Qt and Android
  //painter->setDashCount(1);
  painter->setStroke(bgStroke, 1, Painter::SquareCap, Painter::BevelJoin);
  painter->setVectorEffect(Painter::NonScalingStroke);
  painter->drawPath(lasso);
  //painter->setDashCount(0);
  painter->restore();
}

void RuledSelector::drawBG(Painter* painter)
{
  if(!selRange.isValid())
    return;
  painter->save();
  painter->setFillBrush(bgFill);
  painter->setStroke(bgStroke, 0);
  // note that we're assuming ruled sel doesn't coexist with scaling
  painter->translate(selection->transform.xoffset(), selection->transform.yoffset());
  bool antialias = painter->setAntiAlias(false);

  Page* page = selection->page;
  Path2D path;
  int nlines = selRange.nlines();
  Dim x;
  Dim y = selRange.ymin;
  int ii = 0;
  for(; ii < nlines; ++ii) {
    x = MIN(page->width(), MAX(Dim(0), selRange.lstop(ii)));
    path.addPoint(x, y);
    y += selRange.yruling;
    path.addPoint(x, y);
  }
  for(--ii; ii >= 0; --ii) {
    x = MIN(page->width(), MAX(Dim(0), selRange.rstop(ii)));
    path.addPoint(x, y);
    y -= selRange.yruling;
    path.addPoint(x, y);
  }
  // close path
  path.closeSubpath();
  painter->drawPath(path);
  painter->setAntiAlias(antialias);
  painter->restore();
}

void RectSelector::drawBG(Painter* painter)
{
  if(!selRect.isValid())
    return;
  Dim a = HANDLE_SIZE/mZoom;
  Rect handlerect = Rect::ltrb(-a, -a, a, a);
  Rect r = selRect;
  painter->save();
  painter->setFillBrush(bgFill);
  painter->setStrokeBrush(bgStroke);
  painter->setStrokeWidth(1.0/mZoom);
  if(selection->transform.isRotating())
    painter->transform(selection->transform);

  // Rect sel looks much better w/o antialiasing
  painter->drawRect(r);
  if(drawHandles) {
    // draw resize handles on top of border
    painter->fillRect(Rect(handlerect).translate(r.left, r.top), Color::BLACK);
    painter->fillRect(Rect(handlerect).translate(r.right, r.top), Color::BLACK);
    painter->fillRect(Rect(handlerect).translate(r.left, r.bottom), Color::BLACK);
    painter->fillRect(Rect(handlerect).translate(r.right, r.bottom), Color::BLACK);
    // draw rotation handle
    painter->setAntiAlias(true);
    painter->setFillBrush(Color::NONE);
    //painter->setPen(bgStroke, 0);
    Path2D rh;
    rh.moveTo(r.center().x, r.top);
    rh.lineTo(r.center().x, r.top - 5*a);
    rh.addEllipse(r.center().x, r.top - 6*a, a, a);
    painter->drawPath(rh);
    // cropping handles
    if(enableCrop) {
      painter->setFillBrush(Color::RED);
      painter->setStrokeBrush(Color::NONE);
      painter->drawPath(Path2D().addEllipse(r.left, r.center().y, a, a));
      painter->drawPath(Path2D().addEllipse(r.right, r.center().y, a, a));
      painter->drawPath(Path2D().addEllipse(r.center().x, r.top, a, a));
      painter->drawPath(Path2D().addEllipse(r.center().x, r.bottom, a, a));
    }
  }
  painter->restore();
}
