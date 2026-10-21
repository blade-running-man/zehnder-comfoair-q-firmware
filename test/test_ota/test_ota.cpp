// The pre-flight on an uploaded firmware image, and what the owner is told when it fails.
//
// Every refusal here happens BEFORE esp_ota_begin(), i.e. before a sector of the update slot is
// erased. A wrong answer in this file is either a good image refused (the owner cannot update) or a
// wrong file accepted far enough to wipe the spare slot -- so the boundaries are pinned exactly.
#include <unity.h>

#include <string.h>

#include "comfoair_ota.h"

static const size_t SLOT = 1984u * 1024u;  // partitions.csv: each OTA slot is 1984 KiB

// The first 24 bytes of a real build: `xxd -l 24 .pio/build/m5stack-nanoc6/firmware.bin` on
// 2026-10-17 (feat/rmi-probe). Byte 0 is the magic 0xE9; bytes 12-13 are chip_id 0x000D (ESP32-C6)
// little-endian, per esp_image_header_t in esp_app_format.h.
static const uint8_t REAL_C6_HEADER[COMFOAIR_OTA_HEADER_BYTES] = {
    0xe9, 0x05, 0x02, 0x20, 0x9a, 0x1a, 0x80, 0x40, 0xee, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01,
};
static const uint16_t CHIP_C6 = 0x000D;
static const uint16_t CHIP_C3 = 0x0005;

void setUp(void) {}
void tearDown(void) {}

// --- length ----------------------------------------------------------------------------------

void test_a_body_without_content_length_is_refused(void) {
  // Chunked uploads carry no length, so the slot fit cannot be judged before erasing.
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NO_LENGTH, comfoair_ota_check_length(false, 0, SLOT));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NO_LENGTH, comfoair_ota_check_length(false, 1300000, SLOT));
}

void test_the_minimum_size_is_an_inclusive_boundary(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_TOO_SMALL, comfoair_ota_check_length(true, 0, SLOT));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_TOO_SMALL,
                    comfoair_ota_check_length(true, COMFOAIR_OTA_MIN_IMAGE_BYTES - 1, SLOT));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK,
                    comfoair_ota_check_length(true, COMFOAIR_OTA_MIN_IMAGE_BYTES, SLOT));
}

void test_an_image_exactly_the_slot_size_fits_and_one_byte_more_does_not(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, comfoair_ota_check_length(true, SLOT, SLOT));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_TOO_LARGE, comfoair_ota_check_length(true, SLOT + 1, SLOT));
}

void test_a_typical_image_fits(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, comfoair_ota_check_length(true, 1300000, SLOT));
}

// --- header ----------------------------------------------------------------------------------

void test_a_real_c6_header_passes(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK,
                    comfoair_ota_check_header(REAL_C6_HEADER, sizeof REAL_C6_HEADER, CHIP_C6));
}

void test_more_than_the_header_is_fine(void) {
  uint8_t buf[64] = {};
  memcpy(buf, REAL_C6_HEADER, sizeof REAL_C6_HEADER);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
}

void test_a_c3_image_on_a_c6_is_the_wrong_chip(void) {
  uint8_t buf[COMFOAIR_OTA_HEADER_BYTES];
  memcpy(buf, REAL_C6_HEADER, sizeof buf);
  buf[12] = (uint8_t)(CHIP_C3 & 0xFF);
  buf[13] = (uint8_t)(CHIP_C3 >> 8);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_CHIP, comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
}

void test_the_chip_id_is_read_little_endian_from_both_bytes(void) {
  // 0x0D00 is not 0x000D: a reader that took only byte 12, or the bytes swapped, would pass this.
  uint8_t buf[COMFOAIR_OTA_HEADER_BYTES];
  memcpy(buf, REAL_C6_HEADER, sizeof buf);
  buf[12] = 0x00;
  buf[13] = 0x0d;
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_CHIP, comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
  buf[12] = 0x0d;
  buf[13] = 0x01;
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_CHIP, comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, comfoair_ota_check_header(buf, sizeof buf, 0x010D));
}

void test_a_gzip_file_is_not_an_image(void) {
  // The Update page's file filter once offered .bin.gz; the device cannot gunzip.
  uint8_t buf[COMFOAIR_OTA_HEADER_BYTES];
  memcpy(buf, REAL_C6_HEADER, sizeof buf);
  buf[0] = 0x1f;
  buf[1] = 0x8b;
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE,
                    comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
}

