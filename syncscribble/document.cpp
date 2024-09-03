//#include <fstream>
#include <stdio.h>
#include <string>
//#include <sstream>
#include "usvg/svgparser.h"
#include "document.h"
#include "basics.h"

// Document structure and navigation:
// document is a ordered set of pages, each of which may have different sizes, rulings, etc.
// File formats supported:
// - html + separate svg per page (<name>_page001.svg, etc.)
// - single file svgz w/ full flush before each page to allow independent decompression; directory w/ page
//  block offsets, sizes, and CRCs in gzip header extra data (see block gzip description in miniz_gzip.h)
// - single file html or svg (to support manually unzipped svgz)

// Why split a document into pages instead of having a single continuous canvas?
// - support different rulings
// - support different page widths (esp. given our reflow feature)
// - it is natural to start a new topic on a new page
// - backend: faster and simpler drawing code; one file per page

static const char* HTML_SKELETON =
"<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN'\n"
"  'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n"
"<html xmlns='http://www.w3.org/1999/xhtml'>\n"
"<head> <title>Handwritten Document</title> </head>\n"
" <body>\n"
" </body>\n"
"</html>\n";

// these apply to .svg and .svgz formats
static const char* SVGZ_HEADER =
R"(<svg id="write-document" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
<rect id="write-doc-background" width="100%" height="100%" fill="#808080"/>
)";

// Inkscape, e.g., doesn't like width,height=100% on rect
static const char* SVGZ_CSS =
R"(
<style type="text/css"><![CDATA[
  #write-document, #write-doc-background { width: %.0fpx;  height: %.0fpx; }
]]></style>
)";

static const int SVGZ_BORDER = 10;
size_t Document::memoryLimit = 0;

Document::Document()
{
  history = new UndoHistory;
  xmldoc.load(HTML_SKELETON, pugi::parse_default | pugi::parse_doctype);
}

Document::~Document()
{
  // undo item discard() accesses page->dirtyCount, so history must be deleted before pages
  // order of member destruction is well defined, so we could rely on that, but I'd rather be explicit
  delete history;
  history = NULL;
  for(Page* page : pages)
    delete page;
}

int Document::insertPage(Page* p, int where)
{
  p->document = this;
  if(where >= 0 && where < (int)pages.size()) {
    // if e.g. we insert a new page after page 1 when other pages haven't been loaded, when document is saved,
    //  new page 2 will overwrite old page 2 before it is loaded and saved and all subsequent pages will be
    //  copies of new page 2.
    if(!blockInfo.empty() || ensurePagesLoaded())
      pages.insert(pages.begin() + where, p);
  }
  else {
    pages.push_back(p);
    where = pages.size() - 1;
  }
  if(history->undoable())
    history->addItem(new PageAddedItem(p, where, this));
  return where;
}

// if delstrokes is true, we first delete all elements from the page - this greatly simplifies SWB
Page* Document::deletePage(int where)  //, bool delstrokes)
{
  if(where < 0 || where >= (int)pages.size())
    return NULL;
  Page* page = *(pages.begin() + where);
  // this is to handle case of replace page (delete, add new), save, then undo replacement
  page->ensureLoaded(false);  // shouldn't be necessary
  page->fileName.clear();
  page->blockIdx = -1;

  //if(delstrokes)
  //  page->removeAll();
  if(history->undoable())
    history->addItem(new PageDeletedItem(page, where, this));
  pages.erase(pages.begin() + where);
  return page;
}

int Document::pageNumForElement(const Element* s) const
{
  SvgDocument* doc = s->node->rootDocument();
  if(doc) {
    for(size_t ii = 0; ii < pages.size(); ++ii) {
      if(pages[ii]->svgDoc.get() == doc)
        return ii;
    }
  }
  return -1;
}

Page* Document::pageForElement(const Element* s) const
{
  int n = pageNumForElement(s);
  return n >= 0 ? pages[n] : NULL;
}

