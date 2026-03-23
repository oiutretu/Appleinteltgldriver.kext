//
//  IntelIOFra    ebuffer.h
//
//
//  IOFramebuffer integration for macOS WindowServer
//  Week 15: Native macOS graphics framework integration
//

#ifndef IntelIOFramebuffer_h
#define IntelIOFramebuffer_h

#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "linux_compat.h"

// Forward declarations
class IOGraphicsAccelerator2;
class IntelDisplay;
class IntelPipe;
class IntelPort;
class IntelModeSet;
class IntelFramebuffer;
class IntelDPLinkTraining;
class AppleIntelTGLController;  // GPU command submission controller
class IntelIOAccelerator;  // IOAccelerator for Metal/WindowServer

// Display mode information
#define kMaxDisplayModes    32

// Pixel formats supported
#define kPixelFormat_RGB565     0x00000001
#define kPixelFormat_RGB888     0x00000002
#define kPixelFormat_XRGB8888   0x00000004
#define kPixelFormat_ARGB8888   0x00000008

// Connection types
enum ConnectionType {
    kConnectionTypeNone = 0,
    kConnectionTypeDP,
    kConnectionTypeHDMI,
    kConnectionTypeeDP,
    kConnectionTypeDVI
};

/*!
 * @struct DisplayModeInfo
 * @abstract Display mode information structure
 */
struct DisplayModeInfo {
    IODisplayModeID     modeID;             // Unique mode identifier
    uint32_t            width;              // Horizontal resolution
    uint32_t            height;             // Vertical resolution
    uint32_t            refreshRate;        // Refresh rate in Hz
    uint32_t            flags;              // Mode flags
    IOTimingInformation timing;             // Detailed timing info
};

/*!
 * @class IntelIOFramebuffer
 * @abstract IOFramebuffer subclass for Intel graphics
 * @discussion Integrates with macOS WindowServer for native display support
 */
class IntelIOFramebuffer : public IOFramebuffer {
    OSDeclareDefaultStructors(IntelIOFramebuffer)
    
public:
    // IOService lifecycle
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    
    // IOFramebuffer required methods
    
    /*!
     * @function getPixelInformation
     * @abstract Get pixel format information for a display mode and depth
     */
    virtual IOReturn getPixelInformation(
        IODisplayModeID displayMode,
        IOIndex depth,
        IOPixelAperture aperture,
        IOPixelInformation* pixelInfo) override;
    
    /*!
     * @function getCurrentDisplayMode
     * @abstract Get the currently active display mode
     */
    virtual IOReturn getCurrentDisplayMode(
        IODisplayModeID* displayMode,
        IOIndex* depth) override;
    
    /*!
     * @function getDisplayModeCount
     * @abstract Get the number of available display modes
     */
    virtual IOItemCount getDisplayModeCount() override;
    
    /*!
     * @function getDisplayModes
     * @abstract Get array of available display mode IDs
     */
    virtual IOReturn getDisplayModes(IODisplayModeID* allDisplayModes) override;
    
    /*!
     * @function getInformationForDisplayMode
     * @abstract Get detailed information for a display mode
     */
    virtual IOReturn getInformationForDisplayMode(
        IODisplayModeID displayMode,
        IODisplayModeInformation* info) override;
    
    /*!
     * @function getStartupDisplayMode
     * @abstract Get the display mode to use at startup (CRITICAL for boot display)
     */
    virtual IOReturn getStartupDisplayMode(
        IODisplayModeID* displayMode,
        IOIndex* depth) override;
    
    /*!
     * @function getPixelFormats
     * @abstract Get supported pixel formats
     */
    virtual const char* getPixelFormats() override;
    
    /*!
     * @function getPixelFormatsForDisplayMode
     * @abstract Get pixel formats for a specific display mode
     */
    virtual UInt64 getPixelFormatsForDisplayMode(
        IODisplayModeID displayMode,
        IOIndex depth) override;
    
    /*!
     * @function getTimingInfoForDisplayMode
     * @abstract Get timing information for a display mode
     */
    virtual IOReturn getTimingInfoForDisplayMode(
        IODisplayModeID displayMode,
        IOTimingInformation* info) override;
    
    /*!
     * @function setDisplayMode
     * @abstract Set the display mode and depth
     */
    virtual IOReturn setDisplayMode(
        IODisplayModeID displayMode,
        IOIndex depth) override;
    
