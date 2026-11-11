#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryRegistry.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;

void indexBuildYield(void*) { vTaskDelay(1); }

void copyName(char* out, const size_t outSize, const char* name) {
  strncpy(out, name, outSize - 1);
  out[outSize - 1] = '\0';
}

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lookupPending = true;
  popupMsg = StrId::STR_DICT_LOOKING_UP;
  popup = purpose == Purpose::Lookup ? Popup::Busy : Popup::None;
  requestUpdate();
}

bool DictionaryWordSelectActivity::chooseDictionary() {
  if (dictName[0] != '\0') return true;

  // A stored choice whose folder was removed counts as no choice.
  char stored[sizeof(dictName)];
  std::string basePath;
  const bool hasStored = DictionaryRegistry::loadBookChoice(bookCachePath, stored, sizeof(stored)) &&
                         DictionaryRegistry::resolveBasePath(stored, basePath);
  if (hasStored && purpose == Purpose::Lookup) {
    copyName(dictName, sizeof(dictName), stored);
    return true;
  }

  std::vector<DictionaryEntry> found;
  DictionaryRegistry::discover(found);
  if (found.size() < 2) {
    // Nothing to choose, and nothing is stored, so the book still asks once a
    // second dictionary is added. With none, open() fails and reports it.
    if (!found.empty()) copyName(dictName, sizeof(dictName), found.front().name.c_str());
    return true;
  }

  if (found.size() > static_cast<size_t>(OptionPopup::MAX_OPTIONS)) {
    LOG_INF("DICT", "Offering the first %d of %u dictionaries", OptionPopup::MAX_OPTIONS,
            static_cast<unsigned>(found.size()));
    found.resize(OptionPopup::MAX_OPTIONS);
  }
  int currentIndex = 0;
  dictChoices.clear();
  dictChoices.reserve(found.size());
  for (auto& entry : found) {
    if (hasStored && entry.name == stored) currentIndex = static_cast<int>(dictChoices.size());
    dictChoices.push_back(std::move(entry.name));
  }
  {
    RenderLock lock;
    popup = Popup::None;
    dictPicker.show(StrId::STR_DICTIONARY, dictChoices, currentIndex, [this](int index) { onDictionaryPicked(index); });
  }
  requestUpdate();
  return false;
}

void DictionaryWordSelectActivity::onDictionaryPicked(const int index) {
  if (index < 0 || index >= static_cast<int>(dictChoices.size())) return;
  copyName(dictName, sizeof(dictName), dictChoices[index].c_str());
  std::vector<std::string>{}.swap(dictChoices);
  // A failed save only means the picker asks again next time.
  if (!DictionaryRegistry::saveBookChoice(bookCachePath, dictName)) {
    LOG_ERR("DICT", "Could not store the book's dictionary choice");
  }
  if (purpose == Purpose::Lookup) {
    RenderLock lock;
    popupMsg = StrId::STR_DICT_LOOKING_UP;
    popup = Popup::Busy;
  }
  lookupPending = true;
}

void DictionaryWordSelectActivity::performLookup() {
  if (!dictOpenAttempted && !chooseDictionary()) return;
  // Choosing ends here: the picker was answered, or there was nothing to choose.
  if (purpose == Purpose::ChooseDictionary) {
    finish();
    return;
  }
  {
    RenderLock lock;
    popupMsg = StrId::STR_DICT_LOOKING_UP;
    popup = Popup::Busy;
  }
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(dictName);
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  {
    RenderLock lock;
    popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  }
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  const bool found = ok && dict.lookup(lookupText.c_str(), definition, headword, &result);

  if (found) {
    {
      RenderLock lock;
      popup = Popup::None;
      page.reset();
    }
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                                    std::move(definition), dict.definitionsAreHtml());
    if (!activity || !activity->enableLookup(dict, std::move(lookupText))) {
      LOG_ERR("DICT", "OOM: definition activity");
      popup = Popup::Error;
      popupMsg = StrId::STR_DICT_LOW_MEMORY;
      popupTime = millis();
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
      setResult(ActivityResult{result});
      finish();
    });
    return;
  }
  // Name the failure: a genuine miss is "Not found"; a word that WAS found but
  // couldn't be read is a real error — and we distinguish decompression from a
  // low-memory allocation from a generic read error.
  if (!ok) {
    popup = Popup::Error;
    // An index build allocates a scan buffer, so it fails the same way lookups
    // do on a fragmented heap — name that rather than a generic error.
    switch (indexResult) {
      case Dictionary::IndexResult::LowMemory:
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::IndexResult::ReadError:
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::IndexResult::Ok:
      default:
        popupMsg = StrId::STR_DICT_ERROR;  // dict.open() failed, not the index
        break;
    }
  } else {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_DECOMPRESS_ERROR;
        break;
      case Dictionary::LookupResult::LowMemory:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::LookupResult::ReadError:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::LookupResult::NotFound:
      default:
        popup = Popup::NotFound;
        popupMsg = StrId::STR_DICT_NOT_FOUND;
        break;
    }
  }
  popupTime = millis();
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (dictPicker.isActive()) {
    // Repaint only while the dialog is open or a lookup follows; the reader
    // repaints itself once this activity finishes.
    dictPicker.handleInput(mappedInput, [this] {
      if (dictPicker.isActive() || (lookupPending && purpose == Purpose::Lookup)) requestUpdate();
    });
    if (!dictPicker.isActive() && !lookupPending) {
      // Back, or a tap outside the dialog: return to the book without a lookup.
      result.isCancelled = true;
      finish();
    }
    return;
  }
  if (lookupPending) {
    lookupPending = false;
    performLookup();
    return;
  }
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      result.isCancelled = popup == Popup::Error;
      finish();
    }
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);

  if (dictPicker.processRender(renderer, mappedInput)) return;
  // Choosing shows nothing but the picker, so skip the refresh before it opens.
  if (purpose == Purpose::ChooseDictionary) return;
  if (popup != Popup::None) {
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
