#!/usr/bin/env python3
"""
Genere la configuration personnalisee "RFP" (page de parametres de VESC Tool) :

  rfp/rfp_conf.xml      description ConfigParams (format de VESC Tool), pour memoire et pour VESC Tool
  rfp/rfp_conf_gen.h    structure rfp_config, signature, taille, prototypes
  rfp/rfp_conf_gen.c    serialisation / deserialisation / defauts, XML compresse (qCompress)

Le format est celui des configurations personnalisees de VESC (voir
lispBM/c_libs/examples/config/conf) : signature = crc32c(pour chaque <ser> : nom + type + vTx +
noms d'enum), puis les champs dans l'ordre de <SerOrder>. L'algorithme de signature est verifie
contre l'exemple officiel (BALANCE_CONFIG_SIGNATURE = 32903057).

Pour ajouter un parametre : l'ajouter dans PARAMS, puis : python3 rfp/gen_rfp_conf.py
(la memoire de rfp_ext.c enregistre la configuration serialisee : rien d'autre a changer).
"""
import os
import zlib
import xml.sax.saxutils as su

HERE = os.path.dirname(os.path.abspath(__file__))

CONFIG_NAME = "rfp_config"
PAGE_TITLE = "RFP Extras"


def B(name, title, desc, dflt=0):
    return dict(name=name, kind="bool", title=title, desc=desc, dflt=dflt)


def E(name, title, desc, enums, dflt=0):
    return dict(name=name, kind="enum", title=title, desc=desc, dflt=dflt, enums=enums)


def F(name, title, desc, dflt, lo, hi, step, dec, suffix="", pct=False):
    return dict(name=name, kind="float", title=title, desc=desc, dflt=dflt, lo=lo, hi=hi, step=step, dec=dec,
                suffix=suffix, pct=pct)


REL = ("Applies to relative current requests: ADC and PPM apps, VESC Remote, PAS, CAN. "
       "The VESC Tool keyboard (absolute current) is not affected.")

