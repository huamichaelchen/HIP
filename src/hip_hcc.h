/*
Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef HIP_HCC_H
#define HIP_HCC_H

#include <hc.hpp>
#include <hsa/hsa.h>
#include "hip_util.h"


#if defined(__HCC__) && (__hcc_workweek__ < 16354)
#error("This version of HIP requires a newer version of HCC.");
#endif

#define USE_DISPATCH_HSA_KERNEL 0
//


//---
// Environment variables:

// Intended to distinguish whether an environment variable should be visible only in debug mode, or in debug+release.
//static const int debug = 0;
extern const int release;

extern int HIP_LAUNCH_BLOCKING;

extern int HIP_PRINT_ENV;
extern int HIP_ATP_MARKER;
//extern int HIP_TRACE_API;
extern int HIP_ATP;
extern int HIP_DB;
extern int HIP_STAGING_SIZE;   /* size of staging buffers, in KB */
extern int HIP_STREAM_SIGNALS;  /* number of signals to allocate at stream creation */
extern int HIP_VISIBLE_DEVICES; /* Contains a comma-separated sequence of GPU identifiers */


//---
// Chicken bits for disabling functionality to work around potential issues:
extern int HIP_DISABLE_HW_KERNEL_DEP;

//---
//Extern tls
extern thread_local hipError_t tls_lastHipError;


//---
//Forward defs:
class ihipStream_t;
class ihipDevice_t;
class ihipCtx_t;

// Color defs for debug messages:
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

extern const char *API_COLOR;
extern const char *API_COLOR_END;


// If set, thread-safety is enforced on all stream functions.
// Stream functions will acquire a mutex before entering critical sections.
#define STREAM_THREAD_SAFE 1


#define CTX_THREAD_SAFE 1


// Compile debug trace mode - this prints debug messages to stderr when env var HIP_DB is set.
// May be set to 0 to remove debug if checks - possible code size and performance difference?
#define COMPILE_HIP_DB  1


// Compile HIP tracing capability.
// 0x1 = print a string at function entry with arguments.
// 0x2 = prints a simple message with function name + return code when function exits.
// 0x3 = print both.
// Must be enabled at runtime with HIP_TRACE_API
#define COMPILE_HIP_TRACE_API 0x3


// Compile code that generates trace markers for CodeXL ATP at HIP function begin/end.
// ATP is standard CodeXL format that includes timestamps for kernels, HSA RT APIs, and HIP APIs.
#ifndef COMPILE_HIP_ATP_MARKER
#define COMPILE_HIP_ATP_MARKER 0
#endif


#define DB_SHOW_TID 0

#if DB_SHOW_TID
#define COMPUTE_TID_STR \
    std::stringstream tid_ss;\
    std::stringstream tid_ss_num;\
    tid_ss_num << std::this_thread::get_id();\
    tid_ss << " tid:" << std::hex << std::stoull(tid_ss_num.str());
#else
#define COMPUTE_TID_STR std::stringstream tid_ss;
#endif


// Compile support for trace markers that are displayed on CodeXL GUI at start/stop of each function boundary.
// TODO - currently we print the trace message at the beginning. if we waited, we could also include return codes, and any values returned
// through ptr-to-args (ie the pointers allocated by hipMalloc).
#if COMPILE_HIP_ATP_MARKER
#include "CXLActivityLogger.h"
#define SCOPED_MARKER(markerName,group,userString) amdtScopedMarker(markerName, group, userString)
#else
// Swallow scoped markers:
#define SCOPED_MARKER(markerName,group,userString)
#endif


#if COMPILE_HIP_ATP_MARKER || (COMPILE_HIP_TRACE_API & 0x1)
#define API_TRACE(...)\
{\
    if (HIP_ATP_MARKER || (COMPILE_HIP_DB && HIP_TRACE_API)) {\
        std::string s = std::string(__func__) + " (" + ToString(__VA_ARGS__) + ')';\
        if (COMPILE_HIP_DB && HIP_TRACE_API) {\
            COMPUTE_TID_STR\
            fprintf (stderr, "%s<<hip-api:%s %s\n%s" , API_COLOR, tid_ss.str().c_str(), s.c_str(), API_COLOR_END);\
        }\
        SCOPED_MARKER(s.c_str(), "HIP", NULL);\
    }\
}
#else
// Swallow API_TRACE
#define API_TRACE(...)
#endif


