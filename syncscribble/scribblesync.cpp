#include "scribblesync.h"
#include "ulib/unet.h"
#include "usvg/svgparser.h"
#include "application.h"
#include "scribbledoc.h"
#include "scribbleapp.h"

// shared whiteboarding
// - for now, server is trivial: just rebroadcasts everything it receives
// I think there are some similarities with operational transformation:
// - http://www.codecommit.com/blog/java/understanding-and-applying-operational-transformation
// - http://en.wikipedia.org/wiki/Operational_transformation

unsigned int ScribbleSync::sdlEventType = 0;

ScribbleSync::ScribbleSync(ScribbleDoc* sd) : scribbleDoc(sd) {}

ScribbleSync::~ScribbleSync()
{
  disconnectSync();
  if(netSem)
    delete netSem;  //SDL_DestroySemaphore(netSem);
  netSem = NULL;
}

// xml is reply from API server, from which we get name, token, and flags
void ScribbleSync::connectSync(const char* server, const pugi::xml_node& xml, bool master)
{
  syncServer = server;
  docName = xml.attribute("name").as_string();
  history = scribbleDoc->history;
  cfg = scribbleDoc->cfg;
  recvBuff.reserve(1<<22);  // 4 MB
  rcvdPos = 0;
  sentPos = 0;
  bytesRcvd = 0;
  connectUUID = 0;
  // server will echo back user in reply for createswb and openswb
  syncUser = xml.attribute("user").as_string(cfg->String("syncUser"));
  syncDoc = master;
  deletePlaceholderPage = !master;
  // flags
  bool rxonly = xml.attribute("rxonly").as_bool(false);
  enableTX = master || !rxonly;  // check for RX only (lecture) mode
  // sync view box enabled by default for lecture (rxonly) mode
  syncViewBox = rxonly ? (master ? SYNCVIEW_MASTER : SYNCVIEW_SLAVE) : SYNCVIEW_OFF;
  // create fixed part of start URL; version is protocol version
  startURL = "/start?version=1&user=" + syncUser
      + "&document=" + docName + "&token=" + xml.attribute("token").as_string();

  if(sdlEventType == 0)
    sdlEventType = SDL_RegisterEvents(1);
  netSem = new Semaphore;  //SDL_CreateSemaphore(0);
  syncState = SYNC_CONNECTING;
  netThread = new std::thread(netThreadFn, (void*)this);  //SDL_CreateThread(netThreadFn, "ScribbleSync_netThread", (void*)this);
  // timer for reconnect and viewbox
  ssyncTimer = ScribbleApp::gui->setTimer(1000, NULL, [this]() { doTimerEvent(); return 1000; });

  if(deletePlaceholderPage) {
    // reenter event loop
    ScribbleApp::messageBox(ScribbleApp::Warning, _("Connecting"),
        _("Connecting to whiteboard ... please wait."), {_("Cancel")});
    if(deletePlaceholderPage)
      disconnectSync();  // user cancelled dialog
  }
}

static void pushSyncEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  SDL_PeepEvents(event, 1, SDL_ADDEVENT, 0, 0);
  PLATFORM_WakeEventLoop();
}

int ScribbleSync::netThreadFn(void* _self)
{
  ScribbleSync* self = static_cast<ScribbleSync*>(_self);
  SDL_Event event = {0};
  event.type = self->sdlEventType;
  event.user.data1 = self->scribbleDoc;
  event.user.data2 = self;

  while(self->syncState != SYNC_OFF) {
    // don't want to block on recv in main thread; also, UNET_NOBLOCK required to make connect non-blocking
    self->socket = unet_socket(UNET_TCP, UNET_CONNECT, UNET_NOBLOCK, self->syncServer.c_str(), "7001");
    // For a non-blocking socket, we need to use select to wait for writability to indicate connected!
    if(self->socket != -1 && unet_select(-1, self->socket, 8) == UNET_RDY_WR) {  // wait 8 seconds for connect
      event.user.code = SOCKET_CONNECTED;
      pushSyncEvent(&event);
      self->netSem->wait();  //SDL_SemWait(self->netSem);  // wait for main thread to see if we need to wait for both read and write
      while(self->syncState == SYNC_CONNECTING || self->syncState == SYNC_CONNECTED) {
        int res = unet_select(self->socket, (self->pendingWrite ? self->socket : -1), 4);
        if(res < 0) break;  // error
        // give priority to read - because of the sem wait, easier to post only one message at a time
        if(res & UNET_RDY_RD) {
          event.user.code = SOCKET_RECV;
          pushSyncEvent(&event);
          self->netSem->wait();  //SDL_SemWait(self->netSem);  // wait for main thread to finish processing before calling select() again
        }
        if(res & UNET_RDY_WR) {
          event.user.code = SOCKET_SEND;
          pushSyncEvent(&event);
          self->netSem->wait();  //SDL_SemWait(self->netSem);  // wait for main thread to finish processing before calling select() again
        }
      }
      event.user.code = SOCKET_DISCONNECTED;
    }
    else
      event.user.code = SOCKET_ERROR;
    if(self->syncState == SYNC_OFF)
      break;  // return w/o closing socket (so we can wait for proper disconnect)
    pushSyncEvent(&event);
    if(self->socket != -1)
      unet_close(self->socket);
    self->socket = -1;
    self->netSem->wait();  //SDL_SemWait(self->netSem);  // wait for signal before reconnecting
  }
  return 0;
}

