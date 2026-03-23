/*
 * IntelModeSet.cpp - Display Mode Setting Implementation
 * 
 * Implements EDID parsing, display timing calculation, and mode setting.
 * Ported from Linux i915 intel_display.c and intel_modes.c
 */

#include "IntelModeSet.h"
#include "AppleIntelTGLController.h"
#include "IntelDisplay.h"
#include "IntelPipe.h"
#include "IntelUncore.h"
#include "linux_compat.h"

// I2C/DDC register offsets (for GPIO bit-banging)
#define GPIO_GMBUS0     0xC5100
#define GPIO_GMBUS1     0xC5104
#define GPIO_GMBUS2     0xC5108
#define GPIO_GMBUS3     0xC510C
#define GPIO_GMBUS4     0xC5110
#define GPIO_GMBUS5     0xC5120

// I2C addresses
#define DDC_ADDR        0x50  // EDID address

// Timing constants
#define MIN_CLOCK_KHZ   25000
#define MAX_CLOCK_KHZ   600000
#define I2C_DELAY_US    10

#define super OSObject
OSDefineMetaClassAndStructors(IntelModeSet, OSObject)

bool IntelModeSet::init(AppleIntelTGLController *ctrl, IntelDisplay *disp) {
    if (!super::init()) {
        return false;
    }
    
    controller = ctrl;
    display = disp;
    modeCount = 0;
    
    // Clear mode list
    memset(modes, 0, sizeof(modes));
    
    IOLog("IntelModeSet: Initialized\n");
    return true;
}

void IntelModeSet::free() {
    controller = NULL;
    display = NULL;
    
    super::free();
}

// Read EDID from display via DDC
bool IntelModeSet::readEDID(IntelPipe *pipe, EDID *edid) {
    if (!pipe || !edid) {
        return false;
    }
    
    IOLog("IntelModeSet: Reading EDID...\n");
    
    // Initialize I2C
    if (!i2cStart()) {
        IOLog("IntelModeSet: Failed to start I2C\n");
        return false;
    }
    
    // Write DDC address with write bit
    if (!i2cWriteByte(DDC_ADDR << 1)) {
        IOLog("IntelModeSet: Failed to write DDC address\n");
        i2cStop();
        return false;
    }
    
    // Write EDID offset (0x00)
    if (!i2cWriteByte(0x00)) {
        IOLog("IntelModeSet: Failed to write EDID offset\n");
        i2cStop();
        return false;
    }
    
    // Restart with read bit
    if (!i2cStart()) {
        IOLog("IntelModeSet: Failed to restart I2C\n");
        i2cStop();
        return false;
    }
    
    if (!i2cWriteByte((DDC_ADDR << 1) | 1)) {
        IOLog("IntelModeSet: Failed to write DDC read address\n");
        i2cStop();
        return false;
    }
    
    // Read 128 bytes of EDID
    uint8_t *data = (uint8_t *)edid;
    for (uint32_t i = 0; i < sizeof(EDID); i++) {
        bool ack = (i < sizeof(EDID) - 1);
        if (!i2cReadByte(&data[i], ack)) {
            IOLog("IntelModeSet: Failed to read EDID byte %u\n", i);
            i2cStop();
            return false;
        }
    }
    
    i2cStop();
    
    // Verify EDID header
    const uint8_t edidHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (memcmp(edid->header, edidHeader, 8) != 0) {
        IOLog("IntelModeSet: Invalid EDID header\n");
        return false;
    }
    
    // Verify checksum
    uint8_t sum = 0;
    for (uint32_t i = 0; i < sizeof(EDID); i++) {
        sum += data[i];
    }
    if (sum != 0) {
        IOLog("IntelModeSet: EDID checksum failed\n");
        return false;
    }
    
    IOLog("IntelModeSet: EDID read successfully\n");
    return true;
}

