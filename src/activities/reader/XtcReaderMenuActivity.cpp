#include "XtcReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/HeaderDate.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace ReaderChrome {
const ReaderStatusBarConfig& config(const ReaderStatusBarPosition position, const bool xtc) {
  const auto source = xtc ? xtcStatusBarConfigPosition(position, SETTINGS.legacyXtcTopUsesBottom) : position;
  return source == ReaderStatusBarPosition::Top ? SETTINGS.topReaderStatusBar : SETTINGS.bottomReaderStatusBar;
}

int height(const GfxRenderer& renderer, const ReaderStatusBarPosition position, const bool xtc, const bool forceText) {
  if (xtc && ((position == ReaderStatusBarPosition::Top &&
               SETTINGS.xtcStatusBarMode != CrossPointSettings::XTC_STATUS_BAR_TOP) ||
              (position == ReaderStatusBarPosition::Bottom &&
               SETTINGS.xtcStatusBarMode != CrossPointSettings::XTC_STATUS_BAR_BOTTOM)))
    return 0;
  const auto& bar = config(position, xtc);
  const int progress = bar.progressBar == CrossPointSettings::HIDE_PROGRESS
                           ? 0
                           : (std::min<int>(bar.progressBarThickness, 2) + 1) * 2 + 2;
  return readerStatusBarTotalHeight(
      position, (forceText || bar.hasTextItems(halClock.isAvailable())), progress,
      std::max(renderer.getLineHeight(SMALL_FONT_ID), ReaderStatusBarConfig::BOOKMARK_HEIGHT) + 4);
}

Rect band(const GfxRenderer& renderer, const ReaderStatusBarPosition position, const bool xtc, const bool forceText) {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int h = height(renderer, position, xtc, forceText);
  return Rect{left, position == ReaderStatusBarPosition::Top ? top : renderer.getScreenHeight() - bottom - h,
              std::max(0, renderer.getScreenWidth() - left - right), h};
}

bool tapped(const GfxRenderer& renderer, const int y, const bool xtc, const bool forceFooter) {
  constexpr std::array positions{ReaderStatusBarPosition::Top, ReaderStatusBarPosition::Bottom};
  return std::any_of(positions.begin(), positions.end(), [&](const auto position) {
    const auto rect = band(renderer, position, xtc, forceFooter && position == ReaderStatusBarPosition::Bottom);
    return rect.height > 0 && y >= rect.y && y < rect.y + rect.height;
  });
}