bool Document::ensurePagesLoaded()
{
  bool ok = true;
  for(Page* page : pages)
    ok = page->ensureLoaded(false) && ok;
  return ok;
}

// save() takes ownership of outstrm iff it returns true
bool Document::save(IOStream* outstrm, const char* thumb, saveflags_t flags)
{
  outstrm = outstrm ? outstrm : blockStream.get();
  FSPath fileinfo(outstrm->name()[0] ? outstrm->name() : "untitled.svgz");
  if(fileinfo.extension() == "svgz")
    return saveBgz(outstrm, thumb, flags);

  if(outstrm == blockStream.get())  // otherwise, assumed that outstrm is at beginning (ftell() == 0)
    outstrm->truncate(0);  // this will reopen in wb+ mode
  if(!outstrm->is_open())
    return false;

  bool svgtop = fileinfo.extension() == "svg";
  bool singlefile = svgtop || !((flags & SAVE_MULTIFILE) || FSPath(fileinfo.basePath() + "_page001.svg").exists());
  bool ok = true;
  PugiXMLWriter xmlWriter(*outstrm);
  pugi::xml_node body;
  std::string outstring;
  size_t pos = 0;
  if(svgtop) {
    *outstrm << SVGZ_HEADER << "<defs id=\"write-defs\">\n";
    if(thumb)
      *outstrm << "<image id='thumbnail' xlink:href='data:image/png;base64," << thumb << "'/>\n\n";
    pugi::xml_node cfgnode = getConfigNode();
    if(cfgnode)
      cfgnode.print(xmlWriter, "  ");
    *outstrm << "</defs>\n";
  }
  else {
    // set title
    xmldoc.child("html").child("head").child("title").text().set(fileinfo.baseName().c_str());
    // ensure that body is not empty
    body = xmldoc.child("html").child("body");
    pugi::xml_node pagesdiv = body.find_child_by_attribute("div", "id", "pages");
    while(pagesdiv) {
      body = pagesdiv;
      pagesdiv = pagesdiv.child("div");
    }

    pugi::xml_node stopnode = body.append_child("stopprintinghere");
    std::ostringstream outstream;
    // use .print() instead of passing format_no_declaration flag to .save()
    xmldoc.print(outstream, " ");
    body.remove_child(stopnode);
    outstring = outstream.str();
    pos = outstring.rfind("<stopprintinghere");
    outstrm->write(outstring.c_str(), pos);
    *outstrm << "\n";
    if(thumb)
      *outstrm << "  <img id='thumbnail' style='display:none;' src='data:image/png;base64," << thumb << "'/>\n\n";
  }

  // for svg(z), pages have to be manually positioned and total width and height have to be set on top-level
  //  <svg> (overflow="scroll", e.g., does not work)
  Dim totalheight = 0, maxwidth = 0;
  unsigned int pagenum = 0;
  for(; pagenum < pages.size(); ++pagenum) {
    Page* p = pages[pagenum];
    if(svgtop)
      ok = p->saveSVG(*outstrm, SVGZ_BORDER, totalheight + SVGZ_BORDER) && ok;
    else if(singlefile)
      ok = p->saveSVG(*outstrm) && ok;
    else {
      pugi::xml_node objnode = body.append_child("object");
      // Force explicit </object> closing - this is necessary because although <object ... /> is valid XHTML,
      //  it is not valid HTML and browsers actually ignore the doctype tag and instead use HTTP content
      //  type or local file extension to determine doctype - and we use .html not .xhtml extension!
      objnode.text().set("");

      // write out SVG if page is dirty, if we are saving under a new name, or if page has never been saved
      //  before (to ensure that blank pages get included); note that filenames start from 001 instead of 000
      std::string svgfile = fileinfo.basePath() + fstring("_page%03d.svg", pagenum+1);
      if(p->dirtyCount != 0 || flags & SAVE_FORCE || p->fileName != svgfile) {
        ok = p->saveSVGFile(svgfile.c_str()) && ok;
        p->fileName = svgfile;
      }
      // use relative path for link to file
      // we need to use pugixml to write filename to ensure it is properly escaped (&quot;, etc)
      objnode.append_attribute("data").set_value(FSPath(svgfile).fileName().c_str());

      objnode.append_attribute("type").set_value("image/svg+xml");
      // width and height (valid HTML attributes for <object>) allow continuous scrolling to work with delay loading
      objnode.append_attribute("width").set_value(p->props.width);
      objnode.append_attribute("height").set_value(p->props.height);
      objnode.print(xmlWriter, " ");
      body.remove_child(objnode);
    }
    if(ok)
      p->dirtyCount = 0;
      //p->autoSavedDirtyCount = Page::NOT_AUTO_SAVED;
    totalheight += p->props.height + 2*SVGZ_BORDER;
    maxwidth = std::max(maxwidth, p->props.width);
  }

  if(svgtop)
    *outstrm << "<defs>" << fstring(SVGZ_CSS, maxwidth + 2*SVGZ_BORDER, totalheight) << "</defs>\n</svg>\n";
  else
    *outstrm << (outstring.c_str() + pos + 20);  // finish writing html - skip "<stopprintinghere />"

  outstrm->flush();  //close();
  if(ok && outstrm != blockStream.get())
    blockStream.reset(outstrm);

  // remove any extra files, in case pages were deleted ... should we actually
  //  keep track of number of pages in loaded doc instead?
  std::string svgfile;
  do {
    // preincrement pagenum to account for filename numbers starting at 1
    svgfile = fileinfo.basePath() + fstring("_page%03d.svg", ++pagenum);
  } while(!singlefile && ok && FSPath(svgfile).exists() && removeFile(svgfile));
  // document is now clean
  if(ok)
    dirtyCount = 0;
  return ok;
}

