/*
 * Copyright 2008 Hans Leidekker for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif
#ifdef HAVE_POLL_H
# include <poll.h>
#endif
#ifdef HAVE_OPENSSL_SSL_H
# include <openssl/ssl.h>
#undef FAR
#undef DSA
#endif

#include "wine/debug.h"
#include "wine/library.h"

#include "windef.h"
#include "winbase.h"
#include "winhttp.h"
#include "wincrypt.h"

#include "winhttp_private.h"

/* to avoid conflicts with the Unix socket headers */
#define USE_WS_PREFIX
#include "winsock2.h"

WINE_DEFAULT_DEBUG_CHANNEL(winhttp);

#ifndef HAVE_GETADDRINFO

/* critical section to protect non-reentrant gethostbyname() */
static CRITICAL_SECTION cs_gethostbyname;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &cs_gethostbyname,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": cs_gethostbyname") }
};
static CRITICAL_SECTION cs_gethostbyname = { &critsect_debug, -1, 0, 0, 0, 0 };

#endif

#ifdef SONAME_LIBSSL

#include <openssl/err.h>

static CRITICAL_SECTION init_ssl_cs;
static CRITICAL_SECTION_DEBUG init_ssl_cs_debug =
{
    0, 0, &init_ssl_cs,
    { &init_ssl_cs_debug.ProcessLocksList,
      &init_ssl_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": init_ssl_cs") }
};
static CRITICAL_SECTION init_ssl_cs = { &init_ssl_cs_debug, -1, 0, 0, 0, 0 };

static void *libssl_handle;
static void *libcrypto_handle;

static SSL_METHOD *method;
static SSL_CTX *ctx;

#define MAKE_FUNCPTR(f) static typeof(f) * p##f

MAKE_FUNCPTR( SSL_library_init );
MAKE_FUNCPTR( SSL_load_error_strings );
MAKE_FUNCPTR( SSLv23_method );
MAKE_FUNCPTR( SSL_CTX_free );
MAKE_FUNCPTR( SSL_CTX_new );
MAKE_FUNCPTR( SSL_new );
MAKE_FUNCPTR( SSL_free );
MAKE_FUNCPTR( SSL_set_fd );
MAKE_FUNCPTR( SSL_connect );
MAKE_FUNCPTR( SSL_shutdown );
MAKE_FUNCPTR( SSL_write );
MAKE_FUNCPTR( SSL_read );
MAKE_FUNCPTR( SSL_get_verify_result );
MAKE_FUNCPTR( SSL_get_peer_certificate );
MAKE_FUNCPTR( SSL_CTX_set_default_verify_paths );

MAKE_FUNCPTR( BIO_new_fp );
MAKE_FUNCPTR( CRYPTO_num_locks );
MAKE_FUNCPTR( CRYPTO_set_id_callback );
MAKE_FUNCPTR( CRYPTO_set_locking_callback );
MAKE_FUNCPTR( ERR_get_error );
MAKE_FUNCPTR( ERR_error_string );
MAKE_FUNCPTR( i2d_X509 );
#undef MAKE_FUNCPTR

static CRITICAL_SECTION *ssl_locks;
static unsigned int num_ssl_locks;

static unsigned long ssl_thread_id(void)
{
    return GetCurrentThreadId();
}

static void ssl_lock_callback(int mode, int type, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        EnterCriticalSection( &ssl_locks[type] );
    else
        LeaveCriticalSection( &ssl_locks[type] );
}

#endif

