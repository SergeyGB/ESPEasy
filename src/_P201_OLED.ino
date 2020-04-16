#include "_Plugin_Helper.h"
//#include <LiquidCrystal_I2C.h>

//uncomment one of the following as needed
//#ifdef PLUGIN_BUILD_DEVELOPMENT
//#ifdef PLUGIN_BUILD_TESTING

#define PLUGIN_201
#define PLUGIN_ID_201     201               //plugin id
#define PLUGIN_NAME_201   "BSG_OLED"     //"Plugin Name" is what will be dislpayed in the selection list
#define PLUGIN_VALUENAME1_201 "output1"     //variable output of the plugin. The label is in quotation marks
#define PLUGIN_201_DEBUG  true             //set to true for extra log info in the debug

#define P201_Nlines 12         // The number of different lines which can be displayed - each line is 64 chars max
#define P201_NcharsV0 32       // max chars per line up to 22.11.2019 (V0)
#define P201_NcharsV1 64       // max chars per line from 22.11.2019 (V1)
#define P201_MaxSizesCount 3   // number of different OLED sizes

#define P201_MaxDisplayWidth 128
#define P201_MaxDisplayHeight 64
#define P201_DisplayCentre 64

#define P201_CONTRAST_OFF    1
#define P201_CONTRAST_LOW    64
#define P201_CONTRAST_MED  0xCF
#define P201_CONTRAST_HIGH 0xFF


#include "SPI.h"
#include "SSD1306.h"
#include "SSD1306Spi.h"
#include "OLED_SSD1306_SH1106_images.h"
#include "Dialog_Plain_12_font.h"

#define P201_WIFI_STATE_UNSET          -2
#define P201_WIFI_STATE_NOT_CONNECTED  -1
#define P201_MAX_LinesPerPage          4
#define P201_WaitScrollLines           5   // wait 0.5s before and after scrolling line
#define P201_PageScrollTimer           25  // timer for page Scrolling
#define P201_PageScrollTick            (P201_PageScrollTimer+20)  // total time for one PageScrollTick (including the handling time of 20ms in PLUGIN_TIMER_IN)
#define P201_PageScrollPix             4  // min pixel change while page scrolling

// DISP - ESP8266  -  ESP32    -  PinBoard
// GND                         - 6
// VCC                         - 7 
//  DO  - 14 SCK   -  18 SCK   - 3 
//  DI  - 13 MISO  -  19 MISO  - 1 (рядом с jtag)
// RES  - nc       -  nc       -    
//  DC  - 16       -  16       - 5
//  CS  - 15 SS    -  05 SS    - 4 
//                    23 MOSI  - 2 

uint8_t Plugin_201_SPI_CS_Pin = 15;  
uint8_t Plugin_201_SPI_DC_Pin = 16;  

static int8_t lastWiFiState = P201_WIFI_STATE_UNSET;
static uint8_t OLEDIndex = 0;
static boolean bPin3Invers;
static boolean bScrollLines;
static boolean bAlternativHeader = false;
static uint16_t HeaderCount = 0;
static boolean bPageScrollDisabled = true;   // first page after INIT without scrolling
static uint8_t TopLineOffset = 0;   // Offset for top line, used for rotated image while using displays < P201_MaxDisplayHeight lines

enum eHeaderContent {
    eSSID = 1,
    eSysName = 2,
    eIP = 3,
    eMAC = 4,
    eRSSI = 5,
    eBSSID = 6,
    eWiFiCh = 7,
    eUnit = 8,
    eSysLoad = 9,
    eSysHeap = 10,
    eSysStack = 11,
    eTime = 12,
    eDate = 13,
    ePageNo = 14,
};

static eHeaderContent HeaderContent=eSysName;
static eHeaderContent HeaderContentAlternative=eSysName;
static uint8_t MaxFramesToDisplay = 0xFF;
static uint8_t currentFrameToDisplay = 0;
static uint8_t nextFrameToDisplay = 0xFF;  // next frame because content changed in PLUGIN_WRITE

typedef struct {
  uint8_t       Top;                  // top in pix for this line setting
  const char    *fontData;            // font for this line setting
  uint8_t       Space;                // space in pix between lines for this line setting
} tFontSettings;

typedef struct {
  uint8_t       Width;                // width in pix
  uint8_t       Height;               // height in pix
  uint8_t       PixLeft;              // first left pix position
  uint8_t       MaxLines;             // max. line count
  tFontSettings L1;                   // settings for 1 line
  tFontSettings L2;                   // settings for 2 lines
  tFontSettings L3;                   // settings for 3 lines
  tFontSettings L4;                   // settings for 4 lines
  uint8_t       WiFiIndicatorLeft;    // left of WiFi indicator
  uint8_t       WiFiIndicatorWidth;   // width of WiFi indicator
} tSizeSettings;

const tSizeSettings SizeSettings[P201_MaxSizesCount] = {
   { P201_MaxDisplayWidth, P201_MaxDisplayHeight, 0,   // 128x64
     4,
     { 20, ArialMT_Plain_24, 28},  //  Width: 24 Height: 28
     { 15, ArialMT_Plain_16, 19},  //  Width: 16 Height: 19
     { 13, Dialog_plain_12,  12},  //  Width: 13 Height: 15
     { 12, ArialMT_Plain_10, 10},  //  Width: 10 Height: 13
     105,
     15
   },
   { P201_MaxDisplayWidth, 32, 0,               // 128x32
     2,
     { 14, Dialog_plain_12,  15},  //  Width: 13 Height: 15
     { 12, ArialMT_Plain_10, 10},  //  Width: 10 Height: 13
     {  0, ArialMT_Plain_10,  0},  //  Width: 10 Height: 13 not used!
     {  0, ArialMT_Plain_10,  0},  //  Width: 10 Height: 13 not used!
     105,
     10
   },
   { 64, 48, 32,               // 64x48
     3,
     { 20, ArialMT_Plain_24, 28},  //  Width: 24 Height: 28
     { 14, Dialog_plain_12,  16},  //  Width: 13 Height: 15
     { 13, ArialMT_Plain_10, 11},  //  Width: 10 Height: 13
     {  0, ArialMT_Plain_10,  0},  //  Width: 10 Height: 13 not used!
     32,
     10
   }
 };

 enum ePageScrollSpeed {
   ePSS_VerySlow = 1,   // 800ms
   ePSS_Slow = 2,       // 400ms
   ePSS_Fast = 4,       // 200ms
   ePSS_VeryFast = 8,   // 100ms
   ePSS_Instant = 32    // 20ms
};

typedef struct {
   String        Content;              // content
   uint16_t      LastWidth;            // width of last line in pix
   uint16_t      Width;                // width in pix
   uint8_t       Height;               // Height in Pix
   uint8_t       ypos;                 // y position in pix
   int           CurrentLeft;          // current left pix position
   float         dPix;                 // pix change per scroll time (100ms)
   float         fPixSum;              // pix sum while scrolling (100ms)
} tScrollLine;
typedef struct {
  const char    *Font;                 // font for this line setting
  uint8_t       Space;                 // space in pix between lines for this line setting
  uint16_t      wait;                  // waiting time before scrolling
  tScrollLine   Line[P201_MAX_LinesPerPage];
} tScrollingLines;
static tScrollingLines ScrollingLines;

