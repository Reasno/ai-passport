#!/usr/bin/env python3
"""Generate and audit the three Kids Points LVGL fonts.

The audit decodes lv_font_conv's generated cmaps instead of trusting the
command-line comment.  Run without a TTF to audit checked-in fonts only:
    tools/generate_kids_fonts.py --audit-only
"""
import argparse
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
out = root / "main" / "fonts"
FONT_FILES = {
    14: out / "lv_font_noto_sc_14.c",
    18: out / "lv_font_noto_sc_18.c",
    24: out / "lv_font_noto_sc_24.c",
}
SOURCE_FILES = [
    path for path in sorted((root / "main").rglob("*"))
    if path.suffix in {".c", ".h"} and "fonts" not in path.parts
]
LOCAL_CONFIG_FILES = [root / "sdkconfig.defaults", root / "sdkconfig"]
EXPLICIT_CHILD_NAME_CHARS = "".join(chr(codepoint) for codepoint in (0x8C37, 0x6C81, 0x56ED, 0x8431))
ASCII_PUNCT = "".join(chr(i) for i in range(0x20, 0x7F)) + "，。！？：；、（）《》“”‘’￥"
FORBIDDEN_UI_SYMBOLS = "↑↓✓○★·…—　"
SMALL_TEXT = """
积分余额今日进度今日任务兑换奖品互动游戏请稍候选择确认长按主页
上下中键上键下键设备滚动打卡执行切换返回兑换页抽奖进行中请等待
已配对未配对重配找伙伴响铃和近距离信号请先完成配对
纯娱乐不增减积分可用内存等待扫描中让伙伴离线不可用
正在说话松开结束对讲本地娱乐对战今日价格当前余额
哥哥妹妹小朋友分秒完成调试预览未发送
""" + "+：%/.MQTTWi-FidBmKBPassport"
TITLE_TEXT = """
积分余额当前余额确认完成确认兑换恭喜伙伴就在附近等待近距离信号
伙伴正在找你分
""" + "！？：0123456789"


def gb2312_level1():
    chars = []
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                chars.append(bytes((high, low)).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    if len(chars) != 3755:
        raise SystemExit(f"GB2312 level-1 count mismatch: {len(chars)}")
    return "".join(chars)


def unique(text):
    return "".join(dict.fromkeys(text.replace("\n", "")))


def c_string_literals(text):
    # UI text is kept in ordinary UTF-8 C literals; concatenated and formatted
    # strings are intentionally included so toast/status glyphs are audited too.
    return re.findall(r'"((?:\\.|[^"\\])*)"', text)


def static_source_chars():
    chars = []
    for path in SOURCE_FILES:
        text = path.read_text(encoding="utf-8")
        if path.parent.name == "ui" and "LV_FONT_MONTSERRAT" in text:
            raise SystemExit(f"Chinese UI must not use Montserrat: {path}")
        for literal in c_string_literals(text):
            try:
                decoded = bytes(literal, "utf-8").decode("unicode_escape").encode("latin1").decode("utf-8")
            except (UnicodeDecodeError, UnicodeEncodeError):
                decoded = literal
            forbidden = sorted(set(decoded) & set(FORBIDDEN_UI_SYMBOLS))
            if forbidden:
                raise SystemExit(f"unverified Unicode UI symbol(s) in {path}: {''.join(forbidden)}")
            chars.extend(c for c in decoded if ord(c) >= 0x80)
    # Local identity is intentionally ignored by Git, but it is still rendered on Home.
    # Scan only the user-visible identity keys so Wi-Fi credentials never enter generated comments.
    for path in LOCAL_CONFIG_FILES:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for key in ("KP_CHILD_NAME", "KP_CHILD_ROLE"):
            match = re.search(rf'^CONFIG_{key}="(.*)"$', text, re.M)
            if match:
                chars.extend(c for c in match.group(1) if ord(c) >= 0x80)
    chars.extend("小朋友哥哥妹妹" + EXPLICIT_CHILD_NAME_CHARS)
    return "".join(sorted(set(chars)))


def parse_generated_cmap(path):
    text = path.read_text(encoding="utf-8")
    arrays = {}
    for name, body in re.findall(
        r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", text, re.S
    ):
        arrays[name] = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body)]
    glyphs = set()
    block_match = re.search(r"static const lv_font_fmt_txt_cmap_t cmaps\[\].*?=\s*\{(.*?)\n\};", text, re.S)
    if not block_match:
        raise SystemExit(f"cannot find cmap table: {path}")
    for block in re.findall(r"\{(.*?)\}", block_match.group(1), re.S):
        start_match = re.search(r"\.range_start\s*=\s*(\d+)", block)
        length_match = re.search(r"\.range_length\s*=\s*(\d+)", block)
        if not start_match or not length_match:
            continue
        start, length = int(start_match.group(1)), int(length_match.group(1))
        if "CMAP_FORMAT0" in block:
            glyphs.update(range(start, start + length))
        else:
            list_match = re.search(r"\.unicode_list\s*=\s*(unicode_list_\d+)", block)
            if not list_match or list_match.group(1) not in arrays:
                raise SystemExit(f"cannot decode sparse cmap in {path}")
            glyphs.update(start + offset for offset in arrays[list_match.group(1)])
    return glyphs


def required_sets():
    source = static_source_chars()
    return {
        14: unique(ASCII_PUNCT + SMALL_TEXT + source),
        18: unique(ASCII_PUNCT + gb2312_level1() + source),
        24: unique(ASCII_PUNCT + TITLE_TEXT + source + EXPLICIT_CHILD_NAME_CHARS),
    }


def audit(required):
    for size, path in FONT_FILES.items():
        glyphs = parse_generated_cmap(path)
        missing = sorted(set(map(ord, required[size])) - glyphs)
        if missing:
            preview = "".join(chr(cp) for cp in missing[:80])
            raise SystemExit(f"{size}px missing {len(missing)} glyph(s): {preview}")
        print(f"{size}px: glyphs={len(glyphs)} required={len(set(required[size]))} coverage=100%")
    dynamic = set(gb2312_level1()) | set(ASCII_PUNCT)
    print(f"dynamic 18px contract: ASCII + GB2312 level-1 ({len(dynamic)} unique glyphs); outside range falls back to '?'")


def generate(font, required):
    out.mkdir(parents=True, exist_ok=True)
    for size in (14, 18, 24):
        subprocess.run([
            "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
            "--size", str(size), "--bpp", "4", "--format", "lvgl",
            "--no-compress", "--no-prefilter", "--lv-include", "lvgl.h",
            "--symbols", required[size], "--output", str(FONT_FILES[size]),
        ], check=True)
        generated = FONT_FILES[size].read_text(encoding="utf-8").rstrip() + "\n"
        FONT_FILES[size].write_text(generated, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("font", nargs="?", type=pathlib.Path)
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()
    required = required_sets()
    if not args.audit_only:
        if not args.font:
            parser.error("font path is required unless --audit-only is used")
        font = args.font.expanduser().resolve()
        if not font.is_file():
            raise SystemExit(f"font not found: {font}")
        generate(font, required)
    audit(required)


if __name__ == "__main__":
    main()