/* translate a unix error code into a winsock error code */
static int sock_get_error( int err )
{
#if !defined(__MINGW32__) && !defined (_MSC_VER)
    switch (err)
    {
        case EINTR:             return WSAEINTR;
        case EBADF:             return WSAEBADF;
        case EPERM:
        case EACCES:            return WSAEACCES;
        case EFAULT:            return WSAEFAULT;
        case EINVAL:            return WSAEINVAL;
        case EMFILE:            return WSAEMFILE;
        case EWOULDBLOCK:       return WSAEWOULDBLOCK;
        case EINPROGRESS:       return WSAEINPROGRESS;
        case EALREADY:          return WSAEALREADY;
        case ENOTSOCK:          return WSAENOTSOCK;
        case EDESTADDRREQ:      return WSAEDESTADDRREQ;
        case EMSGSIZE:          return WSAEMSGSIZE;
        case EPROTOTYPE:        return WSAEPROTOTYPE;
        case ENOPROTOOPT:       return WSAENOPROTOOPT;
        case EPROTONOSUPPORT:   return WSAEPROTONOSUPPORT;
        case ESOCKTNOSUPPORT:   return WSAESOCKTNOSUPPORT;
        case EOPNOTSUPP:        return WSAEOPNOTSUPP;
        case EPFNOSUPPORT:      return WSAEPFNOSUPPORT;
        case EAFNOSUPPORT:      return WSAEAFNOSUPPORT;
        case EADDRINUSE:        return WSAEADDRINUSE;
        case EADDRNOTAVAIL:     return WSAEADDRNOTAVAIL;
        case ENETDOWN:          return WSAENETDOWN;
        case ENETUNREACH:       return WSAENETUNREACH;
        case ENETRESET:         return WSAENETRESET;
        case ECONNABORTED:      return WSAECONNABORTED;
        case EPIPE:
        case ECONNRESET:        return WSAECONNRESET;
        case ENOBUFS:           return WSAENOBUFS;
        case EISCONN:           return WSAEISCONN;
        case ENOTCONN:          return WSAENOTCONN;
        case ESHUTDOWN:         return WSAESHUTDOWN;
        case ETOOMANYREFS:      return WSAETOOMANYREFS;
        case ETIMEDOUT:         return WSAETIMEDOUT;
        case ECONNREFUSED:      return WSAECONNREFUSED;
        case ELOOP:             return WSAELOOP;
        case ENAMETOOLONG:      return WSAENAMETOOLONG;
        case EHOSTDOWN:         return WSAEHOSTDOWN;
        case EHOSTUNREACH:      return WSAEHOSTUNREACH;
        case ENOTEMPTY:         return WSAENOTEMPTY;
#ifdef EPROCLIM
        case EPROCLIM:          return WSAEPROCLIM;
#endif
#ifdef EUSERS
        case EUSERS:            return WSAEUSERS;
#endif
#ifdef EDQUOT
        case EDQUOT:            return WSAEDQUOT;
#endif
#ifdef ESTALE
        case ESTALE:            return WSAESTALE;
#endif
#ifdef EREMOTE
        case EREMOTE:           return WSAEREMOTE;
#endif
    default: errno = err; perror( "sock_set_error" ); return WSAEFAULT;
    }
#endif
    return err;
}

