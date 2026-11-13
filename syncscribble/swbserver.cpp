// Shared whiteboard server - see swbserver.h

// sockets are handled by ulib/unet.h, so no platform specific code is needed here

#include "swbserver.h"
#include <stdarg.h>
#include <climits>
#include <string.h>
#include <time.h>
#include <csignal>
#include <random>
#include <algorithm>
#include "ulib/md5.h"
#include "ulib/unet.h"
#include "ulib/stringutil.h"

// relay message size limit (other than /data payload) and HTTP header limit
static constexpr size_t MAX_CMD_LEN = 64*1024;
static constexpr size_t MAX_HTTP_HEADER = 16*1024;
// history chunks smaller than this are merged
static constexpr size_t HIST_MERGE_SIZE = 64*1024;
static constexpr size_t HIST_MERGE_COUNT = 64;
// session cookie lifetime (same as node.js server)
static constexpr int SESSION_LIFETIME_SECS = 5*60;
// time to wait for client to close connection after we finish sending (graceful close)
static constexpr int LINGER_SECS = 5;
// close idle keep-alive HTTP connections (w/o pending poll) after this long
static constexpr int HTTP_IDLE_SECS = 60;
// max bytes of history returned by a single poll request
static constexpr size_t MAX_POLL_BYTES = 4 << 20;

enum ConnKind { CONN_HTTP = 1, CONN_SWB = 2 };

struct SWBConn
{
  int fd = -1;
  int kind = 0;
  std::string remote;
  std::string inbuf;
  size_t expectData = 0;  // remaining bytes of /data payload
  std::string dataBuf;
  struct Seg { SWBServer::Chunk data; size_t pos, end; };
  std::deque<Seg> outq;
  size_t outBytes = 0;
  SWBServer::Clock::time_point lastProgress;  // last successful send (or time queue became non-empty)
  bool closing = false;  // no more input processing; close after outq is sent
  bool halfClosed = false;  // shutdown(SHUT_WR) done; waiting for peer to close
  SWBServer::Clock::time_point closeDeadline;
  bool dead = false;  // to be deleted at end of event loop iteration
  SWBServer::Whiteboard* wb = NULL;
  std::string user;
  std::string proxyUser;  // user authenticated by reverse proxy (w/ trustProxy)
  // HTTP
  std::unique_ptr<HTTPRequest> req;  // header received, waiting for body
  SWBPollClient* pollWait = NULL;  // poll request waiting for data (no further requests processed until answered)
  bool pollKeepAlive = false;
  bool resumeInput = false;  // pipelined requests to process after deferred response is sent
  SWBServer::Clock::time_point lastActivity;
};

// utility fns

static std::string md5hex(const std::string& s)
{
  char buf[2*MD5_DIGEST_SIZE + 1];
  uint8_t digest[MD5_DIGEST_SIZE];
  MD5(s.data(), int(s.size()), digest);
  static const char* hexDigits = "0123456789abcdef";
  for(size_t ii = 0; ii < MD5_DIGEST_SIZE; ++ii) {
    buf[2*ii] = hexDigits[digest[ii] >> 4];
    buf[2*ii+1] = hexDigits[digest[ii] & 0x0F];
  }
  buf[2*MD5_DIGEST_SIZE] = '\0';
  return std::string(buf);
}

// unguessable random token (for session cookies and whiteboard tokens)
static std::string randomToken()
{
  static std::random_device rd;
  static const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string s(20, ' ');
  for(char& c : s) c = chars[rd() % 36];
  return s;
}

