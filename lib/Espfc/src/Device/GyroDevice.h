#pragma once

#include <helper_3dmath.h>
#include "BusDevice.h"
#include "BusAwareDevice.h"

namespace Espfc {

enum GyroDeviceType {
  GYRO_AUTO    = 0,
  GYRO_NONE    = 1,
  GYRO_MPU6000 = 2,
  GYRO_MPU6050 = 3,
  GYRO_MPU6500 = 4,
  GYRO_MPU9250 = 5,
  GYRO_LSM6DSO = 6,
  GYRO_ICM20602 = 7,
  GYRO_BMI160 = 8,
  GYRO_MAX
};

namespace Device {

class GyroDevice: public BusAwareDevice
{
  public:
    typedef GyroDeviceType DeviceType;

    virtual int begin(BusDevice * bus) = 0;
    virtual int begin(BusDevice * bus, uint8_t addr) = 0;

    virtual DeviceType getType() const = 0;
    
    virtual int readGyro(VectorInt16& v) = 0;
    virtual int readAccel(VectorInt16& v) = 0;

    virtual void setDLPFMode(uint8_t mode) = 0;
    virtual int getRate() const = 0;
    virtual void setRate(int rate) = 0;

    virtual bool testConnection() = 0;

    static const char ** getNames();
    static const char * getName(DeviceType type);
};

}

}
