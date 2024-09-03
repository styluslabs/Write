#include "configdialog.h"
#include "pugixml.hpp"
#include "ugui/textedit.h"
#include "basics.h"
#include "scribbleapp.h"
#include "mainwindow.h"  // only for getting bounds for insetting


ConfigDialog::ConfigDialog(ScribbleConfig* _cfg) : Dialog(createDialogNode()), cfg(_cfg)
{
  init();
}

extern const char* prefInfoXML;

void ConfigDialog::init()
{
  ScribbleApp* mw = ScribbleApp::app;
  pugi::xml_document infodoc;
  infodoc.load(prefInfoXML);

  toolStack = createColumn();
  toolStack->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only

  ScrollWidget* scrollWidget = new ScrollWidget(new SvgDocument(), toolStack);
  scrollWidget->node->setAttribute("box-anchor", "fill");  // use vfill and mBounds.w = 0 for horz auto-size

  Widget* dialogBody = selectFirst(".body-container");
  dialogBody->node->setAttribute("box-anchor", "fill");
  dialogBody->node->setAttribute("layout", "box");  // change from flex to box
  dialogBody->node->removeAttr("flex-direction");
  dialogBody->node->restyle();  // needed to pick up layout attribute changes
  dialogBody->addWidget(createFillRect());
  dialogBody->addWidget(scrollWidget);

  pugi::xml_node infomap = infodoc.child("map");
  allprops.clear();
  // load properties from XML:
  for(pugi::xml_node pref = infomap.first_child(); pref; pref = pref.next_sibling()) {
    Widget* p;
    StringRef exclude(pref.attribute("exclude").as_string());
    if(exclude.contains(PLATFORM_NAME) || exclude.contains(PLATFORM_TYPE))
      continue;
    // find the corresponding entry in the info file
    const char* name = pref.attribute("name").as_string();
    std::string type = pref.attribute("type").as_string();
    if(pref.attribute("enum")) {
      auto enumNames = splitStr<std::vector>(pref.attribute("enum").as_string(), ';', true);
      if(StringRef(name) != "docFileExt") {
        for(std::string& s : enumNames)
          s = _(s.c_str());  // apply i18n
      }
      ComboBox* cb = createComboBox(enumNames);
      if(cfg->isString(name))  // type == "string"
        cb->setText(cfg->String(name));
      else {
        int currval = cfg->Int(name);
        if(pref.attribute("enumvals")) {
          auto strvals = splitStr<std::vector>(pref.attribute("enumvals").as_string(), ';', true);
          std::vector<int> vals;
          for(const std::string& s : strvals) {
            vals.push_back(atoi(s.c_str()));
            if(vals.back() == currval)
              cb->setIndex(vals.size() - 1);
          }
          cb->setUserData<std::vector<int>>(vals);
        }
        else
          cb->setIndex(currval);
      }
      p = cb;
    }
    else if(type == "bool") {
      CheckBox* cb = createCheckBox();
      cb->setChecked(cfg->Bool(name));
      p = cb;
    }
    else if(type == "int") {
      SpinBox* sb = createTextSpinBox(cfg->Int(name), pref.attribute("step").as_int(1),
          pref.attribute("min").as_int(INT_MIN), pref.attribute("max").as_int(INT_MAX));
      p = sb;
    }
    else if(type == "float") {
      SpinBox* sb = createTextSpinBox(cfg->Float(name), pref.attribute("step").as_float(1),
          pref.attribute("min").as_float(-FLT_MAX), pref.attribute("max").as_float(FLT_MAX));
      p = sb;
    }
    else if(type == "string") {
      TextEdit* e = createTextEdit();
      e->setText(cfg->String(name));
      p = e;
    }
    else if(type == "label" || type == "button") {
      p = NULL;
    }
    else
      continue; // invalid type
    // add the property to the appropriate sub-tree, creating it if necessary
    const char* group = pref.attribute("group").as_string();
    if(propGroups.find(group) == propGroups.end()) {
      Widget* g = createColumn();
      g->node->setAttribute("box-anchor", "hfill");  // fill or vfill here == disaster
      g->node->setAttribute("margin", "0 9");
      Button* btn = createPushbutton(_(group));
      btn->onClicked = [g](){ g->setVisible(!g->isVisible()); };  //btn->setChecked(g->isVisible());
      g->addHandler([btn](SvgGui* gui, SDL_Event* event){
        if(event->type == SvgGui::VISIBLE || event->type == SvgGui::INVISIBLE)
          btn->setChecked(event->type == SvgGui::VISIBLE);
        return false;
      });
      toolStack->addWidget(btn);
      toolStack->addWidget(g);
      toolStack->addWidget(createHRule());
      g->setVisible(false);  // hide initially
      propGroups[group] = g;
    }
    if(p) {
      //p->setUserData<std::string>(name);
      p->node->setAttribute("__prefname", name);
      p->node->addClass(pref.attribute("level").as_int(0) ? "basic" : "advanced");  // 0: adv, 1: basic
      ///p->setToolTip(pref.attribute("description").as_string());
      const char* label = _(pref.attribute("title").as_string());
      if(p->node->hasClass("checkbox")) {
        propGroups[group]->addWidget(createTitledRow(label, createStretch(), p));
        if(strcmp(name, "showAdvPrefs") == 0)
          static_cast<CheckBox*>(p)->onToggled = [this](bool checked){ toggleAdvPrefs(checked); };
      }
      else
        propGroups[group]->addWidget(createTitledRow(label, p));

      allprops.push_back(p);
    }
    else if(type == "label") {
      Widget* label = new Widget(createTextNode(_(pref.attribute("title").as_string())));
      //label->setWordWrap(true);
      propGroups[group]->addWidget(label);
    }
    else if(type == "button") {
      std::string n = name;
      if(n == "Reset Prefs") {
        Widget* resetbtns = createRow({}, "5 0", "space-between");
        resetbtns->node->addClass("button-container");
        Button* appresetbtn = createPushbutton(_("Reset Preferences"));
        appresetbtn->onClicked = [this](){ resetPrefs(); };
        resetbtns->addWidget(appresetbtn);
        // doc prefs reset
        Button* docresetbtn = createPushbutton(_("Clear Doc. Config."));
        docresetbtn->onClicked = [mw](){ mw->resetDocPrefs(); };
        resetbtns->addWidget(docresetbtn);
        propGroups[group]->addWidget(resetbtns);
        // for basic/adv prefs - note toggleAdvPrefs() shows/hides parent of widget in allprops
        allprops.push_back(appresetbtn);
        appresetbtn->node->addClass(pref.attribute("level").as_int(0) ? "basic" : "advanced");
      }
      else if(n == "About Write") {
        // add a couple items that would otherwise be on the Help menu
        Widget* helpbtns = createRow({}, "5 0", "space-between");
        helpbtns->node->addClass("button-container");
#if ENABLE_UPDATE
        Button* updatebtn = createPushbutton(_("Check for Update"));
        updatebtn->onClicked = [mw](){ mw->updateCheck(); };
        helpbtns->addWidget(updatebtn);
#else
        Button* helpbtn = createPushbutton(_("Help"));
        helpbtn->onClicked = [mw, this](){ mw->openHelp(); finish(CANCELLED); };
        helpbtns->addWidget(helpbtn);
#endif
        Button* aboutbtn = createPushbutton(_("About Write"));
        aboutbtn->onClicked = [mw](){ mw->about(); };
        helpbtns->addWidget(aboutbtn);
        propGroups[group]->addWidget(helpbtns);
      }
      // add edit toolbar button
      //else if(n == "Edit Toolbars") {}
    }
  }
  // append stretch to end of stack
  //toolStack->addWidget(createStretch());
  setTitle(_("Preferences"));
  acceptBtn = addButton(_("OK"), [this](){ accept(); finish(ACCEPTED); });
  cancelBtn = addButton(_("Cancel"), [this](){ finish(CANCELLED); });
  // show/hide advanced view and set window size
  toggleAdvPrefs(cfg->Bool("showAdvPrefs"));
}