void test_an_elf_is_not_an_image(void) {
  uint8_t buf[COMFOAIR_OTA_HEADER_BYTES] = {0x7f, 'E', 'L', 'F'};
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE,
                    comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
}

void test_a_short_or_missing_header_is_not_an_image(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE,
                    comfoair_ota_check_header(REAL_C6_HEADER, COMFOAIR_OTA_HEADER_BYTES - 1,
                                              CHIP_C6));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE, comfoair_ota_check_header(REAL_C6_HEADER, 0, CHIP_C6));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE,
                    comfoair_ota_check_header(nullptr, COMFOAIR_OTA_HEADER_BYTES, CHIP_C6));
}

void test_the_magic_is_judged_before_the_chip(void) {
  // A gzip file whose bytes 12-13 happen to be wrong must still be told "not an image", the
  // actionable answer, not "different chip".
  uint8_t buf[COMFOAIR_OTA_HEADER_BYTES] = {0x1f, 0x8b};
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_NOT_AN_IMAGE,
                    comfoair_ota_check_header(buf, sizeof buf, CHIP_C6));
}

// --- app descriptor --------------------------------------------------------------------------

// The first 0x70 bytes of a real build: `xxd -l 112 .pio/build/m5stack-nanoc6/firmware.bin` on
// 2026-10-17 (built from cda52b6). 0x00-0x17 image header, 0x18-0x1F the first segment header,
// then esp_app_desc_t: magic 32 54 cd ab at 0x20, version "cda52b6" at 0x30, project_name
// "comfoair-q" at 0x50 (the name project() gives in the top-level CMakeLists.txt).
static const uint8_t REAL_C6_HEAD[COMFOAIR_OTA_PREFLIGHT_BYTES] = {
    0xe9, 0x05, 0x02, 0x20, 0xa6, 0x1a, 0x80, 0x40, 0xee, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00, 0x0f, 0x42, 0xec, 0x84, 0x05, 0x00,
    0x32, 0x54, 0xcd, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'c',  'd',  'a',  '5',  '2',  'b',  '6',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'c',  'o',  'm',  'f',  'o',  'a',  'i',  'r',  '-',  'q',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// The owner's other ESP firmware: `xxd -l 112` of
// ../opentherm-thermostat-firmware/.pio/build/lolin_c3_mini/firmware.bin (2026-09-22; the only build
// of it on disk -- there is no supermini_c6 one). project_name "opentherm-thermostat" at 0x50,
// from its CMakeLists.txt:3 `project(opentherm-thermostat)`, which is the same on its C6 target.
static const uint8_t OPENTHERM_C3_HEAD[COMFOAIR_OTA_PREFLIGHT_BYTES] = {
    0xe9, 0x06, 0x02, 0x2f, 0x2a, 0x0d, 0x38, 0x40, 0xee, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00, 0x0e, 0x3c, 0x50, 0x0e, 0x04, 0x00,
    0x32, 0x54, 0xcd, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    '1',  '3',  'f',  'e',  '6',  '0',  'd',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'o',  'p',  'e',  'n',  't',  'h',  'e',  'r',  'm',  '-',  't',  'h',  'e',  'r',  'm',  'o',
    's',  't',  'a',  't',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const char PROJECT[] = "comfoair-q";
static const size_t PROJECT_AT = 0x50;
static const size_t VERSION_AT = 0x30;

static comfoair_ota_fault_t check_desc(const uint8_t *head, size_t len, char *ver, size_t cap) {
  return comfoair_ota_check_app_desc(head, len, PROJECT, ver, cap);
}

void test_a_real_image_of_this_firmware_passes_and_yields_its_version(void) {
  char ver[32] = "garbage";
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(REAL_C6_HEAD, sizeof REAL_C6_HEAD, ver, sizeof ver));
  TEST_ASSERT_EQUAL_STRING("cda52b6", ver);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK,
                    comfoair_ota_check_header(REAL_C6_HEAD, sizeof REAL_C6_HEAD, CHIP_C6));
}

