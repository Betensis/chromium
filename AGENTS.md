# Chromium hackathon — рабочее окружение (Всеволод + Артём)

Этот файл — единая инструкция для рабочего дерева `vsevolod`. `CLAUDE.md` и
`AGENTS.md` идентичны: их читают и люди, и агенты (Claude Code / Codex).
Файл лежит в самой ветке `hackathon/vsevolod`, поэтому одинаков на сервере и на
Mac и распространяется через `git pull`.

## Как мы сейчас работаем с Артёмом

- Работаем ВДВОЁМ в **одной общей ветке** — `hackathon/vsevolod`. Оба (и
  Всеволод, и Артём) коммитим именно в неё. Отдельной ветки `artem` в текущем
  процессе не используем — всё в `vsevolod`.
- **Всеволод** — локально на Mac, рабочее дерево `~/proj/chromium-hackathon/vsevolod/src`.
- **Артём** — на внешнем сервере, рабочее дерево `/root/hackathon/vsevolod/src`.
- Синхронизация — через GitHub `git@github.com:Betensis/chromium.git`:
  `git pull` перед работой, `git push origin hackathon/vsevolod` после.
  Коммитим и тянем ЧАСТО (ветка общая — конфликты ловим и решаем сразу, не
  копим до конца).
- База зафиксирована на `099cfa29f9`. Upstream Chromium НЕ обновляем и в процесс
  хакатона не тянем.

## Как подключиться к внешнему серверу (SSH)

- Сервер: `root@72.56.4.122` (Ubuntu, hostname `msk-1-vm-k8gt`), корень работы `/root/hackathon`.
- Добавь на своём Mac в `~/.ssh/config` алиас:

  ```
  Host chromium-hackathon
      HostName 72.56.4.122
      User root
      IdentityFile ~/.ssh/id_ed25519
  ```

- Подключение: `ssh chromium-hackathon`
- Проверка доступа: `ssh chromium-hackathon true`  (должно пройти молча, без ошибок)
- Артёму: пришли свой публичный ключ (`cat ~/.ssh/id_ed25519.pub`) — он добавляется
  в `/root/.ssh/authorized_keys` на сервере (один раз). Пароль/ключи наружу не
  публикуем, порт SSH наружу не открываем.

## Как пересобирать (Всеволод)

Сборки собирают **macOS arm64** Chromium (`Chromium 152.0.7943.0`).

### Локально на Mac (нативно)

```bash
cd ~/proj/chromium-hackathon
git -C vsevolod/src pull                                   # подтянуть общую ветку
HACKATHON_START_BUILD=YES ./build-local-workspace.sh vsevolod
```

- Артефакт: `~/proj/chromium-hackathon/vsevolod/src/out/mac-arm64/Chromium.app`.
- Инкрементальная пересборка после правки — десятки секунд / пара минут.
- ЛИМИТ: суммарный `-j` всех локальных сборок ≤ 9 (обеспечивается технически
  внутри `build-local-workspace.sh`; по умолчанию `-j9`, одна сборка за раз).
- Запуск: `open vsevolod/src/out/mac-arm64/Chromium.app` либо напрямую
  `vsevolod/src/out/mac-arm64/Chromium.app/Contents/MacOS/Chromium --user-data-dir=/tmp/vsevolod --no-first-run`.

### На сервере (Артём и/или Всеволод)

```bash
ssh chromium-hackathon
cd /root/hackathon/vsevolod/src && git pull
cd /root/hackathon && HACKATHON_START_BUILD=YES ./start-build.sh vsevolod
./build-status.sh
tail -f logs/build-vsevolod.log
```

- Первая полная сборка ≈ несколько часов; инкрементальные — минуты.
- Артефакт: `/root/hackathon/vsevolod/src/out/mac-arm64/Chromium.app` (arm64,
  запускается только на Mac — сервер это Linux).

### Забрать серверный бинарь на свой Mac

```bash
mkdir -p ~/chromium-vsevolod/mac-arm64
rsync -a --delete --exclude=obj \
  chromium-hackathon:/root/hackathon/vsevolod/src/out/mac-arm64/ ~/chromium-vsevolod/mac-arm64/
xattr -rc ~/chromium-vsevolod/mac-arm64/Chromium.app
codesign --force --deep --sign - ~/chromium-vsevolod/mac-arm64/Chromium.app   # обязательно после rsync
~/chromium-vsevolod/mac-arm64/Chromium.app/Contents/MacOS/Chromium --user-data-dir=/tmp/vsevolod --no-first-run
```

Component build: нужен весь `out/mac-arm64` (dylib'ы лежат рядом с `.app`), не одна `.app`.

## Правила (не нарушать)

- `base/src` — только integration/инфраструктура; там НЕ разрабатываем и НЕ собираем.
- `gclient sync` — только через `./sync-workspace.sh vsevolod` (сервер) или
  `./sync-local-workspace.sh vsevolod` (Mac), и только если менялся `DEPS`.
- Никаких `force-push`, переписывания общей истории, `git gc/prune` по общему
  checkout, удаления `base`/worktree, публичного открытия SSH, массовых `pkill/killall`.
- Серверную сборку останавливать только `systemctl stop hackathon-build-vsevolod`.
