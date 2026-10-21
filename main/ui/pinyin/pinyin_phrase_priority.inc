typedef struct {
    const char *py;
    const char *abbr;
    const char *word;
} pinyin_phrase_priority_t;

static const pinyin_phrase_priority_t s_phrase_priorities[] = {
    {"nihao", "nh", "你好"},
    {"ninhao", "nh", "您好"},
    {"zaoshanghao", "zsh", "早上好"},
    {"zhongwuhao", "zwh", "中午好"},
    {"xiawuhao", "xwh", "下午好"},
    {"wanshanghao", "wsh", "晚上好"},
    {"zhongguo", "zg", "中国"},
    {"zhongwen", "zw", "中文"},
    {"hanyu", "hy", "汉语"},
    {"women", "wm", "我们"},
    {"xiexie", "xx", "谢谢"},
    {"zaijian", "zj", "再见"},
    {"duibuqi", "dbq", "对不起"},
    {"meiguanxi", "mgx", "没关系"},
    {"meiwenti", "mwt", "没问题"},
    {"bukeqi", "bkq", "不客气"},
    {"keyi", "ky", "可以"},
};
