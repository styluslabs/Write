// UI automation (`Write --automation <path>`, tests): linked into WriteTest always, and into Write only for
//  dev builds (desktop, AUTOMATION=1 - default except for release packages; see Makefile).  Application
//  calls this code only through hooks set by the static initializer below, so Write links w/o it.

#include "application.h"
#include "ugui/svggui_platform.h"
#include "ugui/uidriver.h"
#include <thread>
#include <chrono>
#include <memory>
#include <cstdio>

// drive app w/ text commands (see UiDriver::queueCommand) read from path (e.g. a FIFO, reopened after each
//  writer closes it) or stdin if path is "-"; replies are written to path + ".out" (stdout for stdin)
static bool startAutomation(const std::string& path)
{
  static std::unique_ptr<UiDriver> driver;
  static FILE* replyOut = NULL;
  if(driver) return false;  // already started
  if(path == "-")
    replyOut = stdout;
  else {
    replyOut = fopen((path + ".out").c_str(), "w");
    if(!replyOut) {
      PLATFORM_LOG("Unable to open %s.out\n", path.c_str());
      return false;
    }
  }
  driver.reset(new UiDriver(Application::gui));
  driver->modalTimeout = 0;  // dialogs stay open until commands close them
  driver->replyFn = [](const std::string& r){ fputs(r.c_str(), replyOut); fflush(replyOut); };
  driver->onQuit = [](){ Application::finish(); };
  Application::uiDriver = driver.get();
  UiDriver* d = driver.get();
  // reader thread blocks in open/read, so it is detached (process exit ends it)
  std::thread([d, path](){
    for(;;) {
      FILE* in = path == "-" ? stdin : fopen(path.c_str(), "r");  // blocks until a writer opens FIFO
      if(!in) return;
      char buf[4096];
      while(fgets(buf, sizeof(buf), in))
        d->queueCommand(buf);
      if(in == stdin) return;
      fclose(in);
    }
  }).detach();
  // event loop may not wake w/o input events (on real platforms), so wake periodically for wait/sleep commands
  std::thread([](){
    for(;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      PLATFORM_WakeEventLoop();
    }
  }).detach();
  PLATFORM_LOG("UI automation: reading commands from %s\n", path == "-" ? "stdin" : path.c_str());
  return true;
}

// Application's hook pointers are constant initialized (NULL), so setting them from a static initializer is safe
static struct AutomationInit {
  AutomationInit() {
    Application::startAutomationFn = startAutomation;
    Application::uiDriverPoll = [](UiDriver* d){ d->poll(); };
  }
} automationInit;
