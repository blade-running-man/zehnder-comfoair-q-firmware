// Ukrainian translation of ../en/broker.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { broker as source } from "../en/broker.ts";

export const broker: Record<keyof typeof source, Message> = {
  "broker.tone.pending": "Очікування",
  "broker.tone.denied": "Відмовлено",
  "broker.tone.error": "Проблема",
  "broker.tone.offline": "Немає відповіді",
  "broker.bootGrace": "Пристрій працює менше п'яти хвилин, тож будинок, можливо, ще вмикається.",
  "broker.absent.headline": "Пристрій нічого не повідомив про брокер",
  "broker.absent.detail":
    "Ця відповідь зовсім не містить розділу про брокер, а це не те саме, що всі "
    + "лічильники на нулі.",
  "broker.off.headline": "MQTT вимкнено",
  "broker.off.detail":
    "Адресу брокера не збережено, тому Home Assistant не бачить цього пристрою. Вкажіть "
    + "брокер на сторінці «Налаштування» і збережіть.",
  "broker.unknownWord.headline": "Пристрій повідомив стан брокера, якого ця сторінка не знає",
  "broker.unknownWord.detail":
    "Документ стану повідомив link \"{link}\", failure \"{failure}\", subscriptions "
    + "\"{subscriptions}\". Одного з цих слів ця сторінка не знає: прошивка отримала нове "
    + "слово, а сторінка — ні.",
  "broker.subsRefused.headline": "Брокер не дозволяє цьому пристрою слухати",
  "broker.subsRefused.detail":
    "Показники публікуються, але команди з Home Assistant не надходитимуть: брокер "
    + "відхилив підписки на команди. Це ACL брокера, а не несправність пристрою.",
  "broker.subsPending.headline": "Очікування підтвердження від брокера",
  "broker.subsPending.detail": "З'єднано; підписки на команди ще не підтверджено.",
  "broker.online.headline": "З'єднано з брокером",
  "broker.online.detail":
    "Опубліковано показників: {published}, прийнято команд: {commands}. Перепідключень: "
    + "{reconnects}.",
  "broker.connecting.headline": "Під'єднання до брокера",
  "broker.connecting.detail": "Пристрій з'єднується з {host}. Зазвичай це займає секунду-дві.",
  "broker.retry": "Наступна спроба через {secs} с.",
  "broker.dns.headline": "Ім'я брокера не розпізнається",
  "broker.dns.detail":
    "Пристрій запитав у роутера {host} і не отримав відповіді. Перевірте написання або "
    + "введіть IP-адресу брокера.",
  "broker.tcp.headline": "За цією адресою ніщо не відповідає",
  "broker.tcp.detail":
    "{host}:{port} не прийняв з'єднання. Можливо, брокер вимкнено або порт неправильний.",
  "broker.tcp.retry": "Пристрій пробує далі — наступна спроба через {secs} с.",
  "broker.unavailable.headline": "Брокер ще не готовий",
  "broker.unavailable.detail":
    "Він відповів, що недоступний, — зазвичай це брокер, який ще запускається.",
  "broker.protocol.headline": "Брокер відхилив з'єднання",
  "broker.protocol.detail":
    "Він відповів «{reason}» (код {code}). Справа не в паролі: перевірте налаштування "
    + "самого брокера. Пристрій повторює спробу кожні п'ять хвилин.",
  "broker.badCredentials.headline": "Брокер відхилив ім'я користувача або пароль",
  "broker.badCredentials.detail":
    "Він відповів «{reason}» (код {code}). Виправте їх на сторінці «Налаштування» і "
    + "збережіть — пристрій одразу спробує знову, не чекаючи п'ятихвилинної паузи.",
  "broker.dropped.headline": "З'єднання з брокером постійно обривається",
  "broker.dropped.detail":
    "Він прийняв пристрій, а потім закрив з'єднання стільки разів: {disconnects}. Так "
    + "буває, коли інший пристрій використовує той самий client id.",
  "broker.identityConflict":
    "Перепідключень {reconnects} проти розривів {disconnects}: те, що вони ростуть разом, "
    + "— єдина ознака цього звідси, бо брокер не може повідомити про це інакше.",
  "broker.notDialled.headline": "Пристрій ще не з'єднувався з брокером",
  "broker.notDialled.detail":
    "Брокер збережено, але з'єднання ще не розпочато. У перші секунди після перезапуску "
    + "це нормально, а якщо триває — це несправність.",
  "broker.noReason.headline": "Пристрій не повідомив, чому брокер недоступний",
  "broker.noReason.detail":
    "Link — «{link}», а помилка — «none»; обидва не можуть бути правдою одночасно. Спроб "
    + "дотепер: {attempts}.",
  "broker.alert.noControl": "Home Assistant не може керувати цим пристроєм",
  "broker.alert.noData": "Home Assistant не отримує даних від цього пристрою",
  "broker.alert.tail": "{detail}. Лічильники брокера — у розділі «Діагностика».",
  "broker.card.title": "Home Assistant і брокер",
  "broker.card.saved": "Збережений брокер:",
  "broker.card.none": "немає",
  "broker.card.stale": "Числа нижче — останні, що надійшли, а не поточні.",
  "broker.counter.link": "Зв'язок",
  "broker.counter.subscriptions": "Підписки на команди",
  "broker.counter.published": "Опубліковано показників",
  "broker.counter.commands": "Прийнято команд",
  "broker.counter.rejected": "Відхилено команд",
  "broker.counter.reconnects": "Перепідключення / розриви",
  "broker.counter.attempts": "Спроб від останнього сталого сеансу",
  "broker.counter.connack": "Останній CONNACK",
  "broker.counter.errno": "Номер помилки сокета",
  "broker.counter.epoch": "Перебудов клієнта",
  "broker.move.offline.headline":
    "На {old} нічого не вдасться прибрати — пристрій до нього не під'єднаний",
  "broker.move.offline.line1":
    "Пристрій не може відкликати те, що опублікував на {old}, поки не має до нього "
    + "доступу. Картка пристрою і всі сутності ({count}) залишаться в кожному Home "
    + "Assistant, що читає цей брокер, з показниками на момент останнього зв'язку, — і "
    + "залишаться там після збереження.",
  "broker.move.offline.line2":
    "Збереження не заблоковано. Брокер, який не відповідає, не може завадити вам змінити "
    + "налаштування.",
  "broker.move.offline.line3":
    "Щоб прибрати їх вручну, коли брокер повернеться, виконайте це на машині, яка має до "
    + "нього доступ:",
  "broker.move.couldNotAsk":
    "Ця сторінка не змогла прочитати, чи під'єднаний пристрій до {old} саме зараз, тож "
    + "вважає, що так. Якщо ні, збереження просто змінить адресу й залишить усе на {old} "
    + "недоторканим.",
  "broker.move.connected": "Пристрій зараз під'єднаний до {old}.",
  "broker.move.confirm": "Видалити й зберегти",
  "broker.move.headline":
    "Збереження видалить цей пристрій і його сутності ({count}) з Home Assistant на {old}",
  "broker.move.line1":
    "Зміна адреси змусить його перед відходом відкликати все, що він там опублікував: "
    + "порожніх discovery-повідомлень — {count}, після чого Home Assistant видалить картку "
    + "пристрою і всі сутності ({count}) зі свого реєстру.",
  "broker.move.line2":
    "Їхні entity_id зникнуть разом із ними, і саме цього не можна скасувати. Панелі, "
    + "автоматизації, скрипти, шаблони й довгострокова статистика, що посилаються на ці "
    + "entity_id, перестануть працювати — без жодної помилки. Якщо потім знову спрямувати "
    + "пристрій на {old}, сутності з'являться знову, але не обов'язково з тими самими "
    + "entity_id і ніколи — з їхньою історією.",
  "broker.move.line3":
    "Так діє лише адреса брокера. Префікс топіків, назву пристрою і перемикач discovery "
    + "можна змінювати без цього — сутності, їхні entity_id та історія збережуться.",
};
