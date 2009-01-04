/*
 * Dynamic devices support
 *
 * Copyright 2006 Alexandre Julliard
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
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

#include "mountmgr.h"
#include "winreg.h"
#include "winuser.h"
#include "dbt.h"

#include "wine/library.h"
#include "wine/list.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mountmgr);

#define MAX_DOS_DRIVES 26

static const WCHAR drive_types[][8] =
{
    { 0 },                           /* DEVICE_UNKNOWN */
    { 0 },                           /* DEVICE_HARDDISK */
    {'h','d',0},                     /* DEVICE_HARDDISK_VOL */
    {'f','l','o','p','p','y',0},     /* DEVICE_FLOPPY */
    {'c','d','r','o','m',0},         /* DEVICE_CDROM */
    {'n','e','t','w','o','r','k',0}, /* DEVICE_NETWORK */
    {'r','a','m','d','i','s','k',0}  /* DEVICE_RAMDISK */
};

static const WCHAR drives_keyW[] = {'S','o','f','t','w','a','r','e','\\',
                                    'W','i','n','e','\\','D','r','i','v','e','s',0};

struct dos_drive
{
    struct list           entry;       /* entry in drives list */
    char                 *udi;         /* unique identifier for dynamic drives */
    int                   drive;       /* drive letter (0 = A: etc.) */
    enum device_type      type;        /* drive type */
    DEVICE_OBJECT        *device;      /* disk device allocated for this drive */
    UNICODE_STRING        name;        /* device name */
    UNICODE_STRING        symlink;     /* device symlink if any */
    STORAGE_DEVICE_NUMBER devnum;      /* device number info */
    struct mount_point   *dosdev;      /* DosDevices mount point */
    struct mount_point   *volume;      /* Volume{xxx} mount point */
    char                 *unix_device; /* unix device path */
    char                 *unix_mount;  /* unix mount point path */
};

static struct list drives_list = LIST_INIT(drives_list);

static DRIVER_OBJECT *harddisk_driver;

static char *get_dosdevices_path( char **drive )
{
    const char *config_dir = wine_get_config_dir();
    size_t len = strlen(config_dir) + sizeof("/dosdevices/a::");
    char *path = HeapAlloc( GetProcessHeap(), 0, len );
    if (path)
    {
        strcpy( path, config_dir );
        strcat( path, "/dosdevices/a::" );
        *drive = path + len - 4;
    }
    return path;
}

static char *strdupA( const char *str )
{
    char *ret;

    if (!str) return NULL;
    if ((ret = RtlAllocateHeap( GetProcessHeap(), 0, strlen(str) + 1 ))) strcpy( ret, str );
    return ret;
}

/* read a Unix symlink; returned buffer must be freed by caller */
static char *read_symlink( const char *path )
{
    char *buffer;
    int ret, size = 128;

    for (;;)
    {
        if (!(buffer = RtlAllocateHeap( GetProcessHeap(), 0, size )))
        {
            SetLastError( ERROR_NOT_ENOUGH_MEMORY );
            return 0;
        }
        ret = readlink( path, buffer, size );
        if (ret == -1)
        {
            RtlFreeHeap( GetProcessHeap(), 0, buffer );
            return 0;
        }
        if (ret != size)
        {
            buffer[ret] = 0;
            return buffer;
        }
        RtlFreeHeap( GetProcessHeap(), 0, buffer );
        size *= 2;
    }
}

/* send notification about a change to a given drive */
static void send_notify( int drive, int code )
{
    DWORD_PTR result;
    DEV_BROADCAST_VOLUME info;

    info.dbcv_size       = sizeof(info);
    info.dbcv_devicetype = DBT_DEVTYP_VOLUME;
    info.dbcv_reserved   = 0;
    info.dbcv_unitmask   = 1 << drive;
    info.dbcv_flags      = DBTF_MEDIA;
    result = BroadcastSystemMessageW( BSF_FORCEIFHUNG|BSF_QUERY, NULL,
                                      WM_DEVICECHANGE, code, (LPARAM)&info );
}

