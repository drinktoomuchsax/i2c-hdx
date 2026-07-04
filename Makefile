CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I src -I third_party/unity -I test

UNITY_SRC = third_party/unity/unity.c
SRC_MASTER = src/i2c_hdx_master.c
SRC_SLAVE = src/i2c_hdx_slave.c
MOCK_PORT = test/mock_port.c

BUILD_DIR = build

.PHONY: all test clean

all: test

test: test_checksum test_master test_slave test_loopback
	@echo "--- Running test_checksum ---"
	@$(BUILD_DIR)/test_checksum
	@echo ""
	@echo "--- Running test_master ---"
	@$(BUILD_DIR)/test_master
	@echo ""
	@echo "--- Running test_slave ---"
	@$(BUILD_DIR)/test_slave
	@echo ""
	@echo "--- Running test_loopback ---"
	@$(BUILD_DIR)/test_loopback
	@echo ""
	@echo "All tests passed."

test_checksum: $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_checksum test/test_checksum.c $(UNITY_SRC)

test_master: $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_master test/test_master.c $(SRC_MASTER) $(MOCK_PORT) $(UNITY_SRC)

test_slave: $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_slave test/test_slave.c $(SRC_SLAVE) $(UNITY_SRC)

test_loopback: $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_loopback test/test_loopback.c $(SRC_MASTER) $(SRC_SLAVE) $(MOCK_PORT) $(UNITY_SRC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
