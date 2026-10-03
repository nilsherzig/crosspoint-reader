"""Exercise the production settings activity with host-only UI/storage boundaries.

Run with python3 test/scripts/test_flashcard_settings.py (requires a C++20 compiler).
"""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY = ROOT / "src/activities/flashcards/FlashcardSettingsActivity"

# Compile the actual class and method bodies; only hardware/UI dependencies are
# replaced. Sub-activities expose their input contract and return through the
# same callback used by the firmware activity stack.
BOUNDARIES = r"""
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <FlashcardStore.h>

struct GfxRenderer {
  int width = 480, height = 800;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
};
struct MappedInputManager { void resetHomeButtonInput() {} };
struct RenderLock {};
struct IntervalResult { uint32_t value = 0; };
struct KeyboardResult { std::string text; };
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, IntervalResult, KeyboardResult> data;
};
namespace freeink::ui {
struct Insets { int16_t top, right, bottom, left; };
struct TextStyle { int maxLines = 0; };
struct ListItem { const char* label = nullptr; const char* value = nullptr; int16_t actionValue = 0; };
struct ListProps {
  ListItem* items = nullptr;
  int count = 0, action = 0, inputMask = 0, valueInset = 0;
  TextStyle labelText;
};
constexpr int InputTouch = 1;
}
struct UiScreen {
  struct Theme { freeink::ui::TextStyle smallText; } tokens;
  freeink::ui::Insets margin{};
  std::vector<std::string> labels, values;
  void setContentMargin(freeink::ui::Insets insets) { margin = insets; }
  void spacer(int16_t) {}
  Theme& theme() { return tokens; }
  void list(const freeink::ui::ListProps& props) {
    labels.clear(); values.clear();
    for (int i = 0; i < props.count; ++i) {
      assert(props.items[i].actionValue == i);
      labels.emplace_back(props.items[i].label);
      values.emplace_back(props.items[i].value ? props.items[i].value : "");
    }
  }
};
struct UiListActivity {
  static constexpr int ACTION_ROW = 1;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  struct App { void clearTapFlash() {} } app;
  std::unique_ptr<UiListActivity> child;
  std::function<void(const ActivityResult&)> callback;
  UiListActivity(const char*, GfxRenderer& r, MappedInputManager& m) : renderer(r), mappedInput(m) {}
  virtual ~UiListActivity() = default;
  virtual int listCount() const { return 0; }
  virtual const char* headerTitle() const { return nullptr; }
  virtual void buildScreen(UiScreen&) {}
  virtual void activateIndex(int) {}
  virtual bool handleCustomInput() { return false; }
  virtual void onEnter() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate() {}
  void syncListViewport(UiScreen&, freeink::ui::ListProps&) {}
  void startActivityForResult(std::unique_ptr<UiListActivity> activity,
                              std::function<void(const ActivityResult&)> handler) {
    child = std::move(activity); callback = std::move(handler);
  }
  void complete(const ActivityResult& result) {
    child.reset(); auto handler = std::move(callback); handler(result);
  }
};
struct Rect { int x, y, width, height; };
struct UITheme {
  struct Metrics { int topPadding = 8, headerHeight = 40, verticalSpacing = 8; } metrics;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const Metrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer& r, bool, bool) { return {4, 6, r.width - 8, r.height - 12}; }
};
struct OptionPopup {
  static inline std::function<void(int)> selection;
  static inline int count = 0, current = 0;
  bool isActive() const { return static_cast<bool>(selection); }
  template<class Title, class Options>
  void show(Title, Options, int n, int index, std::function<void(int)> callback) {
    count = n; current = index; selection = std::move(callback);
  }
  static void choose(int index) {
    assert(index >= 0 && index < count);
    auto callback = std::move(selection); selection = {}; callback(index);
  }
  bool handleInput(MappedInputManager&, const std::function<void()>&) { return isActive(); }
  bool processRender(GfxRenderer&, MappedInputManager&) { return isActive(); }
};
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
#define LOG_ERR(...) ((void)0)
struct IntervalSelectionActivity : UiListActivity {
  int initial, minimum, maximum, smallStep, largeStep;
  IntervalSelectionActivity(GfxRenderer& r, MappedInputManager& m, const char*, StrId,
                            int v, int low, int high, int small, int large, StrId)
      : UiListActivity("interval", r, m), initial(v), minimum(low), maximum(high), smallStep(small), largeStep(large) {}
};
enum class InputType { Numeric };
struct KeyboardEntryActivity : UiListActivity {
  std::string initial;
  size_t limit;
  KeyboardEntryActivity(GfxRenderer& r, MappedInputManager& m, const char*, std::string v, size_t n, InputType)
      : UiListActivity("keyboard", r, m), initial(std::move(v)), limit(n) {}
};
struct FlashcardStepsActivity : UiListActivity {
  flashcards::Config& config;
  bool relearning;
  FlashcardStepsActivity(GfxRenderer& r, MappedInputManager& m, flashcards::Config& c, bool b)
      : UiListActivity("steps", r, m), config(c), relearning(b) {}
};
struct FlashcardBackupSettingsActivity : UiListActivity {
  FlashcardBackupSettingsActivity(GfxRenderer& r, MappedInputManager& m) : UiListActivity("backup", r, m) {}
};
struct FlashcardDisplaySettingsActivity : UiListActivity {
  FlashcardDisplaySettingsActivity(GfxRenderer& r, MappedInputManager& m) : UiListActivity("display", r, m) {}
};
namespace flashcards {
Config persisted;
bool failSave = false;
int writes = 0;
bool FlashcardStore::loadConfig(Config& config, std::string&) { config = persisted; return true; }
bool FlashcardStore::saveConfig(const Config& config, std::string&) {
  if (failSave) return false;
  persisted = config; ++writes; return true;
}
}
"""

