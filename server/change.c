/*
 * Server-side change notification management
 *
 * Copyright (C) 1998 Alexandre Julliard
 * Copyright (C) 2006 Mike McCormack
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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "winternl.h"

/* dnotify support */

#ifdef linux
#ifndef F_NOTIFY
#define F_NOTIFY 1026
#define DN_ACCESS       0x00000001      /* File accessed */
#define DN_MODIFY       0x00000002      /* File modified */
#define DN_CREATE       0x00000004      /* File created */
#define DN_DELETE       0x00000008      /* File removed */
#define DN_RENAME       0x00000010      /* File renamed */
#define DN_ATTRIB       0x00000020      /* File changed attibutes */
#define DN_MULTISHOT    0x80000000      /* Don't remove notifier */
#endif
#endif

/* inotify support */

#if defined(__linux__) && defined(__i386__)

#define SYS_inotify_init	291
#define SYS_inotify_add_watch	292
#define SYS_inotify_rm_watch	293

struct inotify_event {
    int           wd;
    unsigned int  mask;
    unsigned int  cookie;
    unsigned int  len;
    char          name[1];
};

#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400

static inline int inotify_init( void )
{
    int ret;
    __asm__ __volatile__( "int $0x80"
                          : "=a" (ret)
                          : "0" (SYS_inotify_init));
    if (ret<0) { errno = -ret; ret = -1; }
    return ret;
}

static inline int inotify_add_watch( int fd, const char *name, unsigned int mask )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx;\n\t"
                          "movl %2,%%ebx;\n\t"
                          "int $0x80;\n\t"
                          "popl %%ebx"
                          : "=a" (ret) : "0" (SYS_inotify_add_watch),
                            "r" (fd), "c" (name), "d" (mask) );
    if (ret<0) { errno = -ret; ret = -1; }
    return ret;
}

static inline int inotify_remove_watch( int fd, int wd )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx;\n\t"
                          "movl %2,%%ebx;\n\t"
                          "int $0x80;\n\t"
                          "popl %%ebx"
                          : "=a" (ret) : "0" (SYS_inotify_rm_watch),
                            "r" (fd), "c" (wd) );
    if (ret<0) { errno = -ret; ret = -1; }
    return ret;
}

#define USE_INOTIFY

#endif

struct inode;

static void free_inode( struct inode *inode );

static struct fd *inotify_fd;

struct change_record {
    struct list entry;
    int action;
    int len;
    char name[1];
};

struct dir
{
    struct object  obj;      /* object header */
    struct fd     *fd;       /* file descriptor to the directory */
    struct list    entry;    /* entry in global change notifications list */
    struct event  *event;
    unsigned int   filter;   /* notification filter */
    int            notified; /* SIGIO counter */
    int            want_data; /* return change data */
    long           signaled; /* the file changed */
    int            subtree;  /* do we want to watch subdirectories? */
    struct list    change_q; /* change readers */
    struct list    change_records;   /* data for the change */
    struct list    in_entry; /* entry in the inode dirs list */
    struct inode  *inode;    /* inode of the associated directory */
};

static struct fd *dir_get_fd( struct object *obj );
static unsigned int dir_map_access( struct object *obj, unsigned int access );
static void dir_dump( struct object *obj, int verbose );
static void dir_destroy( struct object *obj );
static int dir_signaled( struct object *obj, struct thread *thread );

static const struct object_ops dir_ops =
{
    sizeof(struct dir),       /* size */
    dir_dump,                 /* dump */
    add_queue,                /* add_queue */
    remove_queue,             /* remove_queue */
    dir_signaled,             /* signaled */
    no_satisfied,             /* satisfied */
    no_signal,                /* signal */
    dir_get_fd,               /* get_fd */
    dir_map_access,           /* map_access */
    no_lookup_name,           /* lookup_name */
    fd_close_handle,          /* close_handle */
    dir_destroy               /* destroy */
};

static int dir_get_poll_events( struct fd *fd );
static enum server_fd_type dir_get_info( struct fd *fd, int *flags );
static void dir_cancel_async( struct fd *fd );