static int hexVal(char c)
{
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// decode as node.js querystring does: '+' -> ' ', %XX
static std::string urlDecode(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for(size_t ii = 0; ii < s.size(); ++ii) {
    if(s[ii] == '+')
      out.push_back(' ');
    else if(s[ii] == '%' && ii + 2 < s.size() && hexVal(s[ii+1]) >= 0 && hexVal(s[ii+2]) >= 0) {
      out.push_back(char(hexVal(s[ii+1])*16 + hexVal(s[ii+2])));
      ii += 2;
    }
    else
      out.push_back(s[ii]);
  }
  return out;
}

typedef std::vector<std::pair<std::string, std::string>> QueryArgs;

struct HTTPRequest
{
  std::string reqline, method, target, path;
  QueryArgs args;
  std::string cookie, useragent, proxyUser, realIP, forwardedFor;
  bool keepAlive = false;
  bool expect100 = false;
  bool chunked = false;
  long long contentLength = 0;
  std::string body;
};

// whiteboard client using HTTP relay (long polling)
struct SWBPollClient
{
  std::string id;  // secret session id (from /v1/swb/start)
  std::string user;
  std::string remote;
  SWBServer::Whiteboard* wb = NULL;  // NULL after leaving whiteboard (e.g. kicked)
  std::string deniedMsg;  // if kicked, returned to next request
  SWBConn* poll = NULL;  // connection w/ pending poll request
  size_t pollOffset = 0;
  SWBServer::Clock::time_point pollDeadline;
  SWBServer::Clock::time_point lastSeen;
  long long lastSeq = 0;
};

// split "/path?a=1&b=2" into path and args (in order; first occurrence wins in getArg())
static std::string parseTarget(const std::string& target, QueryArgs& args)
{
  size_t q = target.find('?');
  if(q == std::string::npos)
    return urlDecode(target);
  std::string query = target.substr(q + 1);
  size_t pos = 0;
  while(pos <= query.size()) {
    size_t amp = query.find('&', pos);
    if(amp == std::string::npos) amp = query.size();
    std::string kv = query.substr(pos, amp - pos);
    if(!kv.empty()) {
      size_t eq = kv.find('=');
      if(eq == std::string::npos)
        args.emplace_back(urlDecode(kv), "");
      else
        args.emplace_back(urlDecode(kv.substr(0, eq)), urlDecode(kv.substr(eq + 1)));
    }
    pos = amp + 1;
  }
  return urlDecode(target.substr(0, q));
}

static const std::string* getArg(const QueryArgs& args, const char* key)
{
  for(auto& kv : args) {
    if(kv.first == key)
      return &kv.second;
  }
  return NULL;
}

static std::string argStr(const QueryArgs& args, const char* key, const char* dflt = "")
{
  const std::string* s = getArg(args, key);
  return s ? *s : std::string(dflt);
}

static std::string xmlEscape(const std::string& s)
{
  std::string out;
  for(char c : s) {
    switch(c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '\'': out += "&apos;"; break;
    case '"': out += "&quot;"; break;
    default: out.push_back(c);
    }
  }
  return out;
}

static bool isXmlName(const std::string& s)
{
  if(s.empty() || !(isAlpha(s[0]) || s[0] == '_'))
    return false;
  for(char c : s) {
    if(!(isAlpha(c) || isDigit(c) || c == '_' || c == '-'))
      return false;
  }
  return true;
}

static double secsSince(SWBServer::Clock::time_point t)
{
  return std::chrono::duration<double>(SWBServer::Clock::now() - t).count();
}

static std::string addrToStr(struct unet_addr* addr)
{
  char buf[256];
  return unet_address_str(addr, buf, sizeof(buf)) == 0 ? std::string(buf) : std::string("?");
}

// Whiteboard

void SWBServer::Whiteboard::append(Chunk data)
{
  if(!data || data->empty()) return;
  histStart.push_back(histSize);
  history.push_back(data);
  histSize += data->size();
  // merge trailing small chunks so replaying history doesn't require a huge number of tiny sends
  if(data->size() < HIST_MERGE_SIZE && ++unmergedChunks >= HIST_MERGE_COUNT) {
    size_t n = 0, bytes = 0;
    while(n < history.size() && history[history.size() - 1 - n]->size() < HIST_MERGE_SIZE && n < unmergedChunks) {
      bytes += history[history.size() - 1 - n]->size();
      ++n;
    }
    if(n > 1) {
      std::string merged;
      merged.reserve(bytes);
      size_t first = history.size() - n;
      for(size_t ii = first; ii < history.size(); ++ii)
        merged.append(*history[ii]);
      size_t start = histStart[first];
      history.resize(first);
      histStart.resize(first);
      history.push_back(std::make_shared<const std::string>(std::move(merged)));
      histStart.push_back(start);
    }
    unmergedChunks = 0;
  }
}

// SWBServer

SWBServer::SWBServer(const SWBServerOptions& o) : opts(o), stopFlag(false) {}

SWBServer::~SWBServer()
{
  for(auto& c : conns) {
    if(c->fd >= 0) unet_close(c->fd);
  }
  if(apiListen >= 0) unet_close(apiListen);
  if(swbListen >= 0) unet_close(swbListen);
  if(logOut && logOut != stderr) fclose(logOut);
}

void SWBServer::log(int level, const char* fmt, ...)
{
  if(level > opts.logLevel) return;
  static const char* levels[] = {"ERROR", "WARN", "INFO", "DEBUG"};
  char tbuf[32];
  time_t now = time(NULL);
  struct tm tmv;
#ifdef _WIN32
  gmtime_s(&tmv, &now);
#else
  gmtime_r(&now, &tmv);
#endif
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
  va_list va;
  va_start(va, fmt);
  std::string msg = fstring("%s UTC %s: ", tbuf, levels[std::min(std::max(level, 0), 3)]);
  char buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, va);
  va_end(va);
  msg.append(buf).append("\n");
  FILE* f = logOut ? logOut : stderr;
  fputs(msg.c_str(), f);
  fflush(f);
}

// create listening socket (all interfaces if host is empty; port 0 to choose free port); returns -1 on error
static int listenSocket(const std::string& host, int port, std::string* err)
{
  int s = unet_socket(UNET_TCP, UNET_BIND, UNET_NOBLOCK, host.empty() ? NULL : host.c_str(), std::to_string(port).c_str());
  if(s < 0 || unet_listen(s, 128) != 0) {
    *err = fstring("unable to listen on %s port %d (error %d)", host.empty() ? "all interfaces" : host.c_str(), port, unet_errno());
    if(s >= 0) unet_close(s);
    return -1;
  }
  return s;
}

static int socketPort(int s)
{
  struct unet_addr addr;
  return unet_address(s, &addr) == 0 ? unet_address_port(&addr) : -1;
}

bool SWBServer::start()
{
  if(!opts.logFile.empty()) {
    logOut = fopen(opts.logFile.c_str(), "a");
    if(!logOut) {
      log(0, "unable to open log file %s", opts.logFile.c_str());
      return false;
    }
  }
  std::string err;
  // relay port is always API port + 1 (client expects this)
  for(int attempt = 0; attempt < 20; ++attempt) {
    apiListen = listenSocket(opts.host, opts.port, &err);
    if(apiListen < 0) break;
    boundPort = socketPort(apiListen);
    swbListen = listenSocket(opts.host, boundPort + 1, &err);
    if(swbListen >= 0 || opts.port != 0) break;
    unet_close(apiListen);  // automatic port: try again
    apiListen = -1;
  }
  if(apiListen < 0 || swbListen < 0) {
    log(0, "%s", err.c_str());
    return false;
  }
  if(!opts.trustProxy)
    log(1, "No authentication - all users are accepted; run only on a trusted network");
  else if(opts.host != "127.0.0.1" && opts.host != "::1" && opts.host != "localhost")
    log(1, "--trust-proxy w/o --host=127.0.0.1: proxy headers can be forged by clients connecting directly");
  log(2, "Whiteboard server listening on %s port %d (API) and %d (whiteboard)",
      opts.host.empty() ? "all interfaces" : opts.host.c_str(), boundPort, boundPort + 1);
  lastSweep = Clock::now();
  return true;
}

