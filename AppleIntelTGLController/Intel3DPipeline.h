/*
 * Intel3DPipeline.h - 3D Rendering Pipeline
 * Week 26 - Phase 6: 3D Hardware Acceleration
 *
 * Implements 3D rendering pipeline for Intel Tiger Lake (Gen12) GPU
 * - Pipeline state management (vertex, geometry, fragment)
 * - Shader program support (vertex/fragment/compute)
 * - Resource binding (textures, buffers, samplers)
 * - Render target configuration
 * - Depth/stencil operations
 * - Rasterization state
 */

#ifndef Intel3DPipeline_h
#define Intel3DPipeline_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "AppleIntelTGLController.h"
#include "IntelRingBuffer.h"
#include "IntelContext.h"
#include "IntelRequest.h"
#include "IntelGEMObject.h"

class AppleIntelTGLController;
class IntelRingBuffer;
class IntelContext;
class IntelRequest;
class IntelGEMObject;

// Forward declarations
struct Intel3DState;
struct IntelShaderProgram;
struct IntelRenderTarget;
struct IntelTexture;
struct IntelBuffer;
struct IntelSampler;
struct Pipeline3DStats;

// 3D Command Opcodes (Gen12)
#define GEN12_3DSTATE_PIPELINESELECT         0x69040000
#define GEN12_3DSTATE_BINDING_TABLE_POOL_ALLOC 0x79190000
#define GEN12_3DSTATE_VS                     0x78100000
#define GEN12_3DSTATE_HS                     0x781B0000
#define GEN12_3DSTATE_DS                     0x781D0000
#define GEN12_3DSTATE_GS                     0x78110000
#define GEN12_3DSTATE_PS                     0x78200000
#define GEN12_3DSTATE_SBE                    0x781F0000
#define GEN12_3DSTATE_VIEWPORT_STATE_POINTERS 0x780D0000
#define GEN12_3DSTATE_CC_STATE_POINTERS      0x780E0000
#define GEN12_3DSTATE_SCISSOR_STATE_POINTERS 0x780F0000
#define GEN12_3DSTATE_DEPTH_BUFFER           0x78050000
#define GEN12_3DSTATE_STENCIL_BUFFER         0x78060000
#define GEN12_3DPRIMITIVE                    0x7A000000
#define GEN12_PIPE_CONTROL                   0x7A000000

// Pipeline Types
enum IntelPipelineType {
    PIPELINE_3D      = 0,  // 3D graphics pipeline
    PIPELINE_GPGPU   = 1,  // Compute/GPGPU pipeline
    PIPELINE_MEDIA   = 2,  // Media pipeline
    PIPELINE_BLITTER = 3   // Blitter pipeline
};

// Shader Types
enum IntelShaderType {
    SHADER_VERTEX    = 0,  // Vertex shader
    SHADER_HULL      = 1,  // Hull shader (tessellation control)
    SHADER_DOMAIN    = 2,  // Domain shader (tessellation evaluation)
    SHADER_GEOMETRY  = 3,  // Geometry shader
    SHADER_FRAGMENT  = 4,  // Fragment/Pixel shader
    SHADER_COMPUTE   = 5   // Compute shader
};

// Primitive Topology
enum IntelPrimitiveTopology {
    TOPOLOGY_POINTS          = 0x01,  // Point list
    TOPOLOGY_LINES           = 0x02,  // Line list
    TOPOLOGY_LINE_STRIP      = 0x03,  // Line strip
    TOPOLOGY_TRIANGLES       = 0x04,  // Triangle list
    TOPOLOGY_TRIANGLE_STRIP  = 0x05,  // Triangle strip
    TOPOLOGY_TRIANGLE_FAN    = 0x06,  // Triangle fan
    TOPOLOGY_QUADS           = 0x07,  // Quad list
    TOPOLOGY_QUAD_STRIP      = 0x08   // Quad strip
};

