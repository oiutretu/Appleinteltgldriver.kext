/*
 * IntelMetalShader.h - Metal Shader Compiler Interface
 * Week 43: Rendering Pipeline
 * 
 * Compiles Metal shaders (SPIR-V) to Intel GPU ISA (Gen12+).
 */

#ifndef IntelMetalShader_h
#define IntelMetalShader_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;


// MARK: - Shader Types


typedef enum {
    kMetalShaderTypeVertex   = 0,  // Vertex shader
    kMetalShaderTypeFragment = 1,  // Fragment/Pixel shader
    kMetalShaderTypeKernel   = 2,  // Compute kernel
} MetalShaderType;


// MARK: - Shader Language


typedef enum {
    kMetalShaderLanguageMSL     = 0,  // Metal Shading Language source
    kMetalShaderLanguageSPIRV   = 1,  // SPIR-V bytecode (IR)
    kMetalShaderLanguageIntelISA = 2,  // Intel ISA (native)
} MetalShaderLanguage;


// MARK: - Shader Compilation Options


#define kShaderCompileFlagNone            0x0000
#define kShaderCompileFlagOptimize        0x0001  // Enable optimizations
#define kShaderCompileFlagDebugInfo       0x0002  // Include debug info
#define kShaderCompileFlagFastMath        0x0004  // Fast math operations
#define kShaderCompileFlagInline          0x0008  // Aggressive inlining

struct MetalShaderCompileOptions {
    uint32_t flags;                      // Compilation flags
    uint32_t optimizationLevel;          // 0 (none) - 3 (aggressive)
    bool enableValidation;               // Validate SPIR-V
    bool enableProfiling;                // Insert profiling markers
    const char* entryPoint;              // Entry point name (default: "main")
};


// MARK: - Intel ISA (Gen12+)


// Execution unit (EU) instructions (simplified subset)
#define INTEL_ISA_MOV        0x01  // Move
#define INTEL_ISA_ADD        0x02  // Add
#define INTEL_ISA_MUL        0x03  // Multiply
#define INTEL_ISA_MAD        0x04  // Multiply-add
#define INTEL_ISA_SEL        0x05  // Select
#define INTEL_ISA_CMP        0x06  // Compare
#define INTEL_ISA_SEND       0x07  // Send message
#define INTEL_ISA_NOP        0x00  // No operation
#define INTEL_ISA_JMPI       0x20  // Jump
#define INTEL_ISA_BRC        0x21  // Branch
#define INTEL_ISA_CALL       0x2C  // Call
#define INTEL_ISA_RET        0x2D  // Return
#define INTEL_ISA_MATH       0x30  // Math function

// Register types
#define INTEL_REG_GRF        0     // General register file
#define INTEL_REG_ARF        1     // Architecture register file
#define INTEL_REG_NULL       2     // Null register
#define INTEL_REG_IMM        3     // Immediate

// GRF count (Gen12+)
#define INTEL_GRF_COUNT      128   // 128 GRF registers
#define INTEL_GRF_SIZE       32    // 32 bytes per GRF


// MARK: - Shader Input/Output


struct ShaderInput {
    uint32_t location;               // Input location
    uint32_t binding;                // Binding point
    uint32_t format;                 // Data format
    uint32_t offset;                 // Offset in vertex buffer
    char name[64];                   // Attribute name
};

struct ShaderOutput {
    uint32_t location;               // Output location
    uint32_t format;                 // Data format
    char name[64];                   // Output name
};

struct ShaderResource {
    uint32_t binding;                // Binding point
    uint32_t type;                   // Resource type (buffer/texture)
    uint32_t count;                  // Array count
    char name[64];                   // Resource name
};


// MARK: - Shader Statistics


struct ShaderStatistics {
    uint32_t spirvInstructions;      // SPIR-V instruction count
    uint32_t isaInstructions;        // Intel ISA instruction count
    uint32_t registersUsed;          // GRF registers used
    uint32_t compilationTimeUs;      // Compilation time (uss)
    uint32_t optimizationPasses;     // Optimization pass count
    uint32_t kernelSize;             // Shader kernel size (bytes)
};


