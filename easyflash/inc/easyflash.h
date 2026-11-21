/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2014-2019, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: It is an head file for this library. You can see all be called functions.
 * Created on: 2014-09-10
 */

/*
 * ============================================================================
 * 【B级导读】easyflash.h —— 全库唯一公共 API 头（API 契约总表）
 * ----------------------------------------------------------------------------
 * 应用层只需 include 本文件。API 按"源文件归属"分组（见各组前注释）：
 *   easyflash_init()          整库入口，最先调用；
 *   ef_get_env_blob/ef_set_env_blob   NG 模式推荐 API（任意 blob，即时落盘）；
 *   ef_get_env/ef_set_env/... 旧字符串 API（三套 env 实现都提供）；
 *   ef_erase_bak_app/...      IAP 升级辅助；ef_log_read/... 日志存储；
 *   ef_calc_crc32             CRC 工具；ef_port_* 是移植层需实现的回调。
 * 移植/裁剪见 ef_cfg.h；错误码与核心结构见 ef_def.h。
 * 与 FlashDB 的对应关系：本头文件的 env 部分在 FlashDB 中演化为 fdb_kvdb.h。
 * ============================================================================
 */


#ifndef EASYFLASH_H_
#define EASYFLASH_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ef_cfg.h>
#include <ef_def.h>

#ifdef __cplusplus
extern "C" {
#endif

/* easyflash.c */
/* 【契约】整库唯一入口：必须最先调用，内部幂等；成功前其余 API 一律返回错误 */
EfErrCode easyflash_init(void);

#ifdef EF_USING_ENV
/* only supported on ef_env.c */
/*
 * 【契约】NG 模式（ef_env.c）专属 API：任意二进制 blob、即时落盘、RAM 占用趋近 0。
 *   ef_get_env_blob:  读 KV。value_buf 可传 NULL（只查长度）；saved_value_len 出参
 *                     返回 Flash 上的真实长度；返回实际拷入 buf 的字节数（0=没找到）。
 *                     buf_len 小于真实长度时截断读，不报错。
 *   ef_set_env_blob:  写 KV（同名覆盖，内部先写新值再删旧值，掉电安全）。即时生效，
 *                     无需再调 ef_save_env（旧版 save 系列 API 在此模式是空操作）。
 *   ef_get_env_obj / ef_read_env_value: 先取 KV 对象（元数据在 RAM），再按对象读值，
 *                     适合同一 key 反复读写的场景，省去重复查找。
 */
size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len);
bool ef_get_env_obj(const char *key, env_node_obj_t env);
size_t ef_read_env_value(env_node_obj_t env, uint8_t *value_buf, size_t buf_len);
EfErrCode ef_set_env_blob(const char *key, const void *value_buf, size_t buf_len);

/* ef_env.c, ef_env_legacy_wl.c and ef_env_legacy.c */
/*
 * 【契约】字符串版 ENV API（三套实现都提供，行为因模式而异）：
 *   ef_get_env  返回库内部静态缓冲/RAM 指针，不可长期持有，NG 模式下值必须可打印；
 *   ef_set_env  legacy 模式只改 RAM（脏标志），需 ef_save_env 落盘；NG 模式即时落盘；
 *   ef_save_env legacy 模式整区回写；NG 模式 no-op（保留仅为兼容旧代码）；
 *   ef_env_set_default  整区格式化并重建 default_env_set。
 */
EfErrCode ef_load_env(void);
void ef_print_env(void);
char *ef_get_env(const char *key);
EfErrCode ef_set_env(const char *key, const char *value);
EfErrCode ef_del_env(const char *key);
EfErrCode ef_save_env(void);
EfErrCode ef_env_set_default(void);
size_t ef_get_env_write_bytes(void);
EfErrCode ef_set_and_save_env(const char *key, const char *value);
EfErrCode ef_del_and_save_env(const char *key);
#endif

#ifdef EF_USING_IAP
/* ef_iap.c */
/*
 * 【契约】IAP 升级五步曲：erase_bak_app（清备份区）-> 分包 write_data_to_bak
 * （断点续传由 cur_size 出入参接力）-> copy_app_from_bak（备份区搬运到 App 区，
 * 搬运用 32 字*4B=128B 的栈缓冲逐块拷贝）-> 校验 -> 复位跳转。
 * Spec 系列函数允许传入自定义擦写回调（用于操作非 ef_port 管辖的 App 区）。
 */
EfErrCode ef_erase_bak_app(size_t app_size);
EfErrCode ef_erase_user_app(uint32_t user_app_addr, size_t user_app_size);
EfErrCode ef_erase_spec_user_app(uint32_t user_app_addr, size_t app_size,
                                 EfErrCode (*app_erase)(uint32_t addr, size_t size));
EfErrCode ef_erase_bl(uint32_t bl_addr, size_t bl_size);
EfErrCode ef_write_data_to_bak(uint8_t *data, size_t size, size_t *cur_size,
                               size_t total_size);
EfErrCode ef_copy_app_from_bak(uint32_t user_app_addr, size_t app_size);
EfErrCode ef_copy_spec_app_from_bak(uint32_t user_app_addr, size_t app_size,
                                    EfErrCode (*app_write)(uint32_t addr, const uint32_t *buf, size_t size));
EfErrCode ef_copy_bl_from_bak(uint32_t bl_addr, size_t bl_size);
uint32_t ef_get_bak_app_start_addr(void);
#endif

#ifdef EF_USING_LOG
/* ef_log.c */
/*
 * 【契约】日志 API：Flash 上的环形缓冲区。index 0 是"最旧日志"起点；
 *   size 必须 4 字节对齐（Flash 按字写）；写满自动从头覆盖最旧扇区。
 *   ef_log_clean 整区格式化（不可恢复，慎用）。
 */
EfErrCode ef_log_read(size_t index, uint32_t *log, size_t size);
EfErrCode ef_log_write(const uint32_t *log, size_t size);
EfErrCode ef_log_clean(void);
size_t ef_log_get_used_size(void);
size_t ef_log_get_total_size(void);
#endif

/* ef_utils.c */
/* 【契约】流式 CRC32（多项式 0xEDB88320）：首调 crc 传 0，续算传上次返回值 */
uint32_t ef_calc_crc32(uint32_t crc, const void *buf, size_t size);

/* ef_port.c */
/*
 * 【契约】移植层回调（用户在 ef_port.c 实现，见 Docs/05-移植层.md）：
 *   read/write 的 size 单位是字节，但必须是写粒度对齐（1/8/32bit 三种 Flash）；
 *   erase 的 addr 必须 EF_ERASE_MIN_SIZE 对齐；
 *   env_lock/unlock 在多任务系统中包住所有 ENV API 的临界区（单线程可为空实现）；
 *   三个打印函数接自己的串口/RTT/控制台即可。
 */
EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size);
EfErrCode ef_port_erase(uint32_t addr, size_t size);
EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size);
void ef_port_env_lock(void);
void ef_port_env_unlock(void);
void ef_log_debug(const char *file, const long line, const char *format, ...);
void ef_log_info(const char *format, ...);
void ef_print(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* EASYFLASH_H_ */
