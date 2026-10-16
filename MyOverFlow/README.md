# MyOverFlow RK3399 环境监控 Qt 工程

基于上传的 MyOverFlow 框架整理，使用 Qt 5 Widgets、SerialPort、Network、Sql、Multimedia；采用纯源码构建，移除了产品构建对 Windows 专用串口 DLL/静态库的依赖。原基础源码保留在 `legacy/`，上传的 4G 网关参考源码保留在 `reference/gateway-source/`，均不参与产品编译。

**当前交付是已在 Linux x86_64 编译和本地模拟设备联调的源代码工程，尚未在 RK3399、DS-2SC3Q140LY-TE 或国网正式视频平台验收。GB28181 实现为 2016 UDP 实时点播子集，不等同于国网平台全规范适配或认证。**

## 快速编译与演示

Debian/Ubuntu 开发机依赖（板端包名可能不同）：

```sh
sudo apt-get update
sudo apt-get install build-essential qtbase5-dev libqt5serialport5-dev \
  qtmultimedia5-dev libqt5sql5-sqlite libqt5multimedia5-plugins \
  ffmpeg gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-libav
./scripts/build.sh
./scripts/run.sh --demo
```

也可在 Qt Creator 中打开 `MyOverFlow.pro`，选择 Qt 5.12+ / C++17 Kit 并构建。Qt 6 API 尚未适配。真实设备运行前修改 `config/monitor.json`，然后 `./scripts/run.sh`。模拟模式使用独立 `demo-alarms.sqlite`，不访问串口、MQTT、摄像头及 GB 服务器。

本项目没有新增对 libmosquitto 的链接依赖：通过 Qt Network 实现 MQTT 3.1.1 的客户端发布流程，沿用 4G 示例的 HTTP 鉴权和 MQTT 主题/外层 JSON 结构；Qt 端实现仅支持工程需要的发布、QoS0/1 和心跳。参考工程中的 libmosquitto 版本原样保留供后续替换。

## 已实现

- 最多 8 条串口总线，异步轮询、CRC16、响应分包、异常帧校验、超时重试及串口断线重开。默认五类传感器使用 `0x03`、零基寄存器 `0`，从站地址通过界面设置；每路可挂多个设备。
- `u16/s16/u32/s32/f32`、32 位字节序、倍率及偏移配置。最新数值、质量、时间与报警状态在内存维护，界面和上报共用缓存。
- 上下限、回差、通信异常报警、恢复事件；SQLite WAL 持久化；查询与 CSV 导出；临时数据库锁冲突使用有界内存队列重试。
- HTTP 网关鉴权与 Broker 发现，子设备拓扑注册后上报数据，QoS1 PUBACK、自动重鉴权与重连。QoS0 可配置。密码使用环境变量名称引用。
- 最多 8 个摄像头配置；按选择预览一路；海康 ISAPI 云台、缩放、保存/调用预置位；传感器报警联动配置的摄像头预置位。
- 所有启用摄像头独立 RTSP/TCP 拉流、FFmpeg 码流直存、MKV 分段及重连；按配额与磁盘剩余空间删除本程序旧分段，默认要求独立挂载硬盘。
- GB28181 SIP/UDP REGISTER、MD5 Digest（含 qop=auth）、心跳、Catalog/DeviceInfo/DeviceStatus、基础 PTZ DeviceControl、INVITE/ACK/BYE；H.264 Annex B 转 PS，含系统头、PSM、PES/PTS、MPEG CRC，封装 RTP/UDP 发送。

## 操作

“传感器设置”可修改访问地址、寄存器、数据格式、倍率、阈值及摄像头联动。“系统配置”提供完整 JSON 编辑及校验保存；保存后重启生效。新增传感器或修改串口/摄像头/上报参数通过完整配置编辑完成。

**示例中的五类传感器倍率、类型、单位和阈值不是设备标定数据，必须按实际手册及现场规范修改。** 修改访问地址不会通过厂商私有寄存器重设传感器自身的硬件地址。

## 验证与部署

```sh
./scripts/test.sh
python3 tests/integration.py build/MyOverFlow
# 可选：编译视频解码探针，与本地 RTSP 录像联测
mkdir -p build-media
cd build-media
qmake ../tests/media.pro
make -j2
cd ..
python3 tests/integration.py build/MyOverFlow --media-binary build-media/media_smoke
```

集成测试需要 Python3、FFmpeg（含 libx264）、FFprobe、伪终端和本地 TCP/UDP 权限。测试中的 RTSP 服务是本机夹具，视频为生成的 160×120 H.264，**不代表海康实机或 8 路高清负载验收**。

部署、参数说明、协议边界和验收表见 [docs/部署与协议.md](docs/部署与协议.md)，当前验证记录见 [docs/验证结果.md](docs/验证结果.md)。
