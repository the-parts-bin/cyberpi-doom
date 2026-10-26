idf_component_register(SRCS "main.c" "wad_vfs.c" "hud.c" "audio.c"
                       PRIV_REQUIRES board doomgeneric esp_partition vfs esp_timer fatfs opl esp_driver_dac)