typedef struct {
  uint8_t       Scrolling;                    // 0=Ready, 1=Scrolling
  const char    *Font;                        // font for this line setting
  uint8_t       dPix;                         // pix change per scroll time (25ms)
  int           dPixSum;                      // act pix change
  uint8_t       linesPerFrame;                // the number of lines in each frame
  int           ypos[P201_MAX_LinesPerPage];   // ypos contains the heights of the various lines - this depends on the font and the number of lines
  String        newString[P201_MAX_LinesPerPage];
  String        oldString[P201_MAX_LinesPerPage];
} tScrollingPages;
static tScrollingPages ScrollingPages;

typedef struct {
  char         Content[P201_NcharsV1];
  uint8_t      FontType;
  uint8_t      FontHeight;
  uint8_t      FontSpace;
  uint8_t      reserved;
} tDisplayLines;

// CustomTaskSettings
tDisplayLines P201_DisplayLinesV1[P201_Nlines];    // holds the CustomTaskSettings for V1
String DisplayLinesV0[P201_Nlines];                // used to load the CustomTaskSettings for V0

// Instantiate display here - does not work to do this within the INIT call
OLEDDisplay *display=NULL;

void P201_setBitToUL(uint32_t& number, byte bitnr, bool value) {
  uint32_t mask = (0x01UL << bitnr);
  uint32_t newbit = (value ? 1UL : 0UL) << bitnr;
  number = (number & ~mask) | newbit;
}
uint8_t get8BitFromUL(uint32_t number, byte bitnr) {
  return (number >> bitnr) & 0xFF;
}
void set8BitToUL(uint32_t& number, byte bitnr, uint8_t value) {
  uint32_t mask = (0xFFUL << bitnr);
  uint32_t newvalue = ((value << bitnr) & mask);
  number = (number & ~mask) | newvalue;
}
uint8_t get4BitFromUL(uint32_t number, byte bitnr) {
  return (number >> bitnr) &  0x0F;
}
void set4BitToUL(uint32_t& number, byte bitnr, uint8_t value) {
  uint32_t mask = (0x0FUL << bitnr);
  uint32_t newvalue = ((value << bitnr) & mask);
  number = (number & ~mask) | newvalue;
}

void Plugin_201_loadDisplayLines(taskIndex_t taskIndex, uint8_t LoadVersion) {
  if (LoadVersion == 0) {
      // read data of version 0 (up to 22.11.2019)
      LoadCustomTaskSettings(taskIndex, DisplayLinesV0, P201_Nlines, P201_NcharsV0); // max. length 1024 Byte  (DAT_TASKS_CUSTOM_SIZE)
      for (int i = 0; i < P201_Nlines; ++i) {
        safe_strncpy(P201_DisplayLinesV1[i].Content, DisplayLinesV0[i], P201_NcharsV1);
        P201_DisplayLinesV1[i].Content[P201_NcharsV1-1] = 0; // Terminate in case of uninitalized data
        P201_DisplayLinesV1[i].FontType = 0xff;
        P201_DisplayLinesV1[i].FontHeight = 0xff;
        P201_DisplayLinesV1[i].FontSpace = 0xff;
        P201_DisplayLinesV1[i].reserved = 0xff;
      }
    }
    else {
      // read data of version 1 (beginning from 22.11.2019)
      LoadCustomTaskSettings(taskIndex, (uint8_t*)&P201_DisplayLinesV1, sizeof(P201_DisplayLinesV1));
      for (int i = 0; i < P201_Nlines; ++i) {
        P201_DisplayLinesV1[i].Content[P201_NcharsV1-1] = 0; // Terminate in case of uninitalized data
      }
  }
}

//#define PCONFIG_201_adr 0
//#define PCONFIGN_201_adr "P201_adr"
#define PCONFIG_201_rotate 0
#define PCONFIGN_201_rotate "P201_rotate"
#define PCONFIG_201_nlines 1
#define PCONFIGN_201_nlines "P201_nlines"
#define PCONFIG_201_scroll 2
#define PCONFIGN_201_scroll "P201_scroll"
#define PCONFIG_201_timer 3
#define PCONFIGN_201_timer "P201_timer"
//#define PCONFIG_201_controller 5
//#define PCONFIGN_201_controller "P201_controller"
#define PCONFIG_201_contrast 4
#define PCONFIGN_201_contrast "P201_contrast"
#define PCONFIG_201_size 5
#define PCONFIGN_201_size "P201_size"