// Depth/Stencil Operations
enum IntelDepthStencilOp {
    DEPTH_OP_NEVER    = 0,  // Never pass
    DEPTH_OP_LESS     = 1,  // Pass if less
    DEPTH_OP_EQUAL    = 2,  // Pass if equal
    DEPTH_OP_LEQUAL   = 3,  // Pass if less or equal
    DEPTH_OP_GREATER  = 4,  // Pass if greater
    DEPTH_OP_NOTEQUAL = 5,  // Pass if not equal
    DEPTH_OP_GEQUAL   = 6,  // Pass if greater or equal
    DEPTH_OP_ALWAYS   = 7   // Always pass
};

// Blend Operations
enum IntelBlendOp {
    BLEND_OP_ADD              = 0,  // Add src + dst
    BLEND_OP_SUBTRACT         = 1,  // Subtract src - dst
    BLEND_OP_REVERSE_SUBTRACT = 2,  // Subtract dst - src
    BLEND_OP_MIN              = 3,  // Minimum of src, dst
    BLEND_OP_MAX              = 4   // Maximum of src, dst
};

// Blend Factors
enum IntelBlendFactor {
    BLEND_ZERO                = 0,   // 0
    BLEND_ONE                 = 1,   // 1
    BLEND_SRC_COLOR           = 2,   // Source color
    BLEND_INV_SRC_COLOR       = 3,   // 1 - source color
    BLEND_SRC_ALPHA           = 4,   // Source alpha
    BLEND_INV_SRC_ALPHA       = 5,   // 1 - source alpha
    BLEND_DST_COLOR           = 6,   // Destination color
    BLEND_INV_DST_COLOR       = 7,   // 1 - destination color
    BLEND_DST_ALPHA           = 8,   // Destination alpha
    BLEND_INV_DST_ALPHA       = 9    // 1 - destination alpha
};

// Cull Modes
enum IntelCullMode {
    CULL_NONE  = 0,  // No culling
    CULL_FRONT = 1,  // Cull front faces
    CULL_BACK  = 2   // Cull back faces
};

// Fill Modes
enum IntelFillMode {
    FILL_SOLID     = 0,  // Solid fill
    FILL_WIREFRAME = 1,  // Wireframe
    FILL_POINT     = 2   // Point cloud
};

// Texture Formats
enum IntelTextureFormat {
    TEX_FORMAT_R8_UNORM       = 0,   // 8-bit red
    TEX_FORMAT_RG8_UNORM      = 1,   // 8-bit red/green
    TEX_FORMAT_RGBA8_UNORM    = 2,   // 8-bit RGBA
    TEX_FORMAT_BGRA8_UNORM    = 3,   // 8-bit BGRA
    TEX_FORMAT_R16_FLOAT      = 4,   // 16-bit float red
    TEX_FORMAT_RG16_FLOAT     = 5,   // 16-bit float red/green
    TEX_FORMAT_RGBA16_FLOAT   = 6,   // 16-bit float RGBA
    TEX_FORMAT_R32_FLOAT      = 7,   // 32-bit float red
    TEX_FORMAT_RG32_FLOAT     = 8,   // 32-bit float red/green
    TEX_FORMAT_RGBA32_FLOAT   = 9,   // 32-bit float RGBA
    TEX_FORMAT_DEPTH24_STENCIL8 = 10 // Depth/stencil
};

// Texture Filters
enum IntelTextureFilter {
    FILTER_NEAREST = 0,  // Nearest neighbor
    FILTER_LINEAR  = 1,  // Bilinear
    FILTER_TRILINEAR = 2 // Trilinear with mipmaps
};

// Texture Wrap Modes
enum IntelTextureWrap {
    WRAP_REPEAT         = 0,  // Repeat texture
    WRAP_CLAMP_TO_EDGE  = 1,  // Clamp to edge
    WRAP_CLAMP_TO_BORDER = 2, // Clamp to border color
    WRAP_MIRROR         = 3   // Mirror repeat
};