void SWBServer::run()
{
  std::vector<struct unet_pollfd> pfds;
  std::vector<SWBConn*> pconns;
  while(!stopFlag) {
    pfds.clear();
    pconns.clear();
    // listeners are ignored while accept is paused
    bool acceptOK = Clock::now() >= acceptPausedUntil;
    for(int lfd : {apiListen, swbListen}) {
      struct unet_pollfd p;
      p.sock = acceptOK ? lfd : -1;
      p.events = UNET_POLLIN;
      pfds.push_back(p);
      pconns.push_back(NULL);
    }
    for(auto& c : conns) {
      struct unet_pollfd p;
      p.sock = c->fd;
      p.events = (c->closing && !c->halfClosed ? 0 : UNET_POLLIN) | (c->outq.empty() ? 0 : UNET_POLLOUT);
      pfds.push_back(p);
      pconns.push_back(c.get());
    }
    // timeout is short so that stop() is noticed promptly and timers are serviced
    if(unet_poll(pfds.data(), int(pfds.size()), 0.2) < 0) {
      log(0, "poll failed: error %d", unet_errno());
      break;
    }
    if(pfds[0].revents & UNET_POLLIN) acceptConns(apiListen, CONN_HTTP);
    if(pfds[1].revents & UNET_POLLIN) acceptConns(swbListen, CONN_SWB);
    for(size_t ii = 2; ii < pfds.size(); ++ii) {
      SWBConn* c = pconns[ii];
      short rev = pfds[ii].revents;
      if(!rev || c->dead) continue;
      if(rev & UNET_POLLOUT)
        flush(c);
      if(!c->dead && (rev & (UNET_POLLIN | UNET_POLLHUP | UNET_POLLERR)))
        onReadable(c);
    }
    // pipelined HTTP requests that arrived while waiting to answer a poll
    for(size_t ii = 0; ii < conns.size(); ++ii) {
      SWBConn* c = conns[ii].get();
      if(c->resumeInput && !c->dead) {
        c->resumeInput = false;
        processHTTP(c);
      }
    }
    doTimers();
    // reap closed connections
    conns.erase(std::remove_if(conns.begin(), conns.end(), [](const std::unique_ptr<SWBConn>& c){
      if(c->dead && c->pollWait) {
        // client will repeat poll (w/ same offset) on new connection
        c->pollWait->poll = NULL;
        c->pollWait->lastSeen = Clock::now();
        c->pollWait = NULL;
      }
      if(c->dead && c->fd >= 0) { unet_close(c->fd); c->fd = -1; }
      return c->dead;
    }), conns.end());
  }
  log(2, "Whiteboard server shutting down");
}

void SWBServer::acceptConns(int listenfd, int kind)
{
  for(;;) {
    struct unet_addr addr;
    int fd = unet_accept(listenfd, &addr);
    if(fd < 0) {
      int e = unet_errno();
      if(!unet_would_block(e)) {
        // e.g. too many open files - pause so we don't spin w/ listener always readable
        log(1, "accept failed: error %d", e);
        acceptPausedUntil = Clock::now() + std::chrono::seconds(1);
      }
      return;
    }
    // small whiteboard messages should go out immediately
    if(unet_set_flags(fd, UNET_NOBLOCK | (kind == CONN_SWB ? UNET_NODELAY : 0)) != 0) {
      unet_close(fd);
      continue;
    }
    std::unique_ptr<SWBConn> c(new SWBConn);
    c->fd = fd;
    c->kind = kind;
    c->remote = addrToStr(&addr);
    c->lastProgress = Clock::now();
    c->lastActivity = c->lastProgress;
    if(kind == CONN_SWB)
      log(2, "%s connected", c->remote.c_str());
    conns.push_back(std::move(c));
  }
}

void SWBServer::onReadable(SWBConn* c)
{
  char buf[65536];
  int n = unet_recv(c->fd, buf, int(sizeof(buf)));
  if(n < 0 && unet_would_block(unet_errno()))
    return;
  if(n <= 0) {
    // peer closed or error
    if(c->kind == CONN_SWB && !c->closing)
      log(1, "%s disconnected (%s)", c->remote.c_str(), n == 0 ? "stream end" : "stream error");
    leaveWhiteboard(c);
    c->dead = true;
    return;
  }
  c->lastActivity = Clock::now();
  if(c->closing)
    return;  // discard input after close requested
  if(opts.dump && opts.logLevel >= 3)
    log(3, "rcvd from %s: %s", c->remote.c_str(), std::string(buf, n).c_str());
  c->inbuf.append(buf, n);
  if(c->kind == CONN_SWB)
    processSWB(c);
  else
    processHTTP(c);
}

void SWBServer::queue(SWBConn* c, Chunk data, size_t pos, size_t end)
{
  if(c->dead || c->halfClosed) return;
  end = std::min(end, data->size());
  if(pos >= end) return;
  if(c->outq.empty())
    c->lastProgress = Clock::now();
  c->outq.push_back({data, pos, end});
  c->outBytes += end - pos;
  flush(c);  // send immediately if possible
}

void SWBServer::flush(SWBConn* c)
{
  while(!c->outq.empty() && !c->dead) {
    SWBConn::Seg& seg = c->outq.front();
    size_t len = std::min(seg.end - seg.pos, size_t(1) << 20);
    // SIGPIPE is ignored (by unet_init()), so a send to closed connection just returns an error
    int n = unet_send(c->fd, seg.data->data() + seg.pos, int(len));
    if(n < 0) {
      int e = unet_errno();
      if(unet_would_block(e)) return;
      log(1, "%s send error %d", c->remote.c_str(), e);
      leaveWhiteboard(c);
      c->dead = true;
      return;
    }
    c->lastProgress = Clock::now();
    seg.pos += n;
    c->outBytes -= n;
    if(seg.pos >= seg.end)
      c->outq.pop_front();
  }
  if(c->outq.empty() && c->closing && !c->halfClosed && !c->dead) {
    // all data sent - half close and wait (briefly) for peer to close so pending data isn't lost to a RST
    unet_shutdown(c->fd, UNET_SHUT_WR);
    c->halfClosed = true;
    c->closeDeadline = Clock::now() + std::chrono::seconds(LINGER_SECS);
  }
}

void SWBServer::closeConn(SWBConn* c, bool graceful)
{
  leaveWhiteboard(c);
  if(!graceful) {
    c->dead = true;
    return;
  }
  c->closing = true;
  flush(c);
}

void SWBServer::broadcast(Whiteboard* wb, Chunk data)
{
  wb->append(data);
  // copy since queue() can result in a client leaving (on send error)
  std::vector<SWBConn*> clients = wb->clients;
  for(SWBConn* c : clients)
    queue(c, data);
  // answer pending polls
  std::vector<SWBPollClient*> pollers = wb->pollers;
  for(SWBPollClient* pc : pollers) {
    if(pc->poll)
      respondPoll(pc);
  }
}