bool ScribbleSync::sdlEvent(SDL_Event* event)
{
  if(event->type != sdlEventType)
    return false;
  if(event->user.code == SOCKET_CONNECTED) {
    onConnected();
    netSem->post();  //SDL_SemPost(netSem);  // net thread can now select() again
  }
  else if(event->user.code == SOCKET_DISCONNECTED)
    onDisconnected();
  else if(event->user.code == SOCKET_ERROR)
    onSocketError();
  else if(event->user.code == SOCKET_RECV) {
    onReceiveItem();
    netSem->post();  //SDL_SemPost(netSem);  // net thread can now select() again
  }
  else if(event->user.code == SOCKET_SEND) {
    if(pendingWrite) {
      int written = unet_send(socket, pendingWrite->posdata(), pendingWrite->possize());
      if(written <= 0) {}  // error
      else if(written < int(pendingWrite->possize()))
        pendingWrite->seek(written, SEEK_CUR);
      else
        pendingWrite.reset();
    }
    netSem->post();  //SDL_SemPost(netSem);  // net thread can now select() again
  }
  return true;
}

// should be called when document is closed ... for now, we are not going to act on or even read server's
//  reply to /end (i.e., syncDoc)
void ScribbleSync::disconnectSync()
{
  if(ssyncTimer)
    ScribbleApp::gui->removeTimer(ssyncTimer);
  ssyncTimer = NULL;

  syncState = SYNC_OFF;
  netSem->post();  //SDL_SemPost(netSem);
  if(socket != -1) {
    double dt, timeout = 4.0, t0 = mSecSinceEpoch();
    const char* s = "/end\n";
    unet_send(socket, s, strlen(s));
    // wait for server to disconnect the socket upon receiving /end
    // Any way to make this async? one hurdle is that UndoHistory object will be destroyed when doc is closed
    if(netThread)
      netThread->join();  //SDL_WaitThread(netThread, NULL);
    if(socket != -1) {
      do {
        dt = timeout - (mSecSinceEpoch() - t0)/1000;
        // select() < 0 error, == 0 timeout; recv() < 0 error, == 0 socket closed
      } while (dt > 0 && unet_select(socket, -1, dt) > 0 && unet_recv(socket, recvBuff.data(), recvBuff.capacity) > 0);
      unet_close(socket);
      socket = -1;
    }
  }
  else if(netThread)
    netThread->join();  //SDL_WaitThread(netThread, NULL);
  delete netThread;
  netThread = NULL;
}

void ScribbleSync::onConnected()
{
  // server will echo uuid in <connect> message, allowing it to serve as a unique identifier, which we use
  //  to distinguish this connection attempt from previous ones when reconnecting or when connecting from
  //  a different device (which is why offset cannot be used as unique identifier)
  connectUUID = UndoHistory::newUuid();
  std::string s = fstring("%s&uuid=%llu&offset=%d\n", startURL.c_str(), connectUUID, bytesRcvd);
  unet_send(socket, s.c_str(), s.size());
  // Given that initial serialization could take some time, I think it makes more sense to do it when the
  //  user first shares document, rather than create an unexpected "freeze" when the 2nd user connects
  if(syncDoc)
    startSession();
}

void ScribbleSync::onDisconnected()
{
  userMessage(_("Disconnected from shared session."), 1);
  // disable sendHist until we've reconnected
  syncState = SYNC_DISCONNECTED;
  timerCount = 0;  // try to reconnect immediately
}

void ScribbleSync::onSocketError()
{
  userMessage(_("Error connecting to sync server."), 1); // + socket->errorString(), 1);
  syncState = SYNC_DISCONNECTED;
  timerCount = 1;  // wait for reconnect delay before trying to reconnect
}

void ScribbleSync::doTimerEvent()
{
  // reconnect - 5 ticks (== 5 sec) between attempts
  if(syncState == SYNC_DISCONNECTED && timerCount++ % 5 == 0) {
    syncState = SYNC_CONNECTING;
    netSem->post();  //SDL_SemPost(netSem);
  }
  if(syncState != SYNC_CONNECTED)
    return;
  if(syncViewBox == SYNCVIEW_MASTER) {
    DocViewBox vb = scribbleDoc->activeArea->getViewBox();
    if(vb != lastViewBox)
      sendViewBox(vb);
    lastViewBox = vb;
    pendingViewBox = DocViewBox();  // invalidate in case we later switch back to slave mode
  }
  else if(syncViewBox == SYNCVIEW_SLAVE && pendingViewBox.isValid()
      && mSecSinceEpoch() - scribbleDoc->views[0]->scribbleInput->lastEventTime > 4000) {
    scribbleDoc->views[0]->setViewBox(pendingViewBox);
    scribbleDoc->doRefresh();
    pendingViewBox = DocViewBox();
  }
}