// Pipeline Error Codes
enum Pipeline3DError {
    PIPELINE_SUCCESS           = 0,  // Success
    PIPELINE_INVALID_PARAMS    = 1,  // Invalid parameters
    PIPELINE_SHADER_ERROR      = 2,  // Shader compilation/validation error
    PIPELINE_RESOURCE_ERROR    = 3,  // Resource binding error
    PIPELINE_STATE_ERROR       = 4,  // Invalid pipeline state
    PIPELINE_MEMORY_ERROR      = 5,  // Memory allocation failure
    PIPELINE_TIMEOUT           = 6,  // Operation timeout
    PIPELINE_GPU_HANG          = 7,  // GPU hang detected
    PIPELINE_UNSUPPORTED       = 8   // Unsupported operation
};

// Shader Program Structure
struct IntelShaderProgram {
    IntelShaderType type;              // Shader type
    IntelGEMObject* kernelObject;      // Kernel code GEM object
    uint64_t kernelOffset;             // Kernel offset in object
    uint32_t kernelSize;               // Kernel size in bytes
    uint32_t scratchSize;              // Per-thread scratch space
    uint32_t numThreads;               // Number of threads
    uint32_t bindingTableOffset;      // Binding table offset
    uint32_t samplerStateOffset;      // Sampler state offset
    bool compiled;                     // Compilation status
    uint64_t gpuAddress;               // GPU address of kernel
};

// Texture Structure
struct IntelTexture {
    IntelGEMObject* object;            // Texture data GEM object
    uint32_t width;                    // Texture width
    uint32_t height;                   // Texture height
    uint32_t depth;                    // Texture depth (1 for 2D)
    IntelTextureFormat format;         // Pixel format
    uint32_t mipLevels;                // Number of mip levels
    uint32_t arraySize;                // Array size (1 for non-arrays)
    uint64_t gpuAddress;               // GPU address
    uint32_t pitch;                    // Row pitch in bytes
};

// Sampler Structure
struct IntelSampler {
    IntelTextureFilter minFilter;      // Minification filter
    IntelTextureFilter magFilter;      // Magnification filter
    IntelTextureWrap wrapU;            // U coordinate wrap mode
    IntelTextureWrap wrapV;            // V coordinate wrap mode
    IntelTextureWrap wrapW;            // W coordinate wrap mode
    float borderColor[4];              // Border color (RGBA)
    float minLOD;                      // Minimum LOD
    float maxLOD;                      // Maximum LOD
    bool anisotropyEnable;             // Anisotropic filtering
    uint32_t maxAnisotropy;            // Max anisotropy (1-16)
};

// Buffer Structure
struct IntelBuffer {
    IntelGEMObject* object;            // Buffer data GEM object
    uint64_t size;                     // Buffer size in bytes
    uint64_t gpuAddress;               // GPU address
    uint32_t stride;                   // Stride for vertex buffers
    bool isVertexBuffer;               // Vertex buffer flag
    bool isIndexBuffer;                // Index buffer flag
    bool isConstantBuffer;             // Constant buffer flag
};

// Render Target Structure
struct IntelRenderTarget {
    IntelGEMObject* colorObject;       // Color buffer GEM object
    IntelGEMObject* depthObject;       // Depth buffer GEM object
    IntelGEMObject* stencilObject;     // Stencil buffer GEM object (optional)
    uint32_t width;                    // Render target width
    uint32_t height;                   // Render target height
    IntelTextureFormat colorFormat;    // Color format
    IntelTextureFormat depthFormat;    // Depth format
    uint64_t colorAddress;             // Color buffer GPU address
    uint64_t depthAddress;             // Depth buffer GPU address
    uint64_t stencilAddress;           // Stencil buffer GPU address
    uint32_t colorPitch;               // Color buffer pitch
    uint32_t depthPitch;               // Depth buffer pitch
};

// 3D Pipeline State
struct Intel3DState {
    // Shader programs
    IntelShaderProgram* vertexShader;
    IntelShaderProgram* hullShader;
    IntelShaderProgram* domainShader;
    IntelShaderProgram* geometryShader;
    IntelShaderProgram* fragmentShader;
    
    // Render targets
    IntelRenderTarget* renderTarget;
    
