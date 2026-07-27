#include <StdAfx.h>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstring>

#include "YamlHelper.h"
#include "DummySmartport.h"

#include "Core.h"
#include "Interface.h"
#include "../Registry.h"

#include "Memory.h"
#include "CPU.h"
#include "Log.h"
#include "../resource/resource.h"

const std::string& DummySmartport::GetSnapshotCardName()
{
	static const std::string name("DummySmartport");
	LogFileOutput("DummySmartport: GetSnapshotCardName()\n");
	return name;
}

DummySmartport::DummySmartport(const UINT slot) : Card(CT_DummySmartport, slot)
{
	LogFileOutput("DummySmartport: Constructor for slot %d\n", slot);
}

DummySmartport::~DummySmartport()
{
	LogFileOutput("DummySmartport: Destructor\n");
}

void DummySmartport::Destroy()
{
	LogFileOutput("DummySmartport: Destroy()\n");
}

void DummySmartport::Reset(const bool powerCycle)
{
	LogFileOutput("DummySmartport: Reset(powerCycle=%d)\n", powerCycle);
}

void DummySmartport::Update(const ULONG nExecutedCycles)
{
	// Uncomment if you want to see this called frequently
	// LogFileOutput("DummySmartport: Update(cycles=%lu)\n", nExecutedCycles);
}

BYTE __stdcall dspC0Handler(const WORD programCounter, const WORD address, const BYTE write, const BYTE value, const ULONG nCycles)
{
	const UINT uSlot = ((address & 0xf0) >> 4) - 8;
	if (uSlot < 8)
	{
		auto* pCard = static_cast<DummySmartport*>(MemGetSlotParameters(uSlot));
		if (write)
		{
			return pCard->io_write0(programCounter, address, value, nCycles);
		}
	}
	return 0;
}

void DummySmartport::InitializeIO(LPBYTE pCxRomPeripheral)
{
	LogFileOutput("DummySmartport: InitializeIO()\n");

	// Load firmware into chosen slot - the SP Over SLIP firmware is generic enough to trigger on prodos or smartport activation
	// and will simply call the card with magic bytes ($65 or $66) at Cn02
	const BYTE *pData = GetFrame().GetResource(IDR_SPOVERSLIP_FW, "FIRMWARE", APPLE_SLOT_SIZE);
	if (pData == nullptr)
		return;
	std::memcpy(pCxRomPeripheral + m_slot * APPLE_SLOT_SIZE, pData, APPLE_SLOT_SIZE);

	// Set locations in firmware that need to know the slot number for reading/writing to the card
	const BYTE locN16a = pData[0xF6];		  // location of where to put slot * 16 #1
	const BYTE locN16b = pData[0xF7];		  // location of where to put slot * 16 #2
	const BYTE locCN1 = pData[0xF8];		  // location of where to put $CN
	const BYTE locCN2 = pData[0xF9];		  // location of where to put $CN
	const BYTE locCN3 = pData[0xFA];		  // location of where to put $CN
	const BYTE cn = ((m_slot & 0xff) | 0xC0); // slot(n) || 0xC0 = 0xCn

	const BYTE locN2 = pData[0xFB];											 // location of where to put $n2
	const BYTE n2 = static_cast<BYTE>(((m_slot & 0xff) | 0x08) << 4) + 0x02; // (slot + 8) * 16 + 2, giving low byte of $C0n2

	// Modify the destination memory to hold the slot information needed by the firmware.
	// The pData memory is R/O, and we get an access violation writing to it, so alter the destination instead.
	const LPBYTE pDest = pCxRomPeripheral + m_slot * APPLE_SLOT_SIZE;
	pDest[locN16a] = (m_slot & 0xff) * 16;
	pDest[locN16b] = (m_slot & 0xff) * 16;
	pDest[locCN1] = cn;
	pDest[locCN2] = cn;
	pDest[locCN3] = cn;
	pDest[locN2] = n2;


	RegisterIoHandler(m_slot, nullptr, dspC0Handler, nullptr, nullptr, this, nullptr);
}

void DummySmartport::SaveSnapshot(YamlSaveHelper& yamlSaveHelper)
{
	LogFileOutput("DummySmartport: SaveSnapshot()\n");
}

bool DummySmartport::LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT version)
{
	LogFileOutput("DummySmartport: LoadSnapshot(version=%d)\n", version);
	return true;
}

