#include "logitacker_unifying_crypto.h"
#include "logitacker_unifying.h"
#include "sdk_common.h"
#include "nrf_crypto.h"
#include "nrf_crypto_error.h"

#define NRF_LOG_MODULE_NAME LOGITACKER_UNIFYING_CRYPTO
#include "nrf_log.h"
#include "logitacker_devices.h"
#include "logitacker_options.h"

NRF_LOG_MODULE_REGISTER();


static uint8_t little_known_secret[16] = { 0x04, 0x14, 0x1d, 0x1f, 0x27, 0x28, 0x0d, 0xde, 0xad, 0xbe, 0xef, 0x0a, 0x0d, 0x13, 0x26, 0x0e };

nrf_crypto_aes_info_t const * p_ecb_info = &g_nrf_crypto_aes_ecb_128_info;
static nrf_crypto_aes_context_t * p_ecb_encr_ctx = NULL;

void update_little_known_secret_counter(uint8_t const * const counter) {
    memcpy(&little_known_secret[7], counter, 4);
}

uint32_t logitacker_unifying_crypto_aes_ecb_encrypt(uint8_t *cipher, uint8_t * key, uint8_t * plain) {
    size_t len_in = 16;
    size_t len_out =16;
    ret_code_t ret_val;

    ret_val = nrf_crypto_aes_crypt(p_ecb_encr_ctx,
                                   p_ecb_info,
                                   NRF_CRYPTO_ENCRYPT,
                                   key,
                                   NULL,
                                   plain,
                                   len_in,
                                   cipher,
                                   &len_out);
    if (ret_val != NRF_SUCCESS) {
        NRF_LOG_WARNING("error nrf_crypto_aes_crypt: %d", ret_val);
    } else {
        NRF_LOG_DEBUG("cipher (%d bytes):", len_out);
        NRF_LOG_HEXDUMP_DEBUG(cipher, 16);
    }

    return ret_val;
}

uint32_t logitacker_unifying_crypto_calculate_frame_key(uint8_t * result, uint8_t * device_key, uint8_t * counter) {
    update_little_known_secret_counter(counter);
    return logitacker_unifying_crypto_aes_ecb_encrypt(result, device_key, little_known_secret);
}

uint32_t logitacker_unifying_crypto_decrypt_encrypted_keyboard_frame(uint8_t * result, uint8_t * device_key, nrf_esb_payload_t * rf_frame) {
    // validate frame
    VERIFY_PARAM_NOT_NULL(result);
    VERIFY_PARAM_NOT_NULL(device_key);
    VERIFY_PARAM_NOT_NULL(rf_frame);
    VERIFY_TRUE(rf_frame->length == 22, NRF_ERROR_INVALID_DATA);
    VERIFY_TRUE((rf_frame->data[1] & 0x1f) == 0x13, NRF_ERROR_INVALID_DATA); //encrypted keyboard frame

    ret_code_t ret_val;


    // extract counter
    uint8_t counter[4] = { 0 };
    memcpy(counter, &rf_frame->data[10], 4);

    //generate frame key
    uint8_t frame_key[16] = { 0 };
    ret_val = logitacker_unifying_crypto_calculate_frame_key(frame_key, device_key, counter);
    VERIFY_SUCCESS(ret_val);

    NRF_LOG_DEBUG("Frame key:");
    NRF_LOG_HEXDUMP_DEBUG(frame_key,16);

    //copy cipher part to result
    memcpy(result, &rf_frame->data[2], 8);

    // xor decrypt with relevant part of AES key (yes, only half of the key - generated from high-entropy input data - is used)
    for (int i=0; i<8; i++) {
        result[i] ^= frame_key[i];
    }

    return NRF_SUCCESS;
}

