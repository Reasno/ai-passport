#!/usr/bin/env python3
import pathlib
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: generate_kids_fonts.py /path/to/NotoSansSC-Regular.ttf")
font = pathlib.Path(sys.argv[1]).resolve()
if not font.is_file():
    raise SystemExit(f"font not found: {font}")
root = pathlib.Path(__file__).resolve().parents[1]
out = root / "main" / "fonts"
out.mkdir(parents=True, exist_ok=True)
chars = []
for high in range(0xB0, 0xD8):
    for low in range(0xA1, 0xFF):
        try:
            chars.append(bytes((high, low)).decode("gb2312"))
        except UnicodeDecodeError:
            pass
if len(chars) != 3755:
    raise SystemExit(f"GB2312 level-1 count mismatch: {len(chars)}")
ascii_punct = "".join(chr(i) for i in range(0x20, 0x7F)) + "，。！？：；、（）《》“”‘’—…￥·✓○↑↓★"
fixed = ascii_punct + "今日积分共享余额任务已完成列表兑换奖品立即刷新在线离线同步中选择确认长按主页滚动打卡还没有需要爸爸妈妈当前连接家里的再试电视一次大转盘抽奖价格暂未开放不可换正在处理请稍候幸运麦当劳元开始谢谢恭喜抽中自动入账到账户请求超时格式错误保留旧数据成功失败继续努力哦谷沁园萱哥哥妹妹进度回"
compact = "今日任务兑换抽奖在线离线积分余额进度哥哥妹妹请稍候选择确认长按回主页↑↓✓. "

def run(size: int, symbols: str, output: pathlib.Path):
    subprocess.run([
        "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
        "--size", str(size), "--bpp", "4", "--format", "lvgl",
        "--no-compress", "--no-prefilter", "--lv-include", "lvgl.h", "--symbols", symbols,
        "--output", str(output)
    ], check=True)

run(14, "".join(dict.fromkeys(compact)), out / "lv_font_noto_sc_14.c")
run(18, ascii_punct + "".join(chars), out / "lv_font_noto_sc_18.c")
run(24, "".join(dict.fromkeys(fixed)), out / "lv_font_noto_sc_24.c")
print("generated:", out / "lv_font_noto_sc_14.c", out / "lv_font_noto_sc_18.c", out / "lv_font_noto_sc_24.c")
