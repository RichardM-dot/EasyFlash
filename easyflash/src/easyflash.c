/*
 * ============================================================================
 * 【中文导读】easyflash.c —— EasyFlash 统一初始化入口
 * ----------------------------------------------------------------------------
 * easyflash_init(): 按配置宏依次初始化 ENV(参数)/IAP(在线升级)/LOG(日志)
 * 三大功能模块。版本打印与环境自检也在这里。EasyFlash 是 FlashDB 的前身，
 * 新项目建议 FlashDB；本库源码注释更密，适合作为第一个通读的存储库。
 * ============================================================================
 */

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
 * Function: Initialize interface for this library.
 * Created on: 2014-09-09
 */

/*
 *
 * This all Backup Area Flash storage index. All used flash area configure is under here.
 * |----------------------------|   Storage Size
 * | Environment variables area |   ENV area size @see ENV_AREA_SIZE
 * |----------------------------|
 * |      Saved log area        |   Log area size @see LOG_AREA_SIZE
 * |----------------------------|
 * |(IAP)Downloaded application |   IAP already downloaded application, unfixed size
 * |----------------------------|
 *
 * @note all area sizes must be aligned with EF_ERASE_MIN_SIZE
 *
 * The EasyFlash add the NG (Next Generation) mode start from V4.0. All old mode before V4.0, called LEGACY mode.
 *
 * - NG (Next Generation) mode is default mode from V4.0. It's easy to settings, only defined the ENV_AREA_SIZE.
 * - The LEGACY mode has been DEPRECATED. It is NOT RECOMMENDED to continue using.
 *   Beacuse it will use ram to buffer the ENV and spend more flash erase times.
 *   If you want use it please using the V3.X version.
 */

#include <easyflash.h>

#if !defined(EF_START_ADDR)
#error "Please configure backup area start address (in ef_cfg.h)"
#endif

#if !defined(EF_ERASE_MIN_SIZE)
#error "Please configure minimum size of flash erasure (in ef_cfg.h)"
#endif

/**
 * 【深注】easyflash_init() —— 整库唯一初始化入口，三段式串行流水线。
 *
 * 初始化顺序（严格串行，前一步失败立即终止后续步骤）：
 *   1) ef_port_init() : 移植层入口。把 ef_port.c 里的 default_env_set[]（用户默认
 *                       参数表）与表长交给 ENV 模块，本身不访问 Flash。
 *   2) ef_env_init()  : ENV 挂载（NG 模式走 ef_env.c，legacy 模式走 ef_env_legacy*.c，
 *                       由 EF_ENV_USING_LEGACY_MODE 宏二选一编译）。这一步会读扇区
 *                       头、做 CRC 校验、执行掉电恢复，必要时整区格式化重建。
 *   3) ef_iap_init()  : 仅做地址运算——备份区首地址 = EF_START_ADDR + ENV 区 + LOG 区，
 *                       不碰 Flash，几乎为零开销。
 *   4) ef_log_init()  : 扫描日志区每个扇区头，定位环形缓冲的 start/end 两个指针。
 *
 * @note  init_ok 为 file-scope 静态标志：保证本函数幂等。RTOS 多任务环境下重复调用
 *        直接返回成功；但 ef_get_env/ef_set_env 等 API 仍需 ef_port_env_lock 保护。
 * @note  三个子模块函数在此用 extern 就地声明而非包含模块头——EasyFlash 刻意只对外
 *        暴露 easyflash.h 一个公共头，模块间靠"约定函数原型"解耦，缩短移植改动链。
 *
 * @return EF_NO_ERR 成功；否则返回第一个失败子模块的错误码
 */

/**
 * EasyFlash system initialize.
 *
 * @return result
 */
EfErrCode easyflash_init(void) {
    extern EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size);
    extern EfErrCode ef_env_init(ef_env const *default_env, size_t default_env_size);
    extern EfErrCode ef_iap_init(void);
    extern EfErrCode ef_log_init(void);

    size_t default_env_set_size = 0;
    const ef_env *default_env_set;
    EfErrCode result = EF_NO_ERR;
    static bool init_ok = false; /* 幂等开关：仅本文件可见，成功后置位，重启前不再重复初始化 */

    if (init_ok) {
        return EF_NO_ERR; /* 已初始化过则短路返回，避免重复扫描 Flash（扫一遍日志区较耗时） */
    }

    /* 第 1 步：移植层入口，取出用户默认 ENV 表（ef_port.c 的 default_env_set[]） */
    result = ef_port_init(&default_env_set, &default_env_set_size);

#ifdef EF_USING_ENV
    if (result == EF_NO_ERR) {
        /* 第 2 步：ENV 挂载 + 掉电恢复。这一步是初始化中最重的一步（多次读 Flash、可能格式化） */
        result = ef_env_init(default_env_set, default_env_set_size);
    }
#endif

#ifdef EF_USING_IAP
    if (result == EF_NO_ERR) {
        /* 第 3 步：IAP 只算备份区首地址（ENV 区、LOG 区之后），纯内存运算 */
        result = ef_iap_init();
    }
#endif

#ifdef EF_USING_LOG
    if (result == EF_NO_ERR) {
        /* 第 4 步：日志区扇区头扫描，恢复环形缓冲区读写指针 */
        result = ef_log_init();
    }
#endif

    if (result == EF_NO_ERR) {
        init_ok = true; /* 四步全部通过才置位：失败时下次调用会重试完整初始化 */
        EF_INFO("EasyFlash V%s is initialize success.\n", EF_SW_VERSION);
    } else {
        EF_INFO("EasyFlash V%s is initialize fail.\n", EF_SW_VERSION);
    }
    EF_INFO("You can get the latest version on https://github.com/armink/EasyFlash .\n");

    return result;
}
