typedef struct {
    int year;
    int month;
    int day;
    bool leap;
} ss_lunar_date_t;

static const uint32_t s_lunar_info_2020_2050[] = {
    0x07954, 0x0dd4a, 0x0bd50, 0x17544, 0x056a0, 0x0b550, 0x055d2, 0x04ae0,
    0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0,
    0x14977, 0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60,
    0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0, 0x0ea50, 0x06e95,
};

static int ss_lunar_leap_month(int year)
{
    if (year < 2020 || year > 2050) {
        return 0;
    }
    return (int)(s_lunar_info_2020_2050[year - 2020] & 0x0f);
}

static int ss_lunar_leap_days(int year)
{
    int leap = ss_lunar_leap_month(year);
    if (leap == 0) {
        return 0;
    }
    return (s_lunar_info_2020_2050[year - 2020] & 0x10000) ? 30 : 29;
}

static int ss_lunar_month_days(int year, int month)
{
    if (year < 2020 || year > 2050 || month < 1 || month > 12) {
        return 29;
    }
    return (s_lunar_info_2020_2050[year - 2020] & (0x10000U >> month)) ? 30 : 29;
}

static bool ss_lunar_from_time(const struct tm *tm_now, ss_lunar_date_t *out)
{
    struct tm base_tm = {
        .tm_year = 2020 - 1900,
        .tm_mon = 0,
        .tm_mday = 25,
        .tm_hour = 12,
    };
    struct tm cur_tm;
    time_t base_ts;
    time_t cur_ts;
    int offset_days;
    int year = 2020;
    int month = 1;
    bool leap = false;

    if (!tm_now || !out) {
        return false;
    }
    if (tm_now->tm_year + 1900 < 2020 || tm_now->tm_year + 1900 > 2050) {
        return false;
    }
    cur_tm = *tm_now;
    cur_tm.tm_hour = 12;
    cur_tm.tm_min = 0;
    cur_tm.tm_sec = 0;
    cur_tm.tm_isdst = -1;
    base_tm.tm_isdst = -1;
    base_ts = mktime(&base_tm);
    cur_ts = mktime(&cur_tm);
    if (base_ts == (time_t)-1 || cur_ts == (time_t)-1 || cur_ts < base_ts) {
        return false;
    }
    offset_days = (int)((cur_ts - base_ts) / (24 * 60 * 60));
    for (year = 2020; year <= 2050; year++) {
        int days = 0;
        for (int m = 1; m <= 12; m++) {
            days += ss_lunar_month_days(year, m);
            if (ss_lunar_leap_month(year) == m) {
                days += ss_lunar_leap_days(year);
            }
        }
        if (offset_days < days) {
            break;
        }
        offset_days -= days;
    }
    if (year > 2050) {
        return false;
    }
    month = 1;
    leap = false;
    while (month <= 12) {
        int days = leap ? ss_lunar_leap_days(year) : ss_lunar_month_days(year, month);
        if (offset_days < days) {
            out->year = year;
            out->month = month;
            out->day = offset_days + 1;
            out->leap = leap;
            return true;
        }
        offset_days -= days;
        if (ss_lunar_leap_month(year) == month && !leap) {
            leap = true;
        } else {
            leap = false;
            month++;
        }
    }
    return false;
}

static const char *ss_lunar_year_ganzhi(int year)
{
    static const char *const stems[] = {
        "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
    };
    static const char *const branches[] = {
        "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥",
    };
    static char out[16];
    int idx = year - 1984;
    if (idx < 0) {
        idx = 0;
    }
    snprintf(out,
             sizeof(out),
             "%s%s",
             stems[idx % 10],
             branches[idx % 12]);
    return out;
}

static void ss_lunar_format_day(int day, char *out, size_t out_len)
{
    static const char *const tens[] = {"初", "十", "廿", "三"};
    static const char *const nums[] = {
        "", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十",
    };

    if (!out || out_len == 0) {
        return;
    }
    if (day == 10) {
        snprintf(out, out_len, "初十");
    } else if (day == 20) {
        snprintf(out, out_len, "二十");
    } else if (day == 30) {
        snprintf(out, out_len, "三十");
    } else if (day > 0 && day < 31) {
        snprintf(out, out_len, "%s%s", tens[day / 10], nums[day % 10]);
    } else {
        out[0] = '\0';
    }
}

static bool ss_lunar_format(const struct tm *tm_now, char *out, size_t out_len)
{
    static const char *const months[] = {
        "", "正月", "二月", "三月", "四月", "五月", "六月",
        "七月", "八月", "九月", "十月", "冬月", "腊月",
    };
    ss_lunar_date_t lunar;
    char day[16];

    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!ss_lunar_from_time(tm_now, &lunar)) {
        return false;
    }
    ss_lunar_format_day(lunar.day, day, sizeof(day));
    snprintf(out,
             out_len,
             "农历%s年%s%s%s",
             ss_lunar_year_ganzhi(lunar.year),
             lunar.leap ? "闰" : "",
             months[lunar.month],
             day);
    return true;
}
