#pragma once

#include <stddef.h>
#include "lvgl.h"

/* Standard-size text is capped at 10 Unicode code points per explicit line.
 * Overflow is represented as the first 9 code points plus one ellipsis.
 * Small text deliberately bypasses this helper and is width-clipped by LVGL. */
#define UI_TEXT_STANDARD_MAX_CHARS 10

/*
 * Copies UTF-8 text without splitting a code point. Each explicit line is
 * limited to max_line_chars. A truncated line contains max_line_chars - 1
 * original code points followed by U+2026, so it never exceeds the limit.
 */
void ui_text_limit_lines(const char *input, char *output, size_t output_size,
                         size_t max_line_chars);

/* Replaces unsupported UTF-8 code points with '?' so server-provided text
 * outside the generated font contract never renders as a tofu square. */
void ui_text_font_fallback(const lv_font_t *font, char *text);
