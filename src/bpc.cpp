/*
 * bpc.cpp - BPC 68.5 kHz 时间码编码实现
 *
 * 编码表依据维基百科《BPC (time signal)》：
 *   秒 00 : 帧起始，无间隙
 *   秒 01 : 帧序号 (00 / 20 / 40 秒)，权重 40/20
 *   秒 02 : 未使用 = 0
 *   秒 03 : 时 (00-11) 权重 8/4
 *   秒 04 : 时         权重 2/1
 *   秒 05 : 分 (00-59) 权重 32/16
 *   秒 06 : 分         权重 8/4
 *   秒 07 : 分         权重 2/1
 *   秒 08 : 未使用(0) + 星期(1=一..7=日) 权重 4
 *   秒 09 : 星期       权重 2/1
 *   秒 10 : AM/PM(0/1) + P1(秒 01-09 偶校验)
 *   秒 11 : 未使用(0) + 日 权重 16
 *   秒 12 : 日         权重 8/4
 *   秒 13 : 日         权重 2/1
 *   秒 14 : 月         权重 8/4
 *   秒 15 : 月         权重 2/1
 *   秒 16 : 年         权重 32/16
 *   秒 17 : 年         权重 8/4
 *   秒 18 : 年         权重 2/1
 *   秒 19 : 年 MSB(64) + P2(秒 11-18 偶校验)
 */
#include "bpc.h"

namespace {
inline int bit(int v, int pos) { return (v >> pos) & 1; }
inline int pair2(int msb, int lsb) { return (msb << 1) | lsb; }
}  // namespace

void bpc_encode(struct tm* local_time, uint8_t sym[20])
{
    const int hour24 = local_time->tm_hour;
    const int hour12 = hour24 % 12;          /* 00 - 11 (BPC 约定) */
    const int is_pm  = (hour24 >= 12) ? 1 : 0;
    const int minute = local_time->tm_min;
    int       wday   = local_time->tm_wday;  /* 0=星期日 */
    if (wday == 0) wday = 7;                 /* 1=星期一 .. 7=星期日 */
    const int day    = local_time->tm_mday;
    const int month  = local_time->tm_mon + 1;
    const int year   = local_time->tm_year % 100;

    for (int i = 0; i < 20; ++i) sym[i] = 0;

    sym[0] = BPC_NO_GAP;
    sym[1] = (uint8_t)(local_time->tm_sec / 20);   /* 0,1,2 -> 00/20/40 */
    sym[2] = 0;
    sym[3] = (uint8_t)pair2(bit(hour12, 3), bit(hour12, 2));
    sym[4] = (uint8_t)pair2(bit(hour12, 1), bit(hour12, 0));
    sym[5] = (uint8_t)pair2(bit(minute, 5), bit(minute, 4));
    sym[6] = (uint8_t)pair2(bit(minute, 3), bit(minute, 2));
    sym[7] = (uint8_t)pair2(bit(minute, 1), bit(minute, 0));
    sym[8] = (uint8_t)pair2(0, bit(wday, 2));
    sym[9] = (uint8_t)pair2(bit(wday, 1), bit(wday, 0));

    /* P1: 秒 01-09 的偶校验 */
    int p1 = 0;
    for (int i = 1; i <= 9; ++i) {
        p1 ^= (sym[i] >> 1) & 1;
        p1 ^= sym[i] & 1;
    }
    sym[10] = (uint8_t)pair2(is_pm, p1);

    sym[11] = (uint8_t)pair2(0, bit(day, 4));
    sym[12] = (uint8_t)pair2(bit(day, 3), bit(day, 2));
    sym[13] = (uint8_t)pair2(bit(day, 1), bit(day, 0));
    sym[14] = (uint8_t)pair2(bit(month, 3), bit(month, 2));
    sym[15] = (uint8_t)pair2(bit(month, 1), bit(month, 0));
    sym[16] = (uint8_t)pair2(bit(year, 5), bit(year, 4));
    sym[17] = (uint8_t)pair2(bit(year, 3), bit(year, 2));
    sym[18] = (uint8_t)pair2(bit(year, 1), bit(year, 0));

    /* P2: 秒 11-18 的偶校验 */
    int p2 = 0;
    for (int i = 11; i <= 18; ++i) {
        p2 ^= (sym[i] >> 1) & 1;
        p2 ^= sym[i] & 1;
    }
    sym[19] = (uint8_t)pair2(bit(year, 6), p2);
}