static const struct fd_ops dir_fd_ops =
{
    dir_get_poll_events,      /* get_poll_events */
    default_poll_event,       /* poll_event */
    no_flush,                 /* flush */
    dir_get_info,             /* get_file_info */
    default_fd_queue_async,   /* queue_async */
    dir_cancel_async          /* cancel_async */
};

static struct list change_list = LIST_INIT(change_list);

static void dnotify_adjust_changes( struct dir *dir )
{
#if defined(F_SETSIG) && defined(F_NOTIFY)
    int fd = get_unix_fd( dir->fd );
    unsigned int filter = dir->filter;
    unsigned int val;
    if ( 0 > fcntl( fd, F_SETSIG, SIGIO) )
        return;

    val = DN_MULTISHOT;
    if (filter & FILE_NOTIFY_CHANGE_FILE_NAME)
        val |= DN_RENAME | DN_DELETE | DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_DIR_NAME)
        val |= DN_RENAME | DN_DELETE | DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
        val |= DN_ATTRIB;
    if (filter & FILE_NOTIFY_CHANGE_SIZE)
        val |= DN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_WRITE)
        val |= DN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
        val |= DN_ACCESS;
    if (filter & FILE_NOTIFY_CHANGE_CREATION)
        val |= DN_CREATE;
    if (filter & FILE_NOTIFY_CHANGE_SECURITY)
        val |= DN_ATTRIB;
    fcntl( fd, F_NOTIFY, val );
#endif
}

/* insert change in the global list */
static inline void insert_change( struct dir *dir )
{
    sigset_t sigset;

    sigemptyset( &sigset );
    sigaddset( &sigset, SIGIO );
    sigprocmask( SIG_BLOCK, &sigset, NULL );
    list_add_head( &change_list, &dir->entry );
    sigprocmask( SIG_UNBLOCK, &sigset, NULL );
}

/* remove change from the global list */
static inline void remove_change( struct dir *dir )
{
    sigset_t sigset;

    sigemptyset( &sigset );
    sigaddset( &sigset, SIGIO );
    sigprocmask( SIG_BLOCK, &sigset, NULL );
    list_remove( &dir->entry );
    sigprocmask( SIG_UNBLOCK, &sigset, NULL );
}

static void dir_dump( struct object *obj, int verbose )
{
    struct dir *dir = (struct dir *)obj;
    assert( obj->ops == &dir_ops );
    fprintf( stderr, "Dirfile fd=%p event=%p filter=%08x\n",
             dir->fd, dir->event, dir->filter );
}

static int dir_signaled( struct object *obj, struct thread *thread )
{
    struct dir *dir = (struct dir *)obj;
    assert (obj->ops == &dir_ops);
    return (dir->event == NULL) && dir->signaled;
}

/* enter here directly from SIGIO signal handler */
void do_change_notify( int unix_fd )
{
    struct dir *dir;

    /* FIXME: this is O(n) ... probably can be improved */
    LIST_FOR_EACH_ENTRY( dir, &change_list, struct dir, entry )
    {
        if (get_unix_fd( dir->fd ) != unix_fd) continue;
        interlocked_xchg_add( &dir->notified, 1 );
        break;
    }
}

static void dir_signal_changed( struct dir *dir )
{
    if (dir->event)
        set_event( dir->event );
    else
        wake_up( &dir->obj, 0 );
}

/* SIGIO callback, called synchronously with the poll loop */
void sigio_callback(void)
{
    struct dir *dir;

    LIST_FOR_EACH_ENTRY( dir, &change_list, struct dir, entry )
    {
        long count = interlocked_xchg( &dir->notified, 0 );
        if (count)
        {
            dir->signaled += count;
            if (dir->signaled == count)  /* was it 0? */
                dir_signal_changed( dir );
        }
    }
}

static struct fd *dir_get_fd( struct object *obj )
{
    struct dir *dir = (struct dir *)obj;
    assert( obj->ops == &dir_ops );
    return (struct fd *)grab_object( dir->fd );
}

