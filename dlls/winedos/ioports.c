/*
 * Emulation of processor ioports.
 *
 * Copyright 1995 Morten Welinder
 * Copyright 1998 Andreas Mohr, Ove Kaaven
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

/* Known problems:
   - only a few ports are emulated.
   - real-time clock in "cmos" is bogus.  A nifty alarm() setup could
     fix that, I guess.
*/

#include "config.h"

#include <stdarg.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "winternl.h"
#include "dosexe.h"
#include "vga.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(int);

#if defined(linux) && defined(__i386__)
# define DIRECT_IO_ACCESS
#else
# undef DIRECT_IO_ACCESS
#endif  /* linux && __i386__ */

static struct {
    WORD	countmax;
    BOOL16	byte_toggle; /* if TRUE, then hi byte has already been written */
    WORD	latch;
    BOOL16	latched;
    BYTE	ctrlbyte_ch;
    WORD	oldval;
} tmr_8253[3] = {
    {0xFFFF,	FALSE,	0,	FALSE,	0x36,	0},
    {0x0012,	FALSE,	0,	FALSE,	0x74,	0},
    {0x0001,	FALSE,	0,	FALSE,	0xB6,	0},
};

static int dummy_ctr = 0;

static BYTE parport_8255[4] = {0x4f, 0x20, 0xff, 0xff};

static BYTE cmosaddress;

static int cmos_image_initialized = 0;

static BYTE cmosimage[64] =
{
  0x27, /* 0x00: seconds */
  0x34, /* 0X01: seconds alarm */
  0x31, /* 0x02: minutes */
  0x47, /* 0x03: minutes alarm */
  0x16, /* 0x04: hour */
  0x15, /* 0x05: hour alarm */
  0x00, /* 0x06: week day */
  0x01, /* 0x07: month day */
  0x04, /* 0x08: month */
  0x94, /* 0x09: year */
  0x26, /* 0x0a: state A */
  0x02, /* 0x0b: state B */
  0x50, /* 0x0c: state C */
  0x80, /* 0x0d: state D */
  0x00, /* 0x0e: state diagnostic */
  0x00, /* 0x0f: state state shutdown */
  0x40, /* 0x10: floppy type */
  0xb1, /* 0x11: reserved */
  0x00, /* 0x12: HD type */
  0x9c, /* 0x13: reserved */
  0x01, /* 0x14: equipment */
  0x80, /* 0x15: low base memory */
  0x02, /* 0x16: high base memory (0x280 => 640KB) */
  0x00, /* 0x17: low extended memory */
  0x3b, /* 0x18: high extended memory (0x3b00 => 15MB) */
  0x00, /* 0x19: HD 1 extended type byte */
  0x00, /* 0x1a: HD 2 extended type byte */
  0xad, /* 0x1b: reserved */
  0x02, /* 0x1c: reserved */
  0x10, /* 0x1d: reserved */
  0x00, /* 0x1e: reserved */
  0x00, /* 0x1f: installed features */
  0x08, /* 0x20: HD 1 low cylinder number */
  0x00, /* 0x21: HD 1 high cylinder number */
  0x00, /* 0x22: HD 1 heads */
  0x26, /* 0x23: HD 1 low pre-compensation start */
  0x00, /* 0x24: HD 1 high pre-compensation start */
  0x00, /* 0x25: HD 1 low landing zone */
  0x00, /* 0x26: HD 1 high landing zone */
  0x00, /* 0x27: HD 1 sectors */
  0x00, /* 0x28: options 1 */
  0x00, /* 0x29: reserved */
  0x00, /* 0x2a: reserved */
  0x00, /* 0x2b: options 2 */
  0x00, /* 0x2c: options 3 */
  0x3f, /* 0x2d: reserved  */
  0xcc, /* 0x2e: low CMOS ram checksum (computed automatically) */
  0xcc, /* 0x2f: high CMOS ram checksum (computed automatically) */
  0x00, /* 0x30: low extended memory byte */
  0x1c, /* 0x31: high extended memory byte */
  0x19, /* 0x32: century byte */
  0x81, /* 0x33: setup information */
  0x00, /* 0x34: CPU speed */
  0x0e, /* 0x35: HD 2 low cylinder number */
  0x00, /* 0x36: HD 2 high cylinder number */
  0x80, /* 0x37: HD 2 heads */
  0x1b, /* 0x38: HD 2 low pre-compensation start */
  0x7b, /* 0x39: HD 2 high pre-compensation start */
  0x21, /* 0x3a: HD 2 low landing zone */
  0x00, /* 0x3b: HD 2 high landing zone */
  0x00, /* 0x3c: HD 2 sectors */
  0x00, /* 0x3d: reserved */
  0x05, /* 0x3e: reserved */
  0x5f  /* 0x3f: reserved */
};

