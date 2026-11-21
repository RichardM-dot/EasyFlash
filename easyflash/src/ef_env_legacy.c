/*
 * 【中文导读】ef_env_legacy.c —— ENV 旧版实现：文件模式（B级导读）
 * 依赖文件系统（FAT 等）存 env，把 KV 写进普通文件。了解即可。
 */

/*
 * 【勘误与深读地图（A级深注补充）】上一块导读的"文件模式"说法有误：本文件是
 * legacy 体系中的 "normal 模式"（normal mode，见原英文文件头 Function 注释），
 * 不依赖任何文件系统，直接操作裸 Flash。legacy 三件套关系如下：
 *
 *   ef_env.c            V4.0 NG 模式：追加写 + 双扇区 GC，RAM 占用趋近 0（GBK 编码文件）
 *   ef_env_legacy_wl.c  legacy 磨损均衡模式：数据可在多个扇区间迁移，写放大居中
 *   ef_env_legacy.c     legacy normal 模式（本文件）：RAM 全缓存 + 整区擦写，最简单
 *
 * normal 模式核心思路——"把 Flash 当成 RAM 的落盘镜像"：
 *   1. 所有 KV 以紧凑的 "key=value\0" 文本流缓存在 env_cache[]（RAM）中，4 字节对齐；
 *   2. 系统区只存 4 个参数字（结束地址/保存计数/版本号/CRC32，见下方 enum）；
 *   3. ef_set_env 只改 RAM（env_cache_changed 置位），ef_save_env 才"擦整个区->整块写回"；
 *   4. 可选 EF_ENV_USING_PFS_MODE：ENV 区对半分 Area0/Area1 乒乓写，保存计数大者
 *      有效——擦写中途掉电时另一区仍是上一次的完整副本（两步式原子提交）。
 * 任务书所述 kg 用户配置模式在本版本中无独立实现文件（早期 EasyFlash 的 ef_kg.c
 * 已并入 env 体系），故 kg 相关说明统一合并至本文件与 KB 条目。
 */

/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2014-2018, Armink, <armink.ztl@gmail.com>
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
 * Function: Environment variables operating interface. (normal mode)
 * Created on: 2014-10-06
 */

#include <easyflash.h>
#include <string.h>
#include <stdlib.h>

#if defined(EF_USING_ENV) && defined(EF_ENV_USING_LEGACY_MODE)

#ifndef EF_ENV_USING_WL_MODE

#if defined(EF_USING_ENV) && (!defined(ENV_USER_SETTING_SIZE) || !defined(ENV_AREA_SIZE))
#error "Please configure user setting ENV size or ENV area size (in ef_cfg.h)"
#endif

/**
 * ENV area has 2 sections
 * 1. System section
 *    It storages ENV parameters. (Units: Word)
 * 2. Data section
 *    It storages all ENV. Storage format is key=value\0.
 *    All ENV must be 4 bytes alignment. The remaining part must fill '\0'.
 *
 * @note Word = 4 Bytes in this file
 * @note When using power fail safeguard mode, it has two ENV areas(Area0, Area1).
 */

/*
 * 【深注】系统区（System section）布局：固定在 ENV 区起始处，按字（Word=4B）索引。
 *
 *   字偏移 0            1(optional)   2(optional)   3(optional)   4(optional)   ...
 *        +-------------+-------------+-------------+-------------+-------------+
 *        | ENV 结束地址 | 保存计数(PFS)| 版本号(自动  | 数据段CRC32 |   数据段     |
 *        | (必选)       |             | 更新时)      | (必选，居末)  | key=value\0 |
 *        +-------------+-------------+-------------+-------------+-------------+
 *        |<-- ENV_PARAM_WORD_SIZE 个字 -->|
 *
 * 【深注】数据段（Data section）紧跟系统区之后，是若干条 "key=value\0" 的紧凑串联，
 * 每条按 4 字节对齐、尾部用 '\0' 填充。整段由系统区里的 CRC32 一次性保护。
 */
/* flash ENV parameters index and size in system section */
enum {
    /* data section ENV end address index in system section */
    ENV_PARAM_INDEX_END_ADDR = 0,

#ifdef EF_ENV_USING_PFS_MODE
    /* saved count for ENV area */
    ENV_PARAM_INDEX_SAVED_COUNT,
#endif

#ifdef EF_ENV_AUTO_UPDATE
    /* current version number for ENV */
    ENV_PARAM_INDEX_VER_NUM,
#endif

    /* data section CRC32 code index in system section */
    ENV_PARAM_INDEX_DATA_CRC,
    /* flash ENV parameters word size */
    ENV_PARAM_WORD_SIZE,
    /* flash ENV parameters byte size */
    ENV_PARAM_BYTE_SIZE = ENV_PARAM_WORD_SIZE * 4,
};

/* default ENV set, must be initialized by user */
/* 【深注】用户默认 ENV 表：ef_port_init() 传入，仅 ef_env_set_default()/自动升级时逐条 create_env */
static ef_env const *default_env_set;
/* default ENV set size, must be initialized by user */
static size_t default_env_set_size = 0;
/* ENV ram cache */
/* 【深注】核心全局：整个 ENV 区的 RAM 镜像。字数组保证对齐访问；系统区参数占前几字，
 * 数据段 "key=value\0" 从 env_cache + ENV_PARAM_WORD_SIZE 开始（本文件系统区 4 字定长） */
static uint32_t env_cache[ENV_USER_SETTING_SIZE / 4] = { 0 };
/* ENV start address in flash */
/* 【深注】ENV 区在 Flash 上的物理首地址（= EF_START_ADDR），ef_env_init 时赋值一次 */
static uint32_t env_start_addr = 0;
/* ENV ram cache has changed when ENV created, deleted and changed value. */
/* 【深注】脏标志：legacy 模式"延迟写"的开关。set/del 只改 RAM 并置位，ef_save_env 看到才落盘 */
static bool env_cache_changed = false;
/* initialize OK flag */
static bool init_ok = false; /* 初始化完成前拒绝所有 API，防止对未挂载数据误操作 */

