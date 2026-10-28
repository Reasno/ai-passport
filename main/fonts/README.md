<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Kids Points fonts

The firmware uses Noto Sans SC Regular as uncompressed 4bpp LVGL fonts in
read-only Flash. The 14px and 24px subsets cover fixed UI text. The 18px body
font covers printable ASCII, punctuation, all 6,763 GB2312 level-1/level-2
Hanzi, and fixed UI characters. Dynamic task, reward, notification and ledger
text uses 18px. Unsupported characters display as `?`; arbitrary Unicode is
not guaranteed.

Generate with a licensed Noto Sans SC Regular font:

```bash
python3 tools/generate_kids_fonts.py /path/to/NotoSansSC-Regular.ttf
```

The generator pins `lv_font_conv@1.5.3`, validates the GB2312 inventory and
audits the generated cmap for each size. Audit checked-in assets with
`python3 tools/generate_kids_fonts.py --audit-only`.
Font coverage does not replace on-device rendering and heap measurements.