// Parse EDID and extract display modes
bool IntelModeSet::parseEDID(const EDID *edid) {
    if (!edid) {
        return false;
    }
    
    IOLog("IntelModeSet: Parsing EDID...\n");
    
    // Clear existing modes
    clearModes();
    
    // Parse established timings
    parseEstablishedTiming(edid->establishedTimings);
    
    // Parse standard timings
    for (int i = 0; i < 8; i++) {
        uint8_t byte1 = edid->standardTimings[i * 2];
        uint8_t byte2 = edid->standardTimings[i * 2 + 1];
        
        if (byte1 != 0x01 || byte2 != 0x01) {  // Not unused
            DisplayMode mode;
            if (parseStandardTiming(byte1, byte2, &mode)) {
                addMode(&mode);
            }
        }
    }
    
    // Parse detailed timings
    for (int i = 0; i < 4; i++) {
        const uint8_t *data = edid->detailedTimings + (i * 18);
        
        // Check if this is a valid detailed timing (not a descriptor)
        if (data[0] != 0 || data[1] != 0) {
            DisplayMode mode;
            if (parseDetailedTiming(data, &mode)) {
                addMode(&mode);
            }
        }
    }
    
    IOLog("IntelModeSet: Parsed %u modes from EDID\n", modeCount);
    return modeCount > 0;
}

// Parse detailed timing descriptor
bool IntelModeSet::parseDetailedTiming(const uint8_t *data, DisplayMode *mode) {
    if (!data || !mode) {
        return false;
    }
    
    memset(mode, 0, sizeof(DisplayMode));
    
    // Pixel clock (in 10kHz units)
    uint16_t pixelClock = data[0] | (data[1] << 8);
    mode->clock = pixelClock * 10;
    
    // Horizontal
    mode->hdisplay = data[2] | ((data[4] & 0xF0) << 4);
    uint16_t hblank = data[3] | ((data[4] & 0x0F) << 8);
    mode->htotal = mode->hdisplay + hblank;
    
    mode->hsyncStart = mode->hdisplay + (data[8] | ((data[11] & 0xC0) << 2));
    uint16_t hsyncWidth = data[9] | ((data[11] & 0x30) << 4);
    mode->hsyncEnd = mode->hsyncStart + hsyncWidth;
    
    // Vertical
    mode->vdisplay = data[5] | ((data[7] & 0xF0) << 4);
    uint16_t vblank = data[6] | ((data[7] & 0x0F) << 8);
    mode->vtotal = mode->vdisplay + vblank;
    
    mode->vsyncStart = mode->vdisplay + ((data[10] >> 4) | ((data[11] & 0x0C) << 2));
    uint16_t vsyncWidth = (data[10] & 0x0F) | ((data[11] & 0x03) << 4);
    mode->vsyncEnd = mode->vsyncStart + vsyncWidth;
    
    // Flags
    mode->flags = 0;
    if (data[17] & 0x02) mode->flags |= MODE_FLAG_PHSYNC;
    else mode->flags |= MODE_FLAG_NHSYNC;
    
    if (data[17] & 0x04) mode->flags |= MODE_FLAG_PVSYNC;
    else mode->flags |= MODE_FLAG_NVSYNC;
    
    if (data[17] & 0x80) mode->flags |= MODE_FLAG_INTERLACE;
    
    // Calculate refresh rate
    if (mode->htotal > 0 && mode->vtotal > 0) {
        mode->vrefresh = (mode->clock * 1000) / (mode->htotal * mode->vtotal);
        mode->hrefresh = (mode->clock * 1000) / mode->htotal;
    }
    
    // Generate name
    snprintf(mode->name, sizeof(mode->name), "%dx%d@%dHz",
             mode->hdisplay, mode->vdisplay, mode->vrefresh);
    
    return validateMode(mode);
}

