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

### 2.1. Reconfig FPGA:

USB Out:
| Command | Parameter | Data |
|---------|-----------|------|
| 0x20    | None      | None |

USB In:

```NONE```

```
Note: Please wait 2 seconds to issue any other USB commands after this command
```


### 2.2. Check FPGA configuration status:

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
````

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

### 2.4. Reconfig FPGA:

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

## 3. SPI flash related operations:

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


## 5. SPI Hyperterminal related operations:

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

## 6. SPI Trace related operations:

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