void ScribbleSync::startSession()
{
  // undo back to beginning of undo history, set UUIDs for existing elements, send initial pages, enable
  //  serialization, and redo back to original position - this works whether we do it when first connecting
  //  or if we wait until second user connects
  history->clearUndone();  // instead of this, we should save actual undo position
  while(history->canUndo())
    history->undo();
  // assign sequential values for UUIDs of strokes loaded from file and add to strokemap - this ensures
  //  strokes loaded from shared file have same UUID on all clients, so delete and change will work
  // other clients will assign the same sequential UUIDs for each <addpage> they receive with strokes
  Document* doc = scribbleDoc->document;
  doc->ensurePagesLoaded();
  MemStream ss;
  ss << "<undo uuid='1' user='" << syncUser << "'>";
  for(unsigned int pp = 0; pp < doc->pages.size(); pp++) {
    // now serialize the page w/ all strokes
    PageAddedItem item(doc->pages[pp], pp, doc);
    item.serialize(ss);
    updateStrokemap(&item);
  }
  ss << "</undo>\n";
  // transmit
  sendDataStr(ss);
  // restore doc state and transmit
  syncState = SYNC_CONNECTED;
  while(history->canRedo()) {
    history->redo();
    sendHist();
  }
}

bool ScribbleSync::canUndo()
{
  return isSyncActive() || !(history->pos <= rcvdPos || history->pos <= sentPos);
}

void ScribbleSync::syncClearUndone()
{
  if(syncState != SYNC_OFF && (sentPos > history->pos || rcvdPos > history->pos)) {
    blockingClearUndone = true;
    if(syncState == SYNC_DISCONNECTED) {  // immediate reconnect attempt
      syncState = SYNC_CONNECTING;
      netSem->post();  //SDL_SemPost(netSem);
    }
    else
      sendHist();
    // reenter event loop
    ScribbleApp::messageBox(ScribbleApp::Warning, _("Reconnecting"),
      _("Connection to server required to complete this whiteboard action ... please wait."), {_("Cancel")});
    blockingClearUndone = false;
    if(rcvdPos > history->pos) {
      userMessage(_("Unable to reconnect - whiteboard session closed."), 1);
      disconnectSync();  // didn't work - abort shared session
    }
  }
}

UUID_t ScribbleSync::strToUuid(const char* str)
{
  return strtoull(str, NULL, 0);
}

Element* ScribbleSync::findStroke(const char* uuidstr)
{
  if(!uuidstr || !uuidstr[0]) return NULL;
  UUID_t uuid = strToUuid(uuidstr);
  if(uuid == 0) return NULL;
  std::map<UUID_t, Element*>::iterator it = strokemap.find(uuid);
  return it != strokemap.end() ? it->second : NULL;
}

DisabledUndoItem ScribbleSync::disabledItem;

// Disable items referring to deleted stroke or page by replacing with pointer to DisabledUndoItem
// Note that we do not call discard() before deleting item
void ScribbleSync::removeItems(Element* s, Page* p)
{
  for(auto ii = history->hist.begin(); ii != history->hist.end(); ii++) {
    if((*ii)->isA(UndoHistoryItem::STROKE_ITEM)) {
      Element* t = static_cast<StrokeUndoItem*>(*ii)->getStroke();
      Page* q = static_cast<StrokeUndoItem*>(*ii)->getPage();  //doc->pageForElement(t);
      if(t == s || q == p) {
        delete *ii;
        *ii = &disabledItem;
      }
    }
    else if(p && (((*ii)->isA(UndoHistoryItem::DOCUMENT_ITEM) && p == static_cast<DocumentUndoItem*>(*ii)->p)
        || ((*ii)->isA(UndoHistoryItem::PAGE_CHANGED_ITEM) && p == static_cast<PageChangedItem*>(*ii)->p))) {
      delete *ii;
      *ii = &disabledItem;
    }
  }
}

