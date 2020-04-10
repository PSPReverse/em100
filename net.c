/*
 * Copyright 2020 Alexander Eichner <alexander.eichner@campus.tu-berlin.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <poll.h>
#include <sys/ioctl.h>

#include "em100.h"


/**
 * Buffered SPI flash program request.
 */
typedef struct SPIPROGREQ
{
    /** Pointer to the next write request in the list. */
    struct SPIPROGREQ           *pNext;
    /** Offset written to. */
    uint32_t                    offProg;
    /** Number of bytes being written. */
    size_t                      cbProg;
    /** Data being written - variable in size. */
    uint8_t                     abData[1];
} SPIPROGREQ;
typedef SPIPROGREQ *PSPIPROGREQ;


/**
 * The network mode state.
 */
typedef struct EM100NET
{
    /** The EM100 device instance data. */
    struct em100            *pEm100;
    /** The current connection socket descriptor */
    int                     iFdSock;
    /** Flag whether the SPI master side has locked the flash for access. */
    uint8_t                 fSpiFlashLocked;
    /** The flash image. */
    void                    *pvBin;
    /** Size of the flash image. */
    size_t                  cbBin;
    /** List of cached writes when the flash is locked. */
    PSPIPROGREQ             pProgHead;
    PSPIPROGREQ             pProgTail;
    /** Flag whether we are in a Flash program command. */
    uint8_t                 fFlashProg;
    /** The offset the write starts at. */
    uint32_t                offFlashProg;
    /** The data being written. */
    void                    *pvFlashProgData;
    /** Number of bytes being written. */
    size_t                  cbFlashProg;
    /** Number of bytes allocated (for growing the buffer). */
    size_t                  cbFlashProgAlloc;
    /** Receive pipe. */
    uint8_t                 abRecv[256];
    /** Where we are in the buffer. */
    size_t                  offRecv;
    /** Set to true when EM100NET::offRecv changed from a 0 value. */
    uint8_t                 fNewDataAvailable;
} EM100NET;

#define SPI_MSG_CHAN_HDR_OFF            0xaab000
#define SPI_MSG_CHAN_AVAIL_OFF          0xaaa000
#define SPI_MSG_CHAN_AVAIL_F_OFF        0xaac000
#define SPI_MSG_CHAN_AVAIL_MAGIC        0x19640522 /* (Dan Brown) */

#define SPI_FLASH_LOCK_OFF              0xaa0000
#define SPI_FLASH_LOCK_UNLOCK_REQ_MAGIC 0x19570528 /* (Frank Schaetzing) */
#define SPI_FLASH_LOCK_UNLOCKED_MAGIC   0x18280208 /* (Jules Verne) */
#define SPI_FLASH_LOCK_LOCK_REQ_MAGIC   0x19380110 /* (Donald E Knuth) */
#define SPI_FLASH_LOCK_LOCKED_MAGIC     0x18990223 /* (Erich Kaestner) */



static int set_state(struct em100 *em100, int run)
{
    return write_fpga_register(em100, 0x28, run & 1);
}


#define REPORT_BUFFER_LENGTH	8192
#define REPORT_BUFFER_COUNT	8
static int read_report_buffer(struct em100 *em100,
    unsigned char reportdata[REPORT_BUFFER_COUNT][REPORT_BUFFER_LENGTH])
{
    unsigned char cmd[16] = {0};
    int len;
    unsigned int report;

    cmd[0] = 0xbc; /* read SPI trace buffer*/

    /*
     * Trace length, unit is 4k according to specs
     *
     * cmd1..cmd4 are probably u32BE on how many
     * reports (8192 bytes each) to fetch
     */
    cmd[1] = 0x00;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = REPORT_BUFFER_COUNT;
    /* Timeout in ms */
    cmd[5] = 0x00;
    cmd[6] = 0x00;
    cmd[7] = 0x00;
    cmd[8] = 0x00;
    /* Trace Config
     * [1:0] 00 start/stop spi trace according to emulation status
     *       01 start when TraceConfig[2] == 1
     *       10 start when trig signal goes high
     *       11 RFU
     * [2]   When TraceConfig[1:0] == 01 this bit starts the trace
     * [7:3] RFU
     */
    cmd[9] = 0x15;

    if (!send_cmd(em100->dev, cmd)) {
        printf("sending trace command failed\n");
        return 0;
    }

    for (report = 0; report < REPORT_BUFFER_COUNT; report++) {
        len = get_response(em100->dev, &reportdata[report][0],
                REPORT_BUFFER_LENGTH);
        if (len != REPORT_BUFFER_LENGTH) {
            printf("error, report length = %d instead of %d.\n\n",
                    len, REPORT_BUFFER_LENGTH);
            return 0;
        }
    }

    return 1;
}