#ifdef EF_ENV_USING_PFS_MODE
/* current load ENV area address */
/* 【深注】PFS 掉电保护模式：Area0/Area1 乒乓。cur_load_area_addr 指向当前有效区（读它） */
static uint32_t cur_load_area_addr = 0;
/* next save ENV area address */
/* 【深注】next_save_area_addr 指向下一次保存的目标区（写它）：先擦写副本，成功后指针互换，
 * 保证任何时刻至少有一个完整副本——两步式提交，掉电后上电按 saved_count 大者恢复 */
static uint32_t next_save_area_addr = 0;
#endif

static uint32_t get_env_system_addr(void);
static uint32_t get_env_data_addr(void);
static uint32_t get_env_end_addr(void);
static void set_env_end_addr(uint32_t end_addr);
static EfErrCode write_env(const char *key, const char *value);
static char *find_env(const char *key);
static EfErrCode del_env(const char *key);
static size_t get_env_data_size(void);
static size_t get_env_user_used_size(void);
static EfErrCode create_env(const char *key, const char *value);
static uint32_t calc_env_crc(void);
static bool env_crc_is_ok(void);
#ifdef EF_ENV_AUTO_UPDATE
static EfErrCode env_auto_update(void);
#endif

/**
 * Flash ENV initialize.
 * 【深注】ef_env_init() —— ENV 模块挂载：参数体检 + 记录基址 + 读盘恢复。
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV set size
 * 执行流：EF_ASSERT 一组编译期可查的布局约束（4 字节对齐、区域大小与擦除粒度的
 * 整除关系，PFS 模式要求 ENV_AREA_SIZE 恰为用户设置区两倍）-> 保存 env_start_addr
 * 与默认 ENV 表 -> ef_load_env() 读 Flash 判定"首次使用/脏数据/正常"三种情况 ->
 * 可选 env_auto_update() 按 EF_ENV_VER_NUM 增量补齐新版本默认 ENV -> init_ok 置位。
 *
 * @note  断言失败的常规原因是 ef_cfg.h 里 ENV_USER_SETTING_SIZE / ENV_AREA_SIZE /
 *        EF_ERASE_MIN_SIZE 三者配比不满足本文件头部的布局公式（normal 模式 1:1，
 *        PFS 模式 1:2），上电即死循环便于第一时间暴露配置错误。
 *
 * @param default_env 用户默认 ENV 表（来自 ef_port.c）
 * @param default_env_size 默认 ENV 表条数
 *
 * @note user_size must equal with total_size in normal mode
 *
 * @return result
 */
EfErrCode ef_env_init(ef_env const *default_env, size_t default_env_size) {
    EfErrCode result = EF_NO_ERR;

    EF_ASSERT(ENV_AREA_SIZE);
    EF_ASSERT(ENV_USER_SETTING_SIZE);
    EF_ASSERT(EF_ERASE_MIN_SIZE);
    /* must be word alignment for ENV */
    EF_ASSERT(ENV_USER_SETTING_SIZE % 4 == 0);
    EF_ASSERT(ENV_AREA_SIZE % 4 == 0);
    EF_ASSERT(default_env);
    EF_ASSERT(default_env_size < ENV_USER_SETTING_SIZE);

#ifndef EF_ENV_USING_PFS_MODE
    /* total_size must be aligned with erase_min_size */
    if (ENV_USER_SETTING_SIZE % EF_ERASE_MIN_SIZE == 0) {
        EF_ASSERT(ENV_USER_SETTING_SIZE == ENV_AREA_SIZE);
    } else {
        EF_ASSERT((ENV_USER_SETTING_SIZE / EF_ERASE_MIN_SIZE + 1)*EF_ERASE_MIN_SIZE == ENV_AREA_SIZE);
    }
#else
    /* total_size must be aligned with erase_min_size */
    if (ENV_USER_SETTING_SIZE % EF_ERASE_MIN_SIZE == 0) {
        /* it has double area when used power fail safeguard mode */
        EF_ASSERT(2 * ENV_USER_SETTING_SIZE == ENV_AREA_SIZE);
    } else {
        /* it has double area when used power fail safeguard mode */
        EF_ASSERT(2 * (ENV_USER_SETTING_SIZE / EF_ERASE_MIN_SIZE + 1)*EF_ERASE_MIN_SIZE == ENV_AREA_SIZE);
    }
#endif

    env_start_addr = EF_START_ADDR;
    default_env_set = default_env;
    default_env_set_size = default_env_size;

    EF_DEBUG("ENV start address is 0x%08X, size is %d bytes.\n", EF_START_ADDR, ENV_AREA_SIZE);

    result = ef_load_env();

#ifdef EF_ENV_AUTO_UPDATE
    if (result == EF_NO_ERR) {
        env_auto_update();
    }
#endif

    if (result == EF_NO_ERR) {
        init_ok = true;
    }


    return result;
}

/**
 * ENV set default.
 * 【深注】ef_env_set_default() —— 格式化重建：把 ENV 区恢复为出厂默认参数。
 *
 * 触发场景（都在 ef_load_env 内）：首次上电（结束地址为 0xFFFFFFFF）、Flash 内容
 * 越界/非法、CRC32 校验失败、PFS 双区全部无效。它不直接擦 Flash，而是：
 *   1) 锁缓存 -> 把 RAM 里的结束地址重置到数据段起点（等效"清空数据段"）；
 *   2) 逐条 create_env 把 default_env_set 写进 RAM 缓存；
 *   3) ef_save_env() 落盘（PFS 模式连写两次，把另一块乒乓区也重置干净）。
 *
 * @note  这是"RAM 缓存 + 整区回写"架构的好处：重建默认值不需要先擦 Flash，
 *        只需重放默认表再统一落盘一次。
 *
 * @return result
 */
