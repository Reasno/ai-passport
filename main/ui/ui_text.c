#include "ui_text.h"

#include <stdbool.h>
#include <string.h>

static size_t utf8_codepoint_bytes(const unsigned char *text)
{
    if (text[0] < 0x80) return 1;
    if ((text[0] & 0xE0) == 0xC0 && (text[1] & 0xC0) == 0x80) return 2;
    if ((text[0] & 0xF0) == 0xE0 && (text[1] & 0xC0) == 0x80 &&
        (text[2] & 0xC0) == 0x80) return 3;
    if ((text[0] & 0xF8) == 0xF0 && (text[1] & 0xC0) == 0x80 &&
        (text[2] & 0xC0) == 0x80 && (text[3] & 0xC0) == 0x80) return 4;
    return 1;
}

static size_t line_codepoints(const char *text)
{
    size_t count = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && *p != '\n') {
        p += utf8_codepoint_bytes(p);
        count++;
    }
    return count;
}

static bool append_bytes(char *output, size_t output_size, size_t *used,
                         const char *source, size_t bytes)
{
    if (*used + bytes >= output_size) return false;
    memcpy(output + *used, source, bytes);
    *used += bytes;
    return true;
}

void ui_text_limit_lines(const char *input, char *output, size_t output_size,
                         size_t max_line_chars)
{
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!input || max_line_chars == 0) return;

    const unsigned char *p = (const unsigned char *)input;
    size_t used = 0;
    while (*p) {
        size_t count = line_codepoints((const char *)p);
        bool truncated = count > max_line_chars;
        size_t copy_count = truncated ? (max_line_chars > 3 ? max_line_chars - 3 : 0) : count;
        for (size_t i = 0; i < copy_count && *p && *p != '\n'; i++) {
            size_t bytes = utf8_codepoint_bytes(p);
            if (!append_bytes(output, output_size, &used, (const char *)p, bytes)) goto done;
            p += bytes;
        }
        if (truncated) {
            static const char ellipsis[] = "...";
            size_t dots = max_line_chars < 3 ? max_line_chars : 3;
            if (!append_bytes(output, output_size, &used, ellipsis, dots)) goto done;
            while (*p && *p != '\n') p += utf8_codepoint_bytes(p);
        }
        if (*p == '\n') {
            if (!append_bytes(output, output_size, &used, "\n", 1)) goto done;
            p++;
        }
    }

done:
    output[used] = '\0';
}

void ui_text_font_fallback(const lv_font_t *font, char *text)
{
    if (!font || !text) return;
    unsigned char *read = (unsigned char *)text;
    unsigned char *write = (unsigned char *)text;
    while (*read) {
        size_t bytes = utf8_codepoint_bytes(read);
        uint32_t codepoint = 0;
        if (bytes == 1) codepoint = read[0];
        else if (bytes == 2) codepoint = ((read[0] & 0x1F) << 6) | (read[1] & 0x3F);
        else if (bytes == 3) codepoint = ((read[0] & 0x0F) << 12) | ((read[1] & 0x3F) << 6) | (read[2] & 0x3F);
        else codepoint = ((read[0] & 0x07) << 18) | ((read[1] & 0x3F) << 12) |
                         ((read[2] & 0x3F) << 6) | (read[3] & 0x3F);
        lv_font_glyph_dsc_t glyph;
        bool supported = codepoint == '\n' || codepoint == '\r' || codepoint == '\t' ||
                         lv_font_get_glyph_dsc(font, &glyph, codepoint, 0);
        if (supported) {
            memmove(write, read, bytes);
            write += bytes;
        } else {
            *write++ = '?';
        }
        read += bytes;
    }
    *write = '\0';
}
