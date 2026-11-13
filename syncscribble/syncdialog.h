#pragma once

#include "ugui/widgets.h"
#include "ugui/textedit.h"

class ScribbleSync;

class SyncLoginDialog : public Dialog
{
public:
  // askPassword = false for server w/o login (whiteboard hosted on this device): only name is needed
  SyncLoginDialog(const char* username, bool savepw, const char* msg, bool askPassword = true);

  TextEdit* userEdit = NULL;
  TextEdit* passEdit = NULL;
  CheckBox* savePassword = NULL;
};

class SyncCreateDialog : public Dialog
{
public:
  // canHost: show "Host on this device" option
  SyncCreateDialog(const char* url, const char* title, bool showLink, bool canHost, bool host);
  void setMessage(const char* msg);
  bool hostLocal() const { return hostCheck && hostCheck->isChecked(); }

  TextEdit* urlEdit = NULL;
  TextEdit* titleEdit = NULL;
  CheckBox* lectureMode = NULL;
  CheckBox* hostCheck = NULL;
  TextBox* msgLabel = NULL;
  Button* linkBtn = NULL;
};

class SyncOpenDialog : public Dialog
{
public:
  SyncOpenDialog(bool showLink);
  void setMessage(const char* msg);

  TextEdit* urlEdit = NULL;
  TextBox* msgLabel = NULL;
  Button* linkBtn = NULL;
};

class SyncInfoDialog : public Dialog
{
public:
  SyncInfoDialog(const ScribbleSync* sync);

  TextEdit* urlEdit = NULL;
};
