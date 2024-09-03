#pragma once

#include "ugui/widgets.h"
#include "ugui/textedit.h"

class ScribbleSync;

class SyncLoginDialog : public Dialog
{
public:
  SyncLoginDialog(const char* username, bool savepw, const char* msg);

  TextEdit* userEdit = NULL;
  TextEdit* passEdit = NULL;
  CheckBox* savePassword = NULL;
};

class SyncCreateDialog : public Dialog
{
public:
  SyncCreateDialog(const char* url, const char* title, bool showLink);
  void setMessage(const char* msg);

  TextEdit* urlEdit = NULL;
  TextEdit* titleEdit = NULL;
  CheckBox* lectureMode = NULL;
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
