# STM32F103采集节点

目标芯片：STM32F103C8T6，标准外设库，72MHz。

## 默认引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| 光照模块AO | PA0 / ADC1_CH0 | 模块使用3.3V供电 |
| 电位器AO | PA1 / ADC1_CH1 | 输入不得超过3.3V |
| USART1 TX | PA9 | 接USB-TTL模块RXD |
| USART1 RX | PA10 | 接USB-TTL模块TXD |
| DHT11 DATA | PB12 | 建议用带上拉电阻的模块 |
| 蜂鸣器模块IN | PB13 | 默认高电平有效 |
| 板载LED | PC13 | Blue Pill常见低电平有效 |

两块板分别供电，只连接TX、RX、GND，不连接彼此的5V/3.3V。

## 编译

打开 `Project.uvprojx`，选择STM32F103C8，编译并使用ST-Link下载。

若DHT11未连接或读取失败，`App/app_config.h` 默认允许使用ADC构造演示温湿度，便于先调通链路。正式演示接好DHT11后可将 `EG_USE_SYNTHETIC_ON_DHT_FAILURE` 改为0。
