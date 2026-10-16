# GC6511 4G 工业物联网网关——libmosquitto 版

本工程在上一版 `GC6511_4G_Gateway_Stable` 源码上，将**手写 MQTT 连接、封包、PINGREQ、重连**改为官方 **libmosquitto 客户端库**，并保留用户上传 `main(1).c` 的通信行为。原始源码在 `original/main_user.c` 中逐字节保留。

## 1. 功能和兼容范围

- **串口协议不变**：默认 `/dev/ttyS1`、115200 8N1；`7E 7E | TYPE(1) | LEN(2,高字节先) | JSON | CRC16-MODBUS(低字节先)`。CRC 计算 TYPE+LEN+JSON。0x01 模拟量、0x02 心跳、0x03 开关量、0x04 配置变更、0x05 配置响应、0x85 配置查询，查询载荷为 `{}`。
- **HTTP 认证不变**：`POST /iot/devices/auth/gateway`，网关号、设备型号、认证参数由 `src/gateway.h` 的宏定义控制。获取 `gatewayId` 及 MQTT 服务地址，若无 MQTT 地址则采用源程序默认认证服务器的 1906 端口。每次重新创建客户端前重新认证。
- **MQTT 平台接口不变**：MQTT 3.1.1、Clean Session、Client ID 为 `gatewayId`，主题 `/v1/devices/{gatewayId}/topo/add` 和 `/v1/devices/{gatewayId}/datas`；设备编号及 JSON 字段与原工程相容，默认 QoS 0，不使用 retain。
- **串口与网络分离**：串口主线程持续采集，网络线程 HTTP 鉴权，libmosquitto 独立 loop 线程执行网络收发和 PINGREQ/PINGRESP。
- **自动恢复**：HTTP 失败 2～60 秒退避重试；MQTT 使用 `mosquitto_reconnect_delay_set(2,60,true)` 自动重连；如果持续离线 5 分钟则销毁客户端重新 HTTP 认证、重新获取 Broker。`on_connect/on_disconnect` 管理连接状态；每次连接成功先发布拓扑，再上传等待队列。
- **单报文在途与回调确认**：`on_publish` 匹配 libmosquitto 生成的 message ID，确认后才弹出 FIFO 队首；同步/提前到达的 QoS 0 回调也能正确处理。QoS 1 时该回调通常对应 PUBACK。**QoS 0 的回调绝不代表 Broker 已入库**。
- **有界 RAM 缓存**：最多 48 条待上传数据，每条最大 3800 字节。队列满后有日志并拒绝新数据。断电、进程崩溃将丢失 RAM 缓存，不承诺端到端零丢失。QoS 1 重传可能产生重复报文，需要服务器具备幂等性。
- **设备拓扑**：64 个子设备上限，超出一包时分片为多条合法的 `deviceInfo` 数组 MQTT 消息；配置更新和新设备动态发现时更新拓扑；重连重新发送拓扑。`pm` 与 `sound` 类型码原资料缺失，继续按旧 `L+localId` 回退，不能擅自指定正式编码。
- **保护机制**：保留前版 CRC 错误重同步、帧边界检查、串口断线重试、严格 JSON 解析、HTTP 认证校验和连接超时处理。

## 2. 源码目录

```text
GC6511_4G_Gateway_libmosquitto/
├── src/
│   ├── main.c       串口、帧状态机、数据封装、设备映射
│   ├── gateway.h    原有帧格式、平台接口和默认宏
│   ├── auth.c/.h    HTTP 认证、网关ID/MQTT地址发现
│   ├── net.c/.h     libmosquitto 使用、回调、重连、内存队列
│   └── json.c/.h    有界 JSON tokenizer
├── original/main_user.c        用户上传的原始 main(1).c
├── scripts/start_gateway.sh    进程退出重启脚本示例
├── tests/test_gateway.py       PTY + 模拟 HTTP/ MQTT 服务端联调
├── tests/mock/                **仅测试用的 libmosquitto 接口模拟，禁止部署**
├── Makefile
└── README_中文.md
```

## 3. libmosquitto 依赖和编译

**注意：不能只把 `net.c` 复制到 GC6511 上就运行。** GC6511 设备及对应 ARM SDK 必须具有同一架构的 libmosquitto 头文件、动态/静态库与依赖（可能包括 OpenSSL），且运行设备必须能找到匹配的共享库。

### 3.1 PC Linux 正式库编译

在有包管理器的常见 Debian/Ubuntu 环境上，可安装开发包：

```sh
sudo apt-get update
sudo apt-get install libmosquitto-dev pkg-config make gcc python3
make clean
make
make test
```

`make test` 使用本机实际安装的 **libmosquitto**。可在确认平台支持 QoS 1 后测试：

```sh
GW_MQTT_QOS=1 make test
```

`make` 默认使用 `pkg-config --cflags --libs libmosquitto` 获取库参数；若没有 `.pc` 文件则回退到 `-lmosquitto`。最终执行程序名为 `iot_gateway`。

### 3.2 GC6511 ARM Buildroot 交叉编译

