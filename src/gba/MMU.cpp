#include "../System.h"
#include "../common/Port.h"
#include "Cartridge.h"
#include "CPU.h"
#include "GBA.h"
#include "Globals.h"
#include "Sound.h"
#include <cstdio>


extern bool stopState;
extern bool timer0On;
extern int timer0Ticks;
extern int timer0ClockReload;
extern bool timer1On;
extern int timer1Ticks;
extern int timer1ClockReload;
extern bool timer2On;
extern int timer2Ticks;
extern int timer2ClockReload;
extern bool timer3On;
extern int timer3Ticks;
extern int timer3ClockReload;

namespace MMU
{

static bool ioReadable[0x400];

template<class T>
static T readGeneric(u32 address);
 
template<class T, int mask>
static T readBios(u32 address);

template<class T>
static T readVRAM(u32 address);

static u8 readIo8(u32 address);
static u16 readIo16(u32 address);
static u32 readIo32(u32 address);

static void OldCPUWriteMemory(u32 address, u32 value);
static void OldCPUWriteHalfWord(u32 address, u16 value);
static void OldCPUWriteByte(u32 address, u8 b);
 
template<class T>
static void unwritable(u32 address, T value)
{
	// TODO : log
}

template<class T>
static T unreadable(u32 address)
 {
 #ifdef GBA_LOGGING
		if (systemVerbose & VERBOSE_ILLEGAL_READ)
 		{
			log("Illegal read: %08x at %08x\n", address, CPU::armMode ?
 			    CPU::armNextPC - 4 : CPU::armNextPC - 2);
 		}
 #endif
 
	return 0;
}

struct MemAccess
{
	u8 *mem;
	u32 mask;
	u8 (*read8)(u32);
	u16 (*read16)(u32);
	u32 (*read32)(u32);
	void (*write8)(u32, u8);
	void (*write16)(u32, u16);
	void (*write32)(u32, u32);
};

static MemAccess memMap[] =
{
	{ 0, 0x00003FFF, readBios<u8, 0x03>, readBios<u16, 0x02>, readBios<u32, 0x0F>, unwritable<u8>,    unwritable<u16>,     unwritable<u32>    }, // 0 - bios - mask values are probably wrong
	{ 0, 0x00000000, unreadable<u8>,     unreadable<u16>,     unreadable<u32>,     unwritable<u8>,    unwritable<u16>,     unwritable<u32>    }, // 1
	{ 0, 0x0003FFFF, readGeneric<u8>,    readGeneric<u16>,    readGeneric<u32>,    OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 2
	{ 0, 0x00007FFF, readGeneric<u8>,    readGeneric<u16>,    readGeneric<u32>,    OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 3
	{ 0, 0x000003FF, readIo8,            readIo16,            readIo32,            OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 4
	{ 0, 0x000003FF, readGeneric<u8>,    readGeneric<u16>,    readGeneric<u32>,    OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 5
	{ 0, 0x0001FFFF, readVRAM<u8>,       readVRAM<u16>,       readVRAM<u32>,       OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 6
	{ 0, 0x000003FF, readGeneric<u8>,    readGeneric<u16>,    readGeneric<u32>,    OldCPUWriteByte,   OldCPUWriteHalfWord, OldCPUWriteMemory  }, // 7
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 8
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 9
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 10
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 11
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 12
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }, // 13
	{ 0, 0xFFFFFFFF, Cartridge::read8,   Cartridge::read16,   Cartridge::read32,   Cartridge::write8, Cartridge::write16,  Cartridge::write32 }  // 14
};

static const u32 objTilesAddress [3] = {0x010000, 0x014000, 0x014000};

bool init()
{
	workRAM = new u8[0x40000];
	if (!workRAM)
		return false;

	bios = new u8[0x4000];
	if (!bios)
		return false;

	internalRAM = new u8[0x8000];
	if (!internalRAM)
		return false;

	paletteRAM = new u8[0x400];
	if (!paletteRAM)
		return false;

	vram = new u8[0x20000];
	if (!vram)
		return false;

	oam = new u8[0x400];
	if (!oam)
		return false;

	ioMem = new u8[0x400];
	if (!ioMem)
		return false;

	memMap[0].mem = bios;
	memMap[2].mem = workRAM;
	memMap[3].mem = internalRAM;
	memMap[4].mem = ioMem;
	memMap[5].mem = paletteRAM;
	memMap[6].mem = vram;
	memMap[7].mem = oam;
	
	for (int i = 0; i < 0x400; i++)
		ioReadable[i] = true;
	for (int i = 0x10; i < 0x48; i++)
		ioReadable[i] = false;
	for (int i = 0x4c; i < 0x50; i++)
		ioReadable[i] = false;
	for (int i = 0x54; i < 0x60; i++)
		ioReadable[i] = false;
	for (int i = 0x8c; i < 0x90; i++)
		ioReadable[i] = false;
	for (int i = 0xa0; i < 0xb8; i++)
		ioReadable[i] = false;
	for (int i = 0xbc; i < 0xc4; i++)
		ioReadable[i] = false;
	for (int i = 0xc8; i < 0xd0; i++)
		ioReadable[i] = false;
	for (int i = 0xd4; i < 0xdc; i++)
		ioReadable[i] = false;
	for (int i = 0xe0; i < 0x100; i++)
		ioReadable[i] = false;
	for (int i = 0x110; i < 0x120; i++)
		ioReadable[i] = false;
	for (int i = 0x12c; i < 0x130; i++)
		ioReadable[i] = false;
	for (int i = 0x138; i < 0x140; i++)
		ioReadable[i] = false;
	for (int i = 0x144; i < 0x150; i++)
		ioReadable[i] = false;
	for (int i = 0x15c; i < 0x200; i++)
		ioReadable[i] = false;
	for (int i = 0x20c; i < 0x300; i++)
		ioReadable[i] = false;
	for (int i = 0x304; i < 0x400; i++)
		ioReadable[i] = false;

	return true;
}

void uninit()
{
	if (vram)
 	{
		delete vram;
		vram = 0;
	}
 
	if (paletteRAM)
	{
		delete paletteRAM;
		paletteRAM = 0;
	}

	if (internalRAM)
	{
		delete internalRAM;
		internalRAM = 0;
	}

	if (workRAM)
	{
		delete workRAM;
		workRAM = 0;
	}

	if (bios)
	{
		delete bios;
		bios = 0;
	}

	if (oam)
	{
		delete oam;
		oam = 0;
	}

	if (ioMem)
	{
		delete ioMem;
		ioMem = 0;
	}
}

u32 read32(u32 address)
{
 #ifdef GBA_LOGGING
	if (address & 3)
	{
		if (systemVerbose & VERBOSE_UNALIGNED_MEMORY)
 		{
			log("Unaligned word read: %08x at %08x\n", address, CPU::armMode ?
 			    CPU::armNextPC - 4 : CPU::armNextPC - 2);
 		}
	}
 #endif
 
	// Reads must be 32 bits aligned
	u32 value =  memMap[address >> 24].read32(address & 0xFFFFFFFC);
 
 	if (address & 3)
 	{
 		int shift = (address & 3) << 3;
 		value = (value >> shift) | (value << (32 - shift));
 	}

 	return value;
 }
 
u32 read16(u32 address)
 {
 #ifdef GBA_LOGGING
 	if (address & 1)
	{
		if (systemVerbose & VERBOSE_UNALIGNED_MEMORY)
		{
			log("Unaligned word read: %08x at %08x\n", address, CPU::armMode ?
			    CPU::armNextPC - 4 : CPU::armNextPC - 2);
		}
	}
#endif

	// Reads must be 16 bits aligned
	u16 value = memMap[address >> 24].read16(address & 0xFFFFFFFE);

	if (address & 1)
	{
		value = (value >> 8) | (value << 24);
	}

	return value;
}

u8 read8(u32 address)
{
	return memMap[address >> 24].read8(address);
}

void write32(u32 address, u32 value)
{
	memMap[address >> 24].write32(address, value);
}

void write16(u32 address, u16 value)
{
	memMap[address >> 24].write16(address, value);
}

void write8(u32 address, u8 b)
{
	memMap[address >> 24].write8(address, b);
}

template<class T>
static T readGeneric(u32 address)
{
	int segment = address >> 24;
	u32 mask = memMap[segment].mask;

	return readLE<T>(&memMap[segment].mem[address & mask]);
}

template<class T, int mask>
static T readBios(u32 address)
{
	T value;

	if (CPU::reg[15].I >> 24)
	{
		// TODO: proper handling of reading unreadable bios memory, without "biosProtected"
		if (address < 0x4000)
		{
			value = readLE<T>(&biosProtected[address & mask]);
		}
		else
		{
			value = unreadable<T>(address);
		}
	}
	else
		value = readGeneric<T>(address);

	return value;
}

static u8 readIo8(u32 address)
{
	if ((address < 0x4000400) && ioReadable[address & 0x3FF])
	{
		return readGeneric<u8>(address);
	}
	else
	{
		return unreadable<u8>(address);
	}
}

static u16 readIo16(u32 address)
{
	u16 value;

	if ((address < 0x4000400) && ioReadable[address & 0x3FF])
	{
		value = readGeneric<u16>(address);
		if (((address & 0x3fe)>0xFF) && ((address & 0x3fe)<0x10E))
		{
			if (((address & 0x3fe) == 0x100) && timer0On)
				value = 0xFFFF - ((timer0Ticks-cpuTotalTicks) >> timer0ClockReload);
			else
				if (((address & 0x3fe) == 0x104) && timer1On && !(TM1CNT & 4))
					value = 0xFFFF - ((timer1Ticks-cpuTotalTicks) >> timer1ClockReload);
				else
					if (((address & 0x3fe) == 0x108) && timer2On && !(TM2CNT & 4))
						value = 0xFFFF - ((timer2Ticks-cpuTotalTicks) >> timer2ClockReload);
					else
						if (((address & 0x3fe) == 0x10C) && timer3On && !(TM3CNT & 4))
							value = 0xFFFF - ((timer3Ticks-cpuTotalTicks) >> timer3ClockReload);
		}
	}
	else
	{
		value = unreadable<u16>(address);
	}

	return value;
}

static u32 readIo32(u32 address)
{
	u32 value;

	if ((address < 0x4000400) && ioReadable[address & 0x3FF])
	{
		if (ioReadable[(address & 0x3FF) + 2])
			value = readGeneric<u32>(address);
		else
			value = readGeneric<u16>(address);
	}
	else
	{
		value = unreadable<u32>(address);
	}

	return value;
}

template<class T>
static T readVRAM(u32 address)
{
	address &= 0x1FFFF;

	if (((DISPCNT & 7) > 2) && ((address & 0x1C000) == 0x18000))
	{
		return 0;
	}
	if ((address & 0x18000) == 0x18000)
	{
		address &= 0x17FFF;
	}

	return readGeneric<T>(6 << 24 | address);
}

u16 read16s(u32 address)
{
	u16 value = read16(address);
	if ((address & 1))
		value = (s8)value;
	return value;
}

static void OldCPUWriteMemory(u32 address, u32 value)
{

#ifdef GBA_LOGGING
	if (address & 3)
	{
		if (systemVerbose & VERBOSE_UNALIGNED_MEMORY)
		{
			log("Unaligned word write: %08x to %08x from %08x\n",
			    value,
			    address,
			    CPU::armMode ? CPU::armNextPC - 4 : CPU::armNextPC - 2);
		}
	}
#endif

	switch (address >> 24)
	{
	case 0x02:
		WRITE32LE(((u32 *)&workRAM[address & 0x3FFFC]), value);
		break;
	case 0x03:
		WRITE32LE(((u32 *)&internalRAM[address & 0x7ffC]), value);
		break;
	case 0x04:
		if (address < 0x4000400)
		{
			CPUUpdateRegister((address & 0x3FC), value & 0xFFFF);
			CPUUpdateRegister((address & 0x3FC) + 2, (value >> 16));
		}
		else goto unwritable;
		break;
	case 0x05:
		WRITE32LE(((u32 *)&paletteRAM[address & 0x3FC]), value);
		break;
	case 0x06:
		address = (address & 0x1fffc);
		if (((DISPCNT & 7) >2) && ((address & 0x1C000) == 0x18000))
			return;
		if ((address & 0x18000) == 0x18000)
			address &= 0x17fff;
		WRITE32LE(((u32 *)&vram[address]), value);
		break;
	case 0x07:
		WRITE32LE(((u32 *)&oam[address & 0x3fc]), value);
		break;

		// default
	default:
unwritable:
#ifdef GBA_LOGGING
		if (systemVerbose & VERBOSE_ILLEGAL_WRITE)
		{
			log("Illegal word write: %08x to %08x from %08x\n",
			    value,
			    address,
			    CPU::armMode ? CPU::armNextPC - 4 : CPU::armNextPC - 2);
		}
#endif
		break;
	}
}

static void OldCPUWriteHalfWord(u32 address, u16 value)
{
#ifdef GBA_LOGGING
	if (address & 1)
	{
		if (systemVerbose & VERBOSE_UNALIGNED_MEMORY)
		{
			log("Unaligned halfword write: %04x to %08x from %08x\n",
			    value,
			    address,
			    CPU::armMode ? CPU::armNextPC - 4 : CPU::armNextPC - 2);
		}
	}
#endif

	switch (address >> 24)
	{
	case 2:
		WRITE16LE(((u16 *)&workRAM[address & 0x3FFFE]),value);
		break;
	case 3:
		WRITE16LE(((u16 *)&internalRAM[address & 0x7ffe]), value);
		break;
	case 4:
		if (address < 0x4000400)
			CPUUpdateRegister(address & 0x3fe, value);
		else goto unwritable;
		break;
	case 5:
		WRITE16LE(((u16 *)&paletteRAM[address & 0x3fe]), value);
		break;
	case 6:
		address = (address & 0x1fffe);
		if (((DISPCNT & 7) >2) && ((address & 0x1C000) == 0x18000))
			return;
		if ((address & 0x18000) == 0x18000)
			address &= 0x17fff;
		WRITE16LE(((u16 *)&vram[address]), value);
		break;
	case 7:
		WRITE16LE(((u16 *)&oam[address & 0x3fe]), value);
		break;
	default:
unwritable:
#ifdef GBA_LOGGING
		if (systemVerbose & VERBOSE_ILLEGAL_WRITE)
		{
			log("Illegal halfword write: %04x to %08x from %08x\n",
			    value,
			    address,
			    CPU::armMode ? CPU::armNextPC - 4 : CPU::armNextPC - 2);
		}
#endif
		break;
	}
}

static void OldCPUWriteByte(u32 address, u8 b)
{
	switch (address >> 24)
	{
	case 2:
		workRAM[address & 0x3FFFF] = b;
		break;
	case 3:
		internalRAM[address & 0x7fff] = b;
		break;
	case 4:
		if (address < 0x4000400)
		{
			switch (address & 0x3FF)
			{
			case 0x60:
			case 0x61:
			case 0x62:
			case 0x63:
			case 0x64:
			case 0x65:
			case 0x68:
			case 0x69:
			case 0x6c:
			case 0x6d:
			case 0x70:
			case 0x71:
			case 0x72:
			case 0x73:
			case 0x74:
			case 0x75:
			case 0x78:
			case 0x79:
			case 0x7c:
			case 0x7d:
			case 0x80:
			case 0x81:
			case 0x84:
			case 0x85:
			case 0x90:
			case 0x91:
			case 0x92:
			case 0x93:
			case 0x94:
			case 0x95:
			case 0x96:
			case 0x97:
			case 0x98:
			case 0x99:
			case 0x9a:
			case 0x9b:
			case 0x9c:
			case 0x9d:
			case 0x9e:
			case 0x9f:
				soundEvent(address&0xFF, b);
				break;
			case 0x301: // HALTCNT, undocumented
				if (b == 0x80)
					stopState = true;
				holdState = 1;
				cpuNextEvent = cpuTotalTicks;
				break;
			default: // every other register
				u32 lowerBits = address & 0x3fe;
				if (address & 1)
				{
					CPUUpdateRegister(lowerBits, (READ16LE(&ioMem[lowerBits]) & 0x00FF) | (b << 8));
				}
				else
				{
					CPUUpdateRegister(lowerBits, (READ16LE(&ioMem[lowerBits]) & 0xFF00) | b);
				}
			}
			break;
		}
		else goto unwritable;
		break;
	case 5:
		// no need to switch
		*((u16 *)&paletteRAM[address & 0x3FE]) = (b << 8) | b;
		break;
	case 6:
		address = (address & 0x1fffe);
		if (((DISPCNT & 7) >2) && ((address & 0x1C000) == 0x18000))
			return;
		if ((address & 0x18000) == 0x18000)
			address &= 0x17fff;

		// no need to switch
		// byte writes to OBJ VRAM are ignored
		if ((address) < objTilesAddress[((DISPCNT&7)+1)>>2])
		{
			*((u16 *)&vram[address]) = (b << 8) | b;
		}
		break;
	case 7:
		// no need to switch
		// byte writes to OAM are ignored
		//    *((u16 *)&oam[address & 0x3FE]) = (b << 8) | b;
		break;
		// default
	default:
unwritable:
#ifdef GBA_LOGGING
		if (systemVerbose & VERBOSE_ILLEGAL_WRITE)
		{
			log("Illegal byte write: %02x to %08x from %08x\n",
			    b,
			    address,
			    CPU::armMode ? CPU::armNextPC - 4 : CPU::armNextPC -2 );
		}
#endif
		break;
	}
}

} // namespace MMU