/* create the disk device for a given drive */
static NTSTATUS create_disk_device( const char *udi, enum device_type type, struct dos_drive **drive_ret )
{
    static const WCHAR harddiskvolW[] = {'\\','D','e','v','i','c','e',
                                         '\\','H','a','r','d','d','i','s','k','V','o','l','u','m','e','%','u',0};
    static const WCHAR harddiskW[] = {'\\','D','e','v','i','c','e','\\','H','a','r','d','d','i','s','k','%','u',0};
    static const WCHAR cdromW[] = {'\\','D','e','v','i','c','e','\\','C','d','R','o','m','%','u',0};
    static const WCHAR floppyW[] = {'\\','D','e','v','i','c','e','\\','F','l','o','p','p','y','%','u',0};
    static const WCHAR ramdiskW[] = {'\\','D','e','v','i','c','e','\\','R','a','m','d','i','s','k','%','u',0};
    static const WCHAR physdriveW[] = {'\\','?','?','\\','P','h','y','s','i','c','a','l','D','r','i','v','e','%','u',0};

    UINT i, first = 0;
    NTSTATUS status = 0;
    const WCHAR *format = NULL;
    UNICODE_STRING name;
    DEVICE_OBJECT *dev_obj;
    struct dos_drive *drive;

    switch(type)
    {
    case DEVICE_UNKNOWN:
    case DEVICE_HARDDISK:
    case DEVICE_NETWORK:  /* FIXME */
        format = harddiskW;
        break;
    case DEVICE_HARDDISK_VOL:
        format = harddiskvolW;
        first = 1;  /* harddisk volumes start counting from 1 */
        break;
    case DEVICE_FLOPPY:
        format = floppyW;
        break;
    case DEVICE_CDROM:
        format = cdromW;
        break;
    case DEVICE_RAMDISK:
        format = ramdiskW;
        break;
    }

    name.MaximumLength = (strlenW(format) + 10) * sizeof(WCHAR);
    name.Buffer = RtlAllocateHeap( GetProcessHeap(), 0, name.MaximumLength );
    for (i = first; i < 32; i++)
    {
        sprintfW( name.Buffer, format, i );
        name.Length = strlenW(name.Buffer) * sizeof(WCHAR);
        status = IoCreateDevice( harddisk_driver, sizeof(*drive), &name, 0, 0, FALSE, &dev_obj );
        if (status != STATUS_OBJECT_NAME_COLLISION) break;
    }
    if (!status)
    {
        drive = dev_obj->DeviceExtension;
        drive->drive  = -1;
        drive->device = dev_obj;
        drive->name   = name;
        drive->type   = type;
        drive->dosdev = NULL;
        drive->volume = NULL;
        drive->unix_device = NULL;
        drive->unix_mount  = NULL;
        drive->symlink.Buffer = NULL;
        if (udi)
        {
            if (!(drive->udi = HeapAlloc( GetProcessHeap(), 0, strlen(udi)+1 )))
            {
                RtlFreeUnicodeString( &name );
                IoDeleteDevice( drive->device );
                return STATUS_NO_MEMORY;
            }
            strcpy( drive->udi, udi );
        }
        switch (type)
        {
        case DEVICE_FLOPPY:
        case DEVICE_RAMDISK:
            drive->devnum.DeviceType = FILE_DEVICE_DISK;
            drive->devnum.DeviceNumber = i;
            drive->devnum.PartitionNumber = ~0u;
            break;
        case DEVICE_CDROM:
            drive->devnum.DeviceType = FILE_DEVICE_CD_ROM;
            drive->devnum.DeviceNumber = i;
            drive->devnum.PartitionNumber = ~0u;
            break;
        case DEVICE_UNKNOWN:
        case DEVICE_HARDDISK:
        case DEVICE_NETWORK:  /* FIXME */
            {
                UNICODE_STRING symlink;

                symlink.MaximumLength = sizeof(physdriveW) + 10 * sizeof(WCHAR);
                if ((symlink.Buffer = RtlAllocateHeap( GetProcessHeap(), 0, symlink.MaximumLength)))
                {
                    sprintfW( symlink.Buffer, physdriveW, i );
                    symlink.Length = strlenW(symlink.Buffer) * sizeof(WCHAR);
                    if (!IoCreateSymbolicLink( &symlink, &name )) drive->symlink = symlink;
                }
                drive->devnum.DeviceType = FILE_DEVICE_DISK;
                drive->devnum.DeviceNumber = i;
                drive->devnum.PartitionNumber = 0;
            }
            break;
        case DEVICE_HARDDISK_VOL:
            drive->devnum.DeviceType = FILE_DEVICE_DISK;
            drive->devnum.DeviceNumber = 0;
            drive->devnum.PartitionNumber = i;
            break;
        }
        list_add_tail( &drives_list, &drive->entry );
        *drive_ret = drive;
        TRACE( "created device %s\n", debugstr_w(name.Buffer) );
    }
    else
    {
        FIXME( "IoCreateDevice %s got %x\n", debugstr_w(name.Buffer), status );
        RtlFreeUnicodeString( &name );
    }
    return status;
}