    // Vertex input
    IntelBuffer* vertexBuffers[16];
    IntelBuffer* indexBuffer;
    uint32_t numVertexBuffers;
    
    // Textures and samplers
    IntelTexture* textures[32];
    IntelSampler* samplers[16];
    uint32_t numTextures;
    uint32_t numSamplers;
    
    // Constant buffers
    IntelBuffer* constantBuffers[8];
    uint32_t numConstantBuffers;
    
    // Rasterizer state
    IntelPrimitiveTopology topology;
    IntelCullMode cullMode;
    IntelFillMode fillMode;
    bool depthClipEnable;
    bool scissorEnable;
    bool multisampleEnable;
    
    // Depth/stencil state
    bool depthTestEnable;
    bool depthWriteEnable;
    IntelDepthStencilOp depthFunc;
    bool stencilTestEnable;
    uint32_t stencilRef;
    uint32_t stencilMask;
    
    // Blend state
    bool blendEnable;
    IntelBlendOp blendOp;
    IntelBlendFactor srcBlend;
    IntelBlendFactor dstBlend;
    float blendColor[4];
    
    // Viewport and scissor
    float viewport[4];       // x, y, width, height
    uint32_t scissor[4];     // x, y, width, height
};

// 3D Pipeline Statistics
struct Pipeline3DStats {
    uint64_t drawCalls;                // Total draw calls
    uint64_t primitivesSent;           // Primitives sent to GPU
    uint64_t verticesProcessed;        // Vertices processed
    uint64_t fragmentsGenerated;       // Fragments generated
    uint64_t pipelineStalls;           // Pipeline stalls
    uint64_t shaderInvocations;        // Shader invocations
    uint32_t averageDrawTimeUs;        // Average draw time (microseconds)
    uint32_t maxDrawTimeUs;            // Maximum draw time
    uint32_t gpuUtilization;           // GPU utilization percentage
    uint64_t errors;                   // Error count
};

class Intel3DPipeline : public OSObject {
    OSDeclareDefaultStructors(Intel3DPipeline)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    
    // Pipeline Management
    Pipeline3DError createPipeline(IntelPipelineType type);
    Pipeline3DError destroyPipeline();
    Pipeline3DError bindPipeline();
    
    // Shader Management
    IntelShaderProgram* createShaderProgram(IntelShaderType type,
                                           const void* kernelCode,
                                           uint32_t kernelSize);
    void destroyShaderProgram(IntelShaderProgram* program);
    Pipeline3DError compileShader(IntelShaderProgram* program);
    Pipeline3DError bindShader(IntelShaderProgram* program);
    
    // State Management
    Pipeline3DError setPipelineState(const Intel3DState* state);
    Pipeline3DError getPipelineState(Intel3DState* state);
    Pipeline3DError resetPipelineState();
    
    // Resource Management
    IntelTexture* createTexture(uint32_t width, uint32_t height,
                               IntelTextureFormat format, uint32_t mipLevels);
    void destroyTexture(IntelTexture* texture);
    Pipeline3DError bindTexture(IntelTexture* texture, uint32_t slot);
    Pipeline3DError updateTexture(IntelTexture* texture, const void* data,
                                 uint32_t mipLevel);
    
    IntelSampler* createSampler(IntelTextureFilter filter, IntelTextureWrap wrap);
    void destroySampler(IntelSampler* sampler);
    Pipeline3DError bindSampler(IntelSampler* sampler, uint32_t slot);
    
    IntelBuffer* createBuffer(uint64_t size, bool isVertexBuffer,
                             bool isIndexBuffer);
    void destroyBuffer(IntelBuffer* buffer);
    Pipeline3DError bindVertexBuffer(IntelBuffer* buffer, uint32_t slot);
    Pipeline3DError bindIndexBuffer(IntelBuffer* buffer);
    Pipeline3DError bindConstantBuffer(IntelBuffer* buffer, uint32_t slot);
    Pipeline3DError updateBuffer(IntelBuffer* buffer, const void* data,
                                uint64_t size);
    
