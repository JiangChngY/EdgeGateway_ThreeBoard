# EdgeGateway F103ZE 采集端

本工程面向正点原子精英板 V2.6（STM32F103ZET6），完成 DHT11、NTC 模块和超声波模块的周期采集，并通过两个串口分别输出调试信息和网关数据帧。

## 1. 接线

| 模块 | 模块引脚 | STM32 引脚 | 说明 |
|---|---|---|---|
| NTC | AO | PA5 / ADC1_IN5 | 模拟量，必须小于等于 3.3 V |
| NTC | DO | PA6 | 比较器数字阈值输出 |
| 超声波 | TRIG | PD11 | 10 us 触发脉冲 |
| 超声波 | ECHO | PD12 / TIM4_CH1 | 完全重映射，输入捕获 |
| DHT11 | DATA | PD13 | 开漏单总线 |
| 调试串口 | TX/RX | PA9/PA10 | USART1，115200-8-N-1 |
| MP157 串口 | TX/RX | PA2/PA3 | USART2，115200-8-N-1 |

所有模块与开发板必须共地。建议 NTC 模块使用 3.3 V 供电。DHT11 模块使用 3.3 V，裸传感器 DATA 需用约 4.7 kΩ 电阻上拉到 3.3 V。

常见 HC-SR04 使用 5 V 供电且 ECHO 可能输出 5 V。即使所选 MCU 引脚具备 5 V 容忍能力，也建议在 ECHO 与 PD12 之间使用电阻分压（例如上臂 10 kΩ、下臂 20 kΩ），把高电平降到约 3.3 V。TRIG 的 3.3 V 高电平通常可直接识别。

## 2. 软件结构

- `App/eg_time.*`：TIM6 以 1 MHz 自由运行，提供微秒计时。
- `App/eg_dht11.*`：测量每一个数据位的高电平宽度，含超时、校验和与范围检查。
- `App/eg_ntc.*`：ADC 校准、16 次过采样、毫伏换算和 PA6 阈值读取。
- `App/eg_ultrasonic.*`：TIM4_CH1 上升沿/下降沿中断捕获，40 ms 超时保护。
- `App/eg_protocol.*`：小端二进制帧和 CRC16-Modbus。
- `App/edge_gateway_app.*`：无阻塞周期调度、有效标志维护和双串口发送。

调度周期如下：

| 任务 | 周期 |
|---|---:|
| NTC ADC/DO | 200 ms |
| 超声波启动 | 250 ms |
| DHT11 | 2000 ms |
| USART1/USART2 发布 | 1000 ms |

DHT11 单次读取会占用约 20 ms；超声波回波测量由 TIM4 中断完成，主循环不会等待回波。传感器失败时仅清除相应 `valid_flags`，不会生成替代数据。

## 3. USART2 协议

帧格式：

```text
AA 55 | Version | Type | SeqLE(2) | LengthLE(2) | Payload | CRC16LE(2)
```

- Version：`0x01`
- Type：传感器数据为 `0x01`
- CRC：从 Version 到 Payload 末尾计算 CRC16-Modbus
- 传感器 Payload 固定 12 字节

| 偏移 | 长度 | 字段 | 单位 |
|---:|---:|---|---|
| 0 | 2 | `temperature_centi_c`，有符号小端 | 0.01 ℃ |
| 2 | 2 | `humidity_centi_percent` | 0.01 %RH |
| 4 | 2 | `ntc_adc_raw` | ADC 码值 0~4095 |
| 6 | 2 | `ntc_millivolts` | mV |
| 8 | 2 | `distance_mm` | mm |
| 10 | 1 | `valid_flags` | 见下表 |
| 11 | 1 | `ntc_digital_level` | 0/1 |

`valid_flags`：bit0 DHT11、bit1 NTC 模拟量、bit2 超声波、bit3 NTC 数字量。MP157 必须先检查标志位，再使用相应字段。

NTC 的 AO 与温度不是统一线性关系，还取决于模块中的 NTC 阻值、B 值、分压电阻方向和个体误差。因此当前固件上报原始值与毫伏值，DHT11 提供绝对温度；获得模块参数并做冰水/室温/热水三点标定后，再在上位机或固件中换算 NTC 温度。

## 4. 编译与下载

1. 用 Keil MDK 打开 `EdgeGateway_F103ZE.uvprojx`。
2. 选择 `EdgeGateway_F103ZE` Target，执行 Build。
3. 用 ST-Link/DAP 连接 SWDIO、SWCLK、GND 和 3.3 V 参考电压。
4. 在 Keil 的 Debug/Utilities 中选择对应下载器，执行 Download。
5. 串口工具打开 USART1 对应串口，设置 115200、8 数据位、无校验、1 停止位。

生成文件位于 `EdgeGateway_F103ZE/`：

- `EdgeGateway_F103ZE.hex`：可直接下载的 HEX。
- `EdgeGateway_F103ZE.axf`：Keil 调试文件。

本工程已使用本机 ARM Compiler 5.06 update 5 完整编译：`0 Error(s), 0 Warning(s)`。

## 5. 首次上电检查

USART1 每秒输出一行，例如：

```text
[EG] seq=12 DHT=25.00C/61.00% NTC=2048,1650mV,DO=1 US=356mm flags=0x0F err=0/0/0
```

如果字段前出现 `INVALID,`，检查对应接线和供电；末尾三个错误计数依次为 DHT11、NTC、超声波错误累计值。超声波一直无效时，优先检查 ECHO 分压、是否接到了 PD12，以及探头前方是否在约 2 cm～4.5 m 的有效范围内。

不要在 CubeMX 中重新生成代码前删除 `main.c` 的 USER CODE 内容。应用文件位于 `MDK-ARM/App`，重新生成后还应确认 TIM6 Prescaler 保持为 71、Keil Include Path 仍含 `App`、项目组仍含六个应用源文件。
