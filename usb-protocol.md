# USB communication protocol for EM100Pro

## Endpoints used:

| Endpoint    | Direction | type | size      |
|-------------|-----------|------|-----------|
| Endpoint 1  | output    | bulk | 512 bytes |
| Endpoint 2  | input     | bulk | 512 bytes |

*USB command output length is always 16B;*

## 1. System Level Operations

### 1.1. Get Firmware / FPGA version

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x10    | None      | None |

USB In:

| Result | Data                        |
|--------|-----------------------------|
| DCNT   | FPGA_Ver (2B), MCU_Ver (2B) |

```
DCNT: Data Count; 0 if fail, 4 if pass
FPGA_Ver: FPGA version
MCU_Ver: MCU version
```

### 1.2. Set Voltage

USB Out:

| Command | Parameter    | Data       |
|---------|--------------|------------|
| 0x11    | Channel (1B) | Value (2B) |

USB In:

```NONE```

```
Channel:
0:    Trig VCC
1:    RST_VCC
2:    REF+
3:    REF-
4:    Buffer VCC
```

Value:

The value unit is 1mV. For Buffer VCC, there’s only three steps: 1.8V 2.5V and 3.3V

### 1.3. Measure Voltage

USB Out:

| Command | Parameter    | Data |
|---------|--------------|------|
| 0x12    | Channel (1B) | None |

USB In:

| Result | Data       |
|--------| -----------|
| DCNT   | Value (2B) |

```
Channel:
0:    1.2V
1:    E_VCC
2:    REF+
3:    REF-
4:    Buffer VCC
5:    Trig VCC
6:    RST VCC
7:    3.3V
8:    Buffer 3.3V
9:    5V
```

```
DCNT: Data count; 0 if fail, 2 if pass
```

Value:

### 1.4. Set LED

USB Out:

| Command | Parameter      | Data |
|---------|----------------|------|
| 0x13    | LED State (1B) | None |

USB In:

```NONE```

```
LED State:
0: Both off
1: Green on
2: Red on
3: Both on
```

## 2. FPGA related operations

### 2.1. Reconfig FPGA

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x20    | None      | None |

USB In:

```NONE```

```
Note: Please wait 2 seconds to issue any other USB commands after this command
```


### 2.2. Check FPGA configuration status

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x21    | None      | None |

USB In:

| Result    | Data |
|-----------|------|
| Pass/Fail | None |


```NONE```

```
Pass: 1, Fail: 0
```

### 2.3. Read FPGA Registers

USB Out:

| Command | Parameter    | Data |
|---------|--------------|------|
| 0x22    | RegAddr (1B) | None |

USB In:

| Result | Data       |
|--------|------------|
| DCNT   | Value (2B) |

```
RegAddress: Register Address
DCNT: Data count, 0: read fail, 2: read success
Value: Register Value
```

### 2.4. Reconfigure FPGA

USB Out:

| Command | Parameter   | Data      |
|---------|-------------|-----------|
| 0x23    | RegAddr(1B) | Value(2B) |

USB In:

```NONE```

```
RegAddr: Register Address
Value: Register Value
```

See chapter 2.6 for more information on FPGA commands.

### 2.5. Switch FPGA configuration image

```EM100Pro-G2 only```

EM100Pro-G2 has two FPGA images, one for 1.8V and one for 3.3V SPI flash devices.
The EM100Pro-G2 defaults to the 3.3V image on power-on.

USB command to switch between the two images.

USB Out:

| Command | Parameter                 | Data |
|---------|---------------------------|------|
| 0x24    | Address (4B, high to low) | None |

USB In:

```NONE```

```
Address = 0x00000: Switch to 3.3V image
Address = 0x78000: Switch to 1.8V image
```

After sending the `Switch FPGA configuration image` command, check bit 15 of firmware version (DWORD):

```
0: 3.3V
1: 1.8V
```

Minimum firmware version requirements:

Device      | MCU version | FPGA version |
------------|-------------|--------------|
EM100Pro-G2 | 3.3         | 2.014        |


