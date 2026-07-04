"""
I2C-HDX Bandwidth Report Generator

Calculates real throughput at a given I2C clock rate,
accounting for I2C bus-level overhead (START, STOP, ADDR, ACK bits).

Each I2C byte = 9 SCL clocks (8 data + 1 ACK)
Transaction overhead: START(1 bit) + ADDR+R/W(9 bits) + STOP(1 bit) = 11 bits
"""

import sys

I2C_CLOCK_HZ = 100_000
BITS_PER_BYTE = 9
TX_OVERHEAD_BITS = 11  # START + ADDR+W + STOP

PAYLOADS = [1, 4, 8, 16, 32, 64, 128]

WRITE_OVERHEAD_BYTES = 3   # LEN(2) + SUM(1)
READ_OVERHEAD_BYTES = 4    # STATUS(1) + LEN(2) + SUM(1)
EMPTY_POLL_BYTES = 1       # STATUS(1) only


def bits_per_transaction(data_bytes):
    return TX_OVERHEAD_BITS + data_bytes * BITS_PER_BYTE


def time_seconds(bits):
    return bits / I2C_CLOCK_HZ


def report_line(payload, empty_polls=1):
    write_bytes = payload + WRITE_OVERHEAD_BYTES
    write_bits = bits_per_transaction(write_bytes)

    read_resp_bytes = payload + READ_OVERHEAD_BYTES
    read_resp_bits = bits_per_transaction(read_resp_bytes)

    empty_poll_bits = bits_per_transaction(EMPTY_POLL_BYTES)
    total_empty_bits = empty_polls * empty_poll_bits

    total_bits = write_bits + total_empty_bits + read_resp_bits
    total_time = time_seconds(total_bits)

    effective_payload = 2 * payload  # request + response
    throughput_bps = effective_payload / total_time
    throughput_kbps = throughput_bps * 8 / 1000
    theoretical_max_bps = I2C_CLOCK_HZ / BITS_PER_BYTE
    utilization = throughput_bps / theoretical_max_bps * 100

    return (payload, total_time * 1000, throughput_bps, throughput_kbps, utilization)


def main():
    empty_polls = 1
    if len(sys.argv) > 1:
        empty_polls = int(sys.argv[1])

    clock_khz = I2C_CLOCK_HZ / 1000
    print(f"I2C Clock: {clock_khz:.0f} kHz | Empty polls per cycle: {empty_polls}")
    print(f"Theoretical max: {I2C_CLOCK_HZ / BITS_PER_BYTE:.0f} B/s = {I2C_CLOCK_HZ / BITS_PER_BYTE * 8 / 1000:.1f} kbps")
    print()
    print(f"{'Payload':<10}{'Time/cycle':<14}{'Throughput':<14}{'kbps':<12}{'Utilization':<12}")
    print("-" * 60)

    budget_ok = True
    for p in PAYLOADS:
        payload, time_ms, throughput, kbps, util = report_line(p, empty_polls)
        print(f"{payload:>5} B    {time_ms:>8.2f} ms   {throughput:>8.0f} B/s   {kbps:>6.1f}      {util:>5.1f}%")

        if p >= 16 and util < 50.0:
            print(f"  ERROR: utilization {util:.1f}% < 50% for {p}B payload")
            budget_ok = False

    print()
    if not budget_ok:
        print("FAIL: Bandwidth utilization below minimum threshold")
        sys.exit(1)
    else:
        print("PASS: All bandwidth thresholds met")


if __name__ == "__main__":
    main()