static unsigned int dir_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= FILE_GENERIC_READ;
    if (access & GENERIC_WRITE)   access |= FILE_GENERIC_WRITE;
    if (access & GENERIC_EXECUTE) access |= FILE_GENERIC_EXECUTE;
    if (access & GENERIC_ALL)     access |= FILE_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static struct change_record *get_first_change_record( struct dir *dir )
{
    struct list *ptr = list_head( &dir->change_records );
    if (!ptr) return NULL;
    list_remove( ptr );
    return LIST_ENTRY( ptr, struct change_record, entry );
}

static void dir_destroy( struct object *obj )
{
    struct change_record *record;
    struct dir *dir = (struct dir *)obj;
    assert (obj->ops == &dir_ops);

    if (dir->filter)
        remove_change( dir );

    if (dir->inode)
    {
        list_remove( &dir->in_entry );
        free_inode( dir->inode );
    }

    async_terminate_queue( &dir->change_q, STATUS_CANCELLED );
    while ((record = get_first_change_record( dir ))) free( record );

    if (dir->event)
    {
        set_event( dir->event );
        release_object( dir->event );
    }
    release_object( dir->fd );

    if (inotify_fd && list_empty( &change_list ))
    {
        release_object( inotify_fd );
        inotify_fd = NULL;
    }
}

static struct dir *
get_dir_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct dir *)get_handle_obj( process, handle, access, &dir_ops );
}

static int dir_get_poll_events( struct fd *fd )
{
    return 0;
}

static enum server_fd_type dir_get_info( struct fd *fd, int *flags )
{
    *flags = 0;
    return FD_TYPE_DIR;
}

static void dir_cancel_async( struct fd *fd )
{
    struct dir *dir = (struct dir *) get_fd_user( fd );
    async_terminate_queue( &dir->change_q, STATUS_CANCELLED );
}


#ifdef USE_INOTIFY

#define HASH_SIZE 31

struct inode {
    struct list ch_entry;    /* entry in the children list */
    struct list children;    /* children of this inode */
    struct inode *parent;    /* parent of this inode */
    struct list dirs;        /* directory handles watching this inode */
    struct list ino_entry;   /* entry in the inode hash */
    struct list wd_entry;    /* entry in the watch descriptor hash */
    dev_t dev;               /* device number */
    ino_t ino;               /* device's inode number */
    int wd;                  /* inotify's watch descriptor */
    char *name;              /* basename name of the inode */
};

struct list inode_hash[ HASH_SIZE ];
struct list wd_hash[ HASH_SIZE ];

static int inotify_add_dir( char *path, unsigned int filter );

static struct inode *inode_from_wd( int wd )
{
    struct list *bucket = &wd_hash[ wd % HASH_SIZE ];
    struct inode *inode;

    LIST_FOR_EACH_ENTRY( inode, bucket, struct inode, wd_entry )
        if (inode->wd == wd)
            return inode;

    return NULL;
}

static inline struct list *get_hash_list( dev_t dev, ino_t ino )
{
    return &inode_hash[ (ino ^ dev) % HASH_SIZE ];
}

static struct inode *find_inode( dev_t dev, ino_t ino )
{
    struct list *bucket = get_hash_list( dev, ino );
    struct inode *inode;

    LIST_FOR_EACH_ENTRY( inode, bucket, struct inode, ino_entry )
        if (inode->ino == ino && inode->dev == dev)
             return inode;

    return NULL;
}

static struct inode *create_inode( dev_t dev, ino_t ino )
{
    struct inode *inode;

    inode = malloc( sizeof *inode );
    if (inode)
    {
        list_init( &inode->children );
        list_init( &inode->dirs );
        inode->ino = ino;
        inode->dev = dev;
        inode->wd = -1;
        inode->parent = NULL;
        inode->name = NULL;
        list_add_tail( get_hash_list( dev, ino ), &inode->ino_entry );
    }
    return inode;
}

static struct inode *get_inode( dev_t dev, ino_t ino )
{
    struct inode *inode;

    inode = find_inode( dev, ino );
    if (inode)
        return inode;
    return create_inode( dev, ino );
}

static void inode_set_wd( struct inode *inode, int wd )
{
    if (inode->wd != -1)
        list_remove( &inode->wd_entry );
    inode->wd = wd;
    list_add_tail( &wd_hash[ wd % HASH_SIZE ], &inode->wd_entry );
}