// Just initialize the HIP runtime, but don't log any trace information.
#define HIP_INIT()\
	std::call_once(hip_initialized, ihipInit);\
    ihipCtxStackUpdate();


// This macro should be called at the beginning of every HIP API.
// It initialies the hip runtime (exactly once), and
// generate trace string that can be output to stderr or to ATP file.
#define HIP_INIT_API(...) \
    HIP_INIT()\
    API_TRACE(__VA_ARGS__);

#define ihipLogStatus(hipStatus) \
    ({\
        hipError_t localHipStatus = hipStatus; /*local copy so hipStatus only evaluated once*/ \
        tls_lastHipError = localHipStatus;\
        \
        if ((COMPILE_HIP_TRACE_API & 0x2) && HIP_TRACE_API) {\
            fprintf(stderr, "  %ship-api: %-30s ret=%2d (%s)>>%s\n", (localHipStatus == 0) ? API_COLOR:KRED, __func__, localHipStatus, ihipErrorString(localHipStatus), API_COLOR_END);\
        }\
        localHipStatus;\
    })




//---
//HIP_DB Debug flags:
#define DB_API    0 /* 0x01 - shortcut to enable HIP_TRACE_API on single switch */
#define DB_SYNC   1 /* 0x02 - trace synchronization pieces */
#define DB_MEM    2 /* 0x04 - trace memory allocation / deallocation */
#define DB_COPY1  3 /* 0x08 - trace memory copy commands. . */
#define DB_SIGNAL 4 /* 0x10 - trace signal pool commands */
#define DB_COPY2  5 /* 0x20 - trace memory copy commands. Detailed. */
#define DB_MAX_BITPOS 5
// When adding a new debug flag, also add to the char name table below.
//

struct DbName {
    const char *_color;
    const char *_shortName;
};

static const DbName dbName [] =
{
    {KGRN, "api"}, // not used,
    {KYEL, "sync"},
    {KCYN, "mem"},
    {KMAG, "copy1"},
    {KRED, "signal"},
    {KNRM, "copy2"},
};

 

#if COMPILE_HIP_DB
#define tprintf(trace_level, ...) {\
    if (HIP_DB & (1<<(trace_level))) {\
        char msgStr[1000];\
        snprintf(msgStr, 2000, __VA_ARGS__);\
        COMPUTE_TID_STR\
        fprintf (stderr, "  %ship-%s%s:%s%s", dbName[trace_level]._color, dbName[trace_level]._shortName, tid_ss.str().c_str(), msgStr, KNRM); \
    }\
}
#else
/* Compile to empty code */
#define tprintf(trace_level, ...)
#endif





class ihipException : public std::exception
{
public:
    ihipException(hipError_t e) : _code(e) {};

    hipError_t _code;
};


#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

const hipStream_t hipStreamNull = 0x0;


// Used to remove lock, for performance or stimulating bugs.
class FakeMutex
{
  public:
    void lock() {  }
    bool try_lock() {return true; }
    void unlock() { }
};


#if STREAM_THREAD_SAFE
typedef std::mutex StreamMutex;
#else
#warning "Stream thread-safe disabled"
typedef FakeMutex StreamMutex;
#endif

// Pair Device and Ctx together, these could also be toggled separately if desired.
#if CTX_THREAD_SAFE
typedef std::mutex CtxMutex;
#else
typedef FakeMutex CtxMutex;
#warning "Device thread-safe disabled"
#endif

//
//---
// Protects access to the member _data with a lock acquired on contruction/destruction.
// T must contain a _mutex field which meets the BasicLockable requirements (lock/unlock)
template<typename T>
class LockedAccessor
{
public:
    LockedAccessor(T &criticalData, bool autoUnlock=true) :
        _criticalData(&criticalData),
        _autoUnlock(autoUnlock)

    {
        tprintf(DB_SYNC, "lock critical data %s.%p\n", typeid(T).name(), _criticalData);
        _criticalData->_mutex.lock();
    };

    ~LockedAccessor()
    {
        if (_autoUnlock) {
        tprintf(DB_SYNC, "auto-unlock critical data %s.%p\n",typeid(T).name(),  _criticalData);
            _criticalData->_mutex.unlock();
        }
    }

    void unlock()
    {
        tprintf(DB_SYNC, "unlock critical data %s.%p\n", typeid(T).name(), _criticalData);
       _criticalData->_mutex.unlock();
    }

