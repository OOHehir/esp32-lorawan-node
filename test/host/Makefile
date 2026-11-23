# Host tests. No ESP-IDF, no toolchain, no hardware:
#
#     make -C test/host
#
# Only units that are pure C and whose bugs are silent on a device live here. The Wi-Fi
# state machine and the NVS wrappers are deliberately absent -- testing them would mean
# mocking most of ESP-IDF, and the mocks would be what got tested.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
CPPFLAGS = -I. -I../../components/app_httpd/priv_include \
           -I../../components/app_ota/priv_include \
           -I../../components/app_mqtt/priv_include \
           -I../../main \
           -I../../components/app_lora/priv_include
LDFLAGS  = -fsanitize=address,undefined

UNITS = ../../components/app_httpd/json_util.c \
        ../../components/app_httpd/dns_pack.c \
        ../../components/app_ota/update_policy.c \
        ../../components/app_ota/rollback_policy.c \
        ../../components/app_ota/sha_hex.c \
        ../../components/app_mqtt/mqtt_cmd.c \
        ../../main/shed_proto.c \
        ../../components/app_lora/lora_wait.c
TESTS = main.c test_json_util.c test_dns_pack.c test_update_policy.c \
        test_rollback_policy.c test_sha_hex.c test_mqtt_cmd.c \
        test_shed_proto.c test_lora_wait.c

BIN = run_tests

.PHONY: all check clean
all: check

check: $(BIN)
	./$(BIN)
	node test_formatter.js

$(BIN): $(UNITS) $(TESTS) test.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(UNITS) $(TESTS) $(LDFLAGS)

clean:
	rm -rf $(BIN) $(BIN).dSYM
