#pragma once

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <esp_gattc_api.h>

#include <array>
#include <vector>

namespace esphome {
namespace masterbuilt_smoker {

struct StoredIdentity {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved[3];
  uint64_t smoker_address;
  std::array<uint8_t, 8> grill_half;
  uint32_t checksum;
};

class MasterbuiltSmoker;

class ForgetPairingButton : public button::Button {
 public:
  void set_parent(MasterbuiltSmoker *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;

  MasterbuiltSmoker *parent_{nullptr};
};

// The smoker speaks a two-round challenge/response over a plain (unencrypted) GATT link.
//
//   Service 426f7567-6854-6563-2d57-65694c69fff0
//     fff1 (write)  handle 0x0025   - commands
//     fff2 (read)   handle 0x0027   - challenge / ack responses
//     fff4 (notify) handle 0x002b   - telemetry frames (CCCD 0x002c)
//
// ROUND 1 (first pairing only - needs the rotating code from the advertisement):
//   W fff1  0e08 + XOR(MAC_KEY, pair_code)
//   R fff2  0a08 + grill_half           <- the smoker's 8-byte identity, learned + stored
//   W fff1  0e08 + grill_half           (echo)
//   R fff2  0b..                        (ack)
//   W fff4-CCCD 0100  ;  W fff1 0f0000   (start) -> smoker disconnects on purpose
//
// ROUND 2 (after the round-1 disconnect, and for every later reconnect):
//   W fff1  0e08 + XOR(MAC_KEY, grill_half)
//   R fff2  0a08 + grill_half
//   W fff4-CCCD 0100                     -> fff4 begins streaming telemetry
//
// Once the grill_half is known it is saved to flash, so after a reboot the device skips
// straight to round 2 and reconnects with no pairing button (like the phone app's reconnect).
class MasterbuiltSmoker : public Component, public ble_client::BLEClientNode, public esp32_ble_tracker::ESPBTDeviceListener {
 public:
  void dump_config() override { ESP_LOGCONFIG(TAG, "Masterbuilt Smoker BLE client"); }
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void setup() override {
    if (this->configured_address_ != 0) {
      this->lock_address_(this->configured_address_, BLE_ADDR_TYPE_PUBLIC, 0, true);
    }

    this->pref_ = global_preferences->make_preference<StoredIdentity>(GRILL_HALF_PREF);
    StoredIdentity saved{};
    if (this->pref_.load(&saved) && this->validate_identity_record_(saved) &&
        (this->configured_address_ == 0 || saved.smoker_address == this->configured_address_)) {
      if (this->locked_address_ == 0)
        this->lock_address_(saved.smoker_address, BLE_ADDR_TYPE_PUBLIC, 0, false);
      this->grill_half_ = saved.grill_half;
      this->have_grill_half_ = true;
      this->do_round2_ = true;
      this->log_bytes_(ESP_LOG_INFO, "Loaded stored pairing; will reconnect automatically", saved.grill_half.data(), 8);
    } else {
      ESP_LOGI(TAG, "No stored pairing yet - put the smoker in pairing mode for first-time setup");
    }
  }

  // True once we know this smoker's identity, i.e. we can reconnect without the pairing button.
  bool is_paired() const { return this->have_grill_half_; }

  void set_smoker_address(uint64_t address) { this->configured_address_ = address; }

