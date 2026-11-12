// Shared whiteboard server - see swbserver.h

// sockets are handled by ulib/unet.h; platform specific code here is only for loading sqlite and reading password
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#include <dlfcn.h>
#endif

#include "swbserver.h"
#include <stdarg.h>
#include <climits>
#include <cmath>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
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

static long long wallClockMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string addrToStr(struct unet_addr* addr)
{
  char buf[256];
  return unet_address_str(addr, buf, sizeof(buf)) == 0 ? std::string(buf) : std::string("?");
}

// user database

// password hash stored for each user is md5("styluslabs" + password); app sends md5(hash + timestamp)
class SWBAuth
{
public:
  virtual ~SWBAuth() {}
  virtual bool open(std::string* err) = 0;
  virtual bool getHash(const std::string& user, std::string& hash) = 0;
  virtual bool setHash(const std::string& user, const std::string& hash, std::string* err) = 0;
  virtual int userCount() = 0;

  static std::string hashPassword(const std::string& pw) { return md5hex("styluslabs" + pw); }

  bool checkLogin(const std::string& user, const std::string& signature, const std::string& timestamp)
  {
    std::string hash;
    return !user.empty() && getHash(user, hash) && !hash.empty() && md5hex(hash + timestamp) == signature;
  }
};

// text file w/ one "username:hash" per line; blank lines and lines starting w/ '#' are ignored; reloaded if modified
class SWBUsersFile : public SWBAuth
{
public:
  std::string path;
  std::map<std::string, std::string> users;
  time_t mtime = 0;

  SWBUsersFile(const std::string& p) : path(p) {}

  static time_t fileMTime(const std::string& p)
  {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? st.st_mtime : 0;
  }

  static bool readLines(const std::string& p, std::vector<std::string>& lines)
  {
    FILE* f = fopen(p.c_str(), "rb");
    if(!f) return false;
    std::string s;
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
      s.append(buf, n);
    fclose(f);
    size_t pos = 0;
    while(pos < s.size()) {
      size_t nl = s.find('\n', pos);
      if(nl == std::string::npos) nl = s.size();
      std::string line = s.substr(pos, nl - pos);
      if(!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(line);
      pos = nl + 1;
    }
    return true;
  }

  static bool parseLine(const std::string& line, std::string& user, std::string& hash)
  {
    std::string t = trimStr(line);
    if(t.empty() || t[0] == '#') return false;
    size_t colon = t.rfind(':');
    if(colon == std::string::npos || colon == 0) return false;
    user = t.substr(0, colon);
    hash = trimStr(t.substr(colon + 1));
    return true;
  }

  bool load(std::string* err)
  {
    std::vector<std::string> lines;
    if(!readLines(path, lines)) {
      if(err) *err = "unable to read users file " + path;
      return false;
    }
    users.clear();
    std::string user, hash;
    for(const std::string& line : lines) {
      if(parseLine(line, user, hash))
        users[user] = hash;
    }
    mtime = fileMTime(path);
    return true;
  }

  bool open(std::string* err) override { return load(err); }

  bool getHash(const std::string& user, std::string& hash) override
  {
    if(fileMTime(path) != mtime)
      load(NULL);  // on failure, keep previous users
    auto it = users.find(user);
    if(it == users.end()) return false;
    hash = it->second;
    return true;
  }

  // update existing line for user, otherwise append
  bool setHash(const std::string& user, const std::string& hash, std::string* err) override
  {
    if(user.find(':') != std::string::npos || user.find('\n') != std::string::npos) {
      if(err) *err = "username cannot contain ':' or newline";
      return false;
    }
    std::vector<std::string> lines;
    readLines(path, lines);  // OK if file doesn't exist yet
    bool found = false;
    std::string u, h;
    for(std::string& line : lines) {
      if(parseLine(line, u, h) && u == user) {
        line = user + ":" + hash;
        found = true;
      }
    }
    if(!found) {
      if(lines.empty())
        lines.push_back("# Write whiteboard server users - username:md5(\"styluslabs\" + password)");
      lines.push_back(user + ":" + hash);
    }
    std::string tmppath = path + ".tmp";
    FILE* f = fopen(tmppath.c_str(), "wb");
    if(!f) {
      if(err) *err = "unable to write " + tmppath;
      return false;
    }
    for(const std::string& line : lines)
      fprintf(f, "%s\n", line.c_str());
    bool ok = fclose(f) == 0;
#ifdef _WIN32
    remove(path.c_str());  // rename() won't replace existing file on Windows
#endif
    if(!ok || rename(tmppath.c_str(), path.c_str()) != 0) {
      if(err) *err = "unable to write " + path;
      return false;
    }
    return load(err);
  }

  int userCount() override { return int(users.size()); }
};

// sqlite database w/ same schema as node.js server (users table w/ username and password columns); sqlite3 shared
//  library is loaded at runtime only when needed
class SWBSqlite : public SWBAuth
{
public:
  typedef struct sqlite3 sqlite3;
  typedef struct sqlite3_stmt sqlite3_stmt;
  static constexpr int SQLITE_OK = 0, SQLITE_ROW = 100, SQLITE_DONE = 101;
  static constexpr int SQLITE_OPEN_READWRITE = 0x02, SQLITE_OPEN_CREATE = 0x04;

