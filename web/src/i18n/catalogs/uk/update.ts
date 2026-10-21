// Ukrainian translation of ../en/update.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { update as source } from "../en/update.ts";

export const update: Record<keyof typeof source, Message> = {
  "update.heading": "Оновлення прошивки",
  "update.title": "Встановлення прошивки",
  "update.instructions": "Виберіть firmware.bin, зібраний для цього пристрою (.pio/build/<env>/firmware.bin). Пристрій перевірить файл, запише його у вільний слот і перезавантажиться.",
  "update.fileAria": "Файл прошивки .bin для встановлення",
  "update.file": "{name} · {kib} КіБ",
  "update.install": "Встановити",
  "update.uploading": "Завантаження… {percent} % ({sent} з {total} КіБ)",
  "update.uploadDone": "Завантаження завершено; пристрій встановлює образ.",
  "update.restarting": "Встановлення… пристрій перезавантажується.",
  "update.uncertain": "З'єднання обірвалося після того, як увесь файл було надіслано. Чекаємо, чи пристрій його встановив.",
  "update.waiting": "Очікування відповіді пристрою ({seconds} с).",
  "update.updated": "Оновлено: {before} → {after}",
  "update.backRunning": "Пристрій знову на зв'язку, працює {after}.",
  "update.confirmNote": "Нова прошивка підтверджує себе після 30 с справної роботи; якщо вона збоїть раніше, пристрій автоматично повернеться до попередньої версії.",
  "update.same": "Пристрій знову на зв'язку, але досі працює {version}. Можливо, оновлення відкотилося.",
  "update.systemLink": "Сторінка «Система» показує, який розділ зараз працює.",
  "update.openSystem": "Відкрити «Систему»",
  "update.gaveUp": "Пристрій не відповідає вже 90 с. Можливо, він ще запускається; перезавантажте цю сторінку за хвилину й перевірте сторінку «Система», щойно він відповість.",
  "update.noPassword": "Для оновлення прошивки потрібен пароль пристрою. Задайте його в налаштуваннях і поверніться сюди.",
  "update.noPasswordWhy": "Без пароля будь-хто у вашій мережі міг би встановити на цей пристрій власний код.",
  "update.openSettings": "Відкрити налаштування",
  "update.login": "Цей пристрій має пароль. Увійдіть, коли браузер попросить, і спробуйте знову.",
  "update.refused": "Пристрій відхилив оновлення: {detail}",
  "update.failed": "Помилка завантаження — з'єднання з пристроєм втрачено.",
  "update.toastDone": "Прошивку оновлено",
  "update.compare": "Встановлено: {installed} → Цей файл: {image}",
  "update.fileVersion": "Цей файл: {image}",
  "update.verdict.newer": "Це новіша версія",
  "update.verdict.same": "Це та сама версія (перевстановлення)",
  "update.verdict.older": "Це СТАРІША версія — прошивку пристрою буде понижено. Налаштування збережуться.",

  "update.detail.noLength": "Завантаження не повідомило розмір файлу; надішліть його з Content-Length.",
  "update.detail.tooSmall": "Файл замалий, щоб бути образом прошивки.",
  "update.detail.tooLarge": "Файл більший за слот оновлення.",
  "update.detail.notImage": "Це не образ прошивки; виберіть firmware.bin з .pio/build/<env>/, а не .elf чи стиснутий файл.",
  "update.detail.wrongChip": "Цю прошивку зібрано для іншого чипа.",
  "update.detail.truncated": "Завантаження зупинилося до того, як надійшов увесь файл; спробуйте знову.",
  "update.detail.verifyFailed": "Завантажений образ не пройшов перевірку і не був встановлений.",
  "update.detail.busy": "Інше оновлення вже завантажується.",
  "update.detail.flashError": "Не вдалося записати оновлення у флеш; поточна прошивка не змінилася.",
  "update.detail.noSlot": "На цьому пристрої немає розділу, куди можна встановити оновлення.",
  "update.detail.unconfirmed": "Поточну прошивку щойно оновлено, і вона ще перевіряється; спробуйте за хвилину.",
  "update.detail.wrongProject": "Цей файл — прошивка для іншого пристрою, а не для контролера ComfoAir Q.",

  // --- the USB fallback card ---
  "update.usb.title": "Через USB",
  "update.usb.body":
    "Коли браузер не може дістатися до пристрою, або для першого встановлення, його "
    + "прошивають через USB:",
  "update.usb.strap":
    "Утримання кнопки під час увімкнення живлення запускає завантажувач, а не скидання, — "
    + "цей пін є download strap. Для скидання кнопку утримують {after} запуску плати.",
  "update.usb.strapEm": "після",
};