  void forget_pairing() {
    ESP_LOGI(TAG, "Forgetting stored smoker pairing");
    this->have_pair_payload_ = false;
    this->have_grill_half_ = false;
    this->do_round2_ = false;
    this->connect_started_ = false;
    this->pair_payload_.fill(0);
    this->grill_half_.fill(0);
    this->reset_session_();

    StoredIdentity blank{};
    blank.magic = IDENTITY_MAGIC;
    blank.version = IDENTITY_VERSION;
    blank.smoker_address = this->parent()->get_address();
    blank.grill_half.fill(0);
    blank.checksum = this->identity_checksum_(blank);
    if (!this->pref_.save(&blank)) {
      ESP_LOGW(TAG, "Failed to overwrite stored smoker identity");
    } else {
      global_preferences->sync();
    }

    this->parent()->disconnect();
  }

  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override {
    const uint64_t address = device.address_uint64();
    if (this->configured_address_ != 0 && address != this->configured_address_)
      return false;
    if (this->locked_address_ != 0 && address != this->locked_address_)
      return false;

    const std::vector<uint8_t> *manufacturer_payload = nullptr;
    for (const auto &manufacturer_data : device.get_manufacturer_datas()) {
      if (manufacturer_data.uuid == esp32_ble_tracker::ESPBTUUID::from_uint16(COMPANY_ID)) {
        manufacturer_payload = &manufacturer_data.data;
        break;
      }
    }
    if (manufacturer_payload == nullptr)
      return false;

    if (this->locked_address_ == 0)
      this->lock_address_(address, device.get_address_type(), device.get_rssi(), false);
    else
      this->parent()->set_remote_addr_type(device.get_address_type());

    bool got_code = false;
    if (!this->connect_started_ && manufacturer_payload->size() >= 14) {
      std::vector<uint8_t> payload(8);
      const size_t off = manufacturer_payload->size() - 8;
      for (int i = 0; i < 8; i++)
        payload[i] = MAC_KEY[i] ^ (*manufacturer_payload)[off + i];
      this->set_pair_payload(payload);
      got_code = true;
    }

    if (!this->connect_started_ && (got_code || this->is_paired())) {
      this->connect_started_ = true;
      this->parent()->connect();
    }
    return true;
  }

  void set_chamber_temperature(sensor::Sensor *s) { this->chamber_temp_ = s; }
  void set_target_temperature(sensor::Sensor *s) { this->target_temp_ = s; }
  void set_cook_time(sensor::Sensor *s) { this->cook_time_ = s; }
  void set_time_remaining(sensor::Sensor *s) { this->time_remaining_ = s; }
  void set_probe(int i, sensor::Sensor *s) {
    if (i >= 0 && i < 4)
      this->probes_[i] = s;
  }
  void set_door_sensor(binary_sensor::BinarySensor *s) { this->door_sensor_ = s; }
  void set_temp_error_sensor(binary_sensor::BinarySensor *s) { this->temp_error_sensor_ = s; }

  // Called from the advertisement handler when the smoker is in pairing mode and broadcasts a
  // fresh code. Forces a full round 1 so we (re)learn this unit's grill_half.
  void set_pair_payload(const std::vector<uint8_t> &payload) {
    if (this->session_active_ || this->step_ != Step::IDLE) {
      ESP_LOGD(TAG, "Ignoring pairing code while a BLE session is active");
      return;
    }
    if (payload.size() != this->pair_payload_.size()) {
      ESP_LOGW(TAG, "Ignoring pairing code of length %u (expected 8)", (unsigned) payload.size());
      return;
    }
    std::copy(payload.begin(), payload.end(), this->pair_payload_.begin());
    this->have_pair_payload_ = true;
    this->do_round2_ = false;
    this->step_ = Step::IDLE;
    ESP_LOGI(TAG, "Pairing code received - will run first-time pairing");
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override {
    switch (event) {
      case ESP_GATTC_SEARCH_CMPL_EVT:
        this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
        this->conn_id_ = this->parent()->get_conn_id();
        this->gattc_if_ = this->parent()->get_gattc_if();
        this->session_active_ = true;
        this->session_id_++;
        if (this->do_round2_ && this->have_grill_half_) {
          ESP_LOGI(TAG, "Connected; resuming session");
          const uint32_t session = this->session_id_;
          this->parent()->run_later([this, session]() {
            if (this->session_active_ && session == this->session_id_)
              this->write_round2_();
          });
        } else if (this->have_pair_payload_) {
          ESP_LOGI(TAG, "Connected; running first-time pairing");
          const uint32_t session = this->session_id_;
          this->parent()->run_later([this, session]() {
            if (this->session_active_ && session == this->session_id_)
              this->write_pair_payload_();
          });
        } else {
          ESP_LOGW(TAG, "Connected but not paired and no pairing code - disconnecting");
          this->parent()->disconnect();
        }
        break;

      case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "Write to 0x%04X failed (status %d)", param->write.handle, param->write.status);
          this->fail_and_disconnect_("GATT write failed");
          break;
        }
        if (param->write.handle == FFF1_HANDLE) {
          if (this->step_ == Step::PAIR_SENT) {
            this->read_fff2_(Step::WAIT_CHALLENGE);
          } else if (this->step_ == Step::ECHO_SENT) {
            this->read_fff2_(Step::WAIT_ACK);
          } else if (this->step_ == Step::START_SENT) {
            ESP_LOGD(TAG, "Start sent; expecting the smoker to drop us, then we reconnect");
          } else if (this->step_ == Step::R2_SENT) {
            this->read_fff2_(Step::WAIT_R2_CHALLENGE);
          }
        } else if (param->write.handle == FFF4_CCCD_HANDLE) {
          if (this->step_ == Step::R2_CCCD_SENT) {
            ESP_LOGI(TAG, "Streaming telemetry");
          } else {
            this->write_start_();
          }
        }
        break;

      case ESP_GATTC_READ_CHAR_EVT:
        if (param->read.handle == FFF2_HANDLE)
          this->handle_fff2_read_(param->read.value, param->read.value_len, param->read.status);
        break;

      case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (param->reg_for_notify.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "Register notify failed (status %d)", param->reg_for_notify.status);
          this->fail_and_disconnect_("notify registration failed");
          break;
        }
        if (param->reg_for_notify.handle == FFF4_HANDLE) {
          if (this->step_ == Step::R2_NOTIFY)
            this->write_cccd_(Step::R2_CCCD_SENT);
          else
            this->write_cccd_(Step::CCCD_SENT);
        }
        break;