// queue history bytes [from, to) w/o copying
void SWBServer::queueHistory(SWBConn* c, Whiteboard* wb, size_t from, size_t to)
{
  to = std::min(to, wb->histSize);
  if(from >= to) return;
  size_t idx = std::upper_bound(wb->histStart.begin(), wb->histStart.end(), from) - wb->histStart.begin() - 1;
  for(; idx < wb->history.size() && wb->histStart[idx] < to && !c->dead; ++idx) {
    size_t start = wb->histStart[idx];
    queue(c, wb->history[idx], from > start ? from - start : 0, to - start);
  }
}

// remove client from whiteboard; equivalent of disconn() in node.js server
void SWBServer::leaveWhiteboard(SWBConn* c)
{
  Whiteboard* wb = c->wb;
  if(!wb) return;
  c->wb = NULL;
  auto it = std::find(wb->clients.begin(), wb->clients.end(), c);
  if(it == wb->clients.end()) return;
  wb->clients.erase(it);
  memberLeft(wb, c->user);
}

// called after any client (raw or HTTP relay) leaves whiteboard
void SWBServer::memberLeft(Whiteboard* wb, const std::string& user)
{
  if(wb->clients.empty() && wb->pollers.empty()) {
    // delete whiteboard after specified delay after last user disconnects
    wb->deletePending = true;
    wb->deleteAt = Clock::now() + std::chrono::milliseconds(int64_t(opts.delDelay*1000));
  }
  else
    broadcast(wb, std::make_shared<const std::string>("<undo><disconnect name='" + user + "'/></undo>\n"));
}

// validate start request (raw or HTTP relay); returns whiteboard (created for test token) or NULL w/ deniedMsg set
SWBServer::Whiteboard* SWBServer::checkStart(const std::string& repo, const std::string& user,
    const std::string& token, const std::string& proxyUser, const std::string& remote, std::string* deniedMsg)
{
  auto it = whiteboards.find(repo);
  Whiteboard* wb = it != whiteboards.end() ? it->second.get() : NULL;
  if(!proxyUser.empty() && user != proxyUser) {
    log(1, "%s: start user %s does not match proxy user %s", remote.c_str(), user.c_str(), proxyUser.c_str());
    *deniedMsg = "<undo><accessdenied message='Whiteboard user does not match login.'/></undo>\n";
    return NULL;
  }
  if(token == "SCRIBBLE_SYNC_TEST" && opts.enableTest) {
    log(2, "%s: connecting to test whiteboard %s as %s", remote.c_str(), repo.c_str(), user.c_str());
    if(!wb) {
      wb = new Whiteboard;
      wb->name = repo;
      whiteboards[repo].reset(wb);
    }
    return wb;
  }
  if(!wb || token != md5hex(user + wb->token)) {
    log(1, "%s: whiteboard not found or invalid token", remote.c_str());
    *deniedMsg = "<undo><accessdenied message='Whiteboard not found. Please try again.'/></undo>\n";
    return NULL;
  }
  return wb;
}

// if user was already connected as a different client, remove old client (call after adding new client, so
//  whiteboard isn't deleted if old client was the only client)
void SWBServer::kickUser(Whiteboard* wb, const std::string& user, SWBConn* exceptConn, SWBPollClient* exceptPoll)
{
  static const char* msg = "<undo><accessdenied message='User logged in from another location.'/></undo>\n";
  std::vector<SWBConn*> clients = wb->clients;
  for(SWBConn* old : clients) {
    if(old != exceptConn && old->user == user) {
      log(2, "disconnecting %s due to new connection for user: %s", old->remote.c_str(), user.c_str());
      queue(old, msg);
      closeConn(old, true);
    }
  }
  std::vector<SWBPollClient*> pollers = wb->pollers;
  for(SWBPollClient* old : pollers) {
    if(old != exceptPoll && old->user == user) {
      log(2, "disconnecting %s (HTTP) due to new connection for user: %s", old->remote.c_str(), user.c_str());
      old->deniedMsg = msg;
      wb->pollers.erase(std::find(wb->pollers.begin(), wb->pollers.end(), old));
      old->wb = NULL;
      memberLeft(wb, user);
      // client gets message on next request (or now if poll is pending)
      if(old->poll) {
        respondPoll(old);
        removePollClient(old);
      }
    }
  }
}

// shared whiteboard relay protocol - newline terminated commands; /data?length=N is followed by N bytes of data
//  which are appended to whiteboard history and echoed to all clients (including sender)
void SWBServer::processSWB(SWBConn* c)
{
  size_t pos = 0;
  std::string& in = c->inbuf;
  while(pos < in.size() && !c->closing && !c->dead) {
    if(c->expectData > 0) {
      size_t n = std::min(c->expectData, in.size() - pos);
      c->dataBuf.append(in, pos, n);
      pos += n;
      c->expectData -= n;
      if(c->expectData > 0)
        break;
      log(3, "rcvd %d bytes of data from %s", int(c->dataBuf.size()), c->remote.c_str());
      Chunk data = std::make_shared<const std::string>(std::move(c->dataBuf));
      c->dataBuf = std::string();
      if(c->wb)
        broadcast(c->wb, data);
      continue;
    }
    size_t nl = in.find('\n', pos);
    if(nl == std::string::npos) {
      if(in.size() - pos > MAX_CMD_LEN) {
        log(1, "%s sent invalid command (too long)", c->remote.c_str());
        closeConn(c, false);
      }
      break;
    }
    std::string cmd = in.substr(pos, nl - pos);
    pos = nl + 1;
    if(!cmd.empty() && cmd.back() == '\r') cmd.pop_back();
    if(!handleSWBCommand(c, cmd))
      break;
  }
  in.erase(0, pos);
}

