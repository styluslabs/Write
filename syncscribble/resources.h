#pragma once

// translation fn
extern "C" {
const char* _(const char*);
}
class SvgCssStylesheet;

void setupResources();
SvgCssStylesheet* createStylesheet();
bool setupI18n(const char* lc);