static void inode_set_name( struct inode *inode, const char *name )
{
    free (inode->name);
    inode->name = name ? strdup( name ) : NULL;
}

static void free_inode( struct inode *inode )
{
    int subtree = 0, watches = 0;
    struct dir *dir;

    LIST_FOR_EACH_ENTRY( dir, &inode->dirs, struct dir, in_entry )
    {
        subtree |= dir->subtree;
        watches++;
    }

    if (!subtree && !inode->parent)
    {
        struct inode *tmp, *next;
        LIST_FOR_EACH_ENTRY_SAFE( tmp, next, &inode->children,
                                  struct inode, ch_entry )
        {
            assert( tmp != inode );
            assert( tmp->parent == inode );
            free_inode( tmp );
        }
    }

    if (watches)
        return;

    if (inode->parent)
        list_remove( &inode->ch_entry );

    if (inode->wd != -1)
    {
        inotify_remove_watch( get_unix_fd( inotify_fd ), inode->wd );
        list_remove( &inode->wd_entry );
    }
    list_remove( &inode->ino_entry );

    free( inode->name );
    free( inode );
}

static struct inode *inode_add( struct inode *parent,
                                dev_t dev, ino_t ino, const char *name )
{
    struct inode *inode;
 
    inode = get_inode( dev, ino );
    if (!inode)
        return NULL;
 
    if (!inode->parent)
    {
        list_add_tail( &parent->children, &inode->ch_entry );
        inode->parent = parent;
        assert( inode != parent );
    }
    inode_set_name( inode, name );

    return inode;
}

static struct inode *inode_from_name( struct inode *inode, const char *name )
{
    struct inode *i;

    LIST_FOR_EACH_ENTRY( i, &inode->children, struct inode, ch_entry )
        if (i->name && !strcmp( i->name, name ))
            return i;
    return NULL;
}

static int inotify_get_poll_events( struct fd *fd );
static void inotify_poll_event( struct fd *fd, int event );

static const struct fd_ops inotify_fd_ops =
{
    inotify_get_poll_events,  /* get_poll_events */
    inotify_poll_event,       /* poll_event */
    no_flush,                 /* flush */
    no_get_file_info,         /* get_file_info */
    default_fd_queue_async,   /* queue_async */
    default_fd_cancel_async,  /* cancel_async */
};

static int inotify_get_poll_events( struct fd *fd )
{
    return POLLIN;
}

static void inotify_do_change_notify( struct dir *dir, unsigned int action,
                                      const char *relpath )
{
    struct change_record *record;

    assert( dir->obj.ops == &dir_ops );

    if (dir->want_data)
    {
        size_t len = strlen(relpath);
        record = malloc( offsetof(struct change_record, name[len]) );
        if (!record)
            return;

        record->action = action;
        memcpy( record->name, relpath, len );
        record->len = len;

        list_add_tail( &dir->change_records, &record->entry );
    }

    if (!list_empty( &dir->change_q ))
        async_terminate_head( &dir->change_q, STATUS_ALERTED );
}

static unsigned int filter_from_event( struct inotify_event *ie )
{
    unsigned int filter = 0;

    if (ie->mask & (IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE))
        filter |= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    if (ie->mask & IN_MODIFY)
        filter |= FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    if (ie->mask & IN_ATTRIB)
        filter |= FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY;
    if (ie->mask & IN_ACCESS)
        filter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
    if (ie->mask & IN_CREATE)
        filter |= FILE_NOTIFY_CHANGE_CREATION;

    return filter;
}

/* scan up the parent directories for watches */
static unsigned int filter_from_inode( struct inode *inode, int is_parent )
{
    unsigned int filter = 0;
    struct dir *dir;

    /* combine filters from parents watching subtrees */
    while (inode)
    {
        LIST_FOR_EACH_ENTRY( dir, &inode->dirs, struct dir, in_entry )
            if (dir->subtree || !is_parent)
                filter |= dir->filter;
        is_parent = 1;
        inode = inode->parent;
    }

    return filter;
}