    /*!
     * @function getApertureRange
     * @abstract Get framebuffer aperture range
     */
    virtual IODeviceMemory* getApertureRange(IOPixelAperture aperture) override;
    
    /*!
     * @function getAperture
     * @abstract Get current aperture (system aperture)
     */
    virtual IOIndex getAperture() const;
    
    /*!
     * @function newUserClient
     * @abstract Create IOUserClient for WindowServer connection (CRITICAL!)
     */
    virtual IOReturn newUserClient(task_t owningTask,
                                   void* securityID,
                                   UInt32 type,
                                   OSDictionary* properties,
                                   IOUserClient** handler) override;
    
    /*!
     * @function getConnectionCount
     * @abstract Get number of display connections (usually 1)
     */
    virtual IOItemCount getConnectionCount() override;
    
    /*!
     * @function getAttributeForConnection
     * @abstract Get attribute value for a connection
     */
    virtual IOReturn getAttributeForConnection(
        IOIndex connectIndex,
        IOSelect attribute,
        uintptr_t* value) override;
    
    /*!
     * @function setAttributeForConnection
     * @abstract Set attribute value for a connection
     */
    virtual IOReturn setAttributeForConnection(
        IOIndex connectIndex,
        IOSelect attribute,
        uintptr_t value) override;
    
    // Cursor support
    
    /*!
     * @function setCursorImage
     * @abstract Set hardware cursor image
     */
    virtual IOReturn setCursorImage(void* cursorImage) override;
    
    /*!
     * @function setCursorState
     * @abstract Set cursor position and visibility
     */
    virtual IOReturn setCursorState(
        SInt32 x,
        SInt32 y,
        bool visible) override;
    
    // EDID support
    
    /*!
     * @function hasDDCConnect
     * @abstract Check if DDC/EDID is available
     */
    virtual bool hasDDCConnect(IOIndex connectIndex) override;
    
    /*!
     * @function getDDCBlock
     * @abstract Read EDID data
     */
    virtual IOReturn getDDCBlock(
        IOIndex connectIndex,
        UInt32 blockNumber,
        IOSelect blockType,
        IOOptionBits options,
        UInt8* data,
        IOByteCount* length) override;
    
    // Connection events
    
    /*!
     * @function processConnectChange
     * @abstract Handle display connection/disconnection
     */
    IOReturn processConnectChange(IOOptionBits* connectChanged);
    
    // Additional methods
    
    /*!
     * @function enableController
     * @abstract Enable the display controller
     */
    virtual IOReturn enableController() override;
    
    /*!
     * @function setAttribute
     * @abstract Set a generic attribute
     */
    virtual IOReturn setAttribute(IOSelect attribute, uintptr_t value) override;
    
    /*!
     * @function getAttribute
     * @abstract Get a generic attribute
     */
    virtual IOReturn getAttribute(IOSelect attribute, uintptr_t* value) override;
    
    // CRITICAL: WindowServer required methods
    
    /*!
     * @function isConsoleDevice
     * @abstract Check if this is the console device (for GUI transition)
     */
    virtual bool isConsoleDevice() override;
    
    /*!
     * @function makeUsable
     * @abstract Called by WindowServer before it can use the framebuffer
     */
    virtual bool makeUsable() ;
    
    /*!
     * @function clientMemoryForType
     * @abstract Get framebuffer memory for WindowServer rendering
     */
    virtual IOReturn clientMemoryForType(
        UInt32 type,
        UInt32* flags,
        IOMemoryDescriptor** memory) ;
    
    /*!
     * @function getVRAMRange
     * @abstract Get VRAM range as IODeviceMemory
     */
    virtual IODeviceMemory* getVRAMRange()override;
    
    /*!
     * @function createSharedCursor
     * @abstract Create shared cursor memory
     */
    virtual IOReturn createSharedCursor(
        IOIndex index,
        int version) ;
    
    /*!
     * @function setBounds
     * @abstract Set cursor bounds
     */
    virtual IOReturn setBounds(
        IOIndex index,
        IOGBounds* bounds) ;
    
    /*!
     * @function registerForInterruptType
     * @abstract Register for VSync interrupts
     */
    virtual IOReturn registerForInterruptType(
        IOSelect interruptType,
        IOFBInterruptProc proc,
        OSObject* target,
        void* ref,
        void** interruptRef) override;
    
    /*!
     * @function unregisterInterrupt
     * @abstract Unregister interrupt handler
     */
    virtual IOReturn unregisterInterrupt(void* interruptRef) override;
    