boolean Plugin_201(uint8_t function, struct EventStruct *event, String& string) {
  boolean success = false;
  static uint8_t displayTimer = 0;
  static uint8_t frameCounter = 0;       // need to keep track of framecounter from call to call
  static uint8_t nrFramesToDisplay = 0;
  int NFrames;                // the number of frames
  switch (function)  {
    case PLUGIN_DEVICE_ADD:      {
        Device[++deviceCount].Number = PLUGIN_ID_201;
        Device[deviceCount].Type = DEVICE_TYPE_DUAL;
        Device[deviceCount].VType = SENSOR_TYPE_NONE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = false;
        Device[deviceCount].ValueCount = 0;
        Device[deviceCount].SendDataOption = false;
        Device[deviceCount].TimerOption = true;
    }  break;
    case PLUGIN_GET_DEVICENAME:   {
        string = F(PLUGIN_NAME_201);
    }  break;
    case PLUGIN_GET_DEVICEVALUENAMES: {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_201));  // OnOff
    }  break;
    case PLUGIN_WEBFORM_LOAD:  {
        // Use number 5 to remain compatible with existing configurations,
        // but the item should be one of the first choices.
        // addFormNote(F("<b>1st GPIO</b> = CS (Usable GPIOs : 0, 2, 4, 5, 15)"));
        //addFormSubHeader(F("Display"));
        String options8[P201_MaxSizesCount] = { F("128x64"), F("128x32"), F("64x48") };
        int optionValues8[P201_MaxSizesCount] = { 0, 1, 2 };
        addFormSelector(F("Size"),F(PCONFIGN_201_size), P201_MaxSizesCount, options8, optionValues8, NULL, PCONFIG(PCONFIG_201_size), true);

        uint8_t choice1 = PCONFIG(PCONFIG_201_rotate);
        String options1[2];
        options1[0] = F("Normal");
        options1[1] = F("Rotated");
        int optionValues1[2] = { 1, 2 };
        addFormSelector(F("Rotation"), F(PCONFIGN_201_rotate), 2, options1, optionValues1, choice1);

        OLEDIndex=PCONFIG(PCONFIG_201_size);
        addFormNumericBox(F("Lines per Frame"), F(PCONFIGN_201_nlines), PCONFIG(PCONFIG_201_nlines), 1, SizeSettings[OLEDIndex].MaxLines);

        uint8_t choice3 = PCONFIG(PCONFIG_201_scroll);
        String options3[5];
        options3[0] = F("Very Slow");
        options3[1] = F("Slow");
        options3[2] = F("Fast");
        options3[3] = F("Very Fast");
        options3[4] = F("Instant");
        int optionValues3[5] = {ePSS_VerySlow, ePSS_Slow, ePSS_Fast, ePSS_VeryFast, ePSS_Instant};
        addFormSelector(F("Scroll"), F(PCONFIGN_201_scroll), 5, options3, optionValues3, choice3);
        uint8_t version = get4BitFromUL(PCONFIG_LONG(0), 20);    // Bit23-20 Version CustomTaskSettings

        Plugin_201_loadDisplayLines(event->TaskIndex, version);

        // FIXME TD-er: Why is this using pin3 and not pin1? And why isn't this using the normal pin selection functions?
        addFormPinSelect(F("Display button"), F("taskdevicepin3"), CONFIG_PIN3);
        bPin3Invers = getBitFromUL(PCONFIG_LONG(0), 16);  // Bit 16
        addFormCheckBox(F("Inversed Logic"), F("P201_pin3invers"), bPin3Invers);

        addFormNumericBox(F("Display Timeout"), F(PCONFIGN_201_timer), PCONFIG(PCONFIG_201_timer));

        uint8_t choice6 = PCONFIG(PCONFIG_201_contrast);
        if (choice6 == 0) choice6 = P201_CONTRAST_HIGH;
        String options6[3];
        options6[0] = F("Low");
        options6[1] = F("Medium");
        options6[2] = F("High");
        int optionValues6[3];
        optionValues6[0] = P201_CONTRAST_LOW;
        optionValues6[1] = P201_CONTRAST_MED;
        optionValues6[2] = P201_CONTRAST_HIGH;
        addFormSelector(F("Contrast"), F(PCONFIGN_201_contrast), 3, options6, optionValues6, choice6);
        addFormSubHeader(F("Content"));
        uint8_t choice9 = get8BitFromUL(PCONFIG_LONG(0), 8);    // Bit15-8 HeaderContent
        uint8_t choice10 = get8BitFromUL(PCONFIG_LONG(0), 0);   // Bit7-0 HeaderContentAlternative
        String options9[14] = { F("SSID"), F("SysName"), F("IP"), F("MAC"), F("RSSI"), F("BSSID"), F("WiFi channel"), F("Unit"), F("SysLoad"), F("SysHeap"), F("SysStack"), F("Date"), F("Time"), F("PageNumbers") };
        int optionValues9[14] = { eSSID, eSysName, eIP, eMAC, eRSSI, eBSSID, eWiFiCh, eUnit, eSysLoad, eSysHeap, eSysStack, eDate, eTime , ePageNo};
        addFormSelector(F("Header"),F("P201_header"), 14, options9, optionValues9, choice9);
        addFormSelector(F("Header (alternating)"),F("P201_headerAlternate"), 14, options9, optionValues9, choice10);

        bScrollLines = getBitFromUL(PCONFIG_LONG(0), 17);  // Bit 17
        addFormCheckBox(F("Scroll long lines"), F("P201_ScrollLines"), bScrollLines);

        for (uint8_t varNr = 0; varNr < P201_Nlines; varNr++)
        {
          addFormTextBox(String(F("Line ")) + (varNr + 1), getPluginCustomArgName(varNr), String(P201_DisplayLinesV1[varNr].Content), P201_NcharsV1-1);
        }
        success = true;
    }  break;
    case PLUGIN_WEBFORM_SAVE:  {
        //update now
        schedule_task_device_timer(event->TaskIndex,
           millis() + (Settings.TaskDeviceTimer[event->TaskIndex] * 1000));
        frameCounter=0;

        MaxFramesToDisplay = 0xFF;
        PCONFIG(PCONFIG_201_rotate) = getFormItemInt(F(PCONFIGN_201_rotate));
        PCONFIG(PCONFIG_201_nlines) = getFormItemInt(F(PCONFIGN_201_nlines));
        PCONFIG(PCONFIG_201_scroll) = getFormItemInt(F(PCONFIGN_201_scroll));
        PCONFIG(PCONFIG_201_timer) = getFormItemInt(F(PCONFIGN_201_timer));
        PCONFIG(PCONFIG_201_contrast) = getFormItemInt(F(PCONFIGN_201_contrast));
        PCONFIG(PCONFIG_201_size) = getFormItemInt(F(PCONFIGN_201_size));
        //PCONFIG(PCONFIG_201_adr) = getFormItemInt(F(PCONFIGN_201_rotate));
        //PCONFIG(PCONFIG_201_controller) = getFormItemInt(F(PCONFIGN_201_controller));

        uint32_t lSettings = 0;
        set8BitToUL(lSettings, 8, uint8_t(getFormItemInt(F("P201_header")) & 0xff));            // Bit15-8 HeaderContent
        set8BitToUL(lSettings, 0, uint8_t(getFormItemInt(F("P201_headerAlternate")) & 0xff));   // Bit 7-0 HeaderContentAlternative
        P201_setBitToUL(lSettings, 16, isFormItemChecked(F("P201_pin3invers")));                // Bit 16 Pin3Invers
        P201_setBitToUL(lSettings, 17, isFormItemChecked(F("P201_ScrollLines")));               // Bit 17 ScrollLines
        // save CustomTaskSettings always in version V1
        set4BitToUL(lSettings, 20, 0x01);                                                       // Bit23-20 Version CustomTaskSettings -> version V1

        PCONFIG_LONG(0) = lSettings;

        String error;
        for (uint8_t varNr = 0; varNr < P201_Nlines; varNr++) {
          if (!safe_strncpy(P201_DisplayLinesV1[varNr].Content, web_server.arg(getPluginCustomArgName(varNr)), P201_NcharsV1)) {
            error += getCustomTaskSettingsError(varNr);
          }
          P201_DisplayLinesV1[varNr].Content[P201_NcharsV1-1] = 0; // Terminate in case of uninitalized data
          P201_DisplayLinesV1[varNr].FontType = 0xff;
          P201_DisplayLinesV1[varNr].FontHeight = 0xff;
          P201_DisplayLinesV1[varNr].FontSpace = 0xff;
          P201_DisplayLinesV1[varNr].reserved = 0xff;
        }
        if (error.length() > 0) {
          addHtmlError(error);
        }
        SaveCustomTaskSettings(event->TaskIndex, (uint8_t*)&P201_DisplayLinesV1, sizeof(P201_DisplayLinesV1));
        // After saving, make sure the active lines are updated.
        Plugin_201_loadDisplayLines(event->TaskIndex, 1);
        success = true;
    }  break;
    case PLUGIN_GET_DEVICEGPIONAMES:  {
        event->String1 = formatGpioName_output(F("CS"));
        event->String2 = formatGpioName_output(F("DC"));
    }  break;
    case PLUGIN_INIT: {
        lastWiFiState = P201_WIFI_STATE_UNSET;
        // Load the custom settings from flash
        uint8_t version = get4BitFromUL(PCONFIG_LONG(0), 20);    // Bit23-20 Version CustomTaskSettings
        Plugin_201_loadDisplayLines(event->TaskIndex, version);

        //      Init the display and turn it on
        if (display)  {
          display->end();
          delete display;
        }

        if (CONFIG_PIN1 != 0) {
          // Konvert the GPIO Pin to a Dogotal Puin Number first ...
          Plugin_201_SPI_CS_Pin = CONFIG_PIN1;
        }

        if (CONFIG_PIN2 != 0) {
          // Konvert the GPIO Pin to a Dogotal Puin Number first ...
          Plugin_201_SPI_DC_Pin = CONFIG_PIN2;
        }

        // set the slaveSelectPin as an output:
        SPI.setHwCs(false);
        SPI.begin();
        //addLog(LOG_LEVEL_INFO, F(logMes("")));

        display = new SSD1306Spi(4,Plugin_201_SPI_DC_Pin,Plugin_201_SPI_CS_Pin);
        
        display->init();		// call to local override of init function
        display->displayOn();

        uint8_t OLED_contrast = PCONFIG(PCONFIG_201_contrast);
        P201_setContrast(OLED_contrast);

        //      Set the initial value of OnOff to On
        UserVar[event->BaseVarIndex] = 1;

        //      flip screen if required
        OLEDIndex = PCONFIG(PCONFIG_201_size);
        if (PCONFIG(PCONFIG_201_rotate) == 2) {
          display->flipScreenVertically();
          TopLineOffset = P201_MaxDisplayHeight - SizeSettings[OLEDIndex].Height;
        }
        else TopLineOffset = 0;

        //      Display the device name, logo, time and wifi
        bAlternativHeader = false;  // start with first header content
        HeaderCount = 0;            // reset header count
        display_header();
        display_logo();
        display->display();

        //      Set up the display timer
        displayTimer = PCONFIG(PCONFIG_201_timer);

        if (CONFIG_PIN3 != -1)
        {
          pinMode(CONFIG_PIN3, INPUT_PULLUP);
        }

        //    Initialize frame counter
        frameCounter = 0;
        nrFramesToDisplay = 1;
        currentFrameToDisplay = 0;
        bPageScrollDisabled = true;  // first page after INIT without scrolling
        ScrollingPages.linesPerFrame = PCONFIG(PCONFIG_201_nlines);

        //    Clear scrolling line data
        for (uint8_t i=0; i<P201_MAX_LinesPerPage ; i++) {
          ScrollingLines.Line[i].Width = 0;
          ScrollingLines.Line[i].LastWidth = 0;
        }

        //    prepare font and positions for page and line scrolling
        prepare_pagescrolling();

        success = true;
    }  break;
    case PLUGIN_EXIT: {
          if (display)
          {
            display->end();
            delete display;
            display=NULL;
          }
          for (uint8_t varNr = 0; varNr < P201_Nlines; varNr++) {
            P201_DisplayLinesV1[varNr].Content[0] = 0;
          }
    }  break;
    case PLUGIN_TEN_PER_SECOND:   {  // Check frequently to see if we have a pin signal to switch on display
        int lTaskTimer = Settings.TaskDeviceTimer[event->TaskIndex];
        bAlternativHeader = (++HeaderCount > (lTaskTimer*5)); // change header after half of display time
        if (CONFIG_PIN3 != -1)
        {
          bPin3Invers = getBitFromUL(PCONFIG_LONG(0), 16);  // Bit 16
          if ((!bPin3Invers && digitalRead(CONFIG_PIN3)) || (bPin3Invers && !digitalRead(CONFIG_PIN3)))
          {
            display->displayOn();
            UserVar[event->BaseVarIndex] = 1;      //  Save the fact that the display is now ON
            displayTimer = PCONFIG(PCONFIG_201_timer);
          }
        }
        bScrollLines = getBitFromUL(PCONFIG_LONG(0), 17);  // Bit 17
        if ((UserVar[event->BaseVarIndex] == 1) && bScrollLines && (ScrollingPages.Scrolling == 0)) {
          // Display is on.
          OLEDIndex = PCONFIG(PCONFIG_201_size);
          display_scrolling_lines(ScrollingPages.linesPerFrame); // line scrolling
        }
        success = true;
    }  break;
    case PLUGIN_ONCE_A_SECOND:  {  // Switch off display after displayTimer seconds
        if ( displayTimer > 0)  {
          displayTimer--;
          if (displayTimer == 0) {
            display->displayOff();
            UserVar[event->BaseVarIndex] = 0;      //  Save the fact that the display is now OFF
          }
        }
        if (UserVar[event->BaseVarIndex] == 1) {  // Display is on.
          OLEDIndex = PCONFIG(PCONFIG_201_size);
          HeaderContent = static_cast<eHeaderContent>(get8BitFromUL(PCONFIG_LONG(0), 8));             // Bit15-8 HeaderContent
          HeaderContentAlternative = static_cast<eHeaderContent>(get8BitFromUL(PCONFIG_LONG(0), 0));  // Bit 7-0 HeaderContentAlternative
	        display_header();	// Update Header
          if (display && display_wifibars()) {
            // WiFi symbol was updated.
            display->display();
          }
        }
        success = true;
    }  break;
    case PLUGIN_TIMER_IN: {
      OLEDIndex = PCONFIG(PCONFIG_201_size);
      if (display_scroll_timer()) // page scrolling
        setPluginTaskTimer(P201_PageScrollTimer, event->TaskIndex, event->Par1);  // calls next page scrollng tick
      // return success;
    }  break;
    case PLUGIN_READ:  {
        //      Define Scroll area layout
        if (UserVar[event->BaseVarIndex] == 1) {
          // Display is on.
          ScrollingPages.Scrolling = 1; // page scrolling running -> no line scrolling allowed
          NFrames = P201_Nlines / ScrollingPages.linesPerFrame;
          OLEDIndex = PCONFIG(PCONFIG_201_size);
          HeaderContent = static_cast<eHeaderContent>(get8BitFromUL(PCONFIG_LONG(0), 8));             // Bit15-8 HeaderContent
          HeaderContentAlternative = static_cast<eHeaderContent>(get8BitFromUL(PCONFIG_LONG(0), 0));  // Bit 7-0 HeaderContentAlternative

          //      Now create the string for the outgoing and incoming frames
          String tmpString;
          tmpString.reserve(P201_NcharsV1);

          //      Construct the outgoing string
          for (uint8_t i = 0; i < ScrollingPages.linesPerFrame; i++)
          {
            tmpString = String(P201_DisplayLinesV1[(ScrollingPages.linesPerFrame * frameCounter) + i].Content);
            ScrollingPages.oldString[i] = P201_parseTemplate(tmpString, 20);
          }

          // now loop round looking for the next frame with some content
          //   skip this frame if all lines in frame are blank
          // - we exit the while loop if any line is not empty
          boolean foundText = false;
          int ntries = 0;
          while (!foundText) {

            //        Stop after framecount loops if no data found
            ntries += 1;
            if (ntries > NFrames) break;

            if (nextFrameToDisplay == 0xff) {
              // Increment the frame counter
              frameCounter = frameCounter + 1;
              if ( frameCounter > NFrames - 1) {
                frameCounter = 0;
                currentFrameToDisplay = 0;
              }
            }
            else {
              // next frame because content changed in PLUGIN_WRITE
              frameCounter = nextFrameToDisplay;
            }
            //        Contruct incoming strings
            for (uint8_t i = 0; i < ScrollingPages.linesPerFrame; i++)  {
              tmpString = String(P201_DisplayLinesV1[(ScrollingPages.linesPerFrame * frameCounter) + i].Content);
              ScrollingPages.newString[i] = P201_parseTemplate(tmpString, 20);
              if (ScrollingPages.newString[i].length() > 0) foundText = true;
            }
            if (foundText) {
              if (nextFrameToDisplay == 0xff) {
                if (frameCounter != 0) {
                  ++currentFrameToDisplay;
                }
              }
              else currentFrameToDisplay = nextFrameToDisplay;
            }
          }
          nextFrameToDisplay = 0xFF;
          if ((currentFrameToDisplay + 1) > nrFramesToDisplay) {
            nrFramesToDisplay = currentFrameToDisplay + 1;
          }
          // Update max page count
          if (MaxFramesToDisplay == 0xFF) {
            // not updated yet
            for (uint8_t i = 0; i < NFrames; i++) {
              for (uint8_t k = 0; k < ScrollingPages.linesPerFrame; k++)  {
                tmpString = String(P201_DisplayLinesV1[(ScrollingPages.linesPerFrame * i) + k].Content);
                tmpString = P201_parseTemplate(tmpString, 20);
                if (tmpString.length() > 0) {
                  // page not empty
                  MaxFramesToDisplay ++;
                  break;
                }
              }
            }
          }
          //      Update display
          bAlternativHeader = false;  // start with first header content
          HeaderCount = 0;            // reset header count
          display_header();
          if (SizeSettings[OLEDIndex].Width == P201_MaxDisplayWidth) display_indicator(currentFrameToDisplay, nrFramesToDisplay);
          display->display();
          int lscrollspeed = PCONFIG(PCONFIG_201_scroll);
          if (bPageScrollDisabled) lscrollspeed = ePSS_Instant; // first page after INIT without scrolling
          int lTaskTimer = Settings.TaskDeviceTimer[event->TaskIndex];
          if (display_scroll(lscrollspeed, lTaskTimer))
            setPluginTaskTimer(P201_PageScrollTimer, event->TaskIndex, event->Par1); // calls next page scrollng tick
          bPageScrollDisabled = false;    // next PLUGIN_READ will do page scrolling
				}
        success = true;
    }  break;
    case PLUGIN_WRITE:  {
        String command = parseString(string, 1);
        String subcommand = parseString(string, 2);
        int LineNo = event->Par1;

        if ((command == F("oledframedcmd")) && display) {
          if (subcommand == F("display"))  {
            // display functions
            String para1 = parseString(string, 3);
            if (para1 == F("on")) {
              success = true;
              displayTimer = PCONFIG(PCONFIG_201_timer);
              display->displayOn();
              UserVar[event->BaseVarIndex] = 1;      //  Save the fact that the display is now ON
            }
            if (para1 == F("off")) {
              success = true;
              displayTimer = 0;
              display->displayOff();
              UserVar[event->BaseVarIndex] = 0;      //  Save the fact that the display is now OFF
            }
            if (para1 == F("low")) {
              success = true;
              P201_setContrast(P201_CONTRAST_LOW);
            }
            if (para1 == F("med")) {
              success = true;
              P201_setContrast(P201_CONTRAST_MED);
            }
            if (para1 == F("high")) {
              success = true;
              P201_setContrast(P201_CONTRAST_HIGH);
            }
            // log += String(F("[P36] Display: ")) + String(para1) + String(F(" Success:")) + String(success);
            // addLog(LOG_LEVEL_INFO, log);
        }
        else if ((LineNo > 0) && (LineNo <= P201_Nlines))  {
            // content functions
            success = true;
            String NewContent = parseStringKeepCase(string, 3);
            NewContent = P201_parseTemplate(NewContent, 20);
            if (!safe_strncpy(P201_DisplayLinesV1[LineNo-1].Content, NewContent, P201_NcharsV1)) {
              addHtmlError(getCustomTaskSettingsError(LineNo-1));
            }
            P201_DisplayLinesV1[LineNo-1].Content[P201_NcharsV1-1] = 0;      // Terminate in case of uninitalized data
            P201_DisplayLinesV1[LineNo-1].reserved = (event->Par3 & 0xFF);  // not implemented yet

            // calculate Pix length of new Content
            display->setFont(ScrollingPages.Font);
            uint16_t PixLength = display->getStringWidth(String(P201_DisplayLinesV1[LineNo-1].Content));
            if (PixLength > 255) {
              addHtmlError(String(F("Pixel length of ")) + String(PixLength) + String(F(" too long for line! Max. 255 pix!")));

              int strlen = String(P201_DisplayLinesV1[LineNo-1].Content).length();
              float fAvgPixPerChar = ((float) PixLength)/strlen;
              int iCharToRemove = ceil(((float) (PixLength-255))/fAvgPixPerChar);
              // shorten string because OLED controller can not handle such long strings
              P201_DisplayLinesV1[LineNo-1].Content[strlen-iCharToRemove] = 0;
            }

            nextFrameToDisplay = LineNo / ScrollingPages.linesPerFrame; // next frame shows the new content
            displayTimer = PCONFIG(PCONFIG_201_timer);
            if (UserVar[event->BaseVarIndex] == 0) {
              // display was OFF, turn it ON
              display->displayOn();
              UserVar[event->BaseVarIndex] = 1;      //  Save the fact that the display is now ON
            }

            // String log = String(F("[P36] Line: ")) + String(LineNo);
            // log += String(F(" NewContent:")) + String(NewContent);
            // log += String(F(" Content:")) + String(P201_DisplayLinesV1[LineNo-1].Content);
            // log += String(F(" Length:")) + String(String(P201_DisplayLinesV1[LineNo-1].Content).length());
            // log += String(F(" Pix: ")) + String(display->getStringWidth(String(P201_DisplayLinesV1[LineNo-1].Content)));
            // log += String(F(" Reserved:")) + String(P201_DisplayLinesV1[LineNo-1].reserved);
            // addLog(LOG_LEVEL_INFO, log);
          }
      }
      // if (!success) {
      //   log += String(F("[P36] Cmd: ")) + String(command);
      //   log += String(F(" SubCmd:")) + String(subcommand);
      //   log += String(F(" Success:")) + String(success);
      //   addLog(LOG_LEVEL_INFO, log);
      // }
    }  break;
  }
  return success;
}

