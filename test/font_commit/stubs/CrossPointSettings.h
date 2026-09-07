// Minimal CrossPointSettings for the font-commit suite.
//
// The real header pulls ArduinoJson, PersistableStore and the whole settings
// list; FontUpdater and GithubAuth between them read exactly two fields. Field
// WIDTHS are copied from the real header (CrossPointSettings.h:521, :550)
// because GithubAuth.h takes sizeof(SETTINGS.githubToken) and would report a
// different truncation boundary against a different array.
#pragma once

class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings s;
    return s;
  }
  char sdFontFamilyName[32] = "";
  char githubToken[104] = "";
};

#define SETTINGS CrossPointSettings::getInstance()