      case ESP_GATTC_NOTIFY_EVT:
        if (param->notify.handle == FFF4_HANDLE)
          this->parse_telemetry_(param->notify.value, param->notify.value_len);
        break;

      case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Disconnected");
        this->reset_session_();  // grill_half_ / pairing state are kept
        this->set_timeout("connect_gate", 1200, [this]() { this->connect_started_ = false; });
        break;

      default:
        break;
    }
  }

 protected:
  static constexpr const char *TAG = "masterbuilt_smoker";
  static constexpr uint32_t GRILL_HALF_PREF = 0x4D425348;  // "MBSH"
  static constexpr uint32_t IDENTITY_MAGIC = 0x4D425349;    // "MBSI"
  static constexpr uint8_t IDENTITY_VERSION = 1;
  static constexpr uint16_t COMPANY_ID = 0x4842;
  static constexpr uint16_t FFF1_HANDLE = 0x0025;
  static constexpr uint16_t FFF2_HANDLE = 0x0027;
  static constexpr uint16_t FFF4_HANDLE = 0x002b;
  static constexpr uint16_t FFF4_CCCD_HANDLE = 0x002c;

  // Constant key the app XORs the pairing code / grill-half against (company id 0x4842 little
  // endian + a fixed placeholder). Identical for every Masterbuilt/Bough Tech unit.
  static constexpr std::array<uint8_t, 8> MAC_KEY = {0x42, 0x48, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

  enum class Step {
    IDLE,
    PAIR_SENT,
    WAIT_CHALLENGE,
    ECHO_SENT,
    WAIT_ACK,
    NOTIFY,
    CCCD_SENT,
    START_SENT,
    R2_SENT,
    WAIT_R2_CHALLENGE,
    R2_NOTIFY,
    R2_CCCD_SENT,
  };

  Step step_{Step::IDLE};
  esp_gatt_if_t gattc_if_{0};
  uint16_t conn_id_{0};
  bool session_active_{false};
  bool have_pair_payload_{false};
  bool have_grill_half_{false};
  bool do_round2_{false};
  bool connect_started_{false};
  uint8_t fff2_retries_{0};
  uint32_t session_id_{0};
  uint64_t configured_address_{0};
  uint64_t locked_address_{0};
  std::array<uint8_t, 8> pair_payload_{};
  std::array<uint8_t, 8> grill_half_{};
  ESPPreferenceObject pref_;

  sensor::Sensor *chamber_temp_{nullptr};
  sensor::Sensor *target_temp_{nullptr};
  sensor::Sensor *cook_time_{nullptr};
  sensor::Sensor *time_remaining_{nullptr};
  sensor::Sensor *probes_[4]{nullptr, nullptr, nullptr, nullptr};
  binary_sensor::BinarySensor *door_sensor_{nullptr};
  binary_sensor::BinarySensor *temp_error_sensor_{nullptr};
  bool door_open_{false};

  void lock_address_(uint64_t address, esp_ble_addr_type_t address_type, int rssi, bool configured) {
    this->locked_address_ = address;
    this->parent()->set_address(address);
    this->parent()->set_remote_addr_type(address_type);
    if (configured) {
      ESP_LOGI(TAG, "Pinned smoker address %s", this->parent()->address_str());
    } else {
      ESP_LOGI(TAG, "Discovered smoker %s (RSSI %d)", this->parent()->address_str(), rssi);
    }
  }

  void write_keyed_(const std::array<uint8_t, 8> &secret, Step next) {
    uint8_t out[10] = {0x0e, 0x08};
    for (int i = 0; i < 8; i++)
      out[2 + i] = MAC_KEY[i] ^ secret[i];
    this->step_ = next;
    this->log_bytes_(ESP_LOG_DEBUG, "Write fff1", out, sizeof(out));
    if (!this->write_char_(FFF1_HANDLE, out, sizeof(out)))
      this->fail_and_disconnect_("failed to write fff1");
  }

  void write_pair_payload_() {
    if (!this->have_pair_payload_) {
      this->parent()->disconnect();
      return;
    }
    // pair_payload is already XOR(MAC_KEY, pair_code); send it verbatim with the 0e08 header.
    uint8_t out[10] = {0x0e, 0x08};
    memcpy(out + 2, this->pair_payload_.data(), 8);
    this->step_ = Step::PAIR_SENT;
    if (!this->write_char_(FFF1_HANDLE, out, sizeof(out)))
      this->fail_and_disconnect_("failed to write pairing payload");
  }

  void write_echo_() {
    uint8_t out[10] = {0x0e, 0x08};
    memcpy(out + 2, this->grill_half_.data(), 8);
    this->step_ = Step::ECHO_SENT;
    if (!this->write_char_(FFF1_HANDLE, out, sizeof(out)))
      this->fail_and_disconnect_("failed to echo smoker identity");
  }

  void write_round2_() { this->write_keyed_(this->grill_half_, Step::R2_SENT); }

  void write_start_() {
    static uint8_t start[] = {0x0f, 0x00, 0x00};
    this->step_ = Step::START_SENT;
    this->do_round2_ = true;
    if (!this->write_char_(FFF1_HANDLE, start, sizeof(start)))
      this->fail_and_disconnect_("failed to write start command");
  }

  void write_cccd_(Step next) {
    static uint8_t enable[] = {0x01, 0x00};
    this->step_ = next;
    if (!this->write_char_(FFF4_CCCD_HANDLE, enable, sizeof(enable)))
      this->fail_and_disconnect_("failed to enable notifications");
  }

  void register_notify_(Step next) {
    this->step_ = next;
    esp_err_t err = esp_ble_gattc_register_for_notify(this->gattc_if_, this->parent()->get_remote_bda(), FFF4_HANDLE);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "register_for_notify failed immediately (%d)", err);
      this->fail_and_disconnect_("failed to register notifications");
    }
  }

