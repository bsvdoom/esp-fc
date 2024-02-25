#ifndef _ESPFC_SENSOR_GYRO_SENSOR_H_
#define _ESPFC_SENSOR_GYRO_SENSOR_H_

#include "BaseSensor.h"
#include "Utils/FilterHelper.h"
#include "Device/GyroDevice.h"
#include "Math/Sma.h"
#include "Math/FreqAnalyzer.h"
#ifdef ESPFC_DSP
#include "Math/FFTAnalyzer.h"
#endif

#define ESPFC_FUZZY_ACCEL_ZERO 0.05
#define ESPFC_FUZZY_GYRO_ZERO  0.20

namespace Espfc {

namespace Sensor {

class GyroSensor: public BaseSensor
{
  public:
    GyroSensor(Model& model): _dyn_notch_denom(1), _model(model) {}

    int begin()
    {
      _gyro = _model.state.gyroDev;
      if(!_gyro) return 0;

      _gyro->setDLPFMode(_model.config.gyroDlpf);
      _gyro->setRate(_gyro->getRate());
      _model.state.gyroScale = radians(2000.f) / 32768.f;

      _model.state.gyroCalibrationState = CALIBRATION_START; // calibrate gyro on start
      _model.state.gyroCalibrationRate = _model.state.loopTimer.rate;
      _model.state.gyroBiasAlpha = 5.0f / _model.state.gyroCalibrationRate;

      _sma.begin(_model.config.loopSync);
      _dyn_notch_denom = std::max((uint32_t)1, _model.state.loopTimer.rate / 1000);
      _dyn_notch_sma.begin(_dyn_notch_denom);

      _dyn_notch_enabled = _model.isActive(FEATURE_DYNAMIC_FILTER) && _model.config.dynamicFilter.width > 0 && _model.state.loopTimer.rate >= DynamicFilterConfig::MIN_FREQ;
      _dyn_notch_debug = _model.config.debugMode == DEBUG_FFT_FREQ || _model.config.debugMode == DEBUG_FFT_TIME;
      _rpm_enabled = _model.config.rpmFilterHarmonics > 0 && _model.config.output.dshotTelemetry;

      for(size_t i = 0; i < 3; i++)
      {
#ifdef ESPFC_DSP
        _fft[i].begin(_model.state.loopTimer.rate / _dyn_notch_denom, _model.config.dynamicFilter, i);
#else
       _freqAnalyzer[i].begin(_model.state.loopTimer.rate / _dyn_notch_denom, _model.config.dynamicFilter);
#endif
      }

      _model.logger.info().log(F("GYRO INIT")).log(FPSTR(Device::GyroDevice::getName(_gyro->getType()))).log(_gyro->getAddress()).log(_model.config.gyroDlpf).log(_gyro->getRate()).log(_model.state.gyroTimer.rate).logln(_model.state.gyroTimer.interval);

      return 1;
    }

    int update()
    {
      int status = read();

      if (status) filter();

      return status;
    }

    int read()
    {
      if(!_model.gyroActive()) return 0;

      Stats::Measure measure(_model.state.stats, COUNTER_GYRO_READ);

      _gyro->readGyro(_model.state.gyroRaw);

      align(_model.state.gyroRaw, _model.config.gyroAlign);

      VectorFloat input = static_cast<VectorFloat>(_model.state.gyroRaw) * _model.state.gyroScale;

      if(_model.config.gyroFilter3.freq)
      {
        _model.state.gyroSampled = Utils::applyFilter(_model.state.gyroFilter3, input);
      }
      else
      {
        _model.state.gyroSampled = _sma.update(input);
      }

      return 1;
    }

