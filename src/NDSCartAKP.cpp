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

CartAKP::CartAKP(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    CurrentSDIOCommand = 0;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;
    CurrentHostMode = AKP_MODE_FLASH;
    CurrentSDMode = AKP_SD_MODE_NORMAL;
}

CartAKP::~CartAKP()
{
}

void CartAKP::Reset()
{
    CartSD::Reset();

    CurrentSDIOCommand = 0;
    CurrentSDIOParameter = 0;
    RequestedSectorAddress = 0;
    CurrentHostMode = AKP_MODE_FLASH;
    CurrentSDMode = AKP_SD_MODE_NORMAL;
}

int CartAKP::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    // allow to boot directly to hb.
    // do not merge
    //if (CmdEncMode != 2)
    //   return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);
    Log(LogLevel::Warn, "AKP: command %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());

    if(CurrentSDMode == AKP_SD_MODE_DIRECT)
        return ROMCommandStartDSDMode(nds, cartslot, cmd, data, len);

    switch (cmd[0])
    {
    case 0xB8:
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = ChipID;
        return 0;
    case 0xB7: /* SD read data - normal SD mode */
        {
            if(CurrentHostMode = AKP_MODE_SD)
            {
                u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
                u8 buffer[512] = {};
                if (SD)
                    SD->ReadSectors(GetAdjustedSector(addr), 1, buffer);
                for (u32 pos = 0; pos < len; pos++)
                    data[pos] = buffer[pos & 0x1FF];
                return 0;
            }
            // fall through otherwise
        }
    case 0x00: /* ROM read data */
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memcpy(data, &ROM[addr & (ROMLength-1)], len);
            return 0;
        }
    case 0xD0: /* Set ROM offset */
        {
            // TODO
            memset(data, 0, len);
            return 0;
        }
    case 0xD1: /* Set Slot 1 ROM mode */
        {
            CurrentHostMode = (CartAKPHostMode)cmd[2];
            memset(data, 0, len);
            return 0;
        }
    case 0xD3: /* Set to Direct SD mode */
        {
            Log(LogLevel::Debug, "AKP: Change to Direct SD\n");
            CurrentSDMode = AKP_SD_MODE_DIRECT;
            memset(data, 0, len);
            return 0;
        }
    default:
        Log(LogLevel::Warn, "AKP: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

int CartAKP::ROMCommandStartDSDMode(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    // Compile bits to u8
    // TODO
    Log(LogLevel::Warn, "AKP: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
    for (u32 pos = 0; pos < len; pos += 4)
        *(u32*)&data[pos] = 0;
    return 0;
}

}
}
