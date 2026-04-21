/*
    Copyright 2016-2025 melonDS team

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

CartEZP::CartEZP(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, void* userdata,
            std::optional<FATStorage>&& sdcard)
    : CartSD(std::move(rom), len, chipid, romparams, userdata, std::move(sdcard))
{
    memset(Buffer, 0, 4096);
    ClusterMapInitialized = false;
    RequestedFatMapSaveWriteAddress = 0;
}

void CartEZP::Reset()
{
    memset(Buffer, 0, 4096);
    CartSD::Reset();
    ClusterMapInitialized = false;
    RequestedFatMapSaveWriteAddress = 0;
}

int CartEZP::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2)
        return CartCommon::ROMCommandStart(nds, cartslot, cmd, data, len);
    Log(LogLevel::Warn, "EZP: %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());
        
    switch (cmd[0])
    {
    case 0xB1: /* Save FAT cluster map ? */
        {
            return 1;
        }
    case 0xB8: /* ? Get chip ID ? */
        {
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = (ChipID & 0xFFFFFFF8) | 2;
            return 0;
        }
    case 0xB2: /* Save read request */
    case 0xB6: /* ROM read request */
        {
            u32 address = ((cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4]);
            ReadSDFromClusterMap(address, cmd[0] == 0xB6);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xB9: /* SD read request */
        {
            // B9 00 00 XX AA BB CC DD
            // XX = num sectors
            // AABBCCDD = sector addr
            u32 sector = (cmd[4]<<24) | (cmd[5]<<16) | (cmd[6]<<8) | cmd[7];

            // FAILSAFE - hang if requested more than 4 sectors
            // doesn't fit in buffer, and causes UB on real hardware
            if (cmd[3] > 4)
            {
                Log(LogLevel::Error, "EZP: SYSTEM REQUESTED MORE THAN 4 SECTOR READ\n");
                Log(LogLevel::Error, "EZP: command %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());
                for (u32 pos = 0; pos < len; pos += 4)
                    *(u32*)&data[pos] = 1;
                return 0;
            }
            if (SD)
                SD->ReadSectors(sector, cmd[3], Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xBB: /* SD write start */
    case 0xBD: /* Save write start */
            return 1;
    case 0xBC: /* SD write flush */
        {
            // BC 00 00 XX AA BB CC DD
            // XX = num sectors
            // AABBCCDD = sector addr
            u32 sector = (cmd[4]<<24) | (cmd[5]<<16) | (cmd[6]<<8) | cmd[7];

            // FAILSAFE - hang if requested more than 4 sectors
            // doesn't fit in buffer, and causes UB on real hardware
            if (cmd[3] > 4)
                Log(LogLevel::Warn, "EZP: SYSTEM REQUESTED MORE THAN 4 SECTOR FLUSH. This is unstable on real hardware!\n");

            if (cmd[3] > 8){
                Log(LogLevel::Error, "EZP: SYSTEM REQUESTED MORE THAN 8 SECTOR FLUSH. This is OUT OF BOUNDS. Do not proceed.\n");
                for (u32 pos = 0; pos < len; pos += 4)
                    *(u32*)&data[pos] = 0;
                return 0;
            }
            if (SD)
                SD->WriteSectors(sector, cmd[3], Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xBE: /* Save write flush */
        {
            if (SD)
                SD->WriteSectors(SDFatMapSectorGet(false, RequestedFatMapSaveWriteAddress), 1, Buffer);
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = 0;
            return 0;
        }
    case 0xB7: /* ROM read data */
        {
            /* If the buffer has not been initialized yet, emulate ROM. */
            /* TODO: When does the R4 do this exactly? */
            if (!ClusterMapInitialized)
            {
                u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
                memcpy(data, &ROM[addr & (ROMLength-1)], len);
                return 0;
            }
            /* Otherwise, fall through. */
        }
    case 0xB3: /* Save read data */
    case 0xBA: /* SD read data */
        {
            // TODO: Do these use separate buffers?
            for (u32 pos = 0; pos < len; pos++)
                data[pos] = Buffer[pos & 0xFFF];
            return 0;
        }
    default:
        Log(LogLevel::Warn, "EZP: unknown command %02X %02X %02X %02X %02X %02X %02X %02X (%d) (%08X) (%04X)\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], len, cartslot.GetROMCnt(), cartslot.GetSPICnt());
        for (u32 pos = 0; pos < len; pos += 4)
            *(u32*)&data[pos] = 0;
        return 0;
    }
}

void CartEZP::ReadSDFromClusterMap(u32 address, bool rom)
{
    if (SD)
    {
        // Default mode.
        SD->ReadSectors(SDFatMapSectorGet(rom, address), 1, Buffer);
    }
}

u32 CartEZP::SDFatMapSectorGet(bool rom, u32 address)
{
    u32 *fatMap = rom ? (u32*)RomClusterMap : (u32*)SaveClusterMap;
    u32 clusterTop = fatMap[1];
    u32 clusterTopRomOffset = fatMap[0];
    u32 sector = 0;

    for (u32 i=0; i < 64; i+=2) {
        if (fatMap[i] == 0xFFFFFFFF) break;
        if (fatMap[i] < (address >> 9))
        {
            clusterTop = fatMap[i + 1];
            clusterTopRomOffset = fatMap[i];
        }
    }
    Log(LogLevel::Warn, "EZP: SDFatMapSectorGet device %d clusterTop %08X clusterTopRomOffset %08X SD sector %08X ROM sector %08X\n",
        rom ? 1 : 0,
        clusterTop,
        clusterTopRomOffset,
        (clusterTop + ((address >> 9) - clusterTopRomOffset)),
        address >> 9
    );
    return (clusterTop + ((address >> 9) - clusterTopRomOffset));
}

void CartEZP::ROMCommandFinish(const u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    switch (cmd[0])
    {
    case 0xB1: /* Save FAT cluster map ? */
        {
            if (len < 512)
                break;
            memcpy(RomClusterMap, data, 256);
            memcpy(SaveClusterMap, data + 256, 256);
            ClusterMapInitialized = true;
            Log(LogLevel::Debug, "EZP: ");
            for (u32 i=0; i < 256; i++)
            {
                Log(LogLevel::Debug, "%02X ", SaveClusterMap[i]);
            }
            Log(LogLevel::Debug, "\n");
            break;
        }
    case 0xBB: /* SD write start */
        {
            if (cmd[7] >= 4)
                Log(LogLevel::Warn, "EZP: SYSTEM REQUESTED MORE THAN 4 SECTOR WRITE. This is unstable on real hardware!\n");

            if (cmd[7] >= 8) {
                Log(LogLevel::Error, "EZP: SYSTEM REQUESTED MORE THAN 8 SECTOR WRITE. This is OUT OF BOUNDS. Do not proceed.\n");
                break;
            }
            u32 bufind = (cmd[7]) << 9;
            memcpy(Buffer + bufind, data, len <= 512 ? len : 512);
            break;
        }
    case 0xBD: /* Save write start */
        {
            RequestedFatMapSaveWriteAddress = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memcpy(Buffer, data, len <= 512 ? len : 512);
            break;
        }
    }
}

}
}