EfErrCode ef_env_set_default(void) {
    extern EfErrCode ef_env_ver_num_set_default(void);

    EfErrCode result = EF_NO_ERR;
    size_t i;

    EF_ASSERT(default_env_set);
    EF_ASSERT(default_env_set_size);

    /* lock the ENV cache */
    ef_port_env_lock();

    /* set environment end address is at data section start address */
    set_env_end_addr(get_env_data_addr());

#ifdef EF_ENV_USING_PFS_MODE
    /* set saved count to default 0 */
    env_cache[ENV_PARAM_INDEX_SAVED_COUNT] = 0;
#endif

#ifdef EF_ENV_AUTO_UPDATE
    /* initialize version number */
    env_cache[ENV_PARAM_INDEX_VER_NUM] = EF_ENV_VER_NUM;
#endif

    /* create default ENV */
    for (i = 0; i < default_env_set_size; i++) {
        create_env(default_env_set[i].key, default_env_set[i].value);
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    result = ef_save_env();

#ifdef EF_ENV_USING_PFS_MODE
    /* reset other PFS area's data */
    if (result == EF_NO_ERR) {
        env_cache_changed = true;
        result = ef_save_env();
    }
#endif

    return result;
}

/**
 * Get ENV system section start address.
 * 【深注】get_env_system_addr() —— 系统区首地址（普通模式=ENV 区首，PFS 模式=当前有效区首）。
 * 所有"读系统区参数/CRC"都以它为基准，PFS 下自然跟随乒乓切换。
 *
 * @return system section start address
 */
static uint32_t get_env_system_addr(void) {
#ifndef EF_ENV_USING_PFS_MODE
    return env_start_addr;
#else
    return cur_load_area_addr;
#endif
}

/**
 * Get ENV data section start address.
 * 【深注】get_env_data_addr() —— 数据段首地址 = 系统区首 + 系统区定长 4 字。
 * "结束地址 - 数据段首" 即数据段有效长度，是 find_env 扫描的右边界。
 *
 * @return data section start address
 */
static uint32_t get_env_data_addr(void) {
    return get_env_system_addr() + ENV_PARAM_BYTE_SIZE;
}

/**
 * Get ENV end address.
 * It's the first word in ENV.
 * 【深注】get_env_end_addr() —— 读"ENV 结束地址"参数字（系统区第 0 字，存的是
 * 绝对 Flash 地址）。它是整个模型的游标：上电从 Flash 读回，运行期在 RAM 维护，
 * 落盘时随数据段一起写回。0xFFFFFFFF 即"从未初始化"。
 *
 * @return ENV end address
 */
static uint32_t get_env_end_addr(void) {
    /* it is the first word */
    return env_cache[ENV_PARAM_INDEX_END_ADDR];
}

/**
 * Set ENV end address.
 * It's the first word in ENV.
 * 【深注】set_env_end_addr() —— 写"ENV 结束地址"游标（只改 RAM，落盘靠 ef_save_env）。
 * write_env/del_env/ef_env_set_default 三处调用它推进/回退游标。
 *
 * @param end_addr ENV end address
 */
static void set_env_end_addr(uint32_t end_addr) {
    env_cache[ENV_PARAM_INDEX_END_ADDR] = end_addr;
}

/**
 * Get current ENV data section size.
 * 【深注】get_env_data_size() —— 数据段有效字节数 = 结束地址 - 数据段首。
 * 是 CRC 计算、find_env 扫描、del_env 前移搬移量的共同依据。
 *
 * @return size
 */
static size_t get_env_data_size(void) {
    if (get_env_end_addr() > get_env_data_addr()) {
        return get_env_end_addr() - get_env_data_addr();
    } else {
        return 0;
    }
}

/**
 * Get current user used ENV size.
 * 【深注】get_env_user_used_size() —— 系统区+数据段的总占用（结束地址 - 系统区首）。
 * 注意与 get_env_data_size 的区别：本值包含系统区那几个参数字，是落盘 write_size。
 *
 * @return bytes
 */
static size_t get_env_user_used_size(void) {
    if (get_env_end_addr() > get_env_system_addr()) {
        return get_env_end_addr() - get_env_system_addr();
    } else {
        return 0;
    }
}

/**
 * Get current ENV already write bytes.
 * 【深注】ef_get_env_write_bytes() —— 统计口径接口：本模式实际占用多少 Flash 写入量。
 * 普通模式 = 用户已用字节；PFS 模式 ×2（两块乒乓区都占）。供 ef_print_env 展示。
 *
 * @return write bytes
 */
size_t ef_get_env_write_bytes(void) {
#ifndef EF_ENV_USING_PFS_MODE
    return get_env_user_used_size();
#else
    return get_env_user_used_size() * 2;
#endif
}

/**
 * Write an ENV at the end of cache.
 * 【深注】write_env() —— 在 RAM 缓存末尾追加一条 "key=value\0" 记录（含 4 字节对齐）。
 *
 * 存储格式（每条记录）：
 *   | key | '=' | value | '\0'(结束) | '\0'...（填充到 4 字节对齐） |
 * 记录长度 env_str_len = key_len + value_len + 2 向上取整到 4 的倍数；填充字节必须是
 * '\0'——find_env 靠 strlen 跳记录、ef_print_env 靠 '\0' 换行，填充字节参与扫描。
 *
 * @note  只改 RAM：追加成功即置脏标志 env_cache_changed，真正的 Flash 写入统一由
 *        ef_save_env() 完成（legacy 模式的"延迟写"语义）。
 * @note  容量判断用 >= 而非 >：给"结束地址"游标本身至少留 1 字节余量，防止写满后
 *        get_env_user_used_size 越过缓存边界。
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
static EfErrCode write_env(const char *key, const char *value) {
    EfErrCode result = EF_NO_ERR;
    size_t key_len = strlen(key), value_len = strlen(value), env_str_len;
    char *env_cache_bak = (char *)env_cache;

    /* calculate ENV storage length, contain '=' and '\0'. */
    env_str_len = key_len + value_len + 2;
    if (env_str_len % 4 != 0) {
        env_str_len = (env_str_len / 4 + 1) * 4; /* 向上取整到 4 字节对齐（KV 流必须字对齐） */
    }
    /* check capacity of ENV  */
    if (env_str_len + get_env_user_used_size() >= ENV_USER_SETTING_SIZE) {
        return EF_ENV_FULL; /* 容量不足：立即拒绝，绝不部分写入（保持 RAM 缓存一致性） */
    }

    /* calculate current ENV ram cache end address */
    env_cache_bak += get_env_user_used_size(); /* 追加点 = 系统区 + 已有数据段之后 */

    /* copy key name */
    memcpy(env_cache_bak, key, key_len);
    env_cache_bak += key_len;
    /* copy equal sign */
    *env_cache_bak = '='; /* '=' 是 key 与 value 的分隔符，也用于 get 时定位 value */
    env_cache_bak++;
    /* copy value */
    memcpy(env_cache_bak, value, value_len);
    env_cache_bak += value_len;
    /* fill '\0' for string end sign */
    *env_cache_bak = '\0'; /* 记录结束符：strnlen/strlen 扫描的依据 */
    env_cache_bak ++;
    /* fill '\0' for word alignment */
    memset(env_cache_bak, 0, env_str_len - (key_len + value_len + 2)); /* 填充区必须为 0，见头部格式说明 */
    set_env_end_addr(get_env_end_addr() + env_str_len); /* 游标前移一条记录 */
    /* ENV ram cache has changed */
    env_cache_changed = true; /* 置脏：等待 ef_save_env 统一落盘 */

    return result;
}