static void IO_FixCMOSCheckSum(void)
{
	WORD sum = 0;
	int i;

	for (i=0x10; i < 0x2d; i++)
		sum += cmosimage[i];
	cmosimage[0x2e] = sum >> 8; /* yes, this IS hi byte !! */
	cmosimage[0x2f] = sum & 0xff;
	TRACE("calculated hi %02x, lo %02x\n", cmosimage[0x2e], cmosimage[0x2f]);
}

#ifdef DIRECT_IO_ACCESS

extern int iopl(int level);
static char do_direct_port_access = -1;
static char port_permissions[0x10000];

#define IO_READ  1
#define IO_WRITE 2

#endif  /* DIRECT_IO_ACCESS */

#ifdef HAVE_PPDEV
static int do_pp_port_access = -1; /* -1: uninitialized, 1: not available
				       0: available);*/
#endif

static void set_timer_maxval(unsigned timer, unsigned maxval)
{
    switch (timer) {
        case 0: /* System timer counter divisor */
            DOSVM_SetTimer(maxval);
            break;
        case 1: /* RAM refresh */
            FIXME("RAM refresh counter handling not implemented !\n");
            break;
        case 2: /* cassette & speaker */
            /* speaker on ? */
            if ((parport_8255[1] & 3) == 3)
            {
                TRACE("Beep (freq: %d) !\n", 1193180 / maxval );
                Beep(1193180 / maxval, 20);
            }
            break;
    }
}


/**********************************************************************
 *	    IO_port_init
 */

/* set_IO_permissions(int val1, int val)
 * Helper function for IO_port_init
 */
#ifdef DIRECT_IO_ACCESS
static void set_IO_permissions(int val1, int val, char rw)
{
	int j;
	if (val1 != -1) {
		if (val == -1) val = 0x3ff;
		for (j = val1; j <= val; j++)
			port_permissions[j] |= rw;

		do_direct_port_access = 1;

		val1 = -1;
	} else if (val != -1) {
		do_direct_port_access = 1;

		port_permissions[val] |= rw;
	}

}

/* do_IO_port_init_read_or_write(char* temp, char rw)
 * Helper function for IO_port_init
 */

static void do_IO_port_init_read_or_write(const WCHAR *str, char rw)
{
    int val, val1;
    unsigned int i;
    WCHAR *end;
    static const WCHAR allW[] = {'a','l','l',0};

    if (!strcmpiW(str, allW))
    {
        for (i=0; i < sizeof(port_permissions); i++)
            port_permissions[i] |= rw;
    }
    else
    {
        val = -1;
        val1 = -1;
        while (*str)
        {
            switch(*str)
            {
            case ',':
            case ' ':
            case '\t':
                set_IO_permissions(val1, val, rw);
                val1 = -1;
                val = -1;
                str++;
                break;
            case '-':
                val1 = val;
                if (val1 == -1) val1 = 0;
                str++;
                break;
            default:
                if (isdigitW(*str))
                {
                    val = strtoulW( str, &end, 0 );
                    if (end == str)
                    {
                        val = -1;
                        str++;
                    }
                    else str = end;
                }
                break;
            }
        }
        set_IO_permissions(val1, val, rw);
    }
}

static inline BYTE inb( WORD port )
{
    BYTE b;
    __asm__ __volatile__( "inb %w1,%0" : "=a" (b) : "d" (port) );
    return b;
}