PARAMS = [
    # ---------------------------------------------------------------- duty
    B("basic_duty_enable", "Basic Duty Mode",
      "Duty control: when the requested duty drops below the present duty, the motor is released: no braking "
      "and no acceleration (zero current, the bridge stays on) until the duty falls to the request, then it "
      "follows it again. Off: the Duty Downramp regulator brakes to follow a falling request."),
    # ---------------------------------------------------------------- forward
    B("hybrid_enable", "Hybrid Current / Duty (forward)",
      "Forward requests: the duty is limited proportionally to the throttle, so half throttle gives half torque "
      "and, without load, about half speed. Duty limit = throttle x Duty Limit at Full Throttle x Maximum Duty "
      "Cycle. The current decreases linearly over the Current Taper Band below the limit and is zero at the "
      "limit. " + REL),
    F("hybrid_duty_limit", "Duty Limit at Full Throttle",
      "Duty limit at full throttle, as a fraction of Maximum Duty Cycle.", 1.0, 0.05, 1.0, 0.01, 0, " %", True),
    F("hybrid_band", "Current Taper Band",
      "Part of the duty limit over which the current decreases to zero.", 0.15, 0.02, 0.6, 0.01, 0, " %", True),
    # ---------------------------------------------------------------- braking / reverse
    B("braking_reverse_enable", "Braking / Reverse Handling",
      "On: the Braking and Reverse settings below apply to reverse requests (negative relative current). "
      "Off: official behaviour (negative current, then reverse at full current). " + REL),
    E("braking_mode", "Braking",
      "Reverse request while rolling forward faster than Stop ERPM. Active: negative current as in the official "
      "firmware: torque against the motion, current injected from the battery when regeneration is not enough, "
      "down to Stop ERPM. Regen Only: brake current (as the No Reverse control types): regeneration only, "
      "phases shorted when the current cannot be held. " + REL,
      ["Active (negative current)", "Regen Only (brake)"]),
    E("reverse_mode", "Reverse",
      "Reverse request below Stop ERPM (stopped or already reversing). Current: reverse in current, limited by "
      "the Reverse Current settings (100 % = official firmware). None: never reverse, zero current. Duty: "
      "reverse in duty proportional to the request, up to Reverse Duty Max. Smart: reverse in current with a "
      "duty limit proportional to the request, raised with a ramp. " + REL,
      ["Current", "None", "Duty", "Smart"]),
    F("stop_erpm", "Stop ERPM",
      "Below this speed the vehicle is considered stopped: end of braking, start of the reverse handling. "
      "Sensorless motors: keep a few hundred ERPM (speed reading is noisy near standstill).",
      300.0, 0.0, 10000.0, 10.0, 0, " ERPM"),
    F("stop_hyst", "Stop ERPM Hysteresis",
      "Once reversing (below Stop ERPM), braking resumes only above Stop ERPM x (1 + this value). Avoids "
      "switching back and forth around Stop ERPM.", 0.5, 0.0, 3.0, 0.05, 0, " %", True),
    F("rev_transition", "Brake / Reverse Transition Time",
      "Time over which the current moves from the braking value to the reverse value (and back), instead of a "
      "sudden change of sign. 0 = immediate.", 0.2, 0.0, 2.0, 0.05, 2, " s"),
    F("brake_taper_start_erpm", "Braking Taper Start ERPM",
      "Active braking: below this speed the braking current decreases, down to Braking Taper Min at Stop ERPM. "
      "Limits the overshoot into reverse.", 1500.0, 0.0, 30000.0, 10.0, 0, " ERPM"),
    F("brake_taper_min", "Braking Taper Min",
      "Active braking: fraction of the braking current kept at Stop ERPM. 100 % = no reduction (official).",
      1.0, 0.0, 1.0, 0.01, 0, " %", True),
    F("rev_cur_max", "Reverse Current Max",
      "Reverse = Current: reverse current at full request, as a fraction of the motor current limit.",
      1.0, 0.0, 1.0, 0.01, 0, " %", True),
    F("rev_cur_duty_max", "Reverse Current Duty Max",
      "Reverse = Current: duty limit in reverse (top reverse speed), as a fraction of Maximum Duty Cycle. "
      "The current decreases over the Reverse Taper Band below it. 100 % = no limit.",
      1.0, 0.02, 1.0, 0.01, 0, " %", True),
    F("rev_band", "Reverse Taper Band",
      "Reverse = Current or Smart: part of the reverse duty limit over which the current decreases to zero.",
      0.15, 0.02, 0.6, 0.01, 0, " %", True),
    F("rev_duty_max", "Reverse Duty Max",
      "Reverse = Duty: duty at full reverse request, as a fraction of Maximum Duty Cycle.",
      1.0, 0.02, 1.0, 0.01, 0, " %", True),
    F("smart_cur_max", "Smart Reverse Current Max",
      "Reverse = Smart: reverse current at full request, as a fraction of the motor current limit.",
      1.0, 0.0, 1.0, 0.01, 0, " %", True),
    F("smart_duty_max", "Smart Reverse Duty Max",
      "Reverse = Smart: duty limit at full reverse request, as a fraction of Maximum Duty Cycle.",
      0.08, 0.02, 1.0, 0.01, 0, " %", True),
    F("smart_ramp", "Smart Reverse Ramp Time",
      "Reverse = Smart: time to raise the duty limit from 0 to Smart Reverse Duty Max.", 3.0, 0.01, 20.0, 0.1, 2, " s"),
    B("smart_repress", "Smart Reverse: Re-press to Reverse",
      "Reverse = Smart: after braking to a stop, return to neutral and request reverse again to start reversing."),
    # ---------------------------------------------------------------- tractor
    B("tractor_enable", "Tractor Mode",
      "On: the throttle (relative current request) drives the tractor spring below instead of the motor "
      "current. Off: normal operation. " + REL),
    E("tractor_mode", "Tractor Output",
      "Virtual spring on the wheel position (tachometer). An anchor position is held with zero throttle and "
      "moves with the throttle. Within Freedom of the anchor the motor is free; beyond, a force proportional to "
      "the distance pulls the wheel back (Stiffness). Brake opposes the free motion. For stairs, mud... "
      "Spring Current: output in current. Spring Duty: output in duty (to try). Any other command (brake, "
      "duty, keyboard, timeout) stops the tractor at once.",
      ["Spring Current", "Spring Duty"]),
    E("tractor_speed_mode", "Tractor Speed",
      "Throttle: anchor speed proportional to the throttle, up to Tractor Max ERPM. Preset: the anchor moves "
      "at Tractor Max ERPM as soon as the throttle is pressed (direction given by its sign).",
      ["Throttle", "Preset"]),
    F("tractor_max_erpm", "Tractor Max ERPM", "Anchor speed at full throttle (or the preset speed).",
      1500.0, 0.0, 30000.0, 10.0, 0, " ERPM"),
    F("tractor_freedom", "Tractor Freedom",
      "Free play around the anchor, in electrical degrees (the tachometer counts 60 degree steps).",
      120.0, 0.0, 3600.0, 60.0, 0, " deg"),
    F("tractor_stiffness", "Tractor Stiffness (Current)",
      "Spring Current: current per electrical degree beyond Freedom.", 0.1, 0.0, 10.0, 0.01, 3, " A/deg"),
    F("tractor_brake", "Tractor Brake (Current)",
      "Spring Current: current against the speed difference to the anchor, per 1000 ERPM.",
      2.0, 0.0, 200.0, 0.1, 2, " A/kERPM"),
    F("tractor_max_current", "Tractor Max Current", "Spring Current: current limit (also capped by the motor limit).",
      20.0, 0.0, 500.0, 1.0, 1, " A"),
    F("tractor_max_lag", "Tractor Max Lag",
      "Largest distance between the wheel and the anchor, in electrical degrees. When the wheel is blocked the "
      "anchor stops moving away beyond it (no huge pull when it frees).", 720.0, 60.0, 36000.0, 60.0, 0, " deg"),
    F("tractor_duty_stiffness", "Tractor Stiffness (Duty)",
      "Spring Duty: duty per electrical degree beyond Freedom.", 0.0005, 0.0, 0.1, 0.0001, 4, " /deg"),
    F("tractor_duty_brake", "Tractor Brake (Duty)",
      "Spring Duty: duty against the speed difference to the anchor, per 1000 ERPM.", 0.02, 0.0, 1.0, 0.001, 3, " /kERPM"),
    F("tractor_max_duty", "Tractor Max Duty", "Spring Duty: duty limit, as a fraction of Maximum Duty Cycle.",
      0.3, 0.02, 1.0, 0.01, 0, " %", True),
]

