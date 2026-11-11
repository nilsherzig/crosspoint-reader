#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/Dictionary.h"

// Controller for dictionary lookup: renders the background page, runs the
// lookup against the book's StarDict dictionary, and either opens
// DictionaryDefinitionActivity or displays a status popup (Looking up,
// Indexing, Not found, Error). The book's dictionary is stored in its cache
// directory. When none is stored and several dictionaries exist, it first asks
// which one to use; Purpose::ChooseDictionary only asks, then finishes.
class DictionaryWordSelectActivity final : public Activity {
 public:
  enum class Purpose : uint8_t { Lookup, ChooseDictionary };

  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                        std::string lookupText, std::string bookCachePath, Purpose purpose)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        lookupText(std::move(lookupText)),
        bookCachePath(std::move(bookCachePath)),
        purpose(purpose) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  void performLookup();
  // Sets dictName, or opens the picker and returns false.
  bool chooseDictionary();
  void onDictionaryPicked(int index);

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;

  std::string lookupText;
  const std::string bookCachePath;
  const Purpose purpose;
  bool lookupPending = false;
  // Folder name of the dictionary in use; same bound as discovery's name buffer.
  char dictName[128] = "";
  OptionPopup dictPicker;
  std::vector<std::string> dictChoices;  // picker options, held only while it is open
  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;
};