static inline WORD inw( WORD port )
{
    WORD w;
    __asm__ __volatile__( "inw %w1,%0" : "=a" (w) : "d" (port) );
    return w;
}

static inline DWORD inl( WORD port )
{
    DWORD dw;
    __asm__ __volatile__( "inl %w1,%0" : "=a" (dw) : "d" (port) );
    return dw;
}

static inline void outb( BYTE value, WORD port )
{
    __asm__ __volatile__( "outb %b0,%w1" : : "a" (value), "d" (port) );
}

static inline void outw( WORD value, WORD port )
{
    __asm__ __volatile__( "outw %w0,%w1" : : "a" (value), "d" (port) );
}

static inline void outl( DWORD value, WORD port )
{
    __asm__ __volatile__( "outl %0,%w1" : : "a" (value), "d" (port) );
}

static void IO_port_init(void)
{
    char tmp[1024];
    HANDLE root, hkey;
    DWORD dummy;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING nameW;

    static const WCHAR portsW[] = {'S','o','f','t','w','a','r','e','\\',
                                   'W','i','n','e','\\','V','D','M','\\','P','o','r','t','s',0};
    static const WCHAR readW[] = {'r','e','a','d',0};
    static const WCHAR writeW[] = {'w','r','i','t','e',0};

    do_direct_port_access = 0;
    /* Can we do that? */
    if (!iopl(3))
    {
        iopl(0);

        RtlOpenCurrentUser( KEY_ALL_ACCESS, &root );
        attr.Length = sizeof(attr);
        attr.RootDirectory = root;
        attr.ObjectName = &nameW;
        attr.Attributes = 0;
        attr.SecurityDescriptor = NULL;
        attr.SecurityQualityOfService = NULL;
        RtlInitUnicodeString( &nameW, portsW );

        /* @@ Wine registry key: HKCU\Software\Wine\VDM\Ports */
        if (!NtOpenKey( &hkey, KEY_ALL_ACCESS, &attr ))
        {
            RtlInitUnicodeString( &nameW, readW );
            if (!NtQueryValueKey( hkey, &nameW, KeyValuePartialInformation, tmp, sizeof(tmp), &dummy ))
            {
                WCHAR *str = (WCHAR *)((KEY_VALUE_PARTIAL_INFORMATION *)tmp)->Data;
                do_IO_port_init_read_or_write(str, IO_READ);
            }
            RtlInitUnicodeString( &nameW, writeW );
            if (!NtQueryValueKey( hkey, &nameW, KeyValuePartialInformation, tmp, sizeof(tmp), &dummy ))
            {
                WCHAR *str = (WCHAR *)((KEY_VALUE_PARTIAL_INFORMATION *)tmp)->Data;
                do_IO_port_init_read_or_write(str, IO_WRITE);
            }
            NtClose( hkey );
        }
        NtClose( root );
    }
}

#endif  /* DIRECT_IO_ACCESS */


/**********************************************************************
 *	    inport   (WINEDOS.@)
 *
 * Note: The size argument has to be handled correctly _externally_
 * (as we always return a DWORD)
 */