// returns false if connection is being closed
bool SWBServer::handleSWBCommand(SWBConn* c, const std::string& cmdstr)
{
  log(3, "%s sent command: %s", c->remote.c_str(), cmdstr.c_str());
  QueryArgs args;
  std::string command = cmdstr.size() > 0 && cmdstr[0] == '/' ? parseTarget(cmdstr, args) : "";
  if(command == "/info") {
    // /info?document=<docname> - list of current users
    auto it = whiteboards.find(argStr(args, "document"));
    std::vector<std::string> names;
    if(it != whiteboards.end()) {
      for(SWBConn* cl : it->second->clients)
        names.push_back(cl->user);
      for(SWBPollClient* pc : it->second->pollers)
        names.push_back(pc->user);
    }
    queue(c, it != whiteboards.end() ? joinStr(names, ",") : std::string("-"));
  }
  else if(command == "/start") {
    // args: version (protocol version, ignored), user, document, offset (history offset, > 0 when reconnecting),
    //  uuid (echoed in connect message), token = md5(user + whiteboard.token)
    std::string repo = argStr(args, "document");
    std::string user = argStr(args, "user");
    if(c->wb) {
      log(1, "%s: /start received while already connected", c->remote.c_str());
      closeConn(c, false);
      return false;
    }
    std::string denied;
    Whiteboard* wb = checkStart(repo, user, argStr(args, "token"), c->proxyUser, c->remote, &denied);
    if(!wb) {
      queue(c, denied);
      closeConn(c, true);
      return false;
    }
    c->user = user;
    // send history
    long long offset = atoll(argStr(args, "offset", "0").c_str());
    queueHistory(c, wb, size_t(std::max(offset, 0LL)), wb->histSize);
    if(c->dead) return false;
    c->wb = wb;
    wb->clients.push_back(c);
    wb->deletePending = false;
    kickUser(wb, user, c, NULL);
    // client uses uuid to distinguish this connect message from previous ones when reconnecting
    std::string msg = "<undo><connect name='" + user + "' uuid='" + argStr(args, "uuid", "undefined") + "'/></undo>\n";
    broadcast(wb, std::make_shared<const std::string>(std::move(msg)));
  }
  else if(command == "/data") {
    long long len = atoll(argStr(args, "length", "0").c_str());
    if(!c->wb || len < 0 || size_t(len) > opts.maxDataLen) {
      log(1, "%s: invalid /data (%s)", c->remote.c_str(), c->wb ? "bad length" : "not connected to whiteboard");
      closeConn(c, false);
      return false;
    }
    c->expectData = size_t(len);
    c->dataBuf.reserve(size_t(len));
  }
  else if(command == "/end") {
    log(2, "%s disconnected (/end)", c->remote.c_str());
    closeConn(c, true);
    return false;
  }
  else {
    log(1, "%s sent invalid command: %s", c->remote.c_str(), cmdstr.substr(0, 200).c_str());
    closeConn(c, false);
    return false;
  }
  return true;
}