### 2.6. FPGA Registers

Description of some FPGA command registers (incomplete). Each register
has a 1 byte address and is 2 bytes wide (referred to as a0, a1). This includes
some information of the former file protocol-notes.txt. Please help complete this
information.

#### 2.6.1. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x32    | ?/W | ??                        |

Written through the config files for each emulated chip.
Data written is always observed as 0xff 0xff (?)

#### 2.6.2. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x38    | ?/W | ??                        |

Written through the config files for each emulated chip.
Data written is various values (?)

#### 2.6.3. Set Chip Size

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x3a    | ?/W | Set Chip Size             |

Written through the config files for each emulated chip.
Sets chip size to (a1+1) * 0x10000 (?)

#### 2.6.4. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x40    | ?/W | ??                        |

Written through the config files for each emulated chip.
sometimes: a0 = lowbyte of device id (for 16bit did?), a1 = device id (for 8bit
did)

#### 2.6.5. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x42    | ?/W | ??                        |

Written through the config files for each emulated chip.
a0 = vendor id, a1 = highbyte of device id for 16bit did or device id for 8bit did (?)

#### 2.6.6. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x44    | ?/W | ??                        |

Written through the config files for each emulated chip.
Always 0x03 0x18 (?)

#### 2.6.7. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x46    | ?/W | ??                        |

Written through the config files for each emulated chip.
a0 = various, a1 = always 0x30? (?)

#### 2.6.8. ????

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x48    | ?/W | ??                        |

Written through the config files for each emulated chip.
a0 = various, a1 = always 0xc0? (?)

#### 2.6.9. Default Address Length Setting

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0x4F    | WO  | Set default address space |

| Bit | Description                                    | Initial value |
|-----|------------------------------------------------|---------------|
| 0   | Command address length: 0: 3 bytes, 1: 4 bytes | 0             |

Minimum firmware version requirements:

Device      | MCU version | FPGA version |
------------|-------------|--------------|
EM100Pro    | 2.27        | 0.091        |
EM100Pro-G2 | 3.3         | 2.014        |

#### 2.6.10. CS Pin Selection

FPGA command:

| Address | R/W | Description               |
|---------|-----|---------------------------|
| 0xE0    | WO  | Set default address space |

| Bit | Description                  | Initial value |
|-----|------------------------------|---------------|
| 0   | 0: Select CS1, 1: Select CS2 | 0             |


Note:
CS selection has different internal behavior on EM100Pro and EM100Pro-G2. On
EM100Pro, switching to CS2 will use internal memory starting at 0x80000000. On
EM100Pro-G2, only the CS pin is switched. Since  the memory space is shared, only
one chip can be emulated at a time.


Minimum firmware version requirements:

Device      | MCU version | FPGA version |
------------|-------------|--------------|
EM100Pro-G2 | 3.3         | 2.014        |


## 3. SPI flash related operations

### 3.1. Get SPI flash ID

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x30    | None      | None |

USB In:

| Result | Data   |
|--------|--------|
| None   | ID(3B) |

```ID: SPI flash ID```

### 3.2. Erase SPI flash

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x31    | None      | None |

USB In:

```None```

```Note: please don’t send any other USB command after this command in 5 seconds```

### 3.3. Poll SPI flash status

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x32    | None      | None |

USB In:

| Result | Data |
|--------|------|
| Status | None |


```Status: 0 if busy, 1 if ready```

### 3.4. Read SPI flash Page

USB Out:

| Command | Parameter    | Data |
|---------|--------------|------|
| 0x33    | Address (3B) | None |

USB In:

| Result | Data                  |
|--------|-----------------------|
| None   | SPI flash data (256B) |

### 3.5. Program SPI flash Page

USB Out for command:

| Command | Parameter    | Data |
|---------|--------------|------|
| 0x34    | Address (3B) | None |

USB Out for data:

| Data         |
|------------- |
| 256Byte Data |

### 3.6. ?? Unprotect SPI flash ??

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0x36    | None      | None |

USB In:

