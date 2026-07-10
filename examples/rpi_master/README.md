# Raspberry Pi Master Example

树莓派作为 I2C Master，通过 i2c-hdx 协议与 slave MCU 通信。

## 硬件接线

```
Raspberry Pi          Slave MCU
  GPIO2 (SDA) ─────── SDA
  GPIO3 (SCL) ─────── SCL
  GND ─────────────── GND
```

注意：树莓派 I2C 自带 1.8kΩ 上拉，通常不需要额外上拉电阻。如果 slave 是 3.3V 逻辑则可以直连。

## 编译

```bash
cd examples/rpi_master
make
```

## 使用

```bash
# 默认: /dev/i2c-1, slave 地址 0x20
./i2c_hdx_master_demo

# 指定设备和地址
./i2c_hdx_master_demo /dev/i2c-1 0x30
```

## 前置条件

```bash
# 启用 I2C
sudo raspi-config  # Interface Options → I2C → Enable

# 确认设备存在
ls /dev/i2c-*

# 扫描总线确认 slave 在线
sudo i2cdetect -y 1
```

## 程序行为

1. **Request-Response**: 发送一帧请求，轮询等待 slave 应答（最多 100 次，间隔 10ms）
2. **Poll Reports**: 连续轮询 100 次，打印 slave 主动上报的数据

## 调试工具 `i2c_hdx_master_tool`

`main.c` 的 demo 演示的是链路已经通了之后怎么用。`i2c_hdx_master_tool` 是给**另一种情况**准备的：链路还没通，你需要看清到底卡在哪。

```bash
make i2c_hdx_master_tool

# 用法: ./i2c_hdx_master_tool [-d DEV] [-a ADDR] <command> [payload-hex...]
./i2c_hdx_master_tool -d /dev/i2c-1 -a 0x20 probe
```

| 命令 | 作用 |
|---|---|
| `probe` | 一次**裸读**并逐字节解码（STATUS/LEN/SUM/payload）——link 到底在不在？ |
| `send <hex>` | `i2c_hdx_master_send()` 发一帧请求 |
| `poll` | `i2c_hdx_master_poll()` 轮询一次 |
| `xfer <hex>` | 核心半双工来回：发请求，再轮询等应答 |
| `monitor` | 持续轮询，打印 slave 主动上报的每一帧 |
| `repl` | 交互模式：敲 hex 发送，看应答 |

payload 十六进制接受多种写法：`DEADBEEF`、`DE AD BE EF`、`0xDE 0xAD`、`de:ad`。

排障时先 `probe`：master API（`i2c_hdx_master_poll`）把线上的原始字节压成了一个 err 码，看不出是**从机不在/NAK**、**STATUS=EMPTY（队列空）**、**校验错**还是**长度错位**。`probe` 直接走 port 读原始字节并解码，能一眼区分这几种。链路 `probe` 通了、从机会应答之后，再用 `xfer` 验证完整的请求/响应闭环。

> `probe` 走底层 port 读原始字节；其余命令都用公开的 `i2c_hdx_master_*` API，因此本工具也是一份驱动库的可用示例。
