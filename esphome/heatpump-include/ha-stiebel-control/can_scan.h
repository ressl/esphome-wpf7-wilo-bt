/*
 * Read-only CAN scan for the Stiebel Eltron WPF 7 (ressl fork 2.3.0)
 *
 * Started only by the button "CAN-Scan starten (nur lesen)".
 *
 * Phase 1: every known CAN member (except PC 0x680 = this ESP) gets a few read
 *          requests. Members that answer at all are "alive".
 * Phase 2: every index of ElsterTable is read once from every alive member.
 *
 * Only Elster READ requests (type nibble 1) are sent; canScanSendRead() refuses
 * any other frame type. Nothing is written to the heat pump.
 *
 * Results:
 *   - hits (answers with a value other than 0x8000 = "not available") are
 *     published retained to MQTT  heatingpump/scan/<canid>/<index>  and logged
 *     with tag CAN_SCAN, and kept in RAM for "CAN-Scan Ergebnisse erneut senden"
 *   - progress: text sensor "CAN-Scan Status" + MQTT heatingpump/scan/status
 *
 * Load: one scan request every CAN_SCAN_PERIOD_MS (750 ms) on top of the regular
 * polling; the scan stops by itself on TWAI errors (TEC > CAN_SCAN_MAX_TEC).
 */

#pragma once

#include <vector>
#include <set>
#include <cstring>

#if !defined(HA_DUMMY_BUILD)

struct CanScanPending {
    uint32_t canId;
    uint16_t index;
    uint32_t sentMs;
};

struct CanScanHit {
    uint16_t canId;
    uint16_t index;
    uint16_t raw;
};

static bool canScanActive = false;
static int canScanPhase = 0;                 // 1 = member discovery, 2 = index sweep
static size_t canScanMemberPos = 0;          // phase 1: CanMembers[], phase 2: alive list
static size_t canScanProbePos = 0;
static size_t canScanTablePos = 0;
static uint32_t canScanLastSendMs = 0;
static uint32_t canScanStartMs = 0;
static uint32_t canScanLastStatusMs = 0;
static uint32_t canScanSent = 0, canScanAnswers = 0, canScanHits = 0, canScanNa = 0;
static std::set<uint32_t> canScanAlive;
static std::vector<uint32_t> canScanAliveList;
static std::vector<CanScanHit> canScanHitList;
static CanScanPending canScanRing[8];
static size_t canScanRingPos = 0;
static uint8_t canScanSeen[8192];            // 1 bit per index, reset per member
static bool canScanReadyPublished = false;

// Probe indices for phase 1: GERAETE_ID, SOFTWARE_NUMMER, AUSSENTEMP
static const uint16_t CAN_SCAN_PROBE_INDEX[CAN_SCAN_PROBES_PER_MEMBER] = {0x000b, 0x0199, 0x000c};

static const size_t CAN_SCAN_MEMBER_COUNT = sizeof(CanMembers) / sizeof(CanMember);

static bool canScanSkipMember(uint32_t canId)
{
    return canId == CanMembers[cm_pc].CanId || canId == 0;
}

static uint32_t canScanTotalPlanned()
{
    // phase 2 size estimate (unique indices are counted while sweeping)
    return (uint32_t) canScanAliveList.size() * (uint32_t) High(ElsterTable);
}

static void canScanPublishStatus(const char *state)
{
    char text[128];
    uint32_t mins = (millis() - canScanStartMs) / 60000UL;
    if (canScanPhase == 1) {
        snprintf(text, sizeof(text), "%s: Phase 1 Teilnehmer %u/%u, %u antworten",
                 state, (unsigned) canScanMemberPos, (unsigned) CAN_SCAN_MEMBER_COUNT, (unsigned) canScanAlive.size());
    } else {
        snprintf(text, sizeof(text), "%s: %u Anfragen, %u Treffer, %u n.v., %u Geraete, %u min",
                 state, (unsigned) canScanSent, (unsigned) canScanHits, (unsigned) canScanNa,
                 (unsigned) canScanAliveList.size(), (unsigned) mins);
    }
    id(can_scan_status).publish_state(text);

    char json[320];
    std::string alive;
    for (uint32_t c : canScanAlive) {
        char b[8];
        snprintf(b, sizeof(b), "%s\"%03x\"", alive.empty() ? "" : ",", (unsigned) c);
        alive += b;
    }
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"phase\":%d,\"sent\":%u,\"answers\":%u,\"hits\":%u,\"na\":%u,"
             "\"alive\":[%s],\"planned\":%u,\"minutes\":%u}",
             state, canScanPhase, (unsigned) canScanSent, (unsigned) canScanAnswers, (unsigned) canScanHits,
             (unsigned) canScanNa, alive.c_str(), (unsigned) canScanTotalPlanned(), (unsigned) mins);
    id(mqtt_client).publish("heatingpump/scan/status", json, strlen(json), 0, true);
    canScanLastStatusMs = millis();
}