void P201_setContrast(uint8_t OLED_contrast) { // Set the display contrast really low brightness & contrast: contrast = 10, precharge = 5, comdetect = 0 normal brightness & contrast:  contrast = 100
  char contrast = 100;
  char precharge = 241;
  char comdetect = 64;
  switch (OLED_contrast) {
    case P201_CONTRAST_OFF:
      if (display) {
        display->displayOff();
      }
      return;
    case P201_CONTRAST_LOW:
      contrast = 10; precharge = 5; comdetect = 0;
      break;
    case P201_CONTRAST_MED:
      contrast = P201_CONTRAST_MED; precharge = 0x1F; comdetect = 64;
      break;
    case P201_CONTRAST_HIGH:
    default:
      contrast = P201_CONTRAST_HIGH; precharge = 241; comdetect = 64;
      break;
  }
  if (display) {
    display->displayOn();
    display->setContrast(contrast, precharge, comdetect);
  }
}

String P201_parseTemplate(String &tmpString, uint8_t lineSize) { // Perform some specific changes for OLED display
  String result = parseTemplate_padded(tmpString, lineSize);
  // OLED lib uses this routine to convert UTF8 to extended ASCII
  // http://playground.arduino.cc/Main/Utf8ascii
  // Attempt to display euro sign (FIXME)
  /*
  const char euro[4] = {0xe2, 0x82, 0xac, 0}; // Unicode euro symbol
  const char euro_oled[3] = {0xc2, 0x80, 0}; // Euro symbol OLED display font
  result.replace(euro, euro_oled);
  */
  /*
  if (tmpString.indexOf('{') != -1) {
    String log = F("Gijs: '");
    log += tmpString;
    log += F("'  hex:");
    for (int i = 0; i < tmpString.length(); ++i) {
      log += ' ';
      log += String(tmpString[i], HEX);
    }
    log += F(" out hex:");
    for (int i = 0; i < result.length(); ++i) {
      log += ' ';
      log += String(result[i], HEX);
    }
    addLog(LOG_LEVEL_INFO, log);
  }
  */
  result.trim();
  return result;
}