CHECKS = r"""
int main() {
  GfxRenderer renderer;
  MappedInputManager input;
  FlashcardSettingsActivity activity(renderer, input);
  UiListActivity& screenActivity = activity;
  activity.onEnter();
  assert(screenActivity.listCount() == 10);
  UiScreen screen;
  screenActivity.buildScreen(screen);
  assert(screen.labels[0] == "STR_FLASHCARD_NEW_CARDS_PER_DAY");
  assert(screen.labels[9] == "STR_FLASHCARD_BACKUP");
  assert(screen.values[0] == "20 cards" && screen.values[2] == "90%");
  assert(screen.values[4] == "[1, 10]" && screen.values[5] == "[10]");
  assert(screen.values[8].empty() && screen.values[9].empty());

  // The same theme/safe-area path is used in portrait and both landscape directions.
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.width = orientation < 2 ? 480 : 800;
    renderer.height = orientation < 2 ? 800 : 480;
    screenActivity.buildScreen(screen);
    assert(screen.margin.left == 4 && screen.margin.right == 4);
    assert(screen.margin.top == 54 && screen.margin.bottom == 6);
  }

  const int indexes[] = {0, 2, 3};
  const int minima[] = {0, 70, 1};
  const int maxima[] = {1000, 99, 365000};
  const int largeSteps[] = {10, 5, 100};
  for (int i = 0; i < 3; ++i) {
    screenActivity.activateIndex(indexes[i]);
    auto* picker = dynamic_cast<IntervalSelectionActivity*>(activity.child.get());
    assert(picker && picker->minimum == minima[i] && picker->maximum == maxima[i]);
    assert(picker->smallStep == 1 && picker->largeStep == largeSteps[i]);
    activity.complete({false, IntervalResult{static_cast<uint32_t>(maxima[i])}});
  }
  assert(flashcards::persisted.newCardsPerDay == 1000);
  assert(flashcards::persisted.desiredRetention == 0.99f);
  assert(flashcards::persisted.maximumIntervalDays == 365000);

  // Cancellation and a failed SD write must not change the visible or persisted value.
  int writes = flashcards::writes;
  screenActivity.activateIndex(0);
  activity.complete({true, IntervalResult{1}});
  assert(flashcards::writes == writes);
  flashcards::failSave = true;
  screenActivity.activateIndex(0);
  activity.complete({false, IntervalResult{1}});
  screenActivity.buildScreen(screen);
  assert(screen.values[0] == "1000 cards" && flashcards::persisted.newCardsPerDay == 1000);
  flashcards::failSave = false;

  screenActivity.activateIndex(1);
  auto* keyboard = dynamic_cast<KeyboardEntryActivity*>(activity.child.get());
  assert(keyboard && keyboard->limit == 10 && keyboard->initial == "20");
  activity.complete({false, KeyboardResult{"4294967295"}});
  assert(flashcards::persisted.learnAheadLimitMinutes == UINT32_MAX);
  writes = flashcards::writes;
  for (const char* invalid : {"", "4294967296", "-1", "1.5", "12x"}) {
    screenActivity.activateIndex(1);
    activity.complete({false, KeyboardResult{invalid}});
  }
  assert(flashcards::writes == writes);
  screenActivity.activateIndex(1);
  activity.complete({false, KeyboardResult{"0"}});
  assert(flashcards::persisted.learnAheadLimitMinutes == 0);

  for (int i : {4, 5}) {
    screenActivity.activateIndex(i);
    auto* steps = dynamic_cast<FlashcardStepsActivity*>(activity.child.get());
    assert(steps && steps->relearning == (i == 5));
    auto& sequence = i == 5 ? steps->config.relearningSteps : steps->config.learningSteps;
    sequence.minutes[0] = 7;
    flashcards::persisted = steps->config;
    activity.complete({});
  }
  screenActivity.buildScreen(screen);
  assert(screen.values[4] == "[7, 10]" && screen.values[5] == "[7]");

  screenActivity.activateIndex(6);
  assert(OptionPopup::count == 6);
  OptionPopup::choose(5);
  assert(flashcards::persisted.undoBinding == flashcards::UndoBinding::Disabled);
  screenActivity.activateIndex(7);
  assert(OptionPopup::count == 4);
  OptionPopup::choose(3);
  assert(flashcards::persisted.fontPointSize == 18);

  // Submenus save their own config. Reload before a later scalar save so their
  // independent changes are not overwritten by the parent's earlier copy.
  screenActivity.activateIndex(8);
  assert(dynamic_cast<FlashcardDisplaySettingsActivity*>(activity.child.get()));
  flashcards::persisted.showForecast = true;
  flashcards::persisted.showReviewCount = false;
  activity.complete({});
  screenActivity.activateIndex(9);
  assert(dynamic_cast<FlashcardBackupSettingsActivity*>(activity.child.get()));
  flashcards::persisted.backupPassword = "secret";
  flashcards::persisted.backupDirectory = "/other";
  activity.complete({});
  screenActivity.activateIndex(0);
  activity.complete({false, IntervalResult{25}});
  assert(flashcards::persisted.showForecast && !flashcards::persisted.showReviewCount);
  assert(flashcards::persisted.backupPassword == "secret" && flashcards::persisted.backupDirectory == "/other");

  screenActivity.activateIndex(-1);
  screenActivity.activateIndex(10);
  assert(!activity.child);
}
"""