// Builds and sends one Elster READ request. Returns false if the TX queue is full.
static bool canScanSendRead(uint32_t canId, uint16_t index)
{
    CanIdBytes r = generate_read_id(canId);
    std::vector<uint8_t> data;
    if (index > 0xff || index == 0xfa) {
        data = {r.first, r.second, 0xfa, (uint8_t)(index >> 8), (uint8_t)(index & 0xff), 0x00, 0x00};
    } else {
        data = {r.first, r.second, (uint8_t) index, 0x00, 0x00, 0x00, 0x00};
    }
    // Safety net: never send anything but a READ request from the scan
    if ((data[0] & 0x0f) != 0x01) {
        ESP_LOGE("CAN_SCAN", "Refusing non-read frame for 0x%03x/0x%04x", (unsigned) canId, (unsigned) index);
        return false;
    }
    esphome::canbus::Error err = id(my_can).send_data(CanMembers[cm_pc].CanId, false, data);
    canScanLastSendMs = millis();
    if (err != esphome::canbus::ERROR_OK) {
        ESP_LOGW("CAN_SCAN", "send failed (error %d) for 0x%03x/0x%04x, retrying", (int) err,
                 (unsigned) canId, (unsigned) index);
        return false;
    }
    canScanRing[canScanRingPos % 8] = {canId, index, canScanLastSendMs};
    canScanRingPos++;
    canScanSent++;
    return true;
}

static void canScanPublishHit(const CanScanHit &h)
{
    const ElsterIndex *ei = GetElsterIndex(h.index);
    char val[32];
    if (ei->Type == et_double_val || ei->Type == et_triple_val) {
        SetDoubleType(val, ei->Type, static_cast<double>(h.raw));
    } else {
        SetValueType(val, ei->Type, h.raw);
    }
    for (char *p = val; *p; p++) {
        if (*p == '"' || *p == '\\') *p = '\'';
    }
    const CanMember &cm = lookupCanMember(h.canId);
    char topic[48];
    snprintf(topic, sizeof(topic), "heatingpump/scan/%03x/%04x", (unsigned) h.canId, (unsigned) h.index);
    char json[224];
    snprintf(json, sizeof(json),
             "{\"member\":\"%s\",\"can_id\":\"0x%03x\",\"index\":\"0x%04x\",\"name\":\"%s\",\"type\":\"%s\","
             "\"raw\":\"0x%04x\",\"value\":\"%s\"}",
             cm.Name, (unsigned) h.canId, (unsigned) h.index, ei->Name, ElsterTypeStr[ei->Type],
             (unsigned) h.raw, val);
    id(mqtt_client).publish(topic, json, strlen(json), 0, true);
    ESP_LOGI("CAN_SCAN", "HIT %s (0x%03x) 0x%04x %s = %s (raw 0x%04x)", cm.Name, (unsigned) h.canId,
             (unsigned) h.index, ei->Name, val, (unsigned) h.raw);
}

void canScanStop(const char *reason)
{
    if (!canScanActive) return;
    canScanActive = false;
    ESP_LOGI("CAN_SCAN", "Scan finished (%s): %u requests, %u answers, %u hits, %u not available",
             reason, (unsigned) canScanSent, (unsigned) canScanAnswers, (unsigned) canScanHits, (unsigned) canScanNa);
    canScanPublishStatus(reason);
}

void canScanStart()
{
    if (canScanActive) {
        ESP_LOGW("CAN_SCAN", "Scan already running");
        return;
    }
    if (!requestManagerStarted) {
        id(can_scan_status).publish_state("nicht gestartet: CAN-Abfragen laufen noch nicht (Boot)");
        return;
    }
    canScanActive = true;
    canScanPhase = 1;
    canScanMemberPos = 0;
    canScanProbePos = 0;
    canScanTablePos = 0;
    canScanSent = canScanAnswers = canScanHits = canScanNa = 0;
    canScanAlive.clear();
    canScanAliveList.clear();
    canScanHitList.clear();
    canScanHitList.reserve(1024);
    memset(canScanRing, 0, sizeof(canScanRing));
    canScanRingPos = 0;
    canScanStartMs = canScanLastSendMs = millis();
    ESP_LOGI("CAN_SCAN", "Read-only scan started: %u members, %u table entries",
             (unsigned) CAN_SCAN_MEMBER_COUNT, (unsigned) High(ElsterTable));
    canScanPublishStatus("laeuft");
}

void canScanRepublish()
{
    for (const CanScanHit &h : canScanHitList) canScanPublishHit(h);
    ESP_LOGI("CAN_SCAN", "Republished %u hits", (unsigned) canScanHitList.size());
}

