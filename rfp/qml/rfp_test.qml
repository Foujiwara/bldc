/*
    RFP Test page (VESC Tool): sliders that send commands like a throttle, 50 times per second:
      - Current Rel: relative current request (SET_CURRENT_REL), the path of the ADC / PPM
        current control types. The RFP Extras functions (hybrid, braking / reverse, tractor) apply.
      - Duty: duty cycle request (SET_DUTY), the path of the duty control types. Basic Duty Mode
        applies.
      - ERPM: speed request (SET_RPM), the path of the PID speed control types.
    Only one command type is active at a time.

    This file is part of the VESC firmware (GPLv3).
*/

import QtQuick 2.7
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

import Vedder.vesc.commands 1.0

Item {
    id: mainItem
    anchors.fill: parent

    property Commands mCommands: VescIf.commands()
    // active command: 0 none, 1 current rel, 2 duty, 3 erpm
    property int mode: 0

    function setMode(m) {
        if (mode === m) {
            m = 0
        }
        mode = m
        currentSlider.value = 0.0
        dutySlider.value = 0.0
        rpmSlider.value = 0.0
        if (m === 0) {
            mCommands.setCurrentRel(0.0)
        }
    }

    function sendNow() {
        if (mode === 1) {
            mCommands.setCurrentRel(currentSlider.value)
        } else if (mode === 2) {
            mCommands.setDutyCycle(dutySlider.value * maxDuty.value / 100.0)
        } else if (mode === 3) {
            mCommands.setRpm(Math.round(rpmSlider.value * maxErpm.value))
        }
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: mainItem.width - 20
            x: 10
            spacing: 6

            Label {
                text: "RFP Test"
                font.bold: true
                font.pointSize: 14
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Each slider sends its command 50 times per second while enabled, like a throttle. " +
                      "Only one command type at a time. Keep the wheel free for the first tries."
            }

            CheckBox {
                id: springBack
                text: "Sliders back to neutral when released"
                checked: true
            }

            Button {
                Layout.fillWidth: true
                text: "STOP (release)"
                onClicked: setMode(0)
            }

            // ------------------------------------------------ current rel
            Label {
                text: "Current Rel (ADC / PPM current types, RFP Extras apply)"
                font.bold: true
            }
            Switch {
                text: "Enable Current Rel"
                checked: mode === 1
                onClicked: setMode(1)
            }
            Label { text: "Request: " + Math.round(currentSlider.value * 100) + " %" }
            Slider {
                id: currentSlider
                Layout.fillWidth: true
                from: -1.0; to: 1.0; value: 0.0; stepSize: 0.01
                enabled: mode === 1
                onPressedChanged: if (!pressed && springBack.checked) value = 0.0
            }

            // ------------------------------------------------ duty
            Label {
                text: "Duty (ADC / PPM duty types, Basic Duty Mode applies)"
                font.bold: true
            }
            Switch {
                text: "Enable Duty"
                checked: mode === 2
                onClicked: setMode(2)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Duty at full slider (%)" }
                SpinBox { id: maxDuty; from: 1; to: 95; value: 50; editable: true }
            }
            Label { text: "Duty: " + (dutySlider.value * maxDuty.value).toFixed(1) + " %" }
            Slider {
                id: dutySlider
                Layout.fillWidth: true
                from: -1.0; to: 1.0; value: 0.0; stepSize: 0.01
                enabled: mode === 2
                onPressedChanged: if (!pressed && springBack.checked) value = 0.0
            }

            // ------------------------------------------------ erpm
            Label {
                text: "ERPM (ADC / PPM PID speed types)"
                font.bold: true
            }
            Switch {
                text: "Enable ERPM"
                checked: mode === 3
                onClicked: setMode(3)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "ERPM at full slider" }
                SpinBox { id: maxErpm; from: 100; to: 100000; stepSize: 100; value: 5000; editable: true }
            }
            Label { text: "Speed: " + Math.round(rpmSlider.value * maxErpm.value) + " ERPM" }
            Slider {
                id: rpmSlider
                Layout.fillWidth: true
                from: -1.0; to: 1.0; value: 0.0; stepSize: 0.01
                enabled: mode === 3
                onPressedChanged: if (!pressed && springBack.checked) value = 0.0
            }

            // ------------------------------------------------ values
            Label {
                id: valuesLabel
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "ERPM: -   Motor current: -   Duty: -"
                font.pointSize: 12
            }
        }
    }

    Timer {
        interval: 20
        running: mode !== 0
        repeat: true
        onTriggered: sendNow()
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
