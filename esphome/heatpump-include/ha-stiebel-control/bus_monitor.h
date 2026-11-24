/*
 * Passive CAN bus monitor for the Stiebel Eltron WPF 7 (ressl fork 2.4.1)
 *
 * Sends NOTHING. Runs only while the switch "CAN-Busmonitor (nur lesen)" is on and
 * switches itself off after BUSMON_MAX_MS (2 h).
 *
 * Every received frame is decoded (Elster header: type nibble + receiver) BEFORE the
 * normal processing drops it (processAndUpdate() ignores frames shorter than 7 bytes,
 * read requests of other members, system messages and every (member, index) pair we
 * do not poll). Frames that are only the answer to one of our own reads (type 2 =
 * response, receiver 0x680 = this ESP) are skipped; everything else is counted per
 * (sender, type, receiver, index).
 *
 * Output:
 *   - the first BUSMON_LOG_FIRST frames of every kind: log tag BUSMON
 *     (-> MQTT heatingpump/debug), raw bytes included
 *   - every kind: retained MQTT heatingpump/busmon/<from>/<type>/<to>/<index> (JSON),
 *     published when first seen and refreshed every 10 min and at the end
 *   - progress: text sensor "CAN-Busmonitor Status" + MQTT heatingpump/busmon/status
 *
 * Purpose (2026-11-16): find out whether and how the WPM i looks for a remote control
 * (FEK). Known from real FEK captures (robot300795, chriss1980): system messages type
 * 6/7 with index 0x00fd BUSKONFIGURATION / 0x00fe INITIALISIERUNG, broadcasts of
 * date/time/temperatures to all operating units (receiver 0x379), display symbols
 * from 0x601 to the FEK at 0x301, and an FEK heartbeat (read of 0x000b) about every
 * 60 s.
 */

#pragma once

#include <map>
#include <vector>
#include <cstring>
#include <cstdint>

// ---- BUSMON pure logic begin (host-tested in tests/test_bus_monitor.cpp) ----
static constexpr uint32_t BUSMON_MAX_MS = 2UL * 60UL * 60UL * 1000UL;   // 2 h
static constexpr uint32_t BUSMON_REFRESH_MS = 10UL * 60UL * 1000UL;     // 10 min
static constexpr size_t BUSMON_MAX_KEYS = 300;                          // RAM bound
static constexpr uint32_t BUSMON_LOG_FIRST = 3;
static constexpr uint16_t BUSMON_OWN_ID = 0x680;                        // this ESP (PC)

struct BusMonFrame {
    uint16_t sender = 0;
    uint8_t type = 0xFF;        // Elster type nibble, 0xFF = frame too short
    uint16_t receiver = 0;
    uint16_t index = 0;
    bool hasIndex = false;
    bool hasValue = false;
    uint16_t value = 0;
};

struct BusMonStat {
    uint32_t count = 0;
    uint32_t firstMs = 0;
    uint32_t lastMs = 0;
    uint32_t publishedCount = 0;  // count at the last MQTT publish
    uint8_t dlc = 0;
    uint8_t last[8] = {0};
};

static const char *const BUSMON_TYPE_NAMES[8] = {"write", "read", "resp", "ack", "wack", "wresp", "SYS", "SYSRESP"};

static const char *busMonTypeName(uint8_t type)
{
    return type < 8 ? BUSMON_TYPE_NAMES[type] : "short";
}

// Elster header: data[0] = (receiver >> 7) << 4 | type, data[1] = receiver & 0x7f.
// Index: data[2], or data[3..4] when data[2] == 0xfa (extended format).
// Value: data[3..4] (short format) or data[5..6] (extended format).
static BusMonFrame busMonDecode(uint32_t canId, const uint8_t *d, size_t n)
{
    BusMonFrame f;
    f.sender = (uint16_t) (canId & 0x7FF);
    if (n >= 2) {
        f.type = d[0] & 0x0F;
        f.receiver = (uint16_t) (((d[0] & 0xF0) << 3) | (d[1] & 0x7F));
    }
    if (n >= 5 && d[2] == 0xFA) {
        f.index = (uint16_t) ((d[3] << 8) | d[4]);
        f.hasIndex = true;
        if (n >= 7) {
            f.value = (uint16_t) ((d[5] << 8) | d[6]);
            f.hasValue = true;
        }
    } else if (n >= 3) {
        f.index = d[2];
        f.hasIndex = true;
        if (n >= 5) {
            f.value = (uint16_t) ((d[3] << 8) | d[4]);
            f.hasValue = true;
        }
    }
    return f;
}

