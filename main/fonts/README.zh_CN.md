<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Kids Points 字体

固件使用 Noto Sans SC Regular，生成未压缩 4bpp LVGL 字体，存于只读 Flash。
14px 和 24px 子集覆盖固定界面文案。18px 正文字体覆盖可打印 ASCII、标点、
GB2312 一级和二级全部 6763 个汉字，以及固定界面字符。动态任务、奖品和
通知使用 18px。范围外字符显示为 `?`，不保证任意 Unicode 字符。

使用具有合法授权的 Noto Sans SC Regular 字体生成：

```bash
python3 tools/generate_kids_fonts.py /path/to/NotoSansSC-Regular.ttf
```

生成器固定使用 `lv_font_conv@1.5.3`，验证 GB2312 字表，并逐字号检查生成的 cmap。
执行 `python3 tools/generate_kids_fonts.py --audit-only` 可检查仓库内字库覆盖。
字形覆盖检查不能替代上板显示和堆内存测量。
