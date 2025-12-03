// tests/integration/test_system.c
void test_task_initialization(void) {
    // Test that all tasks start
    system_task_init();
    system_task_start_all(config);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Check tasks are running
    system_status_t status;
    system_task_get_status(&status);
    
    TEST_ASSERT_NOT_EQUAL(SYSTEM_STATE_INIT, status.state);
}

void test_sensor_to_mqtt_flow(void) {
    // Test end-to-end: sensor read → MQTT publish
    // This requires hardware + network
}