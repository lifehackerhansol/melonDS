/*
    Copyright 2016-2024 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <string.h>
#include "NDS.h"
#include "DSi.h"
#include "NDSCart.h"
#include "Platform.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace NDSCart
{

CartTT::CartTT(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    u8 buffer[512];
    R2ResponseCount = 0;
    CurrentSDIOCommand = 0;
    CurrentSDHostMode = SD_HOST_NORESPONSE;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;

    // Parse FAT header.
    SD->ReadSectors(0, 1, buffer);
    u16 bytesPerSector = (buffer[12] << 8) | buffer[11];
    u8 sectorsPerCluster = buffer[13];
    u16 firstFatSector = (buffer[15] << 8) | buffer[14];
    u8 fatTableCount = buffer[16];
    u32 clustersTotal = SD->GetSectorCount() / sectorsPerCluster;
    IsFat32 = clustersTotal >= 65526;
}

CartTT::~CartTT()
{
}

void CartTT::Reset()
{
    CartSD::Reset();

    R2ResponseCount = 0;
    CurrentSDIOCommand = 0;
    CurrentSDHostMode = SD_HOST_NORESPONSE;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;
}

int CartTT::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2)
        return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);

    switch (cmd[0])
    {
    case 0x00: /* ROM read data */
    case 0xB7: /* ROM read data */
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memcpy(data, &ROM[addr & (ROMLength-1)], len);
            return 0;
        }
    /* Starting custom card protocol */
    case 0x5F: /* SD Host control registers */
        {
            // TODO: implement.
            // This controls things such as SD host clock speeds where needed, or SDHC mode.
            Log(LogLevel::Debug, "DSTT: Set SD Host register to %02X\n", cmd[1]);
            *data = 0;
            return 0;
        }
    case 0x50: /* SD Host check if busy */
    case 0x80: /* SD FIFO wait for data ready */
        {
            // Responds with 1 if true, 0 if false.
            // But also, we're never busy :P
            *data = 0;
            return 0;
        }
    case 0x51: /* SD Host mode setting */
        {
            /*
                Note:
                    While this is where the SDIO happens, it isn't always SDIO
                    Thus, the command can sometimes be 0

                Command structure:
                51 AA AA AA AA BB CC 00
                AAAAAAAA = SDIO parameter
                BB = command
                CC = SD host mode, see CartTTSDHostMode enum

                This command doesn't respond with anything.
                The actual response from 0x51, if the host mode is set to respond,
                will be sent in 0x52.
            */
            CurrentSDIOParameter = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            CurrentSDHostMode = (CartTTSDHostMode)cmd[6];
            CurrentSDIOCommand = cmd[5];
            switch(CurrentSDHostMode)
            {
                case SD_HOST_SEND_STOP_CLK:
                {
                    // If requested to stop reading, then our job is done. Reset the variables.
                    CurrentSDHostMode = SD_HOST_NORESPONSE;
                    CurrentSDIOCommand = 0;
                }
                case SD_HOST_NEXT_DATABLOCK:
                {
                    // Next SD block requested. Increment requested sector address.
                    RequestedSectorAddress += 1;
                }
            }
            *data = 0;
            return 0;
        }
    case 0x52: /* SD Host send response */
        {
            // Deliver the data requested from 0x51.
            if (CurrentSDHostMode >= SD_HOST_READ_4B && CurrentSDHostMode <= SD_HOST_NEXT_4B)
            {
                switch(CurrentSDIOCommand)
                {
                    case 2: /* ALL_SEND_CID */
                    {
                        // TODO: What does this command do?
                        // It is the only R2 command ever sent (128-bit response)
                        *data = 0;
                        break;
                    }
                    case 8: /* SEND_IF_COND */
                    {
                        // On SD 2.0 specification, readback from CMD8 is an echo pattern
                        // So echo it back.
                        *data = CurrentSDIOParameter;
                        break;
                    }
                    case 24: /* WRITE_BLOCK */
                    case 25: /* WRITE_MULTIPLE_BLOCK */
                    {
                        RequestedSectorAddress = GetAdjustedSector(CurrentSDIOParameter);
                        *data = 0;
                        break;
                    }
                    case 41: /* SD_SEND_OP_COND */
                    {
                        // If SDHC is supported, provide the HCS bit at bit 30.
                        // To put it simply, if the file system is FAT32, we can assume HCS bit is set.
                        *data = IsFat32 ? (1 << 30) : 0;
                        break;
                    }
                    case 3: /* SEND_RELATIVE_ADDR */
                    case 6: /* SWITCH_FUNC */
                    case 7: /* SELECT_CARD */
                    case 12: /* STOP_TRANSMISSION */
                    case 16: /* SET_BLOCKLEN */
                    case 55: /* APP_CMD */
                    default:
                    {
                        // Handling not necessary
                        *data = 0;
                        break;
                    }
                }
            }
            // If only a 4 byte read was requested, then our job is done. Reset the variables.
            if (CurrentSDHostMode == SD_HOST_READ_4B)
            {
                CurrentSDHostMode = SD_HOST_NORESPONSE;
                CurrentSDIOCommand = 0;
            }
            return 0;
        }
    case 0x53: /* Request read single block */
    case 0x54: /* Request read multiple block */
        {
            RequestedSectorAddress = GetAdjustedSector((cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4]);
            *data = 0;
            return 0;
        }
    case 0x56: /* Flush SD FIFO to disk */
        {
            // Here in melonDS we already flushed it in 0x82.
            *data = 0;
            return 0;
        }
    case 0x81: /* Read data from SD FIFO */
        {
            u8 buffer[512] = {};
            if (SD)
                SD->ReadSectors(RequestedSectorAddress, 1, buffer);

            for (u32 pos = 0; pos < len; pos++)
                data[pos] = buffer[pos & 0x1FF];

            return 0;
        }
    case 0x82: /* Write data to SD FIFO */
        return 1;
    default:
        Log(LogLevel::Warn, "TT: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

void CartTT::ROMCommandFinish(const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    switch (cmd[0])
    {
    case 0x82: /* Write data to SD FIFO */
        {
            if (SD && !SD->IsReadOnly())
                SD->WriteSectors(RequestedSectorAddress, 1, data);

            // Preemptively increase this by 1.
            // There is no particular command for this when doing multiple block write. 
            RequestedSectorAddress += 1;
            break;
        }
    }
}

}
}
