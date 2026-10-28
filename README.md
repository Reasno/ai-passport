<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Kids Points Passport

Each device is bound to `brother` or `sister`. Home opens tasks, rewards and
that child's points ledger. Ledger cards show Shanghai time, points added or
deducted, the resulting balance and the entry summary. Long summaries scroll.
B1 loads the newest page, B2 loads older entries, B3 refreshes the current
page, and holding B1 returns home. Each page contains up to two entries.
An offline or failed request preserves the last page read during this boot.
The ledger is fetched again after a power cycle.

The screen dims after 30 seconds without input and enters deep sleep after
60 seconds. Recording, pending submissions and active page requests defer
sleep. Deep sleep stops network connections, audio, battery polling and the
display. All software wake sources are disabled: B1/B2/B3, messages and timers
cannot wake it. Use the dedicated hardware power button to power off/on.
The board's documented power actions are about two seconds to power off and
0.5 seconds to start. The exact recovery gesture and standby current require
device acceptance; software cannot switch the amplifier's physical supply.

`CONFIG_KIDS_ENABLE_GAMES=n` is the default. Game code, artwork and the connected
standby implementation are retained in the repository and excluded from this
build. To enable games with the full font set, select these settings in
`menuconfig` (or in defaults for a fresh build):

```text
CONFIG_KIDS_ENABLE_GAMES=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.games.csv"
```

The [game partition table](partitions.games.csv) provides a 3.25 MiB factory
partition and retains all data and recovery partition offsets. The points
firmware uses `partitions.csv`. The game UI includes connected standby.
Chinese body text supports all 6,763 GB2312 Hanzi; see the
[font contract](main/fonts/README.md).

## Ledger service

The companion Kids Points integration must include the MQTT ledger handler.
It projects the integration's `ledger_page` query without changing balances.
Requests use `kids_points/ledger/<child>/request` with a unique `request_id`,
`before_id` (`0` for newest) and `limit` (`1` or `2`). Responses use
`kids_points/ledger/<child>/response`, QoS 1, without retention.

Version 1 responses echo `request_id` and `child_id` with `ok`, `items`,
`has_more` and `next_cursor`. Entries contain `id`, `created_at_ms`,
`points_delta`, `balance_snapshot` and `summary`. Errors contain `error_code`.
Pages are ordered by descending ID and filtered by the child in the topic.
The device accepts only its own child and active request. MQTT packets stay
within 1,024 bytes; summaries are bounded to 72 Unicode characters and may be
shortened further to fit. The integration uses the existing LAN MQTT access
boundary; a child topic is a routing key, not an authentication mechanism.

## Validation

Use ESP-IDF 5.5.3 and `./tools/validate.sh`. Build and host checks are separate
from device acceptance. Check both actor variants, page scrolling and glyphs,
no wake from function buttons/messages, power-button recovery, and board
current. Flash only after authorization; a merged image may overwrite NVS,
while compatible segmented flashing can preserve it.
