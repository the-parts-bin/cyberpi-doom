# Vendored from https://github.com/ozkl/doomgeneric (GPL-2.0)
file(GLOB DG_SRCS "src/*.c")
idf_component_register(SRCS ${DG_SRCS}
                       INCLUDE_DIRS "src"
                       LDFRAGMENTS "linker.lf")
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CMAP256 DOOMGENERIC_RESX=320 DOOMGENERIC_RESY=232 DG_SCREENHEIGHT=232
    DG_DIRECT_VIDEOBUFFER DG_AUTO_SAVE_NAMES DG_NO_AUTOMAP_STATUSBAR
    "DG_CONFIG_DIR=\"/data/\"")
# 1990s C: silence the warning flood rather than patch every file
target_compile_options(${COMPONENT_LIB} PRIVATE -w -O2)