    int filter()
    {
      if(!_model.gyroActive()) return 0;

      Stats::Measure measure(_model.state.stats, COUNTER_GYRO_FILTER);

      _model.state.gyro = _model.state.gyroSampled;

      calibrate();

      _model.state.gyroScaled = _model.state.gyro; // must be after calibration

      for(size_t i = 0; i < 3; ++i)
      {
        _model.setDebug(DEBUG_GYRO_RAW, i, _model.state.gyroRaw[i]);
        _model.setDebug(DEBUG_GYRO_SCALED, i, lrintf(degrees(_model.state.gyroScaled[i])));
      }

      _model.setDebug(DEBUG_GYRO_SAMPLE, 0, lrintf(degrees(_model.state.gyro[_model.config.debugAxis])));

      _model.state.gyro = Utils::applyFilter(_model.state.gyroFilter2, _model.state.gyro);

      _model.setDebug(DEBUG_GYRO_SAMPLE, 1, lrintf(degrees(_model.state.gyro[_model.config.debugAxis])));

      if(_rpm_enabled)
      {
        for(size_t m = 0; m < RPM_FILTER_MOTOR_MAX; m++)
        {
          for(size_t n = 0; n < _model.config.rpmFilterHarmonics; n++)
          {
            _model.state.gyro = Utils::applyFilter(_model.state.rpmFilter[m][n], _model.state.gyro);
          }
        }
      }

      _model.setDebug(DEBUG_GYRO_SAMPLE, 2, lrintf(degrees(_model.state.gyro[_model.config.debugAxis])));

      _model.state.gyro = Utils::applyFilter(_model.state.gyroNotch1Filter, _model.state.gyro);
      _model.state.gyro = Utils::applyFilter(_model.state.gyroNotch2Filter, _model.state.gyro);
      _model.state.gyro = Utils::applyFilter(_model.state.gyroFilter, _model.state.gyro);

      _model.setDebug(DEBUG_GYRO_SAMPLE, 3, lrintf(degrees(_model.state.gyro[_model.config.debugAxis])));

      if(_dyn_notch_enabled || _dyn_notch_debug)
      {
        _model.state.gyroDynNotch = _dyn_notch_sma.update(_model.state.gyro);
      }

      if(_dyn_notch_enabled)
      {
        for(size_t p = 0; p < (size_t)_model.config.dynamicFilter.width; p++)
        {
          _model.state.gyro = Utils::applyFilter(_model.state.gyroDynNotchFilter[p], _model.state.gyro);
        }
      }

      for(size_t i = 0; i < 3; ++i)
      {
        _model.setDebug(DEBUG_GYRO_FILTERED, i, lrintf(degrees(_model.state.gyro[i])));
      }

      if(_model.accelActive())
      {
        _model.state.gyroImu = Utils::applyFilter(_model.state.gyroImuFilter, _model.state.gyro);
      }

      return 1;
    }

    void postLoop()
    {
      rpmFilterUpdate();
      dynNotchFilterUpdate();
    }

    void rpmFilterUpdate()
    {
      if(!_rpm_enabled) return;

      const float minFreq = _model.config.rpmFilterMinFreq;
      const float maxFreq = 0.48f * _model.state.loopTimer.rate;
      const float q = _model.config.rpmFilterQ * 0.01f;
      // TODO: optimize, to update filters in steps
      for(size_t m = 0; m < RPM_FILTER_MOTOR_MAX; m++)
      {
        const float motorFreq = _model.state.outputTelemetryFreq[m];
        for(size_t n = 0; n < _model.config.rpmFilterHarmonics; n++)
        {
          const float freq = motorFreq * (n + 1);
          if(minFreq < freq && freq < maxFreq)
          {
            for(size_t i = 0; i < 3; ++i)
            {
              _model.state.rpmFilter[m][n][i].reconfigure(freq, freq, q);
            }
          }
        }
      }
    }

