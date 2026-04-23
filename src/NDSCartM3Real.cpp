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

CartM3Real::CartM3Real(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    memset(Buffer, 0, 512);
    RequestedSector = 0;
}

void CartM3Real::Reset()
{
    CartSD::Reset();

    memset(Buffer, 0, 512);
    RequestedSector = 0;
}

int CartM3Real::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2)
        return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);

    Log(LogLevel::Warn, "M3Real: command %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());

    switch (cmd[0])
    {
    case 0xB0: /* Get card information */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 4;
            return 0;
        }
    case 0xB7: /* ROM read data */
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memcpy(data, &ROM[addr & (ROMLength-1)], len);
            return 0;
        }
    case 0xB8: /* ? Get chip ID ? */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;
        }
    case 0xB3: /* SD stop transmission */
        {
            RequestedSector = 0;
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;
        }
    case 0xBA: /* SD read data */
        {
            // TODO: Do these use separate buffers?
            for (u32 pos = 0; pos < len; pos++)
                data[pos] = Buffer[pos & 0x1FF];
            return 0;
        }
    case 0xB6: /* SD write status - multiple block */
    case 0xBC: /* SD write status - single block*/
        {
            if (SD && !SD->IsReadOnly())
                SD->WriteSectors(RequestedSector++, 1, Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xBD: /* SD read request - single block */
    case 0xB1: /* SD read request - multiple block */
        {
            RequestedSector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
        }
    case 0xB2: /* SD read request - multiple block - next sector */
        {
            if (SD)
                SD->ReadSectors(RequestedSector++, 1, Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xBE: /* SD write start - single block */
        return 1;
    default:
        Log(LogLevel::Warn, "M3Real: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

void CartM3Real::ROMCommandFinish(const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    switch (cmd[0])
    {
    case 0xB4: /* SD write buffer - multiple block */
    case 0xBE: /* SD write buffer - single block */
        {
            RequestedSector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
        }
    case 0xB5: /* SD write buffer - multiple block - next sector */
        {
            memcpy(Buffer, data, len <= 512 ? len : 512);
            break;
        }
    }
}

}
}
