# MP157 原厂桌面「边缘采集」App

保留正点原子的 Qt Quick `systemui` 桌面、原有应用和触摸手势，在第一页新增「边缘采集」图标。点击后进入实时采集、历史记录、连接设置、汇聚上传四个页面；返回桌面后，串口采集仍由同一个后端实例继续运行。点击「暂停采集」会释放串口。

2026-09-19：补齐 ZE 到 6ULL 的持久化上传、确认重传、字段区分和触摸数字键盘，见 [本次接续说明](../docs/12_ZE三板接续与验收.md)。原 2026-09-06 记录和 `artifacts/` 顶层程序属于上一版；本次产物单独保存于 `artifacts/resume-20260919/`。

2026-09-06：ARM 生产程序已生成，并已备份安装到虚拟机的 MP157 NFS 根目录。具体路径、哈希和回退步骤见 [安装记录](INSTALLATION.md)。`artifacts/` 中的程序、日志及 ARM/QEMU 截图仅在本地保存，不随 Git 源码上传；截图中的传感器数值是测试输入。

## 与设备树的关系

项目中的 MP157 设备树是基于正点原子 `stm32mp157d-atk.dts/.dtsi` 修改的，不是套用另一块 ST 评估板的设备树。文件位于：

`../bsp_netboot/overrides/mp157_linux/arch/arm/boot/dts/`

其中保留 `alientek,lcd-rgb` 显示节点、背光以及 GT9147 触摸配置。本次新增 App 不改变屏幕时序、触摸地址、中断脚、以太网或启动参数，只纠正了一条触摸节点注释。原来能正常显示和触摸的 DTB 可以继续使用，无需因新增图标重新烧写 U-Boot/DTB。

设备树负责把硬件交给驱动；图标、页面和按钮属于应用层。这里按原厂桌面的 QML Activity 机制集成，不依赖不受该桌面支持的 `.desktop` 文件。

## 页面与数据

- 实时采集：空气温度、相对湿度、超声波距离、NTC ADC、NTC 电压、NTC DO。
- 历史记录：SQLite 保存接收到的样本，页面展示最近 100 条。
- 连接设置：扫描串口、115200 连接/暂停、查看本机 IPv4 地址和协议错误累计。
- 汇聚上传：填写汇聚 IP/端口、触摸数字键盘、启用/暂停、待确认数量、上传状态。暂停上传期间继续存储和排队。
- 3.5 秒未收到采集帧视为离线，实时数值显示 `--`；无效传感器字段也显示 `--`。
- NTC 不凭空换算温度；没有模块参数及标定时只显示原始 ADC、电压和 DO 电平。

默认连接是 **F103 USART2 → USB-TTL（3.3V 电平）→ MP157 USB HOST**，默认设备 `/dev/ttyUSB0`，115200、8N1、无流控。共地、TX/RX 交叉；不要将 TTL 信号接到 RS-232 接口。USART1 的电脑调试文字不属于这里使用的二进制协议。

采集解析接受 `AA 55` 协议头、版本 1、类型 1、CRC16/Modbus：

| 采集负载 | 字段及处理 |
| --- | --- |
| 当前 F103ZE，12 字节 | 温度 i16/100、湿度 u16/100、NTC ADC u16、电压 u16 mV、距离 u16 mm、有效位 u8、DO u8；多字节均小端 |
| 旧 F103C8，10 字节 | 温湿度、光照、模拟量、有效位、报警；不会把旧光照错误显示成 NTC 或超声波；合成温湿度会标明来源 |

该目录目前包含本地采集与 TCP 持久化上传。ZE 使用 schema 2，旧 C8 使用 schema 1，汇聚端分别处理字段。旧 `mp157_hmi/` 仍保留；不要同时运行两个程序争抢串口。F103ZE 当前没有完整控制命令处理，因此页面不放无效的阈值、LED、蜂鸣器按钮。

## 存储

配置文件默认 `/opt/edge-gateway/config/desktop.ini`，可以通过环境变量 `EDGE_DESKTOP_CONFIG` 指定其他位置。已有配置不会被安装脚本覆盖。

数据库默认 `/opt/edge-gateway/data/desktop.db`，独立工作线程负责 SQLite 和上传，待写上限 128 条，持久化上传队列上限 100000 条；锁等待 200ms，积压、写入失败和未保存数量会提示。历史与上传条目同事务提交，收到正确确认后才移除上传条目。默认采用 DELETE 日志而非 WAL。