1. 按厂家《GC65XX 系列 Buildroot 使用手册》，使用**与设备根文件系统同版本**的 Buildroot SDK 和 `armhf-htnice-linux-gnu` 编译器。GC6511 属 ARMv7-A Cortex-A7/32bit，而不是 x86 PC。
2. 在 Buildroot `make menuconfig` 中查找 `mosquitto` 并启用其客户端库组件（具体项目选项受 Buildroot 版本影响），重新生成 rootfs 与 SDK：`make`、`make sdk`。若采用单独交叉编译的 `libmosquitto`，务必使用目标 SDK 的 sysroot 和依赖。
3. 确认目标 sysroot 有 `mosquitto.h` 和 `libmosquitto.so`，设备根文件系统也具有目标架构库与其依赖，不能直接复制 Ubuntu x86_64 的 `.so`。
4. 确认 SDK 的实际编译器名称，例如 `...-gcc`，然后执行（以下为语法示例）：

```sh
make clean
make CROSS_COMPILE=arm-linux-gnueabihf- \
  PKG_CONFIG=arm-linux-gnueabihf-pkg-config
```

真实厂家编译器前缀**以 SDK 中的 `bin/*gcc` 为准**，上例并非声称厂家文件名如此。

SDK 里若没有适用的 `pkg-config`，也可显式传入：

```sh
make CROSS_COMPILE=<SDK-gcc前缀> \
  MOSQ_CFLAGS='-I<目标sysroot>/usr/include' \
  MOSQ_LIBS='-L<目标sysroot>/usr/lib -lmosquitto'
```

`<...>` 为需要替换的路径占位符，不应原样复制执行。注意交叉编译器的 sysroot 和 rpath/动态链接器也需要正确；`file iot_gateway` 应显示 ARM ELF。

### 3.3 没有 libmosquitto 的离线环境

仅可运行：

```sh
make test-mock
GW_MQTT_QOS=1 make test-mock
```

此命令编译 `tests/mock/` 下**仅供测试的 API 替身**，通过本地 TCP MQTT 模拟服务端验证上层逻辑。它**不调用真正的 libmosquitto**，不能替代正式库编译或真实网络验证。`test-mock` 生成的 `iot_gateway_mock` 只在测试期间使用，运行后删除，不会出现在交付工程的产品可执行文件内。

## 4. 部署和运行

配置好 4G 模块厂家 `4g_server` 和 `/etc/htnice_4g.conf` 中的 SIM/APN 及正确的 `ping_address`；若使用专网 SIM 卡，错误的公网 ping 地址可能触发反复模块复位。`main.c` 不负责拨号，只使用 Buildroot Linux 的网络路由。

```sh
chmod +x /home/htnice/app/iot_gateway
/home/htnice/app/iot_gateway /dev/ttyS1 115200 101.33.253.45 23106
```

参数次序：`串口设备 波特率 HTTP认证服务器地址 HTTP认证端口`，与原程序一致。默认 `101.33.253.45:23106`、网关 `YSHY-TEST-GATEWAY-001` 均为上传源码中的**测试值**，上线需核对正式配置，尤其不能多台设备共用同一网关 ID。

可将 `scripts/start_gateway.sh` 拷贝到 `/home/htnice/app/`，修改里面的参数，在厂家 `/etc/htset/autorun` 中添加：

```sh
/bin/sh /home/htnice/app/start_gateway.sh &
```

这是**进程守护脚本示例**，并不是硬件看门狗；更正式的产品方案可另外添加硬件 watchdog 喂狗、进程监督和日志轮转。

## 5. MQTT 运行参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `GW_MQTT_QOS` | `0` | 可设 `1`，前提是平台 Broker 接受 QoS1 |
| `GW_MQTT_KEEPALIVE` | `60` | libmosquitto 心跳周期（秒），有效范围 5～65535 |
| `GW_MQTT_CA_FILE` | 不设置 | 设置时启用 MQTT TLS、校验服务器证书，要求 Broker 端口支持 TLS；**HTTP 认证仍是明文** |

示例：

```sh
GW_MQTT_QOS=1 GW_MQTT_KEEPALIVE=45 \
  /home/htnice/app/iot_gateway /dev/ttyS1 115200 101.33.253.45 23106
```

QoS1 的 `on_publish` 是 Broker PUBACK 级确认，不代表应用数据库入库成功。若要保障断电不丢失和去重，应当增加非易失缓存、序号和云端应用层 ACK；本工程没有假设不存在于原接口文档中的 ACK/控制 Topic。

## 6. 已完成测试与尚未验证

完成离线 C 编译、PTTY 串口模拟、HTTP 认证和模拟 MQTT 服务器联调，在 QoS0、QoS1 两个配置下测试：

- 设备启动 CFG_REQ、模拟量和离散量批量转换；
- CRC 错帧、异常长度、分段帧恢复；
- MQTT 断线和自动连接恢复，断线期间采集的缓冲数据补发；
- 拓扑重连再发布、动态新设备发布和 30 个设备拆包；
- QoS0 提前完成回调与 QoS1 PUBACK 回调确认。

**当前测试容器未安装 `libmosquitto-dev`，且无法通过网络软件源取得，所以本次测试采用接口模拟。尚未在真实 libmosquitto 客户端库、GC6511 ARM 硬件、正式 HTTP/MQTT 服务器上编译联调；不能据此宣称实机稳定性已通过验收。**

上线前建议实施断网 1～8 小时恢复、基站信号弱、SIM 故障、长时间高频数据、掉电、证书/服务器地址变化，以及 24～72 小时压力测试。更多关于 `mosquitto_loop_start`、`mosquitto_reconnect_delay_set`、`mosquitto_publish_callback_set` 可查阅官方 API：https://mosquitto.org/api/