/**
 * Find ENV.
 * 【深注】find_env() —— 在 RAM 数据段里线性扫描 "key=value\0" 流找记录。
 *
 * 扫描方式：从数据段首开始，每条记录先 strncmp(key, env, key_len) 比对前缀、再查
 * env[key_len]=='=' 确认整名命中（防止 "temp" 误配 "temp_humi"），未命中则用
 * strlen+1 向上取整 4 字节对齐跳到下一条。复杂度 O(记录数×平均长度)——RAM 内
 * memcpy 级别速度，这正是 legacy 模式不需要 NG 模式那套 name-CRC 缓存表的原因。
 *
 * @param key ENV name
 *
 * @return found ENV in ram cache
 * @return found ENV in ram cache（命中返回记录首地址，未命中/参数非法返回 NULL）
 */
static char *find_env(const char *key) {
    char *env_start, *env_end, *env, *found_env = NULL;
    size_t key_len = strlen(key), env_len;

    if ((key == NULL) || *key == '\0') {
        EF_INFO("Flash ENV name must be not empty!\n");
        return NULL;
    }

    /* from data section start to data section end */
    env_start = (char *) ((char *) env_cache + ENV_PARAM_BYTE_SIZE);
    env_end = (char *) ((char *) env_cache + get_env_user_used_size());

    /* ENV is null */
    if (env_start == env_end) {
        return NULL;
    }

    env = env_start;
    while (env < env_end) {
        /* the key length must be equal */
        if (!strncmp(env, key, key_len) && (env[key_len] == '=')) {
            found_env = env;
            break;
        } else {
            /* calculate ENV length, contain '\0'. */
            env_len = strlen(env) + 1;
            /* next ENV and word alignment */
            if (env_len % 4 == 0) {
                env += env_len;
            } else {
                env += (env_len / 4 + 1) * 4;
            }
        }
    }
    return found_env;
}

/**
 * If the ENV is not exist, create it.
 * @see flash_write_env
 * 【深注】create_env() —— 创建入口：三道合法性闸门后转交 write_env 追加到缓存末尾。
 * 闸门：key 非空 -> key 不含 '='（会破坏 key=value 文本协议）-> 同名不存在
 * （normal 模式创建即报"已存在"，改名覆盖由 ef_set_env 的 del+recreate 组合完成）。
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
static EfErrCode create_env(const char *key, const char *value) {
    EfErrCode result = EF_NO_ERR;

    EF_ASSERT(key);
    EF_ASSERT(value);

    if ((key == NULL) || *key == '\0') {
        EF_INFO("Flash ENV name must be not empty!\n");
        return EF_ENV_NAME_ERR;
    }

    if (strchr(key, '=')) {
        EF_INFO("Flash ENV name can't contain '='.\n");
        return EF_ENV_NAME_ERR;
    }

    /* find ENV */
    if (find_env(key)) {
        EF_INFO("The name of \"%s\" is already exist.\n", key);
        return EF_ENV_NAME_EXIST;
    }
    /* write ENV at the end of cache */
    result = write_env(key, value);

    return result;
}

/**
 * Delete an ENV in cache.
 * 【深注】del_env() —— 删除记录：本质是"RAM 内 memmove 前移补洞 + 游标回退"。
 *
 * 因为数据段是紧凑串联的文本流，删掉中间一条必须把后面所有记录整体前移
 * del_env_length 字节，随后结束地址游标同步回退——删除不产生碎片，但代价是
 * O(剩余数据量) 的搬运（只在 RAM 做，落盘仍是整块回写）。
 *
 * @note  读取记录长度时 strlen+1 后向上取整 4 字节，必须与 write_env 的对齐规则
 *         完全一致，否则前移量算错会把数据段扫乱。
 *
 * @param key ENV name
 *
 * @return result
 */
