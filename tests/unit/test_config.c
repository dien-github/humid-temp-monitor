// tests/unit/test_config.c
#include "unity.h"
#include "app_config.h"

void test_config_load_defaults(void) {
    app_config_t *cfg = app_config_get();
    TEST_ASSERT_EQUAL_INT(4, cfg->dht_pin);
    TEST_ASSERT_EQUAL_INT(5, cfg->relay_pin);
}

void test_config_save_wifi(void) {
    app_err_t ret = app_config_save_wifi("TestSSID", "TestPass123");
    TEST_ASSERT_EQUAL_INT(APP_OK, ret);
    
    app_config_t *cfg = app_config_get();
    TEST_ASSERT_EQUAL_STRING("TestSSID", cfg->wifi_ssid);
}