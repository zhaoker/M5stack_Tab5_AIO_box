typedef struct {
    const char *zh;
    const char *zh_tw;
    const char *en;
} webfm_page_text_t;

static const webfm_page_text_t s_webfm_page_texts[] = {
    {"ESP32 在线文件管理", "ESP32 在線文件管理", "ESP32 Web Files"},
    {"M5stack Tab5在线控制面板", "M5stack Tab5在線控制面板", "M5stack Tab5 Web Panel"},
    {"设备信息 - ESP32-P4", "設備資訊 - ESP32-P4", "Device Info - ESP32-P4"},
    {"设备信息", "設備資訊", "Device Info"},
    {"文件管理", "文件管理", "Files"},
    {"查看系统状态、网络、内存和 TF 卡信息", "查看系統狀態、網路、記憶體和 TF 卡資訊", "View system, network, memory and SD card"},
    {"浏览、上传、下载和管理存储器中的文件", "瀏覽、上傳、下載和管理儲存裝置中的文件", "Browse, upload and manage storage files"},
    {"选择一个功能开始使用", "選擇一個功能開始使用", "Choose a tool"},
    {"当前目录", "當前目錄", "Current Dir"},
    {"返回上级", "返回上級", "Up"},
    {"返回", "返回", "Back"},
    {"刷新", "重新整理", "Refresh"},
    {"新建目录", "新建目錄", "New Folder"},
    {"新建文件", "新建文件", "New File"},
    {"上传文件", "上傳文件", "Upload"},
    {"上传完成", "上傳完成", "Upload done"},
    {"正在上传", "正在上傳", "Uploading"},
    {"正在加载", "正在載入", "Loading"},
    {"加载中", "載入中", "Loading"},
    {"加载失败", "載入失敗", "Load failed"},
    {"加载超时", "載入逾時", "Load timeout"},
    {"图片过大，无法预览", "圖片過大，無法預覽", "Image is too large to preview"},
    {"名称", "名稱", "Name"},
    {"类型", "類型", "Type"},
    {"大小", "大小", "Size"},
    {"操作", "操作", "Actions"},
    {"打开", "打開", "Open"},
    {"下载", "下載", "Download"},
    {"改名", "改名", "Rename"},
    {"多选", "多選", "Select"},
    {"取消多选", "取消多選", "Cancel Select"},
    {"取消选择", "取消選擇", "Clear"},
    {"复制所选", "複製所選", "Copy Selected"},
    {"删除所选", "刪除所選", "Delete Selected"},
    {"全选", "全選", "Select All"},
    {"剪切", "剪下", "Cut"},
    {"粘贴", "貼上", "Paste"},
    {"复制", "複製", "Copy"},
    {"移动", "移動", "Move"},
    {"删除", "刪除", "Delete"},
    {"保存", "保存", "Save"},
    {"关闭", "關閉", "Close"},
    {"取消", "取消", "Cancel"},
    {"确认创建", "確認建立", "Create"},
    {"确认复制", "確認複製", "Copy"},
    {"确认移动", "確認移動", "Move"},
    {"开始上传", "開始上傳", "Start"},
    {"请输入新目录名称：", "請輸入新目錄名稱：", "Folder name:"},
    {"请输入新目录名称", "請輸入新目錄名稱", "Folder name"},
    {"请输入新文件名称：", "請輸入新文件名稱：", "File name:"},
    {"请输入新文件名称", "請輸入新文件名稱", "File name"},
    {"目录名称", "目錄名稱", "Folder name"},
    {"选择一个或多个文件上传到当前目录：", "選擇一個或多個文件上傳到當前目錄：", "Select files for this folder:"},
    {"选择一个或多个文件上传到当前目录", "選擇一個或多個文件上傳到當前目錄", "Select files for this folder"},
    {"目标目录", "目標目錄", "Target Dir"},
    {"根目录", "根目錄", "Root"},
    {"确定删除所选", "確定刪除所選", "Delete selected"},
    {"确定删除", "確定刪除", "Delete"},
    {"新名称", "新名稱", "New name"},
    {"复制到文件夹（相对当前盘符）", "複製到文件夾（相對當前磁碟）", "Copy to folder, relative to current drive"},
    {"请选择项目", "請選擇項目", "Select items first"},
    {"剪贴板为空", "剪貼簿為空", "Clipboard is empty"},
    {"粘贴完成", "貼上完成", "Paste done"},
    {"已剪切", "已剪下", "Cut"},
    {"已复制", "已複製", "Copied"},
    {"已删除", "已刪除", "Deleted"},
    {"上传完成：", "上傳完成：", "Upload done: "},
    {"已完成", "已完成", "Done"},
    {"失败", "失敗", "failed"},
    {"编码", "編碼", "Encoding"},
    {"，保存后转为 UTF-8", "，保存後轉為 UTF-8", ", saved as UTF-8"},
    {"保存后转为 UTF-8", "保存後轉為 UTF-8", "saved as UTF-8"},
    {"已保存", "已保存", "Saved"},
    {"此文件夹为空", "此文件夾為空", "This folder is empty"},
    {" 项已选", " 項已選", " items selected"},
    {"，失败", "，失敗", ", failed"},
    {"共 ", "共 ", "Total "},
    {" 项", " 項", " items"},
    {"每页", "每頁", "Page size"},
    {"第 ", "第 ", "Page "},
    {"首页", "首頁", "First"},
    {"上一页", "上一頁", "Prev"},
    {"下一页", "下一頁", "Next"},
    {"末页", "末頁", "Last"},
    {"文件夹", "文件夾", "Folder"},
    {"图片", "圖片", "Image"},
    {"音频", "音訊", "Audio"},
    {"视频", "影片", "Video"},
    {"压缩包", "壓縮包", "Archive"},
    {"文本", "文字", "Text"},
    {"文件", "文件", "File"},
    {"图片查看器", "圖片檢視器", "Image Viewer"},
    {"上一张", "上一張", "Prev"},
    {"下一张", "下一張", "Next"},
    {"旋转", "旋轉", "Rotate"},
    {"适应", "適應", "Fit"},
    {"滚轮缩放，拖拽移动", "滾輪縮放，拖曳移動", "Wheel to zoom, drag to move"},
    {"系统概览", "系統概覽", "System"},
    {"网络状态", "網路狀態", "Network"},
    {"内存信息", "記憶體資訊", "Memory"},
    {"板载设备", "板載裝置", "Board"},
    {"芯片型号", "晶片型號", "Chip"},
    {"IDF 版本", "IDF 版本", "IDF"},
    {"CPU 频率", "CPU 頻率", "CPU"},
    {"运行时间", "運行時間", "Uptime"},
    {"WiFi 状态", "WiFi 狀態", "WiFi"},
    {"信道", "通道", "Channel"},
    {"DRAM 可用", "DRAM 可用", "DRAM Free"},
    {"DRAM 总计", "DRAM 總計", "DRAM Total"},
    {"DMA 可用", "DMA 可用", "DMA Free"},
    {"DMA 总计", "DMA 總計", "DMA Total"},
    {"PSRAM 可用", "PSRAM 可用", "PSRAM Free"},
    {"PSRAM 总计", "PSRAM 總計", "PSRAM Total"},
    {"已更新", "已更新", "Updated"},
    {"连接失败", "連接失敗", "Connect failed"},
};