// Called from processAndUpdate() for every received frame that is not a read request
bool canScanOnFrame(uint32_t can_id, const std::vector<uint8_t> &msg)
{
    if (!canScanActive || msg.size() < 7) return false;

    // Only answers addressed to us (PC 0x680) belong to the scan
    const uint32_t receiver = ((uint32_t)(msg[0] & 0xf0) << 3) | (msg[1] & 0x07);
    if (receiver != CanMembers[cm_pc].CanId) return false;

    uint16_t index, raw;
    if (msg[2] == 0xfa) {
        index = (uint16_t)((msg[3] << 8) | msg[4]);
        raw = (uint16_t)((msg[5] << 8) | msg[6]);
    } else {
        index = msg[2];
        raw = (uint16_t)((msg[3] << 8) | msg[4]);
    }

    const uint32_t now = millis();
    bool matched = false;
    for (CanScanPending &p : canScanRing) {
        if (p.sentMs != 0 && p.canId == can_id && p.index == index && (uint32_t)(now - p.sentMs) < 5000) {
            p.sentMs = 0;
            matched = true;
            break;
        }
    }
    if (!matched) return false;

    canScanAnswers++;
    if (canScanPhase == 1) canScanAlive.insert(can_id);
    if (raw == 0x8000) {
        canScanNa++;
        return true;
    }
    canScanHits++;
    CanScanHit h = {(uint16_t) can_id, index, raw};
    if (canScanHitList.size() < 4096) canScanHitList.push_back(h);
    canScanPublishHit(h);
    return true;
}

// Called every 200 ms from an interval in can_scan.yaml
void canScanTick()
{
    if (!canScanActive) {
        if (!canScanReadyPublished && requestManagerStarted) {
            id(can_scan_status).publish_state("bereit (nicht gestartet)");
            canScanReadyPublished = true;
        }
        return;
    }

    // Safety: stop on bus problems
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK &&
        (st.state != TWAI_STATE_RUNNING || st.tx_error_counter > CAN_SCAN_MAX_TEC)) {
        canScanStop("Abbruch: CAN-Bus-Fehler");
        return;
    }

    const uint32_t now = millis();
    if ((uint32_t)(now - canScanLastStatusMs) > 30000UL) {
        canScanPublishStatus("laeuft");
    }
    if ((uint32_t)(now - canScanLastSendMs) < CAN_SCAN_PERIOD_MS) return;

    if (canScanPhase == 1) {
        while (canScanMemberPos < CAN_SCAN_MEMBER_COUNT && canScanSkipMember(CanMembers[canScanMemberPos].CanId)) {
            canScanMemberPos++;
            canScanProbePos = 0;
        }
        if (canScanMemberPos >= CAN_SCAN_MEMBER_COUNT) {
            if ((uint32_t)(now - canScanLastSendMs) < 3000UL) return;   // wait for late answers
            canScanAliveList.assign(canScanAlive.begin(), canScanAlive.end());
            if (canScanAliveList.empty()) {
                canScanStop("fertig: kein Teilnehmer hat geantwortet");
                return;
            }
            ESP_LOGI("CAN_SCAN", "Phase 1 done: %u members answered", (unsigned) canScanAliveList.size());
            canScanPhase = 2;
            canScanMemberPos = 0;
            canScanTablePos = 1;   // entry 0 is INDEX_NOT_FOUND
            memset(canScanSeen, 0, sizeof(canScanSeen));
            canScanPublishStatus("laeuft");
            return;
        }
        if (canScanSendRead(CanMembers[canScanMemberPos].CanId, CAN_SCAN_PROBE_INDEX[canScanProbePos])) {
            if (++canScanProbePos >= CAN_SCAN_PROBES_PER_MEMBER) {
                canScanProbePos = 0;
                canScanMemberPos++;
            }
        }
        return;
    }

    // Phase 2: next unique index for the current member
    while (canScanMemberPos < canScanAliveList.size()) {
        while (canScanTablePos <= High(ElsterTable)) {
            const uint16_t idx = ElsterTable[canScanTablePos].Index;
            const bool seen = canScanSeen[idx >> 3] & (1 << (idx & 7));
            if (idx == 0 || seen) {
                canScanTablePos++;
                continue;
            }
            if (canScanSendRead(canScanAliveList[canScanMemberPos], idx)) {
                canScanSeen[idx >> 3] |= (1 << (idx & 7));
                canScanTablePos++;
            }
            return;   // one request per period (also after a failed send: retry next period)
        }
        ESP_LOGI("CAN_SCAN", "Member 0x%03x done", (unsigned) canScanAliveList[canScanMemberPos]);
        canScanMemberPos++;
        canScanTablePos = 1;
        memset(canScanSeen, 0, sizeof(canScanSeen));
    }
    if ((uint32_t)(now - canScanLastSendMs) < 3000UL) return;   // wait for late answers
    canScanStop("fertig");
}

#endif // !defined(HA_DUMMY_BUILD)