// MARK: - IntelMetalShader Class


class IntelMetalShader : public OSObject {
    OSDeclareDefaultStructors(IntelMetalShader)
    
public:

    // MARK: - Factory & Lifecycle

    
    // Create shader from source
    static IntelMetalShader* withSource(IntelIOAccelerator* accel,
                                       MetalShaderType type,
                                       const void* source,
                                       size_t sourceLength,
                                       MetalShaderLanguage language,
                                       const MetalShaderCompileOptions* options);
    
    // Initialize shader
    virtual bool initWithSource(IntelIOAccelerator* accel,
                               MetalShaderType type,
                               const void* source,
                               size_t sourceLength,
                               MetalShaderLanguage language,
                               const MetalShaderCompileOptions* options);
    
    virtual void free() override;
    

    // MARK: - Compilation

    
    // Compile shader
    IOReturn compile();
    
    // Compilation stages
    IOReturn parseSPIRV();
    IOReturn convertToIntelISA();
    IOReturn allocateRegisters();
    IOReturn optimizeShader();
    IOReturn generateKernel();
    

    // MARK: - Shader Information

    
    MetalShaderType getType() const { return shaderType; }
    MetalShaderLanguage getLanguage() const { return sourceLanguage; }
    
    bool isCompiled() const { return compiled; }
    
    const void* getKernel() const;
    size_t getKernelSize() const { return kernelSize; }
    
    // Shader reflection
    uint32_t getInputCount() const { return inputCount; }
    const ShaderInput* getInput(uint32_t index) const;
    
    uint32_t getOutputCount() const { return outputCount; }
    const ShaderOutput* getOutput(uint32_t index) const;
    
    uint32_t getResourceCount() const { return resourceCount; }
    const ShaderResource* getResource(uint32_t index) const;
    
    uint32_t getRegisterCount() const { return stats.registersUsed; }  // GRF registers used
    

    // MARK: - Statistics

    
    void getStatistics(ShaderStatistics* outStats);
    
private:

    // MARK: - Internal Methods

    
    // SPIR-V parsing
    IOReturn parseSPIRVHeader();
    IOReturn parseSPIRVInstructions();
    IOReturn extractShaderInterface();
    
    // ISA generation
    IOReturn generateISAInstruction(uint32_t spirvOp, void* operands);
    IOReturn emitISA(uint32_t opcode, uint32_t dst, uint32_t src0, uint32_t src1);
    
    // Register allocation
    uint32_t allocateGRF();
    void freeGRF(uint32_t reg);
    
    // Optimization
    IOReturn optimizeDeadCode();
    IOReturn optimizeConstantFolding();
    IOReturn optimizeRegisterCoalescing();
    
    // SPIR-V structures
    struct SPIRVHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t generator;
        uint32_t bound;
        uint32_t schema;
    };
    
    struct SPIRVInstruction {
        uint16_t wordCount;
        uint16_t opcode;
        uint32_t resultType;
        uint32_t result;
        // Variable length operands follow
    };
    
    // Register allocation tracking
    bool grfAllocated[INTEL_GRF_COUNT];
    uint32_t grfUsageCount;
    

    // MARK: - Member Variables

    
    // Core references
    IntelIOAccelerator* accelerator;
    
    // Shader source
    IOBufferMemoryDescriptor* sourceMemory;
    size_t sourceLength;
    MetalShaderLanguage sourceLanguage;
    
    // Compilation options
    MetalShaderCompileOptions options;
    
    // Shader properties
    MetalShaderType shaderType;
    char entryPoint[64];
    
    // Compiled kernel
    IOBufferMemoryDescriptor* kernelMemory;
    size_t kernelSize;
    bool compiled;
    
    // Shader interface
    ShaderInput inputs[32];
    uint32_t inputCount;
    
    ShaderOutput outputs[16];
    uint32_t outputCount;
    
    ShaderResource resources[32];
    uint32_t resourceCount;
    
    // Compilation statistics
    ShaderStatistics stats;
    
    // State
    bool initialized;
};

#endif /* IntelMetalShader_h */
