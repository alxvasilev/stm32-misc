/**
 * original author:  Tilen Majerle<tilen@majerle.eu>
 * modification for STM32f10x: Alexander Lutsai<s.lyra@ya.ru>

   ----------------------------------------------------------------------
   	Copyright (C) Alexander Lutsai, 2016
    Copyright (C) Tilen Majerle, 2015

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------
 */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/dma.h>
#include <stm32++/timeutl.h>
#include <stm32++/snprint.h>
#include <stm32++/semihosting.hpp>

namespace nsi2c
{
/* Private definitions */
/** @brief Timeout for ACK of sent data. Used also for timeout for device
 * response in \c inDeviceConnected() */
enum { kTimeoutMs = 10 };

/* Private defines */
enum: bool { kTxMode = true, kRxMode = false,
             kAckEnable = true, kAckDisable = false };
template<uint32_t I2C>
struct I2CInfo;

template <uint32_t I2C>
class I2c
{
public:
    enum: uint32_t { I2CBase = I2C };
    enum: bool { hasTxDma = false, hasRxDma = false };
void init(bool fastMode=true, uint8_t ownAddr=0x15)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    I2CInfo<I2C> info;
    /* Set alternate functions for the SCL and SDA pins of I2C1. */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
              GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN,
              info.kPinSda | info.kPinScl);

    rcc_periph_clock_enable(info.clock());
    i2c_reset(I2C);
    /* Disable the I2C before changing any configuration. */
    i2c_peripheral_disable(I2C);
    i2c_set_clock_frequency(I2C, rcc_apb1_frequency / 1000000);
    /* 400KHz */
    if (fastMode)
    {
        // Datasheet suggests 0x1e.
        i2c_set_fast_mode(I2C);
        uint32_t clockRatio = rcc_apb1_frequency / 400000;
        i2c_set_ccr(I2C, (clockRatio*2+3)/6); //round clockRatio/3
        i2c_set_dutycycle(I2C, I2C_CCR_DUTY_DIV2);
        /*
         * rise time for 400kHz => 300ns and 100kHz => 1000ns; 300ns/28ns = 10;
         * Incremented by 1 -> 11.
         */
        uint32_t clocks = 300 / ((2000000000+rcc_apb1_frequency)/(rcc_apb1_frequency*2)) + 1;
        i2c_set_trise(I2C, clocks);
    }
    else
    {
        i2c_set_standard_mode(I2C);
        uint32_t clockRatio = rcc_apb1_frequency / 100000;
        i2c_set_ccr(I2C, clockRatio/2);
        uint32_t clocks = 1000 / ((2000000000+rcc_apb1_frequency)/(rcc_apb1_frequency*2)) + 1;
        i2c_set_trise(I2C, clocks);
    }

    /*
     * This is our slave address - needed only if we want to receive from
     * other masters.
     */
    i2c_set_own_7bit_slave_address(I2C, ownAddr);
    i2c_disable_ack(I2C);
    /* If everything is configured -> enable the peripheral. */
    i2c_peripheral_enable(I2C);
}

void sendBuf(uint8_t* data, uint16_t count)
{
    uint8_t* end = data+count;
    while (data < end)
    {
        sendByte(*(data++));
    }
}

bool startSend(uint8_t address, bool ack=false)
{
    return start(address, kTxMode, ack);
}
bool startRecv(uint8_t address, bool ack=false)
{
    return start(address, kRxMode, ack);
}

