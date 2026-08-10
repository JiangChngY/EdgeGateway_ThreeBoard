# 项目状态

## 已在电脑环境验证

- Python参考协议的Modbus CRC16已通过标准向量 `123456789 -> 0x4B37`。
- Sensor负载编码/解码和完整帧往返。
- 串口流分片、前导噪声重同步、坏CRC丢弃及下一帧恢复。
- NDJSON TCP请求与同序号ACK。
- F103 Keil工程包含STM32F103C8所需的标准外设库、启动文件、工程分组及 `STM32F10X_MD` 设备宏。
- 一键测试共21项，额外覆盖必需文件、Keil/qmake源码引用、Python语法、INI解析、Linux脚本换行符和第二轮代码审查回归项。
- 第二轮审查提出的DHT11可读性、阈值同步、报警复位、MQTT进程、TCP限长、valid_flags、ACK、CORS、构帧错误码和配置保护均已处理；详见 `docs/09_代码审查修复记录.md`。

## 需要实板验证

- Keil实际编译、ST-Link烧录和DHT11时序。
- MP157交叉SDK中的Qt SerialPort、Sql和Network模块。
- MP157 7寸屏Qt平台插件（linuxfb或eglfs）。
- i.MX6ULL交叉SDK中的Qt Sql/Network模块与SQLite插件。
- USB-TTL在MP157上的实际设备名。
- 双板固定IP、systemd自启动、断网补传和HTTP状态页。

上板结果统一写入 `docs/上板验证记录.md`。
