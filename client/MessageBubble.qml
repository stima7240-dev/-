import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A single chat message bubble. Outgoing messages (mine == true) align right in
// a lighter grey; incoming ones align left in a darker grey. Monochrome theme,
// no blue. The bubble grows with its text up to a fraction of the list width
// and wraps.

Item {
    id: bubble
    implicitHeight: shape.height + 6

    // Model roles (auto-bound).
    required property string text
    required property bool mine
    required property string time

    readonly property real maxBubbleWidth: Math.min(440, width * 0.72)

    Rectangle {
        id: shape
        anchors.right: bubble.mine ? parent.right : undefined
        anchors.left: bubble.mine ? undefined : parent.left
        anchors.rightMargin: 12
        anchors.leftMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 3

        width: content.implicitWidth + 24
        height: content.implicitHeight + 16
        radius: 14
        color: bubble.mine ? "#2c2c31" : "#19191c"

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 12
            anchors.topMargin: 8
            width: Math.min(implicitWidth, bubble.maxBubbleWidth)
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: bubble.text
                color: "#ececec"
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            Label {
                Layout.alignment: Qt.AlignRight
                text: bubble.time
                color: "#8b8b91"
                font.pixelSize: 11
            }
        }
    }
}