void test_the_opentherm_firmware_is_the_wrong_project(void) {
  // Its C3 build is already refused by the chip check; its C6 build differs only in the header's
  // chip_id, passes that check -- and is what this check exists for.
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_CHIP,
                    comfoair_ota_check_header(OPENTHERM_C3_HEAD, sizeof OPENTHERM_C3_HEAD, CHIP_C6));
  uint8_t c6[COMFOAIR_OTA_PREFLIGHT_BYTES];
  memcpy(c6, OPENTHERM_C3_HEAD, sizeof c6);
  c6[12] = 0x0d;
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, comfoair_ota_check_header(c6, sizeof c6, CHIP_C6));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(c6, sizeof c6, nullptr, 0));
}

void test_a_name_that_is_only_a_prefix_either_way_is_the_wrong_project(void) {
  uint8_t buf[COMFOAIR_OTA_PREFLIGHT_BYTES];
  memcpy(buf, REAL_C6_HEAD, sizeof buf);
  memcpy(buf + PROJECT_AT, "comfoair-q2", 12);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(buf, sizeof buf, nullptr, 0));
  memset(buf + PROJECT_AT, 0, 32);
  memcpy(buf + PROJECT_AT, "comfoair", 8);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(buf, sizeof buf, nullptr, 0));
  memset(buf + PROJECT_AT, 0, 32);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(buf, sizeof buf, nullptr, 0));
}

void test_a_bad_descriptor_magic_is_the_wrong_project(void) {
  // An image of some pre-descriptor or non-IDF build: there is no descriptor to trust a name in.
  uint8_t buf[COMFOAIR_OTA_PREFLIGHT_BYTES];
  memcpy(buf, REAL_C6_HEAD, sizeof buf);
  buf[0x20] = 0x33;
  char ver[32] = "x";
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(buf, sizeof buf, ver, sizeof ver));
  TEST_ASSERT_EQUAL_STRING("", ver);  // nothing from an untrusted descriptor reaches the log
  // Byte-swapped (read big-endian) must not pass either.
  memcpy(buf + 0x20, "\xab\xcd\x54\x32", 4);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(buf, sizeof buf, nullptr, 0));
}

void test_the_preflight_length_is_an_inclusive_boundary(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK,
                    check_desc(REAL_C6_HEAD, COMFOAIR_OTA_PREFLIGHT_BYTES, nullptr, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    check_desc(REAL_C6_HEAD, COMFOAIR_OTA_PREFLIGHT_BYTES - 1, nullptr, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    check_desc(REAL_C6_HEAD, COMFOAIR_OTA_HEADER_BYTES, nullptr, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT, check_desc(REAL_C6_HEAD, 0, nullptr, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    check_desc(nullptr, COMFOAIR_OTA_PREFLIGHT_BYTES, nullptr, 0));
  uint8_t more[COMFOAIR_OTA_PREFLIGHT_BYTES + 16] = {};
  memcpy(more, REAL_C6_HEAD, sizeof REAL_C6_HEAD);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(more, sizeof more, nullptr, 0));
}

void test_an_unterminated_project_name_is_the_wrong_project(void) {
  // 32 non-NUL bytes: a reader that ran on to byte 0x70 would compare memory past the field.
  uint8_t buf[COMFOAIR_OTA_PREFLIGHT_BYTES + 1];
  memcpy(buf, REAL_C6_HEAD, COMFOAIR_OTA_PREFLIGHT_BYTES);
  memset(buf + PROJECT_AT, 'q', 32);
  buf[COMFOAIR_OTA_PREFLIGHT_BYTES] = 0;
  const char *expected = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq";  // the same 32 chars
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    comfoair_ota_check_app_desc(buf, sizeof buf, expected, nullptr, 0));
  // 31 chars and the NUL in the last byte is the longest legal name, and it matches.
  buf[PROJECT_AT + 31] = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK,
                    comfoair_ota_check_app_desc(buf, sizeof buf, "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq",
                                                nullptr, 0));
}

void test_a_missing_expected_project_matches_nothing(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    comfoair_ota_check_app_desc(REAL_C6_HEAD, sizeof REAL_C6_HEAD, nullptr,
                                                nullptr, 0));
}