void display_time() {
  String dtime = F("%systime%");
  parseSystemVariables(dtime, false);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);
  display->setColor(BLACK);
  display->fillRect(0, TopLineOffset, 28, 10);
  display->setColor(WHITE);
  display->drawString(0, TopLineOffset, dtime.substring(0, 5));
  //display->drawString(0, TopLineOffset, dtime);
}

void display_title(String& title) {
  display->setFont(ArialMT_Plain_10);
  display->setColor(BLACK);
  display->fillRect(0, TopLineOffset, P201_MaxDisplayWidth, 12);   // don't clear line under title.
  display->setColor(WHITE);
  if (SizeSettings[OLEDIndex].Width == P201_MaxDisplayWidth) {
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(P201_DisplayCentre, TopLineOffset, title);
  }
  else {
    display->setTextAlignment(TEXT_ALIGN_LEFT);  // Display right of WiFi bars
    display->drawString(SizeSettings[OLEDIndex].PixLeft + SizeSettings[OLEDIndex].WiFiIndicatorWidth + 3, TopLineOffset, title);
  }
}

void display_header() { // The screen is set up as 10 rows at the top for the header, 10 rows at the bottom for the footer and 44 rows in the middle for the scroll region
  eHeaderContent _HeaderContent;
  String newString, strHeader;
  if ((HeaderContentAlternative==HeaderContent) || !bAlternativHeader) {
    _HeaderContent=HeaderContent;
  }  else   {
    _HeaderContent=HeaderContentAlternative;
  }
  switch (_HeaderContent) {
    case eSSID:
      if (WiFiConnected()) {
        strHeader = WiFi.SSID();
      } else {
        newString=F("%sysname%");
      }
    break;
    case eSysName:
      newString=F("%sysname%");
    break;
    case eTime:
      newString=F("%systime%");
    break;
    case eDate:
      newString = F("%sysday_0%.%sysmonth_0%.%sysyear%");
    break;
    case eIP:
      newString=F("%ip%");
    break;
    case eMAC:
      newString=F("%mac%");
    break;
    case eRSSI:
      newString=F("%rssi%dB");
    break;
    case eBSSID:
      newString=F("%bssid%");
    break;
    case eWiFiCh:
      newString=F("Channel: %wi_ch%");
    break;
    case eUnit:
      newString=F("Unit: %unit%");
    break;
    case eSysLoad:
      newString=F("Load: %sysload%%");
    break;
    case eSysHeap:
      newString=F("Mem: %sysheap%");
    break;
    case eSysStack:
      newString=F("Stack: %sysstack%");
    break;
    case ePageNo:
      strHeader = F("page ");
      strHeader += (currentFrameToDisplay+1);
      if (MaxFramesToDisplay != 0xFF) {
        strHeader += F("/");
        strHeader += (MaxFramesToDisplay+1);
      }
    break;
    default:
      return;
  }
  if (newString.length() > 0) {
    // Right now only systemvariables have been used, so we don't have to call the parseTemplate.
    parseSystemVariables(newString, false);
    strHeader = newString;
  }

  strHeader.trim();
  display_title(strHeader);
  // Display time and wifibars both clear area below, so paint them after the title.
  if (SizeSettings[OLEDIndex].Width == P201_MaxDisplayWidth) display_time(); // only for 128pix wide displays
  display_wifibars();
}