// format: <svg uuid='...'><path or transform or properties or delete/></item>
// need to use <svg> so that SvgHandler will parse
void ScribbleSync::onReceiveItem()
{
  //if(!recvBuff) return;
  // Unix sockets don't have a built-in readline, so let's not rely on that!
  // read everything to buffer and strstr(</undo>) until no more found,
  //  then memmove any remaining data to beginning of buffer
  int torecv = unet_bytes_avail(socket) + 0xFFFF;  // add a little extra room
  if(int(recvBuff.endsize()) < torecv)
    recvBuff.reserve(recvBuff.capacity + torecv);
  int n = unet_recv(socket, recvBuff.enddata(), recvBuff.endsize());

  //int n = unet_recv(socket, recvPtr, RECV_BUFF_LEN - (recvPtr - recvBuff) - 1);
  if(n > 0) {
    recvBuff.buffsize += n;  //recvPtr += n;
    bytesRcvd += n;
  }
  else
    syncState = SYNC_DISCONNECTED;  // 0 bytes read indicates disconnect
  // only process immediately if user isn't doing anything (to minimize lag and simplify some logic)
  if(scribbleDoc->getActiveMode() == MODE_NONE || blockingClearUndone)
    processRecvBuff();
}

void ScribbleSync::processRecvBuff()
{
  if(recvBuff.size() < 13) return;  //if(recvPtr == recvBuff) return;  // nothing to process

  notifyMsg.clear();
  unsigned int origpos = history->pos;
  while(recvBuff.possize() > 12) {
    // check for gzip
    char* pbegin = recvBuff.posdata();
    if(pbegin[0] == 0x1F && (uint8_t)pbegin[1] == 0x8B) {
      // read size from comment field
      size_t gzlen = strtoul(recvBuff.posdata() + 10, NULL, 0);
      if(recvBuff.possize() < 32 || recvBuff.possize() < gzlen)
        break;  // wait for all gzip data
      MemStream outstrm(4*gzlen);
      minigz_io_t zinstrm(recvBuff);
      minigz_io_t zoutstrm(outstrm);
      if(gunzip(zinstrm, zoutstrm) < 0)
        SCRIBBLE_LOG("Error decompressing SWB data!");  //break;  // TODO: force disconnect
      processItems(outstrm.data(), outstrm.enddata());
    }
    else {
      // "\x1F\x8B" should not exist in valid UTF-8 text, so safe to search for it ... but in any case,
      //  position will only be advanced to end of last <undo> block processed
      char* pend = strNstr(pbegin, "\x1F\x8B", recvBuff.possize());
      char* iend = processItems(pbegin, pend ? pend : recvBuff.enddata());
      recvBuff.seek(iend - recvBuff.data());
      if(!pend) break;
      ASSERT(pend == iend && "Invalid SWB data");  // \x1F\x8B was found in unexpected location
    }
  }
  // shift remaining contents of buffer to beginning
  recvBuff.shift(recvBuff.tell());

  // restore undo position
  while(history->pos < origpos) history->redo();
  while(history->pos > origpos) history->undo();

  if(blockingClearUndone && ScribbleApp::currDialog && rcvdPos == history->pos)
    ScribbleApp::currDialog->finish(Dialog::ACCEPTED);

  scribbleDoc->uiChanged(UIState::SyncEvent);
  scribbleDoc->doRefresh();
  if(!notifyMsg.empty())
    userMessage(notifyMsg, -1);  // -1 to concat to previous info message
}

char* ScribbleSync::processItems(char* pbegin, char* pend)
{
  // we could be more efficient by scanning from recvPtr - 7 (to account for </undo> split across packets)
  char* iend = strNstr(pbegin, "</undo>\n", pend - pbegin);
  while(iend && syncState != SYNC_OFF) {  // accessdenied will disable sync
    // handle multiple <undo> nodes in recv buffer
    iend += 8;  // shift past </undo>\n
    pugi::xml_document xml;
    if(xml.load_buffer(pbegin, iend - pbegin)) {
      pugi::xml_node undonode = xml.child("undo");
      UUID_t itemid = strToUuid(undonode.attribute("uuid").as_string());
      // search in both directions (to handle undo) for undo header with matching uuid
      // if uuid match indicates that item belongs to us, there is no need to process it further
      int prevpos = rcvdPos;
      int nextpos = rcvdPos;
      while(++nextpos < (int)history->hist.size() && !history->hist[nextpos]->isA(UndoHistoryItem::HEADER)) {}
      while(--prevpos > 0 && !history->hist[prevpos]->isA(UndoHistoryItem::HEADER)) {}
      if(itemid == 1 && syncDoc)
        syncDoc = false;
      else if(rcvdPos < history->hist.size() && static_cast<UndoGroupHeader*>(history->hist[rcvdPos])->uuid == itemid)
        rcvdPos = nextpos;
      else if(prevpos >= 0 && static_cast<UndoGroupHeader*>(history->hist[prevpos])->uuid == itemid)
        rcvdPos = prevpos;
      else {
        int prevpages = scribbleDoc->document->pages.size();
        int docdirty = scribbleDoc->document->dirtyCount;
        // undo (redo) items not yet recognized by server
        if(isSyncActive()) {
          while(history->pos < rcvdPos) history->redo();
          while(history->pos > rcvdPos) history->undo();
        }
        // handle multiple items, such as produced by a StrokesTransformItem
        pugi::xml_node itemnode = undonode.first_child();
        while(!itemnode.empty()) {
          processItem(itemnode);
          itemnode = itemnode.next_sibling();
        }
        // this will handle preserving position if pages are added or removed; scroll to view dirty = false
        scribbleDoc->undoRedoUpdate(undonode.attribute("pagenum").as_int(-1),
            scribbleDoc->document->dirtyCount == docdirty ? prevpages : 0, false);
      }
    }
    else
      SCRIBBLE_LOG("Error loading XML received from whiteboard!");
    pbegin = iend;
    iend = strNstr(pbegin, "</undo>\n", pend - pbegin);
  }
  return pbegin;
}

