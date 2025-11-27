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

CartDSpico::CartDSpico(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    u8 buffer[512];
    RequestedSectorAddress = 0;
}

CartDSpico::~CartDSpico()
{
}

void CartDSpico::Reset()
{
    CartSD::Reset();
    RequestedSectorAddress = 0;
}

int CartDSpico::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2)
        return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);
    
    Log(LogLevel::Debug, "DSpico: command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);

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
    case 0xE3:
        {
            /*
                SD sector request
                Command: E3 00 00 00 XX XX XX XX
                XX XX XX XX = sector address
            */
            u32 addr = (cmd[4]<<24) | (cmd[5]<<16) | (cmd[6]<<8) | cmd[7];
            RequestedSectorAddress = addr;
            Log(LogLevel::Debug, "DSpico: requested SD sector read at %08X", addr);
            return 0;
        }
    case 0xE4:
        {
            /*
                Poll SD ready (not busy)
                Command: E4 00 00 00 00 00 00 00
                Return 0 if not ready, non-0 if ready
            */
            // always return true, since we're always ready
            *data = 1;
            return 0;
        }
    case 0xE5: /* SD sector data read */
        {
            /*
                SD sector data read
                Command: E5 00 00 00 00 00 00 00
                Return data at sector requested from E3
            */
            u8 buffer[512] = {};
            if (SD)
                SD->ReadSectors(RequestedSectorAddress, 1, buffer);

            for (u32 pos = 0; pos < len; pos++)
                data[pos] = buffer[pos & 0x1FF];

            return 0;
        }
    case 0xF6: /* SD sector write data */
    {
        /*
            SD sector request
            Command: F6 E1 0D XX YY YY YY YY
            YY YY YY YY = sector address
            XX = 0x98 | (if first sector, 0x1) | (if last sector, 0x2)

            XX not implemented for now
        */
        u32 addr = (cmd[4]<<24) | (cmd[5]<<16) | (cmd[6]<<8) | cmd[7];
        RequestedSectorAddress = addr;
        Log(LogLevel::Debug, "DSpico: requested SD sector write at %08X", addr);
        // Continue to write end.
        return 1;
    }
    default:
        Log(LogLevel::Warn, "DSpico: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

void CartDSpico::ROMCommandFinish(const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    switch (cmd[0])
    {
    case 0xF6: /* Write data to SD FIFO */
        {
            u32 addr = (cmd[4]<<24) | (cmd[5]<<16) | (cmd[6]<<8) | cmd[7];
            RequestedSectorAddress = addr;
            if (SD && !SD->IsReadOnly())
                SD->WriteSectors(RequestedSectorAddress, 1, data);
            break;
        }
    }
}

}
}
