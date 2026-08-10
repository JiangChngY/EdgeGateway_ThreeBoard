# i.MX6ULL编译与部署

## 编译

使用阿尔法板Qt 5.12.9对应SDK：

```sh
cd deploy
chmod +x *.sh
./build_imx6ull.sh /opt/fsl-imx-xwayland/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
```

SDK路径以你实际安装结果为准。

## 部署

```sh
deploy/install_imx6ull.sh /path/to/edge-aggregator
vi /opt/edge-gateway/config/imx6ull.ini
systemctl start edge-aggregator
journalctl -u edge-aggregator -f
```

确认监听：

```sh
netstat -lntp | grep -E '9000|8080'
```

电脑浏览器打开：

```text
http://192.168.10.2:8080/
http://192.168.10.2:8080/api/status
http://192.168.10.2:8080/api/latest
```

## MQTT选做

安装或复制 `mosquitto_pub` 后，在 `imx6ull.ini` 中设置：

```ini
[mqtt]
enabled=true
program=/usr/bin/mosquitto_pub
host=192.168.10.10
port=1883
topic=edge/gateway/data
```

第一版不启用MQTT也不影响TCP、SQLite和HTTP演示。

启用后程序以 `mosquitto_pub -l -q 1` 常驻进程方式转发，不会每条数据都fork新进程。MQTT进程异常会写入systemd日志并尝试重启；SQLite中的原始汇聚数据不受MQTT状态影响。

重复运行安装脚本默认保留 `/opt/edge-gateway/config/imx6ull.ini`。需要强制恢复默认配置时使用：

```sh
deploy/install_imx6ull.sh --force-config /path/to/edge-aggregator
```
