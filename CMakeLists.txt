# Zephyr module entry point. PlatformIO/Arduino builds read library.json /
# library.properties and ignore this file entirely.
if(CONFIG_VICTRONBLE)
  zephyr_library()
  zephyr_library_sources(src/victronble_core.c)
  zephyr_library_sources(src/victronble_zephyr.c)
  zephyr_library_sources_ifdef(CONFIG_VICTRONBLE_CRYPTO_SOFTWARE
    src/victronble_aes_sw.c
    src/crypto/vble_aes.c
  )
  zephyr_include_directories(include)
endif()
