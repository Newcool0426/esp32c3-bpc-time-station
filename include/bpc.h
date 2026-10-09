/*
 * bpc.h - BPC (68.5 kHz, 中国国家标准时间码) 编码
 *
 * 一帧 20 秒，每 20 秒重复一次；每秒钟在整秒起始处有一个
 * 载波"负脉冲"(载波关断) 表示一个四进制符号：
 *   符号 0 -> 关断 100 ms
 *   符号 1 -> 关断 200 ms
 *   符号 2 -> 关断 300 ms
 *   符号 3 -> 关断 400 ms
 * 第 0 秒为"帧起始、无信号间隙"。
 */
#pragma once
#include <stdint.h>
#include <time.h>

/* 第 0 秒：帧起始，无间隙 (载波保持) */
#define BPC_NO_GAP  0xFFu

/*
 * 把本地时间编码成 20 个四进制符号 (每秒一个)。
 * sym[0] 为 BPC_NO_GAP，其余为 0..3。
 */
void bpc_encode(struct tm* local_time, uint8_t sym[20]);
