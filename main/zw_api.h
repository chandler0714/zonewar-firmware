/*
 * ZoneWar - 服务器 API 模块
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// 管理界面统计数据
typedef struct {
    int territory_count;
    double total_area;
    int team_online;
    int team_total;
    double win_rate;
    double red_area;
    double blue_area;
} zw_stats_t;

// 标记点
typedef struct {
    int id;
    double lat;
    double lng;
} zw_marker_t;

esp_err_t zw_api_init(const char *server_url);
esp_err_t zw_api_register(bool *has_team, int *team);
esp_err_t zw_api_join_team(int team);
esp_err_t zw_api_heartbeat(void);
esp_err_t zw_api_get_stats(zw_stats_t *stats);
esp_err_t zw_api_get_markers(zw_marker_t *markers, int max_count, int *out_count);
esp_err_t zw_api_add_marker(double lat, double lng, bool *territory_created, double *new_area, int *pending_count);
esp_err_t zw_api_delete_marker(int marker_id);