/* delete the disk device for a given drive */
static void delete_disk_device( struct dos_drive *drive )
{
    TRACE( "deleting device %s\n", debugstr_w(drive->name.Buffer) );
    list_remove( &drive->entry );
    if (drive->dosdev) delete_mount_point( drive->dosdev );
    if (drive->volume) delete_mount_point( drive->volume );
    if (drive->symlink.Buffer)
    {
        IoDeleteSymbolicLink( &drive->symlink );
        RtlFreeUnicodeString( &drive->symlink );
    }
    RtlFreeHeap( GetProcessHeap(), 0, drive->unix_device );
    RtlFreeHeap( GetProcessHeap(), 0, drive->unix_mount );
    RtlFreeHeap( GetProcessHeap(), 0, drive->udi );
    RtlFreeUnicodeString( &drive->name );
    IoDeleteDevice( drive->device );
}

/* set or change the drive letter for an existing drive */
static void set_drive_letter( struct dos_drive *drive, int letter )
{
    void *id = NULL;
    unsigned int id_len = 0;

    if (drive->drive == letter) return;
    if (drive->dosdev) delete_mount_point( drive->dosdev );
    if (drive->volume) delete_mount_point( drive->volume );
    drive->drive = letter;
    if (letter == -1) return;
    if (drive->unix_mount)
    {
        id = drive->unix_mount;
        id_len = strlen( drive->unix_mount ) + 1;
    }
    drive->dosdev = add_dosdev_mount_point( drive->device, &drive->name, letter, id, id_len );
    drive->volume = add_volume_mount_point( drive->device, &drive->name, letter, id, id_len );
}

static inline int is_valid_device( struct stat *st )
{
#if defined(linux) || defined(__sun__)
    return S_ISBLK( st->st_mode );
#else
    /* disks are char devices on *BSD */
    return S_ISCHR( st->st_mode );
#endif
}

/* find or create a DOS drive for the corresponding device */
static int add_drive( const char *device, enum device_type type )
{
    char *path, *p;
    char in_use[26];
    struct stat dev_st, drive_st;
    int drive, first, last, avail = 0;

    if (stat( device, &dev_st ) == -1 || !is_valid_device( &dev_st )) return -1;

    if (!(path = get_dosdevices_path( &p ))) return -1;

    memset( in_use, 0, sizeof(in_use) );

    switch (type)
    {
    case DEVICE_FLOPPY:
        first = 0;
        last = 2;
        break;
    case DEVICE_CDROM:
        first = 3;
        last = 26;
        break;
    default:
        first = 2;
        last = 26;
        break;
    }

    while (avail != -1)
    {
        avail = -1;
        for (drive = first; drive < last; drive++)
        {
            if (in_use[drive]) continue;  /* already checked */
            *p = 'a' + drive;
            if (stat( path, &drive_st ) == -1)
            {
                if (lstat( path, &drive_st ) == -1 && errno == ENOENT)  /* this is a candidate */
                {
                    if (avail == -1)
                    {
                        p[2] = 0;
                        /* if mount point symlink doesn't exist either, it's available */
                        if (lstat( path, &drive_st ) == -1 && errno == ENOENT) avail = drive;
                        p[2] = ':';
                    }
                }
                else in_use[drive] = 1;
            }
            else
            {
                in_use[drive] = 1;
                if (!is_valid_device( &drive_st )) continue;
                if (dev_st.st_rdev == drive_st.st_rdev) goto done;
            }
        }
        if (avail != -1)
        {
            /* try to use the one we found */
            drive = avail;
            *p = 'a' + drive;
            if (symlink( device, path ) != -1) goto done;
            /* failed, retry the search */
        }
    }
    drive = -1;

done:
    HeapFree( GetProcessHeap(), 0, path );
    return drive;
}

static BOOL set_unix_mount_point( struct dos_drive *drive, const char *mount_point )
{
    char *path, *p;
    BOOL modified = FALSE;

    if (!(path = get_dosdevices_path( &p ))) return FALSE;
    p[0] = 'a' + drive->drive;
    p[2] = 0;

    if (mount_point && mount_point[0])
    {
        /* try to avoid unlinking if already set correctly */
        if (!drive->unix_mount || strcmp( drive->unix_mount, mount_point ))
        {
            unlink( path );
            symlink( mount_point, path );
            modified = TRUE;
        }
        RtlFreeHeap( GetProcessHeap(), 0, drive->unix_mount );
        drive->unix_mount = strdupA( mount_point );
        if (drive->dosdev) set_mount_point_id( drive->dosdev, mount_point, strlen(mount_point) + 1 );
        if (drive->volume) set_mount_point_id( drive->volume, mount_point, strlen(mount_point) + 1 );
    }
    else
    {
        if (unlink( path ) != -1) modified = TRUE;
        RtlFreeHeap( GetProcessHeap(), 0, drive->unix_mount );
        drive->unix_mount = NULL;
        if (drive->dosdev) set_mount_point_id( drive->dosdev, NULL, 0 );
        if (drive->volume) set_mount_point_id( drive->volume, NULL, 0 );
    }

    HeapFree( GetProcessHeap(), 0, path );
    return modified;
}

