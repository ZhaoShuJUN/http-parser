# MyOverFlow v5：在 RK3399 适配版 v4 上完善的 Qt 工程

本版以用户上传的 **MyOverFlow_RK3399适配版_v4.zip** 为基础修改，主界面参考 **hmi.rar**，MQTT 协议及编号规则参考 **GC6511_4G_Gateway_libmosquitto_Complete_Source.zip**。

**保留原工程目录、入口及核心类**：`MyOverFlow.pro`、`AppFunc`、`CommObj`、`UserIntf`、`ComnUtils`、`ConfigInfo`；继续使用 `QtSingleApplication → CCoreLogic → QCommObj → QObject1 → QextSerialComm`，以及 `QAppCfgInfo` 和原 `configInfo.ini`。没有再建立一套顶层 `src/` 应用或改用 JSON 作为产品配置。

## 编译和启动

目标为 Linux / RK3399，Qt **5.12～5.15**、C++17、libmosquitto **1.6+**（实际验证 2.0.21）、FFmpeg **5+**。Qt6 尚未适配；不能把电脑的 x86 库复制到 ARM 目标板。

Debian/Ubuntu 开发机安装依赖：

```sh
sudo apt-get update
sudo apt-get install build-essential qtbase5-dev libqt5serialport5-dev \
  qtmultimedia5-dev libqt5sql5-sqlite libqt5multimedia5-plugins \
  libmosquitto-dev ffmpeg gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
./scripts/build.sh
./scripts/run.sh --demo
```

也可以在 Qt Creator 打开原 `MyOverFlow.pro`，选择匹配的 Qt 5 Kit。模拟模式显示显著标记，使用独立 `demo-alarms.sqlite`，不访问串口、MQTT、摄像头及 GB 服务器。

真实运行先通过“设备 / 系统设置”配置，校验保存后重启，然后执行 `./scripts/run.sh`。也可直接编辑原 INI：

```sh
./build/MyOverFlow --config /path/to/configInfo.ini --check-config
./build/MyOverFlow --config /path/to/configInfo.ini
```

默认五个测点是温度、湿度、液位、SF6、烟雾浓度，0x03、零基寄存器 0、从站地址 1～5。**倍率、单位、数据类型和报警阈值尚未得到厂家表格，默认按原始寄存器值演示，并标记“未标定”，不能当作现场测量标定或报警标准。**

## 功能与原目录对应

| 原层 | 本版完善内容 |
|---|---|
| AppFunc | 原 `CCoreLogic` 周期处理采样；原 `QObject1CommIntf` 统一有锁缓存、报警状态、遥测/遥信数组同步；增加 SQLite 报警存储 |
| CommObj | 原 `QCommObj` 管理八路；原 `QObject1` 在每路工作线程读 Modbus；原 `QMainModbusRtu` 修正主站 CRC、长度、索引及数据解码；新增 libmosquitto 网关与视频通信模块 |
| UserIntf | 原 `MainWindow`、`DialogConfigDev`、`.ui` 保留并扩展；HMI 深绿色分类按钮、列表、视频、云台、录像回放、报警查询和设置 |
| ComnUtils | 保留 v4 源码版 `QtSingleApplication` 和 `QextSerialComm`，补齐同步轮询接口、资源断开处理，避免跨线程裸指针接收 |
| ConfigInfo | 原 `QAppCfgInfo`、原 INI 键继续使用；新增传感器阈值/格式、摄像头、MQTT、录像及 GB 配置；校验及原子保存 |

界面和 MQTT 读取同一内存快照，不再通过外部 HMI API 重新获取已采集数值；报警状态变化写 SQLite，同时驱动预置位联动。所有启用摄像头可持续录像，界面仅预览选择的一路。

## 可选 GStreamer / MPP 预览

从 `hmi.rar` 保留并适配的 `GstMppRtspPlayer` 位于原 `UserIntf/Video` 层。默认构建用 Qt Multimedia；需要该路径时：

```sh
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
./scripts/build.sh CONFIG+=gst_mpp
```

此选项使用 GStreamer decodebin；板端是否选择 `mppvideodec` 取决于厂商插件、驱动与插件优先级，不代表 x86 编译测试通过了 RK3399 硬解性能验收。

## 验证与交付

```sh
./scripts/test.sh
python3 tests/integration.py build/MyOverFlow --stall-one-bus
python3 tests/integration.py build/MyOverFlow --qos 0 --stall-one-bus
```

集成测试使用本机八路伪终端、HTTP/MQTT/RTSP/SIP 测试服务和真实 libmosquitto/FFmpeg；媒体是 160×120 H.264 夹具，不是实物摄像头或八路高清性能测试。`tests/fixture-settings.json` 仅为测试脚本输入，脚本最终生成临时 INI，产品不读取该 JSON。

- [原架构与逐文件修改说明](docs/原架构与修改说明.md)
- [配置、部署与协议范围](docs/配置与部署.md)
- [验证记录](docs/验证记录.md)

**当前可在已验证的 Linux 开发机编译和运行；RK3399 上板、真实传感器标定、海康 DS-2SC3Q140LY-TE、正式云平台和国网视频服务器仍需实机验收。GB28181 当前覆盖 2016 UDP 实时接入流程，不包含 SIP/TCP、RTP/TCP、2022/国密、国网私有扩展或 GB 历史回放，不能声称已经通过国网认证。**

本版未自动上传公开仓库。含业务代码的工程应交付到用户指定的私有仓库，并由用户控制访问权限。
