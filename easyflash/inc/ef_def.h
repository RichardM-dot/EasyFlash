/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2019-2020, Armink, <armink.ztl@gmail.com>
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
 * Function: It is the definitions head file for this library.
 * Created on: 2019-11-20
 */

/*
 * ============================================================================
 * 【B级导读】ef_def.h —— 公共定义头：版本号 / 日志宏 / 错误码 / KV 状态机 / KV 对象
 * ----------------------------------------------------------------------------
 * 全库地基：easyflash.h 与所有 src 实现都包含它。核心三件事：
 *   1. enum env_status + struct env_node_obj：NG 模式 KV 节点的状态机与内存对象，
 *      Docs/02 的 stateDiagram 直接对应这里的 6 个状态；
 *   2. EfErrCode：全库统一错误码（API 返回值契约）；
 *   3. EF_DEBUG/EF_INFO/EF_ASSERT：日志与断言宏，行为由 PRINT_DEBUG 控制。
 * 注意：EF_ASSERT 是带续行符的多行宏，任何修改严禁在宏体中间插注释。
 * ============================================================================
 */

#ifndef EF_DEF_H_
#define EF_DEF_H_

#ifdef __cplusplus
extern "C" {
#endif

/* EasyFlash software version number */
#define EF_SW_VERSION                  "4.1.99"
#define EF_SW_VERSION_NUM              0x40199

/*
 * ENV version number defined by user.
 * Please change it when your firmware add a new ENV to default_env_set.
 */
#ifndef EF_ENV_VER_NUM
#define EF_ENV_VER_NUM                 0
#endif

/* the ENV max name length must less then it */
#ifndef EF_ENV_NAME_MAX
#define EF_ENV_NAME_MAX                          32
#endif

/* EasyFlash debug print function. Must be implement by user. */
#ifdef PRINT_DEBUG
#define EF_DEBUG(...) ef_log_debug(__FILE__, __LINE__, __VA_ARGS__)
#else
#define EF_DEBUG(...)
#endif
/* EasyFlash routine print function. Must be implement by user. */
#define EF_INFO(...)  ef_log_info(__VA_ARGS__)
/* EasyFlash assert for developer. */
#define EF_ASSERT(EXPR)                                                       \
if (!(EXPR))                                                                  \
{                                                                             \
    EF_DEBUG("(%s) has assert failed at %s.\n", #EXPR, __FUNCTION__);         \
    while (1);                                                                \
}

typedef struct _ef_env {
    char *key;
    void *value;
    size_t value_len;
} ef_env, *ef_env_t;

/* EasyFlash error code */
/*
 * 【深注】全库错误码契约：0 恒为成功；1~3 是移植层 IO 三兄弟；4~7 是 ENV 业务错。
 * 上层可直接 switch 这些值做重试/降级策略。
 */
typedef enum {
    EF_NO_ERR,
    EF_ERASE_ERR,
    EF_READ_ERR,
    EF_WRITE_ERR,
    EF_ENV_NAME_ERR,
    EF_ENV_NAME_EXIST,
    EF_ENV_FULL,
    EF_ENV_INIT_FAILED,
} EfErrCode;

/* the flash sector current status */
/* 【深注】legacy 体系用的扇区三态（空/在用/满）；NG 模式另有更细的双轴状态见 ef_env.c */
typedef enum {
    EF_SECTOR_EMPTY,
    EF_SECTOR_USING,
    EF_SECTOR_FULL,
} EfSecrorStatus; /* 拼写笔误 EfSecror（原样保留），仅本枚举，无碍使用 */

/*
 * 【深注】KV 节点状态机（NG 模式，status table 按写粒度逐位/逐格落盘，
 * 只能沿"写入方向"前进，天然单调——这是追加写掉电安全的基础）：
 *   ENV_UNUSED     0xFF..：擦除态，此节点从未写过；
 *   ENV_PRE_WRITE  已写状态表但头/数据未写完（掉电在此状态 -> 上电标 ENV_ERR_HDR 作废）；
 *   ENV_WRITE      完整有效（CRC 过），set/get 只认这个状态；
 *   ENV_PRE_DELETE 已标记待删（同名覆盖时先打此标记；掉电 -> 上电 move_env 恢复旧值）；
 *   ENV_DELETED    彻底删除（GC 搬运时新副本已写好才打这个标）；
 *   ENV_ERR_HDR    头部错误（长度非法/半写），节点作废但保留元数据不撞坏相邻数据。
 */
enum env_status {
    ENV_UNUSED,
    ENV_PRE_WRITE,
    ENV_WRITE,
    ENV_PRE_DELETE,
    ENV_DELETED,
    ENV_ERR_HDR,
    ENV_STATUS_NUM, /* 状态总数：决定 status table 大小（见 ef_env.c 的 STATUS_TABLE_SIZE） */
};
typedef enum env_status env_status_t;

/*
 * 【深注】env_node_obj：一条 KV 在 RAM 中的"元数据视图"（不含值本体，值留在 Flash）。
 * name[EF_ENV_NAME_MAX] 是内嵌数组而非指针——find_env 读头时一并读入，避免二次读闪；
 * addr.start/value 分别指向节点头与值本体（Flash 绝对地址），ef_read_env_value 据此读值。
 */
struct env_node_obj {
    env_status_t status;                         /**< ENV node status, @see node_status_t */
    bool crc_is_ok;                              /**< ENV node CRC32 check is OK */
    uint8_t name_len;                            /**< name length */
    uint32_t magic;                              /**< magic word(`K`, `V`, `4`, `0`) */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t value_len;                          /**< value length */
    char name[EF_ENV_NAME_MAX];                  /**< name */
    struct {
        uint32_t start;                          /**< ENV node start address */
        uint32_t value;                          /**< value start address */
    } addr;
};
typedef struct env_node_obj *env_node_obj_t;

#ifdef __cplusplus
}
#endif

#endif /* EF_DEF_H_ */
