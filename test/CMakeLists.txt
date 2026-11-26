cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MemoryBudget
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

option(CROSSPOINT_HOST_SANITIZE "Enable ASan and UBSan for the combined host suite" OFF)
if(CROSSPOINT_HOST_SANITIZE)
  target_compile_options(crosspoint_test_common INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(crosspoint_test_common INTERFACE -fsanitize=address,undefined)
endif()

# Shared imported target must be visible to sibling parser/model harnesses.
find_package(EXPAT REQUIRED)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(url_utils)
add_subdirectory(opds_feed_error)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(em_dash_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(book_metadata_cache)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(inflate_reader)
add_subdirectory(zip_file)
add_subdirectory(xtc_parser)
add_subdirectory(fs_helpers)
add_subdirectory(http_header)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)

add_subdirectory(gif_support)

add_subdirectory(bmp_to_bmp)

add_subdirectory(sd_card_font)
add_subdirectory(sd_font_registry)
add_subdirectory(clipping_preview)
add_subdirectory(glyph_bitmap)

add_subdirectory(clipping_store)
add_subdirectory(reader_session)
add_subdirectory(plugin_catalog_paging)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(plugin_picker_layout)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME HalRetainedOverlayNormal
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/crosspoint_migration/hal-retained-overlay/run.py
          --mode normal --evidence ${CMAKE_CURRENT_BINARY_DIR}/hal-retained-overlay-normal-evidence)
add_test(NAME HalRetainedOverlaySanitized
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/crosspoint_migration/hal-retained-overlay/run.py
          --mode sanitized --evidence ${CMAKE_CURRENT_BINARY_DIR}/hal-retained-overlay-sanitized-evidence)
set_tests_properties(HalRetainedOverlayNormal HalRetainedOverlaySanitized PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME translation_spacing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_translation_spacing.py)
add_test(NAME TrustedTimeTimezone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trusted_time.py)
add_test(NAME RemoteButtonInputIntegration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_remote_button_input.py)
add_test(NAME SideButtonOrientation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_side_button_orientation.py)
add_test(NAME confirmation_translations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_confirmation_translations.py)
add_test(NAME BleLearning COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ble_learning.py)
add_subdirectory(ble_link_note_title)
add_subdirectory(ble_chapter_build_gate)
add_subdirectory(ble_remote_turn_gate)

add_subdirectory(sticky_no_bluetooth)
add_subdirectory(${REPO_ROOT}/lib/BlePageTurner/test BlePageTurnerTests)

add_test(NAME selection_buttons COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_selection_buttons.py)
add_test(NAME button_remap_release COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_button_remap_release.py)
add_test(NAME list_viewport COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_list_viewport.py)
add_test(NAME clipping_highlight_planes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_highlight_planes.py)
add_test(NAME clipping_selection_text COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_selection_text.py)

add_test(NAME recursive_dictionary COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recursive_dictionary.py)

add_subdirectory(preview_block_locator)
add_subdirectory(text_pool)
add_subdirectory(clip_word_store)
add_subdirectory(dict_layout)
add_subdirectory(reader_progress_save_debouncer)
add_subdirectory(manual_page_turn_queue)
add_subdirectory(page_count_estimator)
add_subdirectory(section_page_index)
add_subdirectory(section_persistence)
add_subdirectory(compact_table_layout)
add_subdirectory(quick_lock)
add_subdirectory(frontlight_schedule)
add_subdirectory(pinch_font_resize)
add_subdirectory(font_catalog_index)
add_subdirectory(reader_status_bar_tap_target)
add_subdirectory(two_finger_swipe)
add_subdirectory(option_popup)
add_subdirectory(sleep_wake_policy)
add_subdirectory(firmware_board_tag)
add_subdirectory(ota_staging_guard)
add_subdirectory(reader_drawer_model)
add_subdirectory(stable_page_navigation)
add_subdirectory(touch_reader_preview)
add_subdirectory(dictionary_word_parts)
add_subdirectory(input_release_guard)
add_subdirectory(release_suppression)
add_subdirectory(bitmap_resample)
add_subdirectory(pxc_v2)
add_subdirectory(memory_policy)
add_subdirectory(epub_grayscale)
add_subdirectory(ui_symbol_fallback)
add_subdirectory(buffered_file)
add_subdirectory(ota_boot_switch)
add_subdirectory(http_redirect_policy)
add_subdirectory(hal_file_stream)
add_subdirectory(persistable_store_atomic)
add_subdirectory(book_reading_stats_atomic)
add_subdirectory(app_store_wifi)
add_subdirectory(app_store)
add_subdirectory(lua_http)
add_subdirectory(lua_device_identity)
add_subdirectory(crosscub)
add_subdirectory(file_actions)
add_subdirectory(notes_realtime)
