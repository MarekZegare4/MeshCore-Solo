#include <gtest/gtest.h>

#include <cstring>
#include <string>

// NodePrefs saved as /prefs.json (NodePrefs::structure()) and read back: every
// field comes back as it was -- one without a key would come back zero. The
// fields are packed here (this test only), so that comparing them byte for
// byte has no padding in it, which no key holds. The serializer's own headers
// first, unpacked, as the library they're compiled into has them.
#include <helpers/ConfigSerializer.h>
#include <helpers/DynamicConfigSerializer.h>
#pragma pack(push, 1)
#include "../../examples/companion_radio/NodePrefs.h"
#pragma pack(pop)

namespace {

class StringOut : public Stream {
public:
  std::string text;
  size_t write(uint8_t b) override { text += (char)b; return 1; }
  size_t print(unsigned char v, int) override { return out(std::to_string(v)); }
  size_t print(int v, int) override { return out(std::to_string(v)); }
  size_t print(unsigned int v, int) override { return out(std::to_string(v)); }
  size_t print(long v, int) override { return out(std::to_string(v)); }
  size_t print(unsigned long v, int) override { return out(std::to_string(v)); }
  size_t print(long long v, int) override { return out(std::to_string(v)); }
  size_t print(unsigned long long v, int) override { return out(std::to_string(v)); }
  size_t print(double v, int p) override {
    char t[48];
    snprintf(t, sizeof(t), "%.*f", p, v);
    return out(t);
  }
private:
  size_t out(const std::string& s) { text += s; return s.size(); }
};

class StringIn : public Stream {
  const std::string& _s;
  size_t _at = 0;
public:
  explicit StringIn(const std::string& s) : _s(s) { }
  int available() override { return (int)(_s.size() - _at); }
  int read() override { return _at < _s.size() ? (uint8_t)_s[_at++] : -1; }
  int peek() override { return _at < _s.size() ? (uint8_t)_s[_at] : -1; }
};

// Every byte of the fields a different value; texts printable (quotes and
// backslashes among them) and terminated; floats ones the file's four
// decimals keep exactly.
void fill(NodePrefs& p) {
  uint8_t* b = (uint8_t*)static_cast<NodePrefsFields*>(&p);
  for (size_t i = 0; i < sizeof(NodePrefsFields); i++) b[i] = (uint8_t)(33 + (i * 7) % 90);
  auto text = [](char* s, size_t n) { s[n - 1] = 0; };
  text(p.node_name, sizeof(p.node_name));
  text(p.default_scope_name, sizeof(p.default_scope_name));
  text(p.bot_trigger, sizeof(p.bot_trigger));
  text(p.bot_reply_dm, sizeof(p.bot_reply_dm));
  text(p.bot_reply_ch, sizeof(p.bot_reply_ch));
  text(p.bot_trigger_ch, sizeof(p.bot_trigger_ch));
  text(p.bot_trigger_room, sizeof(p.bot_trigger_room));
  text(p.bot_reply_room, sizeof(p.bot_reply_room));
  text(p.locator_label, sizeof(p.locator_label));
  text(p.sos_ack, sizeof(p.sos_ack));
  text(p.sos_note, sizeof(p.sos_note));
  for (auto& m : p.custom_msgs) text(m, sizeof(m));
  for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
    auto& r = p.user_radio_presets[i];
    text(r.name, sizeof(r.name));
    r.freq = 869.525f + i; r.bw = 62.5f; 
  }
  p.airtime_factor = 1.5f; p.freq = 869.618f; p.bw = 62.5f; p.rx_delay_base = 2.25f;
  p.tx_delay_factor = 0.75f; p.direct_tx_delay_factor = 0.125f;
  p.repeater_freq = 433.875f; p.repeater_bw = 125.0f;
  p.client_repeat = 1;   // a flag in the file (as upstream's "disable")
}

}  // namespace

TEST(NodePrefsJson, EveryFieldRoundTrips) {
  NodePrefs a;
  fill(a);
  a.node_lat = 50.061234; a.node_lon = 19.937654;
  StringOut out;
  ASSERT_TRUE(a.saveSerial(out));

  NodePrefs b;
  StringIn in(out.text);
  ASSERT_TRUE(b.loadSerial(in)) << out.text;
  const uint8_t* pa = (const uint8_t*)static_cast<NodePrefsFields*>(&a);
  const uint8_t* pb = (const uint8_t*)static_cast<NodePrefsFields*>(&b);
  for (size_t i = 0; i < sizeof(NodePrefsFields); i++)
    ASSERT_EQ(pa[i], pb[i]) << "first difference at byte " << i << " of NodePrefsFields";
  EXPECT_DOUBLE_EQ(a.node_lat, b.node_lat);
  EXPECT_DOUBLE_EQ(a.node_lon, b.node_lon);
}

TEST(NodePrefsJson, UpstreamKeysWhereUpstreamHasThem) {
  NodePrefs a;
  strcpy(a.node_name, "Kraków 1");
  a.freq = 869.618f; a.sf = 8; a.tx_power_dbm = 22; a.client_repeat = 1; a.tz_offset_hours = 2;
  StringOut out;
  ASSERT_TRUE(a.saveSerial(out));
  const std::string& t = out.text;
  EXPECT_NE(std::string::npos, t.find("name:\"Kraków 1\""));
  EXPECT_NE(std::string::npos, t.find("radio:{freq:869.6180"));
  EXPECT_NE(std::string::npos, t.find("sf:8"));
  EXPECT_NE(std::string::npos, t.find("tx:22"));
  EXPECT_NE(std::string::npos, t.find("repeat:{disable:0}"));
  EXPECT_NE(std::string::npos, t.find("tz_offset:2"));
}

TEST(NodePrefsJson, AMissingKeyKeepsItsDefault) {
  NodePrefs b;
  b.sf = 9; b.buzzer_volume = 3;
  StringIn in(std::string("{radio:{sf:11}}"));
  ASSERT_TRUE(b.loadSerial(in));
  EXPECT_EQ(11, b.sf);
  EXPECT_EQ(3, b.buzzer_volume);
}

TEST(NodePrefsJson, BoardSettingsGoToTheFemFields) {
  NodePrefs p;
  KeyValueStore* kv = p.getCustom();
  ASSERT_TRUE(kv->setByKey("fem_rxgain", "1"));
  EXPECT_EQ(1, p.radio_fem_rxgain);
  EXPECT_TRUE(p.isDirty());
  char v[8];
  ASSERT_TRUE(kv->getByKey("fem_rxgain", v, sizeof(v) - 1));
  EXPECT_STREQ("1", v);
  ASSERT_TRUE(kv->setByKey("fan", "2"));   // a key with no field: kept in "custom"
  StringOut out;
  ASSERT_TRUE(p.saveSerial(out));
  EXPECT_NE(std::string::npos, out.text.find("custom:{fan:\"2\"}"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