static EfErrCode del_env(const char *key) {
    EfErrCode result = EF_NO_ERR;
    char *del_env = NULL;
    size_t del_env_length, remain_env_length;

    EF_ASSERT(key);

    if ((key == NULL) || *key == '\0') {
        EF_INFO("Flash ENV name must be not NULL!\n");
        return EF_ENV_NAME_ERR;
    }

    if (strchr(key, '=')) {
        EF_INFO("Flash ENV name or value can't contain '='.\n");
        return EF_ENV_NAME_ERR;
    }

    /* find ENV */
    del_env = find_env(key);

    if (!del_env) {
        EF_INFO("Not find \"%s\" in ENV.\n", key);
        return EF_ENV_NAME_ERR;
    }
    del_env_length = strlen(del_env);
    /* '\0' also must be as ENV length */
    del_env_length ++;
    /* the address must multiple of 4 */
    if (del_env_length % 4 != 0) {
        del_env_length = (del_env_length / 4 + 1) * 4;
    }
    /* calculate remain ENV length */
    remain_env_length = get_env_data_size()
                        - (((uint32_t) del_env + del_env_length) - ((uint32_t) env_cache + ENV_PARAM_BYTE_SIZE));
    /* remain ENV move forward */
    memcpy(del_env, del_env + del_env_length, remain_env_length);
    /* reset ENV end address */
    set_env_end_addr(get_env_end_addr() - del_env_length);
    /* ENV ram cache has changed */
    env_cache_changed = true;

    return result;
}

/**
 * Set an ENV.If it value is NULL, delete it.
 * If not find it in ENV table, then create it.
 * 【深注】ef_set_env() —— 写入口：同名覆盖 = "先 del 后 create" 的组合原语。
 *
 * 决策树：value==NULL 即删除；已存在且值相同 -> 静默 no-op（省一次擦写，对 Flash
 * 寿命很重要）；已存在且值不同 -> del_env 补洞 + create_env 追加到尾部（值变化
 * 时记录会搬到数据段末尾，天然把"热点数据"聚在一起）；不存在 -> 直接创建。
 *
 * @note  整个决策过程被 ef_port_env_lock/unlock 包裹：多任务并发调用时保证
 *        "查-删-建"的原子性（锁的实现由移植层决定：关调度/互斥量/关中断）。
 * @note  本函数只改 RAM；掉电安全靠之后的 ef_save_env（PFS 模式下乒乓双副本）。
 *
 * @param key ENV name
 * @param value ENV value
 * @param value ENV value（传 NULL 等效删除）
 *
 * @return result
 */
EfErrCode ef_set_env(const char *key, const char *value) {
    EfErrCode result = EF_NO_ERR;
    char *old_env, *old_value;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED; /* 未初始化直接拒绝，防止对全 0 RAM 缓存误操作 */
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    /* if ENV value is NULL, delete it */
    if (value == NULL) {
        result = del_env(key); /* 约定：set(key, NULL) = del(key)，兼容旧 API 习惯 */
    } else {
        old_env = find_env(key);
        /* If find this ENV, then compare the new value and old value. */
        if (old_env) {
            /* find the old value address */
            old_env = strchr(old_env, '='); /* 定位 '=' 后即是旧值首字符 */
            old_value = old_env + 1;
            /* If it is changed then delete it and recreate it  */
            if (strcmp(old_value, value)) { /* 值相同则不写：Flash 擦写寿命宝贵 */
                result = del_env(key);
                if (result == EF_NO_ERR) {
                    result = create_env(key, value); /* 删旧建新，新记录追加到数据段尾部 */
                }
            }
        } else {
            result = create_env(key, value);
        }
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

/**
 * Del an ENV.
 * 【深注】ef_del_env() —— 删除的对外封装：init 检查 + 缓存锁，内部转 del_env。
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_env(const char *key) {
    EfErrCode result = EF_NO_ERR;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    result = del_env(key);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

/**
 * Get an ENV value by key name.
 * 【深注】ef_get_env() —— 读入口：返回 RAM 缓存内 value 的指针（零拷贝、零 Flash 读）。
 *
 * @note  返回的是 env_cache 内部地址，不是独立缓冲：生命周期属于库本身，
 *        下一次 set/del 可能令其失效，调用方不要长期持有或 free。
 * @note  legacy 模式只存字符串，所以这里能用 strchr('=') 直接定位 value；
 *        V4.0 NG 模式存任意 blob，对应 API 变成了 ef_get_env_blob()。
 *
 * @param key ENV name
 *
 * @return value
 * @return value（未找到返回 NULL）
 */
char *ef_get_env(const char *key) {
    char *env = NULL, *value = NULL;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return NULL;
    }

    /* find ENV */
    env = find_env(key);

    if (env == NULL) {
        return NULL;
    }
    /* get value address */
    value = strchr(env, '=');
    if (value != NULL) {
        /* the equal sign next character is value */
        value++;
    }
    return value;
}
/**
 * Print ENV.
 * 【深注】ef_print_env() —— 调试利器：把 RAM 数据段按字节解包逐条打印 "key=value"。
 *
 * 实现要点：数据段是字数组，必须按小端逐字节右移 (8*j) 还原字符流；遇 '\0' 打印
 * 换行表示一条记录结束（填充区全 0，最多只打印一次换行）。尾部附带模式名、
 * 用量统计，PFS/自动更新启用时还打印保存计数与版本号，是现场排查的第一入口。
 */
void ef_print_env(void) {
    uint32_t *env_cache_data_addr = env_cache + ENV_PARAM_WORD_SIZE,
              *env_cache_end_addr =
                  (uint32_t *) (env_cache + ENV_PARAM_WORD_SIZE + get_env_data_size() / 4);
    uint8_t j;
    char c;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }

    for (; env_cache_data_addr < env_cache_end_addr; env_cache_data_addr += 1) {
        for (j = 0; j < 4; j++) {
            c = (*env_cache_data_addr) >> (8 * j);
            ef_print("%c", c);
            if (c == '\0') {
                ef_print("\n");
                break;
            }
        }
    }

#ifndef EF_ENV_USING_PFS_MODE
    ef_print("\nmode: normal\n");
    ef_print("size: %ld/%ld bytes.\n", get_env_user_used_size(), ENV_USER_SETTING_SIZE);
#else
    ef_print("\nmode: power fail safeguard\n");
    ef_print("size: %ld/%ld bytes, write bytes %ld/%ld.\n", get_env_user_used_size(),
             ENV_USER_SETTING_SIZE, ef_get_env_write_bytes(), ENV_AREA_SIZE);
    ef_print("saved count: %ld\n", env_cache[ENV_PARAM_INDEX_SAVED_COUNT]);
#endif

#ifdef EF_ENV_AUTO_UPDATE
    ef_print("ver num: %d\n", env_cache[ENV_PARAM_INDEX_VER_NUM]);
#endif
}

