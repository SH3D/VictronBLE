# NativeDecode — decode an advertisement on your PC

Decodes one Victron "Instant Readout" advertisement on the host. No board, no
BLE stack, no PlatformIO — just a C compiler. The library's core
(`src/victronble_core.c`) is plain C99 with no dependencies, so the same
decoder that runs on an ESP32, an nRF52 or under Zephyr also runs here.

Handy for:

- checking an advertisement key before you flash anything
- decoding a capture from nRF Connect, `btmon` or a sniffer
- seeing the record layout and which fields your device actually sends

## Build and run

```sh
make
./nativedecode
```

With no arguments it decodes a built-in sample advert (a SmartSolar MPPT in
bulk charge, taken from `tests/vectors/`):

```
advert           31 bytes
device type      solar charger (0x01)
model id         0xa060
nonce            4660 (0x1234)
fields:
  state            bulk (error 0)
  battery          13.24 V
  current          5.4 A
  pv power         340 W
  yield today      1200 Wh
  load current     n/a
```

## Your own capture

```sh
./nativedecode <advert-hex> <key-hex>
```

`advert-hex` is the manufacturer-specific data **starting at the company ID**
(`e1 02 ...`), exactly as a sniffer reports it. Separators are ignored, so
`e1:02:10…` and `e10210…` both work. `key-hex` is the 32-character
advertisement key from VictronConnect → device → gear icon → Product info →
*Instant readout via Bluetooth*.

```sh
./nativedecode "e1021089a30002efbe0d4108532f0d44a51c62a051e97c1fae2fe9e82a0e15" \
               0df4d0395b7d5d4f5a0d0af52e1b4c1e
```

## Reading the output

`n/a` means the device did not send that field — the core returns `NAN` for
absent floats and `0xFFFF` for an unavailable time-to-go, and this example
renders both as `n/a`. An MPPT with no load output always shows
`load current n/a`; that is not a fault.

Three failure messages are worth telling apart:

| Message | Meaning |
|---|---|
| `not a Victron product advertisement` | wrong company ID, or not a product record — you captured something else |
| `key check failed` | the advert is Victron's, but this key belongs to a different device |
| `decode failed: unsupported type` | a real Victron record the library has no decoder for yet (e.g. GX devices) |

## Which API this shows

```c
victronble_parse_key()        /* 32 hex chars -> 16 bytes           */
victronble_is_product_adv()   /* cheap pre-filter, no crypto        */
victronble_key_matches()      /* key-check byte, still no crypto    */
victronble_decode()           /* decrypt + parse into a record      */
victronble_strerror()
victronble_device_type_str()
victronble_state_str()
```

That is the whole portable core. See `include/victronble_core.h` for the record
structures, and `samples/observer/` for the same printing logic driven by a
live BLE scan under Zephyr.