static char *inode_get_path( struct inode *inode, int sz )
{
    struct list *head;
    char *path;
    int len;

    if (!inode)
        return NULL;

    head = list_head( &inode->dirs );
    if (head)
    {
        int unix_fd = get_unix_fd( LIST_ENTRY( head, struct dir, in_entry )->fd );
        path = malloc ( 32 + sz );
        if (path)
            sprintf( path, "/proc/self/fd/%u/", unix_fd );
        return path;
    }

    if (!inode->name)
        return NULL;

    len = strlen( inode->name );
    path = inode_get_path( inode->parent, sz + len + 1 );
    if (!path)
        return NULL;
    
    strcat( path, inode->name );
    strcat( path, "/" );

    return path;
}

static int inode_check_dir( struct inode *parent, const char *name )
{
    char *path;
    unsigned int filter;
    struct inode *inode;
    struct stat st;
    int wd = -1, r = -1;

    path = inode_get_path( parent, strlen(name) );
    if (!path)
        return r;

    strcat( path, name );

    r = stat( path, &st );
    if (r < 0) goto end;

    if (!S_ISDIR(st.st_mode))
    {
        r = 0;
        goto end;
    }

    r = 1;

    filter = filter_from_inode( parent, 1 );
    if (!filter)
        goto end;

    inode = inode_add( parent, st.st_dev, st.st_ino, name );
    if (!inode || inode->wd != -1)
        goto end;

    wd = inotify_add_dir( path, filter );
    if (wd != -1)
        inode_set_wd( inode, wd );
    else
        free_inode( inode );

end:
    free( path );
    return r;
}

static int prepend( char **path, const char *segment )
{
    int extra;
    char *p;

    extra = strlen( segment ) + 1;
    if (*path)
    {
        int len = strlen( *path ) + 1;
        p = realloc( *path, len + extra );
        if (!p) return 0;
        memmove( &p[ extra ], p, len );
        p[ extra - 1 ] = '/';
        memcpy( p, segment, extra - 1 );
    }
    else
    {
        p = malloc( extra );
        if (!p) return 0;
        memcpy( p, segment, extra );
    }

    *path = p;

    return 1;
}

static void inotify_notify_all( struct inotify_event *ie )
{
    unsigned int filter, action;
    struct inode *inode, *i;
    char *path = NULL;
    struct dir *dir;

    inode = inode_from_wd( ie->wd );
    if (!inode)
    {
        fprintf( stderr, "no inode matches %d\n", ie->wd);
        return;
    }

    filter = filter_from_event( ie );
    
    if (ie->mask & IN_CREATE)
    {
        switch (inode_check_dir( inode, ie->name ))
        {
        case 1:
            filter &= ~FILE_NOTIFY_CHANGE_FILE_NAME;
            break;
        case 0:
            filter &= ~FILE_NOTIFY_CHANGE_DIR_NAME;
            break;
        default:
            break;
            /* Maybe the file disappeared before we could check it? */
        }
        action = FILE_ACTION_ADDED;
    }
    else if (ie->mask & IN_DELETE)
        action = FILE_ACTION_REMOVED;
    else
        action = FILE_ACTION_MODIFIED;

    /*
     * Work our way up the inode hierarchy
     *  extending the relative path as we go
     *  and notifying all recursive watches.
     */
    if (!prepend( &path, ie->name ))
        return;

    for (i = inode; i; i = i->parent)
    {
        LIST_FOR_EACH_ENTRY( dir, &i->dirs, struct dir, in_entry )
            if ((filter & dir->filter) && (i==inode || dir->subtree))
                inotify_do_change_notify( dir, action, path );

        if (!i->name || !prepend( &path, i->name ))
            break;
    }

    free( path );

    if (ie->mask & IN_DELETE)
    {
        i = inode_from_name( inode, ie->name );
        if (i)
            free_inode( i );
    }
}

