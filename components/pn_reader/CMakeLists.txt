idf_component_register(SRCS pn_reader_toolbar.c pn_page_index.c pn_turn_transition.c pn_reader.c pn_reader_app.c pn_reader_input.c pn_reader_chrome.c pn_bookmark_ui.c pn_style_ui.c pn_jump_ui.c pn_font_ui.c pn_epub_page.c pn_epub_image.c pn_epub_reader.c pn_epub_app.c pn_toc_ui.c pn_transfer_view.c pn_transfer_hub.c pn_localsend_view.c INCLUDE_DIRS include REQUIRES pn_transfer pn_text pn_storage pn_font pn_display pn_format pn_image pn_library pn_widgets pn_qr esp_hw_support)

# 阻止单函数用掉产品任务一半以上栈；累计调用深度仍需真机水位验证。/ Reject frames exceeding half the product task stack; validate cumulative depth on-device.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=16384 -fstack-usage)