void test_the_version_copy_is_bounded_by_the_caller_and_by_the_field(void) {
  char tiny[4] = "zzz";
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(REAL_C6_HEAD, sizeof REAL_C6_HEAD, tiny, sizeof tiny));
  TEST_ASSERT_EQUAL_STRING("cda", tiny);
  char one[1] = {'z'};
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(REAL_C6_HEAD, sizeof REAL_C6_HEAD, one, sizeof one));
  TEST_ASSERT_EQUAL('\0', one[0]);
  char untouched[2] = {'z', 'z'};
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(REAL_C6_HEAD, sizeof REAL_C6_HEAD, untouched, 0));
  TEST_ASSERT_EQUAL('z', untouched[0]);  // cap 0: not one byte may be written
  // A version filling all 32 bytes without a NUL is cut at the field, not read into project_name.
  uint8_t buf[COMFOAIR_OTA_PREFLIGHT_BYTES];
  memcpy(buf, REAL_C6_HEAD, sizeof buf);
  memset(buf + VERSION_AT, '7', 32);
  char ver[64];
  memset(ver, 'x', sizeof ver);
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_OK, check_desc(buf, sizeof buf, ver, sizeof ver));
  TEST_ASSERT_EQUAL(32, strlen(ver));
  TEST_ASSERT_EQUAL('7', ver[31]);
}

void test_a_foreign_image_still_yields_its_version_for_the_log(void) {
  char ver[32] = "";
  TEST_ASSERT_EQUAL(COMFOAIR_OTA_WRONG_PROJECT,
                    check_desc(OPENTHERM_C3_HEAD, sizeof OPENTHERM_C3_HEAD, ver, sizeof ver));
  TEST_ASSERT_EQUAL_STRING("13fe60d", ver);
}

// --- answers ---------------------------------------------------------------------------------

static const comfoair_ota_fault_t FAULTS[] = {
    COMFOAIR_OTA_NO_LENGTH,     COMFOAIR_OTA_TOO_SMALL,   COMFOAIR_OTA_TOO_LARGE,
    COMFOAIR_OTA_NOT_AN_IMAGE,  COMFOAIR_OTA_WRONG_CHIP,  COMFOAIR_OTA_TRUNCATED,
    COMFOAIR_OTA_VERIFY_FAILED, COMFOAIR_OTA_BUSY,        COMFOAIR_OTA_FLASH_ERROR,
    COMFOAIR_OTA_NO_SLOT,       COMFOAIR_OTA_UNCONFIRMED, COMFOAIR_OTA_WRONG_PROJECT,
};

static bool starts_with_three_digits_and_space(const char *s) {
  return s[0] >= '1' && s[0] <= '5' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' &&
         s[2] <= '9' && s[3] == ' ' && s[4] != '\0';
}

void test_every_fault_has_a_message_and_a_status_line(void) {
  for (size_t i = 0; i < sizeof FAULTS / sizeof FAULTS[0]; i++) {
    const char *msg = comfoair_ota_message(FAULTS[i]);
    const char *line = comfoair_ota_status_line(FAULTS[i]);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_TRUE_MESSAGE(strlen(msg) > 0, "fault without a message");
    TEST_ASSERT_TRUE_MESSAGE(starts_with_three_digits_and_space(line), line);
    TEST_ASSERT_TRUE_MESSAGE(strncmp(line, "200", 3) != 0, "a fault answered 200");
  }
}

void test_ok_is_200_with_an_empty_message(void) {
  TEST_ASSERT_EQUAL_STRING("200 OK", comfoair_ota_status_line(COMFOAIR_OTA_OK));
  TEST_ASSERT_EQUAL_STRING("", comfoair_ota_message(COMFOAIR_OTA_OK));
}

void test_each_fault_maps_to_its_status(void) {
  TEST_ASSERT_EQUAL_STRING("411 Length Required", comfoair_ota_status_line(COMFOAIR_OTA_NO_LENGTH));
  TEST_ASSERT_EQUAL_STRING("422 Unprocessable Entity",
                           comfoair_ota_status_line(COMFOAIR_OTA_TOO_SMALL));
  TEST_ASSERT_EQUAL_STRING("422 Unprocessable Entity",
                           comfoair_ota_status_line(COMFOAIR_OTA_NOT_AN_IMAGE));
  TEST_ASSERT_EQUAL_STRING("422 Unprocessable Entity",
                           comfoair_ota_status_line(COMFOAIR_OTA_WRONG_CHIP));
  TEST_ASSERT_EQUAL_STRING("422 Unprocessable Entity",
                           comfoair_ota_status_line(COMFOAIR_OTA_VERIFY_FAILED));
  TEST_ASSERT_EQUAL_STRING("422 Unprocessable Entity",
                           comfoair_ota_status_line(COMFOAIR_OTA_WRONG_PROJECT));
  TEST_ASSERT_EQUAL_STRING("413 Content Too Large",
                           comfoair_ota_status_line(COMFOAIR_OTA_TOO_LARGE));
  TEST_ASSERT_EQUAL_STRING("400 Bad Request", comfoair_ota_status_line(COMFOAIR_OTA_TRUNCATED));
  TEST_ASSERT_EQUAL_STRING("409 Conflict", comfoair_ota_status_line(COMFOAIR_OTA_BUSY));
  TEST_ASSERT_EQUAL_STRING("409 Conflict", comfoair_ota_status_line(COMFOAIR_OTA_UNCONFIRMED));
  TEST_ASSERT_EQUAL_STRING("500 Internal Server Error",
                           comfoair_ota_status_line(COMFOAIR_OTA_FLASH_ERROR));
  TEST_ASSERT_EQUAL_STRING("500 Internal Server Error",
                           comfoair_ota_status_line(COMFOAIR_OTA_NO_SLOT));
}