    void dynNotchFilterUpdate()
    {
      if(!_model.gyroActive()) return;
      if(_model.state.loopTimer.rate < DynamicFilterConfig::MIN_FREQ) return;

      if(_dyn_notch_enabled || _dyn_notch_debug)
      {
        Stats::Measure measure(_model.state.stats, COUNTER_GYRO_FFT);

        const float q = _model.config.dynamicFilter.q * 0.01;
        bool feed = _model.state.loopTimer.iteration % _dyn_notch_denom == 0;
        bool update = _model.state.dynamicFilterTimer.check();

        for(size_t i = 0; i < 3; ++i)
        {
#ifdef ESPFC_DSP
          (void)update;
          if(feed)
          {
            uint32_t startTime = micros();
            int status = _fft[i].update(_model.state.gyroDynNotch[i]);
            if(_model.config.debugMode == DEBUG_FFT_TIME)
            {
              if(i == 0) _model.state.debug[0] = status;
              _model.state.debug[i + 1] = micros() - startTime;
            }
            if(_model.config.debugMode == DEBUG_FFT_FREQ && i == _model.config.debugAxis)
            {
              _model.state.debug[0] = lrintf(_fft[i].peaks[0].freq);
              _model.state.debug[1] = lrintf(_fft[i].peaks[1].freq);
              _model.state.debug[2] = lrintf(_fft[i].peaks[2].freq);
              _model.state.debug[3] = lrintf(_fft[i].peaks[3].freq);
            }
            if(_dyn_notch_enabled && status)
            {
              for(size_t p = 0; p < (size_t)_model.config.dynamicFilter.width; p++)
              {
                float freq = _fft[i].peaks[p].freq;
                if(freq >= _model.config.dynamicFilter.min_freq && freq <= _model.config.dynamicFilter.max_freq)
                {
                  _model.state.gyroDynNotchFilter[p][i].reconfigure(freq, freq, q);
                }
              }
            }
          }
#else
          if(feed)
          {
            uint32_t startTime = micros();
            _freqAnalyzer[i].update(_model.state.gyroDynNotch[i]);
            float freq = _freqAnalyzer[i].freq;
            if(_model.config.debugMode == DEBUG_FFT_TIME)
            {
              if(i == 0) _model.state.debug[0] = update;
              _model.state.debug[i + 1] = micros() - startTime;
            }
            if(_model.config.debugMode == DEBUG_FFT_FREQ)
            {
              if(update) _model.state.debug[i] = lrintf(freq);
              if(i == _model.config.debugAxis) _model.state.debug[3] = lrintf(degrees(_model.state.gyroDynNotch[i]));
            }
            if(_dyn_notch_enabled && update)
            {
              if(freq >= _model.config.dynamicFilter.min_freq && freq <= _model.config.dynamicFilter.max_freq)
              {
                for(size_t p = 0; p < (size_t)_model.config.dynamicFilter.width; p++)
                {
                  size_t x = (p + i) % 3;
                  int harmonic = (p / 3) + 1;
                  int16_t f = Math::clamp((int16_t)lrintf(freq * harmonic), _model.config.dynamicFilter.min_freq, _model.config.dynamicFilter.max_freq);
                  _model.state.gyroDynNotchFilter[p][x].reconfigure(f, f, q);
                }
              }
            }
          }
#endif
        }
      }
    }

  private:
    void calibrate()
    {
      switch(_model.state.gyroCalibrationState)
      {
        case CALIBRATION_IDLE:
          _model.state.gyro -= _model.state.gyroBias;
          break;
        case CALIBRATION_START:
          //_model.state.gyroBias = VectorFloat();
          _model.state.gyroBiasSamples = 2 * _model.state.gyroCalibrationRate;
          _model.state.gyroCalibrationState = CALIBRATION_UPDATE;
          break;
        case CALIBRATION_UPDATE:
          {
            VectorFloat deltaAccel = _model.state.accel - _model.state.accelPrev;
            _model.state.accelPrev = _model.state.accel;
            if(deltaAccel.getMagnitude() < ESPFC_FUZZY_ACCEL_ZERO && _model.state.gyro.getMagnitude() < ESPFC_FUZZY_GYRO_ZERO)
            {
              _model.state.gyroBias += (_model.state.gyro - _model.state.gyroBias) * _model.state.gyroBiasAlpha;
              _model.state.gyroBiasSamples--;
            }
            if(_model.state.gyroBiasSamples <= 0) _model.state.gyroCalibrationState = CALIBRATION_APPLY;
          }
          break;
        case CALIBRATION_APPLY:
          _model.state.gyroCalibrationState = CALIBRATION_SAVE;
          break;
        case CALIBRATION_SAVE:
          _model.finishCalibration();
          _model.state.gyroCalibrationState = CALIBRATION_IDLE;
          break;
        default:
          _model.state.gyroCalibrationState = CALIBRATION_IDLE;
          break;
      }
    }

    Math::Sma<VectorFloat, 8> _sma;
    Math::Sma<VectorFloat, 8> _dyn_notch_sma;
    size_t _dyn_notch_denom;
    bool _dyn_notch_enabled;
    bool _dyn_notch_debug;
    bool _rpm_enabled;

    Model& _model;
    Device::GyroDevice * _gyro;

#ifdef ESPFC_DSP
    Math::FFTAnalyzer<128> _fft[3];
#else
    Math::FreqAnalyzer _freqAnalyzer[3];
#endif

};

}

}
#endif