```NONE```

### 3.6. Erase SPI flash sector:

USB Out:

| Command | Parameter    | Data |
|---------|--------------|------|
| 0x37    | SectorNo(1B) | None |

USB In:

```NONE```

```
SectorNo: Sector number (0..31). Sectors are 64k
```


## 4. SDRAM related operations:

### 4.1. Write SDRAM

USB Out for command:

| Command | Parameter                 | Data |
|---------|---------------------------|------|
| 0x40    | Address (4B), Length (4B) | None |

USB Out for data:

| Data                        |
|-----------------------------|
| Data to be written to SDRAM |

### 4.2. Read SDRAM

USB Out:

| Command | Parameter                 | Data |
|---------|---------------------------|------|
| 0x41    | Address (4B), Length (4B) | None |

USB In:

| Result | Data                |
|--------|---------------------|
| None   |Data read from SDRAM |


## 5. SPI Hyperterminal related operations

### 5.1. Read HT Registers

USB Out:

|Command | Parameter    | Data |
|--------|--------------|------|
| 0x50   | RegAddr (1B) |      |

USB In:

| Result | Data       |
|--------|------------|
| DCNT   | Value (1B) |

```
RegAddress: Register Address
DCNT: Data count, 0: read fail, 1: read success
Value: Register Value
```

### 5.2. Write HT Registers

USB Out:

| Command | Parameter    | Data      |
|---------|--------------|-----------|
| 0x51    | RegAddr (1B) | Value(1B) |

USB In:

```None```

```
RegAddress: Register Address
Value: Register Value
```

### 5.3. Write dFIFO

USB Out for Command:

| Command | Parameter                 | Data |
|---------|---------------------------|------|
| 0x52    | DataLen (2B), Timeout(2B) | None |

USB Out for data:

| Data                        |
|-----------------------------|
| Data to be written to dFIFO |

USB In:

| Result | Data |
|--------|------|
| DCNT   | None |

```
DataLen: length of data to be written to dFIFO, do not set this value larger than 512.
Timeout: timeout for MCU to write these data to the FPGA dFIFO, unit is ms.
DCNT: number of bytes successfully written to dFIFO.
```

### 5.4. Read uFIFO

USB Out for Command:

| Command | Parameter                | Data |
|---------|--------------------------|------|
| 0x53    | Dataen (2B), Timeout(2B) | None |

USB In:

| Result | Data       |
|--------|------------|
|DCNT    | uFIFO data |

```
DataLen: length of data to be read from uFIFO, do not set this value larger than 512.
Timeout: timeout for MCU to read data from the FPGA uFIFO, unit is ms.
DCNT: number of bytes successfully read from uFIFO.
```

## 6. SPI Trace related operations

### 6.1. Get SPI Trace

USB Out:

|Command| Parameter                                   | Data |
|-------|---------------------------------------------|------|
| 0xBC  | TraceLen(4B), Timeout (4B), TraceConfig(1B) | None |

USB In:

| Result | Data      |
|--------|-----------|
| None   | SPI Trace |

```
 TraceLen: The unit of TraceLen is 4KB.
 Timeout: It defines the max waiting time on MCU side for each 4KB trace data, unit is ms.
 TraceConfig:
  Bit[1:0]
    00: start/stop SPI Trace according to EM100 emulation status;
    01: start when TraceConfig[2]==1;
    10: start when Trig signal goes high;
    11: RFU
  Bit[2]:
     When TraceConfig[1:0[==01, this bit starts the trace.
  Bit[7:3]: RFU
```

The MCU will try to read SPI trace data from FPGA and resend the data to PC. The data length of each transfer is 4KB. If the MCU can not collect 4KB data from FPGA in the time defined by Timeout value, it will stop waiting and padding the 4KB data with all 00 and send it to USB.


### 6.2. Reset SPI Trace

USB Out:

| Command | Parameter | Data |
|---------|-----------|------|
| 0xBD    | None      | None |

USB In:

| Result | Data      |
|--------|-----------|
| None   | SPI Trace |
