/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 2022, Andrea Odetti

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

// ReSharper disable CppInconsistentNaming

#include <StdAfx.h>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>

#include "YamlHelper.h"
#include "SmartPortOverSlip.h"

#include "Core.h"
#include "Interface.h"
#include "../Registry.h"

#include "Memory.h"
#include "CPU.h"
#include "Log.h"
#include "../resource/resource.h"

#include "devrelay/commands/Close.h"
#include "devrelay/commands/Control.h"
#include "devrelay/commands/Format.h"
#include "devrelay/commands/Init.h"
#include "devrelay/commands/Open.h"
#include "devrelay/commands/ReadBlock.h"
#include "devrelay/commands/Read.h"
#include "devrelay/commands/Status.h"
#include "devrelay/commands/WriteBlock.h"
#include "devrelay/commands/Write.h"

#include "devrelay/service/Requestor.h"

int SmartPortOverSlip::active_instances = 0;

const std::string &SmartPortOverSlip::GetSnapshotCardName()
{
	static const std::string name("SmartPortOverSlip");
	LogFileOutput("SmartPortOverSlip Returning name SmartPortOverSlip\n");
	return name;
}

SmartPortOverSlip::SmartPortOverSlip(const UINT slot) : Card(CT_SmartPortOverSlip, slot)
{
	if (active_instances > 0)
	{
		throw std::runtime_error("There is an existing slot active for SP over SLIP. You can only have 1 of these cards active.");
	}
	active_instances++;

	LogFileOutput("SmartPortOverSlip ctor, slot: %d\n", slot);
}

SmartPortOverSlip::~SmartPortOverSlip()
{
	active_instances--;
	LogFileOutput("SmartPortOverSlip destructor\n");
}

void SmartPortOverSlip::Reset(const bool powerCycle)
{
	LogFileOutput("SmartPortOverSlip Bridge Initialization, reset called\n");
}

void SmartPortOverSlip::handle_smartport_call()
{
	// stack pointer location holds the data we need to service this request
	WORD rts_location = static_cast<WORD>(mem[regs.sp + 1]) + static_cast<WORD>(mem[regs.sp + 2] << 8);
	const BYTE command = mem[rts_location + 1];
	const WORD cmd_list_loc = mem[rts_location + 2] + (mem[rts_location + 3] << 8);
	const BYTE param_count = mem[cmd_list_loc];
	const BYTE unit_number = mem[cmd_list_loc + 1];
	const WORD sp_payload_loc = static_cast<WORD>(mem[cmd_list_loc + 2]) + static_cast<WORD>(mem[cmd_list_loc + 3] << 8);
	const WORD params_loc = cmd_list_loc + 2; // we are ONLY skipping the count and destination bytes (used to skip the payload bytes, but that's only certain commands)

	// LogFileOutput("SmartPortOverSlip processing SP command: 0x%02x, unit: "
	//	"0x%02x, cmdList: 0x%04x, spPayLoad: 0x%04x, p1: 0x%02x\n",
	//	command, unit_number, cmd_list_loc, sp_payload_loc, mem[params_loc]);

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

	const auto id_and_connection = GetCommandListener().find_connection_with_device(unit_number);
	if (id_and_connection.second == nullptr)
	{
		regs.a = 1;
		regs.x = 0;
		regs.y = 0;
		unset_processor_status(AF_ZERO);
		return;
	}

	const auto device_id = id_and_connection.first;
	const auto connection = id_and_connection.second.get();

	switch (command)
	{
	case CMD_STATUS:
		status_sp(device_id, connection, sp_payload_loc, param_count, params_loc); // we are including the payload params and count part now too
		break;
	case CMD_READ_BLOCK:
		// TODO: fix the fact params_loc has changed from +4 to +2
		read_block(device_id, connection, sp_payload_loc, param_count, params_loc + 2);
		break;
	case CMD_WRITE_BLOCK:
		// TODO: fix the fact params_loc has changed from +4 to +2
		write_block(device_id, connection, sp_payload_loc, param_count, params_loc + 2);
		break;
	case CMD_FORMAT:
		format(device_id, connection, param_count);
		break;
	case CMD_CONTROL:
		// TODO: fix the fact params_loc has changed from +4 to +2
		control(device_id, connection, sp_payload_loc, param_count, params_loc);
		break;
	case CMD_INIT:
		init(device_id, connection, param_count);
		break;
	case CMD_OPEN:
		open(device_id, connection, param_count);
		break;
	case CMD_CLOSE:
		close(device_id, connection, param_count);
		break;
	case CMD_READ:
		// TODO: fix the fact params_loc has changed from +4 to +2
		read(device_id, connection, sp_payload_loc, param_count, params_loc + 2);
		break;
	case CMD_WRITE:
		// TODO: fix the fact params_loc has changed from +4 to +2
		write(device_id, connection, sp_payload_loc, param_count, params_loc + 2);
		break;
	default:
		break;
	}
}

