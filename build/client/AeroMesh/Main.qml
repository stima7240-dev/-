import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// AeroMesh desktop client shell. Frameless window with custom window controls,
// monochrome black-grey theme, no emoji (all icons are drawn vector shapes).
// Telegram-style left drawer: profile header, account switching, account
// creation, and a menu with drawn icons. Data is still sample data.

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: "AeroMesh"
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    property bool maximized: false
    function toggleMaximized() {
        if (maximized) {
            root.showNormal()
            maximized = false
        } else {
            root.showMaximized()
            maximized = true
        }
    }

    // ---- Account state ----
    ListModel { id: accountsModel }
    property int currentAccount: 0
    property var activeAccount: (currentAccount >= 0 && currentAccount < accountsModel.count)
                                ? accountsModel.get(currentAccount) : null
    property bool accountsExpanded: false

    function addAccount(rawName) {
        var name = (rawName || "").trim()
        if (name.length === 0)
            return
        var colors = ["#5a5a61", "#47474d", "#6a6a72", "#3f3f45", "#54545b", "#605863"]
        accountsModel.append({
            name: name,
            handle: "@" + name.toLowerCase().replace(/[^a-z0-9]+/g, ""),
            avatarColor: colors[accountsModel.count % colors.length],
            bio: "",
            birthday: ""
        })
        currentAccount = accountsModel.count - 1
    }

    // ---- Monochrome black-grey palette ----
    QtObject {
        id: theme
        readonly property color windowBg: "#0d0d0f"
        readonly property color sidebarBg: "#161617"
        readonly property color chatBg: "#0e0e0f"
        readonly property color headerBg: "#161617"
        readonly property color accent: "#3d3d42"
        readonly property color accentHover: "#4c4c52"
        readonly property color text: "#ececec"
        readonly property color subtext: "#8b8b91"
        readonly property color field: "#1f1f21"
        readonly property color rowHover: "#1d1d20"
        readonly property color rowSelected: "#2a2a2e"
        readonly property color closeHover: "#c23b3b"
    }

    // ---- Drawn vector icons (no emoji). Selected by `kind`. ----
    component Glyph: Canvas {
        property string kind: ""
        property color glyphColor: "#aebccb"
        width: 22
        height: 22
        onKindChanged: requestPaint()
        onGlyphColorChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = glyphColor
            ctx.fillStyle = glyphColor
            ctx.lineWidth = 1.6
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            if (kind === "person" || kind === "contacts") {
                ctx.beginPath(); ctx.arc(11, 7.5, 3.6, 0, 2 * Math.PI); ctx.stroke()
                ctx.beginPath(); ctx.arc(11, 20, 7.5, Math.PI * 1.18, Math.PI * 1.82); ctx.stroke()
            } else if (kind === "wallet") {
                ctx.beginPath(); ctx.rect(3, 6, 16, 12); ctx.stroke()
                ctx.beginPath(); ctx.arc(15.5, 12, 1.5, 0, 2 * Math.PI); ctx.fill()
            } else if (kind === "group") {
                ctx.beginPath(); ctx.arc(7.4, 8, 2.7, 0, 2 * Math.PI); ctx.stroke()
                ctx.beginPath(); ctx.arc(7.4, 17.6, 4.3, Math.PI, 2 * Math.PI); ctx.stroke()
                ctx.beginPath(); ctx.arc(14.6, 8, 2.7, 0, 2 * Math.PI); ctx.stroke()
                ctx.beginPath(); ctx.arc(14.6, 17.6, 4.3, Math.PI, 2 * Math.PI); ctx.stroke()
            } else if (kind === "channel") {
                ctx.beginPath(); ctx.moveTo(4, 9); ctx.lineTo(11, 9); ctx.lineTo(17, 5); ctx.lineTo(17, 17); ctx.lineTo(11, 13); ctx.lineTo(4, 13); ctx.closePath(); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(7, 13); ctx.lineTo(8, 18); ctx.stroke()
            } else if (kind === "phone") {
                ctx.beginPath()
                ctx.moveTo(6, 4); ctx.lineTo(9, 4); ctx.lineTo(11, 9); ctx.lineTo(8.5, 11)
                ctx.bezierCurveTo(9.5, 14, 11, 15.5, 14, 16.5); ctx.lineTo(16, 14); ctx.lineTo(20, 16); ctx.lineTo(20, 19)
                ctx.bezierCurveTo(12, 19.5, 4.5, 12, 6, 4)
                ctx.stroke()
            } else if (kind === "bookmark") {
                ctx.beginPath(); ctx.moveTo(6, 3.5); ctx.lineTo(16, 3.5); ctx.lineTo(16, 19); ctx.lineTo(11, 15); ctx.lineTo(6, 19); ctx.closePath(); ctx.stroke()
            } else if (kind === "gear") {
                ctx.lineCap = "butt"
                ctx.lineWidth = 2.6
                for (var gi = 0; gi < 8; gi++) {
                    var ga = gi * Math.PI / 4
                    ctx.beginPath()
                    ctx.moveTo(11 + Math.cos(ga) * 6.2, 11 + Math.sin(ga) * 6.2)
                    ctx.lineTo(11 + Math.cos(ga) * 8.7, 11 + Math.sin(ga) * 8.7)
                    ctx.stroke()
                }
                ctx.lineWidth = 1.6
                ctx.lineCap = "round"
                ctx.beginPath(); ctx.arc(11, 11, 6.2, 0, 2 * Math.PI); ctx.stroke()
                ctx.beginPath(); ctx.arc(11, 11, 2.7, 0, 2 * Math.PI); ctx.stroke()
            } else if (kind === "plus") {
                ctx.beginPath(); ctx.moveTo(11, 5); ctx.lineTo(11, 17); ctx.moveTo(5, 11); ctx.lineTo(17, 11); ctx.stroke()
            } else if (kind === "check") {
                ctx.beginPath(); ctx.moveTo(5, 11.5); ctx.lineTo(9, 15.5); ctx.lineTo(17, 6.5); ctx.stroke()
            } else if (kind === "chevron") {
                ctx.beginPath(); ctx.moveTo(6, 9); ctx.lineTo(11, 14); ctx.lineTo(16, 9); ctx.stroke()
            } else if (kind === "qr") {
                ctx.lineWidth = 1.5
                ctx.strokeRect(3.5, 3.5, 5.5, 5.5)
                ctx.strokeRect(13, 3.5, 5.5, 5.5)
                ctx.strokeRect(3.5, 13, 5.5, 5.5)
                ctx.fillRect(13, 13, 2.2, 2.2)
                ctx.fillRect(16.3, 13, 2.2, 2.2)
                ctx.fillRect(13, 16.3, 2.2, 2.2)
                ctx.fillRect(16.3, 16.3, 2.2, 2.2)
            } else if (kind === "pencil") {
                ctx.beginPath()
                ctx.moveTo(4.5, 17.5)
                ctx.lineTo(6, 13.5)
                ctx.lineTo(14, 5.5)
                ctx.lineTo(17.5, 9)
                ctx.lineTo(9.5, 17)
                ctx.closePath()
                ctx.stroke()
                ctx.beginPath(); ctx.moveTo(4.5, 17.5); ctx.lineTo(7.5, 16); ctx.stroke()
            }
        }
    }

    // ---- Reusable side-menu row (icon + label). No outer-id refs (inline component). ----
    component MenuRow: Rectangle {
        id: menuRow
        property string glyphKind: ""
        property string label: ""
        signal activated()
        Layout.fillWidth: true
        Layout.preferredHeight: 50
        color: menuRowHover.hovered ? "#1d1d20" : "transparent"
        HoverHandler { id: menuRowHover }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 16
            spacing: 18
            Glyph {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                kind: menuRow.glyphKind
                glyphColor: "#aebccb"
            }
            Label {
                Layout.fillWidth: true
                text: menuRow.label
                color: "#ececec"
                font.pixelSize: 15
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: menuRow.activated()
        }
    }

    // ---- Reusable settings toggle row ----
    component SettingRow: Rectangle {
        id: settingRow
        property string label: ""
        property bool active: false
        signal toggled()
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            Label { Layout.fillWidth: true; text: settingRow.label; color: "#ececec"; font.pixelSize: 15 }
            Rectangle {
                width: 44; height: 26; radius: 13
                color: settingRow.active ? "#4c4c52" : "#2a2a2e"
                Behavior on color { ColorAnimation { duration: 120 } }
                Rectangle {
                    width: 20; height: 20; radius: 10
                    color: "#ececec"
                    y: 3
                    x: settingRow.active ? 21 : 3
                    Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.InOutQuad } }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: settingRow.toggled() }
            }
        }
    }

    // ---- Chat state ----
    property var activeChat: (chatList.currentIndex >= 0 && chatList.currentIndex < chatsModel.count)
                             ? chatsModel.get(chatList.currentIndex) : null

    ListModel { id: chatsModel }
    ListModel { id: messagesModel }
    ListModel { id: contactsModel }
    ListModel { id: callsModel }
    property var sampleMessages: ({})

    // ---- Settings state ----
    property bool notificationsEnabled: true
    property bool soundEnabled: true
    property bool hidePreview: false

    function loadMessages(idx) {
        messagesModel.clear()
        var arr = root.sampleMessages[idx]
        if (arr === undefined)
            return
        for (var i = 0; i < arr.length; ++i)
            messagesModel.append(arr[i])
    }

    function sendMessage() {
        var t = msgInput.text.trim()
        if (t.length === 0)
            return
        messagesModel.append({ text: t, mine: true, time: "\u0441\u0435\u0439\u0447\u0430\u0441" })
        msgInput.text = ""
        msgList.positionViewAtEnd()
    }

    function randomAvatarColor() {
        var colors = ["#5a5a61", "#47474d", "#6a6a72", "#3f3f45", "#54545b", "#605863"]
        return colors[Math.floor(Math.random() * colors.length)]
    }

    function createChat(chatName, lastText) {
        var nm = (chatName || "").trim()
        if (nm.length === 0)
            return
        chatsModel.append({ name: nm, last: lastText, time: "\u0441\u0435\u0439\u0447\u0430\u0441", unread: 0, avatarColor: root.randomAvatarColor() })
        chatList.currentIndex = chatsModel.count - 1
    }

    function openSaved() {
        for (var i = 0; i < chatsModel.count; ++i) {
            if (chatsModel.get(i).name === "\u0421\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u044b\u0435") {
                chatList.currentIndex = i
                return
            }
        }
    }

    Component.onCompleted: {
        addAccount("Timoxa")
        addAccount("Artem")
        currentAccount = 0
        accountsModel.setProperty(0, "bio", "Создаю анонимный мессенджер AeroMesh")
        accountsModel.setProperty(0, "birthday", "9 окт 2012")

        chatsModel.append({ name: "Alice", last: "\u041f\u0440\u0438\u0432\u0435\u0442! \u041a\u0430\u043a \u0434\u0435\u043b\u0430?", time: "10:24", unread: 0, avatarColor: "#5a5a61" })
        chatsModel.append({ name: "\u041a\u043e\u043c\u0430\u043d\u0434\u0430 AeroMesh", last: "\u0414\u0435\u043d\u0438\u0441: \u0441\u0431\u043e\u0440 \u0432 18:00", time: "9:58", unread: 3, avatarColor: "#47474d" })
        chatsModel.append({ name: "\u0411\u043e\u0431", last: "\u0421\u043a\u0438\u043d\u0443\u043b \u0444\u0430\u0439\u043b \u043f\u0440\u043e\u0435\u043a\u0442\u0430", time: "\u0412\u0447\u0435\u0440\u0430", unread: 0, avatarColor: "#6a6a72" })
        chatsModel.append({ name: "\u0421\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u044b\u0435", last: "\u0417\u0430\u043c\u0435\u0442\u043a\u0430 \u0434\u043b\u044f \u0441\u0435\u0431\u044f", time: "\u041f\u043d", unread: 0, avatarColor: "#3f3f45" })

        var data = ({})
        data[0] = [ { text: "\u041f\u0440\u0438\u0432\u0435\u0442! \u041a\u0430\u043a \u0434\u0435\u043b\u0430?", mine: false, time: "10:24" }, { text: "\u0412\u0441\u0451 \u043e\u0442\u043b\u0438\u0447\u043d\u043e, \u0437\u0430\u043f\u0443\u0441\u043a\u0430\u044e \u043d\u0430\u0448 \u043c\u0435\u0441\u0441\u0435\u043d\u0434\u0436\u0435\u0440", mine: true, time: "10:25" }, { text: "\u0417\u0432\u0443\u0447\u0438\u0442 \u043a\u0440\u0443\u0442\u043e", mine: false, time: "10:26" } ]
        data[1] = [ { text: "\u0421\u0431\u043e\u0440 \u0432 18:00", mine: false, time: "9:58" }, { text: "\u041f\u0440\u0438\u043d\u044f\u043b, \u0431\u0443\u0434\u0443", mine: true, time: "9:59" } ]
        data[2] = [ { text: "\u0421\u043a\u0438\u043d\u0443\u043b \u0444\u0430\u0439\u043b \u043f\u0440\u043e\u0435\u043a\u0442\u0430", mine: false, time: "\u0412\u0447\u0435\u0440\u0430" } ]
        data[3] = [ { text: "\u041f\u0440\u043e\u0432\u0435\u0440\u0438\u0442\u044c \u0441\u0431\u043e\u0440\u043a\u0443", mine: true, time: "\u041f\u043d" } ]
        root.sampleMessages = data

        contactsModel.append({ name: "Alice", handle: "@alice" })
        contactsModel.append({ name: "Боб", handle: "@bob" })
        contactsModel.append({ name: "Денис", handle: "@denis" })

        callsModel.append({ name: "Alice", kind: "входящий", time: "10:24" })
        callsModel.append({ name: "Боб", kind: "исходящий", time: "Вчера" })

        loadMessages(0)
    }

    // ---------- Telegram-style side menu ----------
    Drawer {
        id: menuDrawer
        width: 308
        height: root.height
        edge: Qt.LeftEdge
        background: Rectangle { color: theme.sidebarBg; topLeftRadius: 12; bottomLeftRadius: 12 }
        onClosed: root.accountsExpanded = false

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // profile header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 138
                color: "#1b1b1d"
                topLeftRadius: 12
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    anchors.topMargin: 16
                    anchors.bottomMargin: 14
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            width: 58; height: 58; radius: 29
                            color: root.activeAccount ? root.activeAccount.avatarColor : theme.accent
                            Label {
                                anchors.centerIn: parent
                                text: root.activeAccount ? root.activeAccount.name.charAt(0).toUpperCase() : "?"
                                color: "#ffffff"
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 34; height: 34; radius: 17
                            color: chevHover.hovered ? theme.rowSelected : "transparent"
                            HoverHandler { id: chevHover }
                            Glyph {
                                anchors.centerIn: parent
                                kind: "chevron"
                                glyphColor: theme.subtext
                                rotation: root.accountsExpanded ? 180 : 0
                                Behavior on rotation { NumberAnimation { duration: 150 } }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.accountsExpanded = !root.accountsExpanded
                            }
                        }
                    }

                    Label {
                        text: root.activeAccount ? root.activeAccount.name : ""
                        color: theme.text
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Label {
                        text: "\u0420\u0435\u0434\u0430\u043a\u0442\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u043f\u0440\u043e\u0444\u0438\u043b\u044c"
                        color: theme.subtext
                        font.pixelSize: 13
                    }
                }
            }

            // ===== Accounts section (animated expand/collapse) =====
            Item {
                Layout.fillWidth: true
                clip: true
                Layout.preferredHeight: root.accountsExpanded ? accountsContent.implicitHeight : 0
                opacity: root.accountsExpanded ? 1 : 0
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }
                Behavior on opacity { NumberAnimation { duration: 160 } }
                ColumnLayout {
                    id: accountsContent
                    width: parent.width
                    spacing: 0

                Repeater {
                    model: accountsModel
                    delegate: Rectangle {
                        required property int index
                        required property string name
                        required property string handle
                        required property string avatarColor
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        color: accHover.hovered ? theme.rowHover : "transparent"
                        HoverHandler { id: accHover }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 14
                            Rectangle {
                                width: 40; height: 40; radius: 20
                                color: avatarColor
                                Label {
                                    anchors.centerIn: parent
                                    text: name.length > 0 ? name.charAt(0).toUpperCase() : "?"
                                    color: "#ffffff"
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label { Layout.fillWidth: true; text: name; color: theme.text; font.pixelSize: 15; font.bold: true }
                                Label { Layout.fillWidth: true; text: handle; color: theme.subtext; font.pixelSize: 12 }
                            }
                            Glyph {
                                visible: index === root.currentAccount
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                kind: "check"
                                glyphColor: theme.text
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentAccount = index
                                root.accountsExpanded = false
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#26262a" }

                MenuRow {
                    glyphKind: "plus"
                    label: "\u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u0430\u043a\u043a\u0430\u0443\u043d\u0442"
                    onActivated: addAccountDialog.open()
                }
                }
            }

            // ===== Main menu (always visible; slides down when accounts expand) =====
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                MenuRow { glyphKind: "person";   label: "\u041c\u043e\u0439 \u043f\u0440\u043e\u0444\u0438\u043b\u044c"; onActivated: { menuDrawer.close(); profileDialog.open() } }
                MenuRow { glyphKind: "wallet";   label: "\u041a\u043e\u0448\u0435\u043b\u0451\u043a"; onActivated: { menuDrawer.close(); walletDialog.open() } }
                MenuRow { glyphKind: "group";    label: "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u0433\u0440\u0443\u043f\u043f\u0443"; onActivated: { menuDrawer.close(); createChatDialog.openWith("group") } }
                MenuRow { glyphKind: "channel";  label: "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043a\u0430\u043d\u0430\u043b"; onActivated: { menuDrawer.close(); createChatDialog.openWith("channel") } }
                MenuRow { glyphKind: "contacts"; label: "\u041a\u043e\u043d\u0442\u0430\u043a\u0442\u044b"; onActivated: { menuDrawer.close(); contactsDialog.open() } }
                MenuRow { glyphKind: "phone";    label: "\u0417\u0432\u043e\u043d\u043a\u0438"; onActivated: { menuDrawer.close(); callsDialog.open() } }
                MenuRow { glyphKind: "bookmark"; label: "\u0418\u0437\u0431\u0440\u0430\u043d\u043d\u043e\u0435"; onActivated: { menuDrawer.close(); root.openSaved() } }
                MenuRow { glyphKind: "gear";     label: "\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438"; onActivated: { menuDrawer.close(); settingsDialog.open() } }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // ---------- Create-account dialog ----------
    Popup {
        id: addAccountDialog
        modal: true
        dim: true
        width: 340
        padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        onOpened: newAccountField.forceActiveFocus()

        function commit() {
            root.addAccount(newAccountField.text)
            newAccountField.text = ""
            addAccountDialog.close()
            root.accountsExpanded = false
        }

        contentItem: ColumnLayout {
            spacing: 0
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                text: "\u041d\u043e\u0432\u044b\u0439 \u0430\u043a\u043a\u0430\u0443\u043d\u0442"
                color: theme.text
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                text: "\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u0438\u043c\u044f \u043d\u043e\u0432\u043e\u0433\u043e \u043f\u0440\u043e\u0444\u0438\u043b\u044f"
                color: theme.subtext
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.preferredHeight: 44
                radius: 12
                color: theme.field
                TextField {
                    id: newAccountField
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "\u0418\u043c\u044f"
                    color: theme.text
                    placeholderTextColor: theme.subtext
                    font.pixelSize: 15
                    background: Rectangle { color: "transparent" }
                    onAccepted: addAccountDialog.commit()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                spacing: 10
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 38
                    radius: 10
                    color: cancelHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: cancelHover }
                    Label { anchors.centerIn: parent; text: "\u041e\u0442\u043c\u0435\u043d\u0430"; color: theme.text; font.pixelSize: 14 }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { newAccountField.text = ""; addAccountDialog.close() }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 116
                    Layout.preferredHeight: 38
                    radius: 10
                    color: createHover.hovered ? theme.accentHover : theme.accent
                    HoverHandler { id: createHover }
                    Label { anchors.centerIn: parent; text: "\u0421\u043e\u0437\u0434\u0430\u0442\u044c"; color: "#ffffff"; font.pixelSize: 14; font.bold: true }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: addAccountDialog.commit()
                    }
                }
            }
        }
    }

    // ---------- Profile screen ----------
    Popup {
        id: profileDialog
        property bool editMode: false
        modal: true; dim: true; width: 410; height: 668; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        function sync() {
            if (root.activeAccount) {
                profileNameField.text = root.activeAccount.name
                profileHandleField.text = root.activeAccount.handle
                profileBioField.text = root.activeAccount.bio
                profileBirthdayField.text = root.activeAccount.birthday
            }
        }
        function save() {
            if (root.currentAccount < 0)
                return
            var nm = profileNameField.text.trim()
            if (nm.length === 0)
                return
            accountsModel.setProperty(root.currentAccount, "name", nm)
            accountsModel.setProperty(root.currentAccount, "handle", profileHandleField.text.trim())
            accountsModel.setProperty(root.currentAccount, "bio", profileBioField.text.trim())
            accountsModel.setProperty(root.currentAccount, "birthday", profileBirthdayField.text.trim())
            var i = root.currentAccount
            root.currentAccount = -1
            root.currentAccount = i
            profileDialog.editMode = false
        }
        onOpened: { profileDialog.editMode = false; profileDialog.sync() }

        contentItem: ColumnLayout {
            spacing: 0

            // ===== Header: avatar, name, online status =====
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 236
                color: "#1b1b1d"
                topLeftRadius: 16
                topRightRadius: 16

                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 12
                    anchors.rightMargin: 12
                    spacing: 2
                    Rectangle {
                        visible: !profileDialog.editMode
                        width: 38; height: 38; radius: 19
                        color: profEditHover.hovered ? "#2a2a2e" : "transparent"
                        HoverHandler { id: profEditHover }
                        Glyph { anchors.centerIn: parent; kind: "pencil"; glyphColor: "#b3b3b9" }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { profileDialog.sync(); profileDialog.editMode = true } }
                    }
                    Rectangle {
                        width: 38; height: 38; radius: 19
                        color: profCloseHover.hovered ? "#2a2a2e" : "transparent"
                        HoverHandler { id: profCloseHover }
                        Item {
                            anchors.centerIn: parent; width: 14; height: 14
                            Rectangle { anchors.centerIn: parent; width: 16; height: 1.6; radius: 1; rotation: 45; color: "#b3b3b9" }
                            Rectangle { anchors.centerIn: parent; width: 16; height: 1.6; radius: 1; rotation: -45; color: "#b3b3b9" }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: profileDialog.close() }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 16
                        width: 96; height: 96; radius: 48
                        color: root.activeAccount ? root.activeAccount.avatarColor : theme.accent
                        Label {
                            anchors.centerIn: parent
                            text: root.activeAccount ? root.activeAccount.name.charAt(0).toUpperCase() : "?"
                            color: "#ffffff"; font.pixelSize: 40; font.bold: true
                        }
                    }

                    Label {
                        visible: !profileDialog.editMode
                        Layout.alignment: Qt.AlignHCenter
                        text: root.activeAccount ? root.activeAccount.name : ""
                        color: theme.text; font.pixelSize: 22; font.bold: true
                    }
                    Rectangle {
                        visible: profileDialog.editMode
                        Layout.alignment: Qt.AlignHCenter
                        width: 240; height: 40; radius: 10; color: theme.field
                        TextField {
                            id: profileNameField
                            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                            verticalAlignment: TextInput.AlignVCenter
                            horizontalAlignment: TextInput.AlignHCenter
                            placeholderText: "Имя"; color: theme.text; placeholderTextColor: theme.subtext
                            font.pixelSize: 16; background: Rectangle { color: "transparent" }
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        Rectangle {
                            width: 18; height: 18; radius: 4; color: theme.accent
                            Label { anchors.centerIn: parent; text: "1"; color: "#ffffff"; font.pixelSize: 11; font.bold: true }
                        }
                        Label { text: "в сети"; color: theme.subtext; font.pixelSize: 13 }
                    }
                }
            }

            // ===== О себе =====
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 18
                spacing: 5
                Label {
                    visible: !profileDialog.editMode
                    Layout.fillWidth: true
                    text: (root.activeAccount && root.activeAccount.bio.length > 0) ? root.activeAccount.bio : "—"
                    color: theme.text; font.pixelSize: 15; wrapMode: Text.Wrap
                }
                Rectangle {
                    visible: profileDialog.editMode
                    Layout.fillWidth: true; Layout.preferredHeight: 44; radius: 10; color: theme.field
                    TextField {
                        id: profileBioField
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Расскажите о себе"; color: theme.text; placeholderTextColor: theme.subtext
                        font.pixelSize: 15; background: Rectangle { color: "transparent" }
                    }
                }
                Label { Layout.fillWidth: true; text: "О себе"; color: theme.subtext; font.pixelSize: 13 }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14; Layout.preferredHeight: 1; color: "#26262a" }

            // ===== Имя пользователя =====
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        visible: !profileDialog.editMode
                        Layout.fillWidth: true
                        text: root.activeAccount ? root.activeAccount.handle : ""
                        color: "#aebccb"; font.pixelSize: 15
                    }
                    Rectangle {
                        visible: profileDialog.editMode
                        Layout.fillWidth: true; Layout.preferredHeight: 44; radius: 10; color: theme.field
                        TextField {
                            id: profileHandleField
                            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                            verticalAlignment: TextInput.AlignVCenter
                            placeholderText: "@username"; color: theme.text; placeholderTextColor: theme.subtext
                            font.pixelSize: 15; background: Rectangle { color: "transparent" }
                        }
                    }
                    Label { Layout.fillWidth: true; text: "Имя пользователя"; color: theme.subtext; font.pixelSize: 13 }
                }
                Glyph {
                    visible: !profileDialog.editMode
                    Layout.alignment: Qt.AlignVCenter
                    kind: "qr"; glyphColor: "#aebccb"
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14; Layout.preferredHeight: 1; color: "#26262a" }

            // ===== День рождения =====
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14
                spacing: 5
                Label {
                    visible: !profileDialog.editMode
                    Layout.fillWidth: true
                    text: (root.activeAccount && root.activeAccount.birthday.length > 0) ? root.activeAccount.birthday : "—"
                    color: theme.text; font.pixelSize: 15
                }
                Rectangle {
                    visible: profileDialog.editMode
                    Layout.fillWidth: true; Layout.preferredHeight: 44; radius: 10; color: theme.field
                    TextField {
                        id: profileBirthdayField
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Например, 9 окт 2012"; color: theme.text; placeholderTextColor: theme.subtext
                        font.pixelSize: 15; background: Rectangle { color: "transparent" }
                    }
                }
                Label { Layout.fillWidth: true; text: "День рождения"; color: theme.subtext; font.pixelSize: 13 }
            }
            Rectangle {
                visible: !profileDialog.editMode
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14; Layout.preferredHeight: 1; color: "#26262a"
            }

            // ===== Идентификатор (реальные данные из ядра) =====
            ColumnLayout {
                visible: !profileDialog.editMode
                Layout.fillWidth: true
                Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 14
                spacing: 5
                Label {
                    Layout.fillWidth: true
                    text: backend.ready ? backend.fingerprint : "Ключ не создан"
                    color: theme.text; font.pixelSize: 15; font.family: "Consolas"
                }
                Label {
                    visible: backend.ready
                    Layout.fillWidth: true
                    text: backend.nodeId
                    color: theme.subtext; font.pixelSize: 11; font.family: "Consolas"
                    wrapMode: Text.WrapAnywhere
                }
                Label { Layout.fillWidth: true; text: "Идентификатор (ключ устройства)"; color: theme.subtext; font.pixelSize: 13 }
            }

            Item { Layout.fillHeight: true }

            Label {
                visible: !profileDialog.editMode
                Layout.fillWidth: true; Layout.margins: 20
                horizontalAlignment: Text.AlignHCenter
                text: "Здесь будут показаны Ваши истории."
                color: theme.subtext; font.pixelSize: 13
            }
            RowLayout {
                visible: profileDialog.editMode
                Layout.fillWidth: true; Layout.margins: 18; spacing: 10
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 96; Layout.preferredHeight: 38; radius: 10
                    color: profCancelHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: profCancelHover }
                    Label { anchors.centerIn: parent; text: "Отмена"; color: theme.text; font.pixelSize: 14 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { profileDialog.sync(); profileDialog.editMode = false } }
                }
                Rectangle {
                    Layout.preferredWidth: 130; Layout.preferredHeight: 38; radius: 10
                    color: profSaveHover.hovered ? theme.accentHover : theme.accent
                    HoverHandler { id: profSaveHover }
                    Label { anchors.centerIn: parent; text: "Сохранить"; color: "#ffffff"; font.pixelSize: 14; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: profileDialog.save() }
                }
            }
        }
    }

    // ---------- Wallet (in development) ----------
    Popup {
        id: walletDialog
        modal: true; dim: true; width: 340; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                Layout.fillWidth: true; Layout.topMargin: 22; Layout.leftMargin: 22; Layout.rightMargin: 22
                text: "Кошелёк"; color: theme.text; font.pixelSize: 18; font.bold: true
            }
            Label {
                Layout.fillWidth: true; Layout.topMargin: 10; Layout.leftMargin: 22; Layout.rightMargin: 22
                text: "Раздел в разработке"; color: theme.subtext; font.pixelSize: 14; wrapMode: Text.Wrap
            }
            Rectangle {
                Layout.alignment: Qt.AlignRight
                Layout.margins: 18
                Layout.preferredWidth: 120; Layout.preferredHeight: 38; radius: 10
                color: walletOkHover.hovered ? theme.accentHover : theme.accent
                HoverHandler { id: walletOkHover }
                Label { anchors.centerIn: parent; text: "Понятно"; color: "#ffffff"; font.pixelSize: 14; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: walletDialog.close() }
            }
        }
    }

    // ---------- Create group / channel ----------
    Popup {
        id: createChatDialog
        property string mode: "group"
        modal: true; dim: true; width: 340; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        function openWith(m) { createChatDialog.mode = m; newChatField.text = ""; createChatDialog.open() }
        onOpened: newChatField.forceActiveFocus()
        function commit() {
            var nm = newChatField.text.trim()
            if (nm.length === 0)
                return
            var note = createChatDialog.mode === "channel" ? "Канал создан" : "Группа создана"
            root.createChat(nm, note)
            newChatField.text = ""
            createChatDialog.close()
        }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                Layout.fillWidth: true; Layout.topMargin: 20; Layout.leftMargin: 22; Layout.rightMargin: 22
                text: createChatDialog.mode === "channel" ? "Новый канал" : "Новая группа"
                color: theme.text; font.pixelSize: 18; font.bold: true
            }
            Label {
                Layout.fillWidth: true; Layout.topMargin: 6; Layout.leftMargin: 22; Layout.rightMargin: 22
                text: createChatDialog.mode === "channel" ? "Введите название канала" : "Введите название группы"
                color: theme.subtext; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            Rectangle {
                Layout.fillWidth: true; Layout.topMargin: 16; Layout.leftMargin: 22; Layout.rightMargin: 22
                Layout.preferredHeight: 44; radius: 12; color: theme.field
                TextField {
                    id: newChatField
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "Название"; color: theme.text; placeholderTextColor: theme.subtext
                    font.pixelSize: 15; background: Rectangle { color: "transparent" }
                    onAccepted: createChatDialog.commit()
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 18; spacing: 10
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 96; Layout.preferredHeight: 38; radius: 10
                    color: ccCancelHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: ccCancelHover }
                    Label { anchors.centerIn: parent; text: "Отмена"; color: theme.text; font.pixelSize: 14 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { newChatField.text = ""; createChatDialog.close() } }
                }
                Rectangle {
                    Layout.preferredWidth: 116; Layout.preferredHeight: 38; radius: 10
                    color: ccCreateHover.hovered ? theme.accentHover : theme.accent
                    HoverHandler { id: ccCreateHover }
                    Label { anchors.centerIn: parent; text: "Создать"; color: "#ffffff"; font.pixelSize: 14; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: createChatDialog.commit() }
                }
            }
        }
    }

    // ---------- Contacts ----------
    Popup {
        id: contactsDialog
        modal: true; dim: true; width: 380; height: 480; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        function addContact() {
            var nm = contactNameField.text.trim()
            if (nm.length === 0)
                return
            var hl = nm.toLowerCase().replace(/[^a-z0-9]+/g, "")
            contactsModel.append({ name: nm, handle: "@" + hl })
            contactNameField.text = ""
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 18; Layout.leftMargin: 22; Layout.rightMargin: 18
                Label { Layout.fillWidth: true; text: "Контакты"; color: theme.text; font.pixelSize: 18; font.bold: true }
                Rectangle {
                    Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 15
                    color: contactsCloseHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: contactsCloseHover }
                    Item {
                        anchors.centerIn: parent; width: 14; height: 14
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: 45; color: "#b3b3b9" }
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: -45; color: "#b3b3b9" }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: contactsDialog.close() }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 12; Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 10
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 42; radius: 12; color: theme.field
                    TextField {
                        id: contactNameField
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Имя контакта"; color: theme.text; placeholderTextColor: theme.subtext
                        font.pixelSize: 14; background: Rectangle { color: "transparent" }
                        onAccepted: contactsDialog.addContact()
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 46; Layout.preferredHeight: 42; radius: 12
                    color: contactAddHover.hovered ? theme.accentHover : theme.accent
                    HoverHandler { id: contactAddHover }
                    Glyph { anchors.centerIn: parent; kind: "plus"; glyphColor: "#ffffff" }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: contactsDialog.addContact() }
                }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.topMargin: 12; Layout.bottomMargin: 14
                clip: true
                model: contactsModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    required property int index
                    required property string name
                    required property string handle
                    width: ListView.view.width
                    height: 58
                    color: contactRowHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: contactRowHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.createChat(name, "Контакт"); contactsDialog.close() }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 22; anchors.rightMargin: 18
                        spacing: 14
                        Rectangle {
                            width: 38; height: 38; radius: 19; color: "#54545b"
                            Label { anchors.centerIn: parent; text: name.length > 0 ? name.charAt(0).toUpperCase() : "?"; color: "#ffffff"; font.pixelSize: 15; font.bold: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { Layout.fillWidth: true; text: name; color: theme.text; font.pixelSize: 15 }
                            Label { Layout.fillWidth: true; text: handle; color: theme.subtext; font.pixelSize: 12 }
                        }
                        Rectangle {
                            Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 15
                            color: contactDelHover.hovered ? theme.closeHover : "transparent"
                            HoverHandler { id: contactDelHover }
                            Item {
                                anchors.centerIn: parent; width: 12; height: 12
                                Rectangle { anchors.centerIn: parent; width: 13; height: 1.5; radius: 1; rotation: 45; color: contactDelHover.hovered ? "#ffffff" : "#8b8b91" }
                                Rectangle { anchors.centerIn: parent; width: 13; height: 1.5; radius: 1; rotation: -45; color: contactDelHover.hovered ? "#ffffff" : "#8b8b91" }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: contactsModel.remove(index) }
                        }
                    }
                }
            }
        }
    }

    // ---------- Calls ----------
    Popup {
        id: callsDialog
        modal: true; dim: true; width: 380; height: 460; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 18; Layout.leftMargin: 22; Layout.rightMargin: 18
                Label { Layout.fillWidth: true; text: "Звонки"; color: theme.text; font.pixelSize: 18; font.bold: true }
                Rectangle {
                    Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 15
                    color: callsCloseHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: callsCloseHover }
                    Item {
                        anchors.centerIn: parent; width: 14; height: 14
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: 45; color: "#b3b3b9" }
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: -45; color: "#b3b3b9" }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: callsDialog.close() }
                }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.topMargin: 10
                clip: true
                model: callsModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    required property string name
                    required property string kind
                    required property string time
                    width: ListView.view.width
                    height: 60
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 22; anchors.rightMargin: 22
                        spacing: 14
                        Rectangle {
                            width: 40; height: 40; radius: 20; color: "#47474d"
                            Label { anchors.centerIn: parent; text: name.length > 0 ? name.charAt(0).toUpperCase() : "?"; color: "#ffffff"; font.pixelSize: 16; font.bold: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { Layout.fillWidth: true; text: name; color: theme.text; font.pixelSize: 15 }
                            Label { Layout.fillWidth: true; text: kind; color: theme.subtext; font.pixelSize: 12 }
                        }
                        Label { text: time; color: theme.subtext; font.pixelSize: 12 }
                    }
                }
            }
            Label {
                visible: callsModel.count === 0
                Layout.fillWidth: true; Layout.topMargin: 30; Layout.bottomMargin: 10
                horizontalAlignment: Text.AlignHCenter
                text: "Нет звонков"; color: theme.subtext; font.pixelSize: 14
            }
            Rectangle {
                Layout.fillWidth: true; Layout.margins: 18; Layout.preferredHeight: 40; radius: 10
                color: callClearHover.hovered ? theme.rowHover : theme.field
                HoverHandler { id: callClearHover }
                Label { anchors.centerIn: parent; text: "Очистить историю"; color: theme.text; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: callsModel.clear() }
            }
        }
    }

    // ---------- Settings ----------
    Popup {
        id: settingsDialog
        modal: true; dim: true; width: 380; padding: 0
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d1d20"; radius: 16; border.color: "#2c2c31"; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 18; Layout.leftMargin: 22; Layout.rightMargin: 18
                Label { Layout.fillWidth: true; text: "Настройки"; color: theme.text; font.pixelSize: 18; font.bold: true }
                Rectangle {
                    Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 15
                    color: settingsCloseHover.hovered ? theme.rowHover : "transparent"
                    HoverHandler { id: settingsCloseHover }
                    Item {
                        anchors.centerIn: parent; width: 14; height: 14
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: 45; color: "#b3b3b9" }
                        Rectangle { anchors.centerIn: parent; width: 15; height: 1.6; radius: 1; rotation: -45; color: "#b3b3b9" }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: settingsDialog.close() }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 12; Layout.preferredHeight: 1; color: "#26262a" }
            SettingRow {
                label: "Уведомления"
                active: root.notificationsEnabled
                onToggled: root.notificationsEnabled = !root.notificationsEnabled
            }
            SettingRow {
                label: "Звук"
                active: root.soundEnabled
                onToggled: root.soundEnabled = !root.soundEnabled
            }
            SettingRow {
                label: "Скрывать текст уведомлений"
                active: root.hidePreview
                onToggled: root.hidePreview = !root.hidePreview
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#26262a" }
            Label {
                Layout.fillWidth: true; Layout.margins: 22
                text: "Тема: чёрно-серая"; color: theme.subtext; font.pixelSize: 13
            }
        }
    }

    // ---------- Main two-pane layout ----------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ===== Left pane: chat list =====
        Item {
            Layout.preferredWidth: 340
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: theme.sidebarBg
                topLeftRadius: 12
                bottomLeftRadius: 12
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56

                    MouseArea {
                        anchors.fill: parent
                        onPressed: root.startSystemMove()
                        onDoubleClicked: root.toggleMaximized()
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 20
                            color: burgerHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: burgerHover }
                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                Repeater {
                                    model: 3
                                    delegate: Rectangle { width: 20; height: 2; radius: 1; color: theme.text }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: menuDrawer.open()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "AeroMesh"
                            color: theme.text
                            font.pixelSize: 20
                            font.bold: true
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 6
                        anchors.bottomMargin: 6
                        radius: 19
                        color: theme.field
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 8
                            Canvas {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = "#8b8b91"
                                    ctx.lineWidth = 1.6
                                    ctx.beginPath()
                                    ctx.arc(6, 6, 4.5, 0, 2 * Math.PI)
                                    ctx.stroke()
                                    ctx.beginPath()
                                    ctx.moveTo(9.5, 9.5)
                                    ctx.lineTo(14, 14)
                                    ctx.stroke()
                                }
                            }
                            TextField {
                                Layout.fillWidth: true
                                placeholderText: "\u041f\u043e\u0438\u0441\u043a"
                                color: theme.text
                                placeholderTextColor: theme.subtext
                                font.pixelSize: 14
                                background: Rectangle { color: "transparent" }
                            }
                        }
                    }
                }

                ListView {
                    id: chatList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: chatsModel
                    currentIndex: 0
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: ChatListItem { width: chatList.width }
                    onCurrentIndexChanged: root.loadMessages(currentIndex)
                    ScrollBar.vertical: ScrollBar { }
                }
            }

            Rectangle {
                width: 54; height: 54; radius: 27
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 18
                anchors.bottomMargin: 18
                color: newHover.hovered ? theme.accentHover : theme.accent
                HoverHandler { id: newHover }
                Rectangle { anchors.centerIn: parent; width: 22; height: 2.6; radius: 1; color: "#ffffff" }
                Rectangle { anchors.centerIn: parent; width: 2.6; height: 22; radius: 1; color: "#ffffff" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
            }
        }

        // ===== Right pane: conversation =====
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: theme.chatBg
                topRightRadius: 12
                bottomRightRadius: 12
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: theme.headerBg
                    topRightRadius: 12

                    MouseArea {
                        anchors.fill: parent
                        onPressed: root.startSystemMove()
                        onDoubleClicked: root.toggleMaximized()
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 6
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 20
                            color: root.activeChat ? root.activeChat.avatarColor : theme.accent
                            Label {
                                anchors.centerIn: parent
                                text: root.activeChat ? root.activeChat.name.charAt(0).toUpperCase() : "?"
                                color: "#ffffff"
                                font.pixelSize: 17
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Label {
                                text: root.activeChat ? root.activeChat.name : ""
                                color: theme.text
                                font.pixelSize: 16
                                font.bold: true
                            }
                            Label {
                                text: "\u0432 \u0441\u0435\u0442\u0438"
                                color: theme.subtext
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: 19
                            color: hSearchHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: hSearchHover }
                            Canvas {
                                anchors.centerIn: parent
                                width: 18; height: 18
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = "#b3b3b9"
                                    ctx.lineWidth = 1.7
                                    ctx.beginPath()
                                    ctx.arc(7, 7, 5, 0, 2 * Math.PI)
                                    ctx.stroke()
                                    ctx.beginPath()
                                    ctx.moveTo(11, 11)
                                    ctx.lineTo(16, 16)
                                    ctx.stroke()
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                        }

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: 19
                            color: hMenuHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: hMenuHover }
                            Column {
                                anchors.centerIn: parent
                                spacing: 3
                                Repeater {
                                    model: 3
                                    delegate: Rectangle { width: 4; height: 4; radius: 2; color: "#b3b3b9" }
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 40
                            color: minHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: minHover }
                            Rectangle { anchors.centerIn: parent; width: 12; height: 1.6; radius: 1; color: "#b3b3b9" }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.showMinimized()
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 40
                            color: maxHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: maxHover }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 11; height: 11; radius: 1
                                color: "transparent"
                                border.color: "#b3b3b9"
                                border.width: 1.3
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleMaximized()
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 40
                            color: closeHover.hovered ? theme.closeHover : "transparent"
                            HoverHandler { id: closeHover }
                            Item {
                                anchors.centerIn: parent
                                width: 14; height: 14
                                Rectangle { anchors.centerIn: parent; width: 16; height: 1.6; radius: 1; rotation: 45; color: closeHover.hovered ? "#ffffff" : "#b3b3b9" }
                                Rectangle { anchors.centerIn: parent; width: 16; height: 1.6; radius: 1; rotation: -45; color: closeHover.hovered ? "#ffffff" : "#b3b3b9" }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.close()
                            }
                        }
                    }
                }

                ListView {
                    id: msgList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: messagesModel
                    spacing: 2
                    topMargin: 12
                    bottomMargin: 12
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: MessageBubble { width: msgList.width }
                    onCountChanged: positionViewAtEnd()
                    ScrollBar.vertical: ScrollBar { }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: theme.headerBg
                    bottomRightRadius: 12
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 20
                            color: attachHover.hovered ? theme.rowHover : "transparent"
                            HoverHandler { id: attachHover }
                            Rectangle { anchors.centerIn: parent; width: 18; height: 2.4; radius: 1; color: "#b3b3b9" }
                            Rectangle { anchors.centerIn: parent; width: 2.4; height: 18; radius: 1; color: "#b3b3b9" }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            radius: 21
                            color: theme.field
                            TextField {
                                id: msgInput
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                verticalAlignment: TextInput.AlignVCenter
                                placeholderText: "\u0421\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0435"
                                color: theme.text
                                placeholderTextColor: theme.subtext
                                font.pixelSize: 14
                                background: Rectangle { color: "transparent" }
                                onAccepted: root.sendMessage()
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 46
                            Layout.preferredHeight: 46
                            radius: 23
                            color: sendHover.hovered ? theme.accentHover : theme.accent
                            HoverHandler { id: sendHover }
                            Canvas {
                                anchors.centerIn: parent
                                width: 22; height: 22
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = "#ffffff"
                                    ctx.beginPath()
                                    ctx.moveTo(3, 4)
                                    ctx.lineTo(19, 11)
                                    ctx.lineTo(3, 18)
                                    ctx.lineTo(7, 11)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.sendMessage()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- Resize handles (frameless window) ----------
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: 10
        height: 10
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: 10
        height: 10
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.startSystemResize(Qt.RightEdge | Qt.TopEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 10
        height: 10
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 10
        height: 10
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }
}