/* create devices for mapped drives */
static void create_drive_devices(void)
{
    char *path, *p, *link, *device;
    struct dos_drive *drive;
    unsigned int i;
    HKEY drives_key;
    enum device_type drive_type;
    WCHAR driveW[] = {'a',':',0};

    if (!(path = get_dosdevices_path( &p ))) return;
    if (RegOpenKeyW( HKEY_LOCAL_MACHINE, drives_keyW, &drives_key )) drives_key = 0;

    for (i = 0; i < MAX_DOS_DRIVES; i++)
    {
        p[0] = 'a' + i;
        p[2] = 0;
        if (!(link = read_symlink( path ))) continue;
        p[2] = ':';
        device = read_symlink( path );

        drive_type = i < 2 ? DEVICE_FLOPPY : DEVICE_HARDDISK_VOL;
        if (drives_key)
        {
            WCHAR buffer[32];
            DWORD j, type, size = sizeof(buffer);

            driveW[0] = 'a' + i;
            if (!RegQueryValueExW( drives_key, driveW, NULL, &type, (BYTE *)buffer, &size ) &&
                type == REG_SZ)
            {
                for (j = 0; j < sizeof(drive_types)/sizeof(drive_types[0]); j++)
                    if (drive_types[j][0] && !strcmpiW( buffer, drive_types[j] ))
                    {
                        drive_type = j;
                        break;
                    }
                if (drive_type == DEVICE_FLOPPY && i >= 2) drive_type = DEVICE_HARDDISK;
            }
        }

        if (!create_disk_device( NULL, drive_type, &drive ))
        {
            drive->unix_mount = link;
            drive->unix_device = device;
            set_drive_letter( drive, i );
        }
        else
        {
            RtlFreeHeap( GetProcessHeap(), 0, link );
            RtlFreeHeap( GetProcessHeap(), 0, device );
        }
    }
    RegCloseKey( drives_key );
    RtlFreeHeap( GetProcessHeap(), 0, path );
}

/* create a new dos drive */
NTSTATUS add_dos_device( int letter, const char *udi, const char *device,
                         const char *mount_point, enum device_type type )
{
    struct dos_drive *drive, *next;

    if (letter == -1)  /* auto-assign a letter */
    {
        letter = add_drive( device, type );
        if (letter == -1) return STATUS_OBJECT_NAME_COLLISION;
    }
    else  /* simply reset the device symlink */
    {
        char *path, *p;

        if (!(path = get_dosdevices_path( &p ))) return STATUS_NO_MEMORY;
        *p = 'a' + letter;
        unlink( path );
        if (device) symlink( device, path );
    }

    LIST_FOR_EACH_ENTRY_SAFE( drive, next, &drives_list, struct dos_drive, entry )
    {
        if (udi && drive->udi && !strcmp( udi, drive->udi ))
        {
            if (type == drive->type) goto found;
            delete_disk_device( drive );
            continue;
        }
        if (drive->drive == letter) delete_disk_device( drive );
    }

    if (create_disk_device( udi, type, &drive )) return STATUS_NO_MEMORY;

found:
    RtlFreeHeap( GetProcessHeap(), 0, drive->unix_device );
    drive->unix_device = strdupA( device );
    set_drive_letter( drive, letter );
    set_unix_mount_point( drive, mount_point );

    if (drive->drive != -1)
    {
        HKEY hkey;

        TRACE( "added device %c: udi %s for %s on %s type %u\n",
                    'a' + drive->drive, wine_dbgstr_a(udi), wine_dbgstr_a(device),
                    wine_dbgstr_a(mount_point), type );

        /* hack: force the drive type in the registry */
        if (!RegCreateKeyW( HKEY_LOCAL_MACHINE, drives_keyW, &hkey ))
        {
            const WCHAR *type_name = drive_types[type];
            WCHAR name[3] = {'a',':',0};

            name[0] += drive->drive;
            if (!type_name[0] && type == DEVICE_HARDDISK) type_name = drive_types[DEVICE_FLOPPY];
            if (type_name[0])
                RegSetValueExW( hkey, name, 0, REG_SZ, (const BYTE *)type_name,
                                (strlenW(type_name) + 1) * sizeof(WCHAR) );
            else
                RegDeleteValueW( hkey, name );
            RegCloseKey( hkey );
        }

        if (udi) send_notify( drive->drive, DBT_DEVICEARRIVAL );
    }
    return STATUS_SUCCESS;
}

