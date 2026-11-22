// Compile with the pinned SDK's FreeInkUI include and test/host include paths.
#define main sdkHostSuiteMain
#include "test_freeinkui.cpp"
#undef main

// Only the availability status uses font 7. Its first lineHeight query is
// observable even if no remaining pixels could be painted.
struct PageTarget : FakeDrawTarget {
  mutable int availabilityQueries = 0;
  int16_t lineHeight(FontId font) const override {
    if (font == 7) ++availabilityQueries;
    return FakeDrawTarget::lineHeight(font);
  }
};

int main() {
  for (int height : {0, 47, 48, 56, 57, 104, 112, 160, 168, 480}) {
    PageTarget draw;
    auto device = makeDevice(240, 600);  // Keep action bands away from screen-edge snapping.
    InputSnapshot input;
    InteractionBuffer<8> hits;
    Frame<8> frame(draw, device, input, hits);
    PublicationPageProps props;
    props.padding = {};
    props.primary.label = "First";
    props.primary.action = 1;
    props.secondary.label = "Second";
    props.secondary.action = 2;
    props.more.label = "More";
    props.more.action = 3;
    props.availability.status = "Available";
    props.availability.headingText.font = 7;
    publicationPage(frame, Rect{0, 32, 240, static_cast<int16_t>(height)}, props);
    const int buttons = height >= 160 ? 3 : height >= 104 ? 2 : height >= 48 ? 1 : 0;
    CHECK_EQ(hits.count(), static_cast<size_t>(buttons));
    for (int i = 0; i < buttons; ++i) {
      CHECK_EQ(hits.data()[i].action, i + 1);
      CHECK_EQ(hits.data()[i].rect.y, 32 + height - 48 - 56 * i);
      CHECK_EQ(hits.data()[i].rect.height, 48);
    }
    CHECK_EQ(draw.availabilityQueries > 0, height > buttons * 56);
  }
  testPublicationPage();
  testPublicationPageDescriptionPainter();
  testPublicationStylingAndButtons();
  std::printf("publication-page: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed != 0;
}
