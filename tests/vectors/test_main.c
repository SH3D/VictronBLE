/**
 * victronble core host tests.
 *
 * Plain C, no framework: non-zero exit on failure. Positive vectors come
 * from test_vectors.h (openssl-encrypted, independent of the bundled AES);
 * negative cases are built inline.
 *
 * Build & run:  ./run.sh   (or see the gcc line inside it)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "victronble.h"
#include "test_vectors.h"

static int failures;

#define CHECK(cond) do {                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static int feq(float a, float b)
{
    return fabsf(a - b) < 0.005f;
}

static victronble_err_t decode(const uint8_t *frame, size_t len,
                               const uint8_t key[16], victronble_record_t *rec)
{
    memset(rec, 0xAA, sizeof(*rec));
    return victronble_decode(frame, len, key, rec);
}

int main(void)
{
    uint8_t key[VICTRONBLE_KEY_LEN];
    victronble_record_t rec;

    CHECK(victronble_parse_key(VEC_KEY_HEX, key));

    /* --- solar charger --- */
    CHECK(victronble_is_product_adv(VEC_SOLAR, sizeof(VEC_SOLAR)));
    CHECK(victronble_key_matches(VEC_SOLAR, sizeof(VEC_SOLAR), key));
    CHECK(decode(VEC_SOLAR, sizeof(VEC_SOLAR), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_SOLAR_CHARGER);
    CHECK(rec.model_id == 0xA060);
    CHECK(rec.nonce == 0x1234);
    CHECK(rec.u.solar.state == VICTRONBLE_STATE_BULK);
    CHECK(rec.u.solar.error == 0);
    CHECK(feq(rec.u.solar.battery_voltage, 13.24f));
    CHECK(feq(rec.u.solar.battery_current, 5.4f));
    CHECK(rec.u.solar.yield_today_wh == 1200);
    CHECK(feq(rec.u.solar.pv_power, 340.0f));
    CHECK(isnan(rec.u.solar.load_current));
    CHECK(strcmp(victronble_state_str(rec.u.solar.state), "bulk") == 0);

    /* --- battery monitor --- */
    CHECK(decode(VEC_BATMON, sizeof(VEC_BATMON), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_BATTERY_MONITOR);
    CHECK(rec.u.batmon.remaining_minutes == 600);
    CHECK(feq(rec.u.batmon.voltage, 12.80f));
    CHECK(rec.u.batmon.alarm == 0x0005);
    CHECK(rec.u.batmon.aux_mode == 2);
    CHECK(feq(rec.u.batmon.temperature, 25.0f));
    CHECK(isnan(rec.u.batmon.aux_voltage));
    CHECK(feq(rec.u.batmon.current, -2.5f));
    CHECK(feq(rec.u.batmon.consumed_ah, -50.0f));
    CHECK(feq(rec.u.batmon.soc, 85.5f));

    /* --- inverter --- */
    CHECK(decode(VEC_INVERTER, sizeof(VEC_INVERTER), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_INVERTER);
    CHECK(rec.u.inverter.state == VICTRONBLE_STATE_INVERTING);
    CHECK(feq(rec.u.inverter.battery_voltage, 25.86f));
    CHECK(feq(rec.u.inverter.battery_current, -12.34f));
    CHECK(feq(rec.u.inverter.ac_power, -230.0f));
    CHECK(rec.u.inverter.alarms == 0x08);

    /* --- dc-dc converter --- */
    CHECK(decode(VEC_DCDC, sizeof(VEC_DCDC), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_DCDC_CONVERTER);
    CHECK(rec.u.dcdc.state == VICTRONBLE_STATE_FLOAT);
    CHECK(feq(rec.u.dcdc.input_voltage, 25.30f));
    CHECK(feq(rec.u.dcdc.output_voltage, 13.31f));
    CHECK(feq(rec.u.dcdc.output_current, 7.65f));
    CHECK(rec.nonce == 0xFFFF);

    /* --- ac charger --- */
    CHECK(decode(VEC_ACCHARGER, sizeof(VEC_ACCHARGER), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_AC_CHARGER);
    CHECK(rec.u.ac.state == VICTRONBLE_STATE_ABSORPTION);
    CHECK(feq(rec.u.ac.voltage1, 14.40f));
    CHECK(feq(rec.u.ac.current1, 10.0f));
    CHECK(isnan(rec.u.ac.voltage2) && isnan(rec.u.ac.current2));
    CHECK(isnan(rec.u.ac.voltage3) && isnan(rec.u.ac.current3));
    CHECK(feq(rec.u.ac.temperature, 35.0f));
    CHECK(feq(rec.u.ac.ac_current, 1.2f));

    /* --- multi RS collapses to the inverter decoder --- */
    CHECK(decode(VEC_MULTIRS, sizeof(VEC_MULTIRS), key, &rec) == VICTRONBLE_OK);
    CHECK(rec.type == VICTRONBLE_DEV_INVERTER);
    CHECK(rec.record_type == VICTRONBLE_DEV_MULTI_RS);
    CHECK(feq(rec.u.inverter.battery_voltage, 25.86f));

    /* --- negative cases --- */

    /* Known record type, no decoder */
    CHECK(decode(VEC_GX, sizeof(VEC_GX), key, &rec) == VICTRONBLE_ERR_UNSUPPORTED);

    /* Truncated: shorter than the header */
    CHECK(decode(VEC_SOLAR, 9, key, &rec) == VICTRONBLE_ERR_SHORT);
    CHECK(!victronble_is_product_adv(VEC_SOLAR, 9));

    /* Wrong company ID */
    {
        uint8_t bad[sizeof(VEC_SOLAR)];
        memcpy(bad, VEC_SOLAR, sizeof(bad));
        bad[0] = 0x4C; bad[1] = 0x00; /* Apple */
        CHECK(decode(bad, sizeof(bad), key, &rec) == VICTRONBLE_ERR_NOT_VICTRON);
        CHECK(!victronble_is_product_adv(bad, sizeof(bad)));
    }

    /* Not a product advertisement */
    {
        uint8_t bad[sizeof(VEC_SOLAR)];
        memcpy(bad, VEC_SOLAR, sizeof(bad));
        bad[2] = 0x01;
        CHECK(decode(bad, sizeof(bad), key, &rec) == VICTRONBLE_ERR_NOT_PRODUCT);
    }

    /* Wrong key: check byte catches it without decrypting */
    {
        uint8_t wrong_key[16];
        memcpy(wrong_key, key, 16);
        wrong_key[0] ^= 0xFF;
        CHECK(decode(VEC_SOLAR, sizeof(VEC_SOLAR), wrong_key, &rec) ==
              VICTRONBLE_ERR_KEY_MISMATCH);
        CHECK(!victronble_key_matches(VEC_SOLAR, sizeof(VEC_SOLAR), wrong_key));
    }

    /* Wrong key with a matching check byte: decrypts to garbage but must
     * not crash; solar parser accepts any bytes, so OK with junk values is
     * acceptable — just require no error other than OK/SHORT. */
    {
        uint8_t wrong_key[16];
        memcpy(wrong_key, key, 16);
        wrong_key[15] ^= 0xFF;
        victronble_err_t err = decode(VEC_SOLAR, sizeof(VEC_SOLAR), wrong_key, &rec);
        CHECK(err == VICTRONBLE_OK || err == VICTRONBLE_ERR_SHORT);
    }

    /* Key parsing */
    {
        uint8_t k[16];
        CHECK(!victronble_parse_key("00112233", k));            /* short */
        CHECK(!victronble_parse_key(NULL, k));
        CHECK(!victronble_parse_key("zz112233445566778899aabbccddeeff", k));
        CHECK(victronble_parse_key("00112233445566778899AABBCCDDEEFF", k));
        CHECK(k[0] == 0x00 && k[15] == 0xFF);
    }

    if (failures == 0) {
        printf("victronble core: all tests passed\n");
        return 0;
    }
    printf("victronble core: %d FAILURE(S)\n", failures);
    return 1;
}