class FlashcardSettingsTests(unittest.TestCase):
    def test_production_activity(self):
        header = ACTIVITY.with_suffix(".h").read_text()
        source = ACTIVITY.with_suffix(".cpp").read_text()
        ids = sorted(set(re.findall(r"STR_[A-Z_]+", header + source)))
        formats = {
            "STR_FLASHCARD_CARD_COUNT": "%u cards",
            "STR_FLASHCARD_MINUTES_FORMAT": "%u min",
            "STR_FLASHCARD_PERCENT_FORMAT": "%u%%",
            "STR_FLASHCARD_DAYS_FORMAT": "%u days",
            "STR_FLASHCARD_POINT_SIZE_FORMAT": "%u pt",
        }
        i18n = "enum class StrId {" + ",".join(ids) + "};\n"
        i18n += "struct FakeI18n { const char* get(StrId id) const { switch (id) {\n"
        for key in ids:
            i18n += f'case StrId::{key}: return "{formats.get(key, key)}";\n'
        i18n += '} return ""; } } I18N;\n#define tr(id) I18N.get(StrId::id)\n'
        header = re.sub(r"^#(?:include|pragma).*\n", "", header, flags=re.MULTILINE)
        source = re.sub(r"^#include.*\n", "", source, flags=re.MULTILINE)
        with tempfile.TemporaryDirectory(prefix="flashcard-settings-") as directory:
            cpp = Path(directory) / "settings.cpp"
            executable = Path(directory) / "settings"
            cpp.write_text(i18n + BOUNDARIES + header + source + CHECKS)
            subprocess.run([
                "c++", "-std=c++20", "-DFREEINK_DEVICE_X4PRO=1",
                "-I", str(ROOT / "lib/Flashcards"), str(cpp), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)

    def test_general_settings_only_launches_the_feature(self):
        header = (ROOT / "src/activities/settings/SettingsActivity.h").read_text()
        source = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text()
        self.assertNotIn("FlashcardStore", header + source)
        self.assertNotIn("flashcards::Config", header + source)
        self.assertNotIn("STR_CAT_FLASHCARDS", header + source)
        self.assertIn("SettingAction::FlashcardSettings", source)
        self.assertIn("makeUniqueNoThrow<FlashcardSettingsActivity>", source)


if __name__ == "__main__":
    unittest.main()
