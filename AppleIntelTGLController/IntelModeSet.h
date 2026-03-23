/*
 * IntelModeSet.h - Display Mode Setting
 * 
 * Handles EDID parsing, display timing calculation, and mode setting.
 * Ported from Linux i915 intel_display.c and intel_modes.c
 */

#ifndef IntelModeSet_h
#define IntelModeSet_h

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>

// Forward declarations
class AppleIntelTGLController;
class IntelDisplay;
class IntelPipe;

// EDID structure (128 bytes)
struct EDID {
    uint8_t header[8];
    uint16_t manufacturerId;
    uint16_t productCode;
    uint32_t serialNumber;
    uint8_t weekOfManufacture;
    uint8_t yearOfManufacture;
    uint8_t edidVersion;
    uint8_t edidRevision;
    uint8_t videoInputDefinition;
    uint8_t maxHorizontalImageSize;
    uint8_t maxVerticalImageSize;
    uint8_t displayTransferCharacteristic;
    uint8_t featureSupport;
    uint8_t colorCharacteristics[10];
    uint8_t establishedTimings[3];
    uint8_t standardTimings[16];
    uint8_t detailedTimings[72];
    uint8_t extensionFlag;
    uint8_t checksum;
} __attribute__((packed));

// Display mode timing
struct DisplayMode {
    // Identification
    char name[32];
    uint32_t modeId;
    
    // Clock
    uint32_t clock;  // kHz
    
    // Horizontal timing
    uint16_t hdisplay;
    uint16_t hsyncStart;
    uint16_t hsyncEnd;
    uint16_t htotal;
    uint16_t hskew;
    
    // Vertical timing
    uint16_t vdisplay;
    uint16_t vsyncStart;
    uint16_t vsyncEnd;
    uint16_t vtotal;
    uint16_t vscan;
    
    // Flags
    uint32_t flags;
    #define MODE_FLAG_PHSYNC    (1 << 0)
    #define MODE_FLAG_NHSYNC    (1 << 1)
    #define MODE_FLAG_PVSYNC    (1 << 2)
    #define MODE_FLAG_NVSYNC    (1 << 3)
    #define MODE_FLAG_INTERLACE (1 << 4)
    #define MODE_FLAG_DBLSCAN   (1 << 5)
    
    // Calculated values
    uint32_t vrefresh;  // Hz
    uint32_t hrefresh;  // kHz
};

class IntelModeSet : public OSObject {
    OSDeclareDefaultStructors(IntelModeSet)
    
public:
    // Initialization
    virtual bool init(AppleIntelTGLController *controller, IntelDisplay *display);
    virtual void free() override;
    
    // EDID operations
    bool readEDID(IntelPipe *pipe, EDID *edid);
    bool parseEDID(const EDID *edid);
    void dumpEDID(const EDID *edid);
    
    // Mode operations
    bool addMode(const DisplayMode *mode);
    DisplayMode* findMode(uint16_t width, uint16_t height, uint32_t refresh);
    DisplayMode* getBestMode();
    void clearModes();
    
    // Mode setting
    bool setMode(IntelPipe *pipe, const DisplayMode *mode);
    bool validateMode(const DisplayMode *mode);
    
    // Timing calculation
    static void calculateTimings(DisplayMode *mode, uint16_t width, uint16_t height, 
                                 uint32_t refresh, bool reduced);
    static bool calculateClock(DisplayMode *mode, uint32_t targetClock);
    
    // Getters
    uint32_t getModeCount() const { return modeCount; }
    DisplayMode* getMode(uint32_t index);
    
private:
    AppleIntelTGLController *controller;
    IntelDisplay *display;
    
    // Mode list
    static const uint32_t MAX_MODES = 64;
    DisplayMode modes[MAX_MODES];
    uint32_t modeCount;
    
    // EDID parsing helpers
    bool parseDetailedTiming(const uint8_t *data, DisplayMode *mode);
    bool parseStandardTiming(uint8_t byte1, uint8_t byte2, DisplayMode *mode);
    bool parseEstablishedTiming(const uint8_t *data);
    
    // DDC/I2C communication
    bool i2cRead(uint8_t addr, uint8_t *data, uint32_t length);
    bool i2cWrite(uint8_t addr, const uint8_t *data, uint32_t length);
    bool i2cStart();
    void i2cStop();
    bool i2cWriteByte(uint8_t byte);
    bool i2cReadByte(uint8_t *byte, bool ack);
    
    // I2C GPIO bit-banging
    void setSDA(bool high);
    void setSCL(bool high);
    bool getSDA();
    bool getSCL();
    void i2cDelay();
    
    // Validation helpers
    bool isValidClock(uint32_t clock);
    bool isValidTiming(const DisplayMode *mode);
};

#endif /* IntelModeSet_h */
