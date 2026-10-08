#include <unity.h>

#include "router.h"
#include "util.h"

void setUp() {}
void tearDown() {}

static Router makeRouter() {
  Router r;
  r.on("ping", [](JsonObjectConst, JsonObject data) {
    data["fw"] = "test";
    return Err::kOk;
  });
  r.on("fail", [](JsonObjectConst, JsonObject) { return Err::kNoCard; });
  r.on("echo", [](JsonObjectConst args, JsonObject data) {
    data["n"] = args["n"];
    return Err::kOk;
  });
  return r;
}

// Key order in the reply is not part of the contract, so success replies are
// checked by value rather than by string.
static JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, s));
  return doc;
}

void test_ping_ok() {
  Router r = makeRouter();
  const JsonDocument res = parse(r.handle(R"({"id":1,"cmd":"ping"})"));
  TEST_ASSERT_EQUAL(1, res["id"].as<int>());
  TEST_ASSERT_TRUE(res["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("test", res["data"]["fw"].as<const char*>());
}

void test_handler_error() {
  Router r = makeRouter();
  TEST_ASSERT_EQUAL_STRING(
      R"({"id":2,"ok":false,"error":{"code":"NO_CARD","msg":"No card in the field"}})",
      r.handle(R"({"id":2,"cmd":"fail"})").c_str());
}

void test_args_passed() {
  Router r = makeRouter();
  const JsonDocument res = parse(r.handle(R"({"id":"a","cmd":"echo","args":{"n":7}})"));
  TEST_ASSERT_EQUAL_STRING("a", res["id"].as<const char*>());
  TEST_ASSERT_TRUE(res["ok"].as<bool>());
  TEST_ASSERT_EQUAL(7, res["data"]["n"].as<int>());
}

void test_unknown_cmd() {
  Router r = makeRouter();
  const std::string res = r.handle(R"({"id":3,"cmd":"nope"})");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, res.find("UNKNOWN_CMD"));
}

void test_invalid_json() {
  Router r = makeRouter();
  const std::string res = r.handle("not json");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, res.find(R"("id":null)"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, res.find("BAD_ARGS"));
}

void test_missing_cmd() {
  Router r = makeRouter();
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.handle(R"({"id":4})").find("BAD_ARGS"));
}

void test_publish_to_all_sinks() {
  Router r;
  std::string a, b;
  r.addSink([&](const std::string& s) { a = s; });
  r.addSink([&](const std::string& s) { b = s; });
  JsonDocument data;
  data["uid"] = "DE AD BE EF";
  r.publish("tag", data);
  TEST_ASSERT_EQUAL_STRING(R"({"event":"tag","data":{"uid":"DE AD BE EF"}})", a.c_str());
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

void test_writable_blocks() {
  TEST_ASSERT_FALSE(mifare::isWritable(0));   // UID
  TEST_ASSERT_FALSE(mifare::isWritable(3));   // trailer
  TEST_ASSERT_FALSE(mifare::isWritable(63));  // trailer
  TEST_ASSERT_FALSE(mifare::isWritable(64));  // out of range
  TEST_ASSERT_TRUE(mifare::isWritable(1));
  TEST_ASSERT_TRUE(mifare::isWritable(4));
  TEST_ASSERT_TRUE(mifare::isWritable(62));
}

void test_hex_roundtrip() {
  uint8_t key[6];
  TEST_ASSERT_TRUE(hex::decode("a0B1c2D3e4F5", key, sizeof(key)));
  TEST_ASSERT_EQUAL_STRING("A0B1C2D3E4F5", hex::encode(key, sizeof(key)).c_str());
  TEST_ASSERT_FALSE(hex::decode("A0B1C2D3E4", key, sizeof(key)));      // short
  TEST_ASSERT_FALSE(hex::decode("A0B1C2D3E4F5FF", key, sizeof(key)));  // long
  TEST_ASSERT_FALSE(hex::decode("ZZB1C2D3E4F5", key, sizeof(key)));
}

void test_soc_curve() {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, soc::fromVoltage(4.25f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, soc::fromVoltage(3.0f));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, soc::fromVoltage(3.84f));
  const float mid = soc::fromVoltage(4.05f);  // between 80 and 85
  TEST_ASSERT_TRUE(mid > 80.0f && mid < 85.0f);
}

void test_reader_modes() {
  reader::Mode m = reader::Mode::kRc522;
  TEST_ASSERT_TRUE(reader::parse("pn532+rdm6300", m));
  TEST_ASSERT_TRUE(m == reader::Mode::kPn532Rdm6300);
  TEST_ASSERT_EQUAL_STRING("pn532+rdm6300", reader::toString(m));
  TEST_ASSERT_TRUE(reader::hasPn532(m) && reader::hasRdm6300(m) && !reader::hasRc522(m));
  TEST_ASSERT_FALSE(reader::parse("rc522+pn532", m));  // not offered
  TEST_ASSERT_FALSE(reader::parse(nullptr, m));
  TEST_ASSERT_TRUE(reader::fromIndex(2) == reader::Mode::kRdm6300);
  TEST_ASSERT_TRUE(reader::fromIndex(99) == reader::Mode::kRc522);
}

void test_em4100_frame() {
  // 0x02, "0F00A1B2C3", checksum "DF" (XOR of the five bytes), 0x03
  const uint8_t frame[] = {0x02, '0', 'F', '0', '0', 'A', '1', 'B', '2', 'C', '3', 'D', 'F', 0x03};
  uint8_t id[em4100::kIdLen];
  TEST_ASSERT_TRUE(em4100::parseFrame(frame, id));
  const uint8_t expected[] = {0x0F, 0x00, 0xA1, 0xB2, 0xC3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, id, sizeof(expected));

  uint8_t bad[sizeof(frame)];
  memcpy(bad, frame, sizeof(frame));
  bad[12] = 'E';  // wrong checksum
  TEST_ASSERT_FALSE(em4100::parseFrame(bad, id));
  memcpy(bad, frame, sizeof(frame));
  bad[13] = 0x00;  // no end byte
  TEST_ASSERT_FALSE(em4100::parseFrame(bad, id));
  memcpy(bad, frame, sizeof(frame));
  bad[3] = 'G';  // not hex
  TEST_ASSERT_FALSE(em4100::parseFrame(bad, id));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ping_ok);
  RUN_TEST(test_handler_error);
  RUN_TEST(test_args_passed);
  RUN_TEST(test_unknown_cmd);
  RUN_TEST(test_invalid_json);
  RUN_TEST(test_missing_cmd);
  RUN_TEST(test_publish_to_all_sinks);
  RUN_TEST(test_writable_blocks);
  RUN_TEST(test_hex_roundtrip);
  RUN_TEST(test_soc_curve);
  RUN_TEST(test_reader_modes);
  RUN_TEST(test_em4100_frame);
  return UNITY_END();
}
