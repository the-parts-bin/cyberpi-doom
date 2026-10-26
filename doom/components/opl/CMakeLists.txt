# OPL library and DOSBox DBOPL emulator from Chocolate Doom 2.2.1 (GPL-2.0),
# plus an ESP32 driver. Kept out of the doomgeneric component so its tables
# stay in internal RAM rather than PSRAM.
idf_component_register(SRCS "dbopl.c" "opl.c" "opl_queue.c" "opl_esp.c"
                       INCLUDE_DIRS "."
                       LDFRAGMENTS "linker.lf")
target_compile_options(${COMPONENT_LIB} PRIVATE -w -O2)
