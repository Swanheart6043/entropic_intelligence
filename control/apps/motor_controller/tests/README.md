# Host regression tests

These tests compile the real `motor_controller.c` and FOC algorithm against a
deterministic hardware port. They exercise lifecycle, sample dispatch, fault
latching and recovery, unsupported algorithm selection, command validation,
elapsed-time ramps, and millisecond counter wraparound. They do not simulate
STM32 peripheral timing or verify electrical output on a board.

Run with a locally installed Zig C compiler from PowerShell:

```powershell
./run.ps1 -ZigPath 'C:/path/to/zig.exe'
```

The executable is written to the ignored `control/build/motor-controller-host-tests`
directory. No STM32 HAL headers, board connection, or firmware flashing is required.
