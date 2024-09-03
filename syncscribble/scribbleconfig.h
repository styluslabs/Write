#pragma once

#include "basics.h"
#include <string.h>
#include <map>
#include <list>
#include <string>
#include "pugixml.hpp"
#include "scribblepen.h"

struct ltstr { bool operator()(const char* s1, const char* s2) const { return strcmp(s1, s2) < 0; } };

class ScribbleConfig {
#ifdef SCRIBBLE_TEST
  friend class ScribbleTest;
#endif
private:
  // bools and ints
  std::map<const char*, int, ltstr> cfg;
  typedef std::map<const char*, int, ltstr>::const_iterator cfgIterator;
  // floats
  std::map<const char*, float, ltstr> cfgF;
  typedef std::map<const char*, float, ltstr>::const_iterator cfgFIterator;
  // strings
  std::map<const char*, std::string, ltstr> cfgS;
  typedef std::map<const char*, std::string, ltstr>::const_iterator cfgSIterator;

  ScribbleConfig* upconfig;

public:
  ScribbleConfig();
  ScribbleConfig(ScribbleConfig* _upconfig) : upconfig(_upconfig) {}
  void init();
  ScribbleConfig* getUpConfig() { return upconfig != NULL ? upconfig : this; }
  bool loadConfig(const pugi::xml_node &cfgroot);
  void saveConfig(pugi::xml_node cfgroot, bool skipdefaults = false);
  bool loadConfigString(const char* cfgstr);
  bool loadConfigFile(const char* cfgfile);
  bool saveConfigFile(const char* cfgfile, bool skipdefaults = false);
  bool setConfigValue(const char* name, const char* val);

  ScribblePen* getPen(int num);
  void savePen(const ScribblePen& pen, int slot=-1);

  const char* isInt(const char* s) const;
  const char* isFloat(const char* s) const;
  const char* isString(const char* s) const;
  bool Bool(const char*, bool defaultval = false) const;
  int Int(const char*, int defaultval = 0) const;
  float Float(const char*, float defaultval = 0.0f) const;
  const char* String(const char* s, const char *defaultval = NULL) const;
  void set(const char* s, bool x);
  void set(const char* s, int x);
  void set(const char* s, unsigned int x) { set(s, int(x)); }
  void set(const char* s, float x);
  void set(const char* s, double x);
  void set(const char* s, const char* x);
  int removeInt(const char* s);
  int removeFloat(const char* s);
  int removeString(const char* s);

  // pens
  std::list<ScribblePen> pens;
  typedef std::list<ScribblePen>::iterator penIterator;
};
