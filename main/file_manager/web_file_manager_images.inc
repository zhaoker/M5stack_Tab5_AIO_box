static esp_err_t webfm_image_cached_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char filename[128];
    char disposition[192];
    FILE *fp = NULL;
    uint8_t *image = NULL;
    uint8_t *scratch = NULL;
    struct stat st;
    bool usb_locked = false;
    bool usb_path;
    size_t total_read = 0;
    size_t scratch_size;
    int64_t start_us;
    esp_err_t ret = ESP_OK;

    if (!webfm_query_decoded(req, "path", rel, sizeof(rel)) ||
        !webfm_build_path(rel, path, sizeof(path)) ||
        !webfm_is_image_path(path)) {
        ESP_LOGW(TAG, "Web image cached rejected: rel='%s'", rel);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    usb_path = webfm_path_uses_usb(path);
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "Web image cached failed: not a file rel='%s' path='%s' errno=%d", rel, path, errno);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    if ((uint64_t)st.st_size > WEBFM_IMAGE_CACHE_MAX_BYTES) {
        ESP_LOGW(TAG, "Web image cached rejected: too large rel='%s' size=%llu", rel, (unsigned long long)st.st_size);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 413, TRS("图片过大，无法预览"));
    }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) <
        (size_t)st.st_size + WEBFM_IMAGE_CACHE_PSRAM_RESERVE_BYTES) {
        ESP_LOGW(TAG,
                 "Web image cached rejected: psram low rel='%s' size=%llu free=%u",
                 rel,
                 (unsigned long long)st.st_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }

    image = heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    scratch_size = WEBFM_IO_BUF_SIZE;
    scratch = heap_caps_malloc(scratch_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!image || !scratch) {
        free(image);
        free(scratch);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 500, TRS("内存不足"));
    }

    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Web image cached failed: fopen rel='%s' path='%s' errno=%d", rel, path, errno);
        free(image);
        free(scratch);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("文件打开失败"));
    }
    if (usb_path) {
        setvbuf(fp, NULL, _IONBF, 0);
    }

    start_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "Web image cache start: rel='%s' path='%s' size=%llu usb=%s scratch=%u psram_free=%u",
             rel,
             path,
             (unsigned long long)st.st_size,
             usb_path ? "yes" : "no",
             (unsigned)scratch_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    while (total_read < (size_t)st.st_size) {
        size_t left = (size_t)st.st_size - total_read;
        size_t want = left < scratch_size ? left : scratch_size;
        size_t got = fread(scratch, 1, want, fp);
        if (got > 0) {
            memcpy(image + total_read, scratch, got);
            total_read += got;
            if (usb_path) {
                vTaskDelay(pdMS_TO_TICKS(WEBFM_USB_READ_DELAY_MS));
            }
        }
        if (got < want) {
            if (ferror(fp)) {
                ESP_LOGW(TAG,
                         "Web image cache read failed: rel='%s' path='%s' read=%u/%llu errno=%d",
                         rel,
                         path,
                         (unsigned)total_read,
                         (unsigned long long)st.st_size,
                         errno);
                ret = ESP_FAIL;
            } else {
                ESP_LOGW(TAG,
                         "Web image cache short read: rel='%s' path='%s' read=%u/%llu",
                         rel,
                         path,
                         (unsigned)total_read,
                         (unsigned long long)st.st_size);
                ret = ESP_FAIL;
            }
            break;
        }
    }
    fclose(fp);
    free(scratch);
    webfm_end_usb_io(usb_locked);

    if (ret != ESP_OK || total_read != (size_t)st.st_size) {
        ESP_LOGI(TAG,
                 "Web image cache done: rel='%s' read=%u/%llu usb=%s elapsed=%lld ms ret=%s",
                 rel,
                 (unsigned)total_read,
                 (unsigned long long)st.st_size,
                 usb_path ? "yes" : "no",
                 (long long)((esp_timer_get_time() - start_us) / 1000),
                 esp_err_to_name(ret));
        free(image);
        return webfm_send_error(req, 500, TRS("读取失败"));
    }

    httpd_resp_set_type(req, webfm_content_type_for_path(path));
    webfm_sanitize_header_filename(webfm_basename(path), filename, sizeof(filename));
    snprintf(disposition, sizeof(disposition), "inline; filename=\"%s\"", filename[0] ? filename : "image");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ret = httpd_resp_send(req, (const char *)image, total_read);
    ESP_LOGI(TAG,
             "Web image cache done: rel='%s' sent=%u/%llu usb=%s elapsed=%lld ms ret=%s psram_free=%u",
             rel,
             (unsigned)total_read,
             (unsigned long long)st.st_size,
             usb_path ? "yes" : "no",
             (long long)((esp_timer_get_time() - start_us) / 1000),
             esp_err_to_name(ret),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    free(image);
    return ret;
}