/* Private functions */
bool start(uint8_t address, bool tx, bool ack)
{
	/* Generate I2C start pulse */
    i2c_send_start(I2C);
    /* Waiting for START to be sent and switched to master mode. */
    while (!(I2C_SR1(I2C) & I2C_SR1_SB));
    assert(I2C_SR2(I2C) & I2C_SR2_MSL);

    if (ack)
        i2c_enable_ack(I2C);
    else
        i2c_disable_ack(I2C);

    /* Send destination address. */
    i2c_send_7bit_address(I2C, address, tx ? I2C_WRITE : I2C_READ);

    ElapsedTimer timer;
    /* Waiting for address is transferred. */
    while (!(I2C_SR1(I2C) & I2C_SR1_ADDR))
    {
        if (timer.msElapsed() > kTimeoutMs)
            return false;
    }
    assert(!(I2C_SR1(I2C) & I2C_SR1_SB));
    (volatile uint32_t)I2C_SR2(I2C);

#ifndef NDEBUG
    uint32_t sr2 = I2C_SR2(I2C);
    if (tx)
        assert(sr2 & I2C_SR2_TRA);
    else
        assert(!(sr2 & I2C_SR2_TRA));
#endif

    assert((I2C_SR1(I2C) & I2C_SR1_ADDR) == 0);
    return true;
}

bool sendByte(uint8_t data)
{
//  tprintf("sendByte %\n", fmtNum<16>(data));
    if (!(I2C_SR1(I2C) & I2C_SR1_TxE))
    {
        ElapsedTimer timer;
        while (!(I2C_SR1(I2C) & I2C_SR1_TxE))
        {
            if (timer.msElapsed() > kTimeoutMs)
                return false;
        }
    }
    i2c_send_data(I2C, data);
    return true;
}

template <typename... Args>
void sendByte(uint8_t byte, Args... args)
{
    sendByte(byte);
    sendByte(args...);
}
template <typename T>
void vsend(T data)
{
    uint8_t* end = ((uint8_t*)&data)+sizeof(data);
    for (uint8_t* ptr = (uint8_t*)&data; ptr<end; ptr++)
        sendByte(*ptr);
}

template <typename T, typename... Args>
void vsend(T val, Args... args)
{
    if (sizeof(T) == 1)
        sendByte(val);
    else
        send(val);
    send(args...);
}

uint8_t recvByte()
{
    while((I2C_SR1(I2C) & I2C_SR1_RxNE) == 0);
    return I2C_DR(I2C);
}

void recv(uint8_t* buf, size_t count)
{
    uint8_t* end = buf+count;
    while(buf < end)
    {
        while ((I2C_SR1(I2C) & I2C_SR1_RxNE) == 0);
        *(buf++) = I2C_DR(I2C);
    }
}

void stop()
{
    //wait transfer complete
#ifndef NDEBUG
    ElapsedTimer timer;
    while (!(I2C_SR1(I2C) & (I2C_SR1_BTF | I2C_SR1_TxE)))
    {
        if (timer.msElapsed() > kTimeoutMs)
        {
            assert(false && "stop(): Timeout waiting for output flush");
            for(;;);
        }
    }
#else
    while (!(I2C_SR1(I2C) & (I2C_SR1_BTF | I2C_SR1_TxE)));
#endif
    /* Send STOP condition. */
    i2c_send_stop(I2C);
}

bool isDeviceConnected(uint8_t address)
{
    /* Try to start, function will return 0 in case device will send ACK */
    bool connected = start(address, kTxMode, kAckEnable);
    if (!connected)
        return false;
    stop();
    return true;
}

uint8_t findFirstDevice(uint8_t from=0)
{
    for (uint8_t i = from; i < 128; i++)
        if (isDeviceConnected(i))
            return i;
    return 0xff;
}
};

template<>
struct I2CInfo<I2C1>
{
    enum: uint16_t { kPinScl = GPIO_I2C1_SCL, kPinSda = GPIO_I2C1_SDA };
    static constexpr rcc_periph_clken clock() { return RCC_I2C1; }
};
template<>
struct I2CInfo<I2C2>
{
    enum: uint16_t { kPinScl = GPIO_I2C2_SCL, kPinSda = GPIO_I2C2_SDA };
    static constexpr rcc_periph_clken clock() { return RCC_I2C2; }
};
template<uint32_t I2C>
struct DmaInfo;