void DummySmartport::handle_prodos_call()
{
	// See https://prodos8.com/docs/techref/adding-routines-to-prodos/
	// $42 = COMMAND:
	//   0 = STATUS
	//   1 = READ
	//   2 = WRITE
	//   3 = FORMAT

	// $43 = UNIT NUMBER, bits 4,5,6 = SLOT, if bit 8 = 0, D1, if bit 8 = 1, D2
	// $44,45 = BUFFER POINTER
	// $46,47 = BLOCK NUMBER

	// Error codes (returned in A)
	// $27 = I/O error
	// $28 = No device Connected
	// $2B = Write Protected

	const uint8_t drive_num = (mem[0x43] & 0x80) == 0 ? 1 : 2;
	const uint8_t slot_num = (mem[0x43] & 0x70) >> 4;
	const uint8_t command = mem[0x42];

	LogFileOutput("SmartPortOverSlip ProDOS drive_num: %d, slot_num: %d, command: %d\n", drive_num, slot_num, command);
	regs.a = 0x28;
	regs.x = 0;
	regs.y = 0;
	return;
}

void DummySmartport::device_count(const WORD sp_payload_loc)
{
	// Fill the status information directly into SP payload memory.
	// We will simply advertise no devices on this dummy smartport card
	const BYTE deviceCount = 0;
	mem[sp_payload_loc] = deviceCount;
	mem[sp_payload_loc + 1] = 1 << 6; // no interrupt
	mem[sp_payload_loc + 2] = 0x4D;	  // 0x4D46 == MF for vendor ID
	mem[sp_payload_loc + 3] = 0x46;
	mem[sp_payload_loc + 4] = 0x0A; // version 1.00 Alpha = $100A
	mem[sp_payload_loc + 5] = 0x10;
	int dirty_page_start = (sp_payload_loc & 0xFF00) >> 8;
	int dirty_page_end = ((sp_payload_loc + 5) & 0xFF00) >> 8;
	memdirty[dirty_page_start] = 0xFF;
	memdirty[dirty_page_end] = 0xFF;

	regs.a = 0;
	regs.x = 6;
	regs.y = 0;
	set_processor_status(AF_ZERO);
}

void DummySmartport::handle_smartport_call()
{
	// stack pointer location holds the data we need to service this request
	WORD rts_location = static_cast<WORD>(mem[regs.sp + 1]) + static_cast<WORD>(mem[regs.sp + 2] << 8);
	const BYTE command = mem[rts_location + 1];
	const WORD cmd_list_loc = mem[rts_location + 2] + (mem[rts_location + 3] << 8);
	const BYTE param_count = mem[cmd_list_loc];
	const BYTE unit_number = mem[cmd_list_loc + 1];
	const WORD sp_payload_loc = static_cast<WORD>(mem[cmd_list_loc + 2]) + static_cast<WORD>(mem[cmd_list_loc + 3] << 8);
	const WORD params_loc = cmd_list_loc + 2; // we are ONLY skipping the count and destination bytes (used to skip the payload bytes, but that's only certain commands)

	// Fix the stack so the RTS in the firmware returns to the instruction after the data
	rts_location += 3;
	mem[regs.sp + 1] = rts_location & 0xff;
	mem[regs.sp + 2] = (rts_location >> 8) & 0xff;

	int dirty_page_start = ((regs.sp + 1) & 0xFF00) >> 8;
	int dirty_page_end = ((regs.sp + 2) & 0xFF00) >> 8;
	memdirty[dirty_page_start] = 0xFF;
	memdirty[dirty_page_end] = 0xFF;

	// Deal with status call (command == 0) with params unit == 0, status_code == 0 to return device count, doesn't need connection details.
	if (command == 0 && unit_number == 0 && mem[params_loc + 2] == 0)
	{
		device_count(sp_payload_loc);
		return;
	}

	// everything else just return everything ok
	regs.a = 1;
	regs.x = 0;
	regs.y = 0;
	unset_processor_status(AF_ZERO);
	return;

}

BYTE DummySmartport::io_write0(WORD programCounter, WORD address, BYTE value, ULONG nCycles)
{
	const uint8_t loc = address & 0x0f;
	// SP = $65 in $02
	if (value == 0x65 && loc == 0x02)
	{
		handle_smartport_call();
	}
	// ProDos = $66 in $02
	else if (value == 0x66 && loc == 0x02)
	{
		handle_prodos_call();
	}
	return 0;
} 