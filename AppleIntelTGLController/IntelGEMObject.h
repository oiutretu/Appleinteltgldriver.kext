/*
 * IntelGEMObject.h
 * 
 * Graphics Execution Manager - Buffer Object
 * Ported from Linux gem/i915_gem_object.h
 */

#ifndef IntelGEMObject_h
#define IntelGEMObject_h

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "linux_compat.h"

// Forward declarations
class IntelGEM;
class IntelVMA;

/* Cache levels for buffer objects */
enum intel_cache_level {
    I915_CACHE_NONE = 0,        /* Uncached */
    I915_CACHE_LLC,             /* CPU cache (Last Level Cache) */
    I915_CACHE_L3_LLC,          /* L3 + LLC */
    I915_CACHE_WT,              /* Write-through */
};

/* Memory domains */
enum intel_memory_domain {
    I915_GEM_DOMAIN_CPU    = 0x00000001,  /* CPU cache */
    I915_GEM_DOMAIN_RENDER = 0x00000002,  /* Render engine */
    I915_GEM_DOMAIN_SAMPLER = 0x00000004, /* Texture sampler */
    I915_GEM_DOMAIN_COMMAND = 0x00000008, /* Command buffer */
    I915_GEM_DOMAIN_INSTRUCTION = 0x00000010, /* Instruction buffer */
    I915_GEM_DOMAIN_VERTEX = 0x00000020,  /* Vertex buffer */
    I915_GEM_DOMAIN_GTT    = 0x00000040,  /* GTT (Global Graphics Translation Table) */
    I915_GEM_DOMAIN_WC     = 0x00000080,  /* Write-combined */
};

/* GPU domains that can be flushed */
#define I915_GEM_GPU_DOMAINS \
    (I915_GEM_DOMAIN_RENDER | \
     I915_GEM_DOMAIN_SAMPLER | \
     I915_GEM_DOMAIN_COMMAND | \
     I915_GEM_DOMAIN_INSTRUCTION | \
     I915_GEM_DOMAIN_VERTEX)

/* Buffer object flags */
enum intel_gem_object_flags {
    I915_BO_ALLOC_CONTIGUOUS    = (1 << 0),  /* Physically contiguous */
    I915_BO_ALLOC_VOLATILE      = (1 << 1),  /* Can be discarded */
    I915_BO_ALLOC_USER          = (1 << 2),  /* User-visible object */
    I915_BO_ALLOC_CPU_CLEAR     = (1 << 3),  /* Clear on CPU map */
};

/* Buffer object structure - represents a GPU buffer */
class IntelGEMObject {
    friend class IntelGEM;  // Allow IntelGEM to access private members including next/prev
    
public:
    /* Creation and destruction */
    static IntelGEMObject* create(IntelGEM *gem, u64 size, u32 flags = 0);
    void destroy();
    
    /* Constructor/Destructor - public for create pattern */
    IntelGEMObject();
    ~IntelGEMObject();
    
    /* Reference counting */
    void retain();
    void release();
    
    /* Memory mapping */
    bool mapCPU(void **address);
    void unmapCPU();
    bool mapGTT(u64 *gpu_address);
    void unmapGTT();
    
    /* Domain management */
    bool setDomain(u32 read_domains, u32 write_domain);
    u32 getReadDomains() const { return read_domains; }
    u32 getWriteDomain() const { return write_domain; }
    
    /* Cache control */
    bool setCacheLevel(enum intel_cache_level level);
    enum intel_cache_level getCacheLevel() const { return cache_level; }
    
    /* Synchronization */
    bool waitIdle(u64 timeout_ns);
    bool flush();
    bool finish();
    
    /* Properties */
    u64 getSize() const { return size; }
    u64 getGPUAddress() const { return gpu_address; }
    u64 getGTTAddress() const { return gpu_address; }  // GTT address is same as GPU address
    void setGTTAddress(u64 addr) { gpu_address = addr; }  // Set GTT/GPU address
    void* getCPUAddress() const { return cpu_address; }  // Get CPU virtual address
    IOMemoryDescriptor* getMemoryDescriptor() const { return memory_descriptor; }
    bool isValid() const { return (memory_descriptor != NULL); }
    bool isMapped() const { return cpu_mapped || gtt_mapped; }
    
    /* Statistics */
    u64 getRefCount() const { return ref_count; }
    u64 getMapCount() const { return map_count; }
    
    /* Initialization - public for external use */
    bool init(IntelGEM *gem, u64 size, u32 flags = 0);
    
private:
    /* Cleanup */
    void cleanup();
    
    /* Internal allocation */
    bool allocateMemory();
    void freeMemory();
    bool allocateGPUAddress();
    void freeGPUAddress();
    
    /* Parent GEM manager */
    IntelGEM *gem;
    
    /* Memory descriptor */
    IOMemoryDescriptor *memory_descriptor;  /* macOS memory object */
    IOMemoryMap *memory_map;                /* CPU mapping */
    void *cpu_address;                      /* CPU virtual address */
    
    /* GPU addressing */
    u64 gpu_address;                        /* GPU virtual address */
    u64 size;                               /* Object size in bytes */
    
    /* Cache and domain state */
    enum intel_cache_level cache_level;
    u32 read_domains;                       /* Current read domains */
    u32 write_domain;                       /* Current write domain */
    
    /* Flags and state */
    u32 flags;                              /* Allocation flags */
    bool cpu_mapped;                        /* Mapped for CPU access */
    bool gtt_mapped;                        /* Mapped for GTT access */
    bool is_coherent;                       /* CPU/GPU coherent */
    
    /* Reference counting */
    volatile u64 ref_count;                 /* Reference count */
    IOLock *ref_lock;                       /* Lock for ref_count */
    
    /* Statistics */
    u64 map_count;                          /* Number of maps */
    u64 created_time;                       /* Creation timestamp */
    u64 last_access_time;                   /* Last access timestamp */
    
    /* List linkage (for tracking) */
    IntelGEMObject *next;
    IntelGEMObject *prev;
};

/* Helper functions */
static inline bool i915_gem_object_size_2big(u64 size)
{
    /* Check if size would overflow (macOS IOMemoryDescriptor limit) */
    return (size > (1ULL << 32));  /* 4GB max for now */
}

#endif /* IntelGEMObject_h */
