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

CartSCDS::CartSCDS(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    CurrentSDIOCommand = 0;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;
}

CartSCDS::~CartSCDS()
{
}

void CartSCDS::Reset()
{
    CartSD::Reset();

    CurrentSDIOCommand = 0;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;
}

int CartSCDS::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    // allow to boot directly to hb.
    // do not merge
    //if (CmdEncMode != 2)
    //   return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);
    //Log(LogLevel::Warn, "SCDS: command %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());

    switch (cmd[0])
    {
    case 0x00: /* ROM read data */
    case 0xB7: /* ROM read data */
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memcpy(data, &ROM[addr & (ROMLength-1)], len);
            return 0;
        }
    case 0xB8:
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = ChipID;
        return 0;
    /* Starting custom card protocol */
    case 0x33: /* SDIO command */
        // TODO fully implement
        {
            CurrentSDIOCommand = cmd[2] & ~0x40;
            CurrentSDIOParameter = ((cmd[3]<<24) | (cmd[4]<<16) | (cmd[5]<<8) | cmd[6]);
            switch(CurrentSDIOCommand) {
                case 17:
                case 18:
                case 24:
                    RequestedSectorAddress = CurrentSDIOParameter >> 9;
            }
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0x34: /* SD data read request */
        {
            //Log(LogLevel::Warn, "SCDS: read 1 sector (%08X)\n", RequestedSectorAddress);

            if (SD)
                SD->ReadSectors(RequestedSectorAddress++, 1, Buffer);

            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0x35: /* Flush data from SD FIFO to disk */
        {
            //Log(LogLevel::Warn, "SCDS: write 1 sector (%08X)\n", RequestedSectorAddress);
            if (SD && !SD->IsReadOnly())
                SD->WriteSectors(RequestedSectorAddress, 1, Buffer);

            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0x30: /* Card response */
        // TODO what exactly does this return?
    case 0x38: /* Card is busy */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0x36: /* Read data from SD FIFO */
        {
            for (u32 pos = 0; pos < len; pos++)
                data[pos] = Buffer[pos & 0x1FF];
            return 0;
        }
    case 0x37: /* Write data to SD FIFO */
        {
            u32 BufferIdx = ((cmd[1]) << 8) | cmd[2];
            Buffer[BufferIdx++ & 0x1FF] = cmd[3];
            Buffer[BufferIdx++ & 0x1FF] = cmd[4];
            Buffer[BufferIdx++ & 0x1FF] = cmd[5];
            Buffer[BufferIdx++ & 0x1FF] = cmd[6];

            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    default:
        Log(LogLevel::Warn, "SCDS: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

}
}