DWORD WINAPI DOSVM_inport( int port, int size )
{
    DWORD res = ~0U;

    TRACE("%d-byte value from port 0x%04x\n", size, port );

#ifdef HAVE_PPDEV
    if (do_pp_port_access == -1) do_pp_port_access =IO_pp_init();
    if ((do_pp_port_access == 0 ) && (size == 1))
    {
        if (!IO_pp_inp(port,&res)) return res;
    }
#endif

#ifdef DIRECT_IO_ACCESS
    if (do_direct_port_access == -1) IO_port_init();
    if ((do_direct_port_access)
        /* Make sure we have access to the port */
        && (port_permissions[port] & IO_READ))
    {
        iopl(3);
        switch(size)
        {
        case 1: res = inb( port ); break;
        case 2: res = inw( port ); break;
        case 4: res = inl( port ); break;
        }
        iopl(0);
        return res;
    }
#endif

    switch (port)
    {
    case 0x40:
    case 0x41:
    case 0x42:
        {
            BYTE chan = port & 3;
            WORD tempval = 0;
            if (tmr_8253[chan].latched)
                tempval = tmr_8253[chan].latch;
            else
            {
                dummy_ctr -= 1 + (int)(10.0 * rand() / (RAND_MAX + 1.0));
                if (chan == 0) /* System timer counter divisor */
                {
                    /* FIXME: DOSVM_GetTimer() returns quite rigid values */
                    tempval = dummy_ctr + (WORD)DOSVM_GetTimer();
                }
                else
                {
                    /* FIXME: intelligent hardware timer emulation needed */
                    tempval = dummy_ctr;
                }
            }

            switch ((tmr_8253[chan].ctrlbyte_ch & 0x30) >> 4)
            {
            case 0:
                res = 0; /* shouldn't happen? */
                break;
            case 1: /* read lo byte */
                res = (BYTE)tempval;
                tmr_8253[chan].latched = FALSE;
                break;
            case 3: /* read lo byte, then hi byte */
                tmr_8253[chan].byte_toggle ^= 1; /* toggle */
                if (tmr_8253[chan].byte_toggle)
                {
                    res = (BYTE)tempval;
                    break;
                }
                /* else [fall through if read hi byte !] */
            case 2: /* read hi byte */
                res = (BYTE)(tempval >> 8);
                tmr_8253[chan].latched = FALSE;
                break;
            }
        }
        break;
    case 0x60:
        res = DOSVM_Int09ReadScan(NULL);
        break;
    case 0x61:
        res = (DWORD)parport_8255[1];
        break;
    case 0x62:
        res = (DWORD)parport_8255[2];
        break;
    case 0x70:
        res = (DWORD)cmosaddress;
        break;
    case 0x71:
        if (!cmos_image_initialized)
        {
            IO_FixCMOSCheckSum();
            cmos_image_initialized = 1;
        }
        res = (DWORD)cmosimage[cmosaddress & 0x3f];
        break;
    case 0x200:
    case 0x201:
        res = ~0U; /* no joystick */
        break;
    case 0x22a:
    case 0x22c:
    case 0x22e:
        res = (DWORD)SB_ioport_in( port );
	break;
    /* VGA read registers */
    case 0x3b4:  /* CRT Controller Register - Index (MDA) */
    case 0x3b5:  /* CRT Controller Register - Other (MDA) */
    case 0x3ba:  /* General Register - Input status 1 (MDA) */
    case 0x3c0:  /* Attribute Controller - Address */
    case 0x3c1:  /* Attribute Controller - Other */
    case 0x3c2:  /* General Register - Input status 0 */
    case 0x3c3:  /* General Register - Video subsystem enable */
    case 0x3c4:  /* Sequencer Register - Address */
    case 0x3c5:  /* Sequencer Register - Other */
    case 0x3c6:
    case 0x3c7:  /* General Register -  DAC State */
    case 0x3c8:
    case 0x3c9:
    case 0x3ca:  /* General Register - Feature control */
    case 0x3cb:
    case 0x3cc:  /* General Register - Misc output */
    case 0x3cd:
    case 0x3ce:  /* Graphics Controller Register - Address */
    case 0x3cf:  /* Graphics Controller Register - Other */
    case 0x3d0:
    case 0x3d1:
    case 0x3d2:
    case 0x3d3:
    case 0x3d4:  /* CRT Controller Register - Index (CGA) */
    case 0x3d5:  /* CRT Controller Register - Other (CGA) */
    case 0x3d6:
    case 0x3d7:
    case 0x3d8:
    case 0x3d9:
    case 0x3da:
    case 0x3db:
    case 0x3dc:
    case 0x3dd:
    case 0x3de:
    case 0x3df:
        if (size > 1)
            FIXME("Trying to read more than one byte from VGA!\n");
        res = (DWORD)VGA_ioport_in( port );
        break;
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0xC0:
    case 0xC2:
    case 0xC4:
    case 0xC6:
    case 0xC8:
    case 0xCA:
    case 0xCC:
    case 0xCE:
    case 0x87:
    case 0x83:
    case 0x81:
    case 0x82:
    case 0x8B:
    case 0x89:
    case 0x8A:
    case 0x487:
    case 0x483:
    case 0x481:
    case 0x482:
    case 0x48B:
    case 0x489:
    case 0x48A:
    case 0x08:
    case 0xD0:
    case 0x0D:
    case 0xDA:
        res = (DWORD)DMA_ioport_in( port );
        break;
    default:
        WARN("Direct I/O read attempted from port %x\n", port);
        break;
    }
    return res;
}


