/*
 * Config.c
 *
 *  Created on: 26-Feb-2026
 *      Author: Yashwanth
 */


#include "Config.h"

ram_queue_t log_queues[QUEUE_TYPES][PRIORITY_LEVELS];

config_payload_t config_data_instance;
config_payload_t *out_config = &config_data_instance;

read_request_t req_data_instance;
read_request_t *out_req = &req_data_instance;