  int (*p_open_v2)(const char*, sqlite3**, int, const char*) = NULL;
  int (*p_close)(sqlite3*) = NULL;
  int (*p_exec)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**) = NULL;
  int (*p_prepare_v2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = NULL;
  int (*p_bind_text)(sqlite3_stmt*, int, const char*, int, void (*)(void*)) = NULL;
  int (*p_step)(sqlite3_stmt*) = NULL;
  const unsigned char* (*p_column_text)(sqlite3_stmt*, int) = NULL;
  int (*p_column_int)(sqlite3_stmt*, int) = NULL;
  int (*p_finalize)(sqlite3_stmt*) = NULL;
  const char* (*p_errmsg)(sqlite3*) = NULL;

  std::string path;
  std::string libPath;
  void* lib = NULL;
  sqlite3* db = NULL;

  SWBSqlite(const std::string& p, const std::string& lp) : path(p), libPath(lp) {}

  ~SWBSqlite() override
  {
    if(db) p_close(db);
    // we never unload the library
  }

  static void* loadLib(const char* name)
  {
#ifdef _WIN32
    return (void*)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
  }

  void* sym(const char* name)
  {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
  }

  bool loadLibrary(std::string* err)
  {
    std::vector<const char*> names;
    if(!libPath.empty())
      names.push_back(libPath.c_str());
    else {
#if defined(_WIN32)
#if defined(_WIN64)
      names.push_back("winsqlite3.dll");  // included w/ Windows 10+ (uses __stdcall, so only on 64-bit)
#endif
      names.push_back("sqlite3.dll");
#elif defined(__APPLE__)
      names.push_back("/usr/lib/libsqlite3.dylib");
      names.push_back("libsqlite3.dylib");
#else
      names.push_back("libsqlite3.so.0");
      names.push_back("libsqlite3.so");
#endif
    }
    for(const char* name : names) {
      if((lib = loadLib(name)))
        break;
    }
    if(!lib) {
      if(err) *err = "unable to load sqlite3 library (" + joinStr(std::vector<std::string>(names.begin(), names.end()), ", ")
          + ") - install sqlite3 or use --sqlite-lib=<path> to specify location";
      return false;
    }
    // casting void* to fn ptr is conditionally supported; go through memcpy to keep compilers quiet
#define SWB_LOAD_SYM(var, name) do { void* p = sym(name); if(!p) { if(err) *err = "sqlite3 library missing " name; return false; } \
    memcpy(&var, &p, sizeof(p)); } while(0)
    SWB_LOAD_SYM(p_open_v2, "sqlite3_open_v2");
    SWB_LOAD_SYM(p_close, "sqlite3_close");
    SWB_LOAD_SYM(p_exec, "sqlite3_exec");
    SWB_LOAD_SYM(p_prepare_v2, "sqlite3_prepare_v2");
    SWB_LOAD_SYM(p_bind_text, "sqlite3_bind_text");
    SWB_LOAD_SYM(p_step, "sqlite3_step");
    SWB_LOAD_SYM(p_column_text, "sqlite3_column_text");
    SWB_LOAD_SYM(p_column_int, "sqlite3_column_int");
    SWB_LOAD_SYM(p_finalize, "sqlite3_finalize");
    SWB_LOAD_SYM(p_errmsg, "sqlite3_errmsg");
#undef SWB_LOAD_SYM
    return true;
  }