#define MAX_TRACE_BLOCKLENGTH    6
static int spi_trace_process(EM100NET *pThis)
{
    unsigned char reportdata[REPORT_BUFFER_COUNT][REPORT_BUFFER_LENGTH] =
            {{0}};
    unsigned char *data;
    unsigned int count, i, report;
    static int outbytes = 0;
    static int additional_pad_bytes = 0;
    static unsigned int address = 0;
    static unsigned long long timestamp = 0;
    static unsigned long long start_timestamp = 0;
    static struct spi_cmd_values *spi_cmd_vals = NULL;
    static unsigned int counter = 0;
    static unsigned char curpos = 0;
    static unsigned char cmdid = 0xff; // timestamp, so never a valid command id

    if (!read_report_buffer(pThis->pEm100, reportdata))
        return 0;

    for (report = 0; report < REPORT_BUFFER_COUNT; report++) {
        data = &reportdata[report][0];
        count = (data[0] << 8) | data[1];
        if (count > 1023) {
            printf("Warning: EM100pro sends too much data %u.\n", count);
            count = 1023;
        }
        for (i = 0; i < count; i++) {
            unsigned int j = additional_pad_bytes;
            additional_pad_bytes = 0;
            unsigned char cmd = data[2 + i*8];
            if (cmd == 0xff)
            {
                /* timestamp */
                timestamp = data[2 + i*8 + 2];
                timestamp = (timestamp << 8) | data[2 + i*8 + 3];
                timestamp = (timestamp << 8) | data[2 + i*8 + 4];
                timestamp = (timestamp << 8) | data[2 + i*8 + 5];
                timestamp = (timestamp << 8) | data[2 + i*8 + 6];
                timestamp = (timestamp << 8) | data[2 + i*8 + 7];
                continue;
            }

            /* from here, it must be data */
            if (cmd != cmdid) {
                unsigned char spi_command = data[i * 8 + 4];
                spi_cmd_vals = get_command_vals(spi_command);

                /* Check if there is a previous write pending and add a new cached write. */
                if (pThis->fFlashProg)
                {
                    PSPIPROGREQ pProgReq = (PSPIPROGREQ)calloc(1, sizeof(SPIPROGREQ) + pThis->cbFlashProg);
                    if (pProgReq)
                    {
                        pProgReq->pNext   = NULL;
                        pProgReq->offProg = pThis->offFlashProg;
                        pProgReq->cbProg  = pThis->cbFlashProg;
                        memcpy(&pProgReq->abData[0], pThis->pvFlashProgData, pProgReq->cbProg);

                        if (!pThis->pProgHead)
                        {
                            pThis->pProgHead = pProgReq;
                            pThis->pProgTail = pProgReq;
                        }
                        else
                        {
                            pThis->pProgTail->pNext = pProgReq;
                            pThis->pProgTail        = pProgReq;
                        }
                    }
                    else
                        return 0;
                }

                if (spi_command == 0x2) /* page program */
                {
                    pThis->fFlashProg  = 1;
                    pThis->cbFlashProg = 0;
                }
                else
                    pThis->fFlashProg = 0;

                /* new command */
                cmdid = cmd;
                if (counter == 0)
                    start_timestamp = timestamp;

                /* set up address if used by this command*/
                if (!spi_cmd_vals->uses_address) {
                    j = 1; /* skip command byte */
                } else {
                    address = (data[i * 8 + 5] << 16) +
                            (data[i * 8 + 6] << 8) +
                            data[i * 8 + 7];

                    if (pThis->fFlashProg)
                        pThis->offFlashProg = address;

                    /* skip command, address bytes, and padding */
                    j = 4 + spi_cmd_vals->pad_bytes;
                    if (j > MAX_TRACE_BLOCKLENGTH) {
                        additional_pad_bytes = j -
                            MAX_TRACE_BLOCKLENGTH;
                        j = MAX_TRACE_BLOCKLENGTH;
                    }
                }
#if 0
                printf("\nTime: %06lld.%08lld",
                        (timestamp - start_timestamp) /
                        100000000,
                        (timestamp - start_timestamp) %
                        100000000);
                printf(" command # %-6d : 0x%02x - %s",
                        ++counter, spi_command,
                        spi_cmd_vals->cmd_name);
#endif
                curpos = 0;
                outbytes = 0;
            }

            /* this exploits 8bit wrap around in curpos */
            unsigned char blocklen = (data[2 + i*8 + 1] - curpos);
            blocklen /= 8;

            for (; j < blocklen; j++) {
#if 0
                if (outbytes == 0) {
                    if (spi_cmd_vals->uses_address) {
                        printf("\n%08x : ",
                                address);
                    } else {
                        printf("\n         : ");
                    }
                }
                printf("%02x ", data[i * 8 + 4 + j]);
#endif

                if (pThis->fFlashProg)
                {
                    /* Add a byte to the buffer. */
                    if (pThis->cbFlashProg == pThis->cbFlashProgAlloc)
                    {
                        /* Grow buffer. */
                        void *pvNew = realloc(pThis->pvFlashProgData, pThis->cbFlashProgAlloc + 1024);
                        if (pvNew)
                        {
                            pThis->pvFlashProgData   = pvNew;
                            pThis->cbFlashProgAlloc += 1024;
                        }
                        else
                            return 0;
                    }

                    *((uint8_t *)pThis->pvFlashProgData + pThis->cbFlashProg) = data[i * 8 + 4 + j];
                    pThis->cbFlashProg++;
                }

                outbytes++;
                if (outbytes == 16) {
                    outbytes = 0;
                    if (spi_cmd_vals->uses_address)
                        address += 16;
                }
            }
            // this is because the em100 counts funny
            curpos = data[2 + i*8 + 1] + 0x10;
            //fflush(stdout);
        }
    }
    return 1;
}


