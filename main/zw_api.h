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
    int red_territories;
    int blue_territories;
} zw_stats_t;

// 标记点
typedef struct {
    int id;
    double lat;
    double lng;
    char status[12];      // pending / accepted / rejected / deleted
    double created_at;    // 服务器时间戳（秒）
} zw_marker_t;

esp_err_t zw_api_init(const char *server_url);
esp_err_t zw_api_register(bool *has_team, int *team);
esp_err_t zw_api_join_team(int team);
esp_err_t zw_api_heartbeat(void);
esp_err_t zw_api_get_stats(zw_stats_t *stats);
esp_err_t zw_api_get_markers(zw_marker_t *markers, int max_count, int *out_count);
// 按状态过滤获取标记点：status = "recent"(最近10条) / "pending" 等，NULL 等价于 pending
esp_err_t zw_api_get_markers_ex(const char *status, zw_marker_t *markers, int max_count, int *out_count);
esp_err_t zw_api_add_marker(double lat, double lng, bool *territory_created, double *new_area, int *pending_count);
esp_err_t zw_api_delete_marker(int marker_id);
// 退出阵营：服务器端清零该用户领地与标记点，需配合 zw_nvs_clear_team() 使用
esp_err_t zw_api_exit_team(void);