void ScribbleSync::processItem(pugi::xml_node& node)
{
  UndoHistoryItem* item = NULL;
  Document* doc = scribbleDoc->document;
  std::string nodename = node.name();
  // get stroke and page for stroke being modified; since strokemap is only updated when local edits are sent,
  //  page may be gone even though stroke is still in strokemap
  Element* mstroke = nodename != "addstroke" ? findStroke(node.attribute("strokeuuid").as_string()) : NULL;
  Page* mpage = mstroke ? doc->pageForElement(mstroke) : NULL;

  // handle control messages
  if(nodename == "accessdenied") {
    disconnectSync();
    //syncEnabled = false;
    userMessage(_("Error connecting to whiteboard. Please try again."), 2);
  }
  else if(nodename == "connect") {
    const char* clientname = node.attribute("name").as_string();
    // use level = 0 with userMessage to clear other msgs
    if(clientname == syncUser) {
      // ignore old <connect> messages when reconnecting
      if(strToUuid(node.attribute("uuid").as_string()) != connectUUID)
        return;
      std::string msg = _("Connected as ") + syncUser + " to whiteboard " + docName;
      if(!clientList.empty())
        msg += " with " + joinStr(clientList, ", ");
      if(!enableTX)
        msg += ".\nWhiteboard is in lecture mode: adding and removing pages disabled";
      userMessage(msg + ".", 0);
    }
    else if(isSyncActive())
      notifyMsg.append(clientname).append(" connected. ");
    // add to client list after generating message so that our name isn't included
    clientList.push_back(clientname);
    // for now, first client (creator of whiteboard) is always the master for view syncing
    if(masterClient.empty())
      masterClient = clientname;
    // on initial connect, this prevents syncActive until there is a second user connected
    // ... but right now we are not using this for connect, only reconnect
    // on reconnect, this prevents syncActive until we receive our <connect> message, indicating the end
    //  of history that we missed while disconnected
    if(!isSyncActive() && clientList.size() > 1 &&
        std::find(clientList.begin(), clientList.end(), syncUser) != clientList.end()) {
      sentPos = rcvdPos;  // for handling reconnect
      syncState = SYNC_CONNECTED;
      sendHist();  // send all history to server
    }
  }
  else if(nodename == "disconnect") {
    const char* clientname = node.attribute("name").as_string();
    clientList.erase(std::remove(clientList.begin(), clientList.end(), clientname), clientList.end());
    if(clientname != syncUser)
      notifyMsg.append(clientname).append(" disconnected. ");
  }
  else if(isSyncActive() && node.parent().attribute("user").as_string() == syncUser) {
    // updatestroke and viewbox are outside undo system, so their appearance here is normal, as are our own
    //  items when receiving history upon reconnection before syncActive; all other cases are errors
    if(nodename != "updatestroke" && nodename != "viewbox")
      SCRIBBLE_LOG("received own whiteboard item out-of-order!");
  }
  else if(nodename == "addstroke") {
    // <addstroke strokeuuid= pageuuid= nextstrokeuuid= ><path ... /></addstroke>
    std::unique_ptr<XmlStreamReader> reader(new XmlStreamReader(node));
    std::unique_ptr<SvgDocument> svgDoc(SvgParser().parseXmlFragment(reader.get()));
    Element* stroke = NULL;
    if(!svgDoc->children().empty()) {
      SvgNode* n = svgDoc->children().front();
      svgDoc->removeChild(n);
      stroke = new Element(n);
    }
    unsigned int pagenum = node.attribute("pagenum").as_uint(-1);
    if(!stroke) {}
    else if(pagenum < doc->pages.size()) {
      item = new StrokeAddedItem(stroke, doc->pages[pagenum],
          findStroke(node.attribute("nextstrokeuuid").as_string()));
      stroke->uuid = strToUuid(node.attribute("strokeuuid").as_string());
      // update strokemap
      strokemap[stroke->uuid] = stroke;
    }
    else {
      stroke->deleteNode(); //delete stroke;
      stroke = NULL;
    }
  }
  else if(nodename == "delstroke") {
    // StrokeDeletedItem
    if(mpage) {
      auto e = mpage->children().end();
      auto ii = std::find(mpage->children().begin(), e, mstroke);
      item = new StrokeDeletedItem(mstroke, mpage, (ii != e && ++ii != e) ? *ii : NULL);
      // remove any local undo items referring to deleted stroke
      removeItems(mstroke, NULL);
      // update strokemap
      strokemap.erase(mstroke->uuid);
      // remove stroke from selection and recent strokes if necessary
      scribbleDoc->invalidateStroke(mstroke);
    }
  }
  else if(nodename == "translate") {
    // StrokeTranslateItem
    if(mpage) {
      item = new StrokeTranslateItem(mstroke, mpage,
          node.attribute("x").as_double(), node.attribute("y").as_double());
      scribbleDoc->invalidateStroke(mstroke);  // remove transformed stroke from selection
    }
  }
  else if(nodename == "transform") {
    // StrokeTransformItem
    if(mpage) {
      std::vector<Dim> m = parseNumbersList(node.attribute("matrix").as_string(), 9);
      std::vector<Dim> intscale = parseNumbersList(node.attribute("internalscale").as_string(), 2);
      // serialized as m[0], m[1], 0, m[2], m[3], 0, m[4], m[5], 1
      ScribbleTransform tf(Transform2D(m[0], m[1], m[3], m[4], m[6], m[7]), intscale[0], intscale[1]);
      item = new StrokeTransformItem(mstroke, mpage, tf);
      scribbleDoc->invalidateStroke(mstroke);  // remove transformed stroke from selection
    }
  }
  else if(nodename == "strokechanged") {
    if(mpage)
      item = new StrokeChangedItem(mstroke, mpage, StrokeProperties(
          Color::fromArgb(node.attribute("color").as_uint()), node.attribute("width").as_float()));
  }
  else if(nodename == "updatestroke") {
    // this handles the special case of a change to stroke outside the undo system, currently limited to
    //  update of COM by groupStrokes
    if(mstroke)
      mstroke->setCom(Point(mstroke->com().x, node.attribute("__comy").as_double(mstroke->com().y)));
  }
  else if(nodename == "pagechanged") {
    unsigned int pagenum = node.attribute("pagenum").as_uint(-1);
    if(pagenum < doc->pages.size()) {
      Page* p = doc->pages[pagenum];
      PageProperties props(node.attribute("width").as_float(), node.attribute("height").as_float(),
          node.attribute("xruling").as_float(), node.attribute("yruling").as_float(),
          node.attribute("marginLeft").as_float(), Color::fromArgb(node.attribute("color").as_uint()),
          Color::fromArgb(node.attribute("rulecolor").as_uint()));
      item = new PageChangedItem(p, props);
    }
  }
  else if(nodename == "addpage") {
    Page* p = new Page();
    // document must be set in case page contains any strokes - Layer::addStroke will access UndoHistory obj
    p->document = scribbleDoc->document;
    pugi::xml_node pagenode = node.first_child();
    XmlStreamReader reader(pagenode);
    SvgDocument* pagesvgdoc = SvgParser().parseXml(&reader);
    if(p->loadSVG(pagesvgdoc)) {
      item = new PageAddedItem(p, node.attribute("pagenum").as_int(), doc);
      // hack to delete placeholder page used while waiting for connection; the problem is
      //  that there is too much code in ScribbleArea that assumes document will always have at least one
      //  page, but the proper initial state for opening a shared doc is to have no pages
      if(deletePlaceholderPage) {
        Page* pp = scribbleDoc->document->pages[0];
        scribbleDoc->invalidatePage(pp);
        scribbleDoc->document->deletePage(0);
        delete pp;  // Document::deletePage() just removes page from document; doesn't actually delete it
        deletePlaceholderPage = false;
        if(ScribbleApp::currDialog)
          ScribbleApp::currDialog->finish(Dialog::ACCEPTED);  // close blocking dialog
      }
      // assign UUID to any strokes included with page
      UUID_t seqUUID = strToUuid(node.attribute("firstuuid").as_string());
      for(Element* s : p->children()) {
        strokemap[seqUUID] = s;
        s->uuid = seqUUID++;
      }
    }
  }
  else if(nodename == "delpage") {
    unsigned int pagenum = node.attribute("pagenum").as_uint(-1);
    if(pagenum < doc->pages.size()) {
      Page* page = doc->pages[pagenum];
      item = new PageDeletedItem(page, pagenum, doc);
      // remove any local undo items referring to deleted page
      removeItems(NULL, page);
      scribbleDoc->invalidatePage(page);
      // we previously relied on the deleting user to send a separate StrokeDeletedItem for each stroke on
      //  the page, but that didn't work for other users' strokes on page when undoing page addition
      for(Element* s : page->children()) {
        removeItems(s, NULL);
        // update strokemap
        strokemap.erase(s->uuid);
        // remove stroke from selection and recent strokes if necessary
        scribbleDoc->invalidateStroke(s);  // I don't think this is necessary w/ invalidatePage?!
      }
    }
  }
  else if(nodename == "viewbox") {
    // this handles (optional) sync of view (scroll, zoom)
    const char* clientname = node.parent().attribute("user").as_string();
    Rect r = Rect::ltrb(node.attribute("left").as_double(MAX_DIM), node.attribute("top").as_double(MAX_DIM),
        node.attribute("right").as_double(MIN_DIM), node.attribute("bottom").as_double(MIN_DIM));
    int pagenum = node.attribute("pagenum").as_int(-1) + cfg->Int("syncViewPageOffset", 0);
    DocViewBox vb(pagenum, r, node.attribute("zoom").as_double(1));
    if(vb.isValid()) {
      if(isSyncActive() && syncViewBox == SYNCVIEW_MASTER) {
        // our own viewbox events have already been filtered out, of course
        syncViewBox = SYNCVIEW_SLAVE;
        notifyMsg.append(clientname).append(" is now controlling the view. ");
      }
      if(syncViewBox == SYNCVIEW_SLAVE
          && mSecSinceEpoch() - scribbleDoc->views[0]->scribbleInput->lastEventTime > 4000) {
        scribbleDoc->views[0]->setViewBox(vb);
        pendingViewBox = DocViewBox();
      }
      else
        pendingViewBox = vb;  // save viewbox in case sync view is enabled later
    }
  }

  if(item) {
    // apply the received item
    item->redo();
    // delete object for Stroke and PageDeletedItems
    item->discard(false);
    // TODO: draw node.parent().attribute("user") next to stroke->bbox - just use QToolTip! Hide if user scrolls
    //if(stroke && cfg->Bool("syncShowUser"))
    delete item;
  }
}

