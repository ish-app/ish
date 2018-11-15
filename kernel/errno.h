#ifndef SYS_ERRNO_H
#define SYS_ERRNO_H

#include <errno.h>

// stolen from asm-generic/errno-base.h

#define _EPERM          -1 /* Operation not permitted */
#define _ENOENT         -2 /* No such file or directory */
#define _ESRCH          -3 /* No such process */
#define _EINTR          -4 /* Interrupted system call */
#define _EIO            -5 /* I/O error */
#define _ENXIO          -6 /* No such device or address */
#define _E2BIG          -7 /* That's what she said */
#define _ENOEXEC        -8 /* Exec format error */
#define _EBADF          -9 /* Bad file number */
#define _ECHILD        -10 /* No child processes */
#define _EAGAIN        -11 /* Try again */
#define _ENOMEM        -12 /* Out of memory */
#define _EACCES        -13 /* Permission denied */
#define _EFAULT        -14 /* Bad address */
#define _ENOTBLK       -15 /* Block device required */
#define _EBUSY         -16 /* Device or resource busy */
#define _EEXIST        -17 /* File exists */
#define _EXDEV         -18 /* Cross-device link */
#define _ENODEV        -19 /* No such device */
#define _ENOTDIR       -20 /* Not a directory */
#define _EISDIR        -21 /* Is a directory */
#define _EINVAL        -22 /* Invalid argument */
#define _ENFILE        -23 /* File table overflow */
#define _EMFILE        -24 /* Too many open files */
#define _ENOTTY        -25 /* Not a typewriter */
#define _ETXTBSY       -26 /* Text file busy */
#define _EFBIG         -27 /* File too large */
#define _ENOSPC        -28 /* No space left on device */
#define _ESPIPE        -29 /* Illegal seek */
#define _EROFS         -30 /* Read-only file system */
#define _EMLINK        -31 /* Too many links */
#define _EPIPE         -32 /* Broken pipe */
#define _EDOM          -33 /* Math argument out of domain of func */
#define _ERANGE        -34 /* Math result not representable */
#define _EDEADLK       -35 /* Resource deadlock would occur */
#define _ENAMETOOLONG  -36 /* File name too long */
#define _ENOLCK        -37 /* No record locks available */
#define _ENOSYS        -38 /* Invalid system call number */
#define _ENOTEMPTY     -39 /* Directory not empty */
#define _ELOOP         -40 /* Too many symbolic links encountered */

#define _EBFONT        -59 /* Bad font file format */
#define _ENOSTR        -60 /* Device not a stream */
#define _ENODATA       -61 /* No data available */
#define _ETIME         -62 /* Timer expired */
#define _ENOSR         -63 /* Out of streams resources */
#define _ENONET        -64 /* Machine is not on the network */
#define _ENOPKG        -65 /* Package not installed */
#define _EREMOTE       -66 /* Object is remote */
#define _ENOLINK       -67 /* Link has been severed */
#define _EADV          -68 /* Advertise error */
#define _ESRMNT        -69 /* Srmount error */
#define _ECOMM         -70 /* Communication error on send */
#define _EPROTO        -71 /* Protocol error */
#define _EMULTIHOP     -72 /* Multihop attempted */
#define _EDOTDOT       -73 /* RFS specific error */
#define _EBADMSG       -74 /* Not a data message */
#define _EOVERFLOW     -75 /* Value too large for defined data type */
#define _ENOTUNIQ      -76 /* Name not unique on network */
#define _EBADFD        -77 /* File descriptor in bad state */
#define _EREMCHG       -78 /* Remote address changed */
#define _ELIBACC       -79 /* Can not access a needed shared library */
#define _ELIBBAD       -80 /* Accessing a corrupted shared library */
#define _ELIBSCN       -81 /* .lib section in a.out corrupted */
#define _ELIBMAX       -82 /* Attempting to link in too many shared libraries */
#define _ELIBEXEC      -83 /* Cannot exec a shared library directly */
#define _EILSEQ        -84 /* Illegal byte sequence */
#define _ERESTART      -85 /* Interrupted system call should be restarted */
#define _ESTRPIPE      -86 /* Streams pipe error */
#define _EUSERS        -87 /* Too many users */
#define _ENOTSOCK      -88 /* Socket operation on non-socket */
#define _EDESTADDRREQ  -89 /* Destination address required */
#define _EMSGSIZE      -90 /* Message too long */
#define _EPROTOTYPE    -91 /* Protocol wrong type for socket */
#define _ENOPROTOOPT   -92 /* Protocol not available */
#define _EPROTONOSUPPORT    93 /* Protocol not supported */
#define _ESOCKTNOSUPPORT    94 /* Socket type not supported */
#define _EOPNOTSUPP    -95 /* Operation not supported on transport endpoint */
#define _ENOTSUP _EOPNOTSUPP
#define _EPFNOSUPPORT  -96 /* Protocol family not supported */
#define _EAFNOSUPPORT  -97 /* Address family not supported by protocol */
#define _EADDRINUSE    -98 /* Address already in use */
#define _EADDRNOTAVAIL -99 /* Cannot assign requested address */
#define _ENETDOWN     -100 /* Network is down */
#define _ENETUNREACH  -101 /* Network is unreachable */
#define _ENETRESET    -102 /* Network dropped connection because of reset */
#define _ECONNABORTED -103 /* Software caused connection abort */
#define _ECONNRESET   -104 /* Connection reset by peer */
#define _ENOBUFS      -105 /* No buffer space available */
#define _EISCONN      -106 /* Transport endpoint is already connected */
#define _ENOTCONN     -107 /* Transport endpoint is not connected */
#define _ESHUTDOWN    -108 /* Cannot send after transport endpoint shutdown */
#define _ETOOMANYREFS -109 /* Too many references: cannot splice */
#define _ETIMEDOUT    -110 /* Connection timed out */
#define _ECONNREFUSED -111 /* Connection refused */
#define _EHOSTDOWN    -112 /* Host is down */
#define _EHOSTUNREACH -113 /* No route to host */
#define _EALREADY     -114 /* Operation already in progress */
#define _EINPROGRESS  -115 /* Operation now in progress */
#define _ESTALE       -116 /* Stale file handle */
#define _EUCLEAN      -117 /* Structure needs cleaning */
#define _ENOTNAM      -118 /* Not a XENIX named type file */
#define _ENAVAIL      -119 /* No XENIX semaphores available */
#define _EISNAM       -120 /* Is a named type file */
#define _EREMOTEIO    -121 /* Remote I/O error */
#define _EDQUOT       -122 /* Quota exceeded */


int err_map(int err);
int errno_map(void);

#endif