/**********************************************************************
 *	    outport  (WINEDOS.@)
 */
void WINAPI DOSVM_outport( int port, int size, DWORD value )
{
    TRACE("IO: 0x%x (%d-byte value) to port 0x%04x\n", value, size, port );

#ifdef HAVE_PPDEV
    if (do_pp_port_access == -1) do_pp_port_access = IO_pp_init();
    if ((do_pp_port_access == 0) && (size == 1))
    {
        if (!IO_pp_outp(port,&value)) return;
    }
#endif

#ifdef DIRECT_IO_ACCESS

    if (do_direct_port_access == -1) IO_port_init();
    if ((do_direct_port_access)
        /* Make sure we have access to the port */
        && (port_permissions[port] & IO_WRITE))
    {
        iopl(3);
        switch(size)
        {
        case 1: outb( LOBYTE(value), port ); break;
        case 2: outw( LOWORD(value), port ); break;
        case 4: outl( value, port ); break;
        }
        iopl(0);
        return;
    }
#endif

    switch (port)
    {
    case 0x20:
        DOSVM_PIC_ioport_out( port, (BYTE)value );
        break;
    case 0x40:
    case 0x41:
    case 0x42:
        {
            BYTE chan = port & 3;

            /* we need to get the oldval before any lo/hi byte change has been made */
            if (((tmr_8253[chan].ctrlbyte_ch & 0x30) != 0x30) ||
                !tmr_8253[chan].byte_toggle)
                tmr_8253[chan].oldval = tmr_8253[chan].countmax;
            switch ((tmr_8253[chan].ctrlbyte_ch & 0x30) >> 4)
            {
            case 0:
                break; /* shouldn't happen? */
            case 1: /* write lo byte */
                tmr_8253[chan].countmax =
                    (tmr_8253[chan].countmax & 0xff00) | (BYTE)value;
                break;
            case 3: /* write lo byte, then hi byte */
                tmr_8253[chan].byte_toggle ^= TRUE; /* toggle */
                if (tmr_8253[chan].byte_toggle)
                {
                    tmr_8253[chan].countmax =
                        (tmr_8253[chan].countmax & 0xff00) | (BYTE)value;
                    break;
                }
                /* else [fall through if write hi byte !] */
            case 2: /* write hi byte */
                tmr_8253[chan].countmax =
                    (tmr_8253[chan].countmax & 0x00ff) | ((BYTE)value << 8);
                break;
            }
            /* if programming is finished and value has changed
               then update to new value */
            if ((((tmr_8253[chan].ctrlbyte_ch & 0x30) != 0x30) ||
                 !tmr_8253[chan].byte_toggle) &&
                (tmr_8253[chan].countmax != tmr_8253[chan].oldval))
                set_timer_maxval(chan, tmr_8253[chan].countmax);
        }
        break;
    case 0x43:
       {
           BYTE chan = ((BYTE)value & 0xc0) >> 6;
           /* ctrl byte for specific timer channel */
           if (chan == 3)
           {
               FIXME("8254 timer readback not implemented yet\n");
               break;
           }
           switch (((BYTE)value & 0x30) >> 4)
           {
           case 0:	/* latch timer */
               tmr_8253[chan].latched = TRUE;
               dummy_ctr -= 1 + (int)(10.0 * rand() / (RAND_MAX + 1.0));
               if (chan == 0) /* System timer divisor */
                   tmr_8253[chan].latch = dummy_ctr + (WORD)DOSVM_GetTimer();
               else
               {
                   /* FIXME: intelligent hardware timer emulation needed */
                   tmr_8253[chan].latch = dummy_ctr;
               }
               break;
           case 3:	/* write lo byte, then hi byte */
               tmr_8253[chan].byte_toggle = FALSE; /* init */
               /* fall through */
           case 1:	/* write lo byte only */
           case 2:	/* write hi byte only */
               tmr_8253[chan].ctrlbyte_ch = (BYTE)value;
               break;
           }
       }
       break;
    case 0x61:
        parport_8255[1] = (BYTE)value;
        if (((parport_8255[1] & 3) == 3) && (tmr_8253[2].countmax != 1))
        {
            TRACE("Beep (freq: %d) !\n", 1193180 / tmr_8253[2].countmax);
            Beep(1193180 / tmr_8253[2].countmax, 20);
        }
        break;
    case 0x70:
        cmosaddress = (BYTE)value & 0x7f;
        break;
    case 0x71:
        if (!cmos_image_initialized)
        {
            IO_FixCMOSCheckSum();
            cmos_image_initialized = 1;
        }
        cmosimage[cmosaddress & 0x3f] = (BYTE)value;
        break;
    case 0x226:
    case 0x22c:
        SB_ioport_out( port, (BYTE)value );
        break;
    /* VGA Write registers */
    case 0x3b4:  /* CRT Controller Register - Index (MDA) */
    case 0x3b5:  /* CRT Controller Register - Other (MDA) */
    case 0x3ba:  /* General Register - Feature Control */
    case 0x3c0:  /* Attribute Controller - Address/Other */
    case 0x3c1:
    case 0x3c2:  /* General Register - Misc output */
    case 0x3c3:  /* General Register - Video subsystem enable */
    case 0x3c4:  /* Sequencer Register - Address */
    case 0x3c5:  /* Sequencer Register - Other */
    case 0x3c6:
    case 0x3c7:
    case 0x3c8:
    case 0x3c9:
    case 0x3ca:
    case 0x3cb:
    case 0x3cc:
    case 0x3cd:
    case 0x3ce:  /* Graphics Controller Register - Address */
    case 0x3cf:  /* Graphics Controller Register - Other */
    case 0x3d0:
    case 0x3d1:
    case 0x3d2:
    case 0x3d3:
    case 0x3d4:  /* CRT Controller Register - Index (CGA) */
    case 0x3d5:  /* CRT Controller Register - Other (CGA) */
    case 0x3d6:
    case 0x3d7:
    case 0x3d8:
    case 0x3d9:
    case 0x3da:
    case 0x3db:
    case 0x3dc:
    case 0x3dd:
    case 0x3de:
    case 0x3df:
        VGA_ioport_out( port, LOBYTE(value) );
        if(size > 1) {
            VGA_ioport_out( port+1, HIBYTE(value) );
            if(size > 2) {
                VGA_ioport_out( port+2, LOBYTE(HIWORD(value)) );
                VGA_ioport_out( port+3, HIBYTE(HIWORD(value)) );
            }
        }
        break;
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0xC0:
    case 0xC2:
    case 0xC4:
    case 0xC6:
    case 0xC8:
    case 0xCA:
    case 0xCC:
    case 0xCE:
    case 0x87:
    case 0x83:
    case 0x81:
    case 0x82:
    case 0x8B:
    case 0x89:
    case 0x8A:
    case 0x487:
    case 0x483:
    case 0x481:
    case 0x482:
    case 0x48B:
    case 0x489:
    case 0x48A:
    case 0x08:
    case 0xD0:
    case 0x0B:
    case 0xD6:
    case 0x0A:
    case 0xD4:
    case 0x0F:
    case 0xDE:
    case 0x09:
    case 0xD2:
    case 0x0C:
    case 0xD8:
    case 0x0D:
    case 0xDA:
    case 0x0E:
    case 0xDC:
        DMA_ioport_out( port, (BYTE)value );
        break;
    default:
        WARN("Direct I/O write attempted to port %x\n", port );
        break;
    }
}
