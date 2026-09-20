# 本次安装记录（2026-09-06）

## 已完成

- 已实际通过 SSH 登录 `violet_mio@192.168.88.131`；不是仅根据截图判断。
- `ens33` 固定为 `192.168.88.131/24`，默认路由经 `192.168.88.2`；`ens37=192.168.137.2/24`，`ens38=192.168.138.2/24`。
- 修复的根因是上网连接未绑定网卡，曾错误激活到 ens38。现已绑定 ens33 和 MAC `00:0c:29:d1:d6:16`。变更前网络配置备份：`/root/edge-network-backup-fD2PwJ/`。
- 已生成 ARM hard-float / Qt 5.12.9 生产程序，并完成依赖检查与 ARM/QEMU 界面回归。
- 用户已确认 157 暂时未连接；按离线安装处理，未远程重启开发板。

## 生产文件与安装位置

本地交付目录：

项目目录内的 `mp157_desktop/`（本地资料已迁移）。

| 文件 | 位置 / 用途 |
| --- | --- |
| 完整原厂桌面加新 App | 本地 `artifacts/systemui`；不是测试插桩程序 |
| 独立采集程序 | 本地 `artifacts/edge-desktop`；供调试，未另行启动占用屏幕/串口 |
| 虚拟机生产桌面 | `/srv/nfs/edgegateway/mp157/opt/ui/systemui` |
| 桌面配置 | `/srv/nfs/edgegateway/mp157/opt/edge-gateway/config/desktop.ini` |
| 编译环境 | `$HOME/edgegateway_desktop_sdk` |
| 应用源码副本 | `$HOME/edgegateway_desktop` |
| 原厂源码工作副本 | `$HOME/edgegateway_desktop_sdk/systemui-source/systemui` |

安装后重新读取并确认的 SHA-256：

```text
c4d73fd1ad7ea6821468d625d6f7c6cf619f26844031221fd56dc5ffd05ec186  新 systemui
68e825c7bf1872f1ae21aa8b8c17552dd6726488b5394c66d78894a986cec6ed  原 systemui 备份
```

备份的确切路径：

`/srv/nfs/edgegateway/mp157/opt/ui/systemui.before-edgegateway.20260906-200519.ynIa3x`

仍沿用原厂 `systemui.service`，启动命令 `/opt/ui/systemui -platform linuxfb`，环境文件 `/etc/default/atk-qtenv`。未修改原桌面资源、Qt 运行库、板端 libc、U-Boot 或 DTB。设备树只有一处说明性注释纠正，不改变编译语义。

## 下次接板

1. 开启这台 Ubuntu 虚拟机，保持开发板网卡桥接到实际接网线的物理网口。
2. 157 接电源、网线和需要时的 USB-TTL 调试线；使用原来已经调通的 TFTP 内核/DTB + NFS 根启动配置，NFS 根仍为 `/srv/nfs/edgegateway/mp157`。
3. 桌面第一页应出现「边缘采集」，点击进入。没有 F103 数据时显示「等待设备」和 `--` 是正常状态。
4. F103 USART2 通过 3.3V USB-TTL 接 MP157 USB HOST；默认 `/dev/ttyUSB0`、115200、8N1。如端口不同，在「连接设置」中选择。
5. 确认真实触摸点按、DHT11/NTC/距离变化、暂停/重连以及返回桌面后继续采集。

**直接从原 eMMC 根文件系统启动，不会自动获得这次 NFS 目录中的新图标。** 本次未刷写 eMMC，也不需要为了这次应用更新重刷 U-Boot。不要把 `systemui` 当成内核镜像用 TFTP/bootm 启动。

## 回退

如果接板后新桌面异常，先关闭 157，确保不再使用该 NFS 根。在 Ubuntu 虚拟机终端执行以下确切文件操作，恢复原桌面：

```bash
sudo cp -p /srv/nfs/edgegateway/mp157/opt/ui/systemui.before-edgegateway.20260906-200519.ynIa3x /srv/nfs/edgegateway/mp157/opt/ui/systemui.restore-new
sudo mv /srv/nfs/edgegateway/mp157/opt/ui/systemui.restore-new /srv/nfs/edgegateway/mp157/opt/ui/systemui
sha256sum /srv/nfs/edgegateway/mp157/opt/ui/systemui
```

应恢复为上表中的原程序哈希。备份文件和采集配置/历史数据均保留。

## 已做的软件检查

- 独立 Qt 程序：CRC 标准向量、真实 PTY 接收、分片/噪声/坏 CRC 恢复、无效字段、旧 C8/新 ZE 区分、SQLite 保存。
- x86 Qt 5.15 UI 回归：切页点击、测试数值渲染、异步历史、串口候选不被状态刷新重置、离线后隐藏旧值。
- ARM Qt 5.12.9 + 目标运行库 + QEMU/Xvfb：独立 UI 回归和完整原厂桌面图标/打开/返回/重进。
- 安装前再次针对原始 NFS 根检查 ELF32 ARM hard-float、解释器、集成身份和递归版本依赖；安装后校验新文件与备份哈希。

这些软件检查不能代替真实 157 上的 linuxfb、触摸、物理串口、音视频和传感器验收。当前新 App 是本地采集/历史/连接层，不声称已经打通新的 F103ZE 数据到旧 i.MX6ULL 汇聚协议。
