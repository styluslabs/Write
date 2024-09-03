#pragma once

#include "scribbleconfig.h"
#include "ugui/widgets.h"

class ConfigDialog : public Dialog
{
public:
  ConfigDialog(ScribbleConfig* _cfg);
  void accept();
  void resetPrefs();
  void toggleAdvPrefs(bool show);

private:
  void init();

  ScribbleConfig* cfg = NULL;
  Widget* toolStack;
  std::vector<Widget*> allprops;
  typedef std::map<std::string, Widget*> PropGroups_t;
  PropGroups_t propGroups;
};
