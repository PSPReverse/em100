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
 * Request header sent over the network.
 */
typedef struct REQHDR
{
    /** Magic for the header. */
    uint32_t                u32Magic;
    /** Command ID 0 = read, 1 = write. */
    uint32_t                u32Cmd;
    /** Start address to access. */
    uint32_t                u32AddrStart;
    /** Number of bytes for the transfer. */
    uint32_t                cbXfer;
} REQHDR;


#define REQHDR_MAGIC 0xebadc0de

static int network_io_loop(struct em100 *em100, int iFdCon)
{
    int rc = 0;
    void *pvScratch = NULL;
    size_t cbScratch = 0;

    do
    {
        /* Receive the header first. */
        REQHDR Req;
        ssize_t cbRecv = recv(iFdCon, &Req, sizeof(Req), 0);
        if (   cbRecv == sizeof(Req)
            && Req.u32Magic == REQHDR_MAGIC)
        {
            if (cbScratch < Req.cbXfer)
            {
                void *pvNew = realloc(pvScratch, Req.cbXfer);
                if (pvNew)
                {
                    pvScratch = pvNew;
                    cbScratch = Req.cbXfer;
                }
            }

            if (cbScratch >= Req.cbXfer)
            {
                int rcReq = 0;

                if (Req.u32Cmd == 1)
                {
                    /* Receive the data to write. */
                    cbRecv = recv(iFdCon, pvScratch, Req.cbXfer, 0);
                    if (cbRecv == Req.cbXfer)
                        rcReq = write_sdram(em100, pvScratch, Req.u32AddrStart, Req.cbXfer);
                    else
                        rc = -1;
                }
                else
                    rcReq = read_sdram(em100, pvScratch, Req.u32AddrStart, Req.cbXfer);

                /* Send response and optional data. */
                int32_t rcResp = rcReq == 1 ? 0 : -1;
                ssize_t cbSend = send(iFdCon, &rcResp, sizeof(rcResp), 0);
                if (   cbSend == sizeof(rcResp)
                    && Req.u32Cmd == 0
                    && rcResp == 0)
                {
                    /* Send payload on successful read. */
                    cbSend = send(iFdCon, pvScratch, Req.cbXfer, 0);
                    if (cbSend != Req.cbXfer)
                        rc = -1;
                }
                else
                    rc = -1;
            }
            else
                rc = -1;
        }
        else
            rc = -1;
    } while (!rc);

    if (pvScratch)
        free(pvScratch);
    close(iFdCon);
    return rc;
}


int network_mainloop(struct em100 *em100, int port)
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
                    rc = network_io_loop(em100, iFdCon);
                    if (rc < 0)
                    {
                        printf("EM100: Network I/O loop failed\n");
                        rc = 1;
                    }
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

