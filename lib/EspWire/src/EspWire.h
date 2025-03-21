/*
  EspTwoWire.h - TWI/I2C library for Arduino & Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Modified 2012 by Todd Krein (todd@krein.org) to implement repeated starts
  Modified December 2014 by Ivan Grokhotkov (ivan@esp8266.com) - esp8266 support
  Modified April 2015 by Hrsto Gochkov (ficeto@ficeto.com) - alternative esp8266 support
*/

#ifndef EspTwoWire_h
#define EspTwoWire_h

#include <cinttypes>
#include <Arduino.h>

#define ESPWIRE_BUFFER_LENGTH 64

class EspTwoWire : public Stream
{
  private:
    static uint8_t rxBuffer[];
    static uint8_t rxBufferIndex;
    static uint8_t rxBufferLength;

    static uint8_t txAddress;
    static uint8_t txBuffer[];
    static uint8_t txBufferIndex;
    static uint8_t txBufferLength;

    static uint8_t transmitting;
    static void (*user_onRequest)(void);
    static void (*user_onReceive)(int);
    static void onRequestService(void);
    static void onReceiveService(uint8_t*, int);
  public:
    EspTwoWire();
    void begin(int sda, int scl, uint32_t frequency = 0);
    void pins(int sda, int scl) __attribute__((deprecated)); // use begin(sda, scl) in new code
    void begin();
    void begin(uint8_t);
    void begin(int);
    void setClock(uint32_t);
    void setClockStretchLimit(uint32_t);
    void beginTransmission(uint8_t)/* IRAM_ATTR */;
    void beginTransmission(int);
    uint8_t endTransmission(void) /* IRAM_ATTR */;
    uint8_t endTransmission(uint8_t);
    size_t requestFrom(uint8_t address, size_t size, bool sendStop);
	  uint8_t status();

    uint8_t requestFrom(uint8_t, uint8_t) /* IRAM_ATTR */;
    uint8_t requestFrom(uint8_t, uint8_t, uint8_t);
    uint8_t requestFrom(int, int);
    uint8_t requestFrom(int, int, int);

    virtual size_t write(uint8_t) /* IRAM_ATTR */;
    virtual size_t write(const uint8_t *, size_t);
    virtual int available(void) /* IRAM_ATTR */;
    virtual int read(void) /* IRAM_ATTR */;
    virtual int peek(void);
    virtual void flush(void);
    void onReceive( void (*)(int) );
    void onRequest( void (*)(void) );

    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }
    using Print::write;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_TWOWIRE)
extern EspTwoWire EspWire;
#endif

#endif
