#ifndef _ESPFC_CLI_H_
#define _ESPFC_CLI_H_

#include <cstring>
#include <cctype>

extern "C" {
#include "user_interface.h"
}

#include "Model.h"
#include "Msp.h"
#include "Hardware.h"
#include "Logger.h"

namespace Espfc {

class Cli
{
  public:
    class Cmd
    {
      public:
        Cmd() { for(size_t i = 0; i < ARGS_SIZE; ++i) args[i] = NULL; }
        static const size_t ARGS_SIZE = 8;
        const char * args[ARGS_SIZE];
    };

    enum ParamType {
      PARAM_NONE,   // unused
      PARAM_BOOL,   // boolean
      PARAM_BYTE,   // 8 bit int
      PARAM_SHORT,  // 16 bit int
      PARAM_INT,    // 32 bit int
      PARAM_FLOAT   // 32 bit float
    };

    class Param
    {
      public:
        Param(): Param(NULL, NULL, PARAM_NONE) {}
        Param(const char * n, char * a, ParamType t): name(n), addr(a), type(t) {}
        Param(const char * n, bool    * a): Param(n, (char*)a, PARAM_BOOL) {}
        Param(const char * n, int8_t  * a): Param(n, (char*)a, PARAM_BYTE) {}
        Param(const char * n, int16_t * a): Param(n, (char*)a, PARAM_SHORT) {}
        Param(const char * n, int32_t * a): Param(n, (char*)a, PARAM_INT) {}
        Param(const Param& c): Param(c.name, c.addr, c.type) {}

        void print(Stream& stream) const
        {
          if(!addr)
          {
            stream.print(F("UNSET"));
            return;
          }
          switch(type)
          {
            case PARAM_NONE:  stream.print("NONE"); break;
            case PARAM_BOOL:  stream.print(*addr != 0); break;
            case PARAM_BYTE:  stream.print((int8_t)(*addr)); break;
            case PARAM_SHORT: stream.print(*reinterpret_cast<int16_t*>(addr)); break;
            case PARAM_INT:   stream.print(*reinterpret_cast<int32_t*>(addr)); break;
            case PARAM_FLOAT: stream.print(*reinterpret_cast<float*>(addr), 4); break;
          }
        }

        void update(const char * v)
        {
          if(!addr || !v) return;
          switch(type)
          {
            case PARAM_NONE:  break;
            case PARAM_BOOL:
              if(*v == '0') *addr = 0;
              if(*v == '1') *addr = 1;
              break;
            case PARAM_BYTE:
            case PARAM_SHORT:
            case PARAM_INT:
              {
                String tmp = v;
                *addr = tmp.toInt();
              }
              break;
            case PARAM_FLOAT:
              {
                String tmp = v;
                *addr = tmp.toFloat();
              }
              break;
          }
        }
        const char * name;
        char * addr;
        ParamType type;
    };

    Cli(Model& model): _model(model), _index(0), _msp(model), _ignore(false)
    {
      ModelConfig * c = &_model.config;
      size_t i = 0;
      _params[i++] = Param(PSTR("telemetry"), &c->telemetry);
      _params[i++] = Param(PSTR("telemetry_interval"), &c->telemetryInterval);
      _params[i++] = Param(PSTR("accel_mode"), &c->accelMode);
      _params[i++] = Param(PSTR("gyro_rate"), &c->gyroSampleRate);
      _params[i++] = Param(PSTR("mag_rate"), &c->magSampleRate);
      _params[i++] = Param(PSTR("angle_limit"), &c->angleLimit);
      _params[i++] = Param(PSTR("angle_rate_limit"), &c->angleRateLimit);
      _params[i++] = Param(PSTR("gyro_cal_x"), &c->gyroBias[0]);
      _params[i++] = Param(PSTR("gyro_cal_y"), &c->gyroBias[1]);
      _params[i++] = Param(PSTR("gyro_cal_z"), &c->gyroBias[2]);  // 10

      _params[i++] = Param(PSTR("accel_cal_x"), &c->accelBias[0]);
      _params[i++] = Param(PSTR("accel_cal_y"), &c->accelBias[1]);
      _params[i++] = Param(PSTR("accel_cal_z"), &c->accelBias[2]);
      _params[i++] = Param(PSTR("mag_cal_offset_x"), &c->magCalibrationOffset[0]);
      _params[i++] = Param(PSTR("mag_cal_offset_y"), &c->magCalibrationOffset[1]);
      _params[i++] = Param(PSTR("mag_cal_offset_z"), &c->magCalibrationOffset[2]);
      _params[i++] = Param(PSTR("mag_cal_scale_x"), &c->magCalibrationScale[0]);
      _params[i++] = Param(PSTR("mag_cal_scale_y"), &c->magCalibrationScale[1]);
      _params[i++] = Param(PSTR("mag_cal_scale_z"), &c->magCalibrationScale[2]);
      _params[i++] = Param(PSTR("pin_out_0"), &c->outputPin[0]); // 20

      _params[i++] = Param(PSTR("pin_out_1"), &c->outputPin[1]);
      _params[i++] = Param(PSTR("pin_out_2"), &c->outputPin[2]);
      _params[i++] = Param(PSTR("pin_out_3"), &c->outputPin[3]);
      _params[i++] = Param(PSTR("pin_ppm"), &c->ppmPin);
      _params[i++] = Param(PSTR("loop_sync"), &c->loopSync);
      _params[i++] = Param(PSTR("mixer_sync"), &c->mixerSync);
    }

