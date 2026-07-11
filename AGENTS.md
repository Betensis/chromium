# Chromium hackathon — рабочее окружение (Всеволод + Артём)

**Это единый источник правды по процессу.** `CLAUDE.md` и `AGENTS.md`
идентичны (их читают люди и агенты Claude Code / Codex), лежат в самой ветке
`hackathon/vsevolod`, поэтому одинаковы на сервере и на Mac и распространяются
через `git`. Этот файл **отменяет** более старые описания с отдельными ветками
на разработчика / integration-PR в `WORKFLOW.md` и control-plane `AGENTS.md`.

## Как мы сейчас работаем с Артёмом

- Работаем ВДВОЁМ в **одной общей ветке** — `hackathon/vsevolod`. Оба (и
  Всеволод, и Артём) коммитим именно в неё. Отдельной ветки `artem` не используем.
- **Всеволод** — локально на Mac, дерево `~/proj/chromium-hackathon/vsevolod/src`.
- **Артём** — на внешнем сервере, дерево `/root/hackathon/vsevolod/src`.
- Синхронизация — только через GitHub `git@github.com:Betensis/chromium.git`.
- База зафиксирована на `099cfa29f9`. Upstream Chromium НЕ обновляем.

## Общая ветка без гонок (обязательный протокол)

Раз оба пишем в одну ветку с двух машин — соблюдаем строго, иначе потеряем коммиты:

- Один раз на каждой машине: `git config pull.rebase true` (никаких merge-коммитов от `git pull`).
- **Коммить и пушь маленькими частями и часто** — меньше конфликтов.
- Рабочий цикл:
  ```bash
  git fetch origin
  git rebase origin/hackathon/vsevolod      # свои НЕопубликованные коммиты поверх свежего remote
  # (собрать/проверить — см. ниже)
  git push origin hackathon/vsevolod
  ```
- Если `push` отклонён как non-fast-forward — значит второй успел запушить раньше:
  ```bash
  git fetch origin && git rebase origin/hackathon/vsevolod   # разрешить конфликты, если есть
  git push origin hackathon/vsevolod                          # повторить
  ```
- **Никогда** не `--force`/`--force-with-lease` по общей ветке, не переписывать уже
  опубликованные коммиты. Конфликт разрешает тот, кто пушит вторым.
- Кто закоммитил изменение `DEPS` — пишет об этом второму; второй перед сборкой
  делает `./sync-workspace.sh vsevolod` (сервер) / `./sync-local-workspace.sh vsevolod` (Mac).

## Подключение к внешнему серверу (SSH)

- Сервер — общий, вход по обычному (публичному) `:22` под пользователем `root`:
  `root@72.56.4.122` (Ubuntu, hostname `msk-1-vm-k8gt`), корень работы `/root/hackathon`.
- Алиас на своём Mac (`~/.ssh/config`):
  ```
  Host chromium-hackathon
      HostName 72.56.4.122
      User root
      IdentityFile ~/.ssh/id_ed25519
  ```
- Подключение: `ssh chromium-hackathon` ; проверка: `ssh chromium-hackathon true` (молча = ок).
- Доступ Артёма: он присылает свой публичный ключ (`cat ~/.ssh/id_ed25519.pub`), ключ
  добавляется в `/root/.ssh/authorized_keys` (с комментарием-владельцем для attribution;
  отзыв — удалением строки). Пароли/приватные ключи не публикуем.
- Отдельное правило (НЕ про сервер): user-space `sshd` на Mac Всеволода слушает только
  `127.0.0.1:2222`, reverse-туннель — server-local `127.0.0.1:2202`. Эти порты наружу
  НЕ открываем и password-auth не включаем. (Публичный `:22` самого сервера — это норма.)

## Как пересобирать (Всеволод)

Сборки — **macOS arm64** Chromium (`Chromium 152.0.7943.0`).
Перед любой сборкой: `git fetch && git rebase origin/hackathon/vsevolod`, рабочее дерево
чистое (`git status` пуст), собираешь именно актуальный remote HEAD.

### Локально на Mac (нативно)
```bash
cd ~/proj/chromium-hackathon
( cd vsevolod/src && git fetch origin && git rebase origin/hackathon/vsevolod )
HACKATHON_START_BUILD=YES ./build-local-workspace.sh vsevolod
```
- Артефакт: `~/proj/chromium-hackathon/vsevolod/src/out/mac-arm64/Chromium.app`.
- Инкрементально после правки — десятки секунд / минуты.
- ЛИМИТ: суммарный `-j` всех локальных сборок ≤ 9 — жёстко (нельзя поднять даже env'ом),
  обеспечивается внутри `build-local-workspace.sh`. По умолчанию `-j9`, одна сборка за раз.
- Запуск: `vsevolod/src/out/mac-arm64/Chromium.app/Contents/MacOS/Chromium --user-data-dir=/tmp/vsevolod --no-first-run`.

### На сервере (Артём и/или Всеволод)
```bash
ssh chromium-hackathon
cd /root/hackathon/vsevolod/src && git fetch origin && git rebase origin/hackathon/vsevolod
cd /root/hackathon && HACKATHON_START_BUILD=YES ./start-build.sh vsevolod
./build-status.sh ; tail -f logs/build-vsevolod.log
```
- Артефакт: `/root/hackathon/vsevolod/src/out/mac-arm64/Chromium.app` (arm64, запускается только на Mac).
- Остановка сборки только: `systemctl stop hackathon-build-vsevolod`.

### Забрать серверный бинарь на свой Mac (единая процедура)
Component build: нужен весь `out/mac-arm64` (dylib'ы рядом с `.app`). Копировать в
ЧИСТУЮ директорию (`--delete`), затем подписать и запустить:
```bash
DEST=~/chromium-vsevolod/mac-arm64
mkdir -p "$DEST"
rsync -a --delete --exclude=obj chromium-hackathon:/root/hackathon/vsevolod/src/out/mac-arm64/ "$DEST/"
xattr -rc "$DEST/Chromium.app"
codesign --force --deep --sign - "$DEST/Chromium.app"   # ad-hoc; вынужденный шаг после rsync (lld подписывает пофайлово, bundle seal ломается)
"$DEST/Chromium.app/Contents/MacOS/Chromium" --user-data-dir=/tmp/vsevolod --no-first-run
```

## Правила (не нарушать)

- `base/src` — только integration/инфраструктура; там НЕ разрабатываем и НЕ собираем.
- Сборку запускать ТОЛЬКО через wrapper (`build-local-workspace.sh` / `start-build.sh`) —
  прямой запуск `autoninja` обходит `-j9`-гвард и общий lock.
- `gclient sync` — только через `./sync-workspace.sh vsevolod` / `./sync-local-workspace.sh vsevolod`,
  и только если менялся `DEPS`.
- Никаких `force-push`, переписывания общей истории, `git gc/prune` по общему checkout,
  удаления `base`/worktree, публичного открытия Mac-портов, массовых `pkill/killall`.
- `out/` и `build/mac_files/xcode_binaries` в `.gitignore` — сборочный мусор в ветку не коммитим.