static void inotify_poll_event( struct fd *fd, int event )
{
    int r, ofs, unix_fd;
    char buffer[0x1000];
    struct inotify_event *ie;

    unix_fd = get_unix_fd( fd );
    r = read( unix_fd, buffer, sizeof buffer );
    if (r < 0)
    {
        fprintf(stderr,"inotify_poll_event(): inotify read failed!\n");
        return;
    }

    for( ofs = 0; ofs < r - offsetof(struct inotify_event, name); )
    {
        ie = (struct inotify_event*) &buffer[ofs];
        if (!ie->len)
            break;
        ofs += offsetof( struct inotify_event, name[ie->len] );
        if (ofs > r) break;
        inotify_notify_all( ie );
    }
}

static inline struct fd *create_inotify_fd( void )
{
    int unix_fd;

    unix_fd = inotify_init();
    if (unix_fd<0)
        return NULL;
    return create_anonymous_fd( &inotify_fd_ops, unix_fd, NULL );
}

static int map_flags( unsigned int filter )
{
    unsigned int mask;

    /* always watch these so we can track subdirectories in recursive watches */
    mask = (IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF);

    if (filter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
        mask |= IN_ATTRIB;
    if (filter & FILE_NOTIFY_CHANGE_SIZE)
        mask |= IN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_WRITE)
        mask |= IN_MODIFY;
    if (filter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
        mask |= IN_ACCESS;
    if (filter & FILE_NOTIFY_CHANGE_SECURITY)
        mask |= IN_ATTRIB;

    return mask;
}

static int inotify_add_dir( char *path, unsigned int filter )
{
    int wd = inotify_add_watch( get_unix_fd( inotify_fd ),
                                path, map_flags( filter ) );
    if (wd != -1)
        set_fd_events( inotify_fd, POLLIN );
    return wd;
}

static int init_inotify( void )
{
    int i;

    if (inotify_fd)
        return 1;

    inotify_fd = create_inotify_fd();
    if (!inotify_fd)
        return 0;

    for (i=0; i<HASH_SIZE; i++)
    {
        list_init( &inode_hash[i] );
        list_init( &wd_hash[i] );
    }

    return 1;
}

static int inotify_adjust_changes( struct dir *dir )
{
    unsigned int filter;
    struct inode *inode;
    struct stat st;
    char path[32];
    int wd, unix_fd;

    if (!inotify_fd)
        return 0;

    unix_fd = get_unix_fd( dir->fd );

    inode = dir->inode;
    if (!inode)
    {
        /* check if this fd is already being watched */
        if (-1 == fstat( unix_fd, &st ))
            return 0;

        inode = get_inode( st.st_dev, st.st_ino );
        if (!inode)
            inode = create_inode( st.st_dev, st.st_ino );
        if (!inode)
            return 0;
        list_add_tail( &inode->dirs, &dir->in_entry );
        dir->inode = inode;
    }

    filter = filter_from_inode( inode, 0 );

    sprintf( path, "/proc/self/fd/%u", unix_fd );
    wd = inotify_add_dir( path, filter );
    if (wd == -1) return 0;

    inode_set_wd( inode, wd );

    return 1;
}

static char *get_basename( const char *link )
{
    char *buffer, *name = NULL;
    int r, n = 0x100;

    while (1)
    {
        buffer = malloc( n );
        if (!buffer) return NULL;

        r = readlink( link, buffer, n );
        if (r < 0)
            break;

        if (r < n)
        {
            name = buffer;
            break;
        }
        free( buffer );
        n *= 2;
    }

    if (name)
    {
        while (r > 0 && name[ r - 1 ] == '/' )
            r--;
        name[ r ] = 0;

        name = strrchr( name, '/' );
        if (name)
            name = strdup( &name[1] );
    }

    free( buffer );
    return name;
}

static int dir_add_to_existing_notify( struct dir *dir )
{
    struct inode *inode, *parent;
    unsigned int filter = 0;
    struct stat st, st_new;
    char link[35], *name;
    int wd, unix_fd;

    if (!inotify_fd)
        return 0;

    unix_fd = get_unix_fd( dir->fd );

    /* check if it's in the list of inodes we want to watch */
    if (-1 == fstat( unix_fd, &st_new ))
        return 0;
    inode = find_inode( st_new.st_dev, st_new.st_ino );
    if (inode)
        return 0;

    /* lookup the parent */
    sprintf( link, "/proc/self/fd/%u/..", unix_fd );
    if (-1 == stat( link, &st ))
        return 0;

    /*
     * If there's no parent, stop.  We could keep going adding
     *  ../ to the path until we hit the root of the tree or
     *  find a recursively watched ancestor.
     * Assume it's too expensive to search up the tree for now.
     */
    parent = find_inode( st.st_dev, st.st_ino );
    if (!parent)
        return 0;

    if (parent->wd == -1)
        return 0;

    filter = filter_from_inode( parent, 1 );
    if (!filter)
        return 0;

    sprintf( link, "/proc/self/fd/%u", unix_fd );
    name = get_basename( link );
    if (!name)
        return 0;
    inode = inode_add( parent, st_new.st_dev, st_new.st_ino, name );
    free( name );
    if (!inode)
        return 0;

    /* Couldn't find this inode at the start of the function, must be new */
    assert( inode->wd == -1 );

    wd = inotify_add_dir( link, filter );
    if (wd != -1)
        inode_set_wd( inode, wd );

    return 1;
}

#else

static int init_inotify( void )
{
    return 0;
}

static int inotify_adjust_changes( struct dir *dir )
{
    return 0;
}

static void free_inode( struct inode *inode )
{
    assert( 0 );
}

static int dir_add_to_existing_notify( struct dir *dir )
{
    return 0;
}

#endif  /* USE_INOTIFY */

struct object *create_dir_obj( struct fd *fd )
{
    struct dir *dir;

    dir = alloc_object( &dir_ops );
    if (!dir)
        return NULL;

    list_init( &dir->change_q );
    list_init( &dir->change_records );
    dir->event = NULL;
    dir->filter = 0;
    dir->notified = 0;
    dir->signaled = 0;
    dir->want_data = 0;
    dir->inode = NULL;
    grab_object( fd );
    dir->fd = fd;
    set_fd_user( fd, &dir_fd_ops, &dir->obj );

    dir_add_to_existing_notify( dir );

    return &dir->obj;
}

/* enable change notifications for a directory */
DECL_HANDLER(read_directory_changes)
{
    struct event *event = NULL;
    struct dir *dir;

    if (!req->filter)
    {
        set_error(STATUS_INVALID_PARAMETER);
        return;
    }

    dir = get_dir_obj( current->process, req->handle, 0 );
    if (!dir)
        return;

    /* possibly send changes through an event flag */
    if (req->event)
    {
        event = get_event_obj( current->process, req->event, EVENT_MODIFY_STATE );
        if (!event)
            goto end;
    }

    /* discard the current data, and move onto the next event */
    if (dir->event) release_object( dir->event );
    dir->event = event;

    /* requests don't timeout */
    if ( req->io_apc && !create_async( current, NULL, &dir->change_q,
                        req->io_apc, req->io_user, req->io_sb ))
        return;

    /* assign it once */
    if (!dir->filter)
    {
        init_inotify();
        insert_change( dir );
        dir->filter = req->filter;
        dir->subtree = req->subtree;
        dir->want_data = req->want_data;
    }

    /* remove any notifications */
    if (dir->signaled>0)
        dir->signaled--;

    /* clear the event */
    if (event)
        reset_event( event );

    /* if there's already a change in the queue, send it */
    if (!list_empty( &dir->change_q ) &&
        !list_empty( &dir->change_records ))
        async_terminate_head( &dir->change_q, STATUS_ALERTED );

    /* setup the real notification */
    if (!inotify_adjust_changes( dir ))
        dnotify_adjust_changes( dir );

    set_error(STATUS_PENDING);

end:
    release_object( dir );
}

DECL_HANDLER(read_change)
{
    struct change_record *record;
    struct dir *dir;

    dir = get_dir_obj( current->process, req->handle, 0 );
    if (!dir)
        return;

    if ((record = get_first_change_record( dir )) != NULL)
    {
        reply->action = record->action;
        set_reply_data( record->name, record->len );
        free( record );
    }
    else
        set_error( STATUS_NO_DATA_DETECTED );

    /* now signal it */
    dir->signaled++;
    dir_signal_changed( dir );

    release_object( dir );
}
