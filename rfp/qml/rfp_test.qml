/*
    RFP Test page (VESC Tool): a slider that sends a relative current request
    (SET_CURRENT_REL), like a throttle: positive, neutral, negative. It goes through the
    same path as the apps (ADC, PPM...), so the RFP Extras functions (hybrid, braking,
    reverse, tractor) apply.

    This file is part of the VESC firmware (GPLv3).
*/

import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3

import Vedder.vesc.commands 1.0

Item {
    id: mainItem
    anchors.fill: parent

    property Commands mCommands: VescIf.commands()

    function neutral() {
        throttle.value = 0.0
        mCommands.setCurrentRel(0.0)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Label {
            text: "RFP Test Throttle"
            font.bold: true
            font.pointSize: 14
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Sends a relative current request (as an ADC or PPM throttle) 50 times per second " +
                  "while enabled. The RFP Extras settings apply. Keep the wheel free for the first tries."
        }

        Switch {
            id: enable
            text: "Enable"
            checked: false
            onCheckedChanged: {
                if (!checked) {
                    neutral()
                }
            }
        }

        Label {
            text: "Request: " + Math.round(throttle.value * 100) + " %"
            font.pointSize: 13
        }

        Slider {
            id: throttle
            Layout.fillWidth: true
            from: -1.0
            to: 1.0
            value: 0.0
            stepSize: 0.01
            enabled: enable.checked
            onPressedChanged: {
                if (!pressed && springBack.checked) {
                    value = 0.0
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                Layout.fillWidth: true
                text: "-100 %"
                enabled: enable.checked
                onClicked: throttle.value = -1.0
            }
            Button {
                Layout.fillWidth: true
                text: "Neutral"
                onClicked: neutral()
            }
            Button {
                Layout.fillWidth: true
                text: "+100 %"
                enabled: enable.checked
                onClicked: throttle.value = 1.0
            }
        }

        CheckBox {
            id: springBack
            text: "Back to neutral when released"
            checked: true
        }

        Label {
            id: valuesLabel
            Layout.fillWidth: true
            text: "ERPM: -   Motor current: -   Duty: -"
            font.pointSize: 12
        }

        Item {
            Layout.fillHeight: true
        }
    }

    Timer {
        interval: 20
        running: enable.checked
        repeat: true
        onTriggered: mCommands.setCurrentRel(throttle.value)
    }

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: mCommands.getValues()
    }

    Connections {
        target: mCommands

        onValuesReceived: {
            valuesLabel.text = "ERPM: " + Math.round(values.rpm) +
                    "   Motor current: " + values.current_motor.toFixed(1) + " A" +
                    "   Duty: " + (values.duty_now * 100).toFixed(1) + " %"
        }
    }
}