bool Document::saveBgz(IOStream* outstrm, const char* thumb, saveflags_t flags)
{
  static size_t MAX_BLOCK_INFO_COUNT = 1024;  // MAX_BLOCK_INFO_COUNT*sizeof(bgz_block_info_t) must be < 64KB

  int level = (flags >> 24) & 0x0F;
  Dim totalheight = 0, maxwidth = 0;
  uint32_t crc_32 = MINIZ_GZ_CRC32_INIT;
  uint32_t len = 0;
  int nout = 0;

  size_t pagenum = 0;
  if((flags & SAVE_BGZ_PARTIAL))  {
    // find first dirty page
    while(pagenum < pages.size() && pages[pagenum]->dirtyCount == 0 && pages[pagenum]->blockIdx == int(pagenum+1))
      ++pagenum;
  }
  // make sure all pages beyond first dirty page are loaded
  bool loadok = true;
  for(size_t ii = pagenum; ii < pages.size(); ++ii)
    loadok = pages[ii]->ensureLoaded(false) && loadok;
  if(!loadok && !(flags & SAVE_FORCE))
    return false;

  // now it is safe to close stream
  if(!(flags & SAVE_BGZ_PARTIAL) || outstrm != blockStream.get())
    blockInfo.clear();

  MemStream tempstrm(4 << 20);  // 4 MB
  minigz_io_t ztempstrm(tempstrm);
  minigz_io_t zoutstrm(*outstrm);

  uint32_t fileSize = blockInfo.empty() ? 0 : blockInfo.back().offset;  // excluding 8 byte gzip footer
  if(blockInfo.empty()) {  // || pagenum+1 >= blockInfo.size()  -- should never happen
    // writing entire file
    outstrm->truncate(0);
    if(!outstrm->is_open())
      return false;
    bgz_header(zoutstrm, MAX_BLOCK_INFO_COUNT*sizeof(bgz_block_info_t));
    blockInfo.push_back({uint32_t(outstrm->tell()), crc_32, len, 0});
    tempstrm << SVGZ_HEADER;
    tempstrm.seek(0);
    nout = miniz_go(level | MINIZ_GZ_NO_FINISH, ztempstrm, zoutstrm, &crc_32);
    if(nout < 0) return false;
    len += (uint32_t)nout;
    blockInfo.push_back({uint32_t(outstrm->tell()), crc_32, len, 0});
  }
  else {
    // writing partial file starting at pagenum
    size_t blockidx = pagenum+1;
    outstrm->seek(blockInfo[blockidx].offset);
    crc_32 = blockInfo[blockidx].crc32_cum;
    len = blockInfo[blockidx].len_cum;
    blockInfo.resize(blockidx+1);
    // get SVG position for pages[pagenum]
    for(size_t ii = 0; ii < pagenum; ++ii) {
      totalheight += pages[ii]->props.height + 2*SVGZ_BORDER;
      maxwidth = std::max(maxwidth, pages[ii]->props.width);
    }
  }

  bool ok = true;
  for(; pagenum < pages.size(); ++pagenum) {
    Page* p = pages[pagenum];
    tempstrm.truncate(0);  //MemStream tempstrm;
    ok = p->saveSVG(tempstrm, SVGZ_BORDER, totalheight) && ok;
    tempstrm.seek(0);
    nout = miniz_go(level | MINIZ_GZ_NO_FINISH, ztempstrm, zoutstrm, &crc_32);
    if(nout < 0) return false;
    len += (uint32_t)nout;
    blockInfo.push_back({uint32_t(outstrm->tell()), crc_32, len, 0});

    if(ok) {
      p->blockIdx = blockInfo.size() - 2;  // needed to handle page deletions properly
      p->dirtyCount = 0;
      //p->autoSavedDirtyCount = Page::NOT_AUTO_SAVED;
    }
    totalheight += p->props.height + 2*SVGZ_BORDER;
    maxwidth = std::max(maxwidth, p->props.width);
  }

  // final block is thumbnail, config, and CSS for browser
  //MemStream tempstrm;
  tempstrm.truncate(0);
  tempstrm << "<defs id=\"write-defs\">\n";
  if(thumb)  // style='display:none;' ... not needed inside <defs>
    tempstrm << "<image id=\"thumbnail\" xlink:href=\"data:image/png;base64," << thumb << "\"/>\n\n";

  // page sizes
  tempstrm << "<g id=\"write-pages\">\n";
  for(pagenum = 0; pagenum < pages.size(); ++pagenum) {
    tempstrm << fstring("  <use href=\"#page_%03d\" width=\"%.0f\" height=\"%.0f\"/>\n",
        pagenum+1, pages[pagenum]->width(), pages[pagenum]->height());
  }
  tempstrm << "</g>\n\n";

  PugiXMLWriter xmlWriter(tempstrm);
  pugi::xml_node cfgnode = getConfigNode();
  if(cfgnode)
    cfgnode.print(xmlWriter, "  ");
  tempstrm << fstring(SVGZ_CSS, maxwidth + 2*SVGZ_BORDER, totalheight + SVGZ_BORDER) << "</defs>\n</svg>\n";

  tempstrm.seek(0);
  nout = miniz_go(level, ztempstrm, zoutstrm, &crc_32);  // final block
  if(nout < 0) return false;
  len += (uint32_t)nout;
  blockInfo.push_back({uint32_t(outstrm->tell()), crc_32, len, 0});

  gzip_footer(zoutstrm, len, crc_32);
  // seek back to extra header region and write index (unless too long)
  bgz_write_index(zoutstrm, blockInfo.data(), blockInfo.size() > MAX_BLOCK_INFO_COUNT ? 0 : blockInfo.size());

  outstrm->flush();
  if(blockInfo.back().offset < fileSize)
    ok = outstrm->truncate(blockInfo.back().offset + 8) && ok;
  if(ok)
    dirtyCount = 0;
  if(ok && outstrm != blockStream.get())
    blockStream.reset(outstrm);
  return ok;
}

