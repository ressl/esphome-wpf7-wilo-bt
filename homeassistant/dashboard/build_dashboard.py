#!/usr/bin/env python3
"""Build and optionally entity-validate the WPF 7 + Wilo Stratos PICO Home Assistant Sections dashboard."""
import json
import os
import re
import urllib.request

def load_ha_env():
    url = os.environ.get("HASS_URL", "").rstrip("/")
    token = os.environ.get("HASS_TOKEN", "")
    if not url or not token:
        raise RuntimeError("Set HASS_URL and HASS_TOKEN environment variables to validate against live Home Assistant.")
    return url, token

def tile(entity, name=None, cols=6, rows=1, features=None, features_pos=None, visibility=None, icon=None, color=None):
    c = {
        "type": "tile",
        "entity": entity,
        "grid_options": {"columns": cols, "rows": rows},
    }
    if name:
        c["name"] = name
    if icon:
        c["icon"] = icon
    if color:
        c["color"] = color
    if features:
        c["features"] = features
    if features_pos:
        c["features_position"] = features_pos
    if visibility:
        c["visibility"] = visibility
    return c

def heading(text, icon, style="title"):
    return {"type": "heading", "heading": text, "heading_style": style, "icon": icon}

def mcard(entity, primary, secondary, icon, color=None, cols=6, rows=1):
    c = {
        "type": "custom:mushroom-template-card",
        "entity": entity,
        "primary": primary,
        "secondary": secondary,
        "icon": icon,
        "tap_action": {"action": "more-info"},
        "grid_options": {"columns": cols, "rows": rows},
    }
    if color:
        c["color"] = color
    return c