static esp_err_t webfm_imglist_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char path[WEBFM_MAX_PATH_LEN] = {0};
    char escaped_dir[WEBFM_JSON_NAME_LEN];
    DIR *dir;
    bool first = true;
    bool usb_locked = false;
    bool usb_path;
    size_t item_count = 0;
    int64_t start_us;

    (void)webfm_query_decoded(req, "dir", rel, sizeof(rel));
    if (!webfm_build_path(rel, path, sizeof(path))) {
        ESP_LOGW(TAG, "Web imglist rejected: invalid rel='%s'", rel);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    usb_path = webfm_path_uses_usb(path);
    if (!webfm_begin_usb_io_for_paths(path, NULL, &usb_locked)) {
        return webfm_send_error(req, 503, TRS("存储器忙，请稍后再试"));
    }
    start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Web imglist start: rel='%s' path='%s' usb=%s", rel, path, usb_path ? "yes" : "no");
    dir = opendir(path);
    if (!dir) {
        ESP_LOGW(TAG, "Web imglist failed: opendir rel='%s' path='%s' errno=%d", rel, path, errno);
        webfm_end_usb_io(usb_locked);
        return webfm_send_error(req, 404, TRS("目录打开失败"));
    }

    webfm_json_escape(rel, escaped_dir, sizeof(escaped_dir));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    if (httpd_resp_sendstr_chunk(req, "{\"dir\":\"") != ESP_OK ||
        httpd_resp_sendstr_chunk(req, escaped_dir) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, "\",\"items\":[") != ESP_OK) {
        ESP_LOGW(TAG, "Web imglist send header failed: rel='%s'", rel);
        closedir(dir);
        webfm_end_usb_io(usb_locked);
        return ESP_FAIL;
    }
    while (true) {
        struct dirent *ent = readdir(dir);
        char escaped[WEBFM_JSON_NAME_LEN];
        char item[384];
        if (!ent) {
            break;
        }
        if (ent->d_name[0] == '.' || !webfm_is_image_path(ent->d_name)) {
            continue;
        }
        webfm_json_escape(ent->d_name, escaped, sizeof(escaped));
        snprintf(item, sizeof(item), "%s\"%s\"", first ? "" : ",", escaped);
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) {
            ESP_LOGW(TAG, "Web imglist aborted: rel='%s' items=%u", rel, (unsigned)item_count);
            closedir(dir);
            webfm_end_usb_io(usb_locked);
            return ESP_FAIL;
        }
        first = false;
        item_count++;
    }
    closedir(dir);
    if (httpd_resp_sendstr_chunk(req, "]}") != ESP_OK) {
        webfm_end_usb_io(usb_locked);
        return ESP_FAIL;
    }
    webfm_end_usb_io(usb_locked);
    ESP_LOGI(TAG,
             "Web imglist done: rel='%s' items=%u usb=%s elapsed=%lld ms",
             rel,
             (unsigned)item_count,
             usb_path ? "yes" : "no",
             (long long)((esp_timer_get_time() - start_us) / 1000));
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t webfm_imgview_get_handler(httpd_req_t *req)
{
    char rel[WEBFM_MAX_PATH_LEN] = {0};
    char return_dir[WEBFM_MAX_PATH_LEN] = {0};
    char escaped[WEBFM_JSON_NAME_LEN];
    char parent[WEBFM_MAX_PATH_LEN] = {0};
    char escaped_parent[WEBFM_JSON_NAME_LEN];
    char escaped_return_dir[WEBFM_JSON_NAME_LEN];

    if (!webfm_query_decoded(req, "path", rel, sizeof(rel)) || !webfm_is_safe_rel_path(rel)) {
        ESP_LOGW(TAG, "Web imgview rejected: rel='%s'", rel);
        return webfm_send_error(req, 400, TRS("路径无效"));
    }
    (void)webfm_query_decoded(req, "dir", return_dir, sizeof(return_dir));
    if (!webfm_is_safe_rel_path(return_dir)) {
        return_dir[0] = '\0';
    }
    webfm_parent_rel(rel, parent, sizeof(parent));
    if (return_dir[0] == '\0') {
        strlcpy(return_dir, parent, sizeof(return_dir));
    }
    ESP_LOGI(TAG, "Web imgview opened: rel='%s' parent='%s' return='%s'", rel, parent, return_dir);
    webfm_json_escape(rel, escaped, sizeof(escaped));
    webfm_json_escape(parent, escaped_parent, sizeof(escaped_parent));
    webfm_json_escape(return_dir, escaped_return_dir, sizeof(escaped_return_dir));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t ret = webfm_send_i18n_text(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='/favicon.ico' type='image/svg+xml'>"
        "<title>图片查看器</title><style>"
        "body{margin:0;background:#020617;color:#e5e7eb;font-family:system-ui,sans-serif;overflow:hidden}"
        ".bar{position:fixed;top:0;left:0;right:0;min-height:56px;background:#0f172acc;display:flex;align-items:center;gap:10px;padding:8px 12px;z-index:2;flex-wrap:wrap}"
        "button,.btn{font:inherit;border-radius:8px;border:1px solid #334155;background:#1e293b;color:#e5e7eb;padding:8px 12px;text-decoration:none;cursor:pointer}.name{flex:1 1 220px;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#cbd5e1}.hint{color:#94a3b8;font-size:13px}"
        ".stage{position:absolute;inset:56px 0 0;display:flex;align-items:center;justify-content:center;overflow:hidden;cursor:grab;touch-action:none}.stage.dragging{cursor:grabbing}img{max-width:100%;max-height:100%;transform-origin:center center;user-select:none;-webkit-user-drag:none;pointer-events:none;will-change:transform}"
        ".loading{position:absolute;inset:56px 0 0;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:14px;color:#cbd5e1;background:#020617;z-index:1}.loading.hidden{display:none}.spin{width:36px;height:36px;border:4px solid #334155;border-top-color:#60a5fa;border-radius:50%;animation:s 1s linear infinite}@keyframes s{to{transform:rotate(360deg)}}.loadmsg{font-size:15px}.loadmsg.err{color:#fca5a5}"
        "@media(max-width:760px){.bar{gap:6px}.bar button,.bar .btn{padding:7px 9px;font-size:14px}.hint{display:none}.stage,.loading{inset:92px 0 0}}"
        "</style></head><body><div class='bar'><button onclick='prev()'>上一张</button><button onclick='next()'>下一张</button><button onclick='rot()'>旋转</button><button onclick='resetView()'>适应</button><a class='btn' id='downloadBtn'>下载</a><button onclick='closeView()'>关闭</button><span class='name' id='name'></span><span class='hint'>滚轮缩放，拖拽移动</span></div><div class='loading' id='loading'><div class='spin'></div><div class='loadmsg' id='loadmsg'>加载中...</div></div><div class='stage' id='stage'><img id='img'></div><script>");
    if (ret != ESP_OK) {
        return ret;
    }
    httpd_resp_sendstr_chunk(req, "let cur=\"");
    httpd_resp_sendstr_chunk(req, escaped);
    httpd_resp_sendstr_chunk(req, "\",dir=\"");
    httpd_resp_sendstr_chunk(req, escaped_parent);
    httpd_resp_sendstr_chunk(req, "\",returnDir=\"");
    httpd_resp_sendstr_chunk(req, escaped_return_dir);
    ret = webfm_send_i18n_text(req,
        "\",items=[],shown=false,scale=1,angle=0,tx=0,ty=0,drag=false,sx=0,sy=0,stx=0,sty=0,objUrl='';const enc=encodeURIComponent,img=document.getElementById('img'),stage=document.getElementById('stage'),nameEl=document.getElementById('name'),downloadBtn=document.getElementById('downloadBtn'),loading=document.getElementById('loading'),loadmsg=document.getElementById('loadmsg');"
        "function file(){return cur.split('/').pop()}function apply(){img.style.transform=`translate(${tx}px,${ty}px) scale(${scale}) rotate(${angle}deg)`}function resetView(){scale=1;tx=0;ty=0;apply()}function setLoading(t,err=false){loadmsg.textContent=t;loadmsg.className='loadmsg '+(err?'err':'');loading.className='loading'}function hideLoading(){loading.className='loading hidden'}"
        "async function show(){shown=true;nameEl.textContent=cur;downloadBtn.href='/webfm/download?path='+enc(cur);resetView();setLoading('加载中...');img.removeAttribute('src');if(objUrl){URL.revokeObjectURL(objUrl);objUrl=''}try{let r=await fetch('/webfm/image?path='+enc(cur));if(!r.ok)throw new Error(await r.text()||r.status);let b=await r.blob();objUrl=URL.createObjectURL(b);img.onload=()=>{hideLoading();resetView()};img.onerror=()=>setLoading('加载失败',true);img.src=objUrl}catch(e){setLoading('加载失败',true)}}"
        "function closeView(){if(objUrl)URL.revokeObjectURL(objUrl);location='/webfm'+(returnDir?'?dir='+enc(returnDir):'')}"
        "function loadList(){let listCtl=new AbortController(),listTimer=setTimeout(()=>listCtl.abort(),3000);fetch('/webfm/imglist?dir='+enc(dir),{signal:listCtl.signal}).then(r=>r.json()).then(j=>{items=j.items||[]}).catch(()=>{}).finally(()=>clearTimeout(listTimer))}"
        "function idx(){return items.indexOf(file())}function setBy(i){if(!items.length)return;i=(i+items.length)%items.length;cur=(dir?dir+'/':'')+items[i];angle=0;show()}"
        "function prev(){setBy(idx()-1)}function next(){setBy(idx()+1)}function rot(){angle=(angle+90)%360;apply()}"
        "stage.addEventListener('wheel',e=>{e.preventDefault();let old=scale;let next=Math.max(.15,Math.min(8,scale*(e.deltaY<0?1.12:.88)));let r=stage.getBoundingClientRect(),cx=e.clientX-r.left-r.width/2,cy=e.clientY-r.top-r.height/2;tx=cx-(cx-tx)*(next/old);ty=cy-(cy-ty)*(next/old);scale=next;apply()},{passive:false});"
        "function dragMove(e){if(!drag)return;tx=stx+e.clientX-sx;ty=sty+e.clientY-sy;apply()}"
        "function dragEnd(e){if(!drag)return;drag=false;stage.classList.remove('dragging');try{stage.releasePointerCapture(e.pointerId)}catch(_){}}"
        "stage.addEventListener('pointerdown',e=>{e.preventDefault();drag=true;sx=e.clientX;sy=e.clientY;stx=tx;sty=ty;stage.classList.add('dragging');try{stage.setPointerCapture(e.pointerId)}catch(_){}});"
        "stage.addEventListener('pointermove',dragMove);document.addEventListener('pointermove',dragMove);"
        "stage.addEventListener('pointerup',dragEnd);document.addEventListener('pointerup',dragEnd);stage.addEventListener('pointercancel',dragEnd);"
        "document.addEventListener('keydown',e=>{let k=e.key;if(k==='ArrowUp'||k==='ArrowLeft'){e.preventDefault();prev()}else if(k==='ArrowDown'||k==='ArrowRight'){e.preventDefault();next()}else if(k==='Enter'){e.preventDefault();rot()}else if(k==='Escape'){e.preventDefault();closeView()}});show();setTimeout(loadList,80);"
        "</script></body></html>");
    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}
