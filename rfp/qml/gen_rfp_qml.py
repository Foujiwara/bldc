#!/usr/bin/env python3
"""
Genere rfp/rfp_qml_hw.c / .h (page QML "RFP Test" envoyee a VESC Tool) a partir de
rfp/qml/rfp_test.qml, au format d'export de VESC Tool : prefixe d'imports + qCompress
(u32 taille gros-boutiste + zlib). Usage : python3 rfp/qml/gen_rfp_qml.py
"""
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

PREFIX = 'import Vedder.vesc.vescinterface 1.0;import "qrc:/mobile";'

qml = open(os.path.join(HERE, "rfp_test.qml"), encoding="utf-8").read()
raw = (PREFIX + qml).encode("utf-8")
z = len(raw).to_bytes(4, "big") + zlib.compress(raw, 9)

h = ["// GENERE par rfp/qml/gen_rfp_qml.py depuis rfp/qml/rfp_test.qml - ne pas editer.",
     "#ifndef RFP_QML_HW_H_", "#define RFP_QML_HW_H_", "", "#include <stdint.h>", "",
     "#define DATA_QML_HW_SIZE\t\t%d" % len(z), "", "extern uint8_t data_qml_hw[];", "", "#endif", ""]
c = ["// GENERE par rfp/qml/gen_rfp_qml.py depuis rfp/qml/rfp_test.qml - ne pas editer.", "",
     '#include "rfp_qml_hw.h"', "", "uint8_t data_qml_hw[%d] = {" % len(z)]
for i in range(0, len(z), 16):
    c.append("\t" + ", ".join("0x%02x" % b for b in z[i:i + 16]) + ",")
c += ["};", ""]
open(os.path.join(OUT, "rfp_qml_hw.h"), "w", encoding="utf-8", newline="\n").write("\n".join(h))
open(os.path.join(OUT, "rfp_qml_hw.c"), "w", encoding="utf-8", newline="\n").write("\n".join(c))
print("QML %d octets -> %d" % (len(raw), len(z)))
