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
 * Function: It is the configure head file for this library.
 * Created on: 2015-07-14
 */

/*
 * ============================================================================
 * 【B级导读】ef_cfg.h —— 全库裁剪与布局配置（移植必改清单）
 * ----------------------------------------------------------------------------
 * 必填项（不填编译报错提醒）：
 *   EF_ERASE_MIN_SIZE  最小擦除粒度（一般=扇区，如 stm32f1 片内 1KB / 2KB）；
 *   EF_WRITE_GRAN      写粒度 bit：1(NOR) / 8(stm32f4) / 32(stm32f1)；
 *   EF_START_ADDR      存储区在 Flash 上的物理首地址；
 *   ENV_AREA_SIZE      ENV 区大小（NG 模式要求 >= 2 扇区，留 1 个空扇区给 GC）。
 * 功能开关：EF_USING_ENV / EF_USING_IAP / EF_USING_LOG 三大件；
 *   EF_ENV_AUTO_UPDATE + EF_ENV_VER_NUM 增量升级；EF_BIG_ENDIAN 大端 MCU。
 * 布局总图见文件中部注释块（ENV 区 -> LOG 区 -> IAP 备份区，依次排列）。
 * 注意：本文件给出的默认值形如 `/* @note you must define it for a value *／`
 * 的宏是"故意空着"的占位——直接编译会在对应 .c 的 #error 处拦下。
 * ============================================================================
 */

#ifndef EF_CFG_H_
#define EF_CFG_H_

/* using ENV function, default is NG (Next Generation) mode start from V4.0 */
#define EF_USING_ENV

#ifdef EF_USING_ENV
/* Auto update ENV to latest default when current ENV version number is changed. */
/* #define EF_ENV_AUTO_UPDATE */
/**
 * ENV version number defined by user.
 * Please change it when your firmware add a new ENV to default_env_set.
 */
#define EF_ENV_VER_NUM            /* @note you must define it for a value, such as 0 */
 
/* MCU Endian Configuration, default is Little Endian Order. */
/* #define EF_BIG_ENDIAN  */         

#endif /* EF_USING_ENV */

/* using IAP function */
/* #define EF_USING_IAP */

/* using save log function */
/* #define EF_USING_LOG */

/* The minimum size of flash erasure. May be a flash sector size. */
#define EF_ERASE_MIN_SIZE         /* @note you must define it for a value */

/* the flash write granularity, unit: bit
 * only support 1(nor flash)/ 8(stm32f4)/ 32(stm32f1) */
#define EF_WRITE_GRAN             /* @note you must define it for a value */


/* The size of read_env and continue_ff_addr function used*/
#define EF_READ_BUF_SIZE             32     /* @default 32, Larger numbers can improve first-time speed of alloc_env but require more stack space*/
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

/* backup area start address */
#define EF_START_ADDR             /* @note you must define it for a value */

/* ENV area size. It's at least one empty sector for GC. So it's definition must more then or equal 2 flash sector size. */
#define ENV_AREA_SIZE             /* @note you must define it for a value if you used ENV */

/* saved log area size */
#define LOG_AREA_SIZE             /* @note you must define it for a value if you used log */

/* print debug information of flash */
#define PRINT_DEBUG

#endif /* EF_CFG_H_ */
