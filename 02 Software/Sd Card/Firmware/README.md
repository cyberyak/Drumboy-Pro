# Firmware library

Put Drumboy Pro update files (`Drumboy_Pro_Fw_*.bin`) in a `Firmware` folder on the SD card, for example:

```
Firmware/
  Drumboy_Pro_Fw_01_01.bin          ← stock Randomwaves 1.01
  Drumboy_Pro_Fw_01_01_cy03.bin     ← cyberyak 1.01-cy.3
```

Both come with every cyberyak release on GitHub.

## Switching firmware on the device (cyberyak firmware only)

1. Press **FILE** to open the File menu, then press **FILE** again: the **Plugin & Firmware Manager** opens in the song lanes.
2. Press **LEFT** or **RIGHT** to switch to the **FIRMWARE** page.
3. Select a file with **UP/DOWN** (or knob 1) and press **GLOBAL** (the key between LEFT and RIGHT), then **GLOBAL** again to confirm.
4. The Drumboy checks the file (CRC), copies it to the SD root and restarts; the bootloader shows "UPDATING FIRMWARE" and installs it. The library copy stays here.

Stock firmware has no manager: to come back from stock, copy a cyberyak `Drumboy_Pro_Fw_*.bin` to the SD root yourself and power-cycle.

Do not use `System/Firmware/` for the library — the bootloader empties that folder on every update.
