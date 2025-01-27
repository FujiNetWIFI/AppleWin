// ReSharper disable CppInconsistentNaming
#pragma once

#include "Card.h"

#include <memory>

#include "CPU.h"
#include "devrelay/service/Listener.h"
#include "devrelay/types/Response.h"
#include "devrelay/commands/Status.h"

class ControlResponse;
class InitResponse;

class SmartPortOverSlip : public Card
{
public:
	static const std::string &GetSnapshotCardName();

	explicit SmartPortOverSlip(UINT slot);
	~SmartPortOverSlip() override;

	void Destroy() override;
	void InitializeIO(LPBYTE pCxRomPeripheral) override;
	void Reset(bool powerCycle) override;
	void Update(ULONG nExecutedCycles) override;
	void SaveSnapshot(YamlSaveHelper &yamlSaveHelper) override;
	bool LoadSnapshot(YamlLoadHelper &yamlLoadHelper, UINT version) override;

	BYTE io_write0(WORD programCounter, WORD address, BYTE value, ULONG nCycles);
	static void device_count(WORD sp_payload_loc);
	void handle_smartport_call();
	void handle_prodos_call();
	void control(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);
	void init(BYTE unit_number, Connection *connection, BYTE params_count);
	void open(BYTE unit_number, Connection *connection, BYTE params_count);
	void close(BYTE unit_number, Connection *connection, BYTE params_count);
	void format(BYTE unit_number, Connection *connection, BYTE params_count);
	void reset(BYTE unit_number, Connection *connection, BYTE params_count);
	void read_block(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);
	void write_block(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);
	void read(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);
	void write(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);

	std::unique_ptr<Response> status(BYTE unit_number, Connection *connection, BYTE params_count, BYTE status_code, BYTE network_unit);
	std::unique_ptr<StatusResponse> status_pd(BYTE unit_number, Connection *connection, BYTE status_code);
	void status_sp(BYTE unit_number, Connection *connection, WORD sp_payload_loc, BYTE params_count, WORD params_loc);

	void handle_prodos_status(uint8_t drive_num, std::pair<int, int> disk_devices);
	void handle_prodos_read(uint8_t drive_num, std::pair<int, int> disk_devices);
	void handle_prodos_write(uint8_t drive_num, std::pair<int, int> disk_devices);

	static void set_processor_status(const uint8_t flags) { regs.ps |= flags; }
	static void unset_processor_status(const uint8_t flags) { regs.ps &= (0xFF - flags); }
	// if condition is true then set the flags given, else remove them.
	static void update_processor_status(const bool condition, const uint8_t flags) { condition ? set_processor_status(flags) : unset_processor_status(flags); }

	template <typename T>
	void handle_simple_response(const std::unique_ptr<Response> response)
	{
		static_assert(std::is_base_of<Response, T>::value, "T must be a subclass of Response");
		auto specific_response = dynamic_cast<T *>(response.get());

		if (specific_response != nullptr)
		{
			const BYTE status = specific_response->get_status();
			regs.a = status;
			regs.x = 0;
			regs.y = 0;
			update_processor_status(status == 0, AF_ZERO);
		}
		else
		{
			regs.a = 1; // TODO: what error should we return?
			regs.x = 0;
			regs.y = 0;
			unset_processor_status(AF_ZERO);
		}
	}

	template <typename T, typename Func>
	void handle_response(const std::unique_ptr<Response> response, Func call_back, int error_code = 1)
	{
		auto specific_response = dynamic_cast<T *>(response.get());

		if (specific_response != nullptr)
		{
			if (specific_response->get_status() == 0)
			{
				call_back(specific_response);
				set_processor_status(AF_ZERO);
			}
			else
			{
				// An error in the response
				regs.a = specific_response->get_status();
				regs.x = 0;
				regs.y = 0;
				unset_processor_status(AF_ZERO);
			}
		}
		else
		{
			// An error trying to do the request, as there was no response
			regs.a = error_code;
			regs.x = 0;
			regs.y = 0;
			unset_processor_status(AF_ZERO);
		}
	}

private:
	// Ensure no more than 1 card is active as it can cater for all connections to external devices
	static int active_instances;
};