void draw(const GfxRenderer& renderer, const ReaderStatusBarPosition position, const Values& values, const bool visible,
          const bool xtc) {
  const auto rect =
      band(renderer, position, xtc, !values.notice.empty() && position == ReaderStatusBarPosition::Bottom);
  if (rect.width <= 0 || rect.height <= 0) return;
  // Fixed image formats must erase their baked-in strip even when hidden.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  if (!visible) return;
  const auto& bar = config(position, xtc);
  const int bookmarkReserve =
      position == ReaderStatusBarPosition::Bottom ? ReaderStatusBarConfig::BOOKMARK_WIDTH + 5 : 0;
  if (values.bookmarked && bookmarkReserve) {
    const int bookmarkY = rect.y + std::max(0, (rect.height - ReaderStatusBarConfig::BOOKMARK_HEIGHT) / 2);
    for (int x = 0; x < ReaderStatusBarConfig::BOOKMARK_WIDTH; ++x) {
      const int notch = std::min(5, std::min(x, ReaderStatusBarConfig::BOOKMARK_WIDTH - 1 - x));
      renderer.fillRect(rect.x + 1 + x, bookmarkY, 1, ReaderStatusBarConfig::BOOKMARK_HEIGHT - notch, true);
    }
  }
  for (unsigned slot = 0; slot < ReaderStatusBarConfig::SLOT_COUNT; ++slot) {
    char buffer[64] = {};
    std::string value;
    switch (bar.slots[slot]) {
      case ReaderStatusBarItem::Clock:
        if (halClock.isAvailable())
          halClock.formatTime(buffer, sizeof(buffer), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
        break;
      case ReaderStatusBarItem::Date:
        formatHeaderDateText(buffer, sizeof(buffer));
        break;
      case ReaderStatusBarItem::Battery:
        snprintf(buffer, sizeof(buffer), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
        break;
      case ReaderStatusBarItem::TimeLeftBook:
        value = values.timeBook;
        break;
      case ReaderStatusBarItem::TimeLeftChapter:
        value = values.timeChapter;
        break;
      case ReaderStatusBarItem::ChapterPageCount:
        if (values.pages > 0)
          snprintf(buffer, sizeof(buffer), "%s%d/%d", values.estimated ? "~" : "", values.page, values.pages);
        break;
      case ReaderStatusBarItem::StablePageNumber:
        if (values.stablePages > 0 && values.stablePage > 0)
          snprintf(buffer, sizeof(buffer), "%d/%d", values.stablePage, values.stablePages);
        break;
      case ReaderStatusBarItem::BookProgressPercentage:
        snprintf(buffer, sizeof(buffer), "%.*f%%", std::min<int>(bar.percentageFormat, 2),
                 static_cast<double>(std::clamp(values.bookPercent, 0.0f, 100.0f)));
        break;
      case ReaderStatusBarItem::TitleBook:
        value = values.bookTitle;
        break;
      case ReaderStatusBarItem::TitleChapter:
        value = values.chapterTitle;
        break;
      default:
        break;
    }
    if (value.empty()) value = buffer;
    if (slot == ReaderStatusBarConfig::CENTER && position == ReaderStatusBarPosition::Bottom && !values.notice.empty())
      value = values.notice;
    const int usable = rect.width - bookmarkReserve;
    const int x = rect.x + bookmarkReserve + usable * static_cast<int>(slot) / 7;
    const int end = rect.x + bookmarkReserve + usable * static_cast<int>(slot + 1) / 7;
    const auto label = renderer.truncatedText(SMALL_FONT_ID, value.c_str(), std::max(0, end - x - 4));
    renderer.drawText(SMALL_FONT_ID, x + 2, rect.y + 2, label.c_str(), true);
  }
  if (bar.progressBar != CrossPointSettings::HIDE_PROGRESS) {
    const float progress =
        bar.progressBar == CrossPointSettings::BOOK_PROGRESS ? values.bookPercent : values.chapterPercent;
    const int thickness = (std::min<int>(bar.progressBarThickness, 2) + 1) * 2;
    renderer.fillRect(rect.x, rect.y + rect.height - thickness,
                      static_cast<int>(rect.width * std::clamp(progress, 0.0f, 100.0f) / 100.0f), thickness, true);
  }
}
}  // namespace ReaderChrome

namespace fui = freeink::ui;

namespace {
constexpr int kTitleFontId = UI_10_FONT_ID;
constexpr int kTitleMaxLines = 2;
constexpr int kBatteryTextReserveWidth = 90;
constexpr int kTitleLineGap = 1;
constexpr fui::ActionId kActionRow = 1;
}  // namespace

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             const bool hasChapters, const bool isBookCompleted,
                                             const bool statsEnabled)
    : Activity("XtcReaderMenu", renderer, mappedInput),
      title(std::move(title)),
      items(buildMenuItems(hasChapters, isBookCompleted, mappedInput.hasTouchHardware())),
      statsEnabled(statsEnabled),
      ui(renderer) {}