/**
 * Load flash ENV to ram.
 * 【深注】ef_load_env()（普通模式版）—— 上电挂载的三级判定，是掉电恢复第一环。
 *
 * 判定链（读 Flash 结束地址游标 -> 决定命运）：
 *   1. 游标为 0xFFFFFFFF（全新芯片）或越界（<[区首] 或 >[区首+设置区大小]）
 *      -> 判为"未初始化/脏数据"，直接 ef_env_set_default() 重建；
 *   2. 游标合法 -> 先恢复游标，再把数据段从 Flash 读回 RAM、读回 CRC 参数字；
 *   3. 数据段 CRC32 校验失败（掉电半写/位翻转）-> 警告并整区重建默认值。
 *
 * @note  普通模式没有双副本，CRC 失败即丢全部用户参数回退默认值——这是它
 *        与 PFS/NG 模式掉电安全等级的本质差距，也是 legacy 被弃用的原因之一。
 *
 * @return result
 */
#ifndef EF_ENV_USING_PFS_MODE
EfErrCode ef_load_env(void) {
    EfErrCode result = EF_NO_ERR;
    uint32_t *env_cache_bak, env_end_addr;

    /* read ENV end address from flash */
    ef_port_read(get_env_system_addr() + ENV_PARAM_INDEX_END_ADDR * 4, &env_end_addr, 4);
    /* if ENV is not initialize or flash has dirty data, set default for it */
    if ((env_end_addr == 0xFFFFFFFF) || (env_end_addr < env_start_addr)
            || (env_end_addr > env_start_addr + ENV_USER_SETTING_SIZE)) {
        result = ef_env_set_default(); /* 一级判定：游标非法即视为未格式化，重建 */
    } else {
        /* set ENV end address */
        set_env_end_addr(env_end_addr); /* 游标先就位，后续读长度才有依据 */

        env_cache_bak = env_cache + ENV_PARAM_WORD_SIZE;
        /* read all ENV from flash */
        ef_port_read(get_env_data_addr(), env_cache_bak, get_env_data_size()); /* 数据段整体回 RAM */
        /* read ENV CRC code from flash */
        ef_port_read(get_env_system_addr() + ENV_PARAM_INDEX_DATA_CRC * 4,
                     &env_cache[ENV_PARAM_INDEX_DATA_CRC] , 4); /* CRC 参数字最后一个读入 */
        /* if ENV CRC32 check is fault, set default for it */
        if (!env_crc_is_ok()) {
            EF_INFO("Warning: ENV CRC check failed. Set it to default.\n");
            result = ef_env_set_default(); /* 三级判定：CRC 不过=数据不可信，重建 */
        }
    }
    return result;
}
#else
/**
 * 【深注】ef_load_env()（PFS 掉电保护版）—— 双区仲裁：选出"最后一份完整体"。
 *
 * 仲裁流水线（对 Area0/Area1 各跑一遍，4 张选票逐级否决）：
 *   票1 结束地址游标合法性（0xFFFFFFFF/越界即否决）
 *   票2 数据段 CRC32 校验（读入 RAM 后 env_crc_is_ok，含游标+计数+数据段）
 *   票3 双区都活下来 -> 比 saved_count：计数大者是新写入的完整体（0 与 0xFFFFFFFF
 *       的特判处理了 32 位回绕，等价于"区0 更新"）
 *   双双落选 -> 视为未初始化，ef_env_set_default() 重建两区。
 *
 * @note  每区读到 RAM 里做 CRC 时 cur_load_area_addr 已被临时指向该区——
 *        calc_env_crc 内部按 cur_load_area_addr 取数，所以两区可共用同一校验函数。
 * @note  仲裁结果是双区乒乓的"起点复位"：cur=胜者区，next=败者区，下轮保存
 *        自然覆盖旧副本。这正是"擦写中途掉电最多损失一次保存，不损失历史"的根基。
 *
 * @return result
 */