    /*!
     * @function flushDisplay
     * @abstract Flush display updates
     */
    virtual IOReturn flushDisplay() ;
    
    /*!
     * @function detectActivePipe
     * @abstract Detect which display pipe (A, B, or C) is currently active
     * @discussion Scans PIPE_CONF registers to find enabled pipe for scanout
     * @return 0=Pipe A, 1=Pipe B, 2=Pipe C, defaults to 0 if none active
     */
    int detectActivePipe();
    
    /*!
     * @function setScanoutSurface
     * @abstract Set IOSurface as direct scanout plane (MODERN macOS 10.13+)
     * @discussion Programs plane registers to scan out IOSurface directly.
     *             This is the CORRECT way to display GUI - NOT blitting.
     * @param gpuAddress GPU virtual address of the IOSurface (already in GGTT)
     * @param width Surface width in pixels
     * @param height Surface height in pixels
     * @param stride Surface stride (pitch) in bytes
     * @param pixelFormat Pixel format (0=XRGB8888, 1=ARGB8888)
     * @param tiling Tiling format (0=LINEAR, 1=X_TILED, 2=Y_TILED). Pass >2 to preserve current.
     * @return kIOReturnSuccess on success
     */
     IOReturn setScanoutSurface(uint64_t gpuAddress, uint32_t width, uint32_t height,
                                uint32_t stride, uint32_t pixelFormat, uint32_t tiling = 0xFFFFFFFFu);
    
    void deliverFramebufferNotification(IOIndex index, UInt32 event, void* info) ;
    
    // Query current display configuration
    uint32_t getCurrentWidth() const { return currentScanoutWidth; }
    uint32_t getCurrentHeight() const { return currentScanoutHeight; }
    uint32_t getCurrentStride() const { return currentScanoutStride; }

    
    // Power management
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService* whatDevice) override;
    virtual unsigned long maxCapabilityForDomainState(IOPMPowerFlags domainState) override;
    virtual unsigned long initialPowerStateForDomainState(IOPMPowerFlags domainState) override;
    virtual IOReturn powerStateWillChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice) override;
    virtual IOReturn powerStateDidChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice) override;
    