def build_dashboard():
    # =========================================================================
    # TAB 1: Übersicht (modern HA Sections, 3 columns)
    # =========================================================================
    view_overview = {
        "title": "Übersicht",
        "path": "heizung",
        "icon": "mdi:heat-pump",
        "type": "sections",
        "max_columns": 3,
        "sections": [
            # --- Section 1: Jetzt ---
            {
                "type": "grid",
                "cards": [
                    heading("Jetzt", "mdi:heat-pump"),
                    tile("binary_sensor.heizung_verdichter_lauft", "Verdichter", cols=6),
                    mcard(
                        "sensor.utility_room_stiebel_eltron_wpf_7_fehlerstatus_klartext",
                        "Status",
                        "{{ states('sensor.utility_room_stiebel_eltron_wpf_7_fehlerstatus_klartext') }}",
                        "{% if is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_storung_aktiv', 'on') %}mdi:alert-octagon{% else %}mdi:check-circle{% endif %}",
                        color="{% if is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_storung_aktiv', 'on') %}red{% else %}green{% endif %}",
                        cols=6,
                    ),
                    mcard(
                        "sensor.utility_room_stiebel_eltron_wpf_7_relaisstatus_klartext",
                        "Aktive Relais",
                        "{{ states('sensor.utility_room_stiebel_eltron_wpf_7_relaisstatus_klartext') }}",
                        "mdi:electric-switch",
                        color="{% if is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_verdichter_relais', 'on') %}orange{% elif is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_quellenpumpe', 'on') %}amber{% else %}blue-grey{% endif %}",
                        cols=12,
                    ),
                    mcard(
                        "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_1_dhc1",
                        "Heizstab (DHC)",
                        "{% set d1 = is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_1_dhc1', 'on') %}{% set d2 = is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_2_dhc2', 'on') %}{% if d1 and d2 %}Stufe 1+2{% elif d1 %}Stufe 1{% elif d2 %}Stufe 2{% else %}Aus{% endif %}",
                        "mdi:heating-coil",
                        color="{% if is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_1_dhc1', 'on') or is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_2_dhc2', 'on') %}red{% else %}blue-grey{% endif %}",
                        cols=6,
                    ),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_verdichter_rest_stillstandszeit", "Rest-Stillstand", cols=6),
                    tile(
                        "select.stiebel_eltron_wpf_7_betriebsmodus_auswahl",
                        "Betriebsmodus",
                        cols=12,
                        rows=2,
                        features=[{"type": "select-options"}],
                        features_pos="bottom",
                    ),
                    tile("select.utility_room_stiebel_eltron_wpf_7_sg_ready_zustand", "SG Ready", cols=6),
                    tile("binary_sensor.stiebel_eltron_wpf_7_evu_sperre_aktiv", "EVU-Sperre", cols=6),
                ],
            },
            # --- Section 2: Raumführung Wohnzimmer ---
            {
                "type": "grid",
                "cards": [
                    heading("Raumführung Wohnzimmer", "mdi:home-thermometer"),
                    tile(
                        "input_boolean.wpf7_raumfuehrung",
                        "Raumführung",
                        cols=12,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    {
                        "type": "markdown",
                        "content": "⚠️ **Hinweis:** Betriebsmodus steht auf `{{ states('select.stiebel_eltron_wpf_7_betriebsmodus_auswahl') }}` – Raumführung regelt nur im Modus **Automatik**.",
                        "grid_options": {"columns": 12},
                        "visibility": [
                            {"condition": "state", "entity": "input_boolean.wpf7_raumfuehrung", "state": "on"},
                            {"condition": "state", "entity": "select.stiebel_eltron_wpf_7_betriebsmodus_auswahl", "state_not": "Automatik"},
                        ],
                    },
                    tile(
                        "input_number.wpf7_wohnzimmer_zieltemperatur",
                        "Zieltemperatur",
                        cols=12,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    tile("sensor.living_room_climate_sensor_temperature", "Wohnzimmer Ist", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_raumsolltemp_i", "Raum-Soll WPM i", cols=6),
                    tile("input_number.wpf7_raumfuehrung_integral", "I-Anteil", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_heizkurve", "Heizkurve", cols=6),
                    tile(
                        "number.stiebel_eltron_wpf_7_raum_solltemperatur_tag",
                        "Soll Tag",
                        cols=6,
                        visibility=[{"condition": "state", "entity": "input_boolean.wpf7_raumfuehrung", "state": "off"}],
                    ),
                    tile(
                        "number.stiebel_eltron_wpf_7_raum_solltemperatur_nacht",
                        "Soll Nacht",
                        cols=6,
                        visibility=[{"condition": "state", "entity": "input_boolean.wpf7_raumfuehrung", "state": "off"}],
                    ),
                ],
            },
            # --- Section 3: Temperaturen & Durchfluss ---
            {
                "type": "grid",
                "cards": [
                    heading("Temperaturen & Hydraulik", "mdi:thermometer"),
                    tile("sensor.stiebel_eltron_wpf_7_aussentemperatur", "Außen", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_puffersolltemperatur", "Puffer-Soll", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_wp_vorlauf_ist", "WP Vorlauf", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_vorlauf_solltemperatur", "Vorlauf-Soll", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_rucklauf_ist_temperatur", "Rücklauf", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_delta_t_vorlauf_rucklauf", "Spreizung ΔT", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_quellen_isttemperatur_sole", "Sole Ist", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_heissgas_temperatur", "Heißgas", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_volumenstrom", "Wilo Durchfluss", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_wassertemperatur", "Wilo Wasser", cols=6),
                ],
            },
            # --- Section 4: Effizienz, COP & Energie (2 cols wide) ---
            {
                "type": "grid",
                "column_span": 2,
                "cards": [
                    heading("Effizienz, COP & Energie", "mdi:flash"),
                    mcard(
                        "sensor.stiebel_eltron_wpf_7_geschatzter_cop",
                        "Live-COP",
                        "{% set c = states('sensor.stiebel_eltron_wpf_7_geschatzter_cop') %}{% if is_number(c) %}{{ c | float | round(2) }} · {{ states('sensor.utility_room_stiebel_eltron_wpf_7_cop_volumenstrom_quelle') }}{% else %}Standby · {{ states('sensor.utility_room_stiebel_eltron_wpf_7_cop_volumenstrom_quelle') }}{% endif %}",
                        "mdi:gauge",
                        color="{% set c = states('sensor.stiebel_eltron_wpf_7_geschatzter_cop') %}{% if not is_number(c) %}blue-grey{% elif c | float >= 4.0 %}green{% elif c | float >= 3.0 %}amber{% else %}red{% endif %}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.utility_room_stiebel_eltron_wpf_7_warmeleistung_live",
                        "Wärmeleistung",
                        "{% set w = states('sensor.utility_room_stiebel_eltron_wpf_7_warmeleistung_live') %}{{ ('%.2f kW' % (w | float)) if is_number(w) else 'unbekannt' }}",
                        "mdi:fire",
                        color="{% set w = states('sensor.utility_room_stiebel_eltron_wpf_7_warmeleistung_live') | float(0) %}{{ 'deep-orange' if w > 0.1 else 'blue-grey' }}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.shelly_power_meter_heating_pump_total_active_power",
                        "El. Leistung",
                        "{% set p = states('sensor.shelly_power_meter_heating_pump_total_active_power') %}{% set wp = states('sensor.utility_room_stiebel_eltron_wpf_7_wilo_elektrische_leistung') %}{{ ('%.0f W' % (p | float)) if is_number(p) else '?' }}{% if is_number(wp) %} (Wilo {{ wp | float | round(0) }} W){% endif %}",
                        "mdi:flash",
                        color="{% set p = states('sensor.shelly_power_meter_heating_pump_total_active_power') | float(0) %}{{ 'amber' if p > 200 else 'blue-grey' }}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_tages_az",
                        "Tages-AZ",
                        "{% set v = states('sensor.heizung_tages_az') %}{{ v | float | round(2) if is_number(v) else 'wartet auf Lauf' }}",
                        "mdi:chart-line-variant",
                        color="{% set v = states('sensor.heizung_tages_az') %}{{ 'green' if is_number(v) and v|float >= 3.5 else ('amber' if is_number(v) else 'blue-grey') }}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_monats_az",
                        "Monats-AZ",
                        "{% set v = states('sensor.heizung_monats_az') %}{{ v | float | round(2) if is_number(v) else 'sammelt Daten' }}",
                        "mdi:calendar-month",
                        color="{% set v = states('sensor.heizung_monats_az') %}{{ 'green' if is_number(v) and v|float >= 3.5 else ('amber' if is_number(v) else 'blue-grey') }}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_jahres_az_jaz",
                        "Jahres-AZ (JAZ)",
                        "{% set v = states('sensor.heizung_jahres_az_jaz') %}{{ v | float | round(2) if is_number(v) else 'sammelt Daten' }}",
                        "mdi:calendar-star",
                        color="{% set v = states('sensor.heizung_jahres_az_jaz') %}{{ 'green' if is_number(v) and v|float >= 3.5 else ('amber' if is_number(v) else 'blue-grey') }}",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_strom_tag",
                        "Strom heute",
                        "{% set v = states('sensor.heizung_strom_tag') | float(none) %}{{ ('%.2f kWh' % v) if v is number else 'unbekannt' }}",
                        "mdi:lightning-bolt",
                        color="amber",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_strom_monat",
                        "Strom Monat",
                        "{% set v = states('sensor.heizung_strom_monat') | float(none) %}{{ ('%.1f kWh' % v) if v is number else 'unbekannt' }}",
                        "mdi:lightning-bolt",
                        color="amber",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_strom_jahr",
                        "Strom Jahr",
                        "{% set v = states('sensor.heizung_strom_jahr') | float(none) %}{{ ('%.0f kWh' % v) if v is number else 'unbekannt' }}",
                        "mdi:lightning-bolt",
                        color="amber",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_warme_tag",
                        "Wärme heute",
                        "{% set v = states('sensor.heizung_warme_tag') | float(none) %}{{ ('%.2f kWh' % v) if v is number else '0,00 kWh' }}",
                        "mdi:fire-circle",
                        color="deep-orange",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_warme_monat",
                        "Wärme Monat",
                        "{% set v = states('sensor.heizung_warme_monat') | float(none) %}{{ ('%.1f kWh' % v) if v is number else '0,0 kWh' }}",
                        "mdi:fire-circle",
                        color="deep-orange",
                        cols=4,
                    ),
                    mcard(
                        "sensor.heizung_warme_jahr",
                        "Wärme Jahr",
                        "{% set v = states('sensor.heizung_warme_jahr') | float(none) %}{{ ('%.0f kWh' % v) if v is number else '0 kWh' }}",
                        "mdi:fire-circle",
                        color="deep-orange",
                        cols=4,
                    ),
                    tile("sensor.verdichter_laufzeit_heute", "Laufzeit heute", cols=4),
                    tile("sensor.verdichter_starts_heute", "Starts heute", cols=4),
                    tile("sensor.verdichter_laufzeit_gestern", "Laufzeit gestern", cols=4),
                ],
            },
            # --- Section 5: Wilo Schnellblick (1 col) ---
            {
                "type": "grid",
                "cards": [
                    heading("Wilo Stratos PICO", "mdi:pump"),
                    mcard(
                        "sensor.utility_room_stiebel_eltron_wpf_7_wilo_protokollstatus",
                        "Wilo Status",
                        "{% set st = states('sensor.utility_room_stiebel_eltron_wpf_7_wilo_protokollstatus') %}{% set al = states('sensor.utility_room_stiebel_eltron_wpf_7_wilo_aktive_meldungen') %}{{ st }} · Meldungen: {{ al }}",
                        "mdi:pump",
                        color="{% if is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_storung', 'on') or is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_durchfluss_zu_niedrig', 'on') %}red{% elif is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_warnung', 'on') %}amber{% elif is_state('binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_verbunden', 'on') %}green{% else %}blue-grey{% endif %}",
                        cols=12,
                    ),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_forderhohe", "Förderhöhe Ist", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_elektrische_leistung", "Wilo Leistung", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_drehzahl", "Drehzahl", cols=6),
                    tile("binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_durchfluss_zu_niedrig", "Durchfluss-Alarm", cols=6),
                    tile(
                        "number.utility_room_stiebel_eltron_wpf_7_wilo_sollwert_forderhohe",
                        "Sollwert Förderhöhe",
                        cols=12,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "input_boolean.wpf7_wilo_standby_absenkung",
                        "Standby-Absenkung",
                        cols=12,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile("sensor.wilo_strom_tag", "Wilo Strom heute", cols=6),
                    tile("sensor.wilo_stromanteil_heute", "Wilo Anteil heute", cols=6),
                ],
            },
            # --- Section 6: Verlauf 24h (full 3 cols) ---
            {
                "type": "grid",
                "column_span": 3,
                "cards": [
                    heading("Temperaturverlauf (24h)", "mdi:chart-line"),
                    {
                        "type": "custom:apexcharts-card",
                        "grid_options": {"columns": "full"},
                        "header": {
                            "show": True,
                            "title": "Letzte 24 Stunden",
                            "show_states": True,
                            "colorize_states": True,
                        },
                        "graph_span": "24h",
                        "update_interval": "5min",
                        "yaxis": [{"id": "t", "decimals": 0, "apex_config": {"tickAmount": 6}}],
                        "apex_config": {"chart": {"height": 320}, "legend": {"show": True}, "stroke": {"width": 2}},
                        "series": [
                            {
                                "entity": "sensor.living_room_climate_sensor_temperature",
                                "name": "Wohnzimmer",
                                "color": "#e67e22",
                                "yaxis_id": "t",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "10min"},
                            },
                            {
                                "entity": "input_number.wpf7_wohnzimmer_zieltemperatur",
                                "name": "Ziel",
                                "color": "#27ae60",
                                "yaxis_id": "t",
                                "curve": "stepline",
                                "extend_to": "now",
                                "float_precision": 1,
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_raumsolltemp_i",
                                "name": "Raum-Soll",
                                "color": "#8e44ad",
                                "yaxis_id": "t",
                                "curve": "stepline",
                                "extend_to": "now",
                                "float_precision": 1,
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_puffersolltemperatur",
                                "name": "Puffer-Soll",
                                "color": "#c0392b",
                                "yaxis_id": "t",
                                "curve": "stepline",
                                "float_precision": 1,
                                "group_by": {"func": "last", "duration": "10min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_wp_vorlauf_ist",
                                "name": "Vorlauf",
                                "color": "#e74c3c",
                                "yaxis_id": "t",
                                "opacity": 0.6,
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "10min"},
                                "show": {"in_header": False},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_rucklauf_ist_temperatur",
                                "name": "Rücklauf",
                                "color": "#f39c12",
                                "yaxis_id": "t",
                                "opacity": 0.6,
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "10min"},
                                "show": {"in_header": False},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_aussentemperatur",
                                "name": "Außen",
                                "color": "#3498db",
                                "yaxis_id": "t",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "10min"},
                                "show": {"in_header": False},
                            },
                        ],
                    },
                ],
            },
            # --- Section 7: Heizkurve 7 Tage (full 3 cols) ---
            {
                "type": "grid",
                "column_span": 3,
                "cards": [
                    heading("Heizkurve — Außen vs. Vorlauf/Rücklauf (7 Tage)", "mdi:chart-bell-curve-cumulative"),
                    {
                        "type": "custom:apexcharts-card",
                        "grid_options": {"columns": "full"},
                        "header": {"title": "Letzte 7 Tage", "show": True},
                        "graph_span": "7d",
                        "update_interval": "5min",
                        "apex_config": {"chart": {"height": 300}, "stroke": {"width": 2, "curve": "smooth"}},
                        "yaxis": [
                            {
                                "id": "temp",
                                "min": "~-5",
                                "max": "~35",
                                "decimals": 0,
                                "apex_config": {"tickAmount": 8, "title": {"text": "°C"}},
                            }
                        ],
                        "series": [
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_aussentemperatur",
                                "name": "Außentemperatur",
                                "color": "#3498db",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "30min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_wp_vorlauf_ist",
                                "name": "WP Vorlauf",
                                "color": "#e74c3c",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "30min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_rucklauf_ist_temperatur",
                                "name": "Rücklauf",
                                "color": "#e67e22",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "30min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_puffersolltemperatur",
                                "name": "Puffer-Soll",
                                "color": "#8e44ad",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "curve": "stepline",
                                "group_by": {"func": "avg", "duration": "30min"},
                            },
                        ],
                    },
                ],
            },
        ],
    }

    # =========================================================================
    # TAB 2: Wilo Pumpe (Stratos PICO Monitoring, Steuerung & Schutz)
    # =========================================================================
    view_wilo = {
        "title": "Wilo Pumpe",
        "path": "wilo",
        "icon": "mdi:pump",
        "type": "sections",
        "max_columns": 3,
        "sections": [
            # --- Section 1: Live-Messwerte ---
            {
                "type": "grid",
                "cards": [
                    heading("Live-Messwerte", "mdi:gauge"),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_volumenstrom", "Volumenstrom", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_forderhohe", "Förderhöhe", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_elektrische_leistung", "Leistung", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_drehzahl", "Drehzahl", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_wassertemperatur", "Wassertemperatur", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_regelungsart", "Regelungsart", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_sollwert", "Sollwert (m)", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_sollwert_prozent", "Sollwert (%)", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_betriebsstunden", "Betriebsstunden", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_elektrische_energie", "Energie gesamt", cols=6),
                    tile("sensor.wilo_strom_tag", "Strom heute", cols=6),
                    tile("sensor.wilo_strom_monat", "Strom Monat", cols=6),
                ],
            },
            # --- Section 2: Direktsteuerung (Wilo BT) ---
            {
                "type": "grid",
                "cards": [
                    heading("Direktsteuerung (Wilo BT)", "mdi:tune"),
                    tile(
                        "switch.utility_room_stiebel_eltron_wpf_7_wilo_pumpe_betrieb",
                        "Pumpe Betrieb",
                        cols=12,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile(
                        "number.utility_room_stiebel_eltron_wpf_7_wilo_sollwert_forderhohe",
                        "Sollwert Förderhöhe",
                        cols=12,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "number.utility_room_stiebel_eltron_wpf_7_wilo_sollwert_prozent_steuerung",
                        "Sollwert Prozent",
                        cols=12,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "slider"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "select.utility_room_stiebel_eltron_wpf_7_wilo_regelungsart_auswahl",
                        "Regelungsart Auswahl",
                        cols=12,
                        rows=2,
                        features=[{"type": "select-options"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "switch.utility_room_stiebel_eltron_wpf_7_wilo_nachtabsenkung",
                        "Nachtabsenkung",
                        cols=6,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile(
                        "switch.utility_room_stiebel_eltron_wpf_7_wilo_druckunabhangiges_ventil",
                        "Druckunabh. Ventil",
                        cols=6,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile(
                        "switch.utility_room_stiebel_eltron_wpf_7_wilo_entluftung",
                        "Entlüftung starten",
                        cols=6,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile("binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_entluftung_aktiv", "Entlüftung aktiv", cols=6),
                ],
            },
            # --- Section 3: Automatik & WPF-7-Schutz ---
            {
                "type": "grid",
                "cards": [
                    heading("Automatik & WPF-7-Schutz", "mdi:shield-check"),
                    tile(
                        "input_boolean.wpf7_wilo_mindestdurchfluss_schutz",
                        "Mindestdurchfluss-Schutz",
                        cols=12,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile("binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_durchfluss_zu_niedrig", "Durchfluss-Alarm (< 0,75 m³/h)", cols=12),
                    tile(
                        "input_boolean.wpf7_wilo_standby_absenkung",
                        "Standby-Absenkung",
                        cols=12,
                        features=[{"type": "toggle"}],
                        features_pos="inline",
                    ),
                    tile(
                        "input_number.wpf7_wilo_sollwert_betrieb",
                        "Sollwert Betrieb",
                        cols=6,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "input_number.wpf7_wilo_sollwert_standby",
                        "Sollwert Standby",
                        cols=6,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    tile(
                        "input_number.wpf7_wilo_nachlauf_min",
                        "Nachlaufzeit",
                        cols=12,
                        rows=2,
                        features=[{"type": "numeric-input", "style": "buttons"}],
                        features_pos="bottom",
                    ),
                    {
                        "type": "markdown",
                        "content": (
                            "**Funktionsweise:**\n"
                            "- **Schutz:** Hebt Förderhöhe sofort um `+0,5 m` (max. `3,5 m`) an, falls bei WP-Betrieb `< 0,75 m³/h` anliegen (WPF-7-Min.: `0,70 m³/h`).\n"
                            "- **Standby-Absenkung:** Hebt **30 s vor Verdichterstart** über das Signal *Quellenpumpe* sofort auf Betriebs-Sollwert an und senkt erst nach Ablauf der Nachlaufzeit ab."
                        ),
                        "grid_options": {"columns": 12},
                    },
                ],
            },
            # --- Section 4: Wilo Verlauf 24h (2 cols) ---
            {
                "type": "grid",
                "column_span": 2,
                "cards": [
                    heading("Pumpenverlauf (24h)", "mdi:chart-areaspline"),
                    {
                        "type": "custom:apexcharts-card",
                        "grid_options": {"columns": "full"},
                        "header": {
                            "show": True,
                            "title": "Volumenstrom, Förderhöhe & Leistung",
                            "show_states": True,
                            "colorize_states": True,
                        },
                        "graph_span": "24h",
                        "update_interval": "2min",
                        "apex_config": {"chart": {"height": 320}, "stroke": {"width": 2, "curve": "smooth"}},
                        "yaxis": [
                            {"id": "flow", "min": 0, "max": "~2.0", "decimals": 2, "apex_config": {"title": {"text": "m³/h"}}},
                            {"id": "head", "min": 0, "max": "~5.0", "decimals": 1, "opposite": True, "apex_config": {"title": {"text": "m"}}},
                            {"id": "watt", "min": 0, "max": "~45", "decimals": 0, "show": False},
                        ],
                        "series": [
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_volumenstrom",
                                "name": "Volumenstrom",
                                "color": "#2980b9",
                                "yaxis_id": "flow",
                                "float_precision": 2,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_forderhohe",
                                "name": "Förderhöhe",
                                "color": "#16a085",
                                "yaxis_id": "head",
                                "float_precision": 2,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_elektrische_leistung",
                                "name": "Leistung (W)",
                                "color": "#f39c12",
                                "yaxis_id": "watt",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                        ],
                    },
                ],
            },
            # --- Section 5: BLE & Gerätedaten (1 col) ---
            {
                "type": "grid",
                "cards": [
                    heading("BLE & Gerätedaten", "mdi:bluetooth"),
                    {
                        "type": "entities",
                        "show_header_toggle": False,
                        "state_color": True,
                        "grid_options": {"columns": 12},
                        "entities": [
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_verbunden", "name": "Verbunden"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_authentifiziert", "name": "Authentifiziert"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_protokollstatus", "name": "Protokollstatus"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_storung", "name": "Störung"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wilo_warnung", "name": "Warnung"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_aktive_meldungen", "name": "Aktive Meldungen"},
                            {"type": "divider"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_geratename", "name": "Gerätename"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_pumpentyp", "name": "Pumpentyp"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_seriennummer", "name": "Seriennummer"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_softwareversion", "name": "Softwareversion"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_max_forderhohe", "name": "Max. Förderhöhe"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_wilo_max_volumenstrom", "name": "Max. Volumenstrom"},
                            {"type": "divider"},
                            {"entity": "switch.utility_room_stiebel_eltron_wpf_7_wilo_ble_verbindung", "name": "BLE Verbindung"},
                            {"entity": "button.utility_room_stiebel_eltron_wpf_7_wilo_authentifizieren", "name": "Neu authentifizieren"},
                        ],
                    },
                ],
            },
        ],
    }

    # =========================================================================
    # TAB 3: Diagnose & Wartung (Kältekreis R410A, Relais, CAN, ESP32)
    # =========================================================================
    view_diag = {
        "title": "Diagnose & Wartung",
        "path": "diagnose",
        "icon": "mdi:wrench-cog",
        "type": "sections",
        "max_columns": 3,
        "sections": [
            # --- Section 1: Kältekreis (R410A) ---
            {
                "type": "grid",
                "cards": [
                    heading("Kältekreis (R410A)", "mdi:snowflake-thermometer"),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_hochdruck_kaltekreis", "Hochdruck (HD)", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_kondensationstemperatur_r410a", "Kondensationstemp.", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_verflussiger_gradigkeit", "Verflüssiger-Grädigkeit", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_heissgas_uberhitzung", "Heißgas-Überhitzung", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_heissgas_temperatur", "Heißgas", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_quellen_isttemperatur_sole", "Sole Ist", cols=6),
                    tile("sensor.stiebel_eltron_wpf_7_quellen_solltemperatur_sole", "Sole Soll (Min)", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_cop_volumenstrom_quelle", "COP Flow-Quelle", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wp_leistung_ohne_wilo", "WP ohne Wilo", cols=6),
                    tile("sensor.utility_room_stiebel_eltron_wpf_7_wilo_leistungsanteil", "Wilo Anteil", cols=6),
                ],
            },
            # --- Section 2: WPM i Relais & Betriebsstunden ---
            {
                "type": "grid",
                "cards": [
                    heading("WPM i Relais & Zähler", "mdi:electric-switch"),
                    {
                        "type": "entities",
                        "show_header_toggle": False,
                        "state_color": True,
                        "grid_options": {"columns": 12},
                        "entities": [
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_verdichter_relais", "name": "Verdichter (Bit 2+3)"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_quellenpumpe", "name": "Quellenpumpe (Bit 8)"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_pufferladepumpe", "name": "Pufferladepumpe (Bit 6)"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizkreispumpe", "name": "Heizkreispumpe (Bit 5)"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_1_dhc1", "name": "Heizstab DHC1 (Bit 0)"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_wp_heizstab_stufe_2_dhc2", "name": "Heizstab DHC2 (Bit 1)"},
                            {"type": "divider"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_verdichter_betriebsstunden", "name": "Verdichter gesamt"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_heizstab_stufe_1_betriebsstunden", "name": "Heizstab Stufe 1 gesamt"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_heizstab_stufe_2_betriebsstunden", "name": "Heizstab Stufe 2 gesamt"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_verdichter_rest_stillstandszeit", "name": "Rest-Stillstandszeit"},
                            {"entity": "binary_sensor.utility_room_stiebel_eltron_wpf_7_sommerbetrieb", "name": "Sommerbetrieb-Automatik (18 °C)"},
                            {"type": "divider"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_fehlerstatus_klartext", "name": "Fehlerstatus"},
                            {"entity": "sensor.stiebel_eltron_wpf_7_fehler_code", "name": "Fehler-Code"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_relaisstatus_bits", "name": "Relais-Bits (0x0a20)"},
                        ],
                    },
                ],
            },
            # --- Section 3: Bus & ESP32 ---
            {
                "type": "grid",
                "cards": [
                    heading("CAN-Bus & ESP32", "mdi:lan"),
                    {
                        "type": "entities",
                        "show_header_toggle": False,
                        "state_color": True,
                        "grid_options": {"columns": 12},
                        "entities": [
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_can_state", "name": "CAN-Zustand"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_can_bus_errors", "name": "Busfehler"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_can_tx_error_counter", "name": "TX-Fehler"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_can_rx_error_counter", "name": "RX-Fehler"},
                            {"type": "divider"},
                            {"entity": "sensor.stiebel_eltron_wpf_7_wifi_signal_dbm_2", "name": "WLAN"},
                            {"entity": "sensor.stiebel_eltron_wpf_7_firmware_version", "name": "Firmware"},
                            {"entity": "sensor.stiebel_eltron_wpf_7_esphome_version", "name": "ESPHome"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_uptime", "name": "Uptime"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_reset_grund", "name": "Reset-Grund"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_heap_frei", "name": "Heap frei"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_heap_grosster_block", "name": "Heap größter Block"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_psram_frei", "name": "PSRAM frei"},
                        ],
                    },
                ],
            },
            # --- Section 4: Kältekreis-Verlauf (24h, full 3 cols) ---
            {
                "type": "grid",
                "column_span": 3,
                "cards": [
                    heading("Kältekreis- & Leistungsverlauf (24h)", "mdi:chart-line"),
                    {
                        "type": "custom:apexcharts-card",
                        "grid_options": {"columns": "full"},
                        "header": {
                            "show": True,
                            "title": "Hochdruck (bar), Heißgas, Vorlauf & Sole",
                            "show_states": True,
                            "colorize_states": True,
                        },
                        "graph_span": "24h",
                        "update_interval": "5min",
                        "apex_config": {"chart": {"height": 320}, "stroke": {"width": 2}},
                        "yaxis": [
                            {"id": "temp", "min": 0, "max": "~70", "decimals": 0, "apex_config": {"title": {"text": "°C"}}},
                            {"id": "bar", "min": 5, "max": "~35", "decimals": 0, "opposite": True, "apex_config": {"title": {"text": "bar"}}},
                        ],
                        "series": [
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_hochdruck_kaltekreis",
                                "name": "Hochdruck (bar)",
                                "color": "#9b59b6",
                                "yaxis_id": "bar",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_heissgas_temperatur",
                                "name": "Heißgas",
                                "color": "#e74c3c",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_kondensationstemperatur_r410a",
                                "name": "Kondensation (R410A)",
                                "color": "#e67e22",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.stiebel_eltron_wpf_7_wp_vorlauf_ist",
                                "name": "WP Vorlauf",
                                "color": "#f1c40f",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                            {
                                "entity": "sensor.utility_room_stiebel_eltron_wpf_7_quellen_isttemperatur_sole",
                                "name": "Sole Ist",
                                "color": "#3498db",
                                "yaxis_id": "temp",
                                "float_precision": 1,
                                "group_by": {"func": "avg", "duration": "5min"},
                            },
                        ],
                    },
                ],
            },
            # --- Section 5: Wartung & WPM i Zeit / Raum-Soll ---
            {
                "type": "grid",
                "cards": [
                    heading("Wartung & WPM i", "mdi:wrench"),
                    {
                        "type": "entities",
                        "show_header_toggle": False,
                        "state_color": True,
                        "grid_options": {"columns": 12},
                        "entities": [
                            {"entity": "sensor.stiebel_eltron_wpf_7_datum", "name": "WPM i Datum"},
                            {"entity": "sensor.stiebel_eltron_wpf_7_uhrzeit", "name": "WPM i Uhrzeit"},
                            {"entity": "button.stiebel_eltron_wpf_7_update_uhrzeit", "name": "Uhrzeit senden"},
                            {"entity": "button.stiebel_eltron_wpf_7_update_datum", "name": "Datum senden"},
                            {"type": "section", "label": "Sollwerte & SG Ready"},
                            {"entity": "number.stiebel_eltron_wpf_7_raum_solltemperatur_ii", "name": "Raum-Soll II"},
                            {"entity": "number.stiebel_eltron_wpf_7_raum_solltemperatur_iii", "name": "Raum-Soll III"},
                            {"entity": "number.utility_room_stiebel_eltron_wpf_7_sg_ready_boost_zustand_3", "name": "SG Ready Boost 3"},
                            {"entity": "number.utility_room_stiebel_eltron_wpf_7_sg_ready_boost_zustand_4", "name": "SG Ready Boost 4"},
                            {"type": "section", "label": "CAN-Brücke"},
                            {
                                "type": "button",
                                "name": "CAN-Brücke neu starten",
                                "icon": "mdi:restart",
                                "action_name": "Neustart",
                                "tap_action": {
                                    "action": "perform-action",
                                    "perform_action": "button.press",
                                    "target": {"entity_id": "button.stiebel_eltron_wpf_7_restart"},
                                    "confirmation": {
                                        "text": "CAN-Brücke (ESP32) wirklich neu starten? Die Wärmepumpe läuft weiter."
                                    },
                                },
                            },
                        ],
                    },
                ],
            },
            # --- Section 6: Automationen & Rohwerte ---
            {
                "type": "grid",
                "column_span": 2,
                "cards": [
                    heading("Automationen & Roh-Diagnose", "mdi:robot"),
                    {
                        "type": "entities",
                        "show_header_toggle": False,
                        "state_color": True,
                        "grid_options": {"columns": 12},
                        "entities": [
                            {"entity": "automation.wpf_7_wilo_mindestdurchfluss_schutz", "name": "Automation: Mindestdurchfluss-Schutz"},
                            {"entity": "automation.wpf_7_wilo_standby_absenkung", "name": "Automation: Wilo Standby-Absenkung"},
                            {"entity": "automation.wpf_7_storungs_heizstab_alarm", "name": "Automation: Störungs- & Heizstab-Alarm"},
                            {"type": "divider"},
                            {"entity": "binary_sensor.stiebel_eltron_wpf_7_brenner", "name": "Signal BRENNER (0x601)"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_maschinendruck_roh", "name": "Maschinendruck (roh, 0x0268)"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_relaisstatus_roh", "name": "Relaisstatus (roh, 0x0a20)"},
                            {"entity": "sensor.utility_room_stiebel_eltron_wpf_7_zweiter_we_status_roh", "name": "WP_EVU / 2. WE (roh, 0xfdab)"},
                            {"entity": "switch.utility_room_stiebel_eltron_wpf_7_can_busmonitor_nur_lesen", "name": "CAN-Busmonitor (nur lesen)"},
                        ],
                    },
                ],
            },
        ],
    }

    return {"views": [view_overview, view_wilo, view_diag]}

def collect_entities(obj, found=None):
    if found is None:
        found = set()
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ("entity", "entity_id") and isinstance(v, str) and "." in v:
                found.add(v)
            else:
                collect_entities(v, found)
    elif isinstance(obj, list):
        for item in obj:
            collect_entities(item, found)
    elif isinstance(obj, str):
        for m in re.findall(r"(?:states|is_state)\('([a-z_]+\.[a-z0-9_]+)'", obj):
            found.add(m)
    return found

if __name__ == "__main__":
    import argparse
    from pathlib import Path

    parser = argparse.ArgumentParser(description="Build the WPF 7 + Wilo Stratos PICO Home Assistant dashboard JSON.")
    parser.add_argument(
        "-o", "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "dashboard_heatingpump.json",
        help="Output JSON path (default: dashboard_heatingpump.json next to this script)",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Validate all entity IDs against a live Home Assistant instance via HASS_URL and HASS_TOKEN",
    )
    args = parser.parse_args()

    cfg = build_dashboard()
    args.output.write_text(json.dumps(cfg, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    refs = collect_entities(cfg)
    print(f"Built dashboard: {len(cfg['views'])} views, {len(refs)} unique entity refs -> {args.output}")

    if args.validate:
        base, token = load_ha_env()
        req = urllib.request.Request(base + "/api/states", headers={"Authorization": "Bearer " + token})
        live_ids = {s["entity_id"] for s in json.loads(urllib.request.urlopen(req, timeout=15).read().decode())}
        missing = sorted(refs - live_ids)
        print(f"Live validation: {len(missing)} missing entities")
        if missing:
            print("MISSING ENTITIES:", missing)
            raise SystemExit(1)
        print("ALL ENTITY REFERENCES VERIFIED 100% AGAINST LIVE HOME ASSISTANT!")
