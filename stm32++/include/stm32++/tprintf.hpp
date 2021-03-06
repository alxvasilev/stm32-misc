/**
 * @author Alexander Vassilev
 * @copyright BSD License
 */

#ifndef _TPRINTF_H
#define _TPRINTF_H

#include "tsnprintf.hpp"
#include "printSink.hpp"
#include <assert.h>
#include <stdlib.h>
#include <alloca.h>

#ifndef STM32PP_TPRINTF_MAX_DYNAMIC_BUFSIZE
    #define STM32PP_TPRINTF_MAX_DYNAMIC_BUFSIZE 10240
#endif

#ifndef STM32PP_TPRINTF_ASYNC_EXPAND_STEP
    #define STM32PP_TPRINTF_ASYNC_EXPAND_STEP 64
#endif

#ifndef STM32PP_TPRINTF_SYNC_EXPAND_STEP
    #define STM32PP_TPRINTF_SYNC_EXPAND_STEP 128
#endif

template <int InitialBufSize=64, typename... Args>
size_t ftprintf(uint8_t fd, const char* fmtStr, Args... args)
{
    extern IPrintSink* gPrintSink;
    char* staticBuf; // static buf
    char* buf;
    size_t bufsize;

    auto async = gPrintSink->waitReady();
    if (async)
    {
        staticBuf = nullptr;
        if (async->buf)
        {
            buf = (char*)async->buf;
            bufsize = async->bufSize;
        }
        else
        {
            buf = (char*)malloc(InitialBufSize);
            bufsize = InitialBufSize;
        }
    }
    else
    {
        buf = staticBuf = (char*)alloca(InitialBufSize);
        bufsize = InitialBufSize;
    }
    char* ret;
    for(;;)
    {
        ret = tsnprintf(buf, bufsize, fmtStr, args...);
        if (ret)
        {
            break;
        }
        // tsnprintf() returned nullptr, have to increase buf size
        bufsize += async ? STM32PP_TPRINTF_ASYNC_EXPAND_STEP : STM32PP_TPRINTF_SYNC_EXPAND_STEP;
        if (bufsize > STM32PP_TPRINTF_MAX_DYNAMIC_BUFSIZE)
        {
            //too much, bail out
            if ((buf != staticBuf) && !async) // buffer is dynamic and synchronous, free it
            {
                free(buf);
            }
            return 0;
        }
        buf = (buf == staticBuf)
            ? (char*)malloc(bufsize)
            : (char*)realloc(buf, bufsize);
        if (!buf)
        {
            // If we are async, we did a realloc. When realloc fails,
            // it doesn't free the old buffer, so the sink's pointer remains valid
            return 0;
        }
    }
    assert(ret >= buf);
    size_t size = ret-buf;
    if (async)
    {
        gPrintSink->print(buf, size, bufsize);
    }
    else
    {
        gPrintSink->print(buf, size, fd);
        if (buf != staticBuf)
        {
            free(buf);
        }
    }
    return size;
}

template <int InitialBufSize=64, typename ...Args>
uint16_t tprintf(const char* fmtStr, Args... args)
{
    return ftprintf<InitialBufSize>(1, fmtStr, args...);
}

static inline void puts(const char* str, uint16_t len)
{
    extern IPrintSink* gPrintSink;
    gPrintSink->print((char*)str, len, 1);
}

#endif