# page : groupe "General" (seul groupe lu par VESC Tool) -> onglets -> elements
GROUPING = [
    ("General", [
        ("Duty", ["::sep::Basic Duty Mode", "basic_duty_enable"]),
        ("Forward", ["::sep::Hybrid Current / Duty", "hybrid_enable", "hybrid_duty_limit", "hybrid_band"]),
        ("Braking / Reverse", ["::sep::Braking / Reverse", "braking_reverse_enable", "stop_erpm", "stop_hyst",
                               "rev_transition", "::sep::Braking", "braking_mode", "brake_taper_start_erpm",
                               "brake_taper_min", "::sep::Reverse", "reverse_mode", "::sep::Reverse = Current",
                               "rev_cur_max", "rev_cur_duty_max", "rev_band", "::sep::Reverse = Duty",
                               "rev_duty_max", "::sep::Reverse = Smart", "smart_cur_max", "smart_duty_max",
                               "smart_ramp", "smart_repress"]),
        ("Tractor", ["::sep::Tractor Mode", "tractor_enable", "tractor_mode", "tractor_speed_mode", "tractor_max_erpm",
                     "tractor_freedom", "tractor_max_lag", "::sep::Spring Current", "tractor_stiffness",
                     "tractor_brake", "tractor_max_current", "::sep::Spring Duty", "tractor_duty_stiffness",
                     "tractor_duty_brake", "tractor_max_duty"]),
    ]),
]