/* remove an existing dos drive, by letter or udi */
NTSTATUS remove_dos_device( int letter, const char *udi )
{
    HKEY hkey;
    struct dos_drive *drive;

    LIST_FOR_EACH_ENTRY( drive, &drives_list, struct dos_drive, entry )
    {
        if (letter != -1 && drive->drive != letter) continue;
        if (udi)
        {
            if (!drive->udi) continue;
            if (strcmp( udi, drive->udi )) continue;
        }

        if (drive->drive != -1)
        {
            BOOL modified = set_unix_mount_point( drive, NULL );

            /* clear the registry key too */
            if (!RegOpenKeyW( HKEY_LOCAL_MACHINE, drives_keyW, &hkey ))
            {
                WCHAR name[3] = {'a',':',0};
                name[0] += drive->drive;
                RegDeleteValueW( hkey, name );
                RegCloseKey( hkey );
            }

            if (modified && udi) send_notify( drive->drive, DBT_DEVICEREMOVECOMPLETE );
        }
        delete_disk_device( drive );
        return STATUS_SUCCESS;
    }
    return STATUS_NO_SUCH_DEVICE;
}

/* query information about an existing dos drive, by letter or udi */
NTSTATUS query_dos_device( int letter, enum device_type *type,
                           const char **device, const char **mount_point )
{
    struct dos_drive *drive;

    LIST_FOR_EACH_ENTRY( drive, &drives_list, struct dos_drive, entry )
    {
        if (drive->drive != letter) continue;
        if (type) *type = drive->type;
        if (device) *device = drive->unix_device;
        if (mount_point) *mount_point = drive->unix_mount;
        return STATUS_SUCCESS;
    }
    return STATUS_NO_SUCH_DEVICE;
}

/* handler for ioctls on the harddisk device */
static NTSTATUS WINAPI harddisk_ioctl( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = irp->Tail.Overlay.s.u.CurrentStackLocation;
    struct dos_drive *drive = device->DeviceExtension;

    TRACE( "ioctl %x insize %u outsize %u\n",
           irpsp->Parameters.DeviceIoControl.IoControlCode,
           irpsp->Parameters.DeviceIoControl.InputBufferLength,
           irpsp->Parameters.DeviceIoControl.OutputBufferLength );

    switch(irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    {
        DISK_GEOMETRY info;
        DWORD len = min( sizeof(info), irpsp->Parameters.DeviceIoControl.OutputBufferLength );

        info.Cylinders.QuadPart = 10000;
        info.MediaType = (drive->devnum.DeviceType == FILE_DEVICE_DISK) ? FixedMedia : RemovableMedia;
        info.TracksPerCylinder = 255;
        info.SectorsPerTrack = 63;
        info.BytesPerSector = 512;
        memcpy( irp->MdlAddress->StartVa, &info, len );
        irp->IoStatus.Information = len;
        irp->IoStatus.u.Status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
        DWORD len = min( sizeof(drive->devnum), irpsp->Parameters.DeviceIoControl.OutputBufferLength );

        memcpy( irp->MdlAddress->StartVa, &drive->devnum, len );
        irp->IoStatus.Information = len;
        irp->IoStatus.u.Status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_CDROM_READ_TOC:
        irp->IoStatus.u.Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    default:
        FIXME( "unsupported ioctl %x\n", irpsp->Parameters.DeviceIoControl.IoControlCode );
        irp->IoStatus.u.Status = STATUS_NOT_SUPPORTED;
        break;
    }
    return irp->IoStatus.u.Status;
}

/* driver entry point for the harddisk driver */
NTSTATUS WINAPI harddisk_driver_entry( DRIVER_OBJECT *driver, UNICODE_STRING *path )
{
    struct dos_drive *drive;

    harddisk_driver = driver;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = harddisk_ioctl;

    /* create a harddisk0 device that isn't assigned to any drive */
    create_disk_device( "harddisk0 placeholder", DEVICE_HARDDISK, &drive );

    create_drive_devices();

    return STATUS_SUCCESS;
}