    IntelRenderTarget* createRenderTarget(uint32_t width, uint32_t height,
                                         IntelTextureFormat colorFormat,
                                         IntelTextureFormat depthFormat);
    void destroyRenderTarget(IntelRenderTarget* target);
    Pipeline3DError bindRenderTarget(IntelRenderTarget* target);
    
    // Drawing Operations
    Pipeline3DError drawPrimitives(IntelPrimitiveTopology topology,
                                  uint32_t vertexStart, uint32_t vertexCount);
    Pipeline3DError drawIndexedPrimitives(IntelPrimitiveTopology topology,
                                         uint32_t indexStart, uint32_t indexCount);
    Pipeline3DError drawInstanced(IntelPrimitiveTopology topology,
                                 uint32_t vertexCount, uint32_t instanceCount);
    
    // Clear Operations
    Pipeline3DError clearRenderTarget(float r, float g, float b, float a);
    Pipeline3DError clearDepthStencil(float depth, uint8_t stencil);
    
    // Synchronization
    Pipeline3DError flush();
    Pipeline3DError waitForIdle(uint32_t timeoutMs);
    bool isIdle();
    
    // Statistics
    void getStatistics(Pipeline3DStats* stats);
    void resetStatistics();
    void printStatistics();
    
    // Hardware Capabilities
    uint32_t getMaxTextureSize();
    uint32_t getMaxRenderTargetSize();
    uint32_t getMaxVertexBuffers();
    bool supportsGeometryShaders();
    bool supportsTessellation();
    bool supportsComputeShaders();
    
private:
    AppleIntelTGLController* controller;
    IntelRingBuffer* renderRing;       // Render engine ring buffer
    IntelContext* pipelineContext;     // 3D pipeline context
    Intel3DState* currentState;        // Current pipeline state
    Pipeline3DStats stats;             // Statistics
    IORecursiveLock* lock;             // Thread safety
    
    bool initialized;
    bool pipelineActive;
    
    // Command Generation
    uint32_t* buildPipelineSelectCommand(uint32_t* cmd, IntelPipelineType type);
    uint32_t* buildVertexShaderState(uint32_t* cmd, IntelShaderProgram* program);
    uint32_t* buildFragmentShaderState(uint32_t* cmd, IntelShaderProgram* program);
    uint32_t* buildViewportState(uint32_t* cmd, const float* viewport);
    uint32_t* buildScissorState(uint32_t* cmd, const uint32_t* scissor);
    uint32_t* buildDepthBufferState(uint32_t* cmd, IntelRenderTarget* target);
    uint32_t* buildColorBufferState(uint32_t* cmd, IntelRenderTarget* target);
    uint32_t* buildDrawCommand(uint32_t* cmd, IntelPrimitiveTopology topology,
                              uint32_t vertexStart, uint32_t vertexCount);
    uint32_t* buildPipeControlCommand(uint32_t* cmd);
    
    // Command Submission
    Pipeline3DError submitPipelineCommand(uint32_t* commands, uint32_t numDwords,
                                         IntelRequest** requestOut);
    Pipeline3DError waitForCompletion(IntelRequest* request, uint32_t timeoutMs);
    
    // Validation
    bool validateShader(IntelShaderProgram* program);
    bool validateTexture(IntelTexture* texture);
    bool validateBuffer(IntelBuffer* buffer);
    bool validateRenderTarget(IntelRenderTarget* target);
    bool validateDrawParams(uint32_t vertexStart, uint32_t vertexCount);
    
    // Helper Functions
    uint32_t getTextureBytesPerPixel(IntelTextureFormat format);
    uint32_t calculateTexturePitch(uint32_t width, IntelTextureFormat format);
    uint32_t getPrimitiveCount(IntelPrimitiveTopology topology, uint32_t vertexCount);
    
    // Statistics
    void recordDrawStart();
    void recordDrawComplete(uint32_t primitiveCount, uint32_t vertexCount);
};

#endif /* Intel3DPipeline_h */