bool ScribbleSync::isSyncActive()
{
  return syncState == SYNC_CONNECTED && socket != -1;
}

bool ScribbleSync::canSendHist()
{
  return isSyncActive() && sentPos != history->pos && enableTX && !pendingWrite;
}

// send everything between sentPos and history->pos
// Note that we have to send undo group even if it only contains disabled items so that rcvdPos gets updated
// TODO: the right way to send history would be to serialize each item immediately in UndoHistory::addItem(),
//  ::undo(), or ::redo(), so that serialization reflects state at instant of item's application.  Should be
//  fairly easy - just set a flag in UndoHistory and have it write to a string stream which we then read
//  Then we can get rid of hack with PageAddedItem/Page::saveSVG

void ScribbleSync::sendHist(bool force)
{
  if(!(syncImmed || blockingClearUndone || force) || !canSendHist())
    return;

  MemStream ss;
  size_t origpos = history->pos;
  while(history->pos < sentPos) history->redo();
  while(history->pos > sentPos) history->undo();
  while(sentPos < origpos) {
    UndoHistoryItem* item = history->hist[sentPos++];
    if(item->isA(UndoHistoryItem::HEADER)) {
      UndoGroupHeader* hdr = static_cast<UndoGroupHeader*>(item);
      if(ss.size() > 0)
        ss << "</undo>\n";
      if(hdr->uuid == 0)
        hdr->uuid = UndoHistory::newUuid();
      ss << fstring("<undo uuid='%llu' pagenum='%d' user='%s'>", hdr->uuid, hdr->pageNum, syncUser.c_str());
      history->redo();  // apply this item before serializing
    }
    else if(!item->isA(UndoHistoryItem::DISABLED_ITEM)) {
      item->serialize(ss);
      updateStrokemap(item);
    }
  }
  // handle local undo - we use a temporary stringstream since we reach header last
  MemStream tempss;
  while(sentPos > origpos) {
    UndoHistoryItem* item = history->hist[--sentPos];
    // item must be in current state (undone in this case) for serialization
    if(sentPos < history->pos)
      history->undo();
    if(item->isA(UndoHistoryItem::HEADER)) {
      UndoGroupHeader* hdr = static_cast<UndoGroupHeader*>(item);
      if(ss.size() > 0)
        ss << "</undo>\n";
      if(hdr->uuid == 0)
        hdr->uuid = UndoHistory::newUuid();
      ss << fstring("<undo uuid='%llu' user='%s'>", hdr->uuid, syncUser.c_str());
      if(tempss.size() > 0)
        ss.write(tempss.data(), tempss.size());
      tempss.truncate(0);
    }
    else if(!item->isA(UndoHistoryItem::DISABLED_ITEM)) {
      UndoHistoryItem* inv = item->inverse();
      inv->serialize(tempss);
      updateStrokemap(inv);
      delete inv;
    }
  }
  ASSERT(history->pos == origpos && "Uh oh");
  if(ss.size() > 0) {
    ss << "</undo>\n";
    // use a helper fn so we don't have to make a copy of ss.str() since we need length and contents
    sendDataStr(ss);
  }
}

