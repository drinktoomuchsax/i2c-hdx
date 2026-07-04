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