    // Syntactic sugar so -> can be used to get the underlying type.
    T *operator->() { return  _criticalData; };

private:
    T            *_criticalData;
    bool          _autoUnlock;
};


template <typename MUTEX_TYPE>
struct LockedBase {

    // Experts-only interface for explicit locking.
    // Most uses should use the lock-accessor.
    void lock() { _mutex.lock(); }
    void unlock() { _mutex.unlock(); }

    MUTEX_TYPE  _mutex;
};


class ihipModule_t{
public:
  hsa_executable_t executable;
  hsa_code_object_t object;
  std::string fileName;
  void *ptr;
  size_t size;
};


class ihipFunction_t{
public:
    ihipFunction_t(const char *name) {
        size_t nameSz = strlen(name);
        char *kernelName = (char*)malloc(nameSz);
        strncpy(kernelName, name, nameSz);
        _kernelName = kernelName;
    };

    ~ihipFunction_t() {
        if (_kernelName) {
            free((void*)_kernelName);
            _kernelName = NULL;
        };
    };
public:
    const char             *_kernelName;
    hsa_executable_symbol_t _kernelSymbol;
    uint64_t _kernel;
};


template <typename MUTEX_TYPE>
class ihipStreamCriticalBase_t : public LockedBase<MUTEX_TYPE>
{
public:
    ihipStreamCriticalBase_t(hc::accelerator_view av) :
        _kernelCnt(0),
        _av(av)
    {
    };

    ~ihipStreamCriticalBase_t() {
    }

    ihipStreamCriticalBase_t<StreamMutex>  * mlock() { LockedBase<MUTEX_TYPE>::lock(); return this;};

public:
    // TODO - remove _kernelCnt mechanism:
    uint32_t                    _kernelCnt;    // Count of inflight kernels in this stream.  Reset at ::wait().
    hc::accelerator_view        _av;
};


// if HIP code needs to acquire locks for both ihipCtx_t and ihipStream_t, it should first acquire the lock
// for the ihipCtx_t and then for the individual streams.  The locks should not be acquired in reverse order
// or deadlock may occur.  In some cases, it may be possible to reduce the range where the locks must be held.
// HIP routines should avoid acquiring and releasing the same lock during the execution of a single HIP API.


typedef ihipStreamCriticalBase_t<StreamMutex> ihipStreamCritical_t;
typedef LockedAccessor<ihipStreamCritical_t> LockedAccessor_StreamCrit_t;



//---
// Internal stream structure.
class ihipStream_t {
public:
    enum ScheduleMode {Auto, Spin, Yield};
    typedef uint64_t SeqNum_t ;

    ihipStream_t(ihipCtx_t *ctx, hc::accelerator_view av, unsigned int flags);
    ~ihipStream_t();

    // kind is hipMemcpyKind
    void locked_copySync (void* dst, const void* src, size_t sizeBytes, unsigned kind, bool resolveOn = true);


    void locked_copyAsync(void* dst, const void* src, size_t sizeBytes, unsigned kind);


    //---
    // Member functions that begin with locked_ are thread-safe accessors - these acquire / release the critical mutex.
    LockedAccessor_StreamCrit_t  lockopen_preKernelCommand();
    void                 lockclose_postKernelCommand(hc::accelerator_view *av);


    void                 locked_wait(bool assertQueueEmpty=false);

    hc::accelerator_view* locked_getAv() { LockedAccessor_StreamCrit_t crit(_criticalData); return &(crit->_av); };

    void                 locked_waitEvent(hipEvent_t event);
    void                 locked_recordEvent(hipEvent_t event);


    //---

    // Use this if we already have the stream critical data mutex:
    void                 wait(LockedAccessor_StreamCrit_t &crit, bool assertQueueEmpty=false);

    void launchModuleKernel(hc::accelerator_view av, hsa_signal_t signal,
                            uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                            uint32_t gridDimX, uint32_t gridDimY, uint32_t gridDimZ,
														uint32_t groupSegmentSize, uint32_t sharedMemBytes, 
														void *kernarg, size_t kernSize, uint64_t kernel);



    //-- Non-racy accessors:
    // These functions access fields set at initialization time and are non-racy (so do not acquire mutex)
    const ihipDevice_t *     getDevice() const;
    ihipCtx_t *              getCtx() const;


public:
    //---
    //Public member vars - these are set at initialization and never change:
    SeqNum_t                    _id;   // monotonic sequence ID
    unsigned                    _flags;


private:


    // The unsigned return is hipMemcpyKind
    unsigned resolveMemcpyDirection(bool srcTracked, bool dstTracked, bool srcInDeviceMem, bool dstInDeviceMem);

    bool canSeePeerMemory(const ihipCtx_t *thisCtx, ihipCtx_t *dstCtx, ihipCtx_t *srcCtx);


private: // Data
    // Critical Data - MUST be accessed through LockedAccessor_StreamCrit_t
    ihipStreamCritical_t        _criticalData;

    ihipCtx_t  *_ctx;  // parent context that owns this stream.

    // Friends:
    friend std::ostream& operator<<(std::ostream& os, const ihipStream_t& s);
    friend hipError_t hipStreamQuery(hipStream_t);

    ScheduleMode                _scheduleMode;
};



//----
// Internal event structure:
enum hipEventStatus_t {
   hipEventStatusUnitialized = 0, // event is unutilized, must be "Created" before use.
   hipEventStatusCreated     = 1,
   hipEventStatusRecording   = 2, // event has been enqueued to record something.
   hipEventStatusRecorded    = 3, // event has been recorded - timestamps are valid.
} ;


// internal hip event structure.
struct ihipEvent_t {
    hipEventStatus_t       _state;

    hipStream_t           _stream;  // Stream where the event is recorded, or NULL if all streams.
    unsigned              _flags;

    hc::completion_future _marker;
    uint64_t              _timestamp;  // store timestamp, may be set on host or by marker.
} ;





//----
// Properties of the HIP device.
// Multiple contexts can point to same device.
class ihipDevice_t
{
public:
    ihipDevice_t(unsigned deviceId, unsigned deviceCnt, hc::accelerator &acc);
    ~ihipDevice_t();

    // Accessors:
    ihipCtx_t *getPrimaryCtx() const { return _primaryCtx; };

public:
    unsigned                _deviceId; // device ID

    hc::accelerator         _acc;
    hsa_agent_t             _hsaAgent;    // hsa agent handle

    //! Number of compute units supported by the device:
    unsigned                _computeUnits;
    hipDeviceProp_t         _props;        // saved device properties.

    // TODO - report this through device properties, base on HCC API call.
    int                     _isLargeBar;

    ihipCtx_t               *_primaryCtx;

private:
    hipError_t initProperties(hipDeviceProp_t* prop);
};
//=============================================================================



//=============================================================================
//class ihipCtxCriticalBase_t
template <typename MUTEX_TYPE>
class ihipCtxCriticalBase_t : LockedBase<MUTEX_TYPE>
{
public:
    ihipCtxCriticalBase_t(unsigned deviceCnt) :
         _peerCnt(0)
    {
        _peerAgents = new hsa_agent_t[deviceCnt];
    };

    ~ihipCtxCriticalBase_t()  {
        if (_peerAgents != nullptr) {
            delete _peerAgents;
            _peerAgents = nullptr;
        }
        _peerCnt = 0;
    }

    // Streams:
    void addStream(ihipStream_t *stream);
    std::list<ihipStream_t*> &streams() { return _streams; };
    const std::list<ihipStream_t*> &const_streams() const { return _streams; };


    // Peer Accessor classes:
    bool isPeer(const ihipCtx_t *peer); // returns True if peer has access to memory physically located on this device.
    bool addPeer(ihipCtx_t *peer);
    bool removePeer(ihipCtx_t *peer);
    void resetPeers(ihipCtx_t *thisDevice);
    void printPeers(FILE *f) const;

    uint32_t peerCnt() const { return _peerCnt; };
    hsa_agent_t *peerAgents() const { return _peerAgents; };



    friend class LockedAccessor<ihipCtxCriticalBase_t>;
private:
    //--- Stream Tracker:
    std::list< ihipStream_t* > _streams;   // streams associated with this device.


    //--- Peer Tracker:
    // These reflect the currently Enabled set of peers for this GPU:
    // Enabled peers have permissions to access the memory physically allocated on this device.
    // Note the peers always contain the self agent for easy interfacing with HSA APIs.
    std::list<ihipCtx_t*>     _peers;     // list of enabled peer devices.
    uint32_t                  _peerCnt;     // number of enabled peers
    hsa_agent_t              *_peerAgents;  // efficient packed array of enabled agents (to use for allocations.)
private:
    void recomputePeerAgents();
};
// Note Mutex type Real/Fake selected based on CtxMutex
typedef ihipCtxCriticalBase_t<CtxMutex> ihipCtxCritical_t;