// parse request line and headers; returns false if malformed
static bool parseHTTPHeader(const std::string& hdr, HTTPRequest& req)
{
  size_t eol = hdr.find("\r\n");
  req.reqline = hdr.substr(0, eol);
  std::vector<std::string> parts = splitStr<std::vector>(req.reqline.c_str(), ' ', true);
  if(parts.size() != 3 || parts[2].compare(0, 5, "HTTP/") != 0)
    return false;
  req.method = parts[0];
  req.target = parts[1];
  req.path = parseTarget(req.target, req.args);
  bool http11 = parts[2] != "HTTP/1.0";
  std::string connection;
  size_t pos = eol;
  while(pos != std::string::npos && pos < hdr.size()) {
    pos += 2;
    size_t next = hdr.find("\r\n", pos);
    std::string line = hdr.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
    size_t colon = line.find(':');
    if(colon != std::string::npos) {
      std::string name = toLower(trimStr(line.substr(0, colon)));
      std::string value = trimStr(line.substr(colon + 1));
      if(name == "cookie") req.cookie = req.cookie.empty() ? value : req.cookie + "; " + value;
      else if(name == "user-agent" || name == "user agent") req.useragent = value;  // Write sends "User Agent"
      else if(name == "connection") connection = toLower(value);
      else if(name == "content-length") {
        if(value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return false;
        req.contentLength = atoll(value.c_str());
      }
      else if(name == "transfer-encoding") req.chunked = true;  // not supported
      else if(name == "expect") req.expect100 = toLower(value) == "100-continue";
      else if(name == "x-remote-user") req.proxyUser = value;
      else if(name == "x-real-ip") req.realIP = value;
      else if(name == "x-forwarded-for") req.forwardedFor = req.forwardedFor.empty() ? value : req.forwardedFor + "," + value;
    }
    pos = next;
  }
  if(connection.find("close") != std::string::npos) req.keepAlive = false;
  else if(connection.find("keep-alive") != std::string::npos) req.keepAlive = true;
  else req.keepAlive = http11;
  return true;
}

// HTTP/1.1 w/ keep-alive; requests are processed in order (pipelining), so if response to poll request is
//  deferred, subsequent requests wait
void SWBServer::processHTTP(SWBConn* c)
{
  while(c->kind == CONN_HTTP && !c->closing && !c->dead && !c->pollWait) {
    if(!c->req) {
      size_t hdrend = c->inbuf.find("\r\n\r\n");
      if(hdrend == std::string::npos) {
        if(c->inbuf.size() > MAX_HTTP_HEADER)
          sendHTTP(c, 431, "", "", false);
        return;
      }
      std::unique_ptr<HTTPRequest> req(new HTTPRequest);
      bool ok = parseHTTPHeader(c->inbuf.substr(0, hdrend), *req);
      c->inbuf.erase(0, hdrend + 4);
      if(!ok) {
        log(1, "%s - malformed request", c->remote.c_str());
        sendHTTP(c, 400, "", "", false);
        return;
      }
      if(opts.trustProxy) {
        // last X-Forwarded-For entry is the one added by our proxy (earlier entries come from client)
        size_t comma = req->forwardedFor.rfind(',');
        std::string fwd = trimStr(comma == std::string::npos ? req->forwardedFor : req->forwardedFor.substr(comma + 1));
        if(!req->realIP.empty()) c->remote = req->realIP;
        else if(!fwd.empty()) c->remote = fwd;
      }
      else
        req->proxyUser.clear();
      c->proxyUser = req->proxyUser;
      if(req->chunked || req->contentLength < 0 || size_t(req->contentLength) > opts.maxDataLen) {
        log(1, "%s - \"%s\" - %s", c->remote.c_str(), req->reqline.c_str(),
            req->chunked ? "chunked request body not supported" : "request body too large");
        sendHTTP(c, req->chunked ? 411 : 413, "", "", false);
        return;
      }
      if(req->expect100 && c->inbuf.size() < size_t(req->contentLength))
        queue(c, "HTTP/1.1 100 Continue\r\n\r\n");
      c->req = std::move(req);
    }
    size_t len = size_t(c->req->contentLength);
    if(c->inbuf.size() < len)
      return;
    std::unique_ptr<HTTPRequest> req = std::move(c->req);
    req->body = c->inbuf.substr(0, len);
    c->inbuf.erase(0, len);
    handleRequest(c, *req);
  }
}

void SWBServer::handleRequest(SWBConn* c, HTTPRequest& req)
{
  if(req.path.compare(0, 8, "/v1/swb/") == 0) {
    handleRelayHTTP(c, req);
    return;
  }
  int status = 400;
  std::string setCookie;
  std::string body = handleHTTP(req.method, req.target, req.cookie, c->remote, req.proxyUser, &status, &setCookie);
  log(2, "%s - \"%s\" %d - \"%s\"", c->remote.c_str(), req.reqline.c_str(), status, req.useragent.c_str());
  sendHTTP(c, status, body, setCookie.empty() ? "" : "Set-Cookie: " + setCookie + "\r\n", req.keepAlive);
}

static std::string httpHeader(int status, size_t contentLength, const char* contentType, bool keepAlive)
{
  static const std::map<int, const char*> reasons = {{200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"},
      {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"}, {408, "Request Timeout"},
      {411, "Length Required"}, {413, "Payload Too Large"}, {431, "Request Header Fields Too Large"}};
  auto reason = reasons.find(status);
  return fstring("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
      "Connection: %s\r\n", status, reason != reasons.end() ? reason->second : "Error", contentType,
      int(contentLength), keepAlive ? "keep-alive" : "close");
}

void SWBServer::sendHTTP(SWBConn* c, int status, const std::string& body, const std::string& extraHeaders, bool keepAlive)
{
  queue(c, httpHeader(status, body.size(), "text/plain", keepAlive) + extraHeaders + "\r\n" + body);
  if(!keepAlive)
    closeConn(c, true);
}

// HTTP relay - same semantics as raw relay, but w/ plain HTTP requests so clients can use platform HTTP(S) stack:
//  POST /v1/swb/start?document=&user=&token=&uuid=  - join whiteboard; returns session id as body
//  GET /v1/swb/poll?id=&offset=N[&wait=0]  - returns history from offset N (i.e. client's total bytes received),
//   waiting up to pollTimeout for new data if none (returning empty body on timeout), unless wait=0
//  POST /v1/swb/send?id=&seq=K  - body (one or more complete messages) is appended to history and broadcast;
//   requests w/ seq <= last seq are ignored, so failed requests can be retried
//  POST /v1/swb/end?id=  - leave whiteboard
// If access is denied or client is kicked (same user connecting elsewhere), status is 403 w/ <accessdenied> message
//  as body; status 404 means session not found (e.g. expired) - client should start a new session
void SWBServer::handleRelayHTTP(SWBConn* c, HTTPRequest& req)
{
  std::string cmd = req.path.substr(8);
  auto reply = [&](int status, const std::string& body, int loglevel) {
    log(loglevel, "%s - \"%s %s\" %d", c->remote.c_str(), req.method.c_str(), req.path.c_str(), status);
    sendHTTP(c, status, body, "", req.keepAlive);
  };
  if(cmd == "start") {
    if(req.method != "POST") return reply(405, "", 1);
    std::string user = argStr(req.args, "user");
    std::string denied;
    Whiteboard* wb = checkStart(argStr(req.args, "document"), user, argStr(req.args, "token"), req.proxyUser,
        c->remote, &denied);
    if(!wb) return reply(403, denied, 1);
    std::unique_ptr<SWBPollClient> pc(new SWBPollClient);
    do { pc->id = randomToken(); } while(pollClients.count(pc->id));
    pc->user = user;
    pc->remote = c->remote;
    pc->wb = wb;
    pc->lastSeen = Clock::now();
    SWBPollClient* p = pc.get();
    pollClients[pc->id] = std::move(pc);
    wb->pollers.push_back(p);
    wb->deletePending = false;
    kickUser(wb, user, NULL, p);
    log(2, "%s connected (HTTP) to %s as %s", c->remote.c_str(), wb->name.c_str(), user.c_str());
    broadcast(wb, std::make_shared<const std::string>(
        "<undo><connect name='" + user + "' uuid='" + argStr(req.args, "uuid", "undefined") + "'/></undo>\n"));
    return reply(200, p->id, 3);
  }

  auto it = pollClients.find(argStr(req.args, "id"));
  if(it == pollClients.end()) return reply(404, "", 1);
  SWBPollClient* pc = it->second.get();
  if(!req.proxyUser.empty() && req.proxyUser != pc->user) return reply(403, "", 1);
  pc->lastSeen = Clock::now();
  if(!pc->wb) {
    // kicked
    std::string msg = pc->deniedMsg;
    removePollClient(pc);
    return reply(403, msg, 2);
  }
  if(cmd == "poll") {
    if(req.method != "GET") return reply(405, "", 1);
    long long offset = atoll(argStr(req.args, "offset", "0").c_str());
    if(offset < 0 || size_t(offset) > pc->wb->histSize) return reply(400, "invalid offset", 1);
    if(pc->poll)
      respondPoll(pc);  // previous poll (client must have given up on it) - answer w/ whatever is available
    log(3, "%s - poll offset %lld", c->remote.c_str(), offset);
    pc->poll = c;
    pc->pollOffset = size_t(offset);
    pc->pollDeadline = Clock::now() + std::chrono::milliseconds(int64_t(opts.pollTimeout*1000));
    c->pollWait = pc;
    c->pollKeepAlive = req.keepAlive;
    // wait=0: respond immediately even if no data (client checking if session is still valid)
    if(pc->pollOffset < pc->wb->histSize || argStr(req.args, "wait") == "0")
      respondPoll(pc);
    return;
  }
  if(cmd == "send") {
    if(req.method != "POST") return reply(405, "", 1);
    const std::string* seqarg = getArg(req.args, "seq");
    long long seq = seqarg ? atoll(seqarg->c_str()) : 0;
    if(seqarg && seq <= pc->lastSeq) return reply(200, "", 2);  // duplicate
    if(seqarg) pc->lastSeq = seq;
    log(3, "rcvd %d bytes of data from %s (HTTP)", int(req.body.size()), c->remote.c_str());
    if(opts.dump && opts.logLevel >= 3)
      log(3, "rcvd from %s: %s", c->remote.c_str(), req.body.c_str());
    if(!req.body.empty())
      broadcast(pc->wb, std::make_shared<const std::string>(std::move(req.body)));
    return reply(200, "", 3);
  }
  if(cmd == "end") {
    if(req.method != "POST") return reply(405, "", 1);
    log(2, "%s disconnected (HTTP end)", c->remote.c_str());
    removePollClient(pc);
    return reply(200, "", 3);
  }
  reply(404, "", 1);
}

// answer pending poll w/ history from poll offset (empty if none, e.g. on timeout), or deniedMsg if kicked
void SWBServer::respondPoll(SWBPollClient* pc)
{
  SWBConn* c = pc->poll;
  if(!c) return;
  pc->poll = NULL;
  pc->lastSeen = Clock::now();
  c->pollWait = NULL;
  bool keepAlive = c->pollKeepAlive;
  if(!pc->wb) {
    sendHTTP(c, 403, pc->deniedMsg, "", keepAlive);
  }
  else {
    size_t from = pc->pollOffset;
    size_t to = std::min(pc->wb->histSize, from + MAX_POLL_BYTES);
    queue(c, httpHeader(200, to - from, "application/octet-stream", keepAlive) + "\r\n");
    queueHistory(c, pc->wb, from, to);
    if(!keepAlive)
      closeConn(c, true);
  }
  if(keepAlive && !c->inbuf.empty())
    c->resumeInput = true;
}

// remove HTTP relay client, leaving whiteboard if necessary
void SWBServer::removePollClient(SWBPollClient* pc)
{
  if(pc->poll) {
    pc->poll->pollWait = NULL;
    closeConn(pc->poll, false);  // pending poll can't be answered properly
    pc->poll = NULL;
  }
  Whiteboard* wb = pc->wb;
  if(wb) {
    pc->wb = NULL;
    wb->pollers.erase(std::find(wb->pollers.begin(), wb->pollers.end(), pc));
    memberLeft(wb, pc->user);
  }
  pollClients.erase(pc->id);  // deletes pc
}

// returns response body; for successful auth, sets setCookie to "session=<token>"
std::string SWBServer::handleHTTP(const std::string& method, const std::string& target,
    const std::string& cookie, const std::string& remote, const std::string& proxyUser, int* status,
    std::string* setCookie)
{
  QueryArgs args;
  std::string path = parseTarget(target, args);
  if(method != "GET" && method != "HEAD") {
    *status = 405;
    return "";
  }
  if(path == "/v1/debug" && opts.enableTest) {
    *status = 200;
    return debugInfo();
  }
  if(path == "/v1/auth") {
    // no authentication: user, signature, and timestamp args are ignored except for user name; user
    //  authenticated by reverse proxy (if trustProxy) takes precedence
    std::string user = proxyUser.empty() ? argStr(args, "user") : proxyUser;
    std::string token = randomToken();
    sessions[token] = Session{user, Clock::now()};
    *status = 200;
    *setCookie = "session=" + token;
    return "";
  }
  // verify session cookie for all other paths
  std::string sessionid;
  for(const std::string& kv : splitStr<std::vector>(cookie.c_str(), ';')) {
    size_t eq = kv.find('=');
    if(eq != std::string::npos && trimStr(kv.substr(0, eq)) == "session")
      sessionid = trimStr(kv.substr(eq + 1));
  }
  auto sit = sessions.find(sessionid);
  if(sit == sessions.end()) {
    *status = 403;
    return "";
  }
  if(secsSince(sit->second.ctime) > SESSION_LIFETIME_SECS) {
    sessions.erase(sit);
    *status = 408;
    return "error: session expired";
  }
  const std::string& user = sit->second.user;
  if(!proxyUser.empty() && proxyUser != user) {
    log(1, "%s: session user %s does not match proxy user %s", remote.c_str(), user.c_str(), proxyUser.c_str());
    *status = 403;
    return "";
  }

  if(path == "/v1/createswb" || path == "/v1/openswb") {
    bool create = path == "/v1/createswb";
    std::string repo = argStr(args, "name");
    auto it = whiteboards.find(repo);
    // whiteboard kept for delDelay after all clients left can be replaced by createswb (but not a new whiteboard
    //  which nobody has connected to yet, i.e. w/ empty history)
    if(create && it != whiteboards.end() && it->second->clients.empty() && it->second->pollers.empty()
        && it->second->histSize > 0) {
      log(2, "%s replacing unused whiteboard %s", user.c_str(), repo.c_str());
      whiteboards.erase(it);
      it = whiteboards.end();
    }
    if(repo.empty() || (create && it != whiteboards.end()) || (!create && it == whiteboards.end())) {
      *status = 404;
      return "";
    }
    if(create) {
      // all args (name, title, rxonly, ...) are passed back to clients opening the whiteboard
      std::string attribs;
      for(auto& kv : args) {
        if(!isXmlName(kv.first) || kv.first == "user" || kv.first == "token" || kv.first == "relay") continue;
        if(attribs.find(" " + kv.first + "='") != std::string::npos) continue;  // duplicate
        attribs += " " + kv.first + "='" + xmlEscape(kv.second) + "'";
      }
      Whiteboard* wb = new Whiteboard;
      wb->name = repo;
      wb->attribs = attribs;
      wb->token = randomToken();
      // delete if nobody connects
      wb->deletePending = true;
      wb->deleteAt = Clock::now() + std::chrono::seconds(SESSION_LIFETIME_SECS)
          + std::chrono::milliseconds(int64_t(opts.delDelay*1000));
      it = whiteboards.emplace(repo, std::unique_ptr<Whiteboard>(wb)).first;
      log(2, "%s created whiteboard %s", user.c_str(), repo.c_str());
    }
    Whiteboard* wb = it->second.get();
    *status = 200;
    // relay='http' tells client it can use HTTP relay on this port instead of raw relay on port + 1
    return "<swb" + wb->attribs + " user='" + xmlEscape(user) + "' token='" + md5hex(user + wb->token)
        + "' relay='http'/>";
  }
  *status = 404;
  return "";
}

void SWBServer::doTimers()
{
  auto now = Clock::now();
  for(auto it = whiteboards.begin(); it != whiteboards.end();) {
    Whiteboard* wb = it->second.get();
    if(wb->deletePending && wb->clients.empty() && wb->pollers.empty() && now >= wb->deleteAt) {
      log(2, "deleting whiteboard: %s", wb->name.c_str());
      it = whiteboards.erase(it);
    }
    else
      ++it;
  }
  for(auto& c : conns) {
    if(c->dead) continue;
    if(c->halfClosed && now >= c->closeDeadline)
      c->dead = true;
    else if(!c->outq.empty() && secsSince(c->lastProgress) > opts.sendTimeout) {
      log(1, "%s: send timeout w/ %d bytes queued; disconnecting", c->remote.c_str(), int(c->outBytes));
      closeConn(c.get(), false);
    }
    else if(c->kind == CONN_HTTP && !c->closing && !c->pollWait && c->outq.empty()
        && secsSince(c->lastActivity) > HTTP_IDLE_SECS)
      closeConn(c.get(), false);  // idle keep-alive connection (or incomplete request)
  }
  // HTTP relay: answer polls w/ no new data before timeout; disconnect clients that stop polling
  std::vector<SWBPollClient*> expired;
  for(auto& kv : pollClients) {
    SWBPollClient* pc = kv.second.get();
    if(pc->poll && now >= pc->pollDeadline)
      respondPoll(pc);
    else if(!pc->poll && secsSince(pc->lastSeen) > opts.pollClientTimeout)
      expired.push_back(pc);
  }
  for(SWBPollClient* pc : expired) {
    log(pc->wb ? 1 : 3, "%s (HTTP) timed out", pc->remote.c_str());
    removePollClient(pc);
  }
  // expired sessions
  if(secsSince(lastSweep) > 30) {
    lastSweep = now;
    for(auto it = sessions.begin(); it != sessions.end();) {
      if(secsSince(it->second.ctime) > SESSION_LIFETIME_SECS) it = sessions.erase(it);
      else ++it;
    }
  }
}

std::string SWBServer::debugInfo()
{
  std::string s = "whiteboards:\n";
  for(auto& kv : whiteboards) {
    Whiteboard* wb = kv.second.get();
    std::vector<std::string> names;
    for(SWBConn* c : wb->clients) names.push_back(c->user + "@" + c->remote);
    for(SWBPollClient* pc : wb->pollers) names.push_back(pc->user + "@" + pc->remote + " (HTTP)");
    s += fstring("  %s: %d bytes history (%d chunks), clients: %s%s\n", wb->name.c_str(), int(wb->histSize),
        int(wb->history.size()), joinStr(names, ", ").c_str(), wb->deletePending ? " (delete pending)" : "");
  }
  s += fstring("sessions: %d\nHTTP relay clients: %d\nconnections: %d\n", int(sessions.size()),
      int(pollClients.size()), int(conns.size()));
  for(auto& c : conns)
    s += fstring("  %s %s: %d bytes queued\n", c->kind == CONN_SWB ? "SWB" : "HTTP", c->remote.c_str(), int(c->outBytes));
  return s;
}

// command line

static SWBServer* activeServer = NULL;

static void onSignal(int) { if(activeServer) activeServer->stop(); }

static int serverUsage()
{
  fprintf(stderr,
      "Usage: Write --server [options]\n"
      "Runs shared whiteboard server for Write\n"
      "  --host=<address>       address to listen on (default: all interfaces)\n"
      "  --port=<port>          HTTP API port (default 7000); whiteboard data uses port + 1\n"
      "  --del-delay=<secs>     keep whiteboard this long after last user disconnects (default 60)\n"
      "  --send-timeout=<secs>  disconnect client if unable to send data for this long (default 60)\n"
      "  --log-level=<level>    error, warn, info (default), or debug\n"
      "  --log-file=<file>      log to file instead of stderr\n"
      "  --dump                 log all data received (w/ --log-level=debug)\n"
      "  --trust-proxy          running behind reverse proxy: take user name from X-Remote-User header and\n"
      "                         client address from X-Real-IP or X-Forwarded-For; use w/ --host=127.0.0.1\n"
      "  --enable-test          accept test token (no /v1/createswb needed) and enable /v1/debug\n"
      "No authentication is performed - all users are accepted\n");
  return 1;
}

bool swbServerRequested(int argc, char* argv[])
{
  for(int ii = 1; ii < argc; ++ii) {
    if(strcmp(argv[ii], "--server") == 0)
      return true;
  }
  return false;
}

int swbServerMain(int argc, char* argv[])
{
  SWBServerOptions opts;
  for(int ii = 1; ii < argc; ++ii) {
    std::string arg = argv[ii];
    if(arg == "--server") continue;
    if(arg.compare(0, 2, "--") != 0)
      return serverUsage();
    std::string name = arg.substr(2), value;
    size_t eq = name.find('=');
    bool flag = name == "dump" || name == "enable-test" || name == "trust-proxy" || name == "help";
    if(eq != std::string::npos) {
      value = name.substr(eq + 1);
      name = name.substr(0, eq);
    }
    else if(!flag && ii + 1 < argc)
      value = argv[++ii];
    else if(!flag)
      return serverUsage();

    if(name == "host") opts.host = value;
    else if(name == "port") opts.port = atoi(value.c_str());
    else if(name == "del-delay") opts.delDelay = atof(value.c_str());
    else if(name == "send-timeout") opts.sendTimeout = atof(value.c_str());
    else if(name == "log-file") opts.logFile = value;
    else if(name == "log-level") {
      static const char* levels[] = {"error", "warn", "info", "debug"};
      opts.logLevel = -1;
      for(int l = 0; l < 4; ++l) { if(value == levels[l]) opts.logLevel = l; }
      if(opts.logLevel < 0) return serverUsage();
    }
    else if(name == "dump") opts.dump = true;
    else if(name == "enable-test") opts.enableTest = true;
    else if(name == "trust-proxy") opts.trustProxy = true;
    else return serverUsage();
  }
  unet_init();  // WSAStartup on Windows; ignore SIGPIPE
  SWBServer server(opts);
  if(!server.start())
    return 2;
  activeServer = &server;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  server.run();
  activeServer = NULL;
  return 0;
}