void SmartPortOverSlip::handle_prodos_call()
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

	// Let's get the Drive Num and Slot first
	const uint8_t drive_num = (mem[0x43] & 0x80) == 0 ? 1 : 2;
	const uint8_t slot_num = (mem[0x43] & 0x70) >> 4;
	const uint8_t command = mem[0x42];

	if (slot_num != m_slot)
	{
		// not for us... could be mirroring/moving? ignoring for now. see https://www.1000bit.it/support/manuali/apple/technotes/pdos/tn.pdos.20.html
		LogFileOutput("SmartPortOverSlip ProDOS - NOT OUR SLOT! drive_num: %d, slot_num: %d, command: %d\n", drive_num, slot_num, command);
		regs.a = 0x28;
		regs.x = 0;
		regs.y = 0;
		return;
	}

	// The Listener currently holds the information about mappings for device ids to devices.
	// We need to look for the first registered device of all connections to us that are disks, and use first 2 as our drives.

	// we can call the listener to do this for us, and it can cache the results so we can keep calling it
	std::pair<int, int> disk_devices = GetCommandListener().first_two_disk_devices([](const std::vector<uint8_t>& data) -> bool {
		return data.size() >= 22 && (data[21] == 0x01 || data[21] == 0x02 || data[21] == 0x0A) && ((data[0] & 0x10) == 0x10);
	});

	if ((drive_num == 1 && disk_devices.first == -1) || (drive_num == 2 && disk_devices.second == -1))
	{
		// No devices found, pretend it's just an error so ProDOS keeps trying later when we do have disks.
		regs.a = 0x27;
		regs.x = 0;
		regs.y = 0;
		return;
	}

	switch (command)
	{
	case 0x00:
		handle_prodos_status(drive_num, disk_devices);
		break;
	case 0x01:
		handle_prodos_read(drive_num, disk_devices);
		break;
	case 0x02:
		handle_prodos_write(drive_num, disk_devices);
		break;
	case 0x03:
		// format, TBD - send IO error for now
		regs.a = 0x27;
		regs.x = 0;
		regs.y = 0;
		break;
	default:
		// error, unknown command
		LogFileOutput("SmartPortOverSlip ProDOS - UNKNOWN COMMAND! drive_num: %d, slot_num: %d, command: %d\n", drive_num, slot_num, command);
		regs.a = 0x28;
		regs.x = 0;
		regs.y = 0;
		break;
	}
}

std::string getHexValues(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    for(const auto &byte : data) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << ' ';
    }
    return ss.str();
}

