<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Kids Points Passport

Each device is bound to `brother` or `sister`. Home opens tasks, rewards and
interactive games. Games use ESP-NOW between the paired devices: Find the
sibling, Rock Paper Scissors, Buzzer, Whac-A-Mole and Racing. Find also rings
over MQTT when both devices are online, and the Find page offers push-to-talk.

## Racing

Two cars start together on a three-lane, 900 m track and the first across the
line wins. B1 and B2 change lane on press; holding B1 leaves the race.
Nitro cans give 1.5× speed for 2 seconds. The first car to reach one takes it
and it disappears on both screens; a car in the same lane that reaches it
within 0.1 seconds also gets the nitro. Speed bumps stay on the
track and halve speed for 2 seconds; every row leaves at least one lane free.
A newer item replaces the current item effect. A car trailing by at least
5 m can press B3 once per race to open DRS: 1.7× speed for 4 seconds that keeps
running after an overtake. DRS and nitro stack to 2.2×, and picking up nitro
during DRS does not shorten the DRS window. A speed bump ends an
open DRS window. Finishing within 0.1 seconds of each other is a draw.
The device that sends the invitation hosts that race: it referees positions,
items, DRS and the result, and both devices generate the same track from the
invitation's seed. If both devices invite at the same moment, exactly one
invitation stands and the other device joins it. Car colors and win/loss
records follow each device's brother or sister identity, whichever device
hosts. Both devices need the same firmware version to race.

## Power

The screen dims after 30 seconds without input. After 60 seconds the device
enters connected standby: the backlight and UI rendering stop, Wi-Fi/ESP-NOW
stay connected, and automatic Light Sleep is allowed. Any of B1/B2/B3 wakes
the screen; that key press only wakes the device. Notifications, Find alerts
and game invitations also wake it.

On Home, hold B3 for five seconds to turn power-saving mode on or off. The
setting is stored on the device and survives restarts. While it is on, the
status-bar battery icon is yellow. In power-saving mode the
screen dims after 30 seconds and the device enters deep sleep after 60 seconds.
Recording, pending submissions, live game rounds, push-to-talk and Find
ringing delay deep sleep. Deep sleep stops network connections, audio, battery
polling and the display. All software wake sources are disabled: B1/B2/B3,
messages and timers cannot wake it. Use the dedicated hardware power button to
power off/on. The board's documented power actions are about two seconds to
power off and 0.5 seconds to start. The exact recovery gesture and standby
current require device acceptance; software cannot switch the amplifier's
physical supply.

## Build profile

The firmware uses the default [partition table](partitions.csv). It is built
for size (`-Os`) and includes only the features the application uses: Wi-Fi
station mode for WPA2/WPA3 personal networks, IPv4, plain `mqtt://` to the LAN
broker, and the LVGL base object, label, image and timer. SoftAP, enterprise
Wi-Fi, IPv6, the DHCP server, MQTT over TLS/WebSocket, the remaining LVGL
widgets and LVGL examples are excluded. Re-enable the matching options in
`sdkconfig.defaults` before using any of them.
Chinese body text supports all 6,763 GB2312 Hanzi; see the
[font contract](main/fonts/README.md).

## Validation

Use ESP-IDF 5.5.3 and `./tools/validate.sh`. Build and host checks are separate
from device acceptance. Check both actor variants, pairing and every game,
racing item ownership, DRS timing and lane-change latency on both devices,
Find over ESP-NOW and MQTT, push-to-talk audio, connected standby wake from
B1/B2/B3 and messages, the Home B3 five-second power-saving toggle and its
persistence, no wake from function buttons/messages in power-saving deep sleep,
power-button recovery, and board current in both modes. Flash only after
authorization; a merged image may overwrite NVS, while compatible segmented
flashing can preserve it.
