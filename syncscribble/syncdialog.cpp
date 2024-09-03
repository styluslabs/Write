#include "syncdialog.h"
#include "scribbleapp.h"
#include "scribblesync.h"

// login dialog

SyncLoginDialog::SyncLoginDialog(const char* username, bool savepw, const char* msg)
  : Dialog(createDialogNode())
{
  userEdit = createTextEdit(200);
  userEdit->setText(username);
  passEdit = createTextEdit(200);
  passEdit->editMode = TextEdit::PASSWORD_SHOWLAST;

  Widget* dialogBody = selectFirst(".body-container");
  if(msg && msg[0])
    dialogBody->addWidget(new Widget(createTextNode(msg)));
  dialogBody->addWidget(createTitledRow(_("User"), 0, userEdit));
  dialogBody->addWidget(createTitledRow(_("Password"), 0, passEdit));
  if(savepw) {
    savePassword = createCheckBox();
    // if username is filled in but password isn't, the user has seen this dialog before and chose not to save
    //  password, so keep "Save password" unchecked in that case.
    savePassword->setChecked(savepw && !username[0]);
    dialogBody->addWidget(createTitledRow(_("Save password"), savePassword));
  }
  dialogBody->setMargins(8, 8, 0, 8);
  focusedWidget = username[0] ? passEdit : userEdit;
  //urlEdit->selectAll();

  setTitle(_("Whiteboard Login"));
  acceptBtn = addButton(_("OK"), [this](){ finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });

  userEdit->onChanged = [this](const char* s){ acceptBtn->setEnabled(s && s[0]); };
  acceptBtn->setEnabled(username[0]);
}

// share doc dialog

static Button* createLinkBtn()
{
  const char* linktspan = "<tspan fill='#08F'>styluslabs.com/share</tspan>";
  std::string linksvg = "<text>" + fstring(_("Visit %s to get started"), linktspan) + "</text>";
  Button* linkBtn = new Button(loadSVGFragment(linksvg.c_str()));
  linkBtn->onClicked = [](){ ScribbleApp::openURL("http://www.styluslabs.com/share"); };
  return linkBtn;
}

SyncCreateDialog::SyncCreateDialog(const char* url, const char* title, bool showLink)
  : Dialog(createDialogNode())
{
  urlEdit = createTextEdit(240);
  urlEdit->setText(url);
  titleEdit = createTextEdit(240);
  titleEdit->setText(title);
  lectureMode = createCheckBox();

  Widget* dialogBody = selectFirst(".body-container");
  msgLabel = new TextBox(createTextNode(""));
  msgLabel->setVisible(false);
  dialogBody->addWidget(msgLabel);
  if(showLink) {
    linkBtn = createLinkBtn();
    dialogBody->addWidget(linkBtn);
  }
  dialogBody->addWidget(createTitledRow(_("Title"), 0, titleEdit));
  dialogBody->addWidget(createTitledRow(_("ID"), 0, urlEdit));
  dialogBody->addWidget(createTitledRow(_("Lecture mode"), lectureMode));
  dialogBody->setMargins(8, 8, 0, 8);
  focusedWidget = urlEdit;
  urlEdit->selectAll();

  setTitle(_("Create Whiteboard"));
  acceptBtn = addButton(_("OK"), [this](){ finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
}

void SyncCreateDialog::setMessage(const char* msg)
{
  if(linkBtn)
    linkBtn->setVisible(false);
  msgLabel->setText(msg);
  msgLabel->setVisible(msg[0]);
}

// open doc dialog

SyncOpenDialog::SyncOpenDialog(bool showLink) : Dialog(createDialogNode())
{
  urlEdit = createTextEdit(240);
  //nameEdit->setText(fsinfo.baseName().c_str());

  Widget* dialogBody = selectFirst(".body-container");
  msgLabel = new TextBox(createTextNode(""));
  msgLabel->setVisible(false);
  dialogBody->addWidget(msgLabel);
  if(showLink) {
    linkBtn = createLinkBtn();
    dialogBody->addWidget(linkBtn);
  }
  dialogBody->addWidget(createTitledRow(_("ID"), 0, urlEdit));
  dialogBody->setMargins(8, 8, 0, 8);
  focusedWidget = urlEdit;
  //nameEdit->selectAll();

  setTitle(_("Open Whiteboard"));
  acceptBtn = addButton(_("OK"), [this](){ finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
}

void SyncOpenDialog::setMessage(const char* msg)
{
  if(linkBtn)
    linkBtn->setVisible(false);
  msgLabel->setText(msg);
  msgLabel->setVisible(msg[0]);
}

SyncInfoDialog::SyncInfoDialog(const ScribbleSync* sync) : Dialog(createDialogNode())
{
  urlEdit = createTextEdit(240);
  urlEdit->editMode = TextEdit::READ_ONLY;
  urlEdit->setText(sync->getDocPath().c_str());

  Widget* dialogBody = selectFirst(".body-container");
  //TextBox* label1 = new TextBox(createTextNode("Connected to:"));
  //dialogBody->addWidget(label1);
  dialogBody->addWidget(createTitledRow(_("ID"), 0, urlEdit));
  std::string clients = _("Users:\n  ") + joinStr(sync->clients(), "\n  ");
  TextBox* label2 = new TextBox(createTextNode(clients.c_str()));
  label2->node->setAttribute("box-anchor", "left");
  dialogBody->addWidget(label2);
  dialogBody->setMargins(8, 8, 0, 8);
  focusedWidget = urlEdit;
  urlEdit->selectAll();

  setTitle(_("Whiteboard Info"));
  acceptBtn = addButton(_("OK"), [this](){ finish(ACCEPTED); });
}