NFS 根启动时，此默认路径仍位于服务器。建议长期运行时将 `storage/database` 改到已正确挂载的板端 eMMC/SD 本地数据分区；不要在虚拟机和板子上同时打开同一数据库进行写入。DELETE 不代表所有 NFS 锁问题都消失。硬挂载 NFS 故障时的系统级 I/O 停顿也无法仅靠应用超时完全解决。已经提交的队列支持应用重启恢复；尚在内存待写区的样本不具备掉电保证。

## 文件

| 文件 | 用途 |
| --- | --- |
| `edgegatewaybackend.h/.cpp` | 串口协议、有效位、在线判断、异步 SQLite、端口/网络信息 |
| `Dashboard.qml` | 四页触摸界面及数字键盘 |
| `Activity.qml` | 原厂桌面 Activity 与返回桌面集成 |
| `integrate_systemui.py` | 给原厂源码副本添加图标、后端和构建项；可重复运行，保留首次原文件备份 |
| `build_systemui.sh` | 分离构建目录、调用指定 Qt 5 qmake 并记录构建日志 |
| `install_nfs.sh` | 对离线 NFS 根目录备份后安装 ARM systemui，保留用户配置 |
| `edge-desktop.pro` / `main.cpp` / `main.qml` | 独立测试入口，不替换原厂桌面 |
| `test_serial.py` | Linux PTY 对真实 Qt 可执行程序的串口/存储集成测试 |
| `test_uplink.py` | 真实 Qt 进程的 PTY/TCP/SQLite/HTTP 链路，重启、拒收、丢确认、去重和旧库迁移 |
| `test_ui.pro` / `test_ui.cpp` | Xvfb 下的切页、数据显示、历史、串口选择及离线测试 |
| `test_vendor_ui.py` / `test_vendor_driver.h` | 在临时原厂源码副本中验证图标打开、返回、重新进入；不编入部署产物 |

## 构建与安装

先解压原厂 `01、程序源码/09、Qt综合例程源码/systemui.tar.gz`，保留 `ui/resource`。在源码副本上操作，避免改动唯一的原厂原件。

```bash
# sdk-env 必须指向与板端 Qt 5.12.9 / ARM hard-float 匹配的环境
bash build_systemui.sh /absolute/vendor/systemui /absolute/build /absolute/sdk-env
file /absolute/build/systemui
```

只运行电脑端 Qt 的 `qmake` 会得到 x86 程序；**编译成功不代表能放到 ARM 板上运行**。还应检查 ELF 解释器、Qt/GLIBC/GLIBCXX 依赖，并验证完整原厂桌面的启动。

确认板子已关机或退出正在使用该 NFS 根的桌面进程，再安装：

```bash
sudo bash install_nfs.sh /srv/nfs/edgegateway/mp157 /absolute/arm-build/systemui
```

脚本要求原厂程序位于 `/opt/ui/systemui`，保留带时间戳备份。已有 `systemui.service` 会照常启动原路径；不需要修改 U-Boot 环境或重烧 eMMC。出现问题时，在桌面进程停止后，用脚本打印出的确切备份路径恢复 `/opt/ui/systemui`，不要用通配符选择备份。

## 电脑端回归测试

以下验证的是程序行为；PTY 数值和截图均为测试输入，不是接板实测记录。

```bash
mkdir -p /tmp/edge-desktop-build
cd /tmp/edge-desktop-build
qmake /absolute/mp157_desktop/edge-desktop.pro
make -j4
python3 /absolute/mp157_desktop/test_serial.py ./edge-desktop

mkdir -p /tmp/edge-ui-build
cd /tmp/edge-ui-build
qmake /absolute/mp157_desktop/test_ui.pro
make -j4
xvfb-run -a env QT_QPA_PLATFORM=xcb ./edge-ui-test /absolute/test-screenshots

python3 /absolute/mp157_desktop/test_vendor_ui.py \
  /absolute/vendor/systemui /absolute/vendor/ui/resource /absolute/vendor-test-output
```

原厂 systemui 源码及资源版权归其作者所有；本目录的集成脚本不移除原版权声明，也不重新声明其许可证。分发完整修改后的桌面时，应同时遵守原厂源码和 Qt 的许可要求。
