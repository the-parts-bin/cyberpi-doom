idf_component_register(SRCS "main.c" "wad_vfs.c" "hud.c" "audio.c"
                            "ui.c" "launcher.c" "lobby.c" "netplay.c" "net_udp.c" "remote.c"
                       PRIV_REQUIRES board doomgeneric esp_partition vfs esp_timer fatfs opl esp_driver_dac
                                    esp_wifi esp_netif esp_event nvs_flash lwip esp_driver_uart
                       WHOLE_ARCHIVE) # doomgeneric calls back into lobby.c and net_udp.c
