#!/usr/bin/env python3
"""
Genere la configuration personnalisee "RFP" (page de parametres de VESC Tool) :

  rfp/rfp_conf.xml      description ConfigParams (format de VESC Tool), pour memoire et pour VESC Tool
  rfp/rfp_conf_gen.h    structure rfp_config, signature, prototypes
  rfp/rfp_conf_gen.c    serialisation / deserialisation / defauts, XML compresse (qCompress)

Le format est celui des configurations personnalisees de VESC (voir
lispBM/c_libs/examples/config/conf) : signature = crc32c(pour chaque <ser> : nom + type + vTx +
noms d'enum), puis les champs dans l'ordre de <SerOrder>. L'algorithme de signature est verifie
contre l'exemple officiel (BALANCE_CONFIG_SIGNATURE = 32903057).

Pour ajouter un parametre : l'ajouter dans PARAMS (et dans la serialisation de la memoire dans
rfp_ext.c si besoin), puis : python3 rfp/gen_rfp_conf.py
"""
import os
import zlib
import xml.sax.saxutils as su

HERE = os.path.dirname(os.path.abspath(__file__))

CONFIG_NAME = "rfp_config"
PAGE_TITLE = "RFP Extras"

# (nom, type, titre, description, defaut, min, max, pas, decimales, suffixe)
# type : "bool" ou "float". Les booleens ont min/max/pas a None.
PARAMS = [
    ("basic_duty_enable", "bool", "Basic Duty Mode",
     "Duty control: when the requested duty drops below the present duty, the motor is released: no braking "
     "and no acceleration (zero current, the bridge stays on) until the duty falls to the request, then it "
     "follows it again. Example: at 100 % the request goes down to 50 %, the motor coasts down to 50 % duty "
     "and takes over. Off: the Duty Downramp regulator brakes to follow a falling request. A request of zero "
     "always releases the motor.",
     0, None, None, None, None, ""),
    ("hybrid_enable", "bool", "Hybrid Current / Duty Control",
     "For every input that sends a relative current (ADC, PPM, VESC Remote, PAS, CAN): the duty cycle is "
     "limited proportionally to the throttle, so half throttle gives half torque and, without load, about half "
     "speed. In current control alone, a few percent of throttle bring an unloaded motor to full speed. "
     "Duty limit = throttle x Duty Limit at Full Throttle x Maximum Duty Cycle. The current is reduced linearly "
     "over the Current Taper Band below the limit and reaches zero at the limit, without a sudden cut. "
     "Braking is not affected.",
     0, None, None, None, None, ""),
    ("hybrid_duty_limit", "float", "Duty Limit at Full Throttle",
     "Duty cycle limit at full throttle, as a fraction of Maximum Duty Cycle. Lower it to cap the top speed.",
     1.0, 0.05, 1.0, 0.01, 2, ""),
    ("hybrid_band", "float", "Current Taper Band",
     "Part of the duty limit over which the current decreases to zero. Wider: softer approach of the limit, the "
     "speed settles a little lower under load. Narrower: firmer, the current is held closer to the limit.",
     0.15, 0.02, 0.6, 0.01, 2, ""),
    ("smart_rev_enable", "bool", "Smart Reverse",
     "For every input that sends a relative current (ADC, PPM, VESC Remote, PAS, CAN): a negative request "
     "(reverse) while rolling forward faster than Smart Reverse Brake Above ERPM brakes, with a current "
     "proportional to the request. Below that speed the motor reverses at a limited duty cycle, reached with "
     "a ramp. Inputs that already have their own smart reverse (PPM, VESC Remote) keep it.",
     0, None, None, None, None, ""),
    ("smart_rev_erpm", "float", "Smart Reverse Brake Above ERPM",
     "Forward speed above which a reverse request brakes. Below it, the motor reverses.",
     1000.0, 0.0, 30000.0, 100.0, 0, " ERPM"),
    ("smart_rev_max_duty", "float", "Smart Reverse Max Duty Cycle",
     "Duty cycle in reverse at full request.",
     0.07, 0.0, 1.0, 0.01, 2, ""),
    ("smart_rev_ramp", "float", "Smart Reverse Ramp Time",
     "Time to go from 0 to Smart Reverse Max Duty Cycle.",
     3.0, 0.01, 20.0, 0.1, 2, " s"),
]