static int spi_cached_writes_process(EM100NET *pThis)
{
    /*
     * If the flash is locked we have to look for an unlock request first to check whether we can
     * update the flash.
     */
    if (pThis->fSpiFlashLocked)
    {
        PSPIPROGREQ pUnlockReq = pThis->pProgHead;
        while (   pUnlockReq
               && (   pUnlockReq->offProg != SPI_FLASH_LOCK_OFF
                   || pUnlockReq->cbProg != 0x4
                   || *(uint32_t *)&pUnlockReq->abData[0] != SPI_FLASH_LOCK_UNLOCK_REQ_MAGIC))
            pUnlockReq = pUnlockReq->pNext;

        /* No unlock request means we can't update anything here. */
        if (!pUnlockReq)
            return 0;

        /* Update everything up to and including the unlock request. */
        set_state(pThis->pEm100, 0);
        PSPIPROGREQ pReq = pThis->pProgHead;

        for (;;)
        {
            uint8_t fUnlock = 0;
            int rc = 0;

            if (pReq == pUnlockReq)
            {
                /* Unlock request will get another value written. */
                uint32_t uVal = SPI_FLASH_LOCK_UNLOCKED_MAGIC;
                memcpy((uint8_t *)pThis->pvBin + pReq->offProg, &uVal, pReq->cbProg);
                rc = write_sdram(pThis->pEm100, (unsigned char *)&uVal, pReq->offProg, pReq->cbProg);
                fUnlock = 1;

                if (rc != 1)
                {
                    printf("08\n");
                    return -1;
                }
            }
            else if (   pReq->offProg >= SPI_MSG_CHAN_HDR_OFF
                     && pReq->offProg < SPI_MSG_CHAN_HDR_OFF + 4096)
            {
                /* Send the data out over the socket. */
                ssize_t cbSent = send(pThis->iFdSock, &pReq->abData[0], pReq->cbProg, 0);
                if (cbSent != (ssize_t)pReq->cbProg)
                {
                    printf("07\n");
                    return -1;
                }
            }
            else if (pReq->offProg == SPI_MSG_CHAN_AVAIL_OFF)
            {
                /* Acknowledge the read data. */
                if (pReq->cbProg == 0x4)
                {
                    uint32_t cbRead = *(uint32_t *)&pReq->abData[0];
                    printf("SPI Master read %u bytes\n", cbRead);
                    if (cbRead <= pThis->offRecv)
                    {
                        /* Move the data to the front. */
                        memmove(&pThis->abRecv[0], &pThis->abRecv[cbRead], pThis->offRecv - cbRead);
                        pThis->offRecv -= cbRead;
                        if (!pThis->offRecv) /* Reset data available flag?. */
                            write_sdram(pThis->pEm100, (unsigned char *)&pThis->offRecv, SPI_MSG_CHAN_AVAIL_F_OFF, sizeof(pThis->offRecv));
                    }
                    else
                    {
                        printf("06\n");
                        return -1;
                    }
                }
                else
                {
                    printf("05\n");
                    return -1;
                }
            }

            /* Update the in memory flash copy. */
            printf("\nWrite: %#x %zu\n", pReq->offProg, pReq->cbProg);

            PSPIPROGREQ pTmp = pReq;
            pReq = pReq->pNext;

            /* Remove from the list. */
            if (pTmp == pThis->pProgHead)
                pThis->pProgHead = pReq;
            if (pTmp == pThis->pProgTail)
                pThis->pProgTail = pReq;

            free(pTmp);
            if (fUnlock)
            {
                pThis->fSpiFlashLocked = 0;
                printf("Unlocked SPI flash\n");
                break;
            }
        }

        set_state(pThis->pEm100, 1); /* Start emulation again. */
    }

    if (   !pThis->fSpiFlashLocked
        && (   pThis->pProgHead
            || pThis->fNewDataAvailable))
    {
        set_state(pThis->pEm100, 0);

        if (pThis->fNewDataAvailable)
        {
            /* Just write the flag here. */
            uint32_t u32Magic = SPI_MSG_CHAN_AVAIL_MAGIC;
            write_sdram(pThis->pEm100, (unsigned char *)&u32Magic, SPI_MSG_CHAN_AVAIL_F_OFF, sizeof(u32Magic));
            pThis->fNewDataAvailable = 0;
        }

        /* Walk the list until we reach a lock request. */
        PSPIPROGREQ pReq = pThis->pProgHead;

        while (pReq)
        {
            uint8_t fLock = 0;

            if (pReq->offProg == SPI_FLASH_LOCK_OFF)
            {
                if (   pReq->cbProg == 0x4
                    && *(uint32_t *)&pReq->abData[0] == SPI_FLASH_LOCK_LOCK_REQ_MAGIC)
                {
                    /* Lock request will get another value written. */
                    uint32_t uVal = SPI_FLASH_LOCK_LOCKED_MAGIC;
                    memcpy(((uint8_t *)pThis->pvBin) + pReq->offProg, &uVal, pReq->cbProg);
                    int rc = write_sdram(pThis->pEm100, (unsigned char *)&uVal, pReq->offProg, pReq->cbProg);
                    if (!rc)
                    {
                        printf("write_sdram failed!\n");
                        return -1;
                    }
                    fLock = 1;
                }
                else
                    printf("Wrong magic written: %#x\n", *(uint32_t *)&pReq->abData[0]);
            }
            else if (   pReq->offProg >= SPI_MSG_CHAN_HDR_OFF
                     && pReq->offProg < SPI_MSG_CHAN_HDR_OFF + 4096)
            {
                /* Send the data out over the socket. */
                ssize_t cbSent = send(pThis->iFdSock, &pReq->abData[0], pReq->cbProg, 0);
                if (cbSent != (ssize_t)pReq->cbProg)
                {
                    printf("04\n");
                    return -1;
                }
            }
            else if (pReq->offProg == SPI_MSG_CHAN_AVAIL_OFF)
            {
                /* Acknowledge the read data. */
                if (pReq->cbProg == 0x4)
                {
                    uint32_t cbRead = *(uint32_t *)&pReq->abData[0];
                    printf("SPI Master read %u bytes\n", cbRead);
                    if (cbRead <= pThis->offRecv)
                    {
                        /* Move the data to the front. */
                        memmove(&pThis->abRecv[0], &pThis->abRecv[cbRead], pThis->offRecv - cbRead);
                        pThis->offRecv -= cbRead;
                        if (!pThis->offRecv) /* Reset data available flag?. */
                            write_sdram(pThis->pEm100, (unsigned char *)&pThis->offRecv, SPI_MSG_CHAN_AVAIL_F_OFF, sizeof(pThis->offRecv));
                    }
                    else
                    {
                        printf("03\n");
                        return -1;
                    }
                }
                else
                {
                    printf("02\n");
                    return -1;
                }
            }

            /* Update the in memory flash copy. */
            printf("\nWrite: %#x %zu\n", pReq->offProg, pReq->cbProg);

            PSPIPROGREQ pTmp = pReq;
            pReq = pReq->pNext;

            /* Remove from the list. */
            if (pTmp == pThis->pProgHead)
                pThis->pProgHead = pReq;
            if (pTmp == pThis->pProgTail)
                pThis->pProgTail = pReq;

            free(pTmp);
            if (fLock)
            {
                /*
                 * Before we lock the SPI flash we update any data we have received, so the
                 * SPI master sees it.
                 */
                write_sdram(pThis->pEm100, (uint8_t *)&pThis->offRecv, SPI_MSG_CHAN_AVAIL_OFF, sizeof(pThis->offRecv));
                if (pThis->offRecv)
                    write_sdram(pThis->pEm100, &pThis->abRecv[0], SPI_MSG_CHAN_HDR_OFF, pThis->offRecv);

                pThis->fSpiFlashLocked = 1;
                printf("Locked SPI flash\n");
                break;
            }
        }

        set_state(pThis->pEm100, 1); /* Start emulation again. */
    }

    return 0;
}

