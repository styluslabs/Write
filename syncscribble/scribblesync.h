#ifndef SCRIBBLESYNC_H
#define SCRIBBLESYNC_H

#include <map>
#include <thread>
#include "ulib/threadutil.h"
#include "ugui/svggui_platform.h"
#include "syncundo.h"
#include "document.h"

class ScribbleDoc;
class ScribbleConfig;
struct Timer;

class ScribbleSync
{
#ifdef SCRIBBLE_TEST
  friend class ScribbleTest;
#endif

public:
  ScribbleSync(ScribbleDoc* sd);
  ~ScribbleSync();

  static UUID_t strToUuid(const char* str);

  void connectSync(const char *server, const pugi::xml_node& xml, bool master);
  bool isSyncActive();
  bool canSendHist();
  bool canUndo();
  void sendHist(bool force = false);  // public so that it can be called manually when automatic send is disabled
  void syncClearUndone();
  std::string getDocPath() const { return syncServer + "/" + docName; }
  const std::vector<std::string>& clients() const { return clientList; }
  void processRecvBuff();
  void sendStrokeUpdate(const std::vector<Element*>& strokes);
  void doTimerEvent();
  bool sdlEvent(SDL_Event* event);
  std::function<void(std::string msg, int level)> userMessage;
  // we need to be able to access Document object
  ScribbleDoc* scribbleDoc;
  UndoHistory* history;
  ScribbleConfig* cfg;
  bool syncImmed = true;
  bool enableTX = true;
  enum {SYNCVIEW_OFF=0, SYNCVIEW_SLAVE, SYNCVIEW_MASTER} syncViewBox;
  static unsigned int sdlEventType;

private:
  std::string syncUser;
  std::string notifyMsg;
  std::vector<std::string> clientList;
  std::string masterClient;
  std::string syncServer;

  MemStream recvBuff;
  unsigned int rcvdPos = 0;
  unsigned int sentPos = 0;
  unsigned int bytesRcvd = 0;
  UUID_t connectUUID;
  std::string docName;
  std::string startURL;
  bool blockingClearUndone = false;
  volatile enum { SYNC_OFF=0, SYNC_CONNECTING, SYNC_CONNECTED, SYNC_DISCONNECTED } syncState;

  bool syncDoc;
  bool deletePlaceholderPage;
  //int seqUUID;
  DocViewBox lastViewBox;  // last TX view box when master
  DocViewBox pendingViewBox;  // last RX view box when slave and waiting for lull in user input
  std::map<UUID_t, Element*> strokemap;
  typedef std::map<UUID_t, Element*>::iterator StrokeMapIter;
  int timerCount = 0;
  Timer* ssyncTimer = NULL;

  void startSession();
  void disconnectSync();
  //void sendItem(UndoHistoryItem* item);
  void sendDataStr(MemStream& ss);
  Element* findStroke(const char* uuidstr);
  void removeItems(Element* s, Page* p);
  char* processItems(char* pbegin, char* pend);
  void processItem(pugi::xml_node& node);
  void updateStrokemap(UndoHistoryItem* item);
  void sendViewBox(const DocViewBox &vb);

  static DisabledUndoItem disabledItem;

  void onReceiveItem();
  void onConnected();
  void onDisconnected();
  void onSocketError();

  // note volatile does not prevent reordering or ensure atomicity, but I don't think either are an issue here
  volatile int socket = -1;
  enum SocketEvents {SOCKET_CONNECTED=1, SOCKET_DISCONNECTED, SOCKET_ERROR, SOCKET_RECV, SOCKET_SEND};

  std::unique_ptr<MemStream> pendingWrite;
  Semaphore* netSem = NULL;  //SDL_sem*
  std::thread* netThread = NULL;  //SDL_Thread* netThread = NULL;
  static int netThreadFn(void* _self);
};

#endif
