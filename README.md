<h1 align="center">Flipper Car DJ</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/LICENSE-MIT-5C9E31?style=for-the-badge" alt="License"></a>
  <a href="https://rebelcode.com"><img src="https://img.shields.io/badge/BUILT%20BY-REBELCODE-8A2BE2?style=for-the-badge" alt="Built by"></a>
</p>

<p align="center"><b>A Flipper Zero app that lets the back seat run the car's music from a paired iPhone, with house rules built in.</b></p>

## What it does

Car DJ pairs with an iPhone as a Bluetooth remote and reads the current song over Apple Media Service. It controls whatever the phone is playing, so with CarPlay the car follows along.

- OK: play or pause
- Left / Right: previous or next song, at most one skip every 5 seconds, with a countdown on screen
- Turns: the screen shows whose turn it is, and each skip hands the turn over
- Up / Down: volume, with Up capped at 4 steps above where the app started
- Back: hold to exit; a short press shows a hint

## Firmware

Car DJ uses the Apple Media Service client API (`furi_ble/ams_client.h`), which runs on the Full BLE stack. The API is proposed for both firmwares:

- Official: [flipperzero-firmware#4519](https://github.com/flipperdevices/flipperzero-firmware/pull/4519)
- Unleashed: [unleashed-firmware#1228](https://github.com/DarkFlippers/unleashed-firmware/pull/1228)

Build that firmware with the Full stack as described in the PR, then build the app against its SDK:

```
ufbt update --hw-target=f7 --local=path/to/flipper-z-f7-sdk-local.zip
cd car_dj
ufbt
```

Copy `dist/car_dj.fap` to `apps/Bluetooth` on the SD card.

## Pairing

Open Car DJ, then on the iPhone go to Settings > Bluetooth and pick "Control" followed by the Flipper's name. A phone already paired with the Remote app reconnects without pairing again.
