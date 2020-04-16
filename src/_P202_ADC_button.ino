#ifdef USES_P202

// #######################################################################################################
// #################################### Plugin 202: Analog ###############################################
// #######################################################################################################

#define PLUGIN_202
#define PLUGIN_ID_202         2
#define PLUGIN_NAME_202       "Analog Button"
#define PLUGIN_VALUENAME1_202 "Analog"

#include "_Plugin_Helper.h"

#include "ADCKeyb.h"

#ifdef ESP32
  # define P202_MAX_ADC_VALUE    4095
#endif // ifdef ESP32
#ifdef ESP8266
  # define P202_MAX_ADC_VALUE    1023
#endif // ifdef ESP8266

uint32_t Plugin_202_OversamplingValue  = 0;
uint16_t Plugin_202_OversamplingCount  = 0;
uint16_t Plugin_202_OversamplingMinVal = P202_MAX_ADC_VALUE;
uint16_t Plugin_202_OversamplingMaxVal = 0;


//ADCKeybClass *AdcKeyb;

struct P202_data_struct : public PluginTaskData_base {
  P202_data_struct() :  P202_ADCKeyb() {}

  ~P202_data_struct() {
    reset();
  }

  void reset() {

    //addLog(LOG_LEVEL_INFO, F(logMes("1")));

    if (P202_ADCKeyb != nullptr) {
      delete P202_ADCKeyb;
      P202_ADCKeyb = nullptr;
    }
    //addLog(LOG_LEVEL_INFO, F(logMes("2")));
  }

  bool init() {

    reset();
    //addLog(LOG_LEVEL_INFO, F(logMes("3")));
    P202_ADCKeyb = new ADCKeybClass();
    if (isInitialized()) {
      return true;
    }
    return false;
  }

  bool isInitialized() const {
    return P202_ADCKeyb != nullptr;
  }

  bool loop(struct EventStruct *event) {
    if (!isInitialized()) {
      return false;
    }
    if (P202_ADCKeyb != nullptr) {
      P202_ADCKeyb->Pool();
      keymes t = P202_ADCKeyb->queue_get();
      if (t.f!=0) {
             //t = P202_data->P202_ADCKeyb->queue_get();
             String log  = F("BSG1: ");
             log    += UserVar[event->BaseVarIndex];
             addLog(LOG_LEVEL_DEBUG, log);
       		   UserVar[event->BaseVarIndex] = t.key*10 + t.f;
             event->sensorType = SENSOR_TYPE_SWITCH;
             sendData(event);
      } 
    }
    return true;
  }
  
  
private:
  ADCKeybClass *P202_ADCKeyb = nullptr;

};


boolean Plugin_202(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)  {
    case PLUGIN_DEVICE_ADD:    {
      Device[++deviceCount].Number           = PLUGIN_ID_202;
      Device[deviceCount].Type               = DEVICE_TYPE_ANALOG;
      Device[deviceCount].VType              = SENSOR_TYPE_SWITCH;
      Device[deviceCount].Ports              = 0;
      Device[deviceCount].PullUpOption       = false;
      Device[deviceCount].InverseLogicOption = false;
      Device[deviceCount].FormulaOption      = false;
      Device[deviceCount].ValueCount         = 1;
      Device[deviceCount].SendDataOption     = false;
      Device[deviceCount].TimerOption        = true;
      Device[deviceCount].GlobalSyncOption   = false;
      break;
    }
    case PLUGIN_GET_DEVICENAME:   {
      string = F(PLUGIN_NAME_202);
      break;
    }
    case PLUGIN_GET_DEVICEVALUENAMES:   {
      strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_202));
      break;
    }
    case PLUGIN_WEBFORM_LOAD:   {
        #if defined(ESP32)
      addHtml(F("<TR><TD>Analog Pin:<TD>"));
      addPinSelect(false, F("taskdevicepin1"), CONFIG_PIN1);
        #endif // if defined(ESP32)

      addFormCheckBox(F("Oversampling"), F("P202_oversampling"), PCONFIG(0));

      addFormSubHeader(F("Two Point Calibration"));

      addFormCheckBox(F("Calibration Enabled"), F("P202_cal"), PCONFIG(3));

      addFormNumericBox(F("Point 1"), F("P202_adc1"), PCONFIG_LONG(0), 0, P202_MAX_ADC_VALUE);
      html_add_estimate_symbol();
      addTextBox(F("P202_out1"), String(PCONFIG_FLOAT(0), 3), 10);

      addFormNumericBox(F("Point 2"), F("P202_adc2"), PCONFIG_LONG(1), 0, P202_MAX_ADC_VALUE);
      html_add_estimate_symbol();
      addTextBox(F("P202_out2"), String(PCONFIG_FLOAT(1), 3), 10);

      success = true;
      break;
    }
    case PLUGIN_WEBFORM_SAVE:   {
      PCONFIG(0) = isFormItemChecked(F("P202_oversampling"));

      PCONFIG(3) = isFormItemChecked(F("P202_cal"));

      PCONFIG_LONG(0)  = getFormItemInt(F("P202_adc1"));
      PCONFIG_FLOAT(0) = getFormItemFloat(F("P202_out1"));

      PCONFIG_LONG(1)  = getFormItemInt(F("P202_adc2"));
      PCONFIG_FLOAT(1) = getFormItemFloat(F("P202_out2"));

      success = true;
      break;
    }
    case PLUGIN_INIT: { 
      initPluginTaskData(event->TaskIndex, new P202_data_struct());
      P202_data_struct *P202_data =
        static_cast<P202_data_struct *>(getPluginTaskData(event->TaskIndex));
        if (nullptr == P202_data) {
          return false;
        }
     if (P202_data->init()) {
        success = true;
      } else {
        clearPluginTaskData(event->TaskIndex);
      }        
      break;
    }  
    case PLUGIN_EXIT: {
      clearPluginTaskData(event->TaskIndex);
      success = true;
      break;
    }
    case PLUGIN_TEN_PER_SECOND: { // Fall through to PLUGIN_TEN_PER_SECOND
    //case PLUGIN_FIFTY_PER_SECOND: {
      if (Settings.TaskDeviceEnabled[event->TaskIndex]) {
        P202_data_struct *P202_data =
          static_cast<P202_data_struct *>(getPluginTaskData(event->TaskIndex));

        if ((nullptr != P202_data) && P202_data->loop(event)) {
           
             // schedule_task_device_timer(event->TaskIndex, millis() + 10);
             delay(0); // Processing a full sentence may take a while, run some
                      // background tasks.
             //P087_data->getSentence(event->String2);
             //sendData(event);
        }
        success = true;
      }
      break;
    }
    case PLUGIN_READ:   {
        success = true;
        break;
    }
  } // switch
  return success;
}

#endif // USES_P202