// Answers to our own read requests are the normal polling traffic: not interesting.
static bool busMonIsOwnTraffic(const BusMonFrame &f)
{
    return f.type == 2 && f.receiver == BUSMON_OWN_ID;
}

static uint64_t busMonKey(const BusMonFrame &f)
{
    return ((uint64_t) f.sender << 40) | ((uint64_t) f.type << 32) | ((uint64_t) f.receiver << 16) | f.index;
}

// Counts one frame. Returns the stat (nullptr if the key table is full) and sets
// shouldLog for the first BUSMON_LOG_FIRST frames of a kind and isNew for the first one.
static BusMonStat *busMonAccount(std::map<uint64_t, BusMonStat> &stats, const BusMonFrame &f,
                                 const uint8_t *d, size_t n, uint32_t nowMs,
                                 bool &shouldLog, bool &isNew, uint32_t &dropped)
{
    shouldLog = false;
    isNew = false;
    const uint64_t key = busMonKey(f);
    auto it = stats.find(key);
    if (it == stats.end()) {
        if (stats.size() >= BUSMON_MAX_KEYS) {
            dropped++;
            return nullptr;
        }
        BusMonStat s;
        s.firstMs = nowMs;
        it = stats.emplace(key, s).first;
        isNew = true;
    }
    BusMonStat &s = it->second;
    s.count++;
    s.lastMs = nowMs;
    s.dlc = (uint8_t) (n > 8 ? 8 : n);
    memset(s.last, 0, sizeof(s.last));
    memcpy(s.last, d, s.dlc);
    shouldLog = s.count <= BUSMON_LOG_FIRST;
    return &s;
}
// "c0 01 11 00 e4 00 00" (up to 8 bytes, needs a buffer of 25 chars)
static void busMonHex(const uint8_t *d, uint8_t n, char *out, size_t len)
{
    size_t p = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < n && i < 8 && p + 4 <= len; i++) {
        int w = snprintf(out + p, len - p, i ? " %02x" : "%02x", d[i]);
        if (w < 0) break;
        p += (size_t) w;
    }
}

// ---- BUSMON pure logic end ----

#if !defined(HA_DUMMY_BUILD)

static bool busMonActive = false;
static uint32_t busMonStartMs = 0;
static uint32_t busMonLastRefreshMs = 0;
static uint32_t busMonLastStatusMs = 0;
static uint32_t busMonFrames = 0, busMonOwn = 0, busMonDropped = 0;
static bool busMonReadyPublished = false;
static std::map<uint64_t, BusMonStat> busMonStats;

static void busMonTopic(uint64_t key, char *buf, size_t len)
{
    snprintf(buf, len, "heatingpump/busmon/%03x/%s/%03x/%04x",
             (unsigned) ((key >> 40) & 0x7FF), busMonTypeName((uint8_t) ((key >> 32) & 0xFF)),
             (unsigned) ((key >> 16) & 0xFFFF), (unsigned) (key & 0xFFFF));
}

static void busMonPublishKey(uint64_t key, BusMonStat &s)
{
    char topic[72];
    busMonTopic(key, topic, sizeof(topic));
    const uint16_t index = (uint16_t) (key & 0xFFFF);
    const ElsterIndex *ei = GetElsterIndex((unsigned short) index);
    char hex[3 * 8 + 1];
    busMonHex(s.last, s.dlc, hex, sizeof(hex));
    char json[320];
    snprintf(json, sizeof(json),
             "{\"from\":\"%03x\",\"type\":\"%s\",\"to\":\"%03x\",\"index\":\"%04x\",\"name\":\"%s\","
             "\"count\":%u,\"first_s\":%u,\"last_s\":%u,\"dlc\":%u,\"last\":\"%s\"}",
             (unsigned) ((key >> 40) & 0x7FF), busMonTypeName((uint8_t) ((key >> 32) & 0xFF)),
             (unsigned) ((key >> 16) & 0xFFFF), (unsigned) index, ei ? ei->Name : "?",
             (unsigned) s.count, (unsigned) ((s.firstMs - busMonStartMs) / 1000UL),
             (unsigned) ((s.lastMs - busMonStartMs) / 1000UL), (unsigned) s.dlc, hex);
    id(mqtt_client).publish(topic, json, strlen(json), 0, true);
    s.publishedCount = s.count;
}