private:
    // Hardware
    IOPCIDevice*            m_pciDevice;        // PCI device (direct provider)
    AppleIntelTGLController*    m_i915Controller;   // GPU command submission controller
    
    // Hardware interfaces
    IntelDisplay*           m_display;          // Display manager
    IntelPipe*              m_pipe;             // Associated pipe
    IntelPort*              m_port;             // Associated port
    IntelModeSet*           m_modeSet;          // Mode setting
    IntelFramebuffer*       m_framebuffer;      // Framebuffer
    IntelDPLinkTraining*    m_linkTraining;     // DP training
    
    // VRAM/Framebuffer memory (like reference FakeIrisXEFramebuffer)
    IOBufferMemoryDescriptor* vramMemory;       // Framebuffer descriptor
    
    // Current state
    IODisplayModeID         m_currentMode;      // Current mode ID
    IOIndex                 m_currentDepth;     // Current depth index
    bool                    m_enabled;          // Controller enabled
    bool                    m_connected;        // Display connected
    ConnectionType          m_connectionType;   // Connection type
    
    // Display modes
    DisplayModeInfo         m_modes[kMaxDisplayModes];
    uint32_t                m_modeCount;        // Number of modes
    
    // Cursor state
    bool                    m_cursorVisible;
    SInt32                  m_cursorX;
    SInt32                  m_cursorY;
    void*                   m_cursorImage;
    IOBufferMemoryDescriptor* m_cursorBuffer;
    
    // WindowServer critical memory descriptors
    IOBufferMemoryDescriptor* cursorMemory;     // Shared cursor memory
    IODeviceMemory*           vramRange;        // VRAM range for clientMemoryForType
    IOMemoryDescriptor*       framebufferSurface; // Main framebuffer surface
    
    // Interrupt support
    struct InterruptInfo {
        IOFBInterruptProc proc;
        OSObject* target;
        void* ref;
    };
    OSArray*                  interruptList;    // List of registered interrupts
    IOTimerEventSource*       m_vsyncTimer;     // VSync generation timer
    IOTimerEventSource*       m_forceGUITimer;  // Force GUI transition timer
    
    // Hot-plug detection
    IOTimerEventSource*     m_hotplugTimer;     // Hotplug poll timer
    bool                    m_lastConnected;    // Last connection state
    
    // Scanout surface tracking (modern macOS 10.13+)
    // SEMANTICS:



    uint64_t                currentScanoutPhysicalAddress;  // Physical addr (0 if not our binding)
    uint32_t                currentScanoutGTTOffset;        // GTT offset (0 if not our binding)
    uint32_t                currentScanoutWidth;            // Current scanout width
    uint32_t                currentScanoutHeight;           // Current scanout height
    uint32_t                currentScanoutStride;           // Current scanout stride (bytes)
    uint32_t                currentScanoutFormat;           // Current scanout pixel format
    

     IOReturn setNumberOfDisplays(UInt32 count) ;

    /*!
     * @function forceGUITransition
     * @abstract Force console to GUI transition
     */
    void forceGUITransition();
    
    /*!
     * @function forceGUITimerFired
     * @abstract Timer callback to force GUI transition after delay
     */
    static void forceGUITimerFired(OSObject* target, IOTimerEventSource* sender);

    // Power management
    class IntelPowerManagement* m_powerManagement;
    unsigned long           m_currentPowerState;
    bool                    m_displayPowered;
    
    // Synchronization
    IOLock*                 m_lock;
    
    // MMIO access (unified architecture - we own the mapping)
    volatile uint8_t*       mmioBase;
    
    /*!
     * @function safeMMIORead
     * @abstract Safe MMIO read with bounds checking (no mmioMap bounds check in nub mode)
     */
    inline uint32_t safeMMIORead(uint32_t offset) {
        if (!mmioBase || offset >= 0x1000000) {  // 16MB BAR0 size
            IOLog("ERR  MMIO Read attempted with invalid offset: 0x%08X\n", offset);
            return 0;
        }
        return *(volatile uint32_t*)(mmioBase + offset);
    }
    
    /*!
     * @function safeMMIOWrite
     * @abstract Safe MMIO write with bounds checking (no mmioMap bounds check in nub mode)
     */
    inline void safeMMIOWrite(uint32_t offset, uint32_t value) {
        if (!mmioBase || offset >= 0x1000000) {  // 16MB BAR0 size
            IOLog("ERR  MMIO Write attempted with invalid offset: 0x%08X\n", offset);
            return;
        }
        *(volatile uint32_t*)(mmioBase + offset) = value;
    }
    
    // Initialization
    
    /*!
     * @function initializeHardware
     * @abstract Initialize display hardware
     */
    bool initializeHardware();
    
    /*!
     * @function buildModeList
     * @abstract Build list of supported display modes
     */
    bool buildModeList();
    
    /*!
     * @function addMode
     * @abstract Add a display mode to the list
     */
    bool addMode(uint32_t width, uint32_t height, uint32_t refreshRate);
    
    /*!
     * @function findMode
     * @abstract Find mode info by mode ID
     */
    DisplayModeInfo* findMode(IODisplayModeID modeID);
    
    // Hardware control
    
    /*!
     * @function applyDisplayMode
     * @abstract Apply display mode to hardware
     */
    bool applyDisplayMode(DisplayModeInfo* mode);
    
    /*!
     * @function detectConnection
     * @abstract Detect if display is connected
     */
    bool detectConnection();
    
    /*!
     * @function trainDisplayPort
     * @abstract Train DisplayPort link
     */
    bool trainDisplayPort();
    
    // Cursor management
    
    /*!
     * @function initCursor
     * @abstract Initialize cursor hardware
     */
    bool initCursor();
    
    /*!
     * @function updateCursor
     * @abstract Update cursor position and visibility
     */
    void updateCursor();
    
    // Hotplug
    
    /*!
     * @function hotplugTimerFired
     * @abstract Timer callback for hotplug detection
     */
    static void hotplugTimerFired(OSObject* target, IOTimerEventSource* sender);
    
    /*!
     * @function handleHotplug
     * @abstract Handle hotplug event
     */
    void handleHotplug();
    
    /*!
     * @function vsyncTimerFired
     * @abstract Timer callback for VSync interrupt generation
     */
    static void vsyncTimerFired(OSObject* target, IOTimerEventSource* sender);
    
    /*!
     * @function fireVSyncInterrupt
     * @abstract Fire VSync interrupt to WindowServer
     */
    void fireVSyncInterrupt();
};

#endif /* IntelIOFramebuffer_h */
