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

#include <errno.h>
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

CartPowerSaves::CartPowerSaves(std::unique_ptr<u8[]>&& rom, u32 len, u32 chipid, ROMListEntry romparams, melonDS::NDSCart::CartType type, void* userdata) :
    CartCommon(std::move(rom), len, chipid, false, romparams, type, userdata)
{
    int rc;

    // Initialize HID device.
    if (!device) {
        device = hid_open(POWERSAVES_VID, POWERSAVES_PID, NULL);
        if (device == NULL) {
            struct hid_device_info *deviceinfo = hid_enumerate(0, 0);
            if (!deviceinfo) {
                Log(LogLevel::Error, "HID device not found: %s", hid_error(NULL));
                return;
            }
            free(deviceinfo);
            Log(LogLevel::Error, "PowerSaves device not found");
            return;
        }
    }

    // Switch to ROM mode.
    rc = SendMessage(CartPowerSavesCmdType_ROM_MODE, NULL, 0, 0);
    if(rc != 0)
        return;
}

CartPowerSaves::~CartPowerSaves()
{
    hid_close(device);
}

/*
    Send a message to the PowerSaves device.
    Parameters:
        type: PowerSaves command type.
        cmd: Command (may be NULL).
        len: Size of command (may be 0).
        len_response: Size of expected response from command (may be 0).
    Returns:
        0 if successful, errno if not.
*/
int CartPowerSaves::SendMessage(CartPowerSavesCmdType type, const u8* cmd, u16 len, u16 len_response)
{
    int rc = 0;
    // HID limit is 64 bytes.
    // According to hidapi, there should be one more byte,
    // as the first byte is always the report ID.
    int msgBufferSize = 65;
    u8 msgBuffer[msgBufferSize] = {};

    // Make sure the command can fit in the buffer.
    if(len > (msgBufferSize - 6))
    {
        return -ENOMEM;
    }

    msgBuffer[1] = type;
    msgBuffer[2] = (u8)(len & 0xFF);
    msgBuffer[3] = (u8)((len >> 8) & 0xFF);
    msgBuffer[4] = (u8)(len_response & 0xFF);
    msgBuffer[5] = (u8)((len_response >> 8) & 0xFF);

    if(cmd != NULL)
    {
        memcpy(msgBuffer + 6, cmd, len);
    }

    rc = hid_write(device, msgBuffer, msgBufferSize);
    if(rc == -1)
    {
        Log(LogLevel::Error, "Write failed, %s", hid_error(device));
        return -EIO;
    }

    return 0;
}

/*
    Read data from the PowerSaves-inserted cartridge.
    Parameters:
        cmd: NTR card command. Must be 8 bytes.
        data: Pointer to read data to (may be NULL)
        len: Length of data to read (may be 0)
    Returns:
        0 if successful, errno if not.

    TODO: sanity check cmd buffer size.
*/
int CartPowerSaves::ReadCardData(const u8* cmd, u8* data, u32 len)
{
    u32 i = 0;
    int rc = SendMessage(CartPowerSavesCmdType_NTR, cmd, 8, len);
    if (!rc)
        return rc;

    while (i < len)
    {
        rc = hid_read(device, data + i, len - i);
        if(rc < 0) {
            Log(LogLevel::Error, "Read failed, %s", hid_error(device));
            return -EIO;
        }
        i += rc;
    }

    return 0;
}

int CartPowerSaves::ROMCommandStart(NDS& nds, NDSCart::NDSCartSlot& cartslot, const u8* cmd, u8* data, u32 len)
{
    return ReadCardData(cmd, data, len);
}

}
}
