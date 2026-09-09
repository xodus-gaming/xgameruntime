/*
 * Copyright (c) 2025 Weather
 *
 * Permission is hereby granted, CoTaskMemFree of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef XGAMERUNTIME_OS_H
#define XGAMERUNTIME_OS_H

#define MAX_ENV_BUFFER 256

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _RunningOS {
    OS_Windows = 0,
    OS_Linux = 1,
    OS_Darwin = 2,
    OS_Other = 3
} RunningOS;

RunningOS xgameruntime_get_os();

#ifdef __cplusplus
} // extern "C"
#endif

#endif //XGAMERUNTIME_OS_H
