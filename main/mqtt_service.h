#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t mqtt_service_start(void);
void mqtt_service_on_ip_ready(void);
bool mqtt_service_publish_complete_task(const char *task_id);
bool mqtt_service_publish_redeem(const char *reward_id);
void mqtt_service_resubscribe(void);