void SmartPortOverSlip::handle_prodos_status(uint8_t drive_num, std::pair<int, int> disk_devices)
{
	/*
	From: https://prodos8.com/docs/techref/adding-routines-to-prodos/
	The STATUS call should perform a check to verify that the device is ready for a READ or WRITE. If it is not, the carry should
	be set and the appropriate error code returned in the accumulator. If the device is ready for a READ or WRITE, then the driver
	should clear the carry, place a zero in the accumulator, and return the number of blocks on the device in the
	X-register (low-byte) and Y-register (high-byte).
	*/
	auto device_id = drive_num == 1 ? disk_devices.first : disk_devices.second;
	auto id_connection = GetCommandListener().find_connection_with_device(device_id);
	std::unique_ptr<StatusResponse> response = status_pd(id_connection.first, id_connection.second.get(), 0);
	// the first byte of the data in the status response:
	/*
	  // Bit 7: Block  device
	  // Bit 6: Write allowed
	  // Bit 5: Read allowed
	  // Bit 4: Device online or disk in drive
	  // Bit 3: Format allowed
	  // Bit 2: Media write protected (block devices only)
	  // Bit 1: Currently interrupting (//c only)
	  // Bit 0: Disk switched
	*/
	const auto &status_data = response->get_data();
	if (status_data.size() >= 3)
	{
		// check bits 5/6 set for R/W, and the disk is online bit 4.
		uint8_t c = status_data[0] & 0x70;
		if (c == 0x70)
		{
			// we're good, clear carry, set a = 0, and set x/y
			LogFileOutput("SmartPortOverSlip Prodos Status OK x: %02x, y: %02x\n", status_data[1], status_data[2]);
			regs.a = 0;
			regs.x = status_data[1];
			regs.y = status_data[2];
			unset_processor_status(AF_ZERO);
		}
		else
		{
			LogFileOutput("SmartPortOverSlip Prodos, Status bits did not match 0x70, found: 0x%02x\n", c);
			regs.a = 0x27;
			regs.x = 0;
			regs.y = 0;
		}
	}
	else
	{
		// unknown status bytes returned, return an IO error
		LogFileOutput("SmartPortOverSlip Prodos Status, Bad DIB returned\n");
		std::string hex = getHexValues(status_data);
		LogFileOutput("%s\n", hex.c_str());
		regs.a = 0x27;
		regs.x = 0;
		regs.y = 0;
	}
}

void SmartPortOverSlip::handle_prodos_read(uint8_t drive_num, std::pair<int, int> disk_devices)
{
	// $44-$45 = buffer pointer
	WORD buffer_location = static_cast<WORD>(mem[0x44]) + static_cast<WORD>(mem[0x45] << 8);
	auto device_id = drive_num == 1 ? disk_devices.first : disk_devices.second;
	auto id_connection = GetCommandListener().find_connection_with_device(device_id);

	// Do a ReadRequest, and shove the 512 byte block into the required memory
	ReadBlockRequest request(Requestor::next_request_number(), 3, id_connection.first);
	// $46-47 = Block Number
	request.set_block_number_from_bytes(mem[0x46], mem[0x47], 0);
	auto response = Requestor::send_request(request, id_connection.second.get());

	handle_response<ReadBlockResponse>(
		std::move(response),
		[this, buffer_location](const ReadBlockResponse *rbr) {
			memcpy(mem + buffer_location, rbr->get_block_data().data(), 512);
			int dirty_page_start = (buffer_location & 0xFF00) >> 8;
			memdirty[dirty_page_start] = 0xFF;
			memdirty[dirty_page_start + 1] = 0xFF;
			regs.a = 0;
			regs.x = 0;
			regs.y = 2; // 512 bytes
		},
		0x27);
}

