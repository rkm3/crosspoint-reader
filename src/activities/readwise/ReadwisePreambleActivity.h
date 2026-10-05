#pragma once

#include <ReadwiseDocument.h>

#include "activities/UiListActivity.h"

/**
 * Metadata page shown before a Readwise article opens.
 *
 * Holds one Document (~800 bytes, too large for the stack) loaded by id from
 * docs.bin. Confirm, or the Open/Download button, finishes with a result so
 * the library runs the existing open-or-download path. Back cancels.
 */
class ReadwisePreambleActivity final : public UiListActivity {
 public:
  ReadwisePreambleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* id);

  void onEnter() override;

 protected:
  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

 private:
  enum class Row : uint8_t {
    Title,
    Author,
    Site,
    Source,
    Summary,
    Words,
    Category,
    Progress,
    Updated,
    Moved,
    Downloaded,
  };

  static constexpr freeink::ui::ActionId ACTION_OPEN = ACTION_USER;

  void rebuildRows();
  void confirmOpen();
  void cancel();
  static void openTrampoline(const freeink::ui::ActionEvent& event, void* user);

  char documentId[readwise::ID_CAP] = {};
  readwise::Document doc;
  char wordsBuf[16] = {};
  char progressBuf[8] = {};
  uint8_t rows[11] = {};
  uint8_t rowCount = 0;
  bool loaded = false;
};
