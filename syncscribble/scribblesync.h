#ifndef SCRIBBLESYNC_H
#define SCRIBBLESYNC_H

#include <map>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include "ulib/threadutil.h"
#include "ugui/platforms/platformhttp.h"
#include "ugui/svggui_platform.h"
#include "syncundo.h"
#include "document.h"

class ScribbleDoc;
class ScribbleConfig;
struct Timer;

class SWBServerThread;

class ScribbleSync
{
  friend class ScribbleTest;

public:
  ScribbleSync(ScribbleDoc* sd);
  ~ScribbleSync();

  static UUID_t strToUuid(const char* str);
  // split "host[:port]" or "[ipv6]:port" - port is HTTP API port (default 7000); raw whiteboard relay (for old
  //  servers) uses port + 1; for base URL "http[s]://host[:port][/path]", path is ignored and default port is 80/443
  static std::string splitHostPort(const std::string& hostport, int* port);
  // base URL for HTTP API: "http://host:port" (port 7000 if not specified) unless server includes scheme
  static std::string httpBaseUrl(const std::string& server);
  // HTTP Basic auth credentials (e.g. for reverse proxy) are only sent over https, or http to loopback address
  static bool basicAuthAllowed(const std::string& server);

  void connectSync(const char *server, const pugi::xml_node& xml, bool master);
  // HTTP Basic auth credentials for HTTP relay requests (set before connectSync(); empty for none)
  std::string authUser, authPass;
  // whiteboard hosted on this device: server is kept running while this ScribbleSync exists; shareLink is shown
  //  instead of getDocPath() (which has loopback address)
  std::shared_ptr<SWBServerThread> hostedServer;
  std::string shareLink;
  bool isSyncActive();
  bool canSendHist();
  // true if any local history has not been sent or acknowledged (echoed) by server yet
  bool syncPending();
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

  // HTTP relay (server sends relay='http') - see SWBServer::handleRelayHTTP() for protocol; otherwise raw relay
  //  w/ TCP connection to port + 1 (old servers)
  // all requests are made from main thread; HttpClient callbacks (on worker thread) add responses to queue and
  //  post SOCKET_HTTP event so that responses are handled on main thread
  enum HttpReqType { HTTP_START, HTTP_POLL, HTTP_SEND, HTTP_END };
  struct HttpQueue {
    std::mutex mutex;
    std::condition_variable cond;  // for waiting for send to complete in disconnectSync()
    std::deque<std::pair<HttpReqType, HttpResponse>> responses;
    bool dead = false;  // ScribbleSync disconnected - ignore responses
    SDL_Event event;
  };
  bool httpRelay = false;
  std::string relayBase;  // base URL
  std::string relayArgs;  // document, user, token for start request
  std::string relayId;  // session id; empty if no session
  std::shared_ptr<HttpQueue> httpQueue;
  uint64_t startReq = 0, pollReq = 0, sendReq = 0;  // nonzero while request is pending
  std::string sendBuf;  // data to be sent
  std::string sendInFlight;  // data being sent (resent w/ same seq if request fails)
  long long sendSeq = 0;
  int sendRetryTicks = 0;
  int savedState = 0;  // syncState before connection was interrupted (restored if session is resumed)

  void httpConnect();
  void httpRequest(HttpReqType type, HttpRequest req);
  void httpStart();
  void httpPoll(bool wait);
  void httpFlushSend();
  void onHttpResponse(HttpReqType type, HttpResponse& resp);
  void onHttpError(HttpReqType type, const HttpResponse& resp);
  void reconnect();
  void httpFinishSends(double timeout);
  bool sendBusy() const;

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
  enum SocketEvents {SOCKET_CONNECTED=1, SOCKET_DISCONNECTED, SOCKET_ERROR, SOCKET_RECV, SOCKET_SEND, SOCKET_HTTP};

  std::unique_ptr<MemStream> pendingWrite;
  Semaphore* netSem = NULL;  //SDL_sem*
  std::thread* netThread = NULL;  //SDL_Thread* netThread = NULL;
  static int netThreadFn(void* _self);
};

#endif