uint32_t logitacker_unifying_crypto_decrypt_encrypted_hidpp_frame(uint8_t * result, uint8_t * device_key, nrf_esb_payload_t * rf_frame) {
    /*
     * <info> app: Unifying RF frame: ENCRYPTED HID++ long
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  00 5B 5F BF 8C 1A 86 A6|.[_.....
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  4F 0A 94 51 74 16 EE 20|O..Qt..
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  9F C0 21 37 9A B6 85 B1|..!7....
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  29 12 17 45 0C FF      |)..E..
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM: frame RX in passive enumeration mode (addr C6:1B:34:4A:26, len: 30, ch idx 17, raw ch 56)
<info> app: Unifying RF frame: ENCRYPTED HID++ long
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  00 5B F2 B2 8B 04 60 BA|.[....`.
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  E8 EF 06 61 75 D2 D7 BA|...au...
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  E2 FA 29 A7 84 C5 32 AF|..)...2.
<info> LOGITACKER_PROCESSOR_PASIVE_ENUM:  9D 14 17 45 0C 53      |...E.S

     */


    // validate frame
    VERIFY_PARAM_NOT_NULL(result);
    VERIFY_PARAM_NOT_NULL(device_key);
    VERIFY_PARAM_NOT_NULL(rf_frame);
    VERIFY_TRUE(rf_frame->length == 30, NRF_ERROR_INVALID_DATA);
    VERIFY_TRUE((rf_frame->data[1] & 0x1f) == UNIFYING_RF_REPORT_ENCRYPTED_HIDPP_LONG, NRF_ERROR_INVALID_DATA); //encrypted keyboard frame

    ret_code_t ret_val;

    //two successive counters are used to calculate two keys

    // extract counter
    uint8_t counter[4] = { 0 };
    uint8_t counter2[4] = { 0 };
    memcpy(counter, &rf_frame->data[0x19], 4);

    uint32_t tmp_counter = * ((uint32_t *) counter);
    uint32_t tmp_counter2 = tmp_counter;
    tmp_counter2++;
    memcpy(counter2, &tmp_counter2, 4);

    NRF_LOG_INFO("COUNTER1: %08x", tmp_counter)
    NRF_LOG_INFO("COUNTER1: %08x", tmp_counter2)

    //generate frame key1
    uint8_t frame_key1[16] = { 0 };
    ret_val = logitacker_unifying_crypto_calculate_frame_key(frame_key1, device_key, counter);
    VERIFY_SUCCESS(ret_val);
    //generate frame key2
    uint8_t frame_key2[16] = { 0 };
    ret_val = logitacker_unifying_crypto_calculate_frame_key(frame_key2, device_key, counter2);
    VERIFY_SUCCESS(ret_val);

    NRF_LOG_INFO("Frame key1:");
    NRF_LOG_HEXDUMP_INFO(frame_key1,16);
    NRF_LOG_INFO("Frame key2:");
    NRF_LOG_HEXDUMP_INFO(frame_key2,16);

    //copy cipher part to result
    memcpy(result, &rf_frame->data[2], 23);

    // xor decrypt with relevant part of AES key (yes, only half of the key - generated from high-entropy input data - is used)
    for (int i=0; i<16; i++) {
        result[i] ^= frame_key1[i];
    }
    for (int i=0; i<7; i++) {
        result[16+i] ^= frame_key2[i];
    }

    NRF_LOG_INFO("Decrypt:");
    NRF_LOG_HEXDUMP_INFO(result,23);

    return NRF_SUCCESS;
}

uint32_t logitacker_unifying_crypto_encrypt_keyboard_frame(nrf_esb_payload_t * result_rf_frame, uint8_t * plain_payload, logitacker_device_unifying_device_key_t device_key, uint32_t counter) {
    // validate frame
    VERIFY_PARAM_NOT_NULL(plain_payload);
    VERIFY_PARAM_NOT_NULL(device_key);
    VERIFY_PARAM_NOT_NULL(result_rf_frame);

    result_rf_frame->length = 22;
    memset(result_rf_frame->data, 0x00, result_rf_frame->length);
    result_rf_frame->data[1] = 0xd3;

    ret_code_t ret_val;

    // add in counter
    uint8_t counter_bytes[4] = { 0 };
    if (g_logitacker_global_config.workmode == OPTION_LOGITACKER_WORKMODE_LIGHTSPEED) {
        counter_bytes[3] = (uint8_t) ((counter & 0xff000000) >> 24);
        counter_bytes[2] = (uint8_t) ((counter & 0x00ff0000) >> 16);
        counter_bytes[1] = (uint8_t) ((counter & 0x0000ff00) >> 8);
        counter_bytes[0] = (uint8_t) (counter & 0x000000ff);
    } else {
        counter_bytes[0] = (uint8_t) ((counter & 0xff000000) >> 24);
        counter_bytes[1] = (uint8_t) ((counter & 0x00ff0000) >> 16);
        counter_bytes[2] = (uint8_t) ((counter & 0x0000ff00) >> 8);
        counter_bytes[3] = (uint8_t) (counter & 0x000000ff);
    }

    // extract counter
    memcpy(&result_rf_frame->data[10], counter_bytes, 4);

    //generate frame key
    uint8_t frame_key[16] = { 0 };
    ret_val = logitacker_unifying_crypto_calculate_frame_key(frame_key, device_key, counter_bytes);
    VERIFY_SUCCESS(ret_val);

    //copy cipher part to result
    memcpy(&result_rf_frame->data[2], plain_payload, 8);

    //assure 0xC9 for last byte
    result_rf_frame->data[9] = 0xC9;

    // xor decrypt with relevant part of AES key (yes, only half of the key - generated from high-entropy input data - is used)
    for (int i=0; i<8; i++) {
        result_rf_frame->data[i+2] ^= frame_key[i];
    }

    if (g_logitacker_global_config.workmode == OPTION_LOGITACKER_WORKMODE_LIGHTSPEED) {
        //for LIGHTSPEED
        result_rf_frame->data[14] = 0xC9;
        result_rf_frame->data[15] = 0xC9;
        result_rf_frame->data[16] = 0xC9;
        result_rf_frame->data[17] = 0xC9;
        for (int i=8; i<12; i++) {
            result_rf_frame->data[i+6] ^= frame_key[i];
        }
        //end LIGHTSPEED
    }

    // update Logitech CRC
    logitacker_unifying_payload_update_checksum(result_rf_frame->data, result_rf_frame->length);

    return NRF_SUCCESS;
}