# page : groupe -> sous-onglets -> elements (nom de parametre ou ::sep::Titre). VESC Tool (bureau et
# mobile) ne construit la page qu'a partir du groupe nomme exactement "General".
GROUPING = [
    ("General", [
        ("Duty", ["::sep::Basic Duty Mode", "basic_duty_enable"]),
        ("Current", ["::sep::Hybrid Current / Duty", "hybrid_enable", "hybrid_duty_limit", "hybrid_band",
                     "::sep::Smart Reverse", "smart_rev_enable", "smart_rev_erpm", "smart_rev_max_duty",
                     "smart_rev_ramp"]),
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
    for n, t, title, d, dflt, lo, hi, step, dec, suf in PARAMS:
        o.append("        <%s>" % n)
        o.append("            <longName>%s</longName>" % esc(title))
        o.append("            <type>%d</type>" % (5 if t == "bool" else 1))
        o.append("            <transmittable>1</transmittable>")
        o.append(desc(d))
        o.append("            <cDefine>RFP_%s</cDefine>" % n.upper())
        if t == "bool":
            o.append("            <valInt>%d</valInt>" % int(dflt))
        else:
            o += ["            <editorDecimalsDouble>%d</editorDecimalsDouble>" % dec,
                  "            <editorScale>1</editorScale>", "            <editAsPercentage>0</editAsPercentage>",
                  "            <maxDouble>%s</maxDouble>" % fmt(hi), "            <minDouble>%s</minDouble>" % fmt(lo),
                  "            <showDisplay>0</showDisplay>", "            <stepDouble>%s</stepDouble>" % fmt(step),
                  "            <valDouble>%s</valDouble>" % fmt(dflt),
                  "            <vTxDoubleScale>1000</vTxDoubleScale>",
                  "            <suffix>%s</suffix>" % esc(suf), "            <vTx>9</vTx>"]
        o.append("        </%s>" % n)
    o += ["    </Params>", "    <SerOrder>"]
    o += ["        <ser>%s</ser>" % p[0] for p in PARAMS]
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
    s = "".join(p[0] + str(5 if p[1] == "bool" else 1) + ("0" if p[1] == "bool" else "9") for p in PARAMS)
    return crc32c(s.encode("utf-8"))


def main():
    xml = build_xml()
    open(os.path.join(HERE, "rfp_conf.xml"), "w", encoding="utf-8", newline="\n").write(xml)
    raw = xml.encode("utf-8")
    z = len(raw).to_bytes(4, "big") + zlib.compress(raw, 9)       # qCompress
    sig = signature()

    h = ["// GENERE par rfp/gen_rfp_conf.py - ne pas editer.", "#ifndef RFP_CONF_GEN_H_", "#define RFP_CONF_GEN_H_", "",
         "#include <stdint.h>", "#include <stdbool.h>", "", "typedef struct {"]
    for n, t, *_ in PARAMS:
        h.append("\t%s %s;" % ("bool" if t == "bool" else "float", n))
    h += ["} rfp_config;", "", "#define RFP_CONFIG_SIGNATURE\t%du" % sig, "#define RFP_CONF_XML_SIZE\t%d" % len(z), "",
          "int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf);",
          "bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf);",
          "void rfp_conf_set_defaults(rfp_config *conf);", "extern const uint8_t rfp_conf_xml_data[];", "",
          "#endif", ""]

    c = ["// GENERE par rfp/gen_rfp_conf.py - ne pas editer.", '#include "rfp_conf_gen.h"', '#include "buffer.h"', "",
         "int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf) {", "\tint32_t ind = 0;",
         "\tbuffer_append_uint32(buffer, RFP_CONFIG_SIGNATURE, &ind);"]
    for n, t, *_ in PARAMS:
        c.append("\tbuffer[ind++] = conf->%s;" % n if t == "bool" else
                 "\tbuffer_append_float32_auto(buffer, conf->%s, &ind);" % n)
    c += ["\treturn ind;", "}", "", "bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf) {",
          "\tint32_t ind = 0;", "\tuint32_t signature = buffer_get_uint32(buffer, &ind);",
          "\tif (signature != RFP_CONFIG_SIGNATURE) {", "\t\treturn false;", "\t}", ""]
    for n, t, *_ in PARAMS:
        c.append("\tconf->%s = buffer[ind++];" % n if t == "bool" else
                 "\tconf->%s = buffer_get_float32_auto(buffer, &ind);" % n)
    c += ["", "\treturn true;", "}", "", "void rfp_conf_set_defaults(rfp_config *conf) {"]
    for n, t, title, d, dflt, *_ in PARAMS:
        c.append("\tconf->%s = %s;" % (n, str(int(dflt)) if t == "bool" else cfloat(dflt)))
    c += ["}", "", "const uint8_t rfp_conf_xml_data[%d] = {" % len(z)]
    for i in range(0, len(z), 16):
        c.append("\t" + ", ".join("0x%02x" % b for b in z[i:i + 16]) + ",")
    c += ["};", ""]
    open(os.path.join(HERE, "rfp_conf_gen.h"), "w", encoding="utf-8", newline="\n").write("\n".join(h))
    open(os.path.join(HERE, "rfp_conf_gen.c"), "w", encoding="utf-8", newline="\n").write("\n".join(c))
    print("signature %u, XML %d octets -> %d, %d parametres" % (sig, len(raw), len(z), len(PARAMS)))


if __name__ == "__main__":
    main()