// Parse standard timing
bool IntelModeSet::parseStandardTiming(uint8_t byte1, uint8_t byte2, DisplayMode *mode) {
    if (!mode || byte1 == 0x00 || byte1 == 0x01) {
        return false;
    }
    
    memset(mode, 0, sizeof(DisplayMode));
    
    // Calculate width and height
    uint16_t width = (byte1 + 31) * 8;
    uint16_t height;
    
    uint8_t aspectRatio = (byte2 >> 6) & 0x03;
    switch (aspectRatio) {
        case 0: height = (width * 10) / 16; break;  // 16:10
        case 1: height = (width * 3) / 4; break;    // 4:3
        case 2: height = (width * 4) / 5; break;    // 5:4
        case 3: height = (width * 9) / 16; break;   // 16:9
        default: return false;
    }
    
    // Refresh rate
    uint32_t refresh = (byte2 & 0x3F) + 60;
    
    // Calculate timings using CVT reduced blanking
    calculateTimings(mode, width, height, refresh, true);
    
    snprintf(mode->name, sizeof(mode->name), "%dx%d@%dHz",
             width, height, refresh);
    
    return validateMode(mode);
}

// Parse established timings
bool IntelModeSet::parseEstablishedTiming(const uint8_t *data) {
    if (!data) {
        return false;
    }
    
    // Common VESA timings
    struct {
        uint8_t byte;
        uint8_t bit;
        uint16_t width;
        uint16_t height;
        uint32_t refresh;
    } timings[] = {
        {0, 0, 800, 600, 60}, {0, 1, 800, 600, 56},
        {0, 2, 640, 480, 75}, {0, 3, 640, 480, 72},
        {0, 4, 640, 480, 67}, {0, 5, 640, 480, 60},
        {0, 6, 720, 400, 88}, {0, 7, 720, 400, 70},
        {1, 0, 1280, 1024, 75}, {1, 1, 1024, 768, 75},
        {1, 2, 1024, 768, 70}, {1, 3, 1024, 768, 60},
        {1, 4, 1024, 768, 87}, {1, 5, 832, 624, 75},
        {1, 6, 800, 600, 75}, {1, 7, 800, 600, 72},
    };
    
    for (size_t i = 0; i < sizeof(timings) / sizeof(timings[0]); i++) {
        if (data[timings[i].byte] & (1 << timings[i].bit)) {
            DisplayMode mode;
            calculateTimings(&mode, timings[i].width, timings[i].height,
                           timings[i].refresh, false);
            addMode(&mode);
        }
    }
    
    return true;
}

// Calculate display timings using CVT/GTF
void IntelModeSet::calculateTimings(DisplayMode *mode, uint16_t width, uint16_t height,
                                    uint32_t refresh, bool reduced) {
    memset(mode, 0, sizeof(DisplayMode));
    
    mode->hdisplay = width;
    mode->vdisplay = height;
    mode->vrefresh = refresh;
    
    if (reduced) {
        // CVT Reduced Blanking
        mode->hsyncStart = width + 80;
        mode->hsyncEnd = mode->hsyncStart + 32;
        mode->htotal = mode->hsyncEnd + 48;
        
        mode->vsyncStart = height + 3;
        mode->vsyncEnd = mode->vsyncStart + 6;
        mode->vtotal = mode->vsyncEnd + 6;
        
        mode->flags = MODE_FLAG_PHSYNC | MODE_FLAG_NVSYNC;
    } else {
        // GTF standard blanking
        mode->hsyncStart = width + 88;
        mode->hsyncEnd = mode->hsyncStart + 44;
        mode->htotal = mode->hsyncEnd + 148;
        
        mode->vsyncStart = height + 4;
        mode->vsyncEnd = mode->vsyncStart + 5;
        mode->vtotal = mode->vsyncEnd + 15;
        
        mode->flags = MODE_FLAG_NHSYNC | MODE_FLAG_PVSYNC;
    }
    
    // Calculate pixel clock
    mode->clock = (mode->htotal * mode->vtotal * refresh) / 1000;
    mode->hrefresh = (mode->clock * 1000) / mode->htotal;
    
    snprintf(mode->name, sizeof(mode->name), "%dx%d@%dHz",
             width, height, refresh);
}