// This type is used by functions that need access to the critical device structures.
typedef LockedAccessor<ihipCtxCritical_t> LockedAccessor_CtxCrit_t;
//=============================================================================


//=============================================================================
//class ihipCtx_t:
// A HIP CTX (context) points at one of the existing devices and contains the streams,
// peer-to-peer mappings, creation flags.  Multiple contexts can point to the same
// device.
//
class ihipCtx_t
{
public: // Functions:
    ihipCtx_t(ihipDevice_t *device, unsigned deviceCnt, unsigned flags); // note: calls constructor for _criticalData
    ~ihipCtx_t();

    // Functions which read or write the critical data are named locked_.
    // ihipCtx_t does not use recursive locks so the ihip implementation must avoid calling a locked_ function from within a locked_ function.
    // External functions which call several locked_ functions will acquire and release the lock for each function.  if this occurs in
    // performance-sensitive code we may want to refactor by adding non-locked functions and creating a new locked_ member function to call them all.
    void locked_addStream(ihipStream_t *s);
    void locked_removeStream(ihipStream_t *s);
    void locked_reset();
    void locked_waitAllStreams();
    void locked_syncDefaultStream(bool waitOnSelf);

    ihipCtxCritical_t  &criticalData() { return _criticalData; }; // TODO, move private.  Fix P2P.

    const ihipDevice_t *getDevice() const { return _device; };

    // TODO - review uses of getWriteableDevice(), can these be converted to getDevice()
    ihipDevice_t *getWriteableDevice() const { return _device; };

    std::string toString() const; 

public:  // Data
    // The NULL stream is used if no other stream is specified.
    // Default stream has special synchronization properties with other streams.
    ihipStream_t            *_defaultStream;

    // Flags specified when the context is created:
    unsigned                _ctxFlags;

private:
    ihipDevice_t            *_device;


private:  // Critical data, protected with locked access:
    // Members of _protected data MUST be accessed through the LockedAccessor.
    // Search for LockedAccessor<ihipCtxCritical_t> for examples; do not access _criticalData directly.
    ihipCtxCritical_t       _criticalData;

};



//=================================================================================================
// Global variable definition:
extern std::once_flag hip_initialized;
extern unsigned g_deviceCnt;
extern hsa_agent_t g_cpu_agent ;   // the CPU agent.

//=================================================================================================
// Extern functions:
extern void ihipInit();
extern const char *ihipErrorString(hipError_t);
extern ihipCtx_t    *ihipGetTlsDefaultCtx();
extern void          ihipSetTlsDefaultCtx(ihipCtx_t *ctx);
extern hipError_t    ihipSynchronize(void);
extern void          ihipCtxStackUpdate();

extern ihipDevice_t *ihipGetDevice(int);
ihipCtx_t * ihipGetPrimaryCtx(unsigned deviceIndex);

extern void ihipSetTs(hipEvent_t e);


hipStream_t ihipSyncAndResolveStream(hipStream_t);

// Stream printf functions:
inline std::ostream& operator<<(std::ostream& os, const ihipStream_t& s)
{
    os << "stream#";
    os << s.getDevice()->_deviceId;;
    os << '.';
    os << s._id;
    return os;
}

inline std::ostream & operator<<(std::ostream& os, const dim3& s)
{
    os << '{';
    os << s.x;
    os << ',';
    os << s.y;
    os << ',';
    os << s.z;
    os << '}';
    return os;
}

inline std::ostream & operator<<(std::ostream& os, const gl_dim3& s)
{
    os << '{';
    os << s.x;
    os << ',';
    os << s.y;
    os << ',';
    os << s.z;
    os << '}';
    return os;
}

// Stream printf functions:
inline std::ostream& operator<<(std::ostream& os, const hipEvent_t& e)
{
    os << "event:" << std::hex << static_cast<void*> (e);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ihipCtx_t* c)
{
    os << "ctx:" << static_cast<const void*> (c) 
       << " dev:" << c->getDevice()->_deviceId;
    return os;
}


#endif