void SmartPortOverSlip::handle_prodos_write(uint8_t drive_num, std::pair<int, int> disk_devices)
{
	// $44-$45 = buffer pointer
	WORD buffer_location = static_cast<WORD>(mem[0x44]) + static_cast<WORD>(mem[0x45] << 8);
	auto device_id = drive_num == 1 ? disk_devices.first : disk_devices.second;
	auto id_connection = GetCommandListener().find_connection_with_device(device_id);

	WriteBlockRequest request(Requestor::next_request_number(), 3, id_connection.first);
	// $46-47 = Block Number
	request.set_block_number_from_bytes(mem[0x46], mem[0x47], 0);
	// put data into the request we're sending
	request.set_block_data_from_ptr(mem, buffer_location);
	//int dirty_page_start = (buffer_location & 0xFF00) >> 8;
	//memdirty[dirty_page_start] = 0xFF;
	//memdirty[dirty_page_start + 1] = 0xFF;
	auto response = Requestor::send_request(request, id_connection.second.get());

	handle_response<WriteBlockResponse>(
		std::move(response),
		[this, buffer_location](const WriteBlockResponse *rbr) {
			regs.a = 0;
			regs.x = 0;
			regs.y = 2; // 512 bytes
		},
		0x27);
}

BYTE SmartPortOverSlip::io_write0(WORD programCounter, const WORD address, const BYTE value, ULONG nCycles)
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

