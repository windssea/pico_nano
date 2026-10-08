# 设备上传存储适配

`main/device_upload_storage.c` 为已有上传文件port提供ESP-IDF v6.1 FAT卷同步与实时空间回调，已纳入固件构建。后续已连接设备热点服务与页面，见 [设备入口](DEVICE_TRANSFER_ENTRY.md)；代码装配不能代替无线实测。

## 接入契约

使用 `pn_device_upload_storage_options(context, media, "/sdcard", options)` 配置 `pn_upload_files_open`。media必须是阅读、TF字体、USB交接共用的同一个介质服务；context、options引用的根及media在port关闭前保持稳定。需要覆盖时由上层在生成options后设置明确的旧文件长度与摘要。

初始化只检查已有挂载，不取得写租约。实际空间与同步回调必须处在单一有效WRITE租约中，存在READ、USB、多个租约或旧代时拒绝。所有调用由同一owner串行执行，不提供跨线程锁替代owner。卡失效后先停止上传/阅读消费者、关闭句柄，再由已有设备流程处理remount；适配本身不挂载、卸载或格式化。

ESP分支只允许固定 `/sdcard` 根。主机测试可用可信临时根，设备状态与空间接口由显式stub提供。`space_free` 每次调用 `esp_vfs_fat_info`，不使用BSP缓存容量；调用前后检查介质代次、WRITE与卡状态。失败不输出容量。

## 正向同步

FatFs `f_sync` 对未修改的文件可能跳过 `sync_fs`。因此适配在上传私有目录 `.readpico/volume.sync` 写入单字节，再执行 `fsync` 和 `close`；实际写入使FAT文件标记修改，进入卷元数据及驱动同步路径。不是只读打开目录，也不调用会卸载卡的 `read_pico_sd_sync`。

文件port已先创建并检查 `.readpico`。同步文件仅限常规文件、长度0或1，未知类型或更大文件报告损坏，不能截断成合法标记。每次实际写入，包括计数回绕时；该字节不是恢复日志或事务身份。目录参数只允许根或根下无点段的路径，且须存在。主机侧使用lstat及可用时的O_NOFOLLOW拒绝链接，FAT没有符号链接。

任何write、fsync、close或事后卡状态失败都返回错误；即使数据可以读回，也不作为成功同步。上传事务按既有规则阻止不确定状态继续ACK，重开后执行持久恢复。行为见 [上传事务](UPLOAD_TRANSACTION.md) 和 [文件安装](UPLOAD_FILES.md)。

本版SDK的SDMMC diskio在CTRL_SYNC直接返回RES_OK，没有额外卡内缓存刷新指令；屏障确认的是FAT缓存写出及SDK返回成功。FAT的sync_fs另有FSInfo写入未检查返回值的上游路径，因此不能把它称为所有元数据写入均获得独立错误确认。

FAT命名空间及SD卡控制器的断电行为仍需真机验证。软件同步屏障不证明FAT更名中途断电不会形成交叉链，也不证明卡内缓存和掉电时间满足目标。不得以PC的POSIX恢复测试替代这一硬件门。

## 验证边界

`python tools/dev.py host` 的 `device_upload_storage` 使用真实POSIX文件及完整TXT上传事务，设备挂载/容量明确stub；验证无WRITE和READ占用拒绝、实时空间/失败不输出、同步失败不读回放行、I/O期间卡失效、坏同步文件拒绝、上传成功以及换代拒绝旧配置。测试用linker包装fsync模拟“已写但同步返回错误”。

ESP-IDF的CI及板级配置编译使用真实BSP和FAT头文件。编译不等于卡、无线或掉电测试通过。设备无线owner接入后还须验证停止屏障、拔卡、重入、阅读位置返回、TF字体释放及真实上传速率。
