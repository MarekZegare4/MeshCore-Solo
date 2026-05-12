#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
//    Serial.print("DBG: Setting up buzzer on pin ");
//    Serial.println(PIN_BUZZER);
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
    startup();
}

void genericBuzzer::play(const char *melody) {
    if (isPlaying()) rtttl::stop();
    if (_is_quiet) return;
    rtttl::begin(PIN_BUZZER, melody);
//    Serial.print("DBG: Playing melody - isQuiet: ");
//    Serial.println(isQuiet());
}

void genericBuzzer::playForced(const char *melody) {
    if (isPlaying()) rtttl::stop();
    rtttl::begin(PIN_BUZZER, melody);
}

bool genericBuzzer::isPlaying() {
    return rtttl::isPlaying();
}

void genericBuzzer::applyVolume() {
    // After tone() sets 50% duty, analogWrite overrides duty cycle on the same PWM channel.
    // Level 4 = 50% (leave tone as-is), lower levels reduce duty = quieter output.
    static const uint8_t duty[] = { 6, 20, 50, 90, 128 };
    uint8_t d = duty[_volume_level < 5 ? _volume_level : 4];
    if (d < 128) analogWrite(PIN_BUZZER, d);
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
    if (isPlaying()) applyVolume();
}

void genericBuzzer::loop() {
    if (!rtttl::done()) {
        rtttl::play();
        if (_volume_level < 4) applyVolume();
    }
}

void genericBuzzer::startup() {
    play(startup_song);
}

void genericBuzzer::shutdown() {
    play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
#ifdef PIN_BUZZER_EN
    if (_is_quiet) {
      digitalWrite(PIN_BUZZER_EN, LOW);
    } else {
      digitalWrite(PIN_BUZZER_EN, HIGH);
    }
#endif
}

bool genericBuzzer::isQuiet() {
    return _is_quiet;
}

#endif  // ifdef PIN_BUZZER