  bool open(std::string* err) override
  {
    if(!loadLibrary(err))
      return false;
    if(p_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
      if(err) *err = "error opening database " + path + ": " + (db ? p_errmsg(db) : "out of memory");
      return false;
    }
    const char* schema = "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, username TEXT, password TEXT, "
        "email TEXT, displayname TEXT); CREATE INDEX IF NOT EXISTS username_index ON users(username);";
    if(p_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK) {
      if(err) *err = "error initializing database " + path + ": " + p_errmsg(db);
      return false;
    }
    return true;
  }

  // run statement w/ text params; if out is not NULL, it gets the first column of the first row
  bool query(const char* sql, std::initializer_list<const std::string*> params, std::string* out, std::string* err)
  {
    sqlite3_stmt* stmt = NULL;
    if(p_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      if(err) *err = p_errmsg(db);
      return false;
    }
    int idx = 1;
    for(const std::string* p : params)  // SQLITE_TRANSIENT = -1
      p_bind_text(stmt, idx++, p->c_str(), int(p->size()), (void (*)(void*))(intptr_t)-1);
    int res = p_step(stmt);
    bool ok = res == SQLITE_ROW || res == SQLITE_DONE;
    if(res == SQLITE_ROW && out) {
      const unsigned char* s = p_column_text(stmt, 0);
      *out = s ? (const char*)s : "";
    }
    else if(out)
      ok = false;  // no row
    if(!ok && err && res != SQLITE_DONE) *err = p_errmsg(db);
    p_finalize(stmt);
    return ok;
  }

  bool getHash(const std::string& user, std::string& hash) override
  {
    return query("SELECT password FROM users WHERE username = ?;", {&user}, &hash, NULL);
  }

  bool setHash(const std::string& user, const std::string& hash, std::string* err) override
  {
    std::string existing;
    if(query("SELECT password FROM users WHERE username = ?;", {&user}, &existing, NULL))
      return query("UPDATE users SET password = ? WHERE username = ?;", {&hash, &user}, NULL, err);
    return query("INSERT INTO users(username, password) VALUES(?, ?);", {&user, &hash}, NULL, err);
  }