static int network_io_loop(EM100NET *pThis)
{
    int rc = 0;

    do
    {
        /* Read the trace buffer and process the entries first. */
        if (spi_trace_process(pThis) != 1)
            return -1;

        /* Process any cached write requests from the SPI Master. */
        if (spi_cached_writes_process(pThis) == -1)
            return -1;

        /* Check for data on the socket. */
        int cbAvail = 0;
        rc = ioctl(pThis->iFdSock, FIONREAD, &cbAvail);
        if (rc)
            return -1;

        if (   cbAvail
            && pThis->offRecv < sizeof(pThis->abRecv))
        {
            /* Read data in and update the flash. */
            cbAvail = (unsigned)cbAvail < sizeof(pThis->abRecv) - pThis->offRecv ? (unsigned)cbAvail : sizeof(pThis->abRecv) - pThis->offRecv;
            ssize_t cbRecv = recv(pThis->iFdSock, &pThis->abRecv[pThis->offRecv], cbAvail, MSG_DONTWAIT);
            if (cbRecv != (ssize_t)cbAvail)
                return -1;

            if (!pThis->offRecv)
                pThis->fNewDataAvailable = 1;

            pThis->offRecv += cbAvail;
        }

    } while (!rc);

    return rc;
}


int network_mainloop(struct em100 *em100, int port, void *pvBin, size_t cbBin)
{
    int rc = 0;
    int iFdListening = socket(AF_INET, SOCK_STREAM, 0);

    if (iFdListening > -1)
    {
        struct sockaddr_in SockAddr;

        memset(&SockAddr, 0, sizeof(SockAddr));
        SockAddr.sin_family      = AF_INET;
        SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        SockAddr.sin_port        = htons(port);
        int rcPsx = bind(iFdListening, (struct sockaddr *)&SockAddr, sizeof(SockAddr));
        if (!rcPsx)
        {
            printf("EM100: Waiting for incoming connection...\n");
            rcPsx = listen(iFdListening, 1);
            if (!rcPsx)
            {
                int iFdCon = accept(iFdListening, (struct sockaddr *)NULL, NULL);
                if (iFdCon == -1)
                    rc = 1;
                else
                {
                    printf("EM100: Connected, entering I/O loop\n");
                    EM100NET This;

                    This.pEm100           = em100;
                    This.iFdSock          = iFdCon;
                    This.fSpiFlashLocked  = 0;
                    This.pvBin            = pvBin;
                    This.cbBin            = cbBin;
                    This.pProgHead        = NULL;
                    This.pProgTail        = NULL;
                    This.fFlashProg       = 0;
                    This.offFlashProg     = 0;
                    This.pvFlashProgData  = NULL;
                    This.cbFlashProg      = 0;
                    This.cbFlashProgAlloc = 0;
                    This.offRecv          = 0;

                    /* Initialize the lock state. */
                    uint32_t uMagic = SPI_FLASH_LOCK_UNLOCKED_MAGIC;
                    *(uint32_t *)(((uint8_t *)pvBin) + SPI_FLASH_LOCK_OFF) = SPI_FLASH_LOCK_UNLOCKED_MAGIC;
                    set_state(This.pEm100, 0);
                    write_sdram(This.pEm100, (unsigned char *)&uMagic, SPI_FLASH_LOCK_OFF, sizeof(uint32_t));
                    set_state(This.pEm100, 1);

                    rc = network_io_loop(&This);
                    if (rc < 0)
                    {
                        printf("EM100: Network I/O loop failed\n");
                        rc = 1;
                    }

                    close(This.iFdSock);
                }
            }
            else
                rc = 1;
        }
        else
            rc = 1;

        close(iFdListening);
    }
    else
        rc = 1;

    return rc;
}