void ConfigDialog::resetPrefs()
{
  cfg->init();
  accept();  // must accept since config has changed!
}

void ConfigDialog::toggleAdvPrefs(bool show)
{
  cfg->set("showAdvPrefs", show);
  // to show adv prefs: set everything in toolstack visible, then hide all groups, show all prefs
  // to hide adv prefs: hide everything in toolstack, show groups, hide non-adv prefs
  for(Widget* child : toolStack->select("*"))
    child->setVisible(show);
  for(const auto& kv : propGroups)
    kv.second->setVisible(!show);
  for(Widget* p : allprops)
    p->parent()->setVisible(show || p->node->hasClass("basic"));  // parent is the row containing control

  // could we expand all groups, run layout for horz auto-size, then close groups?
  Rect pbbox = ScribbleApp::win->winBounds();
  Dim h = pbbox.height() - 60;
  Dim winheight = show ? h : std::min(h, Dim(600));
  setWinBounds(Rect::centerwh(pbbox.center(), std::min(pbbox.width(), Dim(500)), winheight));
}

void ConfigDialog::accept()
{
  for(size_t ii = 0; ii < allprops.size(); ii++) {
    Widget* p = allprops[ii];
    const char* name = p->node->getStringAttr("__prefname", NULL);
    if(!name) continue;
    if(p->node->hasClass("checkbox") && cfg->isInt(name)) {
      // don't write bool unless changed - for bool prefs that support additional values (e.g. =2 to force)
      if(static_cast<CheckBox*>(p)->isChecked() != cfg->Bool(name))
        cfg->set(name, static_cast<CheckBox*>(p)->isChecked());
    }
    else if(p->node->hasClass("combobox") && cfg->isInt(name)) {
      ComboBox* cb = static_cast<ComboBox*>(p);
      if(cb->hasUserData()) {
        auto& vals = p->userData<std::vector<int>>();
        cfg->set(name, vals[cb->index()]);
      }
      else
        cfg->set(name, cb->index());
    }
    else if(p->node->hasClass("combobox") && cfg->isString(name))
      cfg->set(name, static_cast<ComboBox*>(p)->text());
    // a bit of a hack since we don't have a separate FloatSpinBox
    else if(p->node->hasClass("spinbox") && cfg->isFloat(name))
      cfg->set(name, float(static_cast<SpinBox*>(p)->value()));
    else if(p->node->hasClass("spinbox") && cfg->isInt(name))
      cfg->set(name, int(static_cast<SpinBox*>(p)->value()));
    else if(p->node->hasClass("textbox") && cfg->isString(name))
      cfg->set(name, static_cast<TextEdit*>(p)->text().c_str());
    else
      PLATFORM_LOG("Invalid config key: %s\n", name);
  }
}
