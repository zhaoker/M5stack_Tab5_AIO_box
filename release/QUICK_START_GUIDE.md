# M5Stack Tab5 AIO Box Quick Start Guide

[English](QUICK_START_GUIDE.md) | [简体中文](QUICK_START_GUIDE_zh-CN.md) | [繁體中文](QUICK_START_GUIDE_zh-TW.md)

This guide covers the everyday workflows for the English UI: first boot, SSH connection, WiFi, file management, OTA upgrade, screenshots, backup, and basic maintenance.

## 1. First Boot

1. Flash the full firmware from `flash-at-0x0/` to address `0x0`.
2. Insert a TF card if you want to use the file manager, screenshots, music playback, OTA package storage, or configuration backup.
3. Power on the device.
4. Select **English** on the language screen.
5. The device enters the SSH server list.

You can change the language later from **Settings > System > Language**. After changing language, the device shows a switching prompt, turns off the display, and reboots.

## 2. Home Screen

The home screen is the SSH server list.

Common entries:

| Area | Action |
|---|---|
| Server card | Tap to connect |
| Server card menu | Long-press to edit or delete |
| Add button | Add a new SSH server |
| WiFi status | Open WiFi manager |
| Battery/USB-C area | Open power details |
| IPv4 / IPv6 area | View network address information |
| Settings | Open system settings |

When adding a server, fill in the icon, name, host/IP, port, username, password, and optional command to run after connection.

## 3. WiFi Manager

Open WiFi manager from the WiFi status area.

You can:

1. View connection status, IPv4, and IPv6 information.
2. Enter SSID and password manually, then tap **Save & Connect**.
3. Select a network from the scanned WiFi list.
4. Connect to or delete saved networks.

For open networks, the password field can be left blank.

## 4. SSH Terminal

Tap a server card to enter the terminal. The terminal is designed to stay mostly full-screen and shows controls only when needed.

Touch hot zones:

| Area | Action |
|---|---|
| Top edge | Show the status bar |
| Left edge | Show or hide the control bar |
| Right edge | Show or hide the soft keyboard and control bar |
| Status bar close button | Disconnect SSH and return to the server list |

The control bar includes common terminal keys such as ESC, TAB, CTRL, ALT, HOME, END, PgUp, PgDn, arrow keys, and Enter.

## 5. Physical Keyboard

When a compatible Tab5 physical keyboard is detected, the soft keyboard can stay hidden and input goes directly through the physical keyboard.

Common keys:

| Key | Action |
|---|---|
| Letters, numbers, symbols | Send directly to the remote terminal |
| Enter | Send Enter |
| Tab | Send Tab |
| Esc | Send ESC |
| Backspace | Send Backspace/Delete |
| Arrow keys | Send terminal arrow keys |
| Ctrl + key | Send terminal control combinations, such as Ctrl+C |
| Alt + key | Send ESC-prefixed key sequences |

## 6. TF Card File Manager

Open the file manager from Settings or the status shortcut when the TF card is mounted.

Main operations:

1. Browse folders and files on the TF card.
2. Use **Select All**, **Copy**, **Cut**, **Paste**, and **Delete** from the toolbar.
3. Delete actions show a confirmation dialog before removing files.
4. Open supported images, text files, and music files.
5. Use the online file manager over WiFi for browser-based upload/download.

Screenshots are saved to `/ScreenShots` on the TF card.

## 7. Music Player

The file manager can open MP3 and FLAC files.

Supported behavior:

1. Play music from the current folder or favorites.
2. Add/remove favorites from the music list.
3. Continue playback in the background with the top music bar.
4. Display embedded cover artwork when available.

Audio decoding and image viewing are resource-sensitive. If an operation feels slow, stop playback before doing heavy file or image work.

## 8. OTA Upgrade

Open **Settings > Firmware Upgrade**.

You can:

1. Check for main firmware updates.
2. Check for UPLOAD firmware updates.
3. Download firmware packages to the TF card.
4. Install after the version and package checks pass.

Keep power stable during upgrade. Do not remove the TF card or power off the device while installing firmware.

## 9. Backup and Restore

Open **Settings > Configuration Backup**.

Backup options:

| Type | Use case |
|---|---|
| Encrypted backup | Recommended for portable backups and cross-device migration |
| Plain backup | Useful for local recovery when you understand the privacy risk |

SSH and WiFi credentials are sensitive. Keep backup files private.

## 10. Time, Language, and Screenshot Settings

Open **Settings > System**.

Useful settings:

1. **Language**: English, 简体中文, or 繁體中文.
2. **Timezone**: affects local time display, screenshot names, and NTP-adjusted local time.
3. **Top music bar**: adjust the width and left position of the background music bar.
4. **Three-finger screenshot**: enable or disable screenshot gesture.

## 11. Quick Reference

| Goal | Touch | Physical keyboard |
|---|---|---|
| Show status bar | Tap top edge | - |
| Show control bar | Tap left edge | - |
| Show soft keyboard | Tap right edge | Usually hidden when detected |
| Disconnect SSH | Tap close button on status bar | - |
| Send Ctrl+C | Tap CTRL then C | Ctrl + C |
| Send ESC | Tap ESC | Esc |
| Send Tab | Tap TAB | Tab |
| Move cursor/history | Tap arrow keys | Arrow keys |