bool Document::loadBgzPage(Page* page)
{
  MemStream inf_block(4 << 20);
  bool ok = bgz_read_block(minigz_io_t(*blockStream.get()), &blockInfo[page->blockIdx], minigz_io_t(inf_block));

  SvgDocument* doc = SvgParser().parseString(
      inf_block.data(), inf_block.size(), XmlStreamReader::BufferInPlace | XmlStreamReader::ParseDefault);
  return doc && page->loadSVG(doc) && ok;
}

Document::loadresult_t Document::loadBgzDoc(IOStream* instrm)
{
  if(!instrm->is_open())
    return LOAD_FATAL;
  pugi::xml_document doc;
  minigz_io_t zinstrm(*instrm);
  blockInfo = bgz_get_index(zinstrm);
  if(!blockInfo.empty()) {
    std::stringstream footerstrm;
    if(bgz_read_block(zinstrm, &blockInfo.back() - 1, footerstrm)) {
      doc.load(footerstrm);
      // load page sizes
      pugi::xml_node pg = doc.child("defs").find_child_by_attribute("id", "write-pages").first_child();
      if(pg) {
        // we need to store block index in Page since page number could change due to page insert or delete
        int blockidx = 1;
        for(; pg; pg = pg.next_sibling())
          insertPage(new Page(pg.attribute("width").as_float(0), pg.attribute("height").as_float(0), blockidx++));
        resetConfigNode(doc.child("defs").find_child_by_attribute("script", "type", "text/writeconfig"));
        return LOAD_OK;
      }
    }
    blockInfo.clear();  // something went wrong
  }
  // if anything goes wrong above, just unzip whole file and try to load
  instrm->seek(0);  // reset input stream to beginning
  size_t inlen = instrm->size();
  if(inlen > 0) {
    MemStream gzout(inlen*4);  // typical compression ratio is 3x
    bool ok = gunzip(zinstrm, minigz_io_t(gzout)) > 0;
    if(gzout.size() > 0) {
      ok = doc.load_buffer_inplace(gzout.data(), gzout.size(), pugi::parse_default | pugi::parse_doctype) && ok;
      return load(doc, FSPath(instrm->name()).parentPath().c_str(), false, ok);
    }
    return LOAD_FATAL;
  }
  return LOAD_EMPTYDOC;
}