// Add mode to list
bool IntelModeSet::addMode(const DisplayMode *mode) {
    if (!mode || modeCount >= MAX_MODES) {
        return false;
    }
    
    // Check for duplicates
    for (uint32_t i = 0; i < modeCount; i++) {
        if (modes[i].hdisplay == mode->hdisplay &&
            modes[i].vdisplay == mode->vdisplay &&
            modes[i].vrefresh == mode->vrefresh) {
            return false;  // Already exists
        }
    }
    
    memcpy(&modes[modeCount], mode, sizeof(DisplayMode));
    modes[modeCount].modeId = modeCount;
    modeCount++;
    
    return true;
}

// Find specific mode
DisplayMode* IntelModeSet::findMode(uint16_t width, uint16_t height, uint32_t refresh) {
    for (uint32_t i = 0; i < modeCount; i++) {
        if (modes[i].hdisplay == width &&
            modes[i].vdisplay == height &&
            modes[i].vrefresh == refresh) {
            return &modes[i];
        }
    }
    return NULL;
}

// Get best mode (highest resolution and refresh rate)
DisplayMode* IntelModeSet::getBestMode() {
    if (modeCount == 0) {
        return NULL;
    }
    
    DisplayMode *best = &modes[0];
    for (uint32_t i = 1; i < modeCount; i++) {
        uint32_t bestPixels = best->hdisplay * best->vdisplay;
        uint32_t pixels = modes[i].hdisplay * modes[i].vdisplay;
        
        if (pixels > bestPixels ||
            (pixels == bestPixels && modes[i].vrefresh > best->vrefresh)) {
            best = &modes[i];
        }
    }
    
    return best;
}

// Get mode by index
DisplayMode* IntelModeSet::getMode(uint32_t index) {
    if (index >= modeCount) {
        return NULL;
    }
    return &modes[index];
}

// Clear mode list
void IntelModeSet::clearModes() {
    modeCount = 0;
    memset(modes, 0, sizeof(modes));
}

// Set display mode
bool IntelModeSet::setMode(IntelPipe *pipe, const DisplayMode *mode) {
    if (!pipe || !mode || !controller) {
        return false;
    }
    
    if (!validateMode(mode)) {
        IOLog("IntelModeSet: Invalid mode %s\n", mode->name);
        return false;
    }
    
    IOLog("IntelModeSet: Setting mode %s\n", mode->name);
    
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) {
        return false;
    }
    
    // Get pipe registers
    uint32_t pipeOffset = pipe->getRegisterOffset();
    
    // Program horizontal timings
    uncore->writeRegister32(pipeOffset + 0x00000, mode->htotal << 16 | mode->hdisplay);
    uncore->writeRegister32(pipeOffset + 0x00004, mode->hsyncEnd << 16 | mode->hsyncStart);
    
    // Program vertical timings
    uncore->writeRegister32(pipeOffset + 0x00008, mode->vtotal << 16 | mode->vdisplay);
    uncore->writeRegister32(pipeOffset + 0x0000C, mode->vsyncEnd << 16 | mode->vsyncStart);
    
    // Program clock
    // TODO: Configure DPLL with mode->clock
    
    IOLog("IntelModeSet: Mode set successfully\n");
    return true;
}

// Validate mode
bool IntelModeSet::validateMode(const DisplayMode *mode) {
    if (!mode) {
        return false;
    }
    
    // Check basic validity
    if (mode->hdisplay == 0 || mode->vdisplay == 0 ||
        mode->htotal <= mode->hdisplay || mode->vtotal <= mode->vdisplay) {
        return false;
    }
    
    // Check clock range
    if (!isValidClock(mode->clock)) {
        return false;
    }
    
    // Check timing consistency
    if (mode->hsyncStart < mode->hdisplay ||
        mode->hsyncEnd < mode->hsyncStart ||
        mode->htotal < mode->hsyncEnd) {
        return false;
    }
    
    if (mode->vsyncStart < mode->vdisplay ||
        mode->vsyncEnd < mode->vsyncStart ||
        mode->vtotal < mode->vsyncEnd) {
        return false;
    }
    
    return true;
}

