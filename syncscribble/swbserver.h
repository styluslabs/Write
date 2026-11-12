#pragma once

// Shared whiteboard server, built into Write - run with `Write --server` (see swbServerMain() for options)
// - C++ port of the node.js server (github.com/styluslabs/stylusboard): HTTP API on port (default 7000) for auth
//  and creating/opening whiteboards, and raw TCP relay on port + 1 for the whiteboard data itself (scribblesync.cpp)
// - relay is also available on the API port via GET /v1/swb w/ "Upgrade: swb" (advertised to clients by
//  relay='upgrade' in <swb> reply), so a single port can be put behind a reverse proxy; port + 1 is for old clients
// - single threaded, non-blocking sockets w/ unet_poll() (ulib/unet.h); all state is in memory
// - no authentication: /v1/auth accepts any user (but whiteboard tokens are still random, so relay connections
//  require a token from /v1/openswb); for public servers, auth and TLS are to be provided by a reverse proxy
// - unet_init() must be called before starting server (Write does this in appInit(); swbServerMain() does it too)

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <deque>
#include <chrono>

struct SWBServerOptions
{
  std::string host;  // address to bind; empty for all interfaces
  int port = 7000;  // HTTP API port; whiteboard relay uses port + 1; 0 to choose (free) ports automatically
  double delDelay = 0;  // seconds to keep whiteboard after last user disconnects
  double sendTimeout = 60;  // disconnect client if queued data can't be sent for this many seconds
  size_t maxDataLen = size_t(256) << 20;  // max length of single /data message
  // behind reverse proxy: use X-Remote-User header (if present) as user name and X-Real-IP or X-Forwarded-For as
  //  client address; server must only be reachable via proxy (e.g. host = 127.0.0.1), since headers can be forged
  bool trustProxy = false;
  bool enableTest = false;  // accept test token SCRIBBLE_SYNC_TEST; enable /v1/debug
  bool dump = false;  // log all received data (log level debug)
  int logLevel = 2;  // 0 = error, 1 = warn, 2 = info, 3 = debug
  std::string logFile;  // empty for stderr
};

struct SWBConn;

class SWBServer
{
public:
  typedef std::chrono::steady_clock Clock;
  typedef std::shared_ptr<const std::string> Chunk;

  struct Whiteboard
  {
    std::string name;
    std::string attribs;  // XML attributes for <swb> reply to /v1/openswb
    std::string token;
    std::vector<SWBConn*> clients;
    Clock::time_point deleteAt;  // valid if deletePending
    bool deletePending = false;

    // history, shared by all queued sends; small chunks are merged periodically
    std::vector<Chunk> history;
    std::vector<size_t> histStart;  // offset of each chunk
    size_t histSize = 0;
    size_t unmergedChunks = 0;

    void append(Chunk data);
  };

  struct Session
  {
    std::string user;
    Clock::time_point ctime;
  };

  SWBServerOptions opts;

  SWBServer(const SWBServerOptions& o);
  ~SWBServer();
  // open log file and listening sockets; returns false on error (logged)
  bool start();
  // event loop; returns when stop() is called; can be run on a separate thread
  void run();
  void stop() { stopFlag = true; }
  int apiPort() const { return boundPort; }  // relay port is apiPort() + 1

  void log(int level, const char* fmt, ...);

private:
  void acceptConns(int listenfd, int kind);
  void onReadable(SWBConn* c);
  void flush(SWBConn* c);
  void queue(SWBConn* c, Chunk data, size_t pos = 0, size_t end = std::string::npos);
  void queue(SWBConn* c, std::string s) { queue(c, std::make_shared<const std::string>(std::move(s))); }
  void closeConn(SWBConn* c, bool graceful);
  void leaveWhiteboard(SWBConn* c);
  void broadcast(Whiteboard* wb, Chunk data);
  void processSWB(SWBConn* c);
  bool handleSWBCommand(SWBConn* c, const std::string& cmdstr);
  void processHTTP(SWBConn* c);
  std::string handleHTTP(const std::string& method, const std::string& target,
      const std::string& cookie, const std::string& remote, const std::string& proxyUser, int* status,
      std::string* setCookie);
  void doTimers();
  std::string debugInfo();

  std::atomic_bool stopFlag;
  int apiListen = -1;
  int swbListen = -1;
  int boundPort = 0;
  Clock::time_point acceptPausedUntil;
  Clock::time_point lastSweep;
  std::vector<std::unique_ptr<SWBConn>> conns;
  std::map<std::string, std::unique_ptr<Whiteboard>> whiteboards;
  std::map<std::string, Session> sessions;
  FILE* logOut = NULL;
};

// `Write --server ...` entry point; returns exit code
int swbServerMain(int argc, char* argv[]);
// returns true if argv contains --server
bool swbServerRequested(int argc, char* argv[]);