    int begin()
    {
      _stream = (Stream*)Hardware::getSerialPort((SerialPort)_model.config.cliPort);
      return 1;
    }

    int update()
    {
      if(!_stream) return 0;

      while((*_stream).available() > 0)
      {
        char c = (*_stream).read();
        bool consumed = _msp.process(c, *_stream);
        if(!consumed)
        {
          process(c);
        }
      }
    }

    bool process(const char c)
    {
      bool endl = c == '\n' || c == '\r';
      if(_index && endl)
      {
        parse();
        execute();
        _index = 0;
        _buff[_index] = '\0';
        return true;
      }

      if(c == '#') _ignore = true;
      else if(endl) _ignore = false;

      // don't put characters into buffer in specific conditions
      if(_ignore || endl || _index >= BUFF_SIZE - 1) return false;

      if(c == '\b') // handle backspace
      {
        _buff[--_index] = '\0';
      }
      else
      {
        _buff[_index] = c;
        _buff[++_index] = '\0';
      }
      return false;
    }

    void parse()
    {
      _cmd = Cmd();
      const char * DELIM = " \t";
      char * pch = std::strtok(_buff, DELIM);
      size_t count = 0;
      while(pch)
      {
        _cmd.args[count++] = pch;
        pch = std::strtok(NULL, DELIM);
      }
    }