void test_the_not_an_image_message_points_at_firmware_bin(void) {
  // The owner's likely mistakes are the .elf beside it or a compressed file; the message names the
  // right one.
  TEST_ASSERT_NOT_NULL(strstr(comfoair_ota_message(COMFOAIR_OTA_NOT_AN_IMAGE), "firmware.bin"));
}

void test_the_wrong_project_message_is_the_sentence_the_web_mirrors(void) {
  // The Update page shows this same sentence for a file it judges foreign before uploading; the
  // two must read identically, so it is pinned word for word.
  TEST_ASSERT_EQUAL_STRING(
      "This file is firmware for a different device, not the ComfoAir Q controller.",
      comfoair_ota_message(COMFOAIR_OTA_WRONG_PROJECT));
}

void test_an_unknown_fault_still_gets_an_answer(void) {
  // The header promises "never NULL"; a value from a newer enum must not crash the handler.
  const comfoair_ota_fault_t bogus = (comfoair_ota_fault_t)999;
  TEST_ASSERT_TRUE(starts_with_three_digits_and_space(comfoair_ota_status_line(bogus)));
  TEST_ASSERT_TRUE(strlen(comfoair_ota_message(bogus)) > 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_body_without_content_length_is_refused);
  RUN_TEST(test_the_minimum_size_is_an_inclusive_boundary);
  RUN_TEST(test_an_image_exactly_the_slot_size_fits_and_one_byte_more_does_not);
  RUN_TEST(test_a_typical_image_fits);
  RUN_TEST(test_a_real_c6_header_passes);
  RUN_TEST(test_more_than_the_header_is_fine);
  RUN_TEST(test_a_c3_image_on_a_c6_is_the_wrong_chip);
  RUN_TEST(test_the_chip_id_is_read_little_endian_from_both_bytes);
  RUN_TEST(test_a_gzip_file_is_not_an_image);
  RUN_TEST(test_an_elf_is_not_an_image);
  RUN_TEST(test_a_short_or_missing_header_is_not_an_image);
  RUN_TEST(test_the_magic_is_judged_before_the_chip);
  RUN_TEST(test_a_real_image_of_this_firmware_passes_and_yields_its_version);
  RUN_TEST(test_the_opentherm_firmware_is_the_wrong_project);
  RUN_TEST(test_a_name_that_is_only_a_prefix_either_way_is_the_wrong_project);
  RUN_TEST(test_a_bad_descriptor_magic_is_the_wrong_project);
  RUN_TEST(test_the_preflight_length_is_an_inclusive_boundary);
  RUN_TEST(test_an_unterminated_project_name_is_the_wrong_project);
  RUN_TEST(test_a_missing_expected_project_matches_nothing);
  RUN_TEST(test_the_version_copy_is_bounded_by_the_caller_and_by_the_field);
  RUN_TEST(test_a_foreign_image_still_yields_its_version_for_the_log);
  RUN_TEST(test_every_fault_has_a_message_and_a_status_line);
  RUN_TEST(test_ok_is_200_with_an_empty_message);
  RUN_TEST(test_each_fault_maps_to_its_status);
  RUN_TEST(test_the_not_an_image_message_points_at_firmware_bin);
  RUN_TEST(test_the_wrong_project_message_is_the_sentence_the_web_mirrors);
  RUN_TEST(test_an_unknown_fault_still_gets_an_answer);
  return UNITY_END();
}
