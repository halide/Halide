#ifndef HOST_SHIM_H
#define HOST_SHIM_H

#define FASTRPC_THREAD_PARAMS (1)
#define CDSP_DOMAIN_ID 3

// Used with FASTRPC_THREAD_PARAMS req ID
struct remote_rpc_thread_params {
    int domain;      // Remote subsystem domain ID, pass -1 to set params for all domains
    int prio;        // user thread priority (1 to 255), pass -1 to use default
    int stack_size;  // user thread stack size, pass -1 to use default
};

#ifndef __QAIC_REMOTE
#define __QAIC_REMOTE(ff) ff
#endif  //__QAIC_REMOTE

#ifndef __QAIC_REMOTE_EXPORT
#ifdef _WIN32
#define __QAIC_REMOTE_EXPORT __declspec(dllexport)
#else  //_WIN32
#define __QAIC_REMOTE_EXPORT
#endif  //_WIN32
#endif  //__QAIC_REMOTE_EXPORT

#ifndef __QAIC_REMOTE_ATTRIBUTE
#define __QAIC_REMOTE_ATTRIBUTE
#endif

/* Set remote session parameters
 *
 * @param req, request ID
 * @param data, address of structure with parameters
 * @param datalen, length of data
 * @retval, 0 on success
 */
extern "C" __QAIC_REMOTE_EXPORT int __QAIC_REMOTE(remote_session_control)(uint32_t req, void *data, uint32_t datalen) __QAIC_REMOTE_ATTRIBUTE;

#endif  // HOST_SHIM_H