void display_logo() {
int left = 24;
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_16);
  display->setColor(BLACK);
  display->fillRect(0, 13+TopLineOffset, P201_MaxDisplayWidth, P201_MaxDisplayHeight);
  display->setColor(WHITE);
  display->drawString(65, 15+TopLineOffset, F("ESP"));
  display->drawString(65, 34+TopLineOffset, F("Easy"));
  if (SizeSettings[OLEDIndex].PixLeft<left) left = SizeSettings[OLEDIndex].PixLeft;
  display->drawXbm(left, 13+TopLineOffset, espeasy_logo_width, espeasy_logo_height, espeasy_logo_bits); // espeasy_logo_width=espeasy_logo_height=36
}

void display_indicator(int iframe, int frameCount) { // Draw the frame position
  //  Erase Indicator Area
  display->setColor(BLACK);
  display->fillRect(0, 54+TopLineOffset, P201_MaxDisplayWidth, 10);
  display->setColor(WHITE);

  // Only display when there is something to display.
  if (frameCount <= 1) return;

  // Display chars as required
  for (uint8_t i = 0; i < frameCount; i++) {
    const char *image;
    if (iframe == i) {
      image = activeSymbole;
    } else {
      image = inactiveSymbole;
    }

    int x, y;
    y = 56+TopLineOffset;
    // I would like a margin of 20 pixels on each side of the indicator.
    // Therefore the width of the indicator should be 128-40=88 and so space between indicator dots is 88/(framecount-1)
    // The width of the display is 128 and so the first dot must be at x=20 if it is to be centred at 64
    const int number_spaces = frameCount - 1;
    if (number_spaces <= 0)
      return;
    int margin = 20;
    int spacing = (P201_MaxDisplayWidth - 2 * margin) / number_spaces;
    // Now apply some max of 30 pixels between the indicators and center the row of indicators.
    if (spacing > 30) {
      spacing = 30;
      margin = (P201_MaxDisplayWidth - number_spaces * spacing) / 2;
    }

    x = margin + (spacing * i);
    display->drawXbm(x, y, 8, 8, image);
  }
}

void prepare_pagescrolling() {
  switch (ScrollingPages.linesPerFrame) {
  case 1:
    ScrollingPages.Font = SizeSettings[OLEDIndex].L1.fontData;
    ScrollingPages.ypos[0] = SizeSettings[OLEDIndex].L1.Top+TopLineOffset;
    ScrollingLines.Space = SizeSettings[OLEDIndex].L1.Space+1;
  break;
  case 2:
    ScrollingPages.Font = SizeSettings[OLEDIndex].L2.fontData;
    ScrollingPages.ypos[0] = SizeSettings[OLEDIndex].L2.Top+TopLineOffset;
    ScrollingPages.ypos[1] = ScrollingPages.ypos[0]+SizeSettings[OLEDIndex].L2.Space;
    ScrollingLines.Space = SizeSettings[OLEDIndex].L2.Space+1;
  break;
  case 3:
    ScrollingPages.Font = SizeSettings[OLEDIndex].L3.fontData;
    ScrollingPages.ypos[0] = SizeSettings[OLEDIndex].L3.Top+TopLineOffset;
    ScrollingPages.ypos[1] = ScrollingPages.ypos[0]+SizeSettings[OLEDIndex].L3.Space;
    ScrollingPages.ypos[2] = ScrollingPages.ypos[1]+SizeSettings[OLEDIndex].L3.Space;
    ScrollingLines.Space = SizeSettings[OLEDIndex].L3.Space+1;
  break;
  default:
    ScrollingPages.linesPerFrame = 4;
    ScrollingPages.Font = SizeSettings[OLEDIndex].L4.fontData;
    ScrollingPages.ypos[0] = SizeSettings[OLEDIndex].L4.Top+TopLineOffset;
    ScrollingPages.ypos[1] = ScrollingPages.ypos[0]+SizeSettings[OLEDIndex].L4.Space;
    ScrollingPages.ypos[2] = ScrollingPages.ypos[1]+SizeSettings[OLEDIndex].L4.Space;
    ScrollingPages.ypos[3] = ScrollingPages.ypos[2]+SizeSettings[OLEDIndex].L4.Space;
    ScrollingLines.Space = SizeSettings[OLEDIndex].L4.Space+1;
  }
  ScrollingLines.Font = ScrollingPages.Font;
  for (uint8_t i=0; i<P201_MAX_LinesPerPage ; i++) {
    ScrollingLines.Line[i].ypos = ScrollingPages.ypos[i];
  }
}