  int userCount() override
  {
    std::string n;
    return query("SELECT COUNT(1) FROM users;", {}, &n, NULL) ? atoi(n.c_str()) : -1;
  }
};

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
  if(!opts.dbFile.empty())
    auth.reset(new SWBSqlite(opts.dbFile, opts.sqliteLib));
  else if(!opts.usersFile.empty())
    auth.reset(new SWBUsersFile(opts.usersFile));
  if(auth) {
    if(!auth->open(&err)) {
      log(0, "%s", err.c_str());
      return false;
    }
    log(2, "Loaded %d users from %s", auth->userCount(), opts.dbFile.empty() ? opts.usersFile.c_str() : opts.dbFile.c_str());
  }
  else
    log(2, "No user database specified: running in anonymous mode (all logins accepted)");

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
    doTimers();
    doTimers();
    // reap closed connections
    conns.erase(std::remove_if(conns.begin(), conns.end(), [](const std::unique_ptr<SWBConn>& c){
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
  if(wb->clients.empty()) {
    // delete whiteboard after specified delay after last user disconnects
    wb->deletePending = true;
    wb->deleteAt = Clock::now() + std::chrono::milliseconds(int64_t(opts.delDelay*1000));
  }
  else
    broadcast(wb, std::make_shared<const std::string>("<undo><disconnect name='" + c->user + "'/></undo>\n"));
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
    }
    queue(c, it != whiteboards.end() ? joinStr(names, ",") : std::string("-"));
  }
  else if(command == "/start") {
    // args: version (protocol version, ignored), user, document, offset (history offset, > 0 when reconnecting),
    //  uuid (echoed in connect message), token = md5(user + whiteboard.token)
    std::string repo = argStr(args, "document");
    std::string user = argStr(args, "user");
    std::string token = argStr(args, "token");
    auto it = whiteboards.find(repo);
    Whiteboard* wb = it != whiteboards.end() ? it->second.get() : NULL;
    if(c->wb) {
      log(1, "%s: /start received while already connected", c->remote.c_str());
      closeConn(c, false);
      return false;
    }
    if(token == "SCRIBBLE_SYNC_TEST" && opts.enableTest) {
      log(2, "%s: connecting to test whiteboard %s as %s", c->remote.c_str(), repo.c_str(), user.c_str());
      if(!wb) {
        wb = new Whiteboard;
        wb->name = repo;
        whiteboards[repo].reset(wb);
      }
    }
    else if(!wb || token != md5hex(user + wb->token)) {
      log(1, "%s: whiteboard not found or invalid token", c->remote.c_str());
      queue(c, "<undo><accessdenied message='Whiteboard not found. Please try again.'/></undo>\n");
      closeConn(c, true);
      return false;
    }
    c->user = user;
    // send history
    long long offset = atoll(argStr(args, "offset", "0").c_str());
    if(offset < 0) offset = 0;
    if(size_t(offset) < wb->histSize) {
      size_t idx = std::upper_bound(wb->histStart.begin(), wb->histStart.end(), size_t(offset)) - wb->histStart.begin() - 1;
      queue(c, wb->history[idx], size_t(offset) - wb->histStart[idx]);
      for(++idx; idx < wb->history.size() && !c->dead; ++idx)
        queue(c, wb->history[idx]);
    }
    if(c->dead) return false;
    c->wb = wb;
    wb->clients.push_back(c);
    wb->deletePending = false;
    // if user was already connected as a different client, remove old client (only now, so whiteboard isn't
    //  deleted if it was the only client)
    std::vector<SWBConn*> clients = wb->clients;
    for(SWBConn* old : clients) {
      if(old != c && old->user == user) {
        log(2, "disconnecting %s due to connection of %s for user: %s", old->remote.c_str(), c->remote.c_str(), user.c_str());
        queue(old, "<undo><accessdenied message='User logged in from another location.'/></undo>\n");
        closeConn(old, true);
      }
    }
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

void SWBServer::processHTTP(SWBConn* c)
{
  size_t hdrend = c->inbuf.find("\r\n\r\n");
  if(hdrend == std::string::npos) {
    if(c->inbuf.size() > MAX_HTTP_HEADER) {
      queue(c, "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      closeConn(c, true);
    }
    return;
  }
  std::string hdr = c->inbuf.substr(0, hdrend);
  c->inbuf.clear();
  // request line
  size_t eol = hdr.find("\r\n");
  std::string reqline = hdr.substr(0, eol);
  std::vector<std::string> parts = splitStr<std::vector>(reqline.c_str(), ' ', true);
  int status = 400;
  std::string body;
  std::string extraHeaders;
  if(parts.size() == 3 && parts[2].compare(0, 5, "HTTP/") == 0) {
    // headers
    std::string cookie, useragent;
    size_t pos = eol;
    while(pos != std::string::npos && pos < hdr.size()) {
      pos += 2;
      size_t next = hdr.find("\r\n", pos);
      std::string line = hdr.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
      size_t colon = line.find(':');
      if(colon != std::string::npos) {
        std::string name = toLower(trimStr(line.substr(0, colon)));
        std::string value = trimStr(line.substr(colon + 1));
        if(name == "cookie") cookie = cookie.empty() ? value : cookie + "; " + value;
        else if(name == "user-agent" || name == "user agent") useragent = value;  // Write sends "User Agent"
      }
      pos = next;
    }
    std::string setCookie;
    body = handleHTTP(parts[0], parts[1], cookie, c->remote, &status, &setCookie);
    if(!setCookie.empty())
      extraHeaders = "Set-Cookie: " + setCookie + "\r\n";
    log(2, "%s - \"%s\" %d - \"%s\"", c->remote.c_str(), reqline.c_str(), status, useragent.c_str());
  }
  static const std::map<int, const char*> reasons = {{200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"},
      {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"}, {408, "Request Timeout"}};
  auto reason = reasons.find(status);
  std::string resp = fstring("HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n",
      status, reason != reasons.end() ? reason->second : "Error", int(body.size()));
  resp.append(extraHeaders).append("\r\n").append(body);
  queue(c, std::move(resp));
  closeConn(c, true);
}

// returns response body; for successful auth, sets setCookie to "session=<token>"
std::string SWBServer::handleHTTP(const std::string& method, const std::string& target,
    const std::string& cookie, const std::string& remote, int* status, std::string* setCookie)
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
    std::string user = argStr(args, "user");
    if(auth) {
      std::string ts = argStr(args, "timestamp");
      bool tsOK = true;
      if(opts.maxClockSkew > 0) {
        double skew = std::abs(double(wallClockMs() - atoll(ts.c_str())))/1000;
        tsOK = skew <= opts.maxClockSkew;
        if(!tsOK)
          log(1, "%s: auth timestamp for %s off by %.0f seconds", remote.c_str(), user.c_str(), skew);
      }
      if(!tsOK || !auth->checkLogin(user, argStr(args, "signature"), ts)) {
        *status = 401;
        return "error: invalid username or password";
      }
    }
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

  if(path == "/v1/createswb" || path == "/v1/openswb") {
    bool create = path == "/v1/createswb";
    std::string repo = argStr(args, "name");
    auto it = whiteboards.find(repo);
    if(repo.empty() || (create && it != whiteboards.end()) || (!create && it == whiteboards.end())) {
      *status = 404;
      return "";
    }
    if(create) {
      // all args (name, title, rxonly, ...) are passed back to clients opening the whiteboard
      std::string attribs;
      for(auto& kv : args) {
        if(!isXmlName(kv.first) || kv.first == "user" || kv.first == "token") continue;
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
    return "<swb" + wb->attribs + " user='" + xmlEscape(user) + "' token='" + md5hex(user + wb->token) + "'/>";
  }
  *status = 404;
  return "";
}

void SWBServer::doTimers()
{
  auto now = Clock::now();
  for(auto it = whiteboards.begin(); it != whiteboards.end();) {
    Whiteboard* wb = it->second.get();
    if(wb->deletePending && wb->clients.empty() && now >= wb->deleteAt) {
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
  }
  // expired sessions; HTTP connections that never send a complete request
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
    s += fstring("  %s: %d bytes history (%d chunks), clients: %s%s\n", wb->name.c_str(), int(wb->histSize),
        int(wb->history.size()), joinStr(names, ", ").c_str(), wb->deletePending ? " (delete pending)" : "");
  }
  s += fstring("sessions: %d\nconnections: %d\n", int(sessions.size()), int(conns.size()));
  for(auto& c : conns)
    s += fstring("  %s %s: %d bytes queued\n", c->kind == CONN_SWB ? "SWB" : "HTTP", c->remote.c_str(), int(c->outBytes));
  return s;
}

// command line

static SWBServer* activeServer = NULL;

static void onSignal(int) { if(activeServer) activeServer->stop(); }

static std::string readLine()
{
  char buf[1024];
  if(!fgets(buf, sizeof(buf), stdin)) return "";
  std::string s(buf);
  while(!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

static std::string readPassword(const char* prompt)
{
  fprintf(stderr, "%s", prompt);
  fflush(stderr);
  std::string pw;
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  bool console = GetConsoleMode(h, &mode);
  if(console) SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
  pw = readLine();
  if(console) SetConsoleMode(h, mode);
#else
  struct termios oldt;
  bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
  if(tty) {
    struct termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  }
  pw = readLine();
  if(tty) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
  fprintf(stderr, "\n");
  return pw;
}

static int serverUsage()
{
  fprintf(stderr,
      "Usage: Write --server [options]\n"
      "Runs shared whiteboard server for Write\n"
      "  --host=<address>       address to listen on (default: all interfaces)\n"
      "  --port=<port>          HTTP API port (default 7000); whiteboard data uses port + 1\n"
      "  --users=<file>         require login; users file has lines username:md5(\"styluslabs\" + password)\n"
      "  --db=<file>            require login; users in sqlite database (requires sqlite3 shared library)\n"
      "  --sqlite-lib=<path>    location of sqlite3 shared library\n"
      "  --add-user=<name>      add user to --users or --db file (prompts for password) and exit\n"
      "  --del-delay=<secs>     keep whiteboard this long after last user disconnects (default 0)\n"
      "  --max-clock-skew=<secs> reject logins w/ client clock off by more than this (default: no check)\n"
      "  --send-timeout=<secs>  disconnect client if unable to send data for this long (default 60)\n"
      "  --log-level=<level>    error, warn, info (default), or debug\n"
      "  --log-file=<file>      log to file instead of stderr\n"
      "  --dump                 log all data received (w/ --log-level=debug)\n"
      "  --enable-test          allow test connections w/o login\n"
      "Without --users or --db, all connections are accepted (anonymous mode)\n");
  return 1;
}

bool swbAddUser(const SWBServerOptions& opts, const std::string& user, const std::string& password, std::string* err)
{
  std::unique_ptr<SWBAuth> auth;
  if(!opts.dbFile.empty()) auth.reset(new SWBSqlite(opts.dbFile, opts.sqliteLib));
  else if(!opts.usersFile.empty()) auth.reset(new SWBUsersFile(opts.usersFile));
  else {
    if(err) *err = "no users file or database specified";
    return false;
  }
  if(user.empty()) {
    if(err) *err = "username cannot be empty";
    return false;
  }
  // users file need not exist yet
  if(!auth->open(err) && opts.usersFile.empty())
    return false;
  return auth->setHash(user, SWBAuth::hashPassword(password), err);
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
  std::string addUser;
  for(int ii = 1; ii < argc; ++ii) {
    std::string arg = argv[ii];
    if(arg == "--server") continue;
    if(arg.compare(0, 2, "--") != 0)
      return serverUsage();
    std::string name = arg.substr(2), value;
    size_t eq = name.find('=');
    bool flag = name == "dump" || name == "enable-test" || name == "help";
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
    else if(name == "users") opts.usersFile = value;
    else if(name == "db") opts.dbFile = value;
    else if(name == "sqlite-lib") opts.sqliteLib = value;
    else if(name == "add-user") addUser = value;
    else if(name == "del-delay") opts.delDelay = atof(value.c_str());
    else if(name == "max-clock-skew") opts.maxClockSkew = atof(value.c_str());
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
    else return serverUsage();
  }
  if(!opts.usersFile.empty() && !opts.dbFile.empty()) {
    fprintf(stderr, "Only one of --users and --db can be specified\n");
    return 1;
  }

  if(!addUser.empty()) {
    if(opts.usersFile.empty() && opts.dbFile.empty()) {
      fprintf(stderr, "--add-user requires --users or --db\n");
      return 1;
    }
    std::string pw = readPassword(fstring("Password for %s: ", addUser.c_str()).c_str());
    if(pw.empty()) {
      fprintf(stderr, "Password cannot be empty\n");
      return 1;
    }
    std::string err;
    if(!swbAddUser(opts, addUser, pw, &err)) {
      fprintf(stderr, "Error adding user: %s\n", err.c_str());
      return 1;
    }
    fprintf(stderr, "User %s saved\n", addUser.c_str());
    return 0;
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