void ScribbleSync::sendDataStr(MemStream& ss)
{
  int zlevel = cfg->Int("syncCompress", 0);
  if(zlevel > 0 && ss.size() > 256) {
    MemStream outstrm(1024 + ss.size()/2);
    ss.seek(0);
    minigz_io_t zinstrm(ss);
    minigz_io_t zoutstrm(outstrm);

    gzip_header(zoutstrm, 0x10);  // 0x10 = FCOMMENT
    size_t commentpos = outstrm.tell();
    outstrm << std::string(11, ' ');  // reserve 10 chars + '\0' in the FCOMMENT field
    uint32_t crc_32 = MINIZ_GZ_CRC32_INIT;
    int len = miniz_go(zlevel, zinstrm, zoutstrm, &crc_32);
    gzip_footer(zoutstrm, len, crc_32);
    outstrm.seek(commentpos);
    std::string lenstr = fstring("0x%08x", outstrm.size());
    outstrm.write(lenstr.c_str(), 11);  // include \0
    ss = std::move(outstrm);
  }

  std::string hdr = fstring("/data?length=%d\n", ss.size());
  if(pendingWrite) {
    // need to save and restore send position!
    size_t pos = pendingWrite->tell();
    pendingWrite->seek(pendingWrite->size());  // append to end
    *pendingWrite << hdr;
    pendingWrite->write(ss.data(), ss.size());
    pendingWrite->seek(pos);
    return;
  }
  int written = unet_send(socket, hdr.c_str(), hdr.size());  // < 0 on error!
  if(written < int(hdr.size())) {
    // this should be pretty rare
    pendingWrite.reset(new MemStream(ss.size() + hdr.size()));
    *pendingWrite << hdr.substr(std::max(written, 0));
    pendingWrite->write(ss.data(), ss.size());
    return;
  }
  written = unet_send(socket, ss.data(), ss.size());
  if(written < int(ss.size())) {
    pendingWrite.reset(new MemStream(std::move(ss)));
    pendingWrite->seek(std::max(written, 0));
  }
}

