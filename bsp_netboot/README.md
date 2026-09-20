# STM32MP157 / i.MX6ULL TFTP + NFS 网络启动包

这个目录包含两块正点原子 Linux 开发板的 U-Boot 网络启动环境、设备树修改、兼容新版 Ubuntu 编译器的源码覆盖文件和可重复构建脚本。`artifacts/` 是本地生成镜像目录，不随 Git 源码上传；厂商源码和根文件系统需另行准备。

网络启动的数据路径是：

```text
U-Boot --TFTP--> Linux内核 + DTB
Linux  --NFS----> 根文件系统
```

TFTP只负责下载内核和设备树；根文件系统通过NFS挂载。这样修改应用或根文件系统时无需反复烧写Flash。

## 已生成镜像

```text
artifacts/
├── mp157/
│   ├── uImage
│   ├── stm32mp157d-atk-edgegateway.dtb
│   ├── u-boot-stm32mp157d-atk-edgegateway.stm32
│   ├── kernel.config
│   └── kernel-modules.tar.gz
└── imx6ull/
    ├── emmc/    zImage、DTB、U-Boot、kernel.config、内核模块
    └── nand/    zImage、DTB、U-Boot、kernel.config、内核模块
```

`SHA256SUMS`用于检查传输后文件是否完整。i.MX6ULL同时生成eMMC和NAND版本，使用时必须选择与开发板存储介质相符的一组。

## 源码版本

- STM32MP157：正点原子 U-Boot 2020.01、Linux 5.4.31。
- i.MX6ULL：正点原子 U-Boot `rel_imx_4.1.15_2.1.1_ga`、Linux 4.1.15。
- 交叉编译器：Ubuntu的 `arm-linux-gnueabihf-` 与 `arm-linux-gnueabi-` 工具链。

`overrides/`保留了设备树、U-Boot环境和老版本BSP在Ubuntu 24.04新版编译器下需要的兼容修改。构建脚本会把这些文件覆盖到对应厂商源码树后再编译。

## 一键重新编译

约定源码目录：

```text
~/edgegateway_bsp/src/mp157-uboot
~/edgegateway_bsp/src/mp157-linux
~/edgegateway_bsp/src/imx6ull-uboot
~/edgegateway_bsp/src/imx6ull-linux
```

执行：

```bash
bash bsp_netboot/scripts/build_mp157.sh ~/edgegateway_bsp
bash bsp_netboot/scripts/build_imx6ull.sh ~/edgegateway_bsp
```

i.MX6ULL的厂商Linux 4.1.15源码树对 `O=` 构建支持不完整，因此脚本会在它自己的源码目录中依次清理并构建eMMC、NAND版本；每个版本完成后立即保存镜像。

## 配置TFTP/NFS服务器

先把两份厂商根文件系统放到：

```text
~/edgegateway_bsp/archives/mp157-qt5.12.9-rootfs.tar.bz2
~/edgegateway_bsp/archives/imx6ull-factory-rootfs.tar.bz2
```

然后执行：

```bash
bash bsp_netboot/scripts/prepare_server.sh ~/edgegateway_bsp
bash bsp_netboot/scripts/verify_server.sh 127.0.0.1
```

`prepare_server.sh`默认给i.MX6ULL根文件系统安装eMMC版内核模块。若开发板是NAND版，改为：

```bash
IMX_VARIANT=nand bash bsp_netboot/scripts/prepare_server.sh ~/edgegateway_bsp
```

服务器目录为：

- TFTP：`/srv/tftp`
- MP157 NFS根目录：`/srv/nfs/edgegateway/mp157`
- i.MX6ULL NFS根目录：`/srv/nfs/edgegateway/imx6ull`

开发板接线、U-Boot命令、VMware网络设置和恢复本地启动方法见 [TFTP/NFS网络启动说明](../docs/11_TFTP_NFS网络启动.md)。