EfErrCode ef_load_env(void) {
    EfErrCode result = EF_NO_ERR;
    uint32_t area0_start_address = env_start_addr, area1_start_address = env_start_addr
                                   + ENV_AREA_SIZE / 2; /* ENV 区对半劈成两个独立区 */
    uint32_t area0_end_addr, area1_end_addr, area0_crc, area1_crc, area0_saved_count, area1_saved_count;
    bool area0_is_valid = true, area1_is_valid = true;
    /* read ENV area end address from flash */
    ef_port_read(area0_start_address + ENV_PARAM_INDEX_END_ADDR * 4, &area0_end_addr, 4);
    ef_port_read(area1_start_address + ENV_PARAM_INDEX_END_ADDR * 4, &area1_end_addr, 4);
    if ((area0_end_addr == 0xFFFFFFFF) || (area0_end_addr < area0_start_address)
            || (area0_end_addr > area0_start_address + ENV_USER_SETTING_SIZE)) {
        area0_is_valid = false; /* 票1：区0 游标非法 */
    }
    if ((area1_end_addr == 0xFFFFFFFF) || (area1_end_addr < area1_start_address)
            || (area1_end_addr > area1_start_address + ENV_USER_SETTING_SIZE)) {
        area1_is_valid = false; /* 票1：区1 游标非法 */
    }
    /* check area0 CRC when it is valid */
    if (area0_is_valid) {
        /* read ENV area0 crc32 code from flash */
        ef_port_read(area0_start_address + ENV_PARAM_INDEX_DATA_CRC * 4, &area0_crc, 4);
        /* read ENV from ENV area0 */
        ef_port_read(area0_start_address, env_cache, area0_end_addr - area0_start_address);
        /* current load ENV area address is area0 start address */
        cur_load_area_addr = area0_start_address; /* 让 CRC 计算对准区0 */
        if (!env_crc_is_ok()) {
            area0_is_valid = false; /* 票2：区0 数据损坏 */
        }
    }
    /* check area1 CRC when it is valid */
    if (area1_is_valid) {
        /* read ENV area1 crc32 code from flash */
        ef_port_read(area1_start_address + ENV_PARAM_INDEX_DATA_CRC * 4, &area1_crc, 4);
        /* read ENV from ENV area1 */
        ef_port_read(area1_start_address, env_cache, area1_end_addr - area1_start_address);
        /* current load ENV area address is area1 start address */
        cur_load_area_addr = area1_start_address; /* 让 CRC 计算对准区1 */
        if (!env_crc_is_ok()) {
            area1_is_valid = false; /* 票2：区1 数据损坏 */
        }
    }
    /* all ENV area CRC is OK then compare saved count */
    if (area0_is_valid && area1_is_valid) {
        /* read ENV area saved count from flash */
        ef_port_read(area0_start_address + ENV_PARAM_INDEX_SAVED_COUNT * 4,
                     &area0_saved_count, 4);
        ef_port_read(area1_start_address + ENV_PARAM_INDEX_SAVED_COUNT * 4,
                     &area1_saved_count, 4);
        /* the bigger saved count area is valid */
        if ((area0_saved_count > area1_saved_count) || ((area0_saved_count == 0) && (area1_saved_count == 0xFFFFFFFF))) {
            area1_is_valid = false; /* 票3：计数比较，区0 胜出（含回绕特判） */
        } else {
            area0_is_valid = false; /* 票3：区1 胜出 */
        }
    }
    if (area0_is_valid) {
        /* current load ENV area address is area0 start address */
        cur_load_area_addr = area0_start_address;
        /* next save ENV area address is area1 start address */
        next_save_area_addr = area1_start_address; /* 乒乓指向败者区 */
        /* read all ENV from area0 */
        ef_port_read(area0_start_address, env_cache, area0_end_addr - area0_start_address);
    } else if (area1_is_valid) {
        /* next save ENV area address is area0 start address */
        next_save_area_addr = area0_start_address; /* 区1 数据在票2 阶段已读入 RAM */
    } else {
        /* current load ENV area address is area1 start address */
        cur_load_area_addr = area1_start_address;
        /* next save ENV area address is area0 start address */
        next_save_area_addr = area0_start_address;
        /* set the ENV to default */
        result = ef_env_set_default(); /* 双区皆毁：回退出厂默认 */
    }
    return result;
}
#endif

/**
 * Save ENV to flash.
 * 【深注】ef_save_env() —— legacy 模式唯一的落盘动作：整区"擦->写"原子化尝试。
 *
 * 普通模式：写回地址就是系统区首，write_size = 系统区+数据段总占用，CRC 先算好
 * 缓存在参数字里再一起写。PFS 模式：写目标是"败者区"，写前先交换 cur/next 指针、
 * 把游标平移到新区、saved_count++——即使擦或写中途掉电，另一区仍是上一个完整体，
 * 上电仲裁后数据不丢（代价：每次保存双倍写入量）。
 *
 * @note  擦除失败立即返回（不会带病写入）；写失败仅报错并清脏标志——normal 模式
 *         下这意味着 RAM 与 Flash 可能不一致，需上层重试保存。
 * @note  "延迟写"语义：本函数在 NG 模式里退化为 no-op（ef_env.c 每次写即时落盘），
 *         这也是 ef_save_env 在 V4.x 被标记 DEPRECATED 的原因。
 *
 * @return result
 */
EfErrCode ef_save_env(void) {
    EfErrCode result = EF_NO_ERR;
    uint32_t write_addr, write_size;

    /* ENV ram cache has not changed don't need to save */
    if (!env_cache_changed) {
        return result; /* 干净缓存直接跳过：避免无谓的整区擦写，延长 Flash 寿命 */
    }

#ifndef EF_ENV_USING_PFS_MODE
    write_addr = get_env_system_addr();
    write_size = get_env_user_used_size();
    /* calculate and cache CRC32 code */
    env_cache[ENV_PARAM_INDEX_DATA_CRC] = calc_env_crc(); /* CRC 覆盖游标+数据段 */
#else
    write_addr = next_save_area_addr;
    write_size = get_env_user_used_size();
    /* replace next_save_area_addr with cur_load_area_addr */
    next_save_area_addr = cur_load_area_addr; /* 指针互换：旧有效区沦为下一轮的写目标 */
    cur_load_area_addr = write_addr;
    /* change the ENV end address to next save area address */
    set_env_end_addr(write_addr + write_size); /* 游标平移到新区的绝对地址 */
    /* ENV area saved count +1 */
    env_cache[ENV_PARAM_INDEX_SAVED_COUNT]++; /* 每保存一次递增，上电仲裁的"新钟" */
    /* calculate and cache CRC32 code */
    env_cache[ENV_PARAM_INDEX_DATA_CRC] = calc_env_crc();
#endif

    /* erase ENV */
    result = ef_port_erase(write_addr, write_size); /* 整块擦除（size 由移植层对齐到扇区） */
    switch (result) {
    case EF_NO_ERR: {
        EF_DEBUG("Erased ENV OK.\n");
        break;
    }
    case EF_ERASE_ERR: {
        EF_INFO("Error: Erased ENV fault! Start address is 0x%08X, size is %ld.\n", write_addr, write_size);
        /* will return when erase fault */
        return result;
    }
    }

    /* write ENV to flash */
    result = ef_port_write(write_addr, env_cache, write_size);
    switch (result) {
    case EF_NO_ERR: {
        EF_DEBUG("Saved ENV OK.\n");
        break;
    }
    case EF_WRITE_ERR: {
        EF_INFO("Error: Saved ENV fault! Start address is 0x%08X, size is %ld.\n", write_addr, write_size);
        break;
    }
    }

    env_cache_changed = false;

    return result;
}

