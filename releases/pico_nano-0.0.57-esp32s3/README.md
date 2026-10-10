# PicoNano 0.0.57 开发预览固件（ESP32-S3，RDP-G01-W / Read Pico）

源码提交：dbae723（分支 codex/reader-foundation），ESP-IDF v6.1，板级配置 sdkconfig.defaults（120 MHz 像素时钟依赖 Zbit 闪存）。

> **未经真机验收。** 本固件只在主机测试、PC 模拟器与两套 ESP-IDF 构建中验证过，尚未在样机上启动、显示、触摸、刷新或功耗测试。请准备好原厂固件以便回刷，风险自担。

## 文件

| 文件 | 写入地址 | 说明 |
| --- | --- | --- |
| bootloader.bin | 0x0 | 二级引导 |
| partition-table.bin | 0x8000 | 分区表（nvs 0x9000、otadata 0xF000、ota_0 0x20000 …） |
| ota_data_initial.bin | 0xF000 | OTA 选择复位到 ota_0 |
| pico_nano.bin | 0x20000 | 应用固件 |
| first-install/factory-data.bin | 0xC20000 | **仅新板首次**：内部 data 分区空 LittleFS（会清除阅读进度、书签、设置） |
| first-install/factory-wallpaper.bin | 0xF00000 | **仅首次启用锁屏壁纸**：wallpaper 分区空镜像（会清除已应用壁纸） |

## 烧录（不擦除整片）

```
esptool --chip esp32s3 -p COM? -b 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 pico_nano.bin
```

（旧版 esptool.py 命令名为 `write_flash`。）把 `COM?` 换成设备端口。

- **不要执行 erase-flash，也不要把多个文件合并成从 0x0 起的整片镜像**：那样会抹掉 NVS（0x9000）和内部数据分区。本固件启动时不会格式化或擦除 NVS/内部存储。
- 面板 VCOM 只从 PMU 读取；读不到有效 VCOM 时固件不推屏（屏幕保持原样），这是保护行为而不是故障。
- 新板第一次刷入时内部 data 分区若是空白，阅读进度与设置无法保存，界面会提示；此时可单独写入 `first-install/factory-data.bin` 到 0xC20000。已有数据的设备不要写，会丢失进度与书签。

## 本版内容（摘要）

Mono Glass V2 界面：书架 / 传书 / 设置三个一级页、拟玻璃卡片与状态带（时间、电量）；书名搜索、网格/列表、收藏与书籍操作（长按或“⋯”）、确认删除；阅读工具栏、按百分比跳转（TXT/EPUB）、排版设置、字体选择直达；按键与手势、刷新策略（清晰/均衡/省电）与 GL16 局部刷新、存储与关于；三键焦点导航。完整列表见仓库 docs/CHANGELOG.md 的 0.0.57 各节。

## 校验

见 SHA256SUMS。
