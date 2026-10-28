<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Kids Points Passport

每台设备绑定 `brother` 或 `sister`。首页提供任务、奖品和本机孩子的积分流水。
流水卡片显示上海时间、增减积分、变动后余额和事项，长事项自动滚动。
B1 查看最新页，B2 查看更早页，B3 刷新当前页，长按 B1 返回主页。
每页最多两条。离线或请求失败时保留本次开机已读取的页面；重新开机后重新获取。

无按键操作 30 秒后屏幕变暗，60 秒后深度睡眠。录音、待处理提交及页面请求期间延后休眠。
深睡停止网络连接、音频、电量采样和屏幕，关闭所有软件唤醒源；
B1/B2/B3、消息和定时器均不能唤醒。使用独立硬件开关键关闭并重新上电。
硬件说明中的操作为按住约 2 秒关机、约 0.5 秒开机。
实际恢复手势和待机电流需要上板验收，软件无法关闭功放的物理供电。

默认 `CONFIG_KIDS_ENABLE_GAMES=n`。游戏代码、美术和保持连接的待机实现保留在仓库中，
不进入该构建。启用游戏及完整字库时，在 `menuconfig` 中选择以下配置
（或写入全新构建使用的 defaults）：

```text
CONFIG_KIDS_ENABLE_GAMES=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.games.csv"
```

[游戏分区表](partitions.games.csv) 提供 3.25 MiB 的 factory 分区，
所有数据与 recovery 分区偏移保持一致。积分固件使用 `partitions.csv`。
游戏界面使用保持连接的待机模式。
中文正文支持 GB2312 全部 6763 个汉字，详见[字体约定](main/fonts/README.zh_CN.md)。

## 流水服务

配套 Kids Points 集成须包含 MQTT 流水处理器。它复用集成的 `ledger_page` 查询，不修改余额。
请求主题为 `kids_points/ledger/<child>/request`，携带唯一的 `request_id`、
`before_id`（`0` 表示最新页）和 `limit`（`1` 或 `2`）。
响应主题为 `kids_points/ledger/<child>/response`，QoS 1，不保留消息。

版本 1 响应回显 `request_id` 和 `child_id`，提供 `ok`、`items`、
`has_more`、`next_cursor`。记录包含 `id`、`created_at_ms`、
`points_delta`、`balance_snapshot`、`summary`。错误提供 `error_code`。
分页按 ID 倒序，仅查询主题指定孩子的数据。设备仅接受本机孩子、当前请求的响应。
MQTT 完整报文不超过 1024 字节；事项最多 72 个 Unicode 字符，并可进一步缩短以满足大小限制。
集成沿用现有局域网 MQTT 访问边界，孩子主题是路由键，不是身份认证机制。

## 验证

使用 ESP-IDF 5.5.3 运行 `./tools/validate.sh`。编译、主机检查和设备验收分别报告。
设备须检查兄妹两个版本、流水滚动与字形、功能键和消息不能唤醒、开关键恢复及整板电流。
烧录须获得授权；合并镜像可能覆盖 NVS，分区兼容时可通过分段烧录保留。