HTML_HEAD = ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">\n'
             '<html><head><meta name="qrichtext" content="1" /><style type="text/css">\n'
             'p, li { white-space: pre-wrap; }\n'
             '</style></head><body style=" font-family:\'Roboto\'; ; font-weight:400; font-style:normal;">\n')
HTML_P = ('<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; '
          '-qt-block-indent:0; text-indent:0px;">%s</p></body></html>')


def crc32c(data):
    c = 0xFFFFFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0x82F63B78 & -(c & 1))
    return (~c) & 0xFFFFFFFF


def esc(t):
    return su.escape(t, {'"': "&quot;"})


def fmt(v):
    s = repr(float(v))
    return s[:-2] if s.endswith(".0") else s


def cfloat(v):
    s = "%.6g" % float(v)
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def xtype(p):
    return {"bool": 5, "enum": 4, "float": 1}[p["kind"]]


def build_xml():
    o = ['<?xml version="1.0" encoding="UTF-8"?>', "<ConfigParams>", "    <Params>"]

    def desc(t):
        return "            <description>%s</description>" % esc(HTML_HEAD + (HTML_P % esc(t)))

    o += ["        <config_name>", "            <longName>none</longName>", "            <type>3</type>",
          "            <transmittable>0</transmittable>", desc(""), "            <cDefine></cDefine>",
          "            <valString>%s</valString>" % CONFIG_NAME, "            <maxLen>0</maxLen>", "        </config_name>"]
    o += ["        <hw_name>", "            <longName>%s</longName>" % PAGE_TITLE, "            <type>0</type>",
          "            <transmittable>0</transmittable>", desc("Extra functions added by the RFP build."),
          "            <cDefine></cDefine>", "        </hw_name>"]
    for p in PARAMS:
        n = p["name"]
        o.append("        <%s>" % n)
        o.append("            <longName>%s</longName>" % esc(p["title"]))
        o.append("            <type>%d</type>" % xtype(p))
        o.append("            <transmittable>1</transmittable>")
        o.append(desc(p["desc"]))
        o.append("            <cDefine>RFP_%s</cDefine>" % n.upper())
        if p["kind"] in ("bool", "enum"):
            o.append("            <valInt>%d</valInt>" % int(p["dflt"]))
            for e in p.get("enums", []):
                o.append("            <enumNames>%s</enumNames>" % esc(e))
        else:
            o += ["            <editorDecimalsDouble>%d</editorDecimalsDouble>" % p["dec"],
                  "            <editorScale>%s</editorScale>" % ("100" if p["pct"] else "1"),
                  # pourcentage comme l'officiel (l_max_duty) : editorScale 100, pas en unite affichee
                  "            <editAsPercentage>0</editAsPercentage>",
                  "            <maxDouble>%s</maxDouble>" % fmt(p["hi"]), "            <minDouble>%s</minDouble>" % fmt(p["lo"]),
                  "            <showDisplay>0</showDisplay>",
                  "            <stepDouble>%s</stepDouble>" % fmt(p["step"] * 100 if p["pct"] else p["step"]),
                  "            <valDouble>%s</valDouble>" % fmt(p["dflt"]),
                  "            <vTxDoubleScale>1000</vTxDoubleScale>",
                  "            <suffix>%s</suffix>" % esc(p["suffix"]), "            <vTx>9</vTx>"]
        o.append("        </%s>" % n)
    o += ["    </Params>", "    <SerOrder>"]
    o += ["        <ser>%s</ser>" % p["name"] for p in PARAMS]
    o += ["    </SerOrder>", "    <Grouping>"]
    for g, subs in GROUPING:
        o += ["        <group>", "            <groupName>%s</groupName>" % esc(g)]
        for s, items in subs:
            o += ["            <subgroup>", "                <subgroupName>%s</subgroupName>" % esc(s),
                  "                <subgroupParams>"]
            o += ["                    <param>%s</param>" % esc(i) for i in items]
            o += ["                </subgroupParams>", "            </subgroup>"]
        o.append("        </group>")
    o += ["    </Grouping>", "</ConfigParams>", ""]
    return "\n".join(o)


