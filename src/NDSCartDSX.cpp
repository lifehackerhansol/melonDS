/*
    Copyright 2016-2026 melonDS team

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

// We aren't actually CartSD
// but we use it anyway for the FAT storage access
CartDSX::CartDSX(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
}

CartDSX::~CartDSX()
{
}

void CartDSX::Reset()
{
    CartSD::Reset();
    FpgaAddress = 0;
}

int CartDSX::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2)
        return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);

    switch (cmd[0])
    {
    case 0xB8: /* ? Get chip ID ? */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;
        }
    /* Starting custom card protocol */
    case 0x03: /* Reset FPGA address */
        {
            // This sets the FPGA address to 0
            // Which points to the buffer index that is stored for 
            // write ops
            FpgaAddress = 0;

            // fall through to return 0
        }
    case 0x02: /* Check card busy */
    case 0xBC: /* Stop transmission ? */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xBF: /* Read LBA */
        {
            // This will pass the NAND sector address
            // But the MBR/FAT is actually located at 0x6000 of NAND
            // Below that is where the firmware ARM9/ARM7 is stored (not supported at the moment)
            u32 sector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            sector -= 0x6000;
            if(SD)
                SD->ReadSectors(sector, 1, Buffer);
            for (u32 pos = 0; pos < len; pos++)
                data[pos] = Buffer[pos & 0x1FF];
            return 0;
        }
    case 0x04: /* Write data to buffer */
        {
            Buffer[FpgaAddress++] = cmd[4];
            Buffer[FpgaAddress++] = cmd[3];
            Buffer[FpgaAddress++] = cmd[2];
            Buffer[FpgaAddress++] = cmd[1];
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0x05: /* Flush buffer to LBA */
        {
            // This will pass the NAND sector address
            // But the MBR/FAT is actually located at 0x6000 of NAND
            // Below that is where the firmware ARM9/ARM7 is stored (not supported at the moment)
            u32 sector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            sector -= 0x6000;
            if (SD)
                SD->WriteSectors(sector, 1, Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    default:
        Log(LogLevel::Warn, "DSX: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len);
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

}
}