/**
 * Calculate the cached ENV CRC32 value.
 * 【深注】calc_env_crc() —— 计算整个 ENV 镜像的 CRC32：游标(4B) + [保存计数] + 数据段。
 *
 * 覆盖范围按编译开关扩展：普通模式 = 游标 + 数据段；PFS 模式把 saved_count 也
 * 纳入（防止仲裁计数被篡改）；自动更新模式同样把版本号当数据段一部分保护。
 * CRC 参数字本身不在计算范围内（否则自引用）。
 *
 * @return CRC32 value
 */
static uint32_t calc_env_crc(void) {
    uint32_t crc32 = 0;

    /* Calculate the ENV end address CRC32. The 4 is ENV end address bytes size. */
    crc32 = ef_calc_crc32(crc32, &env_cache[ENV_PARAM_INDEX_END_ADDR], 4);

#ifdef EF_ENV_USING_PFS_MODE
    /* Calculate the ENV area saved count CRC32. */
    crc32 = ef_calc_crc32(crc32, &env_cache[ENV_PARAM_INDEX_SAVED_COUNT], 4);
#endif

    /* Calculate the all ENV data CRC32. */
    crc32 = ef_calc_crc32(crc32, &env_cache[ENV_PARAM_WORD_SIZE], get_env_data_size());

    EF_DEBUG("Calculate ENV CRC32 number is 0x%08X.\n", crc32);

    return crc32;
}

/**
 * Check the ENV CRC32
 * 【深注】env_crc_is_ok() —— 校验入口：现算 CRC 与 Flash/RAM 里存的参数字比对。
 * 依赖 env_cache 中已载入待校验数据（ef_load_env 的两处调用点都遵循这一前置）。
 *
 * @return true is ok
 */
static bool env_crc_is_ok(void) {
    if (calc_env_crc() == env_cache[ENV_PARAM_INDEX_DATA_CRC]) {
        EF_DEBUG("Verify ENV CRC32 result is OK.\n");
        return true;
    } else {
        return false;
    }
}

/**
 * Set and save an ENV. If set ENV is success then will save it.
 * 【深注】ef_set_and_save_env() —— 旧版一步式 API：set 成功后立即 save。
 * legacy 模式下"改一次参数=一次整区擦写"，高频调用会快速消耗寿命；
 * NG 模式（ef_env.c）中它已退化为 ef_set_env 的别名并标记 DEPRECATED。
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
EfErrCode ef_set_and_save_env(const char *key, const char *value) {
    EfErrCode result = EF_NO_ERR;

    result = ef_set_env(key, value);

    if (result == EF_NO_ERR) {
        result = ef_save_env();
    }

    return result;
}

/**
 * Del and save an ENV. If del ENV is success then will save it.
 * 【深注】ef_del_and_save_env() —— 旧版一步式删除 API（del 成功后立即 save），
 * 与 ef_set_and_save_env 同样的写放大问题，V4.x 起标记 DEPRECATED。
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_and_save_env(const char *key) {
    EfErrCode result = EF_NO_ERR;

    result = ef_del_env(key);

    if (result == EF_NO_ERR) {
        result = ef_save_env();
    }

    return result;
}

#ifdef EF_ENV_AUTO_UPDATE
/**
 * Auto update ENV to latest default when current EF_ENV_VER is changed.
 * 【深注】env_auto_update() —— ENV 增量升级：固件换代后只补缺失的默认项。
 *
 * 机制：系统区存有版本号 ENV_PARAM_INDEX_VER_NUM（由 EF_ENV_VER_NUM 标识固件
 * 期望的默认参数集版本）。启动时读到旧版本号 -> 置脏 + 把版本号刷成新值 ->
 * 遍历 default_env_set，只对 find_env 找不到的 key 执行 create_env（用户改过
 * 的既有参数不动），最后 ef_save_env 统一落盘。实现"固件升级，用户参数不丢"。
 *
 * @return result
 */
static EfErrCode env_auto_update(void)
{
    size_t i;

    /* lock the ENV cache */
    ef_port_env_lock();

    /* read ENV version number from flash*/
    ef_port_read(get_env_system_addr() + ENV_PARAM_INDEX_VER_NUM * 4,
                 &env_cache[ENV_PARAM_INDEX_VER_NUM] , 4);

    /* check version number */
    if (env_cache[ENV_PARAM_INDEX_VER_NUM] != EF_ENV_VER_NUM) {
        env_cache_changed = true;
        /* update version number */
        env_cache[ENV_PARAM_INDEX_VER_NUM] = EF_ENV_VER_NUM;
        /* add a new ENV when it's not found */
        for (i = 0; i < default_env_set_size; i++) {
            if (find_env(default_env_set[i].key) == NULL) {
                create_env(default_env_set[i].key, default_env_set[i].value);
            }
        }
    }

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return ef_save_env();
}
#endif /* EF_ENV_AUTO_UPDATE */

#endif /* EF_ENV_USING_WL_MODE */

#endif /* defined(EF_USING_ENV) && defined(EF_ENV_USING_LEGACY_MODE) */