  void read_fff2_(Step next) {
    if (!this->session_active_)
      return;
    this->step_ = next;
    esp_err_t err = esp_ble_gattc_read_char(this->gattc_if_, this->conn_id_, FFF2_HANDLE, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "read fff2 failed immediately (%d)", err);
      this->fail_and_disconnect_("failed to read fff2");
    }
  }

  bool write_char_(uint16_t handle, uint8_t *data, uint16_t len) {
    esp_err_t err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, handle, len, data, ESP_GATT_WRITE_TYPE_RSP,
                                            ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK)
      ESP_LOGW(TAG, "write 0x%04X failed immediately (%d)", handle, err);
    return err == ESP_OK;
  }

  void store_grill_half_(const uint8_t *value) {
    memcpy(this->grill_half_.data(), value, 8);
    this->have_grill_half_ = true;
    StoredIdentity identity{};
    identity.magic = IDENTITY_MAGIC;
    identity.version = IDENTITY_VERSION;
    identity.smoker_address = this->parent()->get_address();
    identity.grill_half = this->grill_half_;
    identity.checksum = this->identity_checksum_(identity);
    if (this->pref_.save(&identity)) {
      global_preferences->sync();
      this->log_bytes_(ESP_LOG_INFO, "Learned + saved smoker identity", this->grill_half_.data(), 8);
    } else {
      ESP_LOGW(TAG, "Failed to save smoker identity; reconnect will require pairing after reboot");
    }
  }

  void handle_fff2_read_(uint8_t *value, uint16_t len, esp_gatt_status_t status) {
    this->log_bytes_(ESP_LOG_DEBUG, "Read fff2", value, len);
    if (status != ESP_GATT_OK) {
      this->fail_and_disconnect_("GATT read failed");
      return;
    }

    if (this->step_ == Step::WAIT_CHALLENGE) {
      if (len >= 10 && value[0] == 0x0a && value[1] == 0x08) {
        this->fff2_retries_ = 0;
        this->store_grill_half_(value + 2);
        this->write_echo_();
      } else if (!this->retry_fff2_(Step::WAIT_CHALLENGE)) {
        ESP_LOGW(TAG, "Pairing rejected (no challenge) - re-enter pairing mode");
        this->have_pair_payload_ = false;
        this->fail_and_disconnect_("pairing challenge not received");
      }
    } else if (this->step_ == Step::WAIT_ACK) {
      if (len >= 1 && value[0] == 0x0b) {
        this->fff2_retries_ = 0;
        this->register_notify_(Step::NOTIFY);
      } else if (!this->retry_fff2_(Step::WAIT_ACK)) {
        ESP_LOGW(TAG, "Pairing echo not acknowledged");
        this->have_pair_payload_ = false;
        this->fail_and_disconnect_("pairing echo not acknowledged");
      }
    } else if (this->step_ == Step::WAIT_R2_CHALLENGE) {
      if (len >= 2 && value[0] == 0x0a && value[1] == 0x08) {
        this->fff2_retries_ = 0;
        this->register_notify_(Step::R2_NOTIFY);
      } else if (!this->retry_fff2_(Step::WAIT_R2_CHALLENGE)) {
        ESP_LOGW(TAG, "Reconnect challenge not received");
        this->fail_and_disconnect_("reconnect challenge not received");
      }
    }
  }

  // The smoker fills in the fff2 response asynchronously after our write, so the first read can
  // come back as all zeros. Re-read a handful of times before giving up.
  bool retry_fff2_(Step step) {
    if (this->fff2_retries_ >= 12) {
      this->fff2_retries_ = 0;
      return false;
    }
    this->fff2_retries_++;
    const uint32_t session = this->session_id_;
    this->set_timeout("fff2_retry", 250, [this, step, session]() {
      if (this->session_active_ && session == this->session_id_ && this->step_ == step)
        this->read_fff2_(step);
    });
    return true;
  }

  // SFLOAT mantissa (low 12 bits) holds the temperature; exponent is 0 for normal ranges.
  static uint16_t sfloat_(const uint8_t *v, uint16_t len, uint16_t i) {
    if (i + 1 >= len)
      return 0;
    return (uint16_t) ((v[i] | (v[i + 1] << 8)) & 0x0FFF);
  }

  void parse_telemetry_(uint8_t *v, uint16_t len) {
    if (len >= 15 && v[0] == 0xb2) {
      uint16_t chamber = sfloat_(v, len, 4);
      uint16_t remain = sfloat_(v, len, 8);
      uint16_t set_min = sfloat_(v, len, 11);
      uint16_t set_temp = sfloat_(v, len, 13);
      ESP_LOGD(TAG, "chamber=%u target=%u cook=%u remaining=%u", chamber, set_temp, set_min, remain);
      if (this->chamber_temp_ != nullptr)
        this->chamber_temp_->publish_state(chamber);
      if (this->target_temp_ != nullptr)
        this->target_temp_->publish_state(set_temp);
      if (this->cook_time_ != nullptr)
        this->cook_time_->publish_state(set_min);
      if (this->time_remaining_ != nullptr)
        this->time_remaining_->publish_state(remain);
      // Leading status flags: v[2] control byte (bit3 = door open), v[3] caps/errors (bit0 = temp error).
      this->door_open_ = ((v[2] >> 3) & 0x01) != 0;
      if (this->door_sensor_ != nullptr)
        this->door_sensor_->publish_state(this->door_open_);
      if (this->temp_error_sensor_ != nullptr)
        this->temp_error_sensor_->publish_state(((v[3] >> 0) & 0x01) != 0);
    } else if (len >= 2 && v[0] == 0xb3) {
      uint8_t flags = v[1];
      for (int i = 0; i < 4; i++) {
        if (this->probes_[i] == nullptr)
          continue;
        if (flags & (1 << i)) {
          if (len < 2 + (i + 1) * 2) {
            ESP_LOGW(TAG, "Ignoring truncated probe frame len=%u", len);
            continue;
          }
          this->probes_[i]->publish_state(sfloat_(v, len, 2 + i * 2));
        } else {
          this->probes_[i]->publish_state(NAN);
        }
      }
    }
  }

  void reset_session_() {
    this->cancel_timeout("fff2_retry");
    this->fff2_retries_ = 0;
    this->session_active_ = false;
    this->session_id_++;
    this->step_ = Step::IDLE;
    this->conn_id_ = 0;
    this->gattc_if_ = 0;
  }

  void fail_and_disconnect_(const char *reason) {
    ESP_LOGW(TAG, "%s; disconnecting", reason);
    this->reset_session_();
    this->set_timeout("connect_gate", 1200, [this]() { this->connect_started_ = false; });
    this->parent()->disconnect();
  }

  static bool invalid_identity_bytes_(const std::array<uint8_t, 8> &value) {
    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t byte : value) {
      all_zero &= byte == 0x00;
      all_ff &= byte == 0xff;
    }
    return all_zero || all_ff;
  }

  uint32_t identity_checksum_(const StoredIdentity &identity) const {
    uint32_t h = 2166136261UL;
    auto mix = [&h](uint8_t byte) {
      h ^= byte;
      h *= 16777619UL;
    };
    for (int i = 0; i < 4; i++)
      mix((IDENTITY_MAGIC >> (i * 8)) & 0xff);
    mix(identity.version);
    for (int i = 0; i < 8; i++)
      mix((identity.smoker_address >> (i * 8)) & 0xff);
    for (uint8_t byte : identity.grill_half)
      mix(byte);
    return h;
  }

  bool validate_identity_record_(const StoredIdentity &identity) {
    if (identity.magic != IDENTITY_MAGIC || identity.version != IDENTITY_VERSION)
      return false;
    if (identity.smoker_address == 0)
      return false;
    if (invalid_identity_bytes_(identity.grill_half))
      return false;
    return identity.checksum == this->identity_checksum_(identity);
  }

  bool validate_identity_(const StoredIdentity &identity) {
    return this->validate_identity_record_(identity) && identity.smoker_address == this->parent()->get_address();
  }

  void log_bytes_(int level, const char *label, const uint8_t *value, uint16_t len) {
    char buf[160];
    size_t pos = 0;
    for (uint16_t i = 0; i < len && pos + 4 < sizeof(buf); i++)
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X%s", value[i], (i + 1 < len) ? " " : "");
    buf[pos] = 0;
    if (level == ESP_LOG_INFO)
      ESP_LOGI(TAG, "%s: %s", label, buf);
    else
      ESP_LOGD(TAG, "%s: %s", label, buf);
  }
};

inline void ForgetPairingButton::press_action() {
  if (this->parent_ != nullptr)
    this->parent_->forget_pairing();
}

}  // namespace masterbuilt_smoker
}  // namespace esphome
