/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2015-2019, Armink, <armink.ztl@gmail.com>
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
 * Function: Portable interface for each platform.
 * Created on: 2015-01-16
 */

/*
 * ============================================================================
 * 【B级导读】ef_port.c —— 移植层（整库唯一需要用户动手的文件）
 * ----------------------------------------------------------------------------
 * 移植契约（详见 Docs/05-移植层.md 与 docs/zh/port.md）：
 *   ef_port_init   注册 default_env_set[]（出厂默认参数表），可不访问 Flash；
 *   ef_port_read   按"字节 size、写粒度对齐"读；NOR Flash 读无限制；
 *   ef_port_write  写前必须已擦除；size 需对齐写粒度（1/8/32bit）；同址禁止重复
 *                  写（部分 MCU 如 stm32f1 会把多次写叠加成位与）；
 *   ef_port_erase  addr 必须 EF_ERASE_MIN_SIZE 对齐，size 不足一个扇区时须向上
 *                  对齐整扇擦除（legacy-WL 的 save_cur_using_data_addr 只传 4B，
 *                  兜底责任在本层）；
 *   env_lock/unlock 多任务系统的临界区（关调度/互斥量）；单线程留空即可；
 *   三个打印函数    按需接串口/RTT；PRINT_DEBUG 未定义时 ef_log_debug 为空宏。
 * 完成以上即可通过 easyflash_init() 的自检链。
 * ============================================================================
 */

#include <easyflash.h>
#include <stdarg.h>

/* default environment variables set for user */
/*
 * 【深注】出厂默认参数表：ef_env_set_default() 与 ENV 自动升级（EF_ENV_AUTO_UPDATE）
 * 都从这里重放。ef_env.value_len==0 时按 C 字符串处理（strlen 取长）；
 * 需要预置二进制值就填 value_len。示例：
 *   {"wifi_ssid", "my-wifi", 0}, {"device_id", buf, sizeof(buf)}
 */
static const ef_env default_env_set[] = {

};

/**
 * Flash port for hardware initialize.
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV size
 *
 * @return result
 */
EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size) {
    EfErrCode result = EF_NO_ERR;

    *default_env = default_env_set;
    *default_env_size = sizeof(default_env_set) / sizeof(default_env_set[0]);

    return result;
}

/**
 * Read data from flash.
 * @note This operation's units is word.
 *
 * @param addr flash address
 * @param buf buffer to store read data
 * @param size read bytes size
 *
 * @return result
 */
EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size) {
    EfErrCode result = EF_NO_ERR;

    /* You can add your code under here. */
    /* 【移植】示例（stm32f1 片内）：memcpy(buf, (const uint32_t*)addr, size);
     * SPI Flash（SFUD）：sfud_read(sfud_dev, addr-EF_START_ADDR 偏移, size, buf) */

    return result;
}

/**
 * Erase data on flash.
 * @note This operation is irreversible.
 * @note This operation's units is different which on many chips.
 *
 * @param addr flash address
 * @param size erase bytes size
 *
 * @return result
 */
EfErrCode ef_port_erase(uint32_t addr, size_t size) {
    EfErrCode result = EF_NO_ERR;

    /* make sure the start address is a multiple of EF_ERASE_MIN_SIZE */
    EF_ASSERT(addr % EF_ERASE_MIN_SIZE == 0); /* 库侧已保证，可加移植层二次防御 */

    /* You can add your code under here. */
    /* 【移植】size 小于一个扇区时也要擦整扇区（flash_erase 物理特性）；
     * 调用方可能传入 4B 这样的"非整扇 size"，此处必须向上对齐 */

    return result;
}
/**
 * Write data to flash.
 * @note This operation's units is word.
 * @note This operation must after erase. @see flash_erase.
 *
 * @param addr flash address
 * @param buf the write data buffer
 * @param size write bytes size
 *
 * @return result
 */
EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size) {
    EfErrCode result = EF_NO_ERR;
    
    /* You can add your code under here. */
    /* 【移植】写粒度提醒：stm32f1 片内 Flash 以半页 16 字为单位编程，
     * size 不足时用 0xFF 补齐到整页写；NOR Flash 则支持 1 字节独立写 */

    return result;
}

/**
 * lock the ENV ram cache
 */
void ef_port_env_lock(void) {
    
    /* You can add your code under here. */
    /* 【移植】多任务系统：递归互斥量或关闭相关优先级中断；
     * 注意 ENV API 可能嵌套调用（set 内部再锁），用递归锁最稳妥 */
    
}

/**
 * unlock the ENV ram cache
 */
void ef_port_env_unlock(void) {
    
    /* You can add your code under here. */
    
}


/**
 * This function is print flash debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 *
 */
void ef_log_debug(const char *file, const long line, const char *format, ...) {

#ifdef PRINT_DEBUG

    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);

#endif

}

/**
 * This function is print flash routine info.
 *
 * @param format output format
 * @param ... args
 */
void ef_log_info(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);
}
/**
 * This function is print flash non-package info.
 *
 * @param format output format
 * @param ... args
 */
void ef_print(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);
}