static const char *webfm_page_translation(const webfm_page_text_t *text)
{
    switch (app_i18n_get_language()) {
    case APP_LANGUAGE_ZH_TW:
        return text->zh_tw;
    case APP_LANGUAGE_EN:
        return text->en;
    case APP_LANGUAGE_ZH_CN:
    case APP_LANGUAGE_UNSET:
    default:
        return text->zh;
    }
}

static const webfm_page_text_t *webfm_find_next_translation(const char *cursor, const char **match_at, size_t *match_len)
{
    const webfm_page_text_t *best = NULL;
    const char *best_at = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < sizeof(s_webfm_page_texts) / sizeof(s_webfm_page_texts[0]); ++i) {
        const char *at = strstr(cursor, s_webfm_page_texts[i].zh);
        size_t len;
        if (!at) {
            continue;
        }
        len = strlen(s_webfm_page_texts[i].zh);
        if (!best_at || at < best_at || (at == best_at && len > best_len)) {
            best = &s_webfm_page_texts[i];
            best_at = at;
            best_len = len;
        }
    }

    if (match_at) {
        *match_at = best_at;
    }
    if (match_len) {
        *match_len = best_len;
    }
    return best;
}

static bool webfm_add_i18n_size(size_t *value, size_t add)
{
    if (!value || SIZE_MAX - *value < add) {
        return false;
    }
    *value += add;
    return true;
}

static char *webfm_translate_i18n_text(const char *text)
{
    const char *cursor = text;
    size_t out_len = 0;
    char *out;
    char *write_at;

    while (*cursor) {
        const char *match_at = NULL;
        size_t match_len = 0;
        const webfm_page_text_t *match = webfm_find_next_translation(cursor, &match_at, &match_len);

        if (!match_at || !match) {
            if (!webfm_add_i18n_size(&out_len, strlen(cursor))) {
                return NULL;
            }
            break;
        }
        if (!webfm_add_i18n_size(&out_len, (size_t)(match_at - cursor)) ||
            !webfm_add_i18n_size(&out_len, strlen(webfm_page_translation(match)))) {
            return NULL;
        }
        cursor = match_at + match_len;
    }
    if (!webfm_add_i18n_size(&out_len, 1)) {
        return NULL;
    }

    out = malloc(out_len);
    if (!out) {
        return NULL;
    }

    cursor = text;
    write_at = out;
    while (*cursor) {
        const char *match_at = NULL;
        size_t match_len = 0;
        const webfm_page_text_t *match = webfm_find_next_translation(cursor, &match_at, &match_len);

        if (!match_at || !match) {
            size_t len = strlen(cursor);
            memcpy(write_at, cursor, len);
            write_at += len;
            break;
        }
        if (match_at > cursor) {
            size_t len = (size_t)(match_at - cursor);
            memcpy(write_at, cursor, len);
            write_at += len;
        }
        const char *translation = webfm_page_translation(match);
        if (translation[0]) {
            size_t len = strlen(translation);
            memcpy(write_at, translation, len);
            write_at += len;
        }
        cursor = match_at + match_len;
    }
    *write_at = '\0';
    return out;
}

static esp_err_t webfm_send_i18n_text(httpd_req_t *req, const char *text)
{
    char *translated;
    esp_err_t ret;

    if (!text) {
        return ESP_OK;
    }
    if (app_i18n_get_language() == APP_LANGUAGE_ZH_CN) {
        return httpd_resp_sendstr_chunk(req, text);
    }

    translated = webfm_translate_i18n_text(text);
    if (!translated) {
        ESP_LOGW(TAG, "Web i18n allocation failed, sending raw text");
        return httpd_resp_sendstr_chunk(req, text);
    }
    ret = httpd_resp_sendstr_chunk(req, translated);
    free(translated);
    return ret;
}

static esp_err_t webfm_send_i18n_page(httpd_req_t *req, const char *page)
{
    esp_err_t ret = webfm_send_i18n_text(req, page);
    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}