uint8_t display_scroll(int lscrollspeed, int lTaskTimer) {
  // outString contains the outgoing strings in this frame
  // inString contains the incomng strings in this frame
  // nlines is the number of lines in each frame
  int iPageScrollTime;
  display->setFont(ScrollingPages.Font);
  // String log = F("Start Scrolling: Speed: ");
  // log += lscrollspeed;
  ScrollingLines.wait = 0;
  // calculate total page scrolling time
  if (lscrollspeed == ePSS_Instant) iPageScrollTime = P201_PageScrollTick-P201_PageScrollTimer; // no scrolling, just the handling time to build the new page
  else iPageScrollTime = (P201_MaxDisplayWidth /(P201_PageScrollPix * lscrollspeed)) * P201_PageScrollTick;
  float fScrollTime = (float)(lTaskTimer*1000 - iPageScrollTime - 2*P201_WaitScrollLines*100)/100.0;

  // log += F(" PageScrollTime: ");
  // log += iPageScrollTime;

    uint16_t MaxPixWidthForPageScrolling = P201_MaxDisplayWidth;
    if (bScrollLines) {
      // Reduced scrolling width because line is displayed left or right aligned
      MaxPixWidthForPageScrolling -= SizeSettings[OLEDIndex].PixLeft;
    }

    for (uint8_t j = 0; j < ScrollingPages.linesPerFrame; j++)   {
      // default no line scrolling and strings are centered
      ScrollingLines.Line[j].LastWidth = 0;
      ScrollingLines.Line[j].Width = 0;

      // get last and new line width
      uint16_t LastPixLength = display->getStringWidth(ScrollingPages.oldString[j]);
      uint16_t NewPixLength = display->getStringWidth(ScrollingPages.newString[j]);

      if (bScrollLines) {
        // settings for following line scrolling
        if (LastPixLength > SizeSettings[OLEDIndex].Width)
          ScrollingLines.Line[j].LastWidth = LastPixLength;   // while page scrolling this line is right aligned

        if ((NewPixLength > SizeSettings[OLEDIndex].Width) && (fScrollTime > 0.0))   {
          // width of the line > display width -> scroll line
          ScrollingLines.Line[j].Content = ScrollingPages.newString[j];
          ScrollingLines.Line[j].Width = NewPixLength;   // while page scrolling this line is left aligned
          ScrollingLines.Line[j].CurrentLeft = SizeSettings[OLEDIndex].PixLeft;
          ScrollingLines.Line[j].fPixSum = (float) SizeSettings[OLEDIndex].PixLeft;
          ScrollingLines.Line[j].dPix = ((float)(NewPixLength-SizeSettings[OLEDIndex].Width))/fScrollTime; // pix change per scrolling line tick

          // log += F(" line: ");
          // log += j+1;
          // log += F(" width: ");
          // log += ScrollingLines.Line[j].Width;
          // log += F(" dPix: ");
          // log += ScrollingLines.Line[j].dPix;
        }
      }

      if (NewPixLength > 255) {
        // shorten string because OLED controller can not handle such long strings
        int strlen = ScrollingPages.newString[j].length();
        float fAvgPixPerChar = ((float) NewPixLength)/strlen;
        int iCharToRemove = ceil(((float) (NewPixLength-255))/fAvgPixPerChar);
        ScrollingLines.Line[j].Content = ScrollingLines.Line[j].Content.substring(0, strlen-iCharToRemove);
      }
      // reduce line content for page scrolling to max width
      if (NewPixLength > MaxPixWidthForPageScrolling) {
        int strlen = ScrollingPages.newString[j].length();
        float fAvgPixPerChar = ((float) NewPixLength)/strlen;
        int iCharToRemove = ceil(((float) (NewPixLength-MaxPixWidthForPageScrolling))/(2*fAvgPixPerChar));
        if (bScrollLines) {
          // shorten string on right side because line is displayed left aligned while scrolling
          ScrollingPages.newString[j] = ScrollingPages.newString[j].substring(0, strlen-(2*iCharToRemove));
        }  else {
          // shorten string on both sides because line is displayed centered
          ScrollingPages.newString[j] = ScrollingPages.newString[j].substring(0, strlen-iCharToRemove);
          ScrollingPages.newString[j] = ScrollingPages.newString[j].substring(iCharToRemove);
        }
        // String log = String(F("Line: ")) + String(j+1);
        // log += String(F(" New: ")) + String(ScrollingPages.newString[j]);
        // log += String(F(" Reduced: ")) + String(NewPixLength);
        // log += String(F(" (")) + String(strlen) + String(F(") -> "));
        // log += String(display->getStringWidth(ScrollingPages.newString[j])) + String(F(" (")) + String(ScrollingPages.newString[j].length()) + String(F(")"));
        // addLog(LOG_LEVEL_INFO, log);
      }
      if (LastPixLength > MaxPixWidthForPageScrolling) {
        int strlen = ScrollingPages.oldString[j].length();
        float fAvgPixPerChar = ((float) LastPixLength)/strlen;
        int iCharToRemove = round(((float) (LastPixLength-MaxPixWidthForPageScrolling))/(2*fAvgPixPerChar));
        if (bScrollLines) {
          // shorten string on left side because line is displayed right aligned while scrolling
          ScrollingPages.oldString[j] = ScrollingPages.oldString[j].substring(2*iCharToRemove);
        }  else {
          // shorten string on both sides because line is displayed centered
          ScrollingPages.oldString[j] = ScrollingPages.oldString[j].substring(0, strlen-iCharToRemove);
          ScrollingPages.oldString[j] = ScrollingPages.oldString[j].substring(iCharToRemove);
        }
        // String log = String(F("Line: ")) + String(j+1);
        // log += String(F(" Old: ")) + String(ScrollingPages.oldString[j]);
        // log += String(F(" Reduced: ")) + String(LastPixLength);
        // log += String(F(" (")) + String(strlen) + String(F(") -> "));
        // log += String(display->getStringWidth(ScrollingPages.oldString[j])) + String(F(" (")) + String(ScrollingPages.oldString[j].length()) + String(F(")"));
        // addLog(LOG_LEVEL_INFO, log);
      }

      // while (NewPixLength > MaxPixWidthForPageScrolling) {
      //   // shorten string on right side because line is displayed left aligned while scrolling
      //   ScrollingPages.newString[j] = ScrollingPages.newString[j].substring(0, ScrollingPages.newString[j].length()-1);
      //   if (bScrollLines == false) {
      //     // shorten string on both sides because line is displayed centered
      //     ScrollingPages.newString[j] = ScrollingPages.newString[j].substring(1);
      //   }
      //   NewPixLength = display->getStringWidth(ScrollingPages.newString[j]);
      // }
      // while (LastPixLength > MaxPixWidthForPageScrolling) {
      //   // shorten string on left side because line is displayed right aligned while scrolling
      //   ScrollingPages.oldString[j] = ScrollingPages.oldString[j].substring(1);
      //   if (bScrollLines == false) {
      //     // shorten string on both sides because line is displayed centered
      //     ScrollingPages.oldString[j] = ScrollingPages.oldString[j].substring(0, ScrollingPages.oldString[j].length()-1);
      //   }
      //   LastPixLength = display->getStringWidth(ScrollingPages.oldString[j]);
      // }

     // addLog(LOG_LEVEL_INFO, log);
  }

  // log = F("Start Scrolling...");
  // addLog(LOG_LEVEL_INFO, log);

  ScrollingPages.dPix = P201_PageScrollPix * lscrollspeed; // pix change per scrolling page tick
  ScrollingPages.dPixSum = ScrollingPages.dPix;

  display->setColor(BLACK);
   // We allow 12 pixels at the top because otherwise the wifi indicator gets too squashed!!
  display->fillRect(0, 12+TopLineOffset, P201_MaxDisplayWidth, 42);   // scrolling window is 44 pixels high - ie 64 less margin of 10 at top and bottom
  display->setColor(WHITE);
  display->drawLine(0, 12+TopLineOffset, P201_MaxDisplayWidth, 12+TopLineOffset);   // line below title

  for (uint8_t j = 0; j < ScrollingPages.linesPerFrame; j++)  {
    if (lscrollspeed < ePSS_Instant ) { // scrolling
      if (ScrollingLines.Line[j].LastWidth > 0 ) {
        // width of oldString[j] > display width -> line at beginning of scrolling page is right aligned
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(P201_MaxDisplayWidth - SizeSettings[OLEDIndex].PixLeft + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.oldString[j]);
      } else {
        // line at beginning of scrolling page is centered
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(P201_DisplayCentre + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.oldString[j]);
      }
    }

    if (ScrollingLines.Line[j].Width > 0 ) {
      // width of newString[j] > display width -> line at end of scrolling page should be left aligned
      display->setTextAlignment(TEXT_ALIGN_LEFT);
      display->drawString(-P201_MaxDisplayWidth + SizeSettings[OLEDIndex].PixLeft + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.newString[j]);
    } else {
      // line at end of scrolling page should be centered
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->drawString(-P201_DisplayCentre + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.newString[j]);
    }
  }

  display->display();

  if (lscrollspeed < ePSS_Instant ) {
    // page scrolling (using PLUGIN_TIMER_IN)
    ScrollingPages.dPixSum += ScrollingPages.dPix;
  }
  else {
    // no page scrolling
    ScrollingPages.Scrolling = 0; // allow following line scrolling
  }
  // log = F("Scrolling finished");
  // addLog(LOG_LEVEL_INFO, log);
  return (ScrollingPages.Scrolling);
}