std::vector<XtcReaderMenuActivity::MenuItem> XtcReaderMenuActivity::buildMenuItems(const bool hasChapters,
                                                                                   const bool isBookCompleted,
                                                                                   const bool hasTouch) {
  std::vector<MenuItem> menuItems;
  menuItems.reserve(7 + (hasTouch ? 1u : 0u));
  if (hasChapters) {
    menuItems.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  }
  menuItems.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  if (SETTINGS.shouldTrackReadingStats()) {
    menuItems.push_back({MenuAction::TOGGLE_STATS, StrId::STR_TRACK_READING_STATS});
  }
  menuItems.push_back(
      {MenuAction::TOGGLE_COMPLETED, isBookCompleted ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  menuItems.push_back({MenuAction::DELETE_STATS, StrId::STR_DELETE_BOOK_STATS});
  menuItems.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  menuItems.push_back({MenuAction::SEND_NEARBY_BOOK, StrId::STR_SEND_NEARBY_BOOK});
  if (hasTouch) {
    menuItems.push_back({MenuAction::DISABLE_TOUCHSCREEN, StrId::STR_DISABLE_TOUCHSCREEN});
  }
  return menuItems;
}

void XtcReaderMenuActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderTouchscreenOverride(true);
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  refreshListItems();
  ui.reset();
  ui.app.on(kActionRow, &XtcReaderMenuActivity::onRowEvent, this);
  ui.app.setScreen(&XtcReaderMenuActivity::listScreen, this);
  requestUpdate();
}

void XtcReaderMenuActivity::onExit() {
  mappedInput.setReaderTouchscreenOverride(false);
  Activity::onExit();
}

void XtcReaderMenuActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void XtcReaderMenuActivity::selectCurrent() {
  if (items[selectedIndex].action == MenuAction::DISABLE_TOUCHSCREEN) {
    SETTINGS.disableReaderTouchscreen = SETTINGS.disableReaderTouchscreen ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  setResult(MenuResult{static_cast<int>(items[selectedIndex].action)});
  finish();
}

void XtcReaderMenuActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<XtcReaderMenuActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int>(self->items.size())) return;
  self->selectedIndex = event.value;
  self->ui.app.clearTapFlash();
  self->selectCurrent();
}

void XtcReaderMenuActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishCancelled();
    return;
  }

  if (mappedInput.wasHomeGesture()) {
    finishCancelled();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectCurrent();
    return;
  }

  if (ui.routingReady()) {
    fui::ActionEvent event{};
    if (ui.routeTouch(mappedInput, event)) {
      if (ui.app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, static_cast<int>(items.size()));
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, static_cast<int>(items.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, static_cast<int>(items.size()));
    requestUpdate();
  });
}

void XtcReaderMenuActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<XtcReaderMenuActivity*>(user)->buildListScreen(screen);
}

void XtcReaderMenuActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + listHeaderHeight + metrics.verticalSpacing;
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(contentTop), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing * 2),
                                      0});

  refreshListItems();
  fui::ListProps props;
  props.items = listItems.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = selectedIndex;
  props.topIndex = topIndex;
  props.action = kActionRow;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.valueText = screen.theme().smallText;
  visibleRows = std::max(1, static_cast<int>(configureUiList(props, screen.theme(), screen.body())));
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(items.size()));
  props.topIndex = topIndex;
  screen.list(props);
}

void XtcReaderMenuActivity::refreshListItems() {
  for (size_t index = 0; index < items.size(); ++index) {
    listItems[index] = fui::ListItem{};
    listItems[index].label = I18N.get(items[index].labelId);
    listItems[index].actionValue = static_cast<int16_t>(index);
    if (items[index].action == MenuAction::TOGGLE_STATS) {
      listItems[index].value = I18N.get(statsEnabled ? StrId::STR_ON : StrId::STR_OFF);
    }
    if (items[index].action == MenuAction::DISABLE_TOUCHSCREEN) {
      listItems[index].value = I18N.get(SETTINGS.disableReaderTouchscreen ? StrId::STR_ON : StrId::STR_OFF);
    }
  }
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const Rect standardHeader{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  const int titleX = mappedInput.hasTouchHardware() ? TouchHeaderBackButton::layout(standardHeader).titleX
                                                    : metrics.contentSidePadding;
  const int titleMaxWidth = std::max(0, pageWidth - titleX - metrics.contentSidePadding - kBatteryTextReserveWidth);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, title.c_str(), titleMaxWidth, kTitleMaxLines, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight +
                               std::max(0, static_cast<int>(titleLines.size()) - 1) * kTitleLineGap;
  const int headerHeight = std::max(metrics.headerHeight, metrics.batteryBarHeight + titleBlockHeight + 16);
  listHeaderHeight = headerHeight;
  const Rect header{0, metrics.topPadding, pageWidth, headerHeight};
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, "", true, 0, nullptr, 0);
  } else {
    GUI.drawHeader(renderer, header, "");
  }

  const int titleY = metrics.topPadding + metrics.batteryBarHeight + 3;
  for (int i = 0; i < static_cast<int>(titleLines.size()); ++i) {
    renderer.drawText(kTitleFontId, titleX, titleY + i * (titleLineHeight + kTitleLineGap), titleLines[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }

  ui.render();

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
