#pragma once

#include "Card.h"

#include <vector>

class SlirpNet;

struct Socket
{
  uint16_t transmitBase;
  uint16_t transmitSize;
  uint16_t receiveBase;
  uint16_t receiveSize;
  uint16_t registers;

  uint16_t sn_rx_wr;
  uint16_t sn_rx_rsr;

  uint8_t sn_sr;

  int myFD;
  int myErrno;

  void clearFD();
  void setFD(const int fd, const int status);
  void process();

  bool isThereRoomFor(const size_t len, const size_t header) const;
  uint16_t getFreeRoom() const;

  Socket();

  ~Socket();
};

class Uthernet2 : public Card
{
public:
  Uthernet2(UINT slot);

  virtual void InitializeIO(LPBYTE pCxRomPeripheral);
  virtual void Init();
  virtual void Reset(const bool powerCycle);
  virtual void Update(const ULONG nExecutedCycles);
  virtual void SaveSnapshot(YamlSaveHelper& yamlSaveHelper);
  virtual bool LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT version);

  BYTE IO_C0(WORD programcounter, WORD address, BYTE write, BYTE value, ULONG nCycles);

private:
  std::vector<uint8_t> myMemory;
  std::vector<Socket> mySockets;
  uint8_t myModeRegister;
  uint16_t myDataAddress;
  std::shared_ptr<SlirpNet> mySlirp;

  void setSocketModeRegister(const size_t i, const uint16_t address, const uint8_t value);
  void setTXSizes(const uint16_t address, uint8_t value);
  void setRXSizes(const uint16_t address, uint8_t value);
  uint16_t getTXDataSize(const size_t i) const;
  uint8_t getTXFreeSizeRegister(const size_t i, const size_t shift) const;
  uint8_t getRXDataSizeRegister(const size_t i, const size_t shift) const;
  
  void receiveOnePacketMacRaw(const size_t i);
  void receiveOnePacketFromSocket(const size_t i);
  void receiveOnePacket(const size_t i);
  
  void sendDataMacRaw(const size_t i, const std::vector<uint8_t> & data) const;
  void sendDataToSocket(const size_t i, std::vector<uint8_t> & data);
  void sendData(const size_t i);
  
  void resetRXTXBuffers(const size_t i);
  void updateRSR(const size_t i);

  void openSystemSocket(const size_t i, const int type, const int protocol, const int state);
  void openSocket(const size_t i);
  void closeSocket(const size_t i);
  void connectSocket(const size_t i);

  void setCommandRegister(const size_t i, const uint8_t value);

  uint8_t readSocketRegister(const uint16_t address);
  uint8_t readValueAt(const uint16_t address);

  void autoIncrement();
  uint8_t readValue();

  void setIPProtocol(const size_t i, const uint16_t address, const uint8_t value);
  void setIPTypeOfService(const size_t i, const uint16_t address, const uint8_t value);
  void setIPTTL(const size_t i, const uint16_t address, const uint8_t value);
  void writeSocketRegister(const uint16_t address, const uint8_t value);

  void setModeRegister(const uint16_t address, const uint8_t value);
  void writeCommonRegister(const uint16_t address, const uint8_t value);
  void writeValueAt(const uint16_t address, const uint8_t value);
  void writeValue(const uint8_t value);

  void processEvents(uint32_t timeout);
};