// send viewbox (in Dim units) for syncing of document view (scroll, zoom)
void ScribbleSync::sendViewBox(const DocViewBox& vb)
{
  MemStream ss(256);
  ss << fstring("<undo uuid='12' pagenum='-1' user='%s'>", syncUser.c_str());
  ss << fstring("<viewbox pagenum='%d' zoom='%f' left='%f' top='%f' right='%f' bottom='%f'/>",
            vb.pagenum, vb.zoom, vb.box.left, vb.box.top, vb.box.right, vb.box.bottom);
  ss << "</undo>\n";
  sendDataStr(ss);
}

// this is an unfortunate hack to handle the update of stroke COM by ScribbleArea::groupStrokes, which occurs
//  outside of the undo system; to be called after strokes are updated - uses current values from strokes
// We don't send if first stroke doesn't have UUID set, which implies that <addstroke> hasn't been sent for
//  it, so there is nothing to update on other clients.  If and when addstroke is sent later, it will include
//  the correct, updated COM of course.
// One corner case to note is that we are disconnected between <addstroke> and this fn, stroke COMs on other
//  clients will be wrong
void ScribbleSync::sendStrokeUpdate(const std::vector<Element*>& strokes)
{
  if(strokes.empty() || strokes.front()->uuid == 0 || !isSyncActive())
    return;
  MemStream ss(80 + strokes.size()*80);
  ss << fstring("<undo uuid='11' pagenum='-1' user='%s'>", syncUser.c_str());
  for(const Element* s : strokes)
    ss << fstring("<updatestroke strokeuuid='%llu'  __comy='%f'/>", s->uuid, s->com().y);
  ss << "</undo>\n";
  sendDataStr(ss);
}

// this is used for items being sent, not received
void ScribbleSync::updateStrokemap(UndoHistoryItem* item)
{
  if(item->isA(UndoHistoryItem::STROKE_ITEM)) {
    Element* s = static_cast<StrokeUndoItem*>(item)->getStroke();
    if(item->isA(UndoHistoryItem::STROKE_ADDED_ITEM))
      strokemap[s->uuid] = s;
    else if(item->isA(UndoHistoryItem::STROKE_DELETED_ITEM))
      strokemap.erase(s->uuid);
  }
  else if(item->isA(UndoHistoryItem::PAGE_ADDED_ITEM)) {
    for(Element* s : static_cast<PageAddedItem*>(item)->p->children())
      strokemap[s->uuid] = s;
  }
  else if(item->isA(UndoHistoryItem::PAGE_DELETED_ITEM)) {
    for(Element* s : static_cast<PageDeletedItem*>(item)->p->children())
      strokemap.erase(s->uuid);
  }
}