BOOL netconn_init( netconn_t *conn, BOOL secure )
{
#if defined(SONAME_LIBSSL) && defined(SONAME_LIBCRYPTO)
    int i;
#endif

    conn->socket = -1;
    if (!secure) return TRUE;

#if defined(SONAME_LIBSSL) && defined(SONAME_LIBCRYPTO)
    EnterCriticalSection( &init_ssl_cs );
    if (libssl_handle)
    {
        LeaveCriticalSection( &init_ssl_cs );
        return TRUE;
    }
    if (!(libssl_handle = wine_dlopen( SONAME_LIBSSL, RTLD_NOW, NULL, 0 )))
    {
        ERR("Trying to use SSL but couldn't load %s. Expect trouble.\n", SONAME_LIBSSL);
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
        LeaveCriticalSection( &init_ssl_cs );
        return FALSE;
    }
    if (!(libcrypto_handle = wine_dlopen( SONAME_LIBCRYPTO, RTLD_NOW, NULL, 0 )))
    {
        ERR("Trying to use SSL but couldn't load %s. Expect trouble.\n", SONAME_LIBCRYPTO);
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
        LeaveCriticalSection( &init_ssl_cs );
        return FALSE;
    }
#define LOAD_FUNCPTR(x) \
    if (!(p##x = wine_dlsym( libssl_handle, #x, NULL, 0 ))) \
    { \
        ERR("Failed to load symbol %s\n", #x); \
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR ); \
        LeaveCriticalSection( &init_ssl_cs ); \
        return FALSE; \
    }
    LOAD_FUNCPTR( SSL_library_init );
    LOAD_FUNCPTR( SSL_load_error_strings );
    LOAD_FUNCPTR( SSLv23_method );
    LOAD_FUNCPTR( SSL_CTX_free );
    LOAD_FUNCPTR( SSL_CTX_new );
    LOAD_FUNCPTR( SSL_new );
    LOAD_FUNCPTR( SSL_free );
    LOAD_FUNCPTR( SSL_set_fd );
    LOAD_FUNCPTR( SSL_connect );
    LOAD_FUNCPTR( SSL_shutdown );
    LOAD_FUNCPTR( SSL_write );
    LOAD_FUNCPTR( SSL_read );
    LOAD_FUNCPTR( SSL_get_verify_result );
    LOAD_FUNCPTR( SSL_get_peer_certificate );
    LOAD_FUNCPTR( SSL_CTX_set_default_verify_paths );
#undef LOAD_FUNCPTR

#define LOAD_FUNCPTR(x) \
    if (!(p##x = wine_dlsym( libcrypto_handle, #x, NULL, 0 ))) \
    { \
        ERR("Failed to load symbol %s\n", #x); \
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR ); \
        LeaveCriticalSection( &init_ssl_cs ); \
        return FALSE; \
    }
    LOAD_FUNCPTR( BIO_new_fp );
    LOAD_FUNCPTR( CRYPTO_num_locks );
    LOAD_FUNCPTR( CRYPTO_set_id_callback );
    LOAD_FUNCPTR( CRYPTO_set_locking_callback );
    LOAD_FUNCPTR( ERR_get_error );
    LOAD_FUNCPTR( ERR_error_string );
    LOAD_FUNCPTR( i2d_X509 );
#undef LOAD_FUNCPTR

    pSSL_library_init();
    pSSL_load_error_strings();
    pBIO_new_fp( stderr, BIO_NOCLOSE );

    method = pSSLv23_method();
    ctx = pSSL_CTX_new( method );
    if (!pSSL_CTX_set_default_verify_paths( ctx ))
    {
        ERR("SSL_CTX_set_default_verify_paths failed: %s\n", pERR_error_string( pERR_get_error(), 0 ));
        set_last_error( ERROR_OUTOFMEMORY );
        LeaveCriticalSection( &init_ssl_cs );
        return FALSE;
    }

    pCRYPTO_set_id_callback(ssl_thread_id);
    num_ssl_locks = pCRYPTO_num_locks();
    ssl_locks = HeapAlloc(GetProcessHeap(), 0, num_ssl_locks * sizeof(CRITICAL_SECTION));
    if (!ssl_locks)
    {
        set_last_error( ERROR_OUTOFMEMORY );
        LeaveCriticalSection( &init_ssl_cs );
        return FALSE;
    }
    for (i = 0; i < num_ssl_locks; i++) InitializeCriticalSection( &ssl_locks[i] );
    pCRYPTO_set_locking_callback(ssl_lock_callback);

    LeaveCriticalSection( &init_ssl_cs );
#else
    WARN("SSL support not compiled in.\n");
    set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
    return FALSE;
#endif
    return TRUE;
}

void netconn_unload( void )
{
#if defined(SONAME_LIBSSL) && defined(SONAME_LIBCRYPTO)
    if (libcrypto_handle)
    {
        wine_dlclose( libcrypto_handle, NULL, 0 );
    }
    if (libssl_handle)
    {
        if (ctx)
            pSSL_CTX_free( ctx );
        wine_dlclose( libssl_handle, NULL, 0 );
    }
    if (ssl_locks)
    {
        int i;
        for (i = 0; i < num_ssl_locks; i++) DeleteCriticalSection( &ssl_locks[i] );
        HeapFree( GetProcessHeap(), 0, ssl_locks );
    }
#endif
}

BOOL netconn_connected( netconn_t *conn )
{
    return (conn->socket != -1);
}

BOOL netconn_create( netconn_t *conn, int domain, int type, int protocol )
{
    if ((conn->socket = socket( domain, type, protocol )) == -1)
    {
        WARN("unable to create socket (%s)\n", strerror(errno));
        set_last_error( sock_get_error( errno ) );
        return FALSE;
    }
    return TRUE;
}

BOOL netconn_close( netconn_t *conn )
{
    int res;

#ifdef SONAME_LIBSSL
    if (conn->secure)
    {
        heap_free( conn->peek_msg_mem );
        conn->peek_msg_mem = NULL;
        conn->peek_msg = NULL;
        conn->peek_len = 0;

        pSSL_shutdown( conn->ssl_conn );
        pSSL_free( conn->ssl_conn );

        conn->ssl_conn = NULL;
        conn->secure = FALSE;
    }
#endif
    res = closesocket( conn->socket );
    conn->socket = -1;
    if (res == -1)
    {
        set_last_error( sock_get_error( errno ) );
        return FALSE;
    }
    return TRUE;
}

BOOL netconn_connect( netconn_t *conn, const struct sockaddr *sockaddr, unsigned int addr_len, int timeout )
{
    BOOL ret = FALSE;
    int res = 0, state;

    if (timeout > 0)
    {
        state = 1;
        ioctlsocket( conn->socket, FIONBIO, &state );
    }
    if (connect( conn->socket, sockaddr, addr_len ) < 0)
    {
        res = sock_get_error( errno );
        if (res == WSAEWOULDBLOCK || res == WSAEINPROGRESS)
        {
            struct pollfd pfd;

            pfd.fd = conn->socket;
            pfd.events = POLLOUT;
            if (poll( &pfd, 1, timeout ) > 0)
                ret = TRUE;
            else
                res = sock_get_error( errno );
        }
    }
    else
        ret = TRUE;
    if (timeout > 0)
    {
        state = 0;
        ioctlsocket( conn->socket, FIONBIO, &state );
    }
    if (!ret)
    {
        WARN("unable to connect to host (%d)\n", res);
        set_last_error( res );
    }
    return ret;
}

BOOL netconn_secure_connect( netconn_t *conn )
{
#ifdef SONAME_LIBSSL
    X509 *cert;
    long res;

    if (!(conn->ssl_conn = pSSL_new( ctx )))
    {
        ERR("SSL_new failed: %s\n", pERR_error_string( pERR_get_error(), 0 ));
        set_last_error( ERROR_OUTOFMEMORY );
        goto fail;
    }
    if (!pSSL_set_fd( conn->ssl_conn, conn->socket ))
    {
        ERR("SSL_set_fd failed: %s\n", pERR_error_string( pERR_get_error(), 0 ));
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
        goto fail;
    }
    if (pSSL_connect( conn->ssl_conn ) <= 0)
    {
        ERR("SSL_connect failed: %s\n", pERR_error_string( pERR_get_error(), 0 ));
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
        goto fail;
    }
    if (!(cert = pSSL_get_peer_certificate( conn->ssl_conn )))
    {
        ERR("No certificate for server: %s\n", pERR_error_string( pERR_get_error(), 0 ));
        set_last_error( ERROR_WINHTTP_SECURE_CHANNEL_ERROR );
        goto fail;
    }
    if ((res = pSSL_get_verify_result( conn->ssl_conn )) != X509_V_OK)
    {
        /* FIXME: we should set an error and return, but we only print an error at the moment */
        ERR("couldn't verify server certificate (%ld)\n", res);
    }
    TRACE("established SSL connection\n");
    conn->secure = TRUE;
    return TRUE;

fail:
    if (conn->ssl_conn)
    {
        pSSL_shutdown( conn->ssl_conn );
        pSSL_free( conn->ssl_conn );
        conn->ssl_conn = NULL;
    }
#endif
    return FALSE;
}

BOOL netconn_send( netconn_t *conn, const void *msg, size_t len, int flags, int *sent )
{
    if (!netconn_connected( conn )) return FALSE;
    if (conn->secure)
    {
#ifdef SONAME_LIBSSL
        if (flags) FIXME("SSL_write doesn't support any flags (%08x)\n", flags);
        *sent = pSSL_write( conn->ssl_conn, msg, len );
        if (*sent < 1 && len) return FALSE;
        return TRUE;
#else
        return FALSE;
#endif
    }
    if ((*sent = send( conn->socket, msg, len, flags )) == -1)
    {
        set_last_error( sock_get_error( errno ) );
        return FALSE;
    }
    return TRUE;
}

BOOL netconn_recv( netconn_t *conn, void *buf, size_t len, int flags, int *recvd )
{
    *recvd = 0;
    if (!netconn_connected( conn )) return FALSE;
    if (!len) return TRUE;

    if (conn->secure)
    {
#ifdef SONAME_LIBSSL
        if (flags & ~(MSG_PEEK | MSG_WAITALL))
            FIXME("SSL_read does not support the following flags: %08x\n", flags);

        /* this ugly hack is all for MSG_PEEK */
        if (flags & MSG_PEEK && !conn->peek_msg)
        {
            if (!(conn->peek_msg = conn->peek_msg_mem = heap_alloc( len + 1 ))) return FALSE;
        }
        else if (flags & MSG_PEEK && conn->peek_msg)
        {
            if (len < conn->peek_len) FIXME("buffer isn't big enough, should we wrap?\n");
            *recvd = min( len, conn->peek_len );
            memcpy( buf, conn->peek_msg, *recvd );
            return TRUE;
        }
        else if (conn->peek_msg)
        {
            *recvd = min( len, conn->peek_len );
            memcpy( buf, conn->peek_msg, *recvd );
            conn->peek_len -= *recvd;
            conn->peek_msg += *recvd;

            if (conn->peek_len == 0)
            {
                heap_free( conn->peek_msg_mem );
                conn->peek_msg_mem = NULL;
                conn->peek_msg = NULL;
            }
            /* check if we have enough data from the peek buffer */
            if (!(flags & MSG_WAITALL) || (*recvd == len)) return TRUE;
        }
        *recvd += pSSL_read( conn->ssl_conn, (char *)buf + *recvd, len - *recvd );
        if (flags & MSG_PEEK) /* must copy into buffer */
        {
            conn->peek_len = *recvd;
            if (!*recvd)
            {
                heap_free( conn->peek_msg_mem );
                conn->peek_msg_mem = NULL;
                conn->peek_msg = NULL;
            }
            else memcpy( conn->peek_msg, buf, *recvd );
        }
        if (*recvd < 1 && len) return FALSE;
        return TRUE;
#else
        return FALSE;
#endif
    }
    if ((*recvd = recv( conn->socket, buf, len, flags )) == -1)
    {
        set_last_error( sock_get_error( errno ) );
        return FALSE;
    }
    return TRUE;
}

BOOL netconn_query_data_available( netconn_t *conn, DWORD *available )
{
#ifdef FIONREAD
    int ret, unread;
#endif
    *available = 0;
    if (!netconn_connected( conn )) return FALSE;

    if (conn->secure)
    {
#ifdef SONAME_LIBSSL
        if (conn->peek_msg) *available = conn->peek_len;
#endif
        return TRUE;
    }
#ifdef FIONREAD
    if (!(ret = ioctlsocket( conn->socket, FIONREAD, &unread ))) *available = unread;
#endif
    return TRUE;
}

BOOL netconn_get_next_line( netconn_t *conn, char *buffer, DWORD *buflen )
{
    struct pollfd pfd;
    BOOL ret = FALSE;
    DWORD recvd = 0;

    if (!netconn_connected( conn )) return FALSE;

    if (conn->secure)
    {
#ifdef SONAME_LIBSSL
        while (recvd < *buflen)
        {
            int dummy;
            if (!netconn_recv( conn, &buffer[recvd], 1, 0, &dummy ))
            {
                set_last_error( ERROR_CONNECTION_ABORTED );
                break;
            }
            if (buffer[recvd] == '\n')
            {
                ret = TRUE;
                break;
            }
            if (buffer[recvd] != '\r') recvd++;
        }
        if (ret)
        {
            buffer[recvd++] = 0;
            *buflen = recvd;
            TRACE("received line %s\n", debugstr_a(buffer));
        }
        return ret;
#else
        return FALSE;
#endif
    }

    pfd.fd = conn->socket;
    pfd.events = POLLIN;
    while (recvd < *buflen)
    {
        int timeout, res;
        struct timeval tv;
        socklen_t len = sizeof(tv);

        if ((res = getsockopt( conn->socket, SOL_SOCKET, SO_RCVTIMEO, (void*)&tv, &len ) != -1))
            timeout = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        else
            timeout = -1;
        if (poll( &pfd, 1, timeout ) > 0)
        {
            if ((res = recv( conn->socket, &buffer[recvd], 1, 0 )) <= 0)
            {
                if (res == -1) set_last_error( sock_get_error( errno ) );
                break;
            }
            if (buffer[recvd] == '\n')
            {
                ret = TRUE;
                break;
            }
            if (buffer[recvd] != '\r') recvd++;
        }
        else
        {
            set_last_error( ERROR_WINHTTP_TIMEOUT );
            break;
        }
    }
    if (ret)
    {
        buffer[recvd++] = 0;
        *buflen = recvd;
        TRACE("received line %s\n", debugstr_a(buffer));
    }
    return ret;
}

DWORD netconn_set_timeout( netconn_t *netconn, BOOL send, int value )
{
    int res;
    struct timeval tv;

    /* value is in milliseconds, convert to struct timeval */
    tv.tv_sec = value / 1000;
    tv.tv_usec = (value % 1000) * 1000;

    if ((res = setsockopt( netconn->socket, SOL_SOCKET, send ? SO_SNDTIMEO : SO_RCVTIMEO, (void*)&tv, sizeof(tv) ) == -1))
    {
        WARN("setsockopt failed (%s)\n", strerror( errno ));
        return sock_get_error( errno );
    }
    return ERROR_SUCCESS;
}

BOOL netconn_resolve( WCHAR *hostnameW, INTERNET_PORT port, struct sockaddr *sa, socklen_t *sa_len )
{
    char *hostname;
#ifdef HAVE_GETADDRINFO
    struct addrinfo *res, hints;
    int ret;
#else
    struct hostent *he;
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
#endif

    if (!(hostname = strdupWA( hostnameW ))) return FALSE;

#ifdef HAVE_GETADDRINFO
    memset( &hints, 0, sizeof(struct addrinfo) );
    /* Prefer IPv4 to IPv6 addresses, since some web servers do not listen on
     * their IPv6 addresses even though they have IPv6 addresses in the DNS.
     */
    hints.ai_family = AF_INET;

    ret = getaddrinfo( hostname, NULL, &hints, &res );
    if (ret != 0)
    {
        TRACE("failed to get IPv4 address of %s (%s), retrying with IPv6\n", debugstr_w(hostnameW), gai_strerror(ret));
        hints.ai_family = AF_INET6;
        ret = getaddrinfo( hostname, NULL, &hints, &res );
        if (ret != 0)
        {
            TRACE("failed to get address of %s (%s)\n", debugstr_w(hostnameW), gai_strerror(ret));
            heap_free( hostname );
            return FALSE;
        }
    }
    heap_free( hostname );
    if (*sa_len < res->ai_addrlen)
    {
        WARN("address too small\n");
        freeaddrinfo( res );
        return FALSE;
    }
    *sa_len = res->ai_addrlen;
    memcpy( sa, res->ai_addr, res->ai_addrlen );
    /* Copy port */
    switch (res->ai_family)
    {
    case AF_INET:
        ((struct sockaddr_in *)sa)->sin_port = htons( port );
        break;
    case AF_INET6:
        ((struct sockaddr_in6 *)sa)->sin6_port = htons( port );
        break;
    }

    freeaddrinfo( res );
    return TRUE;
#else
    EnterCriticalSection( &cs_gethostbyname );

    he = gethostbyname( hostname );
    heap_free( hostname );
    if (!he)
    {
        TRACE("failed to get address of %s (%d)\n", debugstr_w(hostnameW), h_errno);
        LeaveCriticalSection( &cs_gethostbyname );
        return FALSE;
    }
    if (*sa_len < sizeof(struct sockaddr_in))
    {
        WARN("address too small\n");
        LeaveCriticalSection( &cs_gethostbyname );
        return FALSE;
    }
    *sa_len = sizeof(struct sockaddr_in);
    memset( sa, 0, sizeof(struct sockaddr_in) );
    memcpy( &sin->sin_addr, he->h_addr, he->h_length );
    sin->sin_family = he->h_addrtype;
    sin->sin_port = htons( port );

    LeaveCriticalSection( &cs_gethostbyname );
    return TRUE;
#endif
}

const void *netconn_get_certificate( netconn_t *conn )
{
#ifdef SONAME_LIBSSL
    X509 *cert;
    unsigned char *buffer, *p;
    int len;
    BOOL malloc = FALSE;
    const CERT_CONTEXT *ret;

    if (!conn->secure) return NULL;

    if (!(cert = pSSL_get_peer_certificate( conn->ssl_conn ))) return NULL;
    p = NULL;
    if ((len = pi2d_X509( cert, &p )) < 0) return NULL;
    /*
     * SSL 0.9.7 and above malloc the buffer if it is null.
     * however earlier version do not and so we would need to alloc the buffer.
     *
     * see the i2d_X509 man page for more details.
     */
    if (!p)
    {
        if (!(buffer = heap_alloc( len ))) return NULL;
        p = buffer;
        len = pi2d_X509( cert, &p );
    }
    else
    {
        buffer = p;
        malloc = TRUE;
    }

    ret = CertCreateCertificateContext( X509_ASN_ENCODING, buffer, len );

    if (malloc) free( buffer );
    else heap_free( buffer );

    return ret;
#else
    return NULL;
#endif
}