template<uint32_t I2C>
class I2cDma: public I2c<I2C>
{
public:
    enum: bool { hasTxDma = true, hasRxDma = false };
    typedef void(*FreeFunc)(void*);
protected:
    volatile void* mTxBuf = nullptr;
    volatile FreeFunc mTxBufFreeFunc = nullptr;
    enum: uint32_t { Dma = DmaInfo<I2C>::kDmaId };
    enum: uint8_t { kDmaTxChan = DmaInfo<I2C>::kDmaTxChan,
                    kDmaRxChan = DmaInfo<I2C>::kDmaRxChan };
public:
    using I2c<I2C>::I2c;
    void dmaSend(uint8_t* data, uint16_t size, FreeFunc freeFunc)
    {
        while(mTxBuf);
        mTxBuf = data;
        mTxBufFreeFunc = freeFunc;
        rcc_periph_clock_enable(DmaInfo<I2C>::clock());
        dma_channel_reset(Dma, kDmaTxChan);
        dma_set_peripheral_address(Dma, kDmaTxChan, (uint32_t)&(I2C_DR(I2C)));
        dma_set_memory_address(Dma, kDmaTxChan, (uint32_t)data);
        dma_set_number_of_data(Dma, kDmaTxChan, size);
        dma_set_read_from_memory(Dma, kDmaTxChan);
        dma_enable_memory_increment_mode(Dma, kDmaTxChan);
        dma_disable_peripheral_increment_mode(Dma, kDmaTxChan);
        dma_set_peripheral_size(Dma, kDmaTxChan, DMA_CCR_PSIZE_8BIT);
        dma_set_memory_size(Dma, kDmaTxChan, DMA_CCR_MSIZE_8BIT);
        dma_set_priority(Dma, kDmaTxChan, DMA_CCR_PL_HIGH); //TODO: make priority selectable
        dma_enable_transfer_complete_interrupt(Dma, kDmaTxChan);
        nvic_enable_irq(DmaInfo<I2C>::kDmaTxIrq);
        dma_enable_channel(Dma, kDmaTxChan);
        i2c_enable_dma(I2C);
}

void dmaTxIsr()
{
    if ((DMA_ISR(Dma) & DMA_ISR_TCIF(kDmaTxChan)) == 0)
        return;

    DMA_IFCR(Dma) |= DMA_IFCR_CTCIF(kDmaTxChan);
    stopTxDma();
}

void stopTxDma()
{
    //WARNING: This can be called from an ISR
    dma_disable_transfer_complete_interrupt(Dma, kDmaTxChan);
    i2c_disable_dma(I2C);
    dma_disable_channel(Dma, kDmaTxChan);
    this->stop();
    assert(mTxBuf);
    //FIXME: mFreeFunc may not be reentrant
    if (mTxBufFreeFunc)
    {
        mTxBufFreeFunc((void*)mTxBuf);
        mTxBufFreeFunc = nullptr;
    }
    mTxBuf = nullptr;
}
};

template<>
struct DmaInfo<I2C1>
{
    enum: uint32_t { kDmaId = DMA1 };
    enum: uint8_t {
        kDmaTxChan = 6, kDmaRxChan = 7,
        kDmaTxIrq = NVIC_DMA1_CHANNEL6_IRQ,
        kDmaRxIrq = NVIC_DMA1_CHANNEL7_IRQ
    };
    static rcc_periph_clken clock() { return RCC_DMA1; }
};
template<>
struct DmaInfo<I2C2>
{
    enum: uint32_t { kDmaId = DMA1 };
    enum: uint8_t {
        kDmaTxChan = 4, kDmaRxChan = 5,
        kDmaTxIrq = NVIC_DMA1_CHANNEL4_IRQ,
        kDmaRxIrq = NVIC_DMA1_CHANNEL5_IRQ
    };
    static rcc_periph_clken clock() { return RCC_DMA1; }
};
}
