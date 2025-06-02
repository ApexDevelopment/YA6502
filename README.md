# Yet Another 6502
This is a 6502 emulator I wrote primarily so that I could say I finished an emulator. My other emulator projects got far, but were never as complete as this one is.

This is still a personal project, however, so it isn't very user-friendly.

# Usage
After building, run the `build\main` executable with the file path to a binary file/ROM as an argument. The raw data will be written into memory from $0000-$FFFF, so the file should be structured to have the interrupt vector table at the correct location (end of memory).

There is no display output (yet). The 6502's execution can be controlled using terminal commands. It feels similar to GDB in usage. Use `j [location]` to jump to a specific address (e.g. `j 0x0400`). Use `i` to get the processor state, and `i [location]` to read one byte of memory. `b [location]` sets a breakpoint on an address, and `r` will start execution. Pressing enter without entering any command will run 1 instruction. You can also use `t MOS` or `t NES` to switch between NMOS and NES modes, the only difference currently is that NES mode disables BCD functionality (controlled by the D flag).

The processor automatically halts when it encounters an instruction it cannot parse or if the program counter does not change after an instruction, i.e. jumping to the current address - sometimes known as a trap. Eventually I may implement infinite loop detection by checking for repeated machine states.

Even though this has an NES mode, it does not support `.nes` files, also known as the iNES format. Those files are not raw program data, they contain extraneous information like which mapper chip the game uses. NES support was mainly added so that I could run the `.bin` version of `nestest` (courtesy of https://www.emulationonline.com/systems/nes/roms/nestest_bin/).

# Functionality
YA6502 passes Klaus Dormann's `6502_functional_test` as well as the documented opcode section of `nestest`. It does not support most undocumented opcodes. These may be added in the future. This emulator is usable insofar as you are willing to put programs in the required format and read output using `i` commands.

YA6502 is licensed under CC BY-NC-SA 4.0. See [LICENSE.txt](LICENSE.txt).