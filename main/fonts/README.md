# kids_points 中文字体生成

固件内置三套真实字体：

- `lv_font_noto_sc_14.c`：Noto Sans SC Regular 14px，仅包含 Home 紧凑控件固定文案。
- `lv_font_noto_sc_18.c`：Noto Sans SC Regular 18px，ASCII、常用标点和 GB2312 一级汉字（3755 字），用于所有动态 MQTT 文本。
- `lv_font_noto_sc_24.c`：Noto Sans SC Regular 24px，仅包含产品 UI 固定文案、数字、ASCII 和常用标点，不宣称覆盖动态汉字；动态任务名/奖品名始终使用 18px。

重新生成：

```bash
python3 tools/generate_kids_fonts.py /path/to/NotoSansSC-Regular.ttf
```

脚本通过 `npx --yes lv_font_conv@1.5.3` 固定转换器版本，使用 4bpp LVGL 格式。脚本自动按 GB2312 编码 16～55 区生成一级字表，并校验恰好 3755 个汉字。