void SmartPortOverSlip::device_count(const WORD sp_payload_loc)
{
	// Fill the status information directly into SP payload memory.
	// The count is from sum of all devices across all Connections.
	const BYTE deviceCount = GetCommandListener().get_total_device_count();
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

void SmartPortOverSlip::read_block(const BYTE unit_number, Connection *connection, const WORD buffer_location, const BYTE params_count, const WORD block_count_address)
{
	ReadBlockRequest request(Requestor::next_request_number(), params_count, unit_number);
	// Assume that (cmd_list_loc + 4 == block_count_address) holds 3 bytes for the block number. If it's in the payload, this is wrong and will have to be fixed.
	request.set_block_number_from_ptr(mem, block_count_address);
	auto response = Requestor::send_request(request, connection);

	handle_response<ReadBlockResponse>(std::move(response), [this, buffer_location](const ReadBlockResponse *rbr) {
		memcpy(mem + buffer_location, rbr->get_block_data().data(), 512);

		int dirty_page_start = (buffer_location & 0xFF00) >> 8;
		memdirty[dirty_page_start] = 0xFF;
		memdirty[dirty_page_start + 1] = 0xFF;

		regs.a = 0;
		regs.x = 0;
		regs.y = 2; // 512 bytes
	});
}

void SmartPortOverSlip::write_block(const BYTE unit_number, Connection *connection, const WORD sp_payload_loc, const BYTE params_count, const WORD params_loc)
{
	WriteBlockRequest request(Requestor::next_request_number(), params_count, unit_number);
	// Assume that (cmd_list_loc + 4 == params_loc) holds 3 bytes for the block number. The payload contains the data to write
	request.set_block_number_from_ptr(mem, params_loc);
	request.set_block_data_from_ptr(mem, sp_payload_loc);

	auto response = Requestor::send_request(request, connection);
	handle_simple_response<WriteBlockResponse>(std::move(response));
}

void SmartPortOverSlip::read(const BYTE unit_number, Connection *connection, const WORD sp_payload_loc, const BYTE params_count, const WORD params_loc)
{
	ReadRequest request(Requestor::next_request_number(), params_count, unit_number);
	request.set_byte_count_from_ptr(mem, params_loc);
	request.set_address_from_ptr(mem, params_loc + 2); // move along by byte_count size. would be better to get its size rather than hard code it here.
	auto response = Requestor::send_request(request, connection);

	handle_response<ReadResponse>(std::move(response), [sp_payload_loc](const ReadResponse *rr) {
		const auto response_size = rr->get_data().size();
		memcpy(mem + sp_payload_loc, rr->get_data().data(), response_size);

		int dirty_page_start = (sp_payload_loc & 0xFF00) >> 8;
		int dirty_page_end = ((sp_payload_loc + response_size) & 0xFF00) >> 8;
		for (int i = dirty_page_start; i <= dirty_page_end; i++) {
			memdirty[i] = 0xFF;
		}

		regs.a = 0;
		regs.x = response_size & 0xff;
		regs.y = (response_size >> 8) & 0xff;
	});
}

void SmartPortOverSlip::write(const BYTE unit_number, Connection *connection, const WORD sp_payload_loc, const BYTE params_count, const WORD params_loc)
{
	WriteRequest request(Requestor::next_request_number(), params_count, unit_number);
	request.set_byte_count_from_ptr(mem, params_loc);
	request.set_address_from_ptr(mem, params_loc + 2); // move along by byte_count size. would be better to get its size rather than hard code it here.
	const auto byte_count = request.get_byte_count();
	const auto write_length = byte_count[0] + (byte_count[1] << 8);
	request.set_data_from_ptr(mem, sp_payload_loc, write_length);

	auto response = Requestor::send_request(request, connection);
	handle_simple_response<WriteResponse>(std::move(response));
}

void SmartPortOverSlip::status_sp(const BYTE unit_number, Connection *connection, const WORD sp_payload_loc, const BYTE params_count, const WORD params_loc)
{
	const BYTE status_code = mem[params_loc + 2];
	const BYTE network_unit = params_count > 3 ? mem[params_loc + 3] : 0;
	auto response = status(unit_number, connection, params_count, status_code, network_unit);
	handle_response<StatusResponse>(std::move(response), [sp_payload_loc](const StatusResponse *sr) {
		const auto response_size = sr->get_data().size();
		memcpy(mem + sp_payload_loc, sr->get_data().data(), response_size);

		int dirty_page_start = (sp_payload_loc & 0xFF00) >> 8;
		int dirty_page_end = ((sp_payload_loc + response_size) & 0xFF00) >> 8;
		for (int i = dirty_page_start; i <= dirty_page_end; i++) {
			memdirty[i] = 0xFF;
		}

		regs.a = 0;
		regs.x = response_size & 0xff;
		regs.y = (response_size >> 8) & 0xff;
	});
}

std::unique_ptr<Response> SmartPortOverSlip::status(const BYTE unit_number, Connection *connection, const BYTE params_count, const BYTE status_code, const BYTE network_unit)
{
	// see https://www.1000bit.it/support/manuali/apple/technotes/smpt/tn.smpt.2.html
	const StatusRequest request(Requestor::next_request_number(), params_count, unit_number, status_code, network_unit);
	return Requestor::send_request(request, connection);
}

std::unique_ptr<StatusResponse> SmartPortOverSlip::status_pd(const BYTE unit_number, Connection *connection, const BYTE status_code)
{
	auto response = status(unit_number, connection, status_code, 3, 0);

	// Cast the Response to a StatusResponse. We need to release ownership. As ChatGPT explains:
	/*
	You have a std::unique_ptr<Response> and you want to return a std::unique_ptr<StatusResponse>.
	You can't simply cast the std::unique_ptr<Response> to std::unique_ptr<StatusResponse>, because this would create two std::unique_ptrs
	(the original one and the cast one) that own the same object, which is not allowed.
	*/
	return std::unique_ptr<StatusResponse>(static_cast<StatusResponse *>(response.release()));
}

void SmartPortOverSlip::control(const BYTE unit_number, Connection *connection, const WORD sp_payload_loc, const BYTE params_count, const WORD params_loc)
{
	const BYTE control_code = mem[params_loc + 2]; // skip the payload location bytes
	const BYTE network_unit = params_count > 3 ? mem[params_loc + 3] : 0;
	const auto length = mem[sp_payload_loc] + (mem[sp_payload_loc + 1] << 8) + 2;
	uint8_t *start_ptr = &mem[sp_payload_loc];
	std::vector<uint8_t> payload(start_ptr, start_ptr + length);

	const ControlRequest request(Requestor::next_request_number(), params_count, unit_number, control_code, network_unit, payload);
	auto response = Requestor::send_request(request, connection);
	handle_simple_response<ControlResponse>(std::move(response));
}

void SmartPortOverSlip::init(const BYTE unit_number, Connection *connection, const BYTE params_count)
{
	const InitRequest request(Requestor::next_request_number(), params_count, unit_number);
	auto response = Requestor::send_request(request, connection);
	handle_simple_response<InitResponse>(std::move(response));
}

void SmartPortOverSlip::open(const BYTE unit_number, Connection *connection, const BYTE params_count)
{
	const OpenRequest request(Requestor::next_request_number(), params_count, unit_number);
	auto response = Requestor::send_request(request, connection);
	handle_simple_response<OpenResponse>(std::move(response));
}

void SmartPortOverSlip::close(const BYTE unit_number, Connection *connection, const BYTE params_count)
{
	const CloseRequest request(Requestor::next_request_number(), params_count, unit_number);
	auto response = Requestor::send_request(request, connection);
	handle_simple_response<CloseResponse>(std::move(response));
}

void SmartPortOverSlip::format(const BYTE unit_number, Connection *connection, const BYTE params_count)
{
	const FormatRequest request(Requestor::next_request_number(), params_count, unit_number);
	auto response = Requestor::send_request(request, connection);
	handle_simple_response<FormatResponse>(std::move(response));
}

BYTE __stdcall c0Handler(const WORD programCounter, const WORD address, const BYTE write, const BYTE value, const ULONG nCycles)
{
	const UINT uSlot = ((address & 0xf0) >> 4) - 8;

	if (uSlot < 8)
	{
		auto *pCard = static_cast<SmartPortOverSlip *>(MemGetSlotParameters(uSlot));
		if (write)
		{
			return pCard->io_write0(programCounter, address, value, nCycles);
		}
	}
	return 0;
}

//BYTE __stdcall readC0(const WORD programCounter, const WORD address, const BYTE write, const BYTE value, const ULONG nCycles)
//{
//	LogFileOutput("SmartPortOverSlip readC0, pc: %04x, add: %04x, write: %02x, val: %02x\n", programCounter, address, write, value);
//	return 0;
//}

//BYTE __stdcall readCX(const WORD programCounter, const WORD address, const BYTE write, const BYTE value, const ULONG nCycles)
//{
//	auto v = mem[address];
//	LogFileOutput("SmartPortOverSlip readCX, pc: %04x, add: %04x, v: %02x, write: %02x, val: %02x\n", programCounter, address, v, write, value);
//	return v;
//}

//BYTE __stdcall writeCX(const WORD programCounter, const WORD address, const BYTE write, const BYTE value, const ULONG nCycles)
//{
//	LogFileOutput("SmartPortOverSlip writeCX, pc: %04x, add: %04x, write: %02x, val: %02x\n", programCounter, address, write, value);
//	return 0;
//}

void SmartPortOverSlip::InitializeIO(LPBYTE pCxRomPeripheral)
{
	LogFileOutput("SmartPortOverSlip InitialiseIO\n");

	// Load firmware into chosen slot
	const BYTE *pData = GetFrame().GetResource(IDR_SPOVERSLIP_FW, "FIRMWARE", APPLE_SLOT_SIZE);
	if (pData == nullptr)
		return;

	// Copy the data into the destination memory
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

	RegisterIoHandler(m_slot, nullptr, c0Handler, nullptr, nullptr, this, nullptr);
}

void SmartPortOverSlip::Update(const ULONG nExecutedCycles)
{
	// LogFileOutput("SmartPortOverSlip Update\n");
}

void SmartPortOverSlip::SaveSnapshot(YamlSaveHelper &yamlSaveHelper) { LogFileOutput("SmartPortOverSlip SaveSnapshot\n"); }

bool SmartPortOverSlip::LoadSnapshot(YamlLoadHelper &yamlLoadHelper, UINT version)
{
	LogFileOutput("SmartPortOverSlip LoadSnapshot\n");
	return true;
}

void SmartPortOverSlip::Destroy() {}
