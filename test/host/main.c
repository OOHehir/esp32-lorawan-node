#include <stdio.h>

int tests_run = 0;
int tests_failed = 0;

void test_json_util(void);
void test_dns_pack(void);
void test_update_policy(void);
void test_rollback_policy(void);
void test_sha_hex(void);
void test_mqtt_cmd(void);
void test_shed_proto(void);
void test_lora_wait(void);
void test_shed_screen(void);

int main(void)
{
    test_json_util();
    test_dns_pack();
    test_update_policy();
    test_rollback_policy();
    test_sha_hex();
    test_mqtt_cmd();
    test_shed_proto();
    test_lora_wait();
    test_shed_screen();

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