uint8_t display_scroll_timer() {
  // page scrolling (using PLUGIN_TIMER_IN)
  display->setColor(BLACK);
   // We allow 13 pixels (including underline) at the top because otherwise the wifi indicator gets too squashed!!
  display->fillRect(0, 13+TopLineOffset, P201_MaxDisplayWidth, 42);   // scrolling window is 44 pixels high - ie 64 less margin of 10 at top and bottom
  display->setColor(WHITE);
  display->setFont(ScrollingPages.Font);

  for (uint8_t j = 0; j < ScrollingPages.linesPerFrame; j++)   {
    if (ScrollingLines.Line[j].LastWidth > 0 ) {
      // width of oldString[j] > display width -> line is right aligned while scrolling page
      display->setTextAlignment(TEXT_ALIGN_RIGHT);
      display->drawString(P201_MaxDisplayWidth - SizeSettings[OLEDIndex].PixLeft + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.oldString[j]);
    }
    else {
      // line is centered while scrolling page
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->drawString(P201_DisplayCentre + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.oldString[j]);
    }
    if (ScrollingLines.Line[j].Width > 0 ) {
      // width of newString[j] > display width -> line is left aligned while scrolling page
      display->setTextAlignment(TEXT_ALIGN_LEFT);
      display->drawString(-P201_MaxDisplayWidth + SizeSettings[OLEDIndex].PixLeft + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.newString[j]);
    }
    else {
      // line is centered while scrolling page
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->drawString(-P201_DisplayCentre + ScrollingPages.dPixSum, ScrollingPages.ypos[j], ScrollingPages.newString[j]);
    }
  }

  display->display();

  if (ScrollingPages.dPixSum < P201_MaxDisplayWidth ) { // scrolling
    // page still scrolling
    ScrollingPages.dPixSum += ScrollingPages.dPix;
  }  else {
    // page scrolling finished
    ScrollingPages.Scrolling = 0; // allow following line scrolling
    // String log = F("Scrolling finished");
    // addLog(LOG_LEVEL_INFO, log);
  }
  return (ScrollingPages.Scrolling);
}

//Draw scrolling line (1pix/s)
void display_scrolling_lines(int nlines) {
  // line scrolling (using PLUGIN_TEN_PER_SECOND)
  int i;
  boolean bscroll = false;
  boolean updateDisplay = false;
  int iCurrentLeft;

  for (i=0; i<nlines; i++) {
    if (ScrollingLines.Line[i].Width !=0 ) {
      display->setFont(ScrollingLines.Font);
      bscroll = true;
      break;
    }
  }
  if (bscroll) {
    ScrollingLines.wait++;
    if (ScrollingLines.wait < P201_WaitScrollLines)
      return; // wait before scrolling line not finished

    for (i=0; i<nlines; i++) {
      if (ScrollingLines.Line[i].Width !=0 ) {
        // scroll this line
        ScrollingLines.Line[i].fPixSum -= ScrollingLines.Line[i].dPix;
        iCurrentLeft = round(ScrollingLines.Line[i].fPixSum);
        if (iCurrentLeft != ScrollingLines.Line[i].CurrentLeft) {
          // still scrolling
          ScrollingLines.Line[i].CurrentLeft = iCurrentLeft;
          updateDisplay = true;
          display->setColor(BLACK);
          display->fillRect(0 , ScrollingLines.Line[i].ypos, P201_MaxDisplayWidth, ScrollingLines.Space);
          display->setColor(WHITE);
          if (((ScrollingLines.Line[i].CurrentLeft-SizeSettings[OLEDIndex].PixLeft)+ScrollingLines.Line[i].Width) >= SizeSettings[OLEDIndex].Width) {
            display->setTextAlignment(TEXT_ALIGN_LEFT);
            display->drawString(ScrollingLines.Line[i].CurrentLeft, ScrollingLines.Line[i].ypos, ScrollingLines.Line[i].Content);
          }
          else {
            // line scrolling finished -> line is shown as aligned right
            display->setTextAlignment(TEXT_ALIGN_RIGHT);
            display->drawString(P201_MaxDisplayWidth - SizeSettings[OLEDIndex].PixLeft, ScrollingPages.ypos[i], ScrollingLines.Line[i].Content);
            ScrollingLines.Line[i].Width = 0; // Stop scrolling
          }
        }
      }
    }
    if (updateDisplay && (ScrollingPages.Scrolling == 0)) display->display();
  }
}

//Draw Signal Strength Bars, return true when there was an update.
bool display_wifibars() {
  const bool connected = WiFiConnected();
  const int nbars_filled = (WiFi.RSSI() + 100) / 12;  // all bars filled if RSSI better than -46dB
  const int newState = connected ? nbars_filled : P201_WIFI_STATE_NOT_CONNECTED;
  if (newState == lastWiFiState)
    return false; // nothing to do.

  int x = SizeSettings[OLEDIndex].WiFiIndicatorLeft;
  int y = TopLineOffset;
  int size_x = SizeSettings[OLEDIndex].WiFiIndicatorWidth;
  int size_y = 10;
  int nbars = 5;
  int16_t width = (size_x / nbars);
  size_x = width * nbars - 1; // Correct for round errors.

  //  x,y are the x,y locations
  //  sizex,sizey are the sizes (should be a multiple of the number of bars)
  //  nbars is the number of bars and nbars_filled is the number of filled bars.

  //  We leave a 1 pixel gap between bars
  display->setColor(BLACK);
  display->fillRect(x , y, size_x, size_y);
  display->setColor(WHITE);
  if (WiFiConnected()) {
    for (uint8_t ibar = 0; ibar < nbars; ibar++) {
      int16_t height = size_y * (ibar + 1) / nbars;
      int16_t xpos = x + ibar * width;
      int16_t ypos = y + size_y - height;
      if (ibar <= nbars_filled) {
        // Fill complete bar
        display->fillRect(xpos, ypos, width - 1, height);
      } else {
        // Only draw top and bottom.
        display->fillRect(xpos, ypos, width - 1, 1);
        display->fillRect(xpos, y + size_y - 1, width - 1, 1);
      }
    }
  } else {
    // Draw a not connected sign (empty rectangle)
  	display->drawRect(x , y, size_x, size_y-1);
  }
  return true;
}