    void execute()
    {
      if(_cmd.args[0]) print(F("# "));
      for(size_t i = 0; i < Cmd::ARGS_SIZE; ++i)
      {
        if(!_cmd.args[i]) break;
        print(_cmd.args[i]);
        print(' ');
      }
      println();

      if(!_cmd.args[0]) return;

      if(strcmp_P(_cmd.args[0], PSTR("help")) == 0)
      {
        println(F("available commands:\n "
          "help\n dump\n get param\n set param value\n "
          "calgyro\n calmag\n calinfo\n "
          "load\n save\n eeprom\n defaults\n reboot\n "
          "fsinfo\n fsformat\n logs\n log\n "
          "stats\n info\n version"
        ));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("version")) == 0)
      {
        println(F("0.1"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("info")) == 0)
      {
        print(F(" bool: ")); println(sizeof(bool));
        print(F(" char: ")); println(sizeof(char));
        print(F("short: ")); println(sizeof(short));
        print(F("  int: ")); println(sizeof(int));
        print(F(" long: ")); println(sizeof(long));
        print(F("float: ")); println(sizeof(float));
        print(F("model: ")); println(sizeof(ModelConfig));
        println();

        const rst_info * resetInfo = system_get_rst_info();
        print(F("system_get_rst_info() reset reason: "));
        println(resetInfo->reason);

        print(F("system_get_free_heap_size(): "));
        println(system_get_free_heap_size());

        print(F("system_get_os_print(): "));
        println(system_get_os_print());

        //system_print_meminfo();

        print(F("system_get_chip_id(): 0x"));
        println(system_get_chip_id(), HEX);

        print(F("system_get_sdk_version(): "));
        println(system_get_sdk_version());

        print(F("system_get_boot_version(): "));
        println(system_get_boot_version());

        print(F("system_get_userbin_addr(): 0x"));
        println(system_get_userbin_addr(), HEX);

        print(F("system_get_boot_mode(): "));
        println(system_get_boot_mode() == 0 ? F("SYS_BOOT_ENHANCE_MODE") : F("SYS_BOOT_NORMAL_MODE"));

        print(F("system_get_cpu_freq(): "));
        println(system_get_cpu_freq());

        print(F("system_get_flash_size_map(): "));
        println(system_get_flash_size_map());

        print(F("system_get_time(): "));
        println(system_get_time() / 1000000);
      }
      else if(strcmp_P(_cmd.args[0], PSTR("get")) == 0)
      {
        if(!_cmd.args[1])
        {
          println(F("param required"));
          println();
          return;
        }
        bool found = false;
        for(size_t i = 0; i < PARAM_SIZE; ++i)
        {
          if(!_params[i].name) continue;

          if(strcmp_P(_cmd.args[1], _params[i].name) == 0)
          {
            print(_params[i]);
            println();
            found = true;
            break;
          }
        }
        if(!found)
        {
          print(F("param not found: "));
          println(_cmd.args[1]);
        }
      }
      else if(strcmp_P(_cmd.args[0], PSTR("set")) == 0)
      {
        if(!_cmd.args[1])
        {
          println(F("param required"));
          println();
          return;
        }
        bool found = false;
        for(size_t i = 0; i < PARAM_SIZE; ++i)
        {
          if(!_params[i].name) continue;

          if(strcmp_P(_cmd.args[1], _params[i].name) == 0)
          {
            _params[i].update(_cmd.args[2]);
            print(_params[i]);
            found = true;
            break;
          }
        }
        if(!found)
        {
          print(F("param not found: "));
          println(_cmd.args[1]);
        }
      }
      else if(strcmp_P(_cmd.args[0], PSTR("dump")) == 0)
      {
        for(size_t i = 0; i < PARAM_SIZE; ++i)
        {
          if(!_params[i].name) continue;
          print(_params[i]);
        }
      }
      else if(strcmp_P(_cmd.args[0], PSTR("calmag")) == 0)
      {
        if(!_cmd.args[1]) {}
        else if(_cmd.args[1][0] == '1')
        {
          _model.state.magCalibration = 1;
          //_model.config.telemetry = 1;
          //_model.config.telemetryInterval = 200;
          print(F("mag calibration on"));
        }
        else if(_cmd.args[1][0] == '0')
        {
          _model.state.magCalibration = 0;
          //_model.config.telemetry = 0;
          print(F("mag calibration off"));
        }
      }
      else if(strcmp_P(_cmd.args[0], PSTR("calgyro")) == 0)
      {
        _model.calibrate();
        println(F("OK"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("calinfo")) == 0)
      {
        print(F("gyro offset: "));
        print(_model.config.gyroBias[0]); print(' ');
        print(_model.config.gyroBias[1]); print(' ');
        print(_model.config.gyroBias[2]); print(F(" ["));
        print(_model.state.gyroBias[0]); print(' ');
        print(_model.state.gyroBias[1]); print(' ');
        print(_model.state.gyroBias[2]); println(F("]"));

        print(F("accel offset: "));
        print(_model.config.accelBias[0]); print(' ');
        print(_model.config.accelBias[1]); print(' ');
        print(_model.config.accelBias[2]); print(F(" ["));
        print(_model.state.accelBias[0]); print(' ');
        print(_model.state.accelBias[1]); print(' ');
        print(_model.state.accelBias[2]); println(F("]"));

        print(F("mag offset: "));
        print(_model.config.magCalibrationOffset[0]); print(' ');
        print(_model.config.magCalibrationOffset[1]); print(' ');
        print(_model.config.magCalibrationOffset[2]); print(F(" ["));
        print(_model.state.magCalibrationOffset[0]); print(' ');
        print(_model.state.magCalibrationOffset[1]); print(' ');
        print(_model.state.magCalibrationOffset[2]); println(F("]"));

        print(F("mag scale: "));
        print(_model.config.magCalibrationScale[0]); print(' ');
        print(_model.config.magCalibrationScale[1]); print(' ');
        print(_model.config.magCalibrationScale[2]); print(F(" ["));
        print(_model.state.magCalibrationScale[0]); print(' ');
        print(_model.state.magCalibrationScale[1]); print(' ');
        print(_model.state.magCalibrationScale[2]); println(F("]"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("load")) == 0)
      {
        _model.load();
        println(F("OK"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("save")) == 0)
      {
        _model.save();
        println(F("OK"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("eeprom")) == 0)
      {
        int start = 0;
        if(_cmd.args[1])
        {
          start = std::max(String(_cmd.args[1]).toInt(), 0L);
        }

        //start = ((char*)&_model.config.gyroBias - (char*)&_model.config) + 2;
        //println(start);

        for(int i = start; i < start + 32; ++i)
        {
          print(EEPROM.read(i), HEX);
          print(' ');
        }
        println();

        for(int i = start; i < start + 32; ++i)
        {
          print((int8_t)EEPROM.read(i));
          print(' ');
        }
        println();

        for(int i = start; i < start + 32; i += 2)
        {
          int16_t v = EEPROM.read(i) | EEPROM.read(i + 1) << 8;
          print(v);
          print(' ');
        }
        println();
      }
      else if(strcmp_P(_cmd.args[0], PSTR("stats")) == 0)
      {
        for(size_t i = 0; i < COUNTER_COUNT; ++i)
        {
          print(FPSTR(_model.state.stats.getName((StatCounter)i)));
          print(": ");
          print((int)_model.state.stats.getTime((StatCounter)i));
          print("us, ");
          print(_model.state.stats.getLoad((StatCounter)i), 1);
          print("%");
          println();
        }
        print(F("TOTAL       : "));
        print((int)_model.state.stats.getTotalTime());
        print(F("us, "));
        print(_model.state.stats.getTotalLoad(), 1);
        print(F("%"));
        println();
      }
      else if(strcmp_P(_cmd.args[0], PSTR("fsinfo")) == 0)
      {
        _model.logger.info(_stream);
      }
      else if(strcmp_P(_cmd.args[0], PSTR("fsformat")) == 0)
      {
        _model.logger.format();
        println(F("OK"));
      }
      else if(strcmp_P(_cmd.args[0], PSTR("logs")) == 0)
      {
        _model.logger.list(_stream);
      }
      else if(strcmp_P(_cmd.args[0], PSTR("log")) == 0)
      {
        if(!_cmd.args[1])
        {
          println(F("missing log id"));
          println();
          return;
        }
        int id = String(_cmd.args[1]).toInt();
        if(!id)
        {
          println(F("invalid log id"));
          println();
          return;
        }
        _model.logger.show(_stream, id);
      }
      else if(strcmp_P(_cmd.args[0], PSTR("reboot")) == 0)
      {
        ESP.restart();
      }
      else
      {
        print(F("command not found: "));
        println(_cmd.args[0]);
      }
      println();
    }

  private:
    template<typename T>
    void print(const T& t)
    {
      if(!_stream) return;
      (*_stream).print(t);
    }

    template<typename T, typename V>
    void print(const T& t, const V& v)
    {
      if(!_stream) return;
      (*_stream).print(t, v);
    }

    template<typename T>
    void println(const T& t)
    {
      if(!_stream) return;
      (*_stream).println(t);
    }

    template<typename T, typename V>
    void println(const T& t, const V& v)
    {
      if(!_stream) return;
      (*_stream).println(t, v);
    }

    void println()
    {
      if(!_stream) return;
      (*_stream).println();
    }

    void print(const Param& param)
    {
      if(!_stream) return;
      print(F("set "));
      print(FPSTR(param.name));
      print(' ');
      param.print(*_stream);
      println();
    }

    static const size_t PARAM_SIZE = 32;
    static const size_t BUFF_SIZE = 64;

    Model& _model;
    Stream * _stream;
    Param _params[PARAM_SIZE];
    char _buff[BUFF_SIZE];
    size_t _index;
    Cmd _cmd;
    Msp _msp;
    bool _ignore;
};

}

#endif
