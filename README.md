# i2c-hdx

Half-duplex request/response protocol transport over I2C bus.

## Overview

将单工的 I2C 通信转换为半双工的 request/response 模型。通过 master 端轮询实现 slave 的"主动"上报能力。

## Architecture

```
Master                              Slave
  |                                   |
  |── I2C Write [Request Frame] ────▶ |   发送请求
  |                                   |   处理中...
  |── I2C Read [Poll] ──────────────▶ |
  |◀── Status: NOT_READY ──────────── |   还没好
  |                                   |
  |── I2C Read [Poll] ──────────────▶ |
  |◀── Status: READY + Response ───── |   拿到应答
```

## Frame Format

I2C 传输层透传用户协议帧，仅在 slave 读取时添加 1 字节 status 前缀：

### Master Write (Request)

```
I2C Data: [用户协议帧原样写入]
```

### Master Read (Response/Report)

```
I2C Data: [STATUS 1B] [用户协议帧]

STATUS:
  0x00 = 无数据
  0x01 = 有数据 (后续为完整协议帧)
  0xFF = 错误
```

## Features

- 零拷贝透传：不修改用户协议帧格式
- Slave 主动上报：通过发送队列，master poll 时取走
- 极低资源占用：< 1KB Flash, ~200B RAM (含 buffer)
- 无动态内存分配

## Directory Structure

```
src/
├── i2c_hdx.h          # 公共 API 头文件
├── i2c_hdx_master.c   # Master 端实现
├── i2c_hdx_slave.c    # Slave 端实现
├── i2c_hdx_crc.h      # CRC8 实现
├── i2c_hdx_config.h   # 配置项 (buffer 大小等)
└── port/              # 平台适配层
    ├── i2c_hdx_port.h     # 平台接口定义
    └── i2c_hdx_port_template.c  # 移植模板
examples/
├── master_request_response/   # 基础请求/应答示例
└── slave_periodic_report/     # Slave 定时上报示例
```

## Porting

实现 `src/port/i2c_hdx_port.h` 中定义的平台接口即可适配到任意 MCU。

## License

MIT