static void busMonPublishStatus(const char *state)
{
    const uint32_t mins = busMonStartMs ? (millis() - busMonStartMs) / 60000UL : 0;
    char text[128];
    snprintf(text, sizeof(text), "%s: %u min, %u Frames, %u Arten, %u eigene Antworten ignoriert",
             state, (unsigned) mins, (unsigned) busMonFrames, (unsigned) busMonStats.size(), (unsigned) busMonOwn);
    id(bus_monitor_status).publish_state(text);
    char json[200];
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"minutes\":%u,\"frames\":%u,\"kinds\":%u,\"own_skipped\":%u,\"dropped\":%u}",
             state, (unsigned) mins, (unsigned) busMonFrames, (unsigned) busMonStats.size(),
             (unsigned) busMonOwn, (unsigned) busMonDropped);
    id(mqtt_client).publish("heatingpump/busmon/status", json, strlen(json), 0, true);
    busMonLastStatusMs = millis();
}

static void busMonRefresh()
{
    for (auto &kv : busMonStats) {
        if (kv.second.count != kv.second.publishedCount) busMonPublishKey(kv.first, kv.second);
    }
    busMonLastRefreshMs = millis();
}

void busMonStart()
{
    if (busMonActive) return;
    // clear the retained topics of an earlier run in this boot
    for (auto &kv : busMonStats) {
        char topic[72];
        busMonTopic(kv.first, topic, sizeof(topic));
        id(mqtt_client).publish(topic, "", 0, 0, true);
    }
    busMonStats.clear();
    busMonFrames = busMonOwn = busMonDropped = 0;
    busMonStartMs = millis();
    busMonLastRefreshMs = busMonStartMs;
    busMonActive = true;
    ESP_LOGI("BUSMON", "Passive CAN bus monitor started (sends nothing, stops after %u min)",
             (unsigned) (BUSMON_MAX_MS / 60000UL));
    busMonPublishStatus("laeuft");
}

void busMonStop(const char *reason)
{
    if (!busMonActive) return;
    busMonActive = false;
    busMonRefresh();
    ESP_LOGI("BUSMON", "Monitor stopped (%s): %u frames, %u kinds, %u own answers skipped, %u dropped",
             reason, (unsigned) busMonFrames, (unsigned) busMonStats.size(), (unsigned) busMonOwn,
             (unsigned) busMonDropped);
    busMonPublishStatus(reason);
}

// Called for EVERY received frame, first thing in processAndUpdate().
void busMonOnFrame(uint32_t can_id, const std::vector<uint8_t> &msg)
{
    if (!busMonActive) return;
    const BusMonFrame f = busMonDecode(can_id, msg.data(), msg.size());
    if (busMonIsOwnTraffic(f)) {
        busMonOwn++;
        return;
    }
    busMonFrames++;
    bool shouldLog = false, isNew = false;
    BusMonStat *s = busMonAccount(busMonStats, f, msg.data(), msg.size(), millis(), shouldLog, isNew, busMonDropped);
    if (!s) return;
    if (shouldLog) {
        const ElsterIndex *ei = f.hasIndex ? GetElsterIndex((unsigned short) f.index) : nullptr;
        char hex[3 * 8 + 1];
        busMonHex(s->last, s->dlc, hex, sizeof(hex));
        ESP_LOGI("BUSMON", "%03x -> %03x %-7s idx %04x %-24s val %04x | %u: %s (#%u)",
                 (unsigned) f.sender, (unsigned) f.receiver, busMonTypeName(f.type), (unsigned) f.index,
                 ei ? ei->Name : "-", (unsigned) f.value, (unsigned) s->dlc, hex, (unsigned) s->count);
    }
    if (isNew) busMonPublishKey(busMonKey(f), *s);
}

// Called every 5 s.
void busMonTick()
{
    if (!busMonReadyPublished) {
        id(bus_monitor_status).publish_state("bereit (aus)");
        busMonReadyPublished = true;
    }
    if (!busMonActive) return;
    const uint32_t now = millis();
    if (now - busMonStartMs >= BUSMON_MAX_MS) {
        busMonStop("fertig (2 h)");
        id(bus_monitor_switch).turn_off();   // its on_turn_off finds the monitor inactive
        return;
    }
    if (now - busMonLastRefreshMs >= BUSMON_REFRESH_MS) busMonRefresh();
    if (now - busMonLastStatusMs >= 60000UL) busMonPublishStatus("laeuft");
}

#endif  // !HA_DUMMY_BUILD