Document::loadresult_t Document::load(IOStream* instrm, bool delayload)  //const char* filename
{
  // we take ownership of passed IOStream regardless of errors
  blockStream.reset(instrm);
  // decompose filename
  FSPath fileinfo(instrm->name());
  if(fileinfo.extension() == "svgz" || fileinfo.extension() == "gz")
    return loadBgzDoc(instrm);

  // open index file
  pugi::xml_document doc;
  bool ok = false;
  // uncompressed svg or html doc
  size_t inlen = instrm->size();
  if(inlen > 0 && inlen != SIZE_MAX) {
    unsigned char* buff = static_cast<unsigned char*>(pugi::get_memory_allocation_function()(inlen));
    instrm->read(buff, inlen);
    // handle svgz incorrectly renamed to svg (this was observed to happen somehow on iOS)
    if(inlen > 2 && buff[0] == 0x1F && buff[1] == 0x8B && buff[2] == 8) {
      instrm->seek(0);
      auto res = loadBgzDoc(instrm);
      if(res != LOAD_FATAL) {
        pugi::get_memory_deallocation_function()(buff);
        return res;
      }
    }
    ok = doc.load_buffer_inplace_own(buff, inlen, pugi::parse_default | pugi::parse_doctype);  //| pugi::parse_comments);
  }
  // error from load_file doesn't mean doc is completely unreadable, so proceed anyway
  // if HTML file corrupt or empty but _page001.svg file is present, try to load all svg files present
  if((!ok || !doc.first_child()) && FSPath(fileinfo.basePath() + "_page001.svg").exists()) {
    for(int pagenum = 1;;) {
      std::string svgfile = fileinfo.basePath() + fstring("_page%03d.svg", pagenum++);
      if(!FSPath(svgfile).exists())
        break;
      Page* p = new Page();
      insertPage(p);
      p->loadSVGFile(svgfile.c_str());
    }
    return LOAD_NONFATAL;
  }
  // treat empty file as new file if _page001.svg doesn't exist (which we already have verified)
  if(!doc.first_child())
    return inlen == 0 && instrm->is_open() ? LOAD_EMPTYDOC : LOAD_FATAL;

  return load(doc, fileinfo.parentPath().c_str(), delayload, ok);
}