// Check if clock is valid
bool IntelModeSet::isValidClock(uint32_t clock) {
    return clock >= MIN_CLOCK_KHZ && clock <= MAX_CLOCK_KHZ;
}

// Dump EDID information
void IntelModeSet::dumpEDID(const EDID *edid) {
    if (!edid) {
        return;
    }
    
    IOLog("IntelModeSet: EDID Information:\n");
    IOLog("  Manufacturer: %04X\n", edid->manufacturerId);
    IOLog("  Product Code: %04X\n", edid->productCode);
    IOLog("  Serial: %08X\n", edid->serialNumber);
    IOLog("  Manufactured: Week %u, Year %u\n",
          edid->weekOfManufacture, edid->yearOfManufacture + 1990);
    IOLog("  EDID Version: %u.%u\n", edid->edidVersion, edid->edidRevision);
    IOLog("  Max Image Size: %u x %u cm\n",
          edid->maxHorizontalImageSize, edid->maxVerticalImageSize);
}

// I2C operations
bool IntelModeSet::i2cStart() {
    setSDA(true);
    setSCL(true);
    i2cDelay();
    
    setSDA(false);
    i2cDelay();
    setSCL(false);
    i2cDelay();
    
    return true;
}

void IntelModeSet::i2cStop() {
    setSDA(false);
    i2cDelay();
    setSCL(true);
    i2cDelay();
    setSDA(true);
    i2cDelay();
}

bool IntelModeSet::i2cWriteByte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        setSDA((byte >> i) & 1);
        i2cDelay();
        setSCL(true);
        i2cDelay();
        setSCL(false);
        i2cDelay();
    }
    
    // Read ACK
    setSDA(true);
    i2cDelay();
    setSCL(true);
    i2cDelay();
    bool ack = !getSDA();
    setSCL(false);
    i2cDelay();
    
    return ack;
}

bool IntelModeSet::i2cReadByte(uint8_t *byte, bool ack) {
    if (!byte) {
        return false;
    }
    
    *byte = 0;
    setSDA(true);
    
    for (int i = 7; i >= 0; i--) {
        i2cDelay();
        setSCL(true);
        i2cDelay();
        
        if (getSDA()) {
            *byte |= (1 << i);
        }
        
        setSCL(false);
    }
    
    // Send ACK/NACK
    setSDA(!ack);
    i2cDelay();
    setSCL(true);
    i2cDelay();
    setSCL(false);
    i2cDelay();
    setSDA(true);
    
    return true;
}

// GPIO bit-banging for I2C
void IntelModeSet::setSDA(bool high) {
    if (!controller) return;
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) return;
    
    uint32_t val = uncore->readRegister32(GPIO_GMBUS2);
    if (high) {
        val |= (1 << 0);  // SDA high
    } else {
        val &= ~(1 << 0); // SDA low
    }
    uncore->writeRegister32(GPIO_GMBUS2, val);
}

void IntelModeSet::setSCL(bool high) {
    if (!controller) return;
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) return;
    
    uint32_t val = uncore->readRegister32(GPIO_GMBUS2);
    if (high) {
        val |= (1 << 1);  // SCL high
    } else {
        val &= ~(1 << 1); // SCL low
    }
    uncore->writeRegister32(GPIO_GMBUS2, val);
}

bool IntelModeSet::getSDA() {
    if (!controller) return false;
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) return false;
    
    uint32_t val = uncore->readRegister32(GPIO_GMBUS2);
    return (val & (1 << 0)) != 0;
}

bool IntelModeSet::getSCL() {
    if (!controller) return false;
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) return false;
    
    uint32_t val = uncore->readRegister32(GPIO_GMBUS2);
    return (val & (1 << 1)) != 0;
}

void IntelModeSet::i2cDelay() {
    IODelay(I2C_DELAY_US);
}