def signature():
    s = ""
    for p in PARAMS:
        s += p["name"] + str(xtype(p)) + ("9" if p["kind"] == "float" else "0") + "".join(p.get("enums", []))
    return crc32c(s.encode("utf-8"))


def main():
    names = [p["name"] for p in PARAMS]
    for g, subs in GROUPING:
        for s, items in subs:
            for i in items:
                assert i.startswith("::sep::") or i in names, i
    xml = build_xml()
    open(os.path.join(HERE, "rfp_conf.xml"), "w", encoding="utf-8", newline="\n").write(xml)
    raw = xml.encode("utf-8")
    z = len(raw).to_bytes(4, "big") + zlib.compress(raw, 9)       # qCompress
    sig = signature()
    ser_size = 4 + sum(4 if p["kind"] == "float" else 1 for p in PARAMS)

    h = ["// GENERE par rfp/gen_rfp_conf.py - ne pas editer.", "#ifndef RFP_CONF_GEN_H_", "#define RFP_CONF_GEN_H_", "",
         "#include <stdint.h>", "#include <stdbool.h>", ""]
    for p in PARAMS:
        if p["kind"] == "enum":
            h.append("// %s : %s" % (p["name"], ", ".join("%d = %s" % (i, e) for i, e in enumerate(p["enums"]))))
    h += ["", "typedef struct {"]
    for p in PARAMS:
        h.append("\t%s %s;" % ({"bool": "bool", "enum": "uint8_t", "float": "float"}[p["kind"]], p["name"]))
    h += ["} rfp_config;", "", "#define RFP_CONFIG_SIGNATURE\t%du" % sig, "#define RFP_CONF_SER_SIZE\t%d" % ser_size,
          "#define RFP_CONF_XML_SIZE\t%d" % len(z), "",
          "int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf);",
          "bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf);",
          "void rfp_conf_set_defaults(rfp_config *conf);", "extern const uint8_t rfp_conf_xml_data[];", "",
          "#endif", ""]

    c = ["// GENERE par rfp/gen_rfp_conf.py - ne pas editer.", '#include "rfp_conf_gen.h"', '#include "buffer.h"', "",
         "int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf) {", "\tint32_t ind = 0;",
         "\tbuffer_append_uint32(buffer, RFP_CONFIG_SIGNATURE, &ind);"]
    for p in PARAMS:
        c.append("\tbuffer_append_float32_auto(buffer, conf->%s, &ind);" % p["name"] if p["kind"] == "float" else
                 "\tbuffer[ind++] = conf->%s;" % p["name"])
    c += ["\treturn ind;", "}", "", "bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf) {",
          "\tint32_t ind = 0;", "\tuint32_t signature = buffer_get_uint32(buffer, &ind);",
          "\tif (signature != RFP_CONFIG_SIGNATURE) {", "\t\treturn false;", "\t}", ""]
    for p in PARAMS:
        c.append("\tconf->%s = buffer_get_float32_auto(buffer, &ind);" % p["name"] if p["kind"] == "float" else
                 "\tconf->%s = buffer[ind++];" % p["name"])
    c += ["", "\treturn true;", "}", "", "void rfp_conf_set_defaults(rfp_config *conf) {"]
    for p in PARAMS:
        c.append("\tconf->%s = %s;" % (p["name"], cfloat(p["dflt"]) if p["kind"] == "float" else str(int(p["dflt"]))))
    c += ["}", "", "const uint8_t rfp_conf_xml_data[%d] = {" % len(z)]
    for i in range(0, len(z), 16):
        c.append("\t" + ", ".join("0x%02x" % b for b in z[i:i + 16]) + ",")
    c += ["};", ""]
    open(os.path.join(HERE, "rfp_conf_gen.h"), "w", encoding="utf-8", newline="\n").write("\n".join(h))
    open(os.path.join(HERE, "rfp_conf_gen.c"), "w", encoding="utf-8", newline="\n").write("\n".join(c))
    print("signature %u, %d parametres, serialisation %d octets, XML %d octets -> %d" % (
        sig, len(PARAMS), ser_size, len(raw), len(z)))


if __name__ == "__main__":
    main()