pugi::xml_node Document::resetConfigNode(pugi::xml_node newcfg)
{
  pugi::xml_node head = xmldoc.child("html").child("head");
  pugi::xml_node oldcfg = head.find_child_by_attribute("script", "type", "text/writeconfig");
  pugi::xml_node cfgnode;
  if(newcfg)
    cfgnode = oldcfg ? head.insert_copy_before(newcfg, oldcfg) : head.append_copy(newcfg);
  else {
    cfgnode = oldcfg ? head.insert_child_before("script", oldcfg) : head.append_child("script");
    cfgnode.append_attribute("type").set_value("text/writeconfig");
  }
  head.remove_child(oldcfg);
  return cfgnode;
}

pugi::xml_node Document::getConfigNode()
{
  return xmldoc.child("html").child("head").find_child_by_attribute("script", "type", "text/writeconfig");
}

// For HTML documents, we remove <svg> or <object> elements from the pugixml document as they are processed,
//  then retain a copy of the pugixml document, which is written back out when the document is saved

Document::loadresult_t Document::load(const pugi::xml_document& doc, const char* path, bool delayload, bool ok)
{
  pugi::xml_node body = doc.child("html").child("body");
  // allow pages to be contained in <div>s under a top level <div id="pages"> (for layout purposes)
  pugi::xml_node pagesdiv = body.find_child_by_attribute("div", "id", "pages");
  while(pagesdiv) {
    body = pagesdiv;
    pagesdiv = pagesdiv.child("div");
  }
  // handle svg container document (for new .svgz format)
  pugi::xml_node svgcontainer = body ? body : doc.find_child_by_attribute("svg", "id", "write-document");
  // if no svg container, try to load as plain SVG (will return LOAD_NONFATAL if successful
  pugi::xml_node svgem = svgcontainer ? svgcontainer.child("svg") : doc.child("svg");
  // note that for html, any inline <svg> prevents loading of external <object> SVG files
  if(svgem) {
    for(; svgem; svgem = svgem.next_sibling("svg")) {
      Page* p = new Page();
      insertPage(p);
      XmlStreamReader reader(svgem);
      // path w/o name works (as long as it ends with '/')
      SvgDocument* pagesvgdoc = SvgParser().setFileName(path).parseXml(&reader);
      ok = pagesvgdoc && p->loadSVG(pagesvgdoc) && ok;
    }
    if(body)
      while(body.remove_child("svg")) {}
  }
  else {
    pugi::xml_node svgobj = body.child("object");
    for(; svgobj; svgobj = svgobj.next_sibling("object")) {
      Page* p = new Page(svgobj.attribute("width").as_float(0), svgobj.attribute("height").as_float(0));
      std::string svgfile = svgobj.attribute("data").value();
      // check for absolute path (used by autosave backup)
      if(svgfile.compare(0, 7, "file://") == 0)
        svgfile.erase(0, 7);
      else
        svgfile.insert(0, path);
      insertPage(p);
      ok = p->loadSVGFile(svgfile.c_str(), delayload) && ok;
    }
    while(body.remove_child("object")) {}
  }
  // for html, we preserve the doc contents; for svg, we just copy the config
  if(body) {
    body.remove_child(body.find_child_by_attribute("img", "id", "thumbnail"));
    xmldoc.reset(doc);  // copies contents of doc to xmldoc, including config node if present
  }
  else
    resetConfigNode(svgcontainer.find_child_by_attribute("defs", "id", "write-defs")
        .find_child_by_attribute("script", "type", "text/writeconfig"));
  // probably isn't necessary
  dirtyCount = 0;
  if(pages.empty()) return LOAD_FATAL;
  if(!ok) return LOAD_NONFATAL;
  if(!svgcontainer) return LOAD_NONWRITE;
  return LOAD_OK;
}

