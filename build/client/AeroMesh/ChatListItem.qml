import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One row in the chat list: round avatar with an initial, name, last-message
// preview, time, and an unread badge. Monochrome black-grey styling; selection
// and hover use grey tones (no blue). Clicking a row makes it the ListView's
// current item, which drives the conversation pane in Main.qml.

Item {
    id: row
    height: 70

    // Model roles (auto-bound).
    required property int index
    required property string name
    required property string last
    required property string time
    required property int unread
    required property string avatarColor

    readonly property bool selected: ListView.isCurrentItem

    Rectangle {
        anchors.fill: parent
        color: row.selected ? "#2a2a2e"
                            : (hover.hovered ? "#1d1d20" : "transparent")

        HoverHandler { id: hover }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 50
                height: 50
                radius: 25
                color: row.avatarColor
                Label {
                    anchors.centerIn: parent
                    text: row.name.length > 0 ? row.name.charAt(0).toUpperCase() : "?"
                    color: "#ffffff"
                    font.pixelSize: 19
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: row.name
                        color: "#ececec"
                        font.pixelSize: 15
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: row.time
                        color: "#8b8b91"
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: row.last
                        color: "#8b8b91"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: row.unread > 0
                        radius: 11
                        height: 22
                        width: Math.max(22, badge.implicitWidth + 12)
                        color: "#55555c"
                        Label {
                            id: badge
                            anchors.centerIn: parent
                            text: row.unread
                            color: "#ffffff"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: row.ListView.view.currentIndex = row.index
    }
}