bool Document::checkAndClearErrors()
{
  for(unsigned int ii = 0; ii < pages.size(); ++ii) {
    if(pages[ii]->loadStatus == Page::LOAD_SVG_ERROR) {
      ensurePagesLoaded();  // load all pages
      // reset page state; we expect caller to change filename of course
      for(unsigned int jj = 0; jj < pages.size(); ++jj) {
        pages[jj]->fileName.clear();
        // TODO: Need to enable page to be saved while still indicating damaged state!
        pages[jj]->loadStatus = Page::LOAD_OK;
      }
      return true;
    }
  }
  return false;
}

bool Document::isModified() const
{
  if(dirtyCount != 0)
    return true;
  for(const Page* page : pages) {
    if(page->dirtyCount != 0)
      return true;
  }
  return false;
}

// this assumes caller has already checked that doc is unmodified; perhaps we should set flag on
//  LOAD_EMPTYDOC, but I'd like to minimize number of state variables
bool Document::isEmptyFile() const
{
  // first two checks are just to avoid checking size()
  return numPages() == 1 && pages[0]->strokeCount() == 0 && blockStream && blockStream->size() == 0;
}

// To delete a document, first load it with delayload = true, then
//  call this function, then close it; mainly needed for android
 bool Document::deleteFiles()  //const char* filename
{
  if(!blockStream) return false;
  bool ok = true;
  for(const Page* page : pages) {
    if(!page->fileName.empty())
      ok = removeFile(page->fileName) && ok;
  }
  std::string filename = blockStream->name();
  blockStream.reset();  // close file
  // now delete the html file
  return removeFile(filename) && ok;
}

// initial page number (e.g. page on which the id was referenced) can be passed in pagenumout - we search
//  backwards from initial page, wrapping around as needed
SvgNode* Document::findNamedNode(const char* idstr, int* pagenumout) const
{
  int n = pages.size();
  int p0 = pagenumout ? *pagenumout : n - 1;
  for(int ii = n; ii > 0; --ii) {
    int pagenum = (p0 + ii) % n;
    SvgNode* b = pages[pagenum]->findNamedNode(idstr);
    if(b) {
      if(pagenumout)
        *pagenumout = pagenum;
      return b;
    }
  }
  return NULL;
}

// we'll keep this around for possible future use (e.g. supporting unlimited number of open documents), but
//  since we've implemented a better fix for images, disable for now
#ifdef SCRIBBLE_MEMORY_LIMIT  // mallinfo not available on Windows, Mac, iOS
#include <malloc.h>
#endif

void Document::checkMemoryUsage(int currpage)
{
#ifdef SCRIBBLE_MEMORY_LIMIT
  if(memoryLimit <= 0 || blockInfo.empty()) return;
  //const size_t limit = (size_t(1) << 30);
  struct mallinfo2 info = mallinfo2();
  if(info.uordblks < memoryLimit) return;
  int idxfront = 0, idxback = pages.size() - 1;
  while(info.uordblks > memoryLimit/2) {
    if(idxfront == currpage && idxback == currpage)
      return;
    // choose page most distance from currpage
    int idx = currpage - idxfront > idxback - currpage ? idxfront++ : idxback--;
    if(pages[idx]->dirtyCount != 0) continue;
    pages[idx]->unload();
    info = mallinfo2();
    PLATFORM_LOG("Memory usage after unloading page %d: %lu", idx + 1, info.uordblks);
  }
#endif